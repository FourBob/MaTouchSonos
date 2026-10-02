#include "PongScreen.h"

#include <Arduino_GFX_Library.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "Display.h"

using app::game::PongGame;

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

float rad(float deg) { return deg * 0.017453292f; }

}  // namespace

void PongScreen::enter(const PongGame& game, uint32_t best) {
    state_ = -1;
    render(game, best);
}

void PongScreen::paddle(float angle, uint16_t color) {
    // drei Bögen nebeneinander = ein dicker Schläger
    constexpr int kSeg = 6;
    for (int ring = 0; ring < 3; ++ring) {
        const float r = PongGame::kPaddleR + 1 + ring * 3;
        float px = 0, py = 0;
        for (int k = 0; k <= kSeg; ++k) {
            const float a = rad(angle - PongGame::kPaddleHalf + 2 * PongGame::kPaddleHalf * k / kSeg);
            const float x = kCx + cosf(a) * r, y = kCy + sinf(a) * r;
            if (k > 0) canvas_.line(px, py, x, y, color);
            px = x;
            py = y;
        }
    }
}

void PongScreen::drawReady(uint32_t best) {
    const uint16_t dim = rgb(0x9A, 0x9A, 0x9A);
    centeredText("RINGPONG", 130, 4, WHITE);
    char line[32];
    if (best > 0) {
        snprintf(line, sizeof(line), "REKORD %lu", static_cast<unsigned long>(best));
        centeredText(line, 180, 2, dim);
    }
    centeredText("DU UNTEN, COMPUTER OBEN", 226, 2, dim);
    centeredText("RING: SCHLAEGER", 250, 2, dim);
    centeredText("7 PUNKTE GEWINNEN", 274, 2, dim);
    centeredText("DRUECKEN: START", 340, 2, rgb(0xF2, 0xC9, 0x4C));
}

void PongScreen::render(const PongGame& game, uint32_t best) {
    ++frame_;
    const int state = static_cast<int>(game.state());
    const bool changed = state != state_;
    gfx()->startWrite();
    if (changed) {
        gfx()->fillScreen(BLACK);
        canvas_.clear();
        state_ = state;
        if (game.state() == PongGame::State::Ready) drawReady(best);
    }
    if (game.state() == PongGame::State::Ready) {
        gfx()->endWrite();
        return;
    }
    const uint16_t green = rgb(0x1D, 0xB9, 0x54), red = rgb(0xEB, 0x57, 0x57), dim = rgb(0x50, 0x50, 0x50);
    canvas_.begin();
    // Mittellinie (gestrichelt) und die beiden Hälften des Randes (Tore)
    for (int x = -200; x < 200; x += 20) canvas_.line(kCx + x, kCy, kCx + x + 10, kCy, dim);
    for (int k = 0; k < 18; ++k) {
        const float a0 = rad(k * 10.0f), a1 = rad((k + 1) * 10.0f);
        const float r = 237;
        canvas_.line(kCx + cosf(a0) * r, kCy + sinf(a0) * r, kCx + cosf(a1) * r, kCy + sinf(a1) * r, green);
        canvas_.line(kCx + cosf(a0 + 3.14159265f) * r, kCy + sinf(a0 + 3.14159265f) * r,
                     kCx + cosf(a1 + 3.14159265f) * r, kCy + sinf(a1 + 3.14159265f) * r, red);
    }
    paddle(game.playerAngle(), WHITE);
    paddle(game.cpuAngle(), rgb(0xC8, 0xC8, 0xC8));
    // Ball (gefüllt aus waagerechten Linien)
    if (game.state() == PongGame::State::Playing || game.state() == PongGame::State::Serve) {
        const float bx = kCx + game.ballX(), by = kCy + game.ballY(), r = PongGame::kBallR;
        for (int dy = -static_cast<int>(r); dy <= static_cast<int>(r); dy += 2) {
            const float w = sqrtf(r * r - dy * dy);
            canvas_.line(bx - w, by + dy, bx + w, by + dy, rgb(0xF2, 0xC9, 0x4C));
        }
    }
    canvas_.finish();

    // Spielstand: Computer oben, du unten; Gesamtpunkte ganz oben
    char line[32];
    snprintf(line, sizeof(line), "%d", game.cpuPoints());
    centeredText(line, 192, 4, red);
    snprintf(line, sizeof(line), "%d", game.playerPoints());
    centeredText(line, 256, 4, green);
    snprintf(line, sizeof(line), " %lu ", static_cast<unsigned long>(game.score()));
    centeredText(line, 30, 2, WHITE);
    snprintf(line, sizeof(line), "LEVEL %d", game.level());
    centeredText(line, 440, 2, rgb(0x9A, 0x9A, 0x9A));
    switch (game.state()) {
        case PongGame::State::MatchWon: centeredText("MATCH GEWONNEN!", 120, 2, green); break;
        case PongGame::State::GameOver: centeredText("GAME OVER", 120, 3, red); break;
        case PongGame::State::Point:
            centeredText(game.lastPointPlayer() ? "PUNKT FUER DICH" : " PUNKT COMPUTER", 120, 2,
                         game.lastPointPlayer() ? green : red);
            break;
        default: centeredText("               ", 120, 2, WHITE); break;
    }
    gfx()->endWrite();
}
