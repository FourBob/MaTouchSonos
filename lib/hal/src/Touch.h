#pragma once

#include <stdint.h>

namespace hal {

/**
 * Kapazitiver Touch-Controller CST826 an I2C (Adresse 0x15).
 * Liefert den ersten Berührungspunkt in Display-Koordinaten (0..479).
 */
class Touch {
public:
    /** Startet I2C und prüft, ob der Controller antwortet. */
    static bool begin();

    /** @return true, wenn gerade ein Finger aufliegt; x/y enthalten dann die Position. */
    static bool read(int16_t& x, int16_t& y);
};

}  // namespace hal
