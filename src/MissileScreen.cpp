#include "MissileScreen.h"

#include <Arduino_GFX_Library.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "Display.h"

using app::game::MissileGame;

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

}  // namespace

void MissileScreen::enter(const MissileGame& game, uint32_t best) {
    state_ = -1;
    render(game, best);
}

void MissileScreen::circle(float x, float y, float r, int segments, uint16_t color) {
    float px = x + r, py = y;
    for (int k = 1; k <= segments; ++k) {
        const float a = 6.2831853f * k / segments;
        const float nx = x + cosf(a) * r, ny = y + sinf(a) * r;
        canvas_.line(px, py, nx, ny, color);
        px = nx;
        py = ny;
    }
}

void MissileScreen::drawReady(uint32_t best) {
    const uint16_t dim = rgb(0x9A, 0x9A, 0x9A);
    centeredText("RAKETENABWEHR", 120, 3, rgb(0xEB, 0x57, 0x57));
    char line[32];
    if (best > 0) {
        snprintf(line, sizeof(line), "REKORD %lu", static_cast<unsigned long>(best));
        centeredText(line, 166, 2, dim);
    }
    centeredText("SCHUETZE DIE STAEDTE!", 214, 2, dim);
    centeredText("ANTIPPEN: ABWEHRRAKETE", 238, 2, dim);
    centeredText("ODER RING ZIELEN,", 262, 2, dim);
    centeredText("DRUECKEN FEUERN", 286, 2, dim);
    centeredText("DRUECKEN: START", 346, 2, rgb(0xF2, 0xC9, 0x4C));
}

