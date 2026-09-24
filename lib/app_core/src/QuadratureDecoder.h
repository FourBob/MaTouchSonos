#pragma once

#include <stdint.h>

namespace app {

/**
 * Dekodiert die zwei Signale (A = CLK, B = DT) eines mechanischen Drehencoders.
 *
 * Arbeitet mit einer Übergangstabelle: Nur gültige Gray-Code-Übergänge zählen,
 * ungültige Sprünge (zwei Bits gleichzeitig, typisch bei Kontaktprellen) werden
 * ignoriert. Dadurch ist keine zeitbasierte Entprellung nötig.
 *
 * Reine Logik ohne Hardwarezugriff – wird aus der ISR mit den Pin-Pegeln gefüttert
 * und ist auf dem PC unit-testbar.
 */
class QuadratureDecoder {
public:
    explicit QuadratureDecoder(bool a = true, bool b = true) { reset(a, b); }

    /** Setzt den Startzustand, z. B. nach dem Einlesen der Pins beim Booten. */
    void reset(bool a, bool b) { state_ = encode(a, b); }

    /**
     * Verarbeitet neue Pegel.
     * @return +1 (ein Schritt im Uhrzeigersinn), -1 (gegen den Uhrzeigersinn) oder 0
     *         (keine Änderung bzw. ungültiger Übergang).
     */
    int8_t update(bool a, bool b) {
        const uint8_t next = encode(a, b);
        const int8_t delta = kTransition[(state_ << 2) | next];
        state_ = next;
        return delta;
    }

private:
    static uint8_t encode(bool a, bool b) { return static_cast<uint8_t>((a ? 2 : 0) | (b ? 1 : 0)); }

    // Index = (alter Zustand << 2) | neuer Zustand, Zustand = (A << 1) | B.
    // Gray-Folge im Uhrzeigersinn: 00 -> 10 -> 11 -> 01 -> 00  (A eilt B voraus)
    static constexpr int8_t kTransition[16] = {
        //        neu: 00  01  10  11
        /* 00 */       0, -1, +1,  0,
        /* 01 */      +1,  0,  0, -1,
        /* 10 */      -1,  0,  0, +1,
        /* 11 */       0, +1, -1,  0,
    };

    uint8_t state_ = 0;
};

}  // namespace app
