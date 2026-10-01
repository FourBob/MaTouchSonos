#include "AsteroidsScreen.h"

#include <Arduino_GFX_Library.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "Display.h"

using app::game::AsteroidsGame;

namespace {

constexpr float kCx = 240, kCy = 240;

Arduino_GFX* gfx() { return hal::Display::gfx(); }
uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return gfx()->color565(r, g, b); }

void centeredText(const char* text, int y, uint8_t size, uint16_t fg) {
    gfx()->setTextSize(size);
    gfx()->setTextColor(fg, BLACK);
    const int w = static_cast<int>(strlen(text)) * 6 * size;
    gfx()->setCursor(static_cast<int>(kCx) - w / 2, y);
    gfx()->print(text);
}

/** Formvarianten der Felsen: Radius-Faktor je Ecke (10 Ecken). */
const float kShapes[4][10] = {
    {1.00f, 0.82f, 1.05f, 0.90f, 1.10f, 0.78f, 1.00f, 0.92f, 1.08f, 0.85f},
    {0.90f, 1.10f, 0.80f, 1.00f, 0.95f, 1.12f, 0.75f, 1.02f, 0.88f, 1.05f},
    {1.08f, 0.95f, 0.78f, 1.06f, 1.00f, 0.86f, 1.12f, 0.80f, 0.98f, 1.04f},
    {0.85f, 1.00f, 1.12f, 0.80f, 1.05f, 0.95f, 0.88f, 1.10f, 0.78f, 1.00f},
};

/** Schiffsumriss (Spitze oben), Ursprung = Schiffsmitte. */
const float kShip[5][2] = {{0, -13}, {9, 10}, {4, 6}, {-4, 6}, {-9, 10}};

void rotate(float x, float y, float deg, float& ox, float& oy) {
    const float r = deg * 0.017453292f, c = cosf(r), s = sinf(r);
    ox = x * c - y * s;
    oy = x * s + y * c;
}

}  // namespace

void AsteroidsScreen::enter(const AsteroidsGame& game, uint32_t best) {
    gfx()->startWrite();
    gfx()->fillScreen(BLACK);
    gfx()->endWrite();
    canvas_.clear();
    state_ = -1;
    render(game, best);
}

void AsteroidsScreen::drawText(const AsteroidsGame& game, uint32_t best, bool force) {
    const uint16_t white = WHITE, dim = rgb(0x9A, 0x9A, 0x9A);
    char line[32];
    if (game.state() == AsteroidsGame::State::Ready) {
        // jedes Bild: Felsen fliegen durch die Schrift
        centeredText("ASTEROIDEN", 150, 4, white);
        snprintf(line, sizeof(line), "REKORD %lu", static_cast<unsigned long>(best));
        if (best > 0) centeredText(line, 200, 2, dim);
        centeredText("RING: DREHEN", 256, 2, dim);
        centeredText("DRUECKEN: FEUER", 280, 2, dim);
        centeredText("BERUEHREN: SCHUB", 304, 2, dim);
        centeredText("DRUECKEN: START", 350, 2, rgb(0xF2, 0xC9, 0x4C));
        return;
    }
    // jedes Bild neu: Felsen, die durch die Zahl fliegen, reißen sonst Lücken hinein
    (void)force;
    snprintf(line, sizeof(line), " %lu ", static_cast<unsigned long>(game.score()));
    centeredText(line, 14, 2, white);
    if (game.state() == AsteroidsGame::State::GameOver) centeredText("GAME OVER", 226, 3, rgb(0xEB, 0x57, 0x57));
}

void AsteroidsScreen::render(const AsteroidsGame& game, uint32_t best) {
    ++frame_;
    const int state = static_cast<int>(game.state());
    const bool changed = state != state_;
    gfx()->startWrite();
    if (changed) {
        gfx()->fillScreen(BLACK);
        canvas_.clear();
        state_ = state;
    }
    const uint16_t white = WHITE, rockColor = rgb(0xC8, 0xD0, 0xDC), shotColor = rgb(0xF2, 0xC9, 0x4C);
    canvas_.begin();

    // Felsen
    for (int i = 0; i < AsteroidsGame::kMaxRocks; ++i) {
        const AsteroidsGame::Rock& r = game.rock(i);
        if (!r.alive) continue;
        const float rr = AsteroidsGame::rockRadius(r.size);
        float pts[20];
        for (int k = 0; k < 10; ++k) {
            const float a = (r.angle + k * 36.0f) * 0.017453292f;
            const float f = kShapes[r.shape % 4][k] * rr;
            pts[2 * k] = kCx + r.x + cosf(a) * f;
            pts[2 * k + 1] = kCy + r.y + sinf(a) * f;
        }
        canvas_.polyline(pts, 10, true, rockColor);
    }
    // Schüsse (kurze Kreuze, gut sichtbar)
    for (int i = 0; i < AsteroidsGame::kMaxShots; ++i) {
        const AsteroidsGame::Shot& s = game.shot(i);
        if (!s.alive) continue;
        const float x = kCx + s.x, y = kCy + s.y;
        canvas_.line(x - 2, y, x + 2, y, shotColor);
        canvas_.line(x, y - 2, x, y + 2, shotColor);
    }
    // Trümmer
    for (int i = 0; i < AsteroidsGame::kMaxDebris; ++i) {
        const AsteroidsGame::Debris& d = game.debris(i);
        if (d.life <= 0) continue;
        const float x = kCx + d.x, y = kCy + d.y;
        canvas_.line(x, y, x + d.vx * 0.02f, y + d.vy * 0.02f, d.life > 0.3f ? white : rgb(0x80, 0x80, 0x80));
    }
    // Schiff
    if (game.shipVisible()) {
        float pts[10];
        for (int k = 0; k < 5; ++k) {
            float ox, oy;
            rotate(kShip[k][0], kShip[k][1], game.shipAngle(), ox, oy);
            pts[2 * k] = kCx + game.shipX() + ox;
            pts[2 * k + 1] = kCy + game.shipY() + oy;
        }
        canvas_.polyline(pts, 5, true, white);
        if (game.thrusting() && (frame_ & 2)) {
            float ax, ay, bx, by, fx, fy;
            rotate(-3, 8, game.shipAngle(), ax, ay);
            rotate(3, 8, game.shipAngle(), bx, by);
            rotate(0, 17, game.shipAngle(), fx, fy);
            const float sx = kCx + game.shipX(), sy = kCy + game.shipY();
            const uint16_t flame = rgb(0xF2, 0x99, 0x4A);
            canvas_.line(sx + ax, sy + ay, sx + fx, sy + fy, flame);
            canvas_.line(sx + bx, sy + by, sx + fx, sy + fy, flame);
        }
    }
    // Leben als kleine Schiffe unten
    if (game.state() != AsteroidsGame::State::Ready) {
        const int n = game.lives() < 6 ? game.lives() : 6;
        for (int i = 0; i < n; ++i) {
            const float x = kCx - (n - 1) * 9 + i * 18, y = 452;
            canvas_.line(x, y - 7, x + 5, y + 6, white);
            canvas_.line(x + 5, y + 6, x - 5, y + 6, white);
            canvas_.line(x - 5, y + 6, x, y - 7, white);
        }
    }
    canvas_.finish();
    drawText(game, best, changed);
    gfx()->endWrite();
}
