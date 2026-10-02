#pragma once

#include <math.h>
#include <stdint.h>

namespace app {
namespace game {

/**
 * „Ringpong“ – Pong im Kreis, als Easteregg. Du gegen den Computer.
 *
 * Beide Schläger laufen auf dem Rand: deiner auf der unteren Hälfte, der des Computers auf der
 * oberen. Verlässt der Ball den Kreis durch die untere Hälfte, punktet der Computer, durch die obere
 * du. Wo der Ball den Schläger trifft, bestimmt den Abprallwinkel; jeder Treffer macht ihn schneller.
 * Wer zuerst 7 Punkte hat, gewinnt das Match. Gewonnen → nächstes Level (der Computer wird
 * schneller und genauer), verloren → Spielende.
 *
 * Punkte: 100 je gewonnenem Ballwechsel, 10 je eigenem Treffer, 1000 × Level je gewonnenem Match.
 * Koordinaten: Mittelpunkt (0, 0), Pixel, y nach unten. Winkel in Grad, 0° = rechts, im
 * Uhrzeigersinn (90° = unten). Reine Logik, auf dem PC getestet.
 */
class PongGame {
public:
    static constexpr float kArenaR = 236.0f;
    static constexpr float kPaddleR = 214.0f;    // Innenkante der Schläger
    static constexpr float kPaddleThick = 8.0f;
    static constexpr float kPaddleHalf = 16.0f;  // halbe Schlägerbreite in Grad
    static constexpr float kBallR = 6.0f;
    static constexpr float kPlayerMin = 15.0f, kPlayerMax = 165.0f;   // untere Hälfte
    static constexpr float kCpuMin = 195.0f, kCpuMax = 345.0f;        // obere Hälfte
    static constexpr int kWinPoints = 7;

    enum class State : uint8_t { Ready, Serve, Playing, Point, MatchWon, GameOver };

    PongGame() { reset(); }

    void reset() {
        state_ = State::Ready;
        score_ = 0;
        level_ = 1;
        player_ = 90.0f;
        cpu_ = 270.0f;
        resetMatch();
    }

    /** Taste: Start (vom Titel). */
    void press() {
        if (state_ != State::Ready) return;
        score_ = 0;
        level_ = 1;
        resetMatch();
        beginServe(true);
    }

    /** Schläger um `deg` drehen (Drehring) bzw. auf einen Winkel setzen (Touch). */
    void movePaddle(float deg) {
        player_ += deg;  // Ring: am Anschlag stehen bleiben, nicht herumspringen
        if (player_ < kPlayerMin) player_ = kPlayerMin;
        if (player_ > kPlayerMax) player_ = kPlayerMax;
    }
    void setPaddle(float deg) {
        deg = normalize(deg);
        // außerhalb der unteren Hälfte: zum näheren Anschlag (links bis oben = links, sonst rechts)
        if (deg > kPlayerMax && deg <= 270.0f) deg = kPlayerMax;
        else if (deg > 270.0f || deg < kPlayerMin) deg = kPlayerMin;
        player_ = deg;
    }

    void step(float dt) {
        switch (state_) {
            case State::Ready:
            case State::GameOver:
                return;
            case State::Serve:
                timer_ -= dt;
                moveCpu(dt);
                if (timer_ <= 0) launch();
                return;
            case State::Point:
                timer_ -= dt;
                if (timer_ <= 0) afterPoint();
                return;
            case State::MatchWon:
                timer_ -= dt;
                if (timer_ <= 0) {
                    ++level_;
                    resetMatch();
                    beginServe(true);
                }
                return;
            case State::Playing:
                break;
        }
        moveCpu(dt);
        for (int i = 0; i < 2 && state_ == State::Playing; ++i) subStep(dt / 2);
    }

    // --- Abfragen ---------------------------------------------------------------------------------
    State state() const { return state_; }
    uint32_t score() const { return score_; }
    int level() const { return level_; }
    int playerPoints() const { return playerPoints_; }
    int cpuPoints() const { return cpuPoints_; }
    float playerAngle() const { return player_; }
    float cpuAngle() const { return cpu_; }
    float ballX() const { return x_; }
    float ballY() const { return y_; }
    float ballSpeed() const { return sqrtf(vx_ * vx_ + vy_ * vy_); }
    int rally() const { return rally_; }
    bool lastPointPlayer() const { return lastPointPlayer_; }

    // --- für Tests ----------------------------------------------------------------------------------
    void setBall(float x, float y, float vx, float vy) {
        x_ = x; y_ = y; vx_ = vx; vy_ = vy;
        state_ = State::Playing;
    }
    void setCpuAngle(float a) { cpu_ = a; }
    void setPoints(int player, int cpu) { playerPoints_ = player; cpuPoints_ = cpu; }

    static float normalize(float deg) {
        while (deg < 0) deg += 360.0f;
        while (deg >= 360.0f) deg -= 360.0f;
        return deg;
    }
    /** a − b auf −180 … 180. */
    static float angleDiff(float a, float b) {
        float d = normalize(a - b);
        return d > 180.0f ? d - 360.0f : d;
    }

private:
    static constexpr float kMaxBallSpeed = 430.0f;
    static constexpr float kSpeedUp = 1.06f;
    static constexpr float kMaxDeflect = 50.0f;  // Abprallwinkel am Schlägerende

