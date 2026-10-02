#pragma once

#include <stdint.h>

#include "AsteroidsGame.h"
#include "VectorCanvas.h"

/**
 * Anzeige für „Asteroiden“ (Easteregg): Vektorgrafik wie das Original – Schiff, Felsen, Schüsse
 * und Trümmer als Linien, oben die Punkte, unten die Leben. Direkt über Arduino_GFX.
 */
class AsteroidsScreen {
public:
    void enter(const app::game::AsteroidsGame& game, uint32_t best);
    void render(const app::game::AsteroidsGame& game, uint32_t best);

private:
    void drawText(const app::game::AsteroidsGame& game, uint32_t best, bool force);

    VectorCanvas& canvas_ = VectorCanvas::shared();
    uint32_t frame_ = 0;
    int state_ = -1;
};
