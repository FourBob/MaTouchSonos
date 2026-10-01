#include "GameScreen.h"

#include <Arduino_GFX_Library.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "Display.h"

using app::game::RingBreakout;

namespace {

constexpr int kCx = 240, kCy = 240;
constexpr float kBrickGapDeg = 1.6f;
constexpr int kMessageY = kCy + 178;   // Hinweiszeile unten (zwischen Steinen und Schläger)
constexpr int kMessageH = 20;

Arduino_GFX* gfx() { return hal::Display::gfx(); }

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return gfx()->color565(r, g, b); }

/** Farbe je Ring – innen warm, außen das Grün der Oberfläche. */
uint16_t ringColor(int ring) {
    switch (ring) {
        case 0: return rgb(0xEB, 0x57, 0x57);  // rot
        case 1: return rgb(0xF2, 0x99, 0x4A);  // orange
        case 2: return rgb(0xF2, 0xC9, 0x4C);  // gelb
        default: return rgb(0x1D, 0xB9, 0x54); // grün
    }
}

void centeredText(const char* text, int y, uint8_t size, uint16_t color) {
    gfx()->setTextSize(size);
    gfx()->setTextColor(color);
    const int w = static_cast<int>(strlen(text)) * 6 * size;
    gfx()->setCursor(kCx - w / 2, y);
    gfx()->print(text);
}

}  // namespace

void GameScreen::enter(const RingBreakout& game, int highscore) {
    gfx()->fillScreen(BLACK);
    for (int r = 0; r < RingBreakout::kRings; ++r)
        for (int s = 0; s < RingBreakout::kMaxSegments; ++s) bricks_[r][s] = false;
    ballX_ = ballY_ = -1000;
    paddle_ = -1;
    score_ = lives_ = level_ = highscore_ = state_ = -1;
    render(game, highscore);
}

void GameScreen::drawBrick(int ring, int seg, bool alive) {
    const float step = 360.0f / RingBreakout::segments(ring);
    gfx()->fillArc(kCx, kCy, static_cast<int16_t>(RingBreakout::ringOuter(ring)),
                   static_cast<int16_t>(RingBreakout::ringInner(ring)), seg * step + kBrickGapDeg / 2,
                   (seg + 1) * step - kBrickGapDeg / 2, alive ? ringColor(ring) : BLACK);
    bricks_[ring][seg] = alive;
}

void GameScreen::drawPaddleArc(float from, float to, bool visible) {
    if (to - from < 0.05f) return;
    gfx()->fillArc(kCx, kCy, static_cast<int16_t>(RingBreakout::kPaddleR + RingBreakout::kPaddleThick),
                   static_cast<int16_t>(RingBreakout::kPaddleR), RingBreakout::normalize(from),
                   RingBreakout::normalize(to), visible ? WHITE : BLACK);
}

void GameScreen::drawHub(const RingBreakout& game) {
    const int r = static_cast<int>(RingBreakout::kHubR) - 3;
    gfx()->fillCircle(kCx, kCy, r, rgb(0x1E, 0x1E, 0x1E));
    char text[12];
    snprintf(text, sizeof(text), "%d", game.score());
    centeredText(text, kCy - 14, strlen(text) > 4 ? 1 : 2, WHITE);
    // Leben als Punkte darunter
    for (int i = 0; i < game.lives(); ++i) {
        gfx()->fillCircle(kCx - (game.lives() - 1) * 6 + i * 12, kCy + 12, 3, rgb(0x1D, 0xB9, 0x54));
    }
}