    float random01() {
        seed_ ^= seed_ << 13;
        seed_ ^= seed_ >> 17;
        seed_ ^= seed_ << 5;
        return (seed_ & 0xFFFFFF) / 16777216.0f;
    }
    static float rad(float deg) { return deg * 0.017453292f; }
    static float deg(float rad) { return rad * 57.29578f; }

    float cpuSpeed() const {  // Grad je Sekunde
        const float v = 95.0f + 22.0f * level_;
        return v < 280.0f ? v : 280.0f;
    }
    float cpuError() const {  // Zielfehler in Grad
        const float e = 14.0f - 1.5f * level_;
        return e > 3.0f ? e : 3.0f;
    }

    void resetMatch() {
        playerPoints_ = cpuPoints_ = 0;
        rally_ = 0;
        x_ = y_ = vx_ = vy_ = 0;
    }

    void beginServe(bool towardPlayer) {
        state_ = State::Serve;
        timer_ = 1.0f;
        serveToPlayer_ = towardPlayer;
        x_ = y_ = vx_ = vy_ = 0;
        rally_ = 0;
        aimError_ = (random01() * 2 - 1) * cpuError();
    }

    void launch() {
        const float a = (serveToPlayer_ ? 90.0f : 270.0f) + (random01() * 2 - 1) * 35.0f;
        const float speed = 170.0f + 12.0f * level_;
        vx_ = cosf(rad(a)) * speed;
        vy_ = sinf(rad(a)) * speed;
        state_ = State::Playing;
    }

    /** Wo verlässt der Ball den Schlägerkreis? (Winkel) – für den Computer. */
    bool predictExit(float& angle) const {
        const float r = kPaddleR - kBallR;
        const float b = x_ * vx_ + y_ * vy_, a = vx_ * vx_ + vy_ * vy_, c = x_ * x_ + y_ * y_ - r * r;
        if (a <= 0) return false;
        const float disc = b * b - a * c;
        if (disc < 0) return false;
        const float t = (-b + sqrtf(disc)) / a;
        angle = normalize(deg(atan2f(y_ + vy_ * t, x_ + vx_ * t)));
        return true;
    }

    void moveCpu(float dt) {
        float target = 270.0f;
        float exitAngle;
        if (state_ == State::Playing && predictExit(exitAngle) && exitAngle > 180.0f) target = exitAngle + aimError_;
        if (target < kCpuMin) target = kCpuMin;
        if (target > kCpuMax) target = kCpuMax;
        const float d = target - cpu_;
        const float maxStep = cpuSpeed() * dt;
        cpu_ += d > maxStep ? maxStep : (d < -maxStep ? -maxStep : d);
    }

    void subStep(float dt) {
        x_ += vx_ * dt;
        y_ += vy_ * dt;
        const float r = sqrtf(x_ * x_ + y_ * y_);
        const bool outward = x_ * vx_ + y_ * vy_ > 0;
        if (outward && r + kBallR >= kPaddleR && r - kBallR <= kPaddleR + kPaddleThick) {
            const float a = normalize(deg(atan2f(y_, x_)));
            const float slack = deg(asinf(kBallR / r));
            const bool bottom = a < 180.0f;
            const float paddle = bottom ? player_ : cpu_;
            const float diff = angleDiff(a, paddle);
            if (fabsf(diff) <= kPaddleHalf + slack) {
                bounce(a, diff, bottom);
                return;
            }
        }
        if (r - kBallR > kArenaR) {
            // durch: der Gegner der Hälfte, durch die der Ball hinaus ist, punktet
            const bool playerScores = y_ < 0;
            lastPointPlayer_ = playerScores;
            if (playerScores) {
                ++playerPoints_;
                score_ += 100;
            } else {
                ++cpuPoints_;
            }
            state_ = State::Point;
            timer_ = 0.9f;
        }
    }

    void bounce(float a, float diff, bool byPlayer) {
        float speed = ballSpeed() * kSpeedUp;
        if (speed > kMaxBallSpeed) speed = kMaxBallSpeed;
        // nach innen, je nach Trefferpunkt zur Seite gelenkt
        float f = diff / kPaddleHalf;
        if (f > 1) f = 1;
        if (f < -1) f = -1;
        const float out = a + 180.0f + f * kMaxDeflect;
        vx_ = cosf(rad(out)) * speed;
        vy_ = sinf(rad(out)) * speed;
        const float r = kPaddleR - kBallR - 0.5f;
        x_ = cosf(rad(a)) * r;
        y_ = sinf(rad(a)) * r;
        ++rally_;
        if (byPlayer) score_ += 10;
        aimError_ = (random01() * 2 - 1) * cpuError();
    }

    void afterPoint() {
        if (playerPoints_ >= kWinPoints) {
            score_ += 1000u * static_cast<uint32_t>(level_);
            state_ = State::MatchWon;
            timer_ = 2.5f;
            return;
        }
        if (cpuPoints_ >= kWinPoints) {
            state_ = State::GameOver;
            return;
        }
        beginServe(!lastPointPlayer_);  // Aufschlag zu dem, der den Punkt verloren hat
    }

    State state_ = State::Ready;
    uint32_t score_ = 0;
    int level_ = 1;
    int playerPoints_ = 0, cpuPoints_ = 0, rally_ = 0;
    float player_ = 90.0f, cpu_ = 270.0f;
    float x_ = 0, y_ = 0, vx_ = 0, vy_ = 0;
    float timer_ = 0, aimError_ = 0;
    bool serveToPlayer_ = true, lastPointPlayer_ = false;
    uint32_t seed_ = 0x1234ABCDu;
};

}  // namespace game
}  // namespace app
