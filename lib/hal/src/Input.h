#pragma once

#include <stdint.h>

namespace hal {

/**
 * Drehring (Quadratur-Encoder) und Taste.
 *
 * Der Encoder wird vom **Hardware-Pulszähler (PCNT)** des ESP32-S3 gezählt: jede Flanke
 * beider Signale, mit Glitch-Filter gegen Kontaktprellen, unabhängig davon, wie
 * beschäftigt die CPU gerade ist (WLAN, Bildaufbau). Die Umrechnung in Rastungen
 * übernimmt app::DetentTracker in takeDetents().
 */
class Input {
public:
    static void begin();

    /**
     * Rastungen seit dem letzten Aufruf – in jedem Schleifendurchlauf aufrufen.
     * Positiv = im Uhrzeigersinn (Richtung über ENCODER_INVERT einstellbar).
     */
    static int32_t takeDetents();

    /** Rohschritte seit dem letzten Aufruf – nur zur Diagnose (z. B. Schritte pro Rastung). */
    static int32_t takeRawSteps();

    /** Aktueller, nicht entprellter Pegel der Taste (true = gedrückt). */
    static bool buttonRaw();
};

}  // namespace hal
