#pragma once

#include <math.h>
#include <stdint.h>

namespace app {
namespace game {

/**
 * „Ringbrecher“ – Breakout für das runde Display, als Easteregg.
 *
 * Der Schläger läuft außen am Rand entlang (Drehring = Spinner), die Steine liegen als Ringe um
 * die Mitte. Der Ball darf den Kreis nicht verlassen. Alle Steine weg → nächstes Level, schneller.
 *
 * Koordinaten: Mittelpunkt (0, 0), Pixel, y nach unten (wie der Bildschirm). Winkel in Grad,
 * 0° = rechts, im Uhrzeigersinn – wie Arduino_GFX::fillArc.
 * Reine Logik mit festem Zeitschritt (step() = 1/60 s), auf dem PC getestet.
 */
class RingBreakout {
public:
    // --- Spielfeld -----------------------------------------------------------------------------
    static constexpr float kArenaR = 240.0f;    // Bildschirmrand
    static constexpr float kPaddleR = 214.0f;   // Innenkante des Schlägers
    static constexpr float kPaddleThick = 10.0f;
    static constexpr float kBallR = 7.0f;
    static constexpr float kHubR = 40.0f;       // feste Mitte (zeigt Punkte und Leben)
    static constexpr int kRings = 4;
    static constexpr int kMaxSegments = 24;
    static constexpr float kRingInner0 = 60.0f; // innerster Steinring
    static constexpr float kRingWidth = 24.0f;
    static constexpr float kRingGap = 3.0f;
    static constexpr int kLives = 3;

    enum class State : uint8_t { Serving, Playing, GameOver };

    RingBreakout() { reset(); }

    /** Neues Spiel. */
    void reset() {
        score_ = 0;
        lives_ = kLives;
        level_ = 1;
        paddleAngle_ = 90.0f;  // unten
        buildBricks();
        serve();
    }

    /** Schläger um `deltaDeg` drehen (Drehring). */
    void movePaddle(float deltaDeg) { setPaddleAngle(paddleAngle_ + deltaDeg); }

    /** Schläger an einen Winkel setzen (Touch). */
    void setPaddleAngle(float deg) {
        paddleAngle_ = normalize(deg);
        if (state_ == State::Serving) placeBallOnPaddle();
    }

    /** Taste: Ball abschießen bzw. nach „Game over“ neu starten. */
    void press() {
        if (state_ == State::GameOver) {
            reset();
        } else if (state_ == State::Serving) {
            // Richtung Mitte, leicht schräg – sonst pendelt der Ball nur durch die Mitte
            const float a = (paddleAngle_ + 180.0f + 12.0f) * kDegToRad;
            vx_ = cosf(a) * speed_;
            vy_ = sinf(a) * speed_;
            state_ = State::Playing;
        }
    }

    /** Einen Zeitschritt (1/60 s) weiterrechnen. */
    void step() {
        if (state_ != State::Playing) return;
        // Zwei Teilschritte: Bei bis zu ~7 px pro Bild kann der Ball keinen Stein überspringen.
        for (int i = 0; i < 2 && state_ == State::Playing; ++i) subStep(0.5f);
    }

    // --- Abfragen für die Anzeige ---------------------------------------------------------------
    State state() const { return state_; }
    float ballX() const { return bx_; }
    float ballY() const { return by_; }
    float paddleAngle() const { return paddleAngle_; }
    float paddleWidth() const { return paddleWidth_; }
    int score() const { return score_; }
    int lives() const { return lives_; }
    int level() const { return level_; }
    static int segments(int ring) { return 10 + 4 * ring; }  // innen 10, außen 22
    static float ringInner(int ring) { return kRingInner0 + ring * (kRingWidth + kRingGap); }
    static float ringOuter(int ring) { return ringInner(ring) + kRingWidth; }
    bool brick(int ring, int seg) const { return bricks_[ring][seg]; }
    int bricksLeft() const { return bricksLeft_; }

    // --- für Tests --------------------------------------------------------------------------------
    void setBall(float x, float y, float vx, float vy) {
        bx_ = x; by_ = y; vx_ = vx; vy_ = vy;
        state_ = State::Playing;
    }
    float ballVX() const { return vx_; }
    float ballVY() const { return vy_; }
    void clearBricks() {
        for (auto& ring : bricks_) for (auto& b : ring) b = false;
        bricksLeft_ = 0;
    }
    void setBrick(int ring, int seg, bool alive) {
        if (bricks_[ring][seg] != alive) bricksLeft_ += alive ? 1 : -1;
        bricks_[ring][seg] = alive;
    }

