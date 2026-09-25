#pragma once

#include <stdint.h>

namespace hal {

/**
 * Drehring (Quadratur-Encoder) und Taste.
 *
 * Der Encoder wird per Interrupt auf beiden Signalen ausgewertet (RotaryDetentDecoder
 * aus app_core). Die Hauptschleife holt die seit dem letzten Aufruf abgeschlossenen
 * Rastungen mit takeDetents() ab. Die Tastenlogik (kurz/lang) liegt in app_core.
 */
class Input {
public:
    static void begin();

    /**
     * Rastungen seit dem letzten Aufruf.
     * Positiv = im Uhrzeigersinn (Richtung über ENCODER_INVERT einstellbar).
     */
    static int32_t takeDetents();

    /** Rohschritte seit dem letzten Aufruf – nur zur Diagnose (z. B. Schritte pro Rastung). */
    static int32_t takeRawSteps();

    /** Aktueller, nicht entprellter Pegel der Taste (true = gedrückt). */
    static bool buttonRaw();
};

}  // namespace hal
