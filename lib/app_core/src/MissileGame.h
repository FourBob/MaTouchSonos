#pragma once

#include <math.h>
#include <stdint.h>

namespace app {
namespace game {

/**
 * „Raketenabwehr“ – nach dem Vorbild von Missile Command (1980), als Easteregg.
 *
 * Rund gedacht: In der Mitte liegt ein kleiner Planet mit sechs Städten und der Abschussbasis.
 * Feindliche Raketen kommen vom Rand von allen Seiten auf die Städte zu. Wo man den Bildschirm
 * antippt (oder wohin das Fadenkreuz zeigt – Ring dreht es, Drücken feuert), fliegt eine
 * Abwehrrakete hin und explodiert; die Explosionswolke vernichtet alle Raketen, die hineinfliegen.
 * Ab Welle 3 teilen sich manche Raketen unterwegs. Je Welle 30 Abwehrraketen; am Ende gibt es
 * Bonus für übrige Munition und Städte. Alle Städte zerstört → Spielende.
 *
 * Koordinaten: Mittelpunkt (0, 0), Pixel, y nach unten. Reine Logik, auf dem PC getestet.
 */
class MissileGame {
public:
    static constexpr float kWorldR = 236.0f;
    static constexpr float kPlanetR = 34.0f;
    static constexpr float kCityR = 46.0f;       // Abstand der Städte von der Mitte
    static constexpr int kCities = 6;
    static constexpr int kMaxEnemies = 24;
    static constexpr int kMaxCounters = 4;
    static constexpr int kMaxBlasts = 12;
    static constexpr int kAmmoPerWave = 30;
    static constexpr float kBlastR = 30.0f;
    static constexpr float kBlastTime = 1.0f;     // wächst und schrumpft
    static constexpr uint32_t kBonusCityEvery = 10000;

    enum class State : uint8_t { Ready, Playing, WaveEnd, GameOver };

    struct Enemy {
        float sx, sy;      ///< Start (Spur beginnt hier)
        float x, y;        ///< Spitze
        float vx, vy;
        int target;        ///< Stadt 0 … 5, −1 = Basis
        bool split;        ///< teilt sich unterwegs
        bool alive;
    };
    struct Counter {
        float x, y, tx, ty, vx, vy;
        bool alive;
    };
    struct Blast {
        float x, y, age;   ///< age < 0 = frei
        bool enemy;        ///< von einer Stadt/getroffenen Basis (vernichtet nichts)
    };

    MissileGame() { reset(); }

    void reset() {
        state_ = State::Ready;
        score_ = 0;
        wave_ = 1;
        for (bool& c : cities_) c = true;
        nextBonusCity_ = kBonusCityEvery;
        aim_ = 270.0f;
        aimX_ = 0;
        aimY_ = -kAimR;
        startWave();
    }

    /** Taste (losgelassen): Start vom Titel. */
    void press() {
        if (state_ != State::Ready) return;
        state_ = State::Playing;
        score_ = 0;
        wave_ = 1;
        for (bool& c : cities_) c = true;
        nextBonusCity_ = kBonusCityEvery;
        startWave();
    }

    /** Ring: Fadenkreuz um die Mitte drehen (Grad). */
    void rotateAim(float deg) {
        aim_ += deg;
        while (aim_ < 0) aim_ += 360.0f;
        while (aim_ >= 360.0f) aim_ -= 360.0f;
        aimX_ = cosf(aim_ * 0.017453292f) * kAimR;
        aimY_ = sinf(aim_ * 0.017453292f) * kAimR;
    }
    /** Abwehrrakete zum Fadenkreuz. */
    bool fireAtAim() { return fireAt(aimX_, aimY_); }
    /** Abwehrrakete zu einem Punkt (Touch). Setzt auch das Fadenkreuz dorthin. */
    bool fireAt(float x, float y) {
        if (state_ != State::Playing || ammo_ <= 0) return false;
        const float r = sqrtf(x * x + y * y);
        if (r < kPlanetR + 8 || r > kWorldR) return false;
        aimX_ = x;
        aimY_ = y;
        aim_ = atan2f(y, x) * 57.29578f;
        if (aim_ < 0) aim_ += 360.0f;
        for (Counter& c : counters_) {
            if (c.alive) continue;
            const float sx = x / r * kPlanetR, sy = y / r * kPlanetR;  // startet am Planetenrand
            const float d = sqrtf((x - sx) * (x - sx) + (y - sy) * (y - sy));
            c = Counter{sx, sy, x, y, (x - sx) / d * kCounterSpeed, (y - sy) / d * kCounterSpeed, true};
            --ammo_;
            return true;
        }
        return false;
    }

