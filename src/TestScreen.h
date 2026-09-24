#pragma once

#include <stdint.h>

/**
 * Hardware-Testbildschirm für Schritt 0.
 *
 * - Äußerer Bogen: folgt dem Drehring (eine Rastung = ein Segment)
 * - Mitte: Zähler, zuletzt erkannte Taste
 * - Punkt: folgt dem Finger
 * - Farbring blinkt: blau bei kurzem, orange bei langem Druck
 * - Farbbalken oben: Rot / Grün / Blau zur Prüfung der Farbreihenfolge
 */
class TestScreen {
public:
    void create();

    void onDetents(int32_t detents);
    void onShortPress();
    void onLongPress();

    /** Regelmäßig aufrufen, z. B. für das Zurücksetzen des Blink-Effekts. */
    void tick(uint32_t nowMs);

private:
    void flash(uint32_t color, uint32_t nowMs);
    void updateLabels();

    int32_t position_ = 0;
    const char* lastButton_ = "-";
    uint32_t flashUntil_ = 0;
};
