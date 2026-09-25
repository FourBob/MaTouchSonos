#pragma once

#include <stdint.h>

namespace app {

/**
 * Bedienmodi und was Drehring und Taste in welchem Modus bedeuten.
 *
 *   Normal ──lang──▶ Menü ──kurz auf „Spulen“──▶ Spulen
 *     ▲               │  ▲                          │
 *     └──lang/Timeout─┘  └─────────────────────────-┘ (kurz = springen, lang/Timeout = abbrechen,
 *                                                      beides zurück nach Normal)
 *
 * | Modus  | Ring drehen          | kurz drücken         | lang drücken |
 * |--------|----------------------|----------------------|--------------|
 * | Normal | Lautstärke           | Play/Pause           | Menü öffnen  |
 * | Menü   | Eintrag wählen       | Eintrag ausführen    | schließen    |
 * | Spulen | Zielposition ändern  | dorthin springen     | abbrechen    |
 *
 * Menü und Spulen schließen sich nach `timeoutMs` ohne Eingabe von selbst.
 * Die Klasse entscheidet nur – ausführen (Lautstärke senden, Seek, Anzeige) tut der Aufrufer
 * anhand der zurückgegebenen Action. Reine Logik, auf dem PC getestet.
 */
class ModeController {
public:
    enum class Mode : uint8_t { Normal, Menu, Scrub };

    /** Menüeinträge im Uhrzeigersinn, beginnend oben. */
    enum class MenuItem : uint8_t { Scrub, Rooms, Favorites, Close };
    static constexpr int kMenuItemCount = 4;

    struct Action {
        enum class Type : uint8_t {
            None,
            Volume,            ///< value = Rastungen (Normal-Modus)
            TogglePlayPause,
            MenuOpened,        ///< value = gewählter Eintrag
            MenuMoved,         ///< value = gewählter Eintrag
            MenuClosed,
            ScrubStarted,      ///< value = Zielposition [s]
            ScrubMoved,        ///< value = Zielposition [s]
            ScrubCommitted,    ///< value = Zielposition [s] → Seek senden
            ScrubCancelled,
            NotAvailable,      ///< value = MenuItem, das gerade nicht geht (z. B. Spulen bei Radio)
        };
        Type type = Type::None;
        int value = 0;
    };

    /** Was der aktuelle Titel zulässt – vom Aufrufer bei jedem Ereignis mitgegeben. */
    struct Context {
        bool canScrub = false;  ///< Titel mit bekannter Länge
        int positionSec = 0;
        int durationSec = 0;
    };

    explicit ModeController(uint32_t timeoutMs = 10000) : timeoutMs_(timeoutMs) {}

    Mode mode() const { return mode_; }
    int menuSelection() const { return selection_; }
    int scrubTarget() const { return scrubTarget_; }

    /** Ob ein Menüeintrag schon verfügbar ist (Räume/Favoriten folgen in Schritt 5/7). */
    static bool isEnabled(MenuItem item) { return item == MenuItem::Scrub || item == MenuItem::Close; }

    Action onLongPress(uint32_t nowMs) {
        touch(nowMs);
        switch (mode_) {
            case Mode::Normal:
                mode_ = Mode::Menu;
                selection_ = firstEnabled();
                return {Action::Type::MenuOpened, selection_};
            case Mode::Menu:
                mode_ = Mode::Normal;
                return {Action::Type::MenuClosed, 0};
            case Mode::Scrub:
                mode_ = Mode::Normal;
                return {Action::Type::ScrubCancelled, 0};
        }
        return {};
    }

    Action onShortPress(uint32_t nowMs, const Context& ctx) {
        touch(nowMs);
        switch (mode_) {
            case Mode::Normal:
                return {Action::Type::TogglePlayPause, 0};
            case Mode::Menu:
                return select(static_cast<MenuItem>(selection_), ctx);
            case Mode::Scrub:
                mode_ = Mode::Normal;
                return {Action::Type::ScrubCommitted, scrubTarget_};
        }
        return {};
    }

    Action onDetents(int32_t detents, uint32_t nowMs, const Context& ctx) {
        if (detents == 0) return {};
        const uint32_t sinceLast = nowMs - lastInputMs_;
        touch(nowMs);
        switch (mode_) {
            case Mode::Normal:
                return {Action::Type::Volume, static_cast<int>(detents)};
            case Mode::Menu:
                selection_ = moveSelection(selection_, detents);
                return {Action::Type::MenuMoved, selection_};
            case Mode::Scrub: {
                if (ctx.durationSec <= 0) return {};
                // 1 Rastung = 1 % des Titels, mindestens 5 s; schnell gedreht dreifach.
                int step = ctx.durationSec / 100;
                if (step < 5) step = 5;
                if (sinceLast < 60) step *= 3;
                scrubTarget_ += static_cast<int>(detents) * step;
                if (scrubTarget_ < 0) scrubTarget_ = 0;
                if (scrubTarget_ > ctx.durationSec) scrubTarget_ = ctx.durationSec;
                return {Action::Type::ScrubMoved, scrubTarget_};
            }
        }
        return {};
    }

    /** Regelmäßig aufrufen: schließt Menü bzw. Spulen nach Ablauf der Wartezeit. */
    Action tick(uint32_t nowMs) {
        if (mode_ == Mode::Normal || (nowMs - lastInputMs_) < timeoutMs_) return {};
        const Mode was = mode_;
        mode_ = Mode::Normal;
        return {was == Mode::Menu ? Action::Type::MenuClosed : Action::Type::ScrubCancelled, 0};
    }

private:
    void touch(uint32_t nowMs) { lastInputMs_ = nowMs; }

    static int firstEnabled() {
        for (int i = 0; i < kMenuItemCount; ++i) {
            if (isEnabled(static_cast<MenuItem>(i))) return i;
        }
        return 0;
    }

    /** Auswahl um `detents` Schritte weiterbewegen, deaktivierte Einträge überspringen, im Kreis. */
    static int moveSelection(int from, int32_t detents) {
        const int dir = detents > 0 ? 1 : -1;
        int steps = detents > 0 ? static_cast<int>(detents) : static_cast<int>(-detents);
        int i = from;
        while (steps-- > 0) {
            for (int guard = 0; guard < kMenuItemCount; ++guard) {
                i = (i + dir + kMenuItemCount) % kMenuItemCount;
                if (isEnabled(static_cast<MenuItem>(i))) break;
            }
        }
        return i;
    }

    Action select(MenuItem item, const Context& ctx) {
        switch (item) {
            case MenuItem::Scrub:
                if (!ctx.canScrub) {
                    mode_ = Mode::Normal;
                    return {Action::Type::NotAvailable, static_cast<int>(item)};
                }
                mode_ = Mode::Scrub;
                scrubTarget_ = ctx.positionSec < 0 ? 0 : ctx.positionSec;
                return {Action::Type::ScrubStarted, scrubTarget_};
            case MenuItem::Close:
                mode_ = Mode::Normal;
                return {Action::Type::MenuClosed, 0};
            case MenuItem::Rooms:
            case MenuItem::Favorites:
                return {Action::Type::NotAvailable, static_cast<int>(item)};
        }
        return {};
    }

    uint32_t timeoutMs_;
    Mode mode_ = Mode::Normal;
    int selection_ = 0;
    int scrubTarget_ = 0;
    uint32_t lastInputMs_ = 0;
};

}  // namespace app