    /** Winkel auf [0, 360) bringen. */
    static float normalize(float deg) {
        deg = fmodf(deg, 360.0f);
        return deg < 0 ? deg + 360.0f : deg;
    }
    /** Kleinster Winkelabstand a − b in [−180, 180). */
    static float angleDiff(float a, float b) {
        float d = fmodf(a - b + 540.0f, 360.0f) - 180.0f;
        return d;
    }

private:
    static constexpr float kDegToRad = 0.017453292f;
    static constexpr float kRadToDeg = 57.29578f;

    void buildBricks() {
        bricksLeft_ = 0;
        for (int r = 0; r < kRings; ++r)
            for (int s = 0; s < kMaxSegments; ++s) {
                bricks_[r][s] = s < segments(r);
                if (bricks_[r][s]) ++bricksLeft_;
            }
        speed_ = 3.4f + 0.45f * (level_ - 1);
        if (speed_ > 6.0f) speed_ = 6.0f;
        paddleWidth_ = level_ >= 3 ? 30.0f : 36.0f;
    }

    void serve() {
        state_ = State::Serving;
        vx_ = vy_ = 0;
        placeBallOnPaddle();
    }

    void placeBallOnPaddle() {
        const float r = kPaddleR - kBallR - 2.0f;
        const float a = paddleAngle_ * kDegToRad;
        bx_ = cosf(a) * r;
        by_ = sinf(a) * r;
    }

    void subStep(float f) {
        const float prevR = sqrtf(bx_ * bx_ + by_ * by_);
        bx_ += vx_ * f;
        by_ += vy_ * f;
        const float r = sqrtf(bx_ * bx_ + by_ * by_);
        if (r < 0.001f) return;
        const float nx = bx_ / r, ny = by_ / r;  // nach außen
        const float vr = vx_ * nx + vy_ * ny;    // Radialanteil (> 0 = nach außen)

        // Mitte: wie eine Wand
        if (r < kHubR + kBallR && vr < 0) {
            reflect(nx, ny);
            bx_ = nx * (kHubR + kBallR);
            by_ = ny * (kHubR + kBallR);
            return;
        }

        // Steine
        const float angle = normalize(atan2f(by_, bx_) * kRadToDeg);
        for (int ring = 0; ring < kRings; ++ring) {
            const float in = ringInner(ring), out = ringOuter(ring);
            if (r < in - kBallR || r > out + kBallR) continue;
            const int seg = static_cast<int>(angle / (360.0f / segments(ring))) % segments(ring);
            if (!bricks_[ring][seg]) continue;
            bricks_[ring][seg] = false;
            --bricksLeft_;
            score_ += (kRings - ring) * 10;  // innen mehr Punkte (schwerer zu treffen)
            const bool cameFromOutsideBand = prevR < in || prevR > out;
            if (cameFromOutsideBand) {
                reflect(nx, ny);              // von innen/außen: radial abprallen
            } else {
                reflect(-ny, nx);             // seitlich: tangential abprallen
            }
            if (bricksLeft_ == 0) nextLevel();
            return;
        }

        // Schläger
        if (r + kBallR >= kPaddleR && prevR + kBallR < kPaddleR + 1.0f && vr > 0) {
            const float diff = angleDiff(angle, paddleAngle_);
            const float margin = kBallR / kPaddleR * kRadToDeg;
            if (fabsf(diff) <= paddleWidth_ / 2 + margin) {
                // Abprallwinkel je nach Trefferpunkt (wie beim Original): Mitte = gerade nach innen
                float t = diff / (paddleWidth_ / 2);
                if (t > 1) t = 1;
                if (t < -1) t = -1;
                speed_ = fminf(speed_ * 1.02f, 7.0f);
                const float out = (angle + 180.0f - t * 55.0f) * kDegToRad;
                vx_ = cosf(out) * speed_;
                vy_ = sinf(out) * speed_;
                bx_ = nx * (kPaddleR - kBallR);
                by_ = ny * (kPaddleR - kBallR);
                return;
            }
        }

        // Daneben: Ball hat den Kreis verlassen
        if (r - kBallR > kArenaR) {
            --lives_;
            if (lives_ <= 0) {
                lives_ = 0;
                state_ = State::GameOver;
            } else {
                serve();
            }
        }
    }

    void nextLevel() {
        ++level_;
        buildBricks();
        serve();
    }

    /** Geschwindigkeit an der Achse (nx, ny) spiegeln. */
    void reflect(float nx, float ny) {
        const float d = vx_ * nx + vy_ * ny;
        vx_ -= 2 * d * nx;
        vy_ -= 2 * d * ny;
    }

    State state_ = State::Serving;
    float bx_ = 0, by_ = 0, vx_ = 0, vy_ = 0;
    float speed_ = 3.4f;
    float paddleAngle_ = 90.0f;
    float paddleWidth_ = 36.0f;
    bool bricks_[kRings][kMaxSegments] = {};
    int bricksLeft_ = 0;
    int score_ = 0;
    int lives_ = kLives;
    int level_ = 1;
};

}  // namespace game
}  // namespace app
