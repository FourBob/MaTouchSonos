#pragma once

#include <stdint.h>

namespace app {

/** Wiedergabezustand aus Sicht der Bedienung (entspricht sonos::TransportState). */
enum class PlayState : uint8_t { Unknown, Stopped, Playing, Paused, Transitioning };

/** Befehl an den Speaker. */
enum class TransportCommand : uint8_t { Play, Pause };

/**
 * Play/Pause-Logik zwischen Taste, Anzeige und Speaker.
 *
 * - **Optimistisch:** Nach dem Tastendruck zeigt die Anzeige sofort den neuen Zustand.
 * - **Bestätigung:** Gepollte Zustände vom Speaker werden während `holdoffMs` nach einem
 *   Befehl nur übernommen, wenn sie den erwarteten Zustand bestätigen. Das verhindert,
 *   dass ein kurz danach noch veralteter oder „TRANSITIONING“-Zustand die Anzeige
 *   zurückspringen lässt.
 * - **Fehler:** Schlägt der Befehl fehl, springt die Anzeige auf den vorherigen Zustand.
 *
 * Reine Logik, auf dem PC getestet.
 */
class PlaybackController {
public:
    explicit PlaybackController(uint32_t holdoffMs = 2500) : holdoffMs_(holdoffMs) {}

    PlayState state() const { return state_; }

    /** Zustand vom Speaker (Polling). @return true, wenn sich die Anzeige ändert. */
    bool onRemoteState(PlayState s, uint32_t nowMs) {
        if (s == PlayState::Unknown) return false;
        if (pending_) {
            const bool inHoldoff = (nowMs - commandAtMs_) < holdoffMs_;
            if (s == expected_) {
                pending_ = false;  // bestätigt
            } else if (inHoldoff) {
                return false;      // noch nicht bestätigt: optimistische Anzeige behalten
            } else {
                pending_ = false;  // Speaker hat anders entschieden – ihm glauben
            }
        }
        const bool changed = s != state_;
        state_ = s;
        return changed;
    }

    /**
     * Tastendruck „Play/Pause“.
     * @return true, wenn `out` gesendet werden soll (false, solange der Zustand unbekannt ist).
     */
    bool toggle(uint32_t nowMs, TransportCommand& out) {
        if (state_ == PlayState::Unknown) return false;
        previous_ = state_;
        const bool playingNow = state_ == PlayState::Playing || state_ == PlayState::Transitioning;
        out = playingNow ? TransportCommand::Pause : TransportCommand::Play;
        state_ = expected_ = playingNow ? PlayState::Paused : PlayState::Playing;
        pending_ = true;
        commandAtMs_ = nowMs;
        return true;
    }

    /** Der Speaker hat den Befehl abgelehnt: Anzeige zurücksetzen. */
    void onCommandFailed() {
        if (!pending_) return;
        pending_ = false;
        state_ = previous_;
    }

    /** Verbindung weg o. Ä.: Zustand unbekannt, bis der Speaker wieder antwortet. */
    void invalidate() {
        state_ = PlayState::Unknown;
        pending_ = false;
    }

private:
    uint32_t holdoffMs_;
    PlayState state_ = PlayState::Unknown;
    PlayState previous_ = PlayState::Unknown;
    PlayState expected_ = PlayState::Unknown;
    bool pending_ = false;
    uint32_t commandAtMs_ = 0;
};

}  // namespace app
