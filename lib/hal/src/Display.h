#pragma once

#include <stdint.h>

class Arduino_GFX;

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

    /** Helligkeit der Hintergrundbeleuchtung (0 = aus, 255 = voll), per PWM gedimmt. */
    static void setBacklight(uint8_t level);

    /**
     * Wurde das Display seit dem letzten Aufruf berührt? Der Touch wird in lv_timer_handler()
     * gelesen – also danach abfragen. Für das Energiesparen (jede Berührung zählt als Eingabe).
     */
    static bool takeTouchActivity();

    /**
     * Direkter Zugriff auf die Grafik (am LVGL vorbei) – für das Spiel, das mit 60 Bildern/s nur die
     * bewegten Teile neu zeichnet. Solange es zeichnet, darf lv_timer_handler() nicht laufen; danach
     * den Bildschirm mit lv_obj_invalidate() komplett neu zeichnen lassen.
     */
    static Arduino_GFX* gfx();
};

}  // namespace hal
