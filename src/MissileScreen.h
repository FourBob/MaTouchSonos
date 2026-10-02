#pragma once

#include <stdint.h>

#include "MissileGame.h"
#include "VectorCanvas.h"

/**
 * Anzeige für „Raketenabwehr“ (Easteregg): Planet mit Städten in der Mitte, feindliche Raketen
 * als rote Spuren vom Rand, Abwehrraketen blau, Explosionen als flackernde Kreise, Fadenkreuz.
 * Vektorgrafik direkt über Arduino_GFX.
 */
class MissileScreen {
public:
    void enter(const app::game::MissileGame& game, uint32_t best);
    void render(const app::game::MissileGame& game, uint32_t best);

private:
    void circle(float x, float y, float r, int segments, uint16_t color);
    void drawReady(uint32_t best);

    VectorCanvas& canvas_ = VectorCanvas::shared();
    int state_ = -1;
    uint32_t frame_ = 0;
};
