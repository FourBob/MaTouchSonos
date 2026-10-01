#pragma once

#include <math.h>
#include <stdint.h>

namespace app {
namespace game {

/**
 * „Asteroiden“ – Asteroids für das runde Display, als Easteregg.
 *
 * Die Welt ist die Kreisscheibe des Bildschirms. Wer den Rand überquert, kommt auf der
 * gegenüberliegenden Seite wieder herein (Spiegelung durch die Mitte) – das runde Gegenstück zum
 * „Bildschirm-Umbruch“ des Originals.
 *
 * Steuerung: Drehring dreht das Schiff (Rohschritte, eine Ringumdrehung ≈ eine Schiffsumdrehung),
 * Drücken schießt, Berührung hält den Schub.
 *
 * Koordinaten: Mittelpunkt (0, 0), Pixel, y nach unten. Winkel in Grad, 0° = oben, im Uhrzeigersinn.
 * Reine Logik mit festem Zeitschritt, auf dem PC getestet.
 */
class AsteroidsGame {
public:
    static constexpr float kWorldR = 236.0f;
    static constexpr float kShipR = 11.0f;
    static constexpr int kMaxRocks = 40;
    static constexpr int kMaxShots = 5;
    static constexpr int kMaxDebris = 48;
    static constexpr int kLives = 3;
    static constexpr uint32_t kExtraLifeEvery = 10000;

    enum class State : uint8_t { Ready, Playing, GameOver };

    struct Rock {
        float x, y, vx, vy;
        float angle, spin;  ///< Drehung der Form (nur Anzeige)
        uint8_t size;       ///< 0 = groß, 1 = mittel, 2 = klein
        uint8_t shape;      ///< Formvariante (Anzeige)
        bool alive;
    };
    struct Shot {
        float x, y, vx, vy, life;
        bool alive;
    };
    struct Debris {
        float x, y, vx, vy, life;
    };

    static float rockRadius(int size) { return size == 0 ? 34.0f : size == 1 ? 18.0f : 9.0f; }
    static int rockScore(int size) { return size == 0 ? 20 : size == 1 ? 50 : 100; }

    AsteroidsGame() { reset(); }

    /** Neues Spiel (Startbildschirm mit treibenden Felsen). */
    void reset() {
        state_ = State::Ready;
        score_ = 0;
        lives_ = kLives;
        wave_ = 0;
        nextExtraLife_ = kExtraLifeEvery;
        for (Rock& r : rocks_) r.alive = false;
        for (Shot& s : shots_) s.alive = false;
        for (Debris& d : debris_) d.life = 0;
        placeShip();
        shipAlive_ = false;
        startWave();
    }

    /** Taste (losgelassen): Start. Geschossen wird mit fire() schon beim Drücken. */
    void press() {
        if (state_ == State::Ready) start();
    }
    void start() {
        state_ = State::Playing;
        score_ = 0;
        lives_ = kLives;
        wave_ = 0;
        nextExtraLife_ = kExtraLifeEvery;
        for (Rock& r : rocks_) r.alive = false;
        for (Shot& s : shots_) s.alive = false;
        placeShip();
        shipAlive_ = true;
        invulnerable_ = 2.0f;
        startWave();
    }

    void rotate(float deg) {
        if (shipAlive_) angle_ = normalize(angle_ + deg);
    }
    void setThrust(bool on) { thrust_ = on; }

    void fire() {
        if (state_ != State::Playing || !shipAlive_) return;
        for (Shot& s : shots_) {
            if (s.alive) continue;
            const float dx = sinf(rad(angle_)), dy = -cosf(rad(angle_));
            s = Shot{x_ + dx * kShipR, y_ + dy * kShipR, vx_ + dx * kShotSpeed, vy_ + dy * kShotSpeed, kShotLife, true};
            return;
        }
    }

    void step(float dt) {
        moveRocks(dt);
        moveDebris(dt);
        if (state_ != State::Playing) return;
        moveShip(dt);
        moveShots(dt);
        hitRocks();
        if (shipAlive_ && invulnerable_ <= 0) crashShip();
        if (!shipAlive_) respawn(dt);
        if (rocksLeft() == 0) {
            waveDelay_ -= dt;
            if (waveDelay_ <= 0) startWave();
        }
    }