    void step(float dt) {
        ageBlasts(dt);
        switch (state_) {
            case State::Ready:
            case State::GameOver:
                return;
            case State::WaveEnd:
                timer_ -= dt;
                if (timer_ <= 0) {
                    ++wave_;
                    startWave();
                    state_ = State::Playing;
                }
                return;
            case State::Playing:
                break;
        }
        spawn(dt);
        moveCounters(dt);
        moveEnemies(dt);
        if (citiesLeft() == 0 && activeBlasts() == 0) {
            state_ = State::GameOver;
            return;
        }
        if (spawnLeft_ == 0 && aliveEnemies() == 0 && activeBlasts() == 0 && activeCounters() == 0) endWave();
    }

    // --- Abfragen ---------------------------------------------------------------------------------
    State state() const { return state_; }
    uint32_t score() const { return score_; }
    int wave() const { return wave_; }
    int ammo() const { return ammo_; }
    bool city(int i) const { return cities_[i]; }
    int citiesLeft() const {
        int n = 0;
        for (bool c : cities_) n += c ? 1 : 0;
        return n;
    }
    static float cityAngle(int i) { return -90.0f + i * 60.0f + 30.0f; }
    static float cityX(int i) { return cosf(cityAngle(i) * 0.017453292f) * kCityR; }
    static float cityY(int i) { return sinf(cityAngle(i) * 0.017453292f) * kCityR; }
    float aimX() const { return aimX_; }
    float aimY() const { return aimY_; }
    const Enemy& enemy(int i) const { return enemies_[i]; }
    const Counter& counter(int i) const { return counters_[i]; }
    const Blast& blast(int i) const { return blasts_[i]; }
    static float blastRadius(float age) {
        if (age < 0 || age > kBlastTime) return 0;
        return kBlastR * sinf(3.14159265f * age / kBlastTime);
    }
    uint32_t lastBonus() const { return lastBonus_; }
    int aliveEnemies() const {
        int n = 0;
        for (const Enemy& e : enemies_) n += e.alive ? 1 : 0;
        return n;
    }
    int spawnLeft() const { return spawnLeft_; }

    // --- für Tests ----------------------------------------------------------------------------------
    void clearEnemies() {
        for (Enemy& e : enemies_) e.alive = false;
        spawnLeft_ = 0;
    }
    void setSpawnLeft(int n) { spawnLeft_ = n; }
    void placeEnemy(int i, float x, float y, int target, float speed) {
        const float tx = target >= 0 ? cityX(target) : 0, ty = target >= 0 ? cityY(target) : 0;
        const float d = sqrtf((tx - x) * (tx - x) + (ty - y) * (ty - y));
        enemies_[i] = Enemy{x, y, x, y, (tx - x) / d * speed, (ty - y) / d * speed, target, false, true};
    }

private:
    static constexpr float kAimR = 140.0f;
    static constexpr float kCounterSpeed = 340.0f;

    float random01() {
        seed_ ^= seed_ << 13;
        seed_ ^= seed_ >> 17;
        seed_ ^= seed_ << 5;
        return (seed_ & 0xFFFFFF) / 16777216.0f;
    }
    float enemySpeed() const {
        const float v = 20.0f + 5.0f * wave_;
        return v < 75.0f ? v : 75.0f;
    }

    void startWave() {
        for (Enemy& e : enemies_) e.alive = false;
        for (Counter& c : counters_) c.alive = false;
        ammo_ = kAmmoPerWave;
        spawnLeft_ = 6 + 3 * wave_ < 30 ? 6 + 3 * wave_ : 30;
        spawnTimer_ = 1.0f;
    }

    int pickTarget() {
        // meist eine noch stehende Stadt, manchmal die Basis
        if (random01() < 0.15f || citiesLeft() == 0) return -1;
        int n = static_cast<int>(random01() * citiesLeft());
        for (int i = 0; i < kCities; ++i)
            if (cities_[i] && n-- == 0) return i;
        return -1;
    }

    bool launchEnemy(float sx, float sy, int target, bool canSplit) {
        for (Enemy& e : enemies_) {
            if (e.alive) continue;
            const float tx = target >= 0 ? cityX(target) : 0, ty = target >= 0 ? cityY(target) : 0;
            const float d = sqrtf((tx - sx) * (tx - sx) + (ty - sy) * (ty - sy));
            const float v = enemySpeed() * (0.85f + random01() * 0.3f);
            e = Enemy{sx, sy, sx, sy, (tx - sx) / d * v, (ty - sy) / d * v, target,
                      canSplit && wave_ >= 3 && random01() < 0.25f, true};
            return true;
        }
        return false;
    }

    void spawn(float dt) {
        if (spawnLeft_ <= 0) return;
        spawnTimer_ -= dt;
        if (spawnTimer_ > 0) return;
        const float interval = 1.7f - 0.1f * wave_;
        spawnTimer_ = (interval > 0.45f ? interval : 0.45f) * (0.6f + random01() * 0.8f);
        const float a = random01() * 6.2831853f;
        if (launchEnemy(cosf(a) * (kWorldR - 2), sinf(a) * (kWorldR - 2), pickTarget(), true)) --spawnLeft_;
    }

