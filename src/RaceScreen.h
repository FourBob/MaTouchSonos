#pragma once

#include <stdint.h>

#include "RaceGame.h"

/**
 * Anzeige für „Boxenstopp“ (Easteregg). Zeichnet direkt über Arduino_GFX, am LVGL vorbei.
 *
 *   ┌──────── Himmel: Anzeigen (Runde, Zeit, km/h, Sprit, Reifen) ────────┐
 *   │            Berge (verschieben sich in Kurven)                        │
 *   ├──────────────────────── Horizont ────────────────────────────────────┤
 *   │   Straße: jedes Bild Zeile für Zeile neu (Gras, Randsteine, Fahrbahn, │
 *   │   Mittelstreifen), nur innerhalb des runden Bildschirms; Gegner       │
 *   │                       eigenes Auto unten                              │
 *   └──────────────────────────────────────────────────────────────────────┘
 *
 * Die Projektion ist die der klassischen Pseudo-3D-Rennspiele: Segment für Segment von vorn nach
 * hinten, Krümmung als aufsummierter Querversatz, jede Bildzeile wird genau einmal gezeichnet.
 * In der Box: Auto von oben, rundherum Reifen, Tank und LOS.
 */
class RaceScreen {
public:
    /** bestMs = Bestzeit fürs ganze Rennen, bestLapMs = schnellste Runde (0 = noch keine). */
    void enter(const app::game::RaceGame& race, uint32_t bestMs, uint32_t bestLapMs);
    void render(const app::game::RaceGame& race, uint32_t bestMs, uint32_t bestLapMs);

private:
    struct Projected {
        float x1, y1, w1;  // nahe Kante des Segments (Bildschirm)
        float x2, y2, w2;  // ferne Kante
        bool visible;
    };

    void drawSky();
    void drawMountains(int offset);
    void drawRoad(const app::game::RaceGame& race);
    void drawCars(const app::game::RaceGame& race);
    void drawPlayer(const app::game::RaceGame& race);
    void drawHud(const app::game::RaceGame& race, bool force);
    void drawCountdown(const app::game::RaceGame& race);
    void drawPitStatic(const app::game::RaceGame& race);
    void drawPitItems(const app::game::RaceGame& race);
    void drawPitGauges(const app::game::RaceGame& race);
    void drawFinish(const app::game::RaceGame& race, uint32_t bestMs, uint32_t bestLapMs);

    static constexpr int kDrawDistance = 100;
    Projected proj_[kDrawDistance] = {};
    float camX_ = 0;
    int baseSegment_ = 0;
    float offset_ = 0;

    int lastState_ = -1;
    float skyOffset_ = 0;
    int drawnSkyOffset_ = -100000;
    uint32_t frame_ = 0;
    int lastCountdown_ = -1;
    int lastPitSel_ = -1;
    float lastPitWear_[4] = {-1, -1, -1, -1};
    bool lastPitWorking_ = false;
    bool finishDrawn_ = false;
};
