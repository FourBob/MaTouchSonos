#pragma once

#include "RingBreakout.h"

/**
 * Anzeige für „Ringbrecher“ (Easteregg). Zeichnet direkt über Arduino_GFX in den Bildspeicher,
 * am LVGL vorbei: Pro Bild werden nur Ball, Schläger, getroffene Steine und geänderte Zahlen neu
 * gezeichnet – so sind 60 Bilder/s ohne Aufwand möglich.
 *
 *            ╭── Schläger (am Rand, Drehring) ──╮
 *          Steinringe ── Mitte: Punkte, Leben ── Steinringe
 *            ╰── unten: Hinweise (Start, Level, Rekord) ──╯
 */
class GameScreen {
public:
    /** Bildschirm komplett neu aufbauen (beim Start des Spiels). */
    void enter(const app::game::RingBreakout& game, int highscore);

    /** Nur die Änderungen seit dem letzten Aufruf zeichnen. */
    void render(const app::game::RingBreakout& game, int highscore);

private:
    void drawBrick(int ring, int seg, bool alive);
    void drawPaddleArc(float from, float to, bool visible);
    void drawHub(const app::game::RingBreakout& game);
    void drawMessage(const app::game::RingBreakout& game, int highscore);

    bool bricks_[app::game::RingBreakout::kRings][app::game::RingBreakout::kMaxSegments] = {};
    int ballX_ = -1000, ballY_ = -1000;
    float paddle_ = -1;
    int score_ = -1, lives_ = -1, level_ = -1, highscore_ = -1;
    int state_ = -1;
};