    void addBlast(float x, float y, bool enemy) {
        for (Blast& b : blasts_) {
            if (b.age >= 0) continue;
            b = Blast{x, y, 0, enemy};
            return;
        }
    }
    void ageBlasts(float dt) {
        for (Blast& b : blasts_) {
            if (b.age < 0) continue;
            b.age += dt;
            if (b.age > kBlastTime) b.age = -1;
        }
    }
    int activeBlasts() const {
        int n = 0;
        for (const Blast& b : blasts_) n += b.age >= 0 ? 1 : 0;
        return n;
    }
    int activeCounters() const {
        int n = 0;
        for (const Counter& c : counters_) n += c.alive ? 1 : 0;
        return n;
    }

    void moveCounters(float dt) {
        for (Counter& c : counters_) {
            if (!c.alive) continue;
            const float before = (c.tx - c.x) * c.vx + (c.ty - c.y) * c.vy;
            c.x += c.vx * dt;
            c.y += c.vy * dt;
            const float after = (c.tx - c.x) * c.vx + (c.ty - c.y) * c.vy;
            if (before > 0 && after <= 0) {  // Ziel erreicht
                c.alive = false;
                addBlast(c.tx, c.ty, false);
            }
        }
    }

    void addScore(uint32_t points) {
        score_ += points;
    }

    void moveEnemies(float dt) {
        for (Enemy& e : enemies_) {
            if (!e.alive) continue;
            e.x += e.vx * dt;
            e.y += e.vy * dt;
            // in eine Abwehr-Explosion geflogen?
            bool hit = false;
            for (const Blast& b : blasts_) {
                if (b.age < 0 || b.enemy) continue;
                const float r = blastRadius(b.age);
                if ((e.x - b.x) * (e.x - b.x) + (e.y - b.y) * (e.y - b.y) <= r * r) hit = true;
            }
            if (hit) {
                e.alive = false;
                addScore(25);
                continue;
            }
            // teilen (einmal, auf halbem Weg)
            const float r2 = e.x * e.x + e.y * e.y;
            if (e.split && r2 < 150.0f * 150.0f) {
                e.split = false;
                launchEnemy(e.x, e.y, pickTarget(), false);
                launchEnemy(e.x, e.y, pickTarget(), false);
            }
            // angekommen?
            const float tx = e.target >= 0 ? cityX(e.target) : 0, ty = e.target >= 0 ? cityY(e.target) : 0;
            const float reach = e.target >= 0 ? 6.0f : kPlanetR;
            if ((e.x - tx) * (e.x - tx) + (e.y - ty) * (e.y - ty) <= reach * reach ||
                (e.target < 0 && r2 <= kPlanetR * kPlanetR)) {
                e.alive = false;
                if (e.target >= 0) {
                    cities_[e.target] = false;
                    addBlast(tx, ty, true);
                } else {
                    ammo_ = 0;  // Basis getroffen: für den Rest der Welle keine Munition
                    addBlast(e.x, e.y, true);
                }
            }
        }
    }

    void endWave() {
        const uint32_t mult = static_cast<uint32_t>(wave_ < 11 ? (wave_ + 1) / 2 : 6);
        lastBonus_ = (static_cast<uint32_t>(ammo_) * 5 + static_cast<uint32_t>(citiesLeft()) * 100) * mult;
        addScore(lastBonus_);
        // Bonusstadt
        while (score_ >= nextBonusCity_) {
            nextBonusCity_ += kBonusCityEvery;
            for (bool& c : cities_) {
                if (!c) {
                    c = true;
                    break;
                }
            }
        }
        state_ = State::WaveEnd;
        timer_ = 3.0f;
    }

    State state_ = State::Ready;
    uint32_t score_ = 0, nextBonusCity_ = kBonusCityEvery, lastBonus_ = 0;
    int wave_ = 1, ammo_ = kAmmoPerWave, spawnLeft_ = 0;
    float spawnTimer_ = 0, timer_ = 0;
    float aim_ = 270.0f, aimX_ = 0, aimY_ = -kAimR;
    bool cities_[kCities] = {true, true, true, true, true, true};
    uint32_t seed_ = 0xC0FFEE11u;
    Enemy enemies_[kMaxEnemies] = {};
    Counter counters_[kMaxCounters] = {};
    Blast blasts_[kMaxBlasts] = {{0, 0, -1, false}, {0, 0, -1, false}, {0, 0, -1, false}, {0, 0, -1, false},
                                 {0, 0, -1, false}, {0, 0, -1, false}, {0, 0, -1, false}, {0, 0, -1, false},
                                 {0, 0, -1, false}, {0, 0, -1, false}, {0, 0, -1, false}, {0, 0, -1, false}};
};

}  // namespace game
}  // namespace app
