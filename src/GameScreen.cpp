#include "GameScreen.h"

#include <Arduino_GFX_Library.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

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

// --- Schläger: eigene Kreisbogen-Füllung ---------------------------------------------------
// Arduino_GFX::fillArc teilt bei genau 0°/180° durch null und erzeugt dann Keile; zusammengesetzte
// Teilbögen treffen außerdem nicht exakt dieselben Pixel. Deshalb hier ein eigener, deterministischer
// Test je Pixel: Derselbe Test entscheidet beim Zeichnen und beim Löschen – es bleibt nichts stehen.

constexpr int kPaddleInner = static_cast<int>(RingBreakout::kPaddleR);
constexpr int kPaddleOuter = static_cast<int>(RingBreakout::kPaddleR + RingBreakout::kPaddleThick);

/** Kreisbogen um die Mitte: Richtung der Bogenmitte und cos² des halben Öffnungswinkels (< 90°). */
struct Arc {
    float mx = 0, my = 0, cos2 = 0;
    bool valid = false;
};

Arc makeArc(float centerDeg, float halfDeg) {
    Arc a;
    const float rad = centerDeg * 0.017453292f;
    a.mx = cosf(rad);
    a.my = sinf(rad);
    const float c = cosf(halfDeg * 0.017453292f);
    a.cos2 = c * c;
    a.valid = true;
    return a;
}

/** Liegt der Punkt (dx, dy) mit Abstand² r2 im Öffnungswinkel des Bogens? */
bool inArc(const Arc& a, float dx, float dy, float r2) {
    if (!a.valid) return false;
    const float d = dx * a.mx + dy * a.my;
    return d > 0 && d * d >= r2 * a.cos2;
}

struct Box {
    int x0, y0, x1, y1;
};

/** Umschließendes Rechteck eines Schläger-Bogens (abgetastet, mit Rand). */
Box arcBox(float centerDeg, float halfDeg) {
    Box b{kCx, kCy, kCx, kCy};
    bool first = true;
    for (float t = -halfDeg; ; t += 2.0f) {
        if (t > halfDeg) t = halfDeg;
        const float rad = (centerDeg + t) * 0.017453292f;
        for (int r : {kPaddleInner, kPaddleOuter}) {
            const int x = kCx + static_cast<int>(lroundf(r * cosf(rad)));
            const int y = kCy + static_cast<int>(lroundf(r * sinf(rad)));
            if (first) { b = {x, y, x, y}; first = false; }
            if (x < b.x0) b.x0 = x;
            if (x > b.x1) b.x1 = x;
            if (y < b.y0) b.y0 = y;
            if (y > b.y1) b.y1 = y;
        }
        if (t >= halfDeg) break;
    }
    b.x0 = b.x0 - 2 < 0 ? 0 : b.x0 - 2;
    b.y0 = b.y0 - 2 < 0 ? 0 : b.y0 - 2;
    b.x1 = b.x1 + 2 > 479 ? 479 : b.x1 + 2;
    b.y1 = b.y1 + 2 > 479 ? 479 : b.y1 + 2;
    return b;
}

/**
 * Im Rechteck jeden Pixel des Schläger-Rings prüfen: im Bogen `on` → weiß, sonst im Bogen `off` → schwarz,
 * sonst unverändert. Gleiche Farben werden zu waagerechten Linien zusammengefasst.
 */
void paintPaddleRegion(const Box& b, const Arc& on, const Arc& off) {
    const float ri2 = static_cast<float>(kPaddleInner * kPaddleInner);
    const float ro2 = static_cast<float>(kPaddleOuter * kPaddleOuter);
    for (int y = b.y0; y <= b.y1; ++y) {
        const float dy = y + 0.5f - kCy;
        int runStart = -1;
        uint16_t runColor = 0;
        for (int x = b.x0; x <= b.x1 + 1; ++x) {
            int color = -1;  // −1 = nicht anfassen
            if (x <= b.x1) {
                const float dx = x + 0.5f - kCx;
                const float r2 = dx * dx + dy * dy;
                if (r2 >= ri2 && r2 <= ro2) {
                    if (inArc(on, dx, dy, r2)) color = WHITE;
                    else if (inArc(off, dx, dy, r2)) color = BLACK;
                }
            }
            if (runStart >= 0 && (color != static_cast<int>(runColor))) {
                gfx()->writeFastHLine(runStart, y, x - runStart, runColor);
                runStart = -1;
            }
            if (color >= 0 && runStart < 0) {
                runStart = x;
                runColor = static_cast<uint16_t>(color);
            }
        }
    }
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

void GameScreen::paintPaddle(float oldAngle, float newAngle, float halfWidth, bool first) {
    const Arc now = makeArc(newAngle, halfWidth);
    if (first) {
        paintPaddleRegion(arcBox(newAngle, halfWidth), now, Arc{});
        return;
    }
    const float delta = RingBreakout::angleDiff(newAngle, oldAngle);
    const Arc before = makeArc(oldAngle, halfWidth);
    if (fabsf(delta) < 2 * halfWidth + 4) {
        // Alter und neuer Schläger überlappen bzw. liegen nah beieinander: ein gemeinsamer Bereich,
        // jeder Pixel wird genau einmal geschrieben (kein Flackern)
        const float mid = oldAngle + delta / 2;
        paintPaddleRegion(arcBox(mid, halfWidth + fabsf(delta) / 2 + 1), now, before);
    } else {
        paintPaddleRegion(arcBox(oldAngle, halfWidth), now, before);
        paintPaddleRegion(arcBox(newAngle, halfWidth), now, before);
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
        if (paddle_ >= 0 && r + br >= kPaddleInner - 1) {
            // Ball lag am Schläger: die angeschnittenen Pixel wieder weiß machen
            const Box box{ballX_ - br - 1, ballY_ - br - 1, ballX_ + br + 1, ballY_ + br + 1};
            paintPaddleRegion(box, makeArc(paddle_, game.paddleWidth() / 2), Arc{});
        }
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

    // Schläger (eigene Füllroutine, siehe paintPaddleRegion)
    const float half = game.paddleWidth() / 2;
    const float now = game.paddleAngle();
    if (paddle_ >= 0 && half != paddleHalf_) {
        // Schläger wurde schmaler (Level): alten vollständig löschen, neuen zeichnen
        paintPaddleRegion(arcBox(paddle_, paddleHalf_), Arc{}, makeArc(paddle_, paddleHalf_));
        paintPaddle(paddle_, now, half, true);
    } else if (paddle_ < 0) {
        paintPaddle(paddle_, now, half, true);
    } else if (now != paddle_) {
        paintPaddle(paddle_, now, half, false);
    }
    paddleHalf_ = half;
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
