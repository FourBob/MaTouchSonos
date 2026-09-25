#pragma once

#include <stdint.h>

namespace app {

/**
 * Bedienmodi und was Drehring und Taste in welchem Modus bedeuten.
 *
 *   Normal ──lang──▶ Menü ──kurz auf „Spulen“────▶ Spulen
 *     ▲               │    ├─kurz auf „Räume“─────▶ Raumwahl
 *     │               │    └─kurz auf „Favoriten“─▶ Favoritenwahl
 *     └──lang/Timeout─┘  Spulen/Raum/Favorit: kurz = übernehmen, lang/Timeout = abbrechen,
 *                        beides zurück nach Normal
 *
 * | Modus    | Ring drehen          | kurz drücken         | lang drücken |
 * |----------|----------------------|----------------------|--------------|
 * | Normal   | Lautstärke           | Play/Pause           | Menü öffnen  |
 * | Menü     | Eintrag wählen       | Eintrag ausführen    | schließen    |
 * | Spulen   | Zielposition ändern  | dorthin springen     | abbrechen    |
 * | Raum     | Raum wählen          | Raum übernehmen      | abbrechen    |
 * | Favorit  | Favorit wählen       | abspielen            | abbrechen    |
 *
 * Menü, Spulen und Auswahllisten schließen sich nach `timeoutMs` ohne Eingabe von selbst.
 * Die Klasse entscheidet nur – ausführen (Lautstärke senden, Seek, Anzeige) tut der Aufrufer
 * anhand der zurückgegebenen Action. Reine Logik, auf dem PC getestet.
 */
class ModeController {
public:
    enum class Mode : uint8_t { Normal, Menu, Scrub, RoomPicker, FavoritePicker };

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
            RoomPickerOpened,  ///< value = markierter Raum (Index)
            RoomPickerMoved,   ///< value = markierter Raum (Index)
            RoomSelected,      ///< value = gewählter Raum (Index) → umschalten
            RoomPickerCancelled,
            FavoritePickerOpened,     ///< value = markierter Favorit (Index)
            FavoritePickerMoved,      ///< value = markierter Favorit (Index)
            FavoriteSelected,         ///< value = gewählter Favorit (Index) → abspielen
            FavoritePickerCancelled,
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
        int roomCount = 0;     ///< Anzahl gefundener Räume/Gruppen
        int currentRoom = 0;   ///< Index des aktiven Raums
        int favoriteCount = 0; ///< Anzahl geladener Favoriten
    };

    explicit ModeController(uint32_t timeoutMs = 10000) : timeoutMs_(timeoutMs) {}

    Mode mode() const { return mode_; }
    int menuSelection() const { return selection_; }
    int scrubTarget() const { return scrubTarget_; }
    int roomPickerIndex() const { return pickerIndex_; }
    int favoritePickerIndex() const { return favoriteIndex_; }

    Action onLongPress(uint32_t nowMs) {
        touch(nowMs);
        switch (mode_) {
            case Mode::Normal:
                mode_ = Mode::Menu;
                selection_ = 0;
                return {Action::Type::MenuOpened, selection_};
            case Mode::Menu:
                mode_ = Mode::Normal;
                return {Action::Type::MenuClosed, 0};
            case Mode::Scrub:
                mode_ = Mode::Normal;
                return {Action::Type::ScrubCancelled, 0};
            case Mode::RoomPicker:
                mode_ = Mode::Normal;
                return {Action::Type::RoomPickerCancelled, 0};
            case Mode::FavoritePicker:
                mode_ = Mode::Normal;
                return {Action::Type::FavoritePickerCancelled, 0};
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
            case Mode::RoomPicker:
                mode_ = Mode::Normal;
                return {Action::Type::RoomSelected, pickerIndex_};
            case Mode::FavoritePicker:
                mode_ = Mode::Normal;
                return {Action::Type::FavoriteSelected, favoriteIndex_};
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
            case Mode::RoomPicker:
                if (!moveClamped(pickerIndex_, detents, ctx.roomCount)) return {};
                return {Action::Type::RoomPickerMoved, pickerIndex_};
            case Mode::FavoritePicker:
                if (!moveClamped(favoriteIndex_, detents, ctx.favoriteCount)) return {};
                return {Action::Type::FavoritePickerMoved, favoriteIndex_};
        }
        return {};
    }

    /** Regelmäßig aufrufen: schließt Menü bzw. Spulen nach Ablauf der Wartezeit. */
    Action tick(uint32_t nowMs) {
        if (mode_ == Mode::Normal || (nowMs - lastInputMs_) < timeoutMs_) return {};
        const Mode was = mode_;
        mode_ = Mode::Normal;
        switch (was) {
            case Mode::Menu: return {Action::Type::MenuClosed, 0};
            case Mode::Scrub: return {Action::Type::ScrubCancelled, 0};
            case Mode::RoomPicker: return {Action::Type::RoomPickerCancelled, 0};
            case Mode::FavoritePicker: return {Action::Type::FavoritePickerCancelled, 0};
            case Mode::Normal: break;
        }
        return {};
    }

private:
    void touch(uint32_t nowMs) { lastInputMs_ = nowMs; }

    /** Menüauswahl um `detents` Schritte weiterbewegen, im Kreis. */
    static int moveSelection(int from, int32_t detents) {
        const int step = static_cast<int>(detents % kMenuItemCount);
        return (from + step + kMenuItemCount) % kMenuItemCount;
    }

    /**
     * Listenauswahl verschieben. Kein Umlauf: An den Enden bleibt die Auswahl stehen –
     * so behält man die Orientierung. @return false, wenn sich nichts geändert hat.
     */
    static bool moveClamped(int& index, int32_t detents, int count) {
        int next = index + static_cast<int>(detents);
        if (next > count - 1) next = count - 1;
        if (next < 0) next = 0;
        if (next == index) return false;
        index = next;
        return true;
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
                if (ctx.roomCount <= 0) {
                    mode_ = Mode::Normal;
                    return {Action::Type::NotAvailable, static_cast<int>(item)};
                }
                mode_ = Mode::RoomPicker;
                pickerIndex_ = ctx.currentRoom;
                if (pickerIndex_ < 0 || pickerIndex_ >= ctx.roomCount) pickerIndex_ = 0;
                return {Action::Type::RoomPickerOpened, pickerIndex_};
            case MenuItem::Favorites:
                if (ctx.favoriteCount <= 0) {
                    mode_ = Mode::Normal;
                    return {Action::Type::NotAvailable, static_cast<int>(item)};
                }
                // Startet beim zuletzt gewählten Favoriten – oft will man denselben Sender wieder.
                mode_ = Mode::FavoritePicker;
                if (favoriteIndex_ >= ctx.favoriteCount) favoriteIndex_ = ctx.favoriteCount - 1;
                if (favoriteIndex_ < 0) favoriteIndex_ = 0;
                return {Action::Type::FavoritePickerOpened, favoriteIndex_};
        }
        return {};
    }

    uint32_t timeoutMs_;
    Mode mode_ = Mode::Normal;
    int selection_ = 0;
    int scrubTarget_ = 0;
    int pickerIndex_ = 0;
    int favoriteIndex_ = 0;
    uint32_t lastInputMs_ = 0;
};

}  // namespace app
