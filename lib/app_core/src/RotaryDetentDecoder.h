#pragma once

#include <stdint.h>

#include "QuadratureDecoder.h"

namespace app {

/**
 * Macht aus den Encoder-Signalen A/B ganze Rastungen („Klicks“).
 *
 * Prinzip: Die Rohschritte des QuadratureDecoder werden aufsummiert. Ausgewertet wird
 * erst, wenn der Encoder wieder in einer **Ruhelage** steht (dort, wo er mechanisch
 * einrastet). Dann gilt:
 *   Summe ≥ +Schwelle  →  +1 Rastung
 *   Summe ≤ −Schwelle  →  −1 Rastung
 * und die Summe wird auf 0 gesetzt.
 *
 * Weil in jeder Ruhelage neu synchronisiert wird, kann sich kein Versatz ansammeln –
 * auch dann nicht, wenn durch Prellen einzelne Zwischenschritte verloren gehen.
 * (Ein reiner Schrittzähler „alle 4 Schritte eine Rastung“ verschiebt sich dabei
 * dauerhaft um einen halben Klick; Hin-und-her-Drehen kommt dann nie an.)
 *
 * Encoder-Typen:
 *  - Vollschritt (Standard): 4 Zustandswechsel pro Rastung, Ruhelage A=B=1
 *    (Kontakte offen, Pull-ups). Schwelle 2 = mehr als die halbe Strecke.
 *  - Halbschritt: 2 Zustandswechsel pro Rastung, Ruhelagen A=B=1 und A=B=0.
 *    Schwelle 1.
 *
 * Reine Logik ohne Hardwarezugriff – auf dem PC unit-testbar.
 */
class RotaryDetentDecoder {
public:
    explicit RotaryDetentDecoder(bool halfStep = false) : halfStep_(halfStep) {}

    /** Startzustand setzen, z. B. mit den beim Booten gelesenen Pegeln. */
    void reset(bool a, bool b) {
        quad_.reset(a, b);
        acc_ = 0;
    }

    /**
     * Verarbeitet neue Pegel.
     * @return +1 / −1, wenn gerade eine Rastung abgeschlossen wurde, sonst 0.
     */
    int8_t update(bool a, bool b) {
        const int8_t step = quad_.update(a, b);
        acc_ = static_cast<int8_t>(acc_ + step);
        lastStep_ = step;

        if (!isRest(a, b)) return 0;

        const int8_t threshold = halfStep_ ? 1 : 2;
        int8_t detent = 0;
        if (acc_ >= threshold) detent = +1;
        else if (acc_ <= -threshold) detent = -1;
        acc_ = 0;  // in der Ruhelage immer neu synchronisieren
        return detent;
    }

    /** Letzter Rohschritt (−1/0/+1), nur für Diagnose-Logs. */
    int8_t lastStep() const { return lastStep_; }

private:
    bool isRest(bool a, bool b) const { return halfStep_ ? (a == b) : (a && b); }

    QuadratureDecoder quad_;
    bool halfStep_;
    int8_t acc_ = 0;
    int8_t lastStep_ = 0;
};

}  // namespace app
