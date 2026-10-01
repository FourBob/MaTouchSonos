#pragma once

#include <stdint.h>

#include "TubeGame.h"
#include "VectorCanvas.h"

/**
 * Anzeige für „Röhrensturm“ (Easteregg): Blick in die Röhre als Vektorgrafik. Der Rand der Röhre
 * liegt am Bildschirmrand, die Tiefe in der Mitte; jede Ebene ist eine verkleinerte Kopie des Randes
 * (Perspektive). Beim Tauchen ins nächste Level fährt die Kamera durch die Röhre.
 * Fünf Formen: Kreis, Quadrat, Stern, Blume, Dreieck. Direkt über Arduino_GFX.
 */
class TubeScreen {
public:
    void enter(const app::game::TubeGame& game, uint32_t best);
    void render(const app::game::TubeGame& game, uint32_t best);

private:
    struct Pt {
        float x, y;
        bool ok;
    };
    void buildRim(int shape);
    Pt point(float edge, float z) const;  ///< Kante (Kommazahl) in Tiefe z → Bildschirm
    void drawTube(const app::game::TubeGame& game, uint16_t color, uint16_t laneColor);
    void drawEnemies(const app::game::TubeGame& game);
    void drawPlayer(const app::game::TubeGame& game);
    void drawShots(const app::game::TubeGame& game);
    void drawBlasts(const app::game::TubeGame& game);
    void drawLives(const app::game::TubeGame& game);
    void drawReady(uint32_t best);

    VectorCanvas canvas_;
    float rimX_[app::game::TubeGame::kLanes + 1] = {};
    float rimY_[app::game::TubeGame::kLanes + 1] = {};
    int shape_ = -1;
    float cam_ = 0;  ///< Kamerafahrt beim Tauchen: 0 = am Rand, 1 = ganz hinten
    int state_ = -1;
    int level_ = -1;
    uint32_t levelShownUntil_ = 0;
    uint32_t frame_ = 0;
    bool levelText_ = false;
};