void GameScreen::drawMessage(const RingBreakout& game, int highscore) {
    gfx()->fillRect(kCx - 120, kMessageY - 2, 240, kMessageH + 4, BLACK);
    char text[32];
    switch (game.state()) {
        case RingBreakout::State::Serving:
            if (game.level() > 1) snprintf(text, sizeof(text), "LEVEL %d - DRUECKEN", game.level());
            else snprintf(text, sizeof(text), "DRUECKEN: START");
            centeredText(text, kMessageY, 2, rgb(0x9A, 0x9A, 0x9A));
            break;
        case RingBreakout::State::GameOver:
            if (game.score() > 0 && game.score() >= highscore) snprintf(text, sizeof(text), "NEUER REKORD!");
            else snprintf(text, sizeof(text), "REKORD %d", highscore);
            centeredText("GAME OVER", kMessageY - 24, 2, rgb(0xEB, 0x57, 0x57));
            centeredText(text, kMessageY, 2, rgb(0x9A, 0x9A, 0x9A));
            break;
        case RingBreakout::State::Playing:
            gfx()->fillRect(kCx - 120, kMessageY - 26, 240, kMessageH + 28, BLACK);  // auch „GAME OVER“ weg
            break;
    }
}

void GameScreen::render(const RingBreakout& game, int highscore) {
    gfx()->startWrite();

    // Ball: alte Stelle löschen. Steht er im Steinbereich, die Steine darunter nachzeichnen.
    const int bx = kCx + static_cast<int>(lroundf(game.ballX()));
    const int by = kCy + static_cast<int>(lroundf(game.ballY()));
    const bool ballMoved = bx != ballX_ || by != ballY_;
    if (ballMoved && ballX_ > -1000) {
        const int br = static_cast<int>(RingBreakout::kBallR) + 1;
        gfx()->fillCircle(ballX_, ballY_, br, BLACK);
        const float dx = static_cast<float>(ballX_ - kCx), dy = static_cast<float>(ballY_ - kCy);
        const float r = sqrtf(dx * dx + dy * dy);
        if (r > RingBreakout::ringInner(0) - br && r < RingBreakout::ringOuter(RingBreakout::kRings - 1) + br) {
            const float angle = RingBreakout::normalize(atan2f(dy, dx) * 57.29578f);
            for (int ring = 0; ring < RingBreakout::kRings; ++ring) {
                const int n = RingBreakout::segments(ring);
                const int seg = static_cast<int>(angle / (360.0f / n)) % n;
                for (int d = -1; d <= 1; ++d) {
                    const int s = (seg + d + n) % n;
                    if (bricks_[ring][s] && game.brick(ring, s)) drawBrick(ring, s, true);
                }
            }
        }
    }

    // Steine: nur Änderungen (getroffen bzw. neues Level)
    for (int ring = 0; ring < RingBreakout::kRings; ++ring)
        for (int s = 0; s < RingBreakout::segments(ring); ++s)
            if (game.brick(ring, s) != bricks_[ring][s]) drawBrick(ring, s, game.brick(ring, s));

    // Schläger: bei kleinen Bewegungen nur den frei werdenden und den neuen Streifen zeichnen
    const float half = game.paddleWidth() / 2;
    const float now = game.paddleAngle();
    if (paddle_ < 0) {
        drawPaddleArc(now - half, now + half, true);
    } else if (now != paddle_) {
        const float delta = RingBreakout::angleDiff(now, paddle_);
        if (fabsf(delta) >= game.paddleWidth()) {
            drawPaddleArc(paddle_ - half, paddle_ + half, false);
            drawPaddleArc(now - half, now + half, true);
        } else if (delta > 0) {
            drawPaddleArc(paddle_ - half, now - half, false);
            drawPaddleArc(paddle_ + half, now + half, true);
        } else {
            drawPaddleArc(now + half, paddle_ + half, false);
            drawPaddleArc(now - half, paddle_ - half, true);
        }
    }
    paddle_ = now;

    // Ball neu
    if (ballMoved || ballX_ <= -1000) {
        gfx()->fillCircle(bx, by, static_cast<int>(RingBreakout::kBallR), WHITE);
        ballX_ = bx;
        ballY_ = by;
    }

    // Mitte und Hinweise nur bei Änderung
    if (game.score() != score_ || game.lives() != lives_) {
        drawHub(game);
        score_ = game.score();
        lives_ = game.lives();
    }
    const int state = static_cast<int>(game.state());
    if (state != state_ || game.level() != level_ || highscore != highscore_) {
        drawMessage(game, highscore);
        state_ = state;
        level_ = game.level();
        highscore_ = highscore;
    }

    gfx()->endWrite();
}
