#pragma once

#include <stdint.h>

namespace hal {

/**
 * Drehring (Quadratur-Encoder) und Taste.
 *
 * Der Encoder wird per Interrupt auf beiden Signalen ausgewertet; die Hauptschleife
 * holt die seit dem letzten Aufruf gezählten Rohschritte mit takeEncoderSteps() ab.
 * Die Umrechnung in Rastungen und die Tastenlogik (kurz/lang) liegen in app_core.
 */
class Input {
public:
    static void begin();

    /** Rohschritte seit dem letzten Aufruf (positiv = eine Richtung, negativ = andere). */
    static int32_t takeEncoderSteps();

    /** Aktueller, nicht entprellter Pegel der Taste (true = gedrückt). */
    static bool buttonRaw();
};

}  // namespace hal
