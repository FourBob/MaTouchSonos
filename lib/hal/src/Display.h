#pragma once

#include <stdint.h>

namespace hal {

/**
 * Rundes 480x480-Display (ST7701S über RGB565) inklusive LVGL-Anbindung.
 *
 * begin() erledigt alles in dieser Reihenfolge:
 *   1. ST7701 per 3-Wire-SPI initialisieren und RGB-Panel starten
 *   2. Hintergrundbeleuchtung einschalten
 *   3. LVGL initialisieren und als Display registrieren
 *   4. Touch (CST826) als LVGL-Eingabegerät registrieren
 *
 * Danach muss regelmäßig lv_timer_handler() aufgerufen werden (siehe main.cpp).
 * LVGL ist nicht threadsicher: Alle lv_*-Aufrufe aus derselben Task.
 */
class Display {
public:
    /** @return false, wenn Display oder Puffer nicht initialisiert werden konnten. */
    static bool begin();

    /** Helligkeit der Hintergrundbeleuchtung (0 = aus, 255 = voll). */
    static void setBacklight(uint8_t level);
};

}  // namespace hal
