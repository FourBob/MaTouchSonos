#pragma once

#include <stdint.h>

namespace app {

/**
 * Lautstärke-Logik zwischen Drehring, Anzeige und Speaker.
 *
 * - **Optimistisch:** Drehen ändert den angezeigten Wert sofort (`value()`).
 * - **Drossel:** An den Speaker geht höchstens alle `throttleMs` ein Wert – immer der
 *   jeweils neueste. Zwischenwerte beim schnellen Drehen werden übersprungen.
 * - **Beschleunigung:** Schnell aufeinanderfolgende Rastungen zählen mehrfach
 *   (1 → 2 → 4 Schritte), damit man zügig von leise nach laut kommt.
 * - **Konfliktregel:** Werte vom Speaker (z. B. Änderung in der Sonos-App) werden
 *   erst übernommen, wenn der Nutzer `holdoffMs` lang nicht gedreht hat und nichts
 *   mehr zu senden ist – sonst „kämpft“ die Anzeige gegen den Ring.
 *
 * Reine Logik, auf dem PC getestet. Zeitangaben in Millisekunden (millis()).
 */
class VolumeController {
public:
    struct Config {
        uint32_t throttleMs = 150;  ///< Mindestabstand zwischen zwei SetVolume
        uint32_t holdoffMs = 1000;  ///< Nach letzter Drehung: so lange Speakerwerte ignorieren
        uint32_t fastMs = 60;       ///< Rastungen schneller als das → Schrittweite 2
        uint32_t veryFastMs = 25;   ///< Rastungen schneller als das → Schrittweite 4
    };

    VolumeController() : VolumeController(Config{}) {}
    explicit VolumeController(const Config& cfg) : cfg_(cfg) {}

    /** true, sobald der Wert des Speakers einmal bekannt ist. Vorher wird Drehen ignoriert. */
    bool hasValue() const { return hasValue_; }

    /** Anzuzeigender Wert (0..100). */
    int value() const { return target_; }

    /**
     * Wert vom Speaker (GetVolume, später auch Events).
     * @return true, wenn der Wert übernommen wurde (Anzeige aktualisieren).
     */
    bool onRemoteVolume(int volume, uint32_t nowMs) {
        volume = clamp(volume);
        if (!hasValue_) {
            hasValue_ = true;
            target_ = sent_ = volume;
            return true;
        }
        const bool userIdle = !hadUserInput_ || (nowMs - lastUserInputMs_) >= cfg_.holdoffMs;
        if (userIdle && target_ == sent_) {
            const bool changed = target_ != volume;
            target_ = sent_ = volume;
            return changed;
        }
        return false;
    }

    /**
     * Rastungen vom Drehring.
     * @return true, wenn sich der angezeigte Wert geändert hat.
     */
    bool onUserDetents(int32_t detents, uint32_t nowMs) {
        if (!hasValue_ || detents == 0) return false;

        int step = 1;
        if (hadUserInput_) {
            const uint32_t dt = nowMs - lastUserInputMs_;
            if (dt < cfg_.veryFastMs) step = 4;
            else if (dt < cfg_.fastMs) step = 2;
        }
        hadUserInput_ = true;
        lastUserInputMs_ = nowMs;

        const int before = target_;
        target_ = clamp(target_ + static_cast<int>(detents) * step);
        return target_ != before;
    }

    /**
     * Nach jedem Schleifendurchlauf aufrufen.
     * @return true, wenn jetzt `outVolume` an den Speaker gesendet werden soll.
     */
    bool takeValueToSend(uint32_t nowMs, int& outVolume) {
        if (!hasValue_ || target_ == sent_) return false;
        if (hasSent_ && (nowMs - lastSendMs_) < cfg_.throttleMs) return false;
        hasSent_ = true;
        lastSendMs_ = nowMs;
        sent_ = target_;
        outVolume = target_;
        return true;
    }

    /** Verbindung verloren o. Ä.: Wert gilt als unbekannt, bis der Speaker wieder antwortet. */
    void invalidate() { hasValue_ = false; }

private:
    static int clamp(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }

    Config cfg_;
    bool hasValue_ = false;
    int target_ = 0;  ///< angezeigter/gewünschter Wert
    int sent_ = 0;    ///< zuletzt gesendeter bzw. vom Speaker bestätigter Wert
    bool hadUserInput_ = false;
    uint32_t lastUserInputMs_ = 0;
    bool hasSent_ = false;
    uint32_t lastSendMs_ = 0;
};

}  // namespace app