    // --- Abfragen ---------------------------------------------------------------------------------
    State state() const { return state_; }
    uint32_t score() const { return score_; }
    int lives() const { return lives_; }
    int wave() const { return wave_; }
    bool shipAlive() const { return shipAlive_; }
    bool shipVisible() const { return shipAlive_ && (invulnerable_ <= 0 || fmodf(invulnerable_, 0.3f) < 0.18f); }
    bool thrusting() const { return shipAlive_ && thrust_; }
    float shipX() const { return x_; }
    float shipY() const { return y_; }
    float shipAngle() const { return angle_; }
    const Rock& rock(int i) const { return rocks_[i]; }
    const Shot& shot(int i) const { return shots_[i]; }
    const Debris& debris(int i) const { return debris_[i]; }
    int rocksLeft() const {
        int n = 0;
        for (const Rock& r : rocks_) n += r.alive ? 1 : 0;
        return n;
    }

    // --- für Tests ----------------------------------------------------------------------------------
    void clearRocks() { for (Rock& r : rocks_) r.alive = false; }
    void placeRock(int i, float x, float y, float vx, float vy, int size) {
        rocks_[i] = Rock{x, y, vx, vy, 0, 0, static_cast<uint8_t>(size), 0, true};
    }
    void setShip(float x, float y, float vx, float vy) { x_ = x; y_ = y; vx_ = vx; vy_ = vy; }
    void setInvulnerable(float s) { invulnerable_ = s; }
    void setWaveDelay(float s) { waveDelay_ = s; }

    /** Rand überquert? Dann auf der gegenüberliegenden Seite wieder herein. */
    static void wrap(float& x, float& y) {
        const float r2 = x * x + y * y;
        if (r2 <= kWorldR * kWorldR) return;
        const float r = sqrtf(r2);
        const float k = -(kWorldR - 0.5f) / r;
        x *= k;
        y *= k;
    }

private:
    static constexpr float kThrust = 280.0f;     // px/s²
    static constexpr float kMaxSpeed = 260.0f;
    static constexpr float kDrag = 0.55f;        // je Sekunde
    static constexpr float kShotSpeed = 420.0f;
    static constexpr float kShotLife = 0.85f;

    static float rad(float deg) { return deg * 0.017453292f; }
    static float normalize(float deg) {
        while (deg < 0) deg += 360.0f;
        while (deg >= 360.0f) deg -= 360.0f;
        return deg;
    }
    float random01() {
        seed_ ^= seed_ << 13;
        seed_ ^= seed_ >> 17;
        seed_ ^= seed_ << 5;
        return (seed_ & 0xFFFFFF) / 16777216.0f;
    }

    void placeShip() {
        x_ = y_ = vx_ = vy_ = 0;
        angle_ = 0;
        thrust_ = false;
    }

    void startWave() {
        ++wave_;
        const int count = 3 + wave_ < 10 ? 3 + wave_ : 10;
        for (int i = 0; i < count; ++i) {
            // weit weg von der Mitte (dort steht das Schiff)
            const float a = random01() * 6.2831853f;
            const float r = 140.0f + random01() * 80.0f;
            spawnRock(cosf(a) * r, sinf(a) * r, 0, 1.0f + 0.08f * wave_);
        }
        waveDelay_ = 2.0f;
    }

    void spawnRock(float x, float y, int size, float speedFactor) {
        for (Rock& r : rocks_) {
            if (r.alive) continue;
            const float a = random01() * 6.2831853f;
            const float base = size == 0 ? 35.0f : size == 1 ? 60.0f : 85.0f;
            const float v = (base + random01() * 30.0f) * speedFactor;
            r = Rock{x, y, cosf(a) * v, sinf(a) * v, random01() * 360.0f, (random01() - 0.5f) * 120.0f,
                     static_cast<uint8_t>(size), static_cast<uint8_t>(random01() * 4), true};
            return;
        }
    }

    void moveRocks(float dt) {
        for (Rock& r : rocks_) {
            if (!r.alive) continue;
            r.x += r.vx * dt;
            r.y += r.vy * dt;
            r.angle = normalize(r.angle + r.spin * dt);
            wrap(r.x, r.y);
        }
    }

    void moveDebris(float dt) {
        for (Debris& d : debris_) {
            if (d.life <= 0) continue;
            d.x += d.vx * dt;
            d.y += d.vy * dt;
            d.life -= dt;
        }
    }

