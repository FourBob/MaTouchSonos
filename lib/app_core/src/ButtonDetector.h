#pragma once

#include <stdint.h>

namespace app {

enum class ButtonEvent : uint8_t {
    None,
    Short,  ///< Kurz gedrückt und losgelassen
    Long,   ///< Lange gehalten – wird bereits beim Erreichen der Haltezeit gemeldet
};

/**
 * Erkennt kurze und lange Tastendrücke inklusive Entprellung.
 *
 * - Ein Pegel muss `debounceMs` stabil sein, bevor er zählt.
 * - `Long` wird ausgelöst, sobald die Taste `longPressMs` gehalten wird (nicht erst
 *   beim Loslassen) – so reagiert die Oberfläche sofort. Das anschließende Loslassen
 *   erzeugt dann kein `Short` mehr.
 * - `Short` wird beim Loslassen gemeldet, wenn die Haltezeit kürzer war.
 *
 * Reine Logik: Aufrufer liefert Pegel und Zeit, daher auf dem PC testbar.
 */
class ButtonDetector {
public:
    explicit ButtonDetector(uint32_t debounceMs = 30, uint32_t longPressMs = 600)
        : debounceMs_(debounceMs), longPressMs_(longPressMs) {}

    /**
     * @param pressed Aktueller Rohpegel (true = gedrückt).
     * @param nowMs   Aktuelle Zeit in Millisekunden (z. B. millis()).
     */
    ButtonEvent update(bool pressed, uint32_t nowMs) {
        // Entprellen: Rohpegel muss eine Weile stabil bleiben.
        if (pressed != rawState_) {
            rawState_ = pressed;
            rawChangedAt_ = nowMs;
        }
        if (pressed != stableState_ && (nowMs - rawChangedAt_) >= debounceMs_) {
            stableState_ = pressed;
            if (pressed) {
                pressedAt_ = nowMs;
                longFired_ = false;
            } else if (!longFired_) {
                return ButtonEvent::Short;
            }
        }

        if (stableState_ && !longFired_ && (nowMs - pressedAt_) >= longPressMs_) {
            longFired_ = true;
            return ButtonEvent::Long;
        }
        return ButtonEvent::None;
    }

    bool isPressed() const { return stableState_; }

private:
    uint32_t debounceMs_;
    uint32_t longPressMs_;
    bool rawState_ = false;
    bool stableState_ = false;
    bool longFired_ = false;
    uint32_t rawChangedAt_ = 0;
    uint32_t pressedAt_ = 0;
};

}  // namespace app
