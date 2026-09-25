#pragma once

#include <stdint.h>

namespace app {

/**
 * Spielposition eines Titels zwischen den Abfragen weiterzählen.
 *
 * Der Speaker wird nur alle 1,5 s gefragt; damit der Fortschrittsring trotzdem flüssig
 * läuft, wird lokal ab dem letzten bekannten Wert hochgezählt – solange gespielt wird.
 * Jede neue Antwort des Speakers korrigiert die Basis.
 *
 * Reine Logik, auf dem PC getestet. Zeiten in Millisekunden (millis()), Position in Sekunden.
 */
class ProgressTracker {
public:
    /** Neuer Wert vom Speaker. durationSec ≤ 0 = kein Fortschritt (Radio, TV, …). */
    void onRemote(int positionSec, int durationSec, bool playing, uint32_t nowMs) {
        durationSec_ = durationSec;
        known_ = positionSec >= 0 && durationSec > 0;
        baseMs_ = static_cast<int64_t>(positionSec < 0 ? 0 : positionSec) * 1000;
        baseAtMs_ = nowMs;
        playing_ = playing;
    }

    /** Wiedergabezustand geändert (z. B. Pause per Taste): Position einfrieren bzw. weiterlaufen lassen. */
    void setPlaying(bool playing, uint32_t nowMs) {
        if (playing == playing_) return;
        baseMs_ = currentMs(nowMs);
        baseAtMs_ = nowMs;
        playing_ = playing;
    }

    /** Lokal gesprungen (Scrub, ab Schritt 4): sofort anzeigen, der Speaker bestätigt später. */
    void jumpTo(int positionSec, uint32_t nowMs) {
        baseMs_ = static_cast<int64_t>(positionSec) * 1000;
        baseAtMs_ = nowMs;
    }

    bool known() const { return known_; }
    int durationSec() const { return durationSec_; }

    /** Aktuelle Position in Sekunden (0..Länge), −1 wenn unbekannt. */
    int positionSec(uint32_t nowMs) const {
        if (!known_) return -1;
        return static_cast<int>(currentMs(nowMs) / 1000);
    }

    /** Anteil 0..1000 für den Fortschrittsring, 0 wenn unbekannt. */
    int permille(uint32_t nowMs) const {
        if (!known_) return 0;
        return static_cast<int>(currentMs(nowMs) * 1000 / (static_cast<int64_t>(durationSec_) * 1000));
    }

    void reset() { known_ = false; }

private:
    int64_t currentMs(uint32_t nowMs) const {
        int64_t ms = baseMs_;
        if (playing_) ms += static_cast<uint32_t>(nowMs - baseAtMs_);
        const int64_t maxMs = static_cast<int64_t>(durationSec_ > 0 ? durationSec_ : 0) * 1000;
        if (ms > maxMs) ms = maxMs;
        if (ms < 0) ms = 0;
        return ms;
    }

    bool known_ = false;
    bool playing_ = false;
    int durationSec_ = 0;
    int64_t baseMs_ = 0;
    uint32_t baseAtMs_ = 0;
};

}  // namespace app