    void burst(float x, float y, int count, float speed) {
        for (int n = 0; n < count; ++n) {
            for (Debris& d : debris_) {
                if (d.life > 0) continue;
                const float a = random01() * 6.2831853f;
                const float v = speed * (0.3f + random01());
                d = Debris{x, y, cosf(a) * v, sinf(a) * v, 0.4f + random01() * 0.5f};
                break;
            }
        }
    }

    void moveShip(float dt) {
        if (invulnerable_ > 0) invulnerable_ -= dt;
        if (!shipAlive_) return;
        if (thrust_) {
            vx_ += sinf(rad(angle_)) * kThrust * dt;
            vy_ += -cosf(rad(angle_)) * kThrust * dt;
        }
        vx_ -= vx_ * kDrag * dt;
        vy_ -= vy_ * kDrag * dt;
        const float v = sqrtf(vx_ * vx_ + vy_ * vy_);
        if (v > kMaxSpeed) {
            vx_ *= kMaxSpeed / v;
            vy_ *= kMaxSpeed / v;
        }
        x_ += vx_ * dt;
        y_ += vy_ * dt;
        wrap(x_, y_);
    }

    void moveShots(float dt) {
        for (Shot& s : shots_) {
            if (!s.alive) continue;
            s.x += s.vx * dt;
            s.y += s.vy * dt;
            wrap(s.x, s.y);
            s.life -= dt;
            if (s.life <= 0) s.alive = false;
        }
    }

    void addScore(uint32_t points) {
        score_ += points;
        while (score_ >= nextExtraLife_) {
            ++lives_;
            nextExtraLife_ += kExtraLifeEvery;
        }
    }

    void breakRock(Rock& r) {
        r.alive = false;
        addScore(static_cast<uint32_t>(rockScore(r.size)));
        burst(r.x, r.y, r.size == 0 ? 10 : 6, 70.0f);
        if (r.size < 2) {
            const float x = r.x, y = r.y;
            const int size = r.size + 1;
            spawnRock(x, y, size, 1.0f + 0.08f * wave_);
            spawnRock(x, y, size, 1.0f + 0.08f * wave_);
        }
    }

    void hitRocks() {
        for (Shot& s : shots_) {
            if (!s.alive) continue;
            for (Rock& r : rocks_) {
                if (!r.alive) continue;
                const float dx = s.x - r.x, dy = s.y - r.y, rr = rockRadius(r.size);
                if (dx * dx + dy * dy > rr * rr) continue;
                s.alive = false;
                breakRock(r);
                break;
            }
        }
    }

    void crashShip() {
        for (Rock& r : rocks_) {
            if (!r.alive) continue;
            const float dx = x_ - r.x, dy = y_ - r.y, rr = rockRadius(r.size) + kShipR * 0.8f;
            if (dx * dx + dy * dy > rr * rr) continue;
            breakRock(r);
            shipAlive_ = false;
            burst(x_, y_, 16, 110.0f);
            --lives_;
            respawnTimer_ = 2.0f;
            if (lives_ <= 0) state_ = State::GameOver;
            return;
        }
    }

    void respawn(float dt) {
        if (state_ != State::Playing) return;
        respawnTimer_ -= dt;
        if (respawnTimer_ > 0) return;
        // erst, wenn die Mitte frei ist (spätestens nach weiteren 3 s)
        bool clear = true;
        for (const Rock& r : rocks_) {
            if (!r.alive) continue;
            const float d = rockRadius(r.size) + 60.0f;
            if (r.x * r.x + r.y * r.y < d * d) clear = false;
        }
        if (!clear && respawnTimer_ > -3.0f) return;
        placeShip();
        shipAlive_ = true;
        invulnerable_ = 2.0f;
    }

    State state_ = State::Ready;
    uint32_t score_ = 0, nextExtraLife_ = kExtraLifeEvery;
    int lives_ = kLives, wave_ = 0;
    float x_ = 0, y_ = 0, vx_ = 0, vy_ = 0, angle_ = 0;
    bool thrust_ = false, shipAlive_ = false;
    float invulnerable_ = 0, respawnTimer_ = 0, waveDelay_ = 0;
    uint32_t seed_ = 0x2545F491u;
    Rock rocks_[kMaxRocks] = {};
    Shot shots_[kMaxShots] = {};
    Debris debris_[kMaxDebris] = {};
};

}  // namespace game
}  // namespace app
