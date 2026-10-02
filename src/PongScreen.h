#pragma once

#include <stdint.h>

#include "PongGame.h"
#include "VectorCanvas.h"

/**
 * Anzeige für „Ringpong“ (Easteregg): Schläger als Bögen am Rand, gestrichelte Mittellinie,
 * Spielstand groß in der Mitte (oben Computer, unten du). Vektorgrafik direkt über Arduino_GFX.
 */
class PongScreen {
public:
    void enter(const app::game::PongGame& game, uint32_t best);
    void render(const app::game::PongGame& game, uint32_t best);

private:
    void paddle(float angle, uint16_t color);
    void drawReady(uint32_t best);

    VectorCanvas& canvas_ = VectorCanvas::shared();
    int state_ = -1;
    uint32_t frame_ = 0;
};