void MissileScreen::render(const MissileGame& game, uint32_t best) {
    ++frame_;
    const int state = static_cast<int>(game.state());
    const bool changed = state != state_;
    gfx()->startWrite();
    if (changed) {
        gfx()->fillScreen(BLACK);
        canvas_.clear();
        state_ = state;
        if (game.state() == MissileGame::State::Ready) drawReady(best);
    }
    if (game.state() == MissileGame::State::Ready) {
        gfx()->endWrite();
        return;
    }
    const uint16_t red = rgb(0xEB, 0x40, 0x40), blue = rgb(0x50, 0x90, 0xFF), cyan = rgb(0x30, 0xD0, 0xE0),
                   planet = rgb(0x30, 0x80, 0x40), yellow = rgb(0xF2, 0xC9, 0x4C), grey = rgb(0x60, 0x60, 0x60);
    canvas_.begin();

    // Planet mit Basis und Städten
    circle(kCx, kCy, MissileGame::kPlanetR, 20, planet);
    canvas_.line(kCx - 6, kCy + 4, kCx, kCy - 6, game.ammo() > 0 ? yellow : grey);  // Basis
    canvas_.line(kCx, kCy - 6, kCx + 6, kCy + 4, game.ammo() > 0 ? yellow : grey);
    for (int i = 0; i < MissileGame::kCities; ++i) {
        const float a = MissileGame::cityAngle(i) * 0.017453292f;
        const float ox = cosf(a), oy = sinf(a);   // nach außen
        const float tx = -oy, ty = ox;              // quer dazu
        const float x = kCx + MissileGame::cityX(i), y = kCy + MissileGame::cityY(i);
        if (game.city(i)) {
            // drei Häuser unterschiedlicher Höhe
            const float h[3] = {6, 10, 7};
            for (int k = 0; k < 3; ++k) {
                const float bx = x + tx * (k - 1) * 4.5f - ox * 4, by = y + ty * (k - 1) * 4.5f - oy * 4;
                canvas_.line(bx, by, bx + ox * h[k], by + oy * h[k], cyan);
                canvas_.line(bx + tx * 3, by + ty * 3, bx + tx * 3 + ox * h[k], by + ty * 3 + oy * h[k], cyan);
                canvas_.line(bx + ox * h[k], by + oy * h[k], bx + tx * 3 + ox * h[k], by + ty * 3 + oy * h[k], cyan);
            }
        } else {
            canvas_.line(x - tx * 6 - ox * 3, y - ty * 6 - oy * 3, x + tx * 6 - ox * 3, y + ty * 6 - oy * 3, grey);
        }
    }
    // feindliche Raketen: Spur vom Start bis zur Spitze
    for (int i = 0; i < MissileGame::kMaxEnemies; ++i) {
        const MissileGame::Enemy& e = game.enemy(i);
        if (!e.alive) continue;
        canvas_.line(kCx + e.sx, kCy + e.sy, kCx + e.x, kCy + e.y, red);
        canvas_.line(kCx + e.x - 1, kCy + e.y, kCx + e.x + 1, kCy + e.y, WHITE);
        canvas_.line(kCx + e.x, kCy + e.y - 1, kCx + e.x, kCy + e.y + 1, WHITE);
    }
    // Abwehrraketen mit Zielmarke
    for (int i = 0; i < MissileGame::kMaxCounters; ++i) {
        const MissileGame::Counter& c = game.counter(i);
        if (!c.alive) continue;
        const float r = MissileGame::kPlanetR;
        const float d = sqrtf(c.tx * c.tx + c.ty * c.ty);
        canvas_.line(kCx + c.tx / d * r, kCy + c.ty / d * r, kCx + c.x, kCy + c.y, blue);
        const float tx = kCx + c.tx, ty = kCy + c.ty;
        canvas_.line(tx - 3, ty - 3, tx + 3, ty + 3, blue);
        canvas_.line(tx - 3, ty + 3, tx + 3, ty - 3, blue);
    }
    // Explosionen: flackernde Kreise
    static const uint8_t kFlicker[4][3] = {{0xFF, 0xFF, 0xFF}, {0xF2, 0xC9, 0x4C}, {0xF2, 0x99, 0x4A}, {0xEB, 0x57, 0x57}};
    for (int i = 0; i < MissileGame::kMaxBlasts; ++i) {
        const MissileGame::Blast& b = game.blast(i);
        if (b.age < 0) continue;
        const float r = MissileGame::blastRadius(b.age);
        if (r < 1) continue;
        const uint8_t* c = kFlicker[(frame_ / 3 + i) % 4];
        const uint16_t color = rgb(c[0], c[1], c[2]);
        circle(kCx + b.x, kCy + b.y, r, 14, color);
        circle(kCx + b.x, kCy + b.y, r * 0.6f, 10, color);
    }
    // Fadenkreuz
    if (game.state() == MissileGame::State::Playing) {
        const float x = kCx + game.aimX(), y = kCy + game.aimY();
        canvas_.line(x - 7, y, x - 2, y, yellow);
        canvas_.line(x + 2, y, x + 7, y, yellow);
        canvas_.line(x, y - 7, x, y - 2, yellow);
        canvas_.line(x, y + 2, x, y + 7, yellow);
    }
    canvas_.finish();

    // Anzeigen: Punkte oben, Welle und Munition unten
    char line[32];
    snprintf(line, sizeof(line), " %lu ", static_cast<unsigned long>(game.score()));
    centeredText(line, 14, 2, WHITE);
    snprintf(line, sizeof(line), " W%d  %2d ", game.wave(), game.ammo());
    centeredText(line, 448, 2, game.ammo() > 5 ? rgb(0x9A, 0x9A, 0x9A) : red);
    if (game.state() == MissileGame::State::WaveEnd) {
        snprintf(line, sizeof(line), "WELLE %d GESCHAFFT", game.wave());
        centeredText(line, 130, 2, yellow);
        snprintf(line, sizeof(line), "BONUS %lu", static_cast<unsigned long>(game.lastBonus()));
        centeredText(line, 330, 2, WHITE);
    } else if (game.state() == MissileGame::State::GameOver) {
        centeredText("THE END", 130, 3, red);
    }
    gfx()->endWrite();
}
