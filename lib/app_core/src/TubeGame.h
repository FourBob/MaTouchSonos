#pragma once

#include <math.h>
#include <stdint.h>

namespace app {
namespace game {

/**
 * „Röhrensturm“ – nach dem Vorbild von Tempest (1981), als Easteregg.
 *
 * Man blickt in eine Röhre aus kLanes Bahnen. Das eigene Schiff sitzt am Rand (vorn) und wird mit
 * dem Drehring von Bahn zu Bahn bewegt – genau wie am Automaten mit dem Drehknopf. Gegner kriechen
 * aus der Tiefe (Mitte) herauf:
 *  - Flipper klettern und kippen dabei in Nachbarbahnen; oben angekommen kippen sie am Rand auf
 *    das Schiff zu und fangen es.
 *  - Tanker klettern geradeaus und zerfallen getroffen (oder oben) in zwei Flipper.
 *  - Spiker klettern ein Stück, hinterlassen einen Stachel in ihrer Bahn und ziehen sich zurück.
 * Gegner schießen auch. Sind alle Gegner eines Levels erledigt, taucht das Schiff durch die Röhre
 * ins nächste Level (andere Form, schneller) – ein Stachel in der eigenen Bahn ist dabei tödlich.
 * Superzapper (Mitte antippen): einmal je Level alle Gegner, ein zweites Mal einen.
 *
 * Tiefe z: 0 = hinten (Mitte des Bildschirms), 1 = vorn (Rand). Bahn i liegt zwischen den
 * Kanten i und i+1; die Röhre ist geschlossen. Reine Logik, auf dem PC getestet.
 */
class TubeGame {
public:
    static constexpr int kLanes = 16;
    static constexpr int kMaxEnemies = 24;
    static constexpr int kMaxShots = 8;
    static constexpr int kMaxEnemyShots = 8;
    static constexpr int kMaxBlasts = 12;
    static constexpr int kLives = 3;
    static constexpr int kShapes = 5;
    static constexpr uint32_t kExtraLifeEvery = 20000;

    enum class State : uint8_t { Ready, Playing, Dying, Warp, GameOver };
    enum class Type : uint8_t { Flipper, Tanker, Spiker };

    struct Enemy {
        Type type;
        bool alive;
        bool onRim;        ///< oben angekommen (nur Flipper)
        bool retreating;   ///< Spiker auf dem Rückweg
        int lane;          ///< aktuelle Bahn (bei einem Kipp-Vorgang: Ausgangsbahn)
        int flipTo;        ///< Zielbahn beim Kippen (= lane, wenn nicht gekippt wird)
        float flipT;       ///< Fortschritt des Kippens 0 … 1
        float z;
        float target;      ///< Spiker: so hoch klettert er
        float flipTimer, fireTimer;
    };
    struct Shot {
        int lane;
        float z;
        bool alive;
    };
    struct Blast {
        float lane;        ///< Bahnmitte (kann zwischen Bahnen liegen)
        float z;
        float age;         ///< < 0 = frei
    };

    TubeGame() { reset(); }

    void reset() {
        state_ = State::Ready;
        score_ = 0;
        lives_ = kLives;
        level_ = 1;
        playerPos_ = 0;
        nextExtraLife_ = kExtraLifeEvery;
        startLevel();
    }

    /** Taste (losgelassen): Start. Gefeuert wird über setFire() solange die Taste gedrückt ist. */
    void press() {
        if (state_ == State::Ready) {
            state_ = State::Playing;
            score_ = 0;
            lives_ = kLives;
            level_ = 1;
            nextExtraLife_ = kExtraLifeEvery;
            startLevel();
        }
    }

    /** Drehring: Rohschritte (4 je Rastung) – eine Rastung = eine Bahn. */
    void rotate(int32_t rawSteps) {
        if (state_ != State::Playing) return;
        playerPos_ += rawSteps * 0.25f;
        while (playerPos_ < 0) playerPos_ += kLanes;
        while (playerPos_ >= kLanes) playerPos_ -= kLanes;
    }
    void setFire(bool held) { fireHeld_ = held; }

    /** Superzapper: einmal je Level alle Gegner, beim zweiten Mal einer. */
    void zap() {
        if (state_ != State::Playing || zaps_ >= 2) return;
        if (zaps_ == 0) {
            for (Enemy& e : enemies_)
                if (e.alive) kill(e);
            for (Shot& s : enemyShots_) s.alive = false;
        } else {
            for (Enemy& e : enemies_)
                if (e.alive) {
                    kill(e);
                    break;
                }
        }
        ++zaps_;
        zapFlash_ = 0.25f;
    }

    void step(float dt) {
        ageBlasts(dt);
        if (zapFlash_ > 0) zapFlash_ -= dt;
        switch (state_) {
            case State::Ready:
            case State::GameOver:
                return;
            case State::Dying:
                stateTimer_ -= dt;
                if (stateTimer_ <= 0) afterDeath();
                return;
            case State::Warp:
                stepWarp(dt);
                return;
            case State::Playing:
                break;
        }
        spawn(dt);
        moveShots(dt);
        moveEnemies(dt);
        moveEnemyShots(dt);
        hitTest();
        if (state_ == State::Playing && spawnLeft_ == 0 && aliveEnemies() == 0) {
            state_ = State::Warp;
            playerZ_ = 1.0f;
            for (Shot& s : shots_) s.alive = false;
            for (Shot& s : enemyShots_) s.alive = false;
        }
    }

    // --- Abfragen ---------------------------------------------------------------------------------
    State state() const { return state_; }
    uint32_t score() const { return score_; }
    int lives() const { return lives_; }
    int level() const { return level_; }
    int shape() const { return (level_ - 1) % kShapes; }
    int playerLane() const { return static_cast<int>(playerPos_ + 0.5f) % kLanes; }
    /** Tiefe des Schiffs: 1 = am Rand; beim Tauchen ins nächste Level sinkt sie auf 0. */
    float playerZ() const { return playerZ_; }
    bool playerVisible() const { return state_ == State::Playing || state_ == State::Warp; }
    const Enemy& enemy(int i) const { return enemies_[i]; }
    /** Bahnlage eines Gegners als Kommazahl (beim Kippen zwischen zwei Bahnen). */
    float enemyLane(const Enemy& e) const {
        if (e.flipTo == e.lane) return static_cast<float>(e.lane);
        int to = e.flipTo;
        if (to - e.lane > kLanes / 2) to -= kLanes;
        if (e.lane - to > kLanes / 2) to += kLanes;
        return e.lane + (to - e.lane) * e.flipT;
    }
    const Shot& shot(int i) const { return shots_[i]; }
    const Shot& enemyShot(int i) const { return enemyShots_[i]; }
    const Blast& blast(int i) const { return blasts_[i]; }
    float spike(int lane) const { return spikes_[lane]; }
    int zapsLeft() const { return 2 - zaps_; }
    bool zapFlash() const { return zapFlash_ > 0; }
    int aliveEnemies() const {
        int n = 0;
        for (const Enemy& e : enemies_) n += e.alive ? 1 : 0;
        return n;
    }
    int spawnLeft() const { return spawnLeft_; }

    // --- für Tests ----------------------------------------------------------------------------------
    void startPlaying() { press(); }
    void clearEnemies() {
        for (Enemy& e : enemies_) e.alive = false;
        spawnLeft_ = 0;
    }
    void setSpawnLeft(int n) { spawnLeft_ = n; }
    void placeEnemy(int i, Type type, int lane, float z) {
        Enemy& e = enemies_[i];
        e = Enemy{type, true, false, false, lane, lane, 0, z, 0.6f, 99.0f, 99.0f};
    }
    void setPlayerLane(int lane) { playerPos_ = static_cast<float>(lane); }
    void setSpike(int lane, float h) { spikes_[lane] = h; }

private:
    static constexpr float kShotSpeed = 2.4f;        // Tiefe je Sekunde
    static constexpr float kEnemyShotSpeed = 0.55f;
    static constexpr float kFireInterval = 0.11f;
    static constexpr float kFlipDuration = 0.3f;
    static constexpr float kHitDepth = 0.05f;
    static constexpr float kWarpSpeed = 0.45f;       // Tiefe je Sekunde beim Tauchen
    static constexpr float kSpikeShrink = 0.06f;

    float random01() {
        seed_ ^= seed_ << 13;
        seed_ ^= seed_ >> 17;
        seed_ ^= seed_ << 5;
        return (seed_ & 0xFFFFFF) / 16777216.0f;
    }
    static int wrapLane(int lane) { return ((lane % kLanes) + kLanes) % kLanes; }
    float climbSpeed() const {
        const float v = 0.10f + 0.018f * level_;
        return v < 0.32f ? v : 0.32f;
    }

    void startLevel() {
        for (Enemy& e : enemies_) e.alive = false;
        for (Shot& s : shots_) s.alive = false;
        for (Shot& s : enemyShots_) s.alive = false;
        for (float& s : spikes_) s = 0;
        spawnLeft_ = 6 + 2 * level_ < 30 ? 6 + 2 * level_ : 30;
        spawnTimer_ = 1.0f;
        zaps_ = 0;
        playerZ_ = 1.0f;
        fireCooldown_ = 0;
    }

    void addScore(uint32_t points) {
        score_ += points;
        while (score_ >= nextExtraLife_) {
            ++lives_;
            nextExtraLife_ += kExtraLifeEvery;
        }
    }

    void addBlast(float lane, float z) {
        for (Blast& b : blasts_) {
            if (b.age >= 0) continue;
            b = Blast{lane, z, 0};
            return;
        }
    }
    void ageBlasts(float dt) {
        for (Blast& b : blasts_) {
            if (b.age < 0) continue;
            b.age += dt;
            if (b.age > 0.4f) b.age = -1;
        }
    }

    Enemy* freeEnemy() {
        for (Enemy& e : enemies_)
            if (!e.alive) return &e;
        return nullptr;
    }

    bool spawnEnemy(Type type, int lane, float z) {
        Enemy* e = freeEnemy();
        if (!e) return false;
        *e = Enemy{type, true, false, false, wrapLane(lane), wrapLane(lane), 0, z, 0.35f + random01() * 0.4f,
                   1.0f + random01() * 2.0f, 1.5f + random01() * 3.0f};
        return true;
    }

    void spawn(float dt) {
        if (spawnLeft_ <= 0) return;
        spawnTimer_ -= dt;
        if (spawnTimer_ > 0) return;
        const float interval = 1.4f - 0.06f * level_;
        spawnTimer_ = interval > 0.5f ? interval : 0.5f;
        const float r = random01();
        Type type = Type::Flipper;
        if (level_ >= 3 && r < 0.2f) type = Type::Spiker;
        else if (level_ >= 2 && r < 0.45f) type = Type::Tanker;
        if (spawnEnemy(type, static_cast<int>(random01() * kLanes), 0.0f)) --spawnLeft_;
    }

    void moveShots(float dt) {
        if (fireCooldown_ > 0) fireCooldown_ -= dt;
        if (fireHeld_ && fireCooldown_ <= 0) {
            for (Shot& s : shots_) {
                if (s.alive) continue;
                s = Shot{playerLane(), 1.0f, true};
                fireCooldown_ = kFireInterval;
                break;
            }
        }
        for (Shot& s : shots_) {
            if (!s.alive) continue;
            s.z -= kShotSpeed * dt;
            if (spikes_[s.lane] > 0 && s.z <= spikes_[s.lane]) {
                // Stachel wird kürzer
                spikes_[s.lane] -= kSpikeShrink;
                if (spikes_[s.lane] < 0.02f) spikes_[s.lane] = 0;
                addScore(1);
                s.alive = false;
                continue;
            }
            if (s.z <= 0) s.alive = false;
        }
    }

    void startFlip(Enemy& e, int dir) {
        e.flipTo = wrapLane(e.lane + dir);
        e.flipT = 0;
    }

    void moveEnemies(float dt) {
        const int player = playerLane();
        for (Enemy& e : enemies_) {
            if (!e.alive) continue;
            // laufendes Kippen fortsetzen
            if (e.flipTo != e.lane) {
                e.flipT += dt / kFlipDuration;
                if (e.flipT >= 1.0f) {
                    e.lane = e.flipTo;
                    e.flipT = 0;
                }
            }
            switch (e.type) {
                case Type::Flipper:
                    if (e.onRim) {
                        if (e.flipTo == e.lane && e.lane == player) {
                            catchPlayer(e);
                            return;
                        }
                        if (e.flipTo == e.lane) {
                            // auf dem kürzeren Weg zum Spieler kippen
                            int d = player - e.lane;
                            if (d > kLanes / 2) d -= kLanes;
                            if (d < -kLanes / 2) d += kLanes;
                            startFlip(e, d > 0 ? 1 : -1);
                        }
                        break;
                    }
                    e.z += climbSpeed() * dt;
                    if (e.z >= 1.0f) {
                        e.z = 1.0f;
                        e.onRim = true;
                    }
                    e.flipTimer -= dt;
                    if (e.flipTimer <= 0 && e.flipTo == e.lane) {
                        startFlip(e, random01() < 0.5f ? 1 : -1);
                        e.flipTimer = 1.0f + random01() * 2.0f;
                    }
                    break;
                case Type::Tanker:
                    e.z += climbSpeed() * 0.8f * dt;
                    if (e.z >= 1.0f) splitTanker(e);
                    break;
                case Type::Spiker:
                    if (!e.retreating) {
                        e.z += climbSpeed() * 1.1f * dt;
                        if (e.z > spikes_[e.lane]) spikes_[e.lane] = e.z;
                        if (e.z >= e.target) e.retreating = true;
                    } else {
                        e.z -= climbSpeed() * 1.5f * dt;
                        if (e.z <= 0) e.alive = false;  // zurück in der Tiefe: erledigt
                    }
                    break;
            }
            // schießen (nicht vom Rand aus)
            if (e.type != Type::Spiker && !e.onRim && e.z > 0.15f) {
                e.fireTimer -= dt;
                if (e.fireTimer <= 0) {
                    e.fireTimer = 2.0f + random01() * 3.0f;
                    for (Shot& s : enemyShots_) {
                        if (s.alive) continue;
                        s = Shot{e.lane, e.z, true};
                        break;
                    }
                }
            }
        }
    }

    void moveEnemyShots(float dt) {
        const int player = playerLane();
        for (Shot& s : enemyShots_) {
            if (!s.alive) continue;
            s.z += kEnemyShotSpeed * dt;
            if (s.z >= 1.0f) {
                s.alive = false;
                if (s.lane == player) {
                    die(static_cast<float>(player), 1.0f);
                    return;
                }
            }
        }
    }

    /** Gegner in der Bahn des Schusses, dessen Tiefe der Schuss in diesem Schritt erreicht hat. */
    void hitTest() {
        for (Shot& s : shots_) {
            if (!s.alive) continue;
            for (Shot& es : enemyShots_) {
                if (es.alive && es.lane == s.lane && fabsf(es.z - s.z) < kHitDepth * 1.5f) {
                    es.alive = false;
                    s.alive = false;
                    addBlast(static_cast<float>(s.lane), s.z);
                    break;
                }
            }
            if (!s.alive) continue;
            for (Enemy& e : enemies_) {
                if (!e.alive) continue;
                const int lane = (e.flipTo != e.lane && e.flipT >= 0.5f) ? e.flipTo : e.lane;
                if (lane != s.lane) continue;
                // Schuss fliegt nach hinten, Gegner nach vorn: getroffen, wenn der Schuss ihn erreicht hat
                if (s.z > e.z + kHitDepth) continue;
                if (s.z < e.z - kHitDepth - kShotSpeed / 60.0f) continue;
                s.alive = false;
                if (e.type == Type::Tanker) {
                    addScore(100);
                    splitTanker(e);
                } else {
                    kill(e);
                }
                break;
            }
        }
    }

    void kill(Enemy& e) {
        e.alive = false;
        addScore(e.type == Type::Flipper ? 150 : e.type == Type::Tanker ? 100 : 50);
        addBlast(enemyLane(e), e.z);
    }

    void splitTanker(Enemy& e) {
        e.alive = false;
        addBlast(static_cast<float>(e.lane), e.z);
        const float z = e.z >= 1.0f ? 0.98f : e.z;
        const int lane = e.lane;
        spawnEnemy(Type::Flipper, lane - 1, z);
        spawnEnemy(Type::Flipper, lane + 1, z);
    }

    void catchPlayer(Enemy& e) {
        e.alive = false;
        die(static_cast<float>(e.lane), 1.0f);
    }

    void die(float lane, float z) {
        addBlast(lane, z);
        --lives_;
        state_ = State::Dying;
        stateTimer_ = 1.6f;
    }

    void afterDeath() {
        if (lives_ <= 0) {
            state_ = State::GameOver;
            return;
        }
        if (playerZ_ < 1.0f) {
            // beim Tauchen gestorben: weiter im nächsten Level
            ++level_;
            startLevel();
            state_ = State::Playing;
            return;
        }
        // Gegner auf der Röhre kommen später noch einmal
        for (Enemy& e : enemies_) {
            if (!e.alive) continue;
            e.alive = false;
            ++spawnLeft_;
        }
        for (Shot& s : shots_) s.alive = false;
        for (Shot& s : enemyShots_) s.alive = false;
        spawnTimer_ = 1.5f;
        state_ = State::Playing;
    }

    void stepWarp(float dt) {
        playerZ_ -= kWarpSpeed * dt;
        const int lane = playerLane();
        if (spikes_[lane] > 0 && playerZ_ <= spikes_[lane]) {
            die(static_cast<float>(lane), playerZ_);
            return;
        }
        if (playerZ_ <= 0) {
            ++level_;
            startLevel();
            state_ = State::Playing;
        }
    }

    State state_ = State::Ready;
    uint32_t score_ = 0, nextExtraLife_ = kExtraLifeEvery;
    int lives_ = kLives, level_ = 1;
    float playerPos_ = 0, playerZ_ = 1.0f;
    bool fireHeld_ = false;
    float fireCooldown_ = 0;
    int spawnLeft_ = 0;
    float spawnTimer_ = 0;
    int zaps_ = 0;
    float zapFlash_ = 0;
    float stateTimer_ = 0;
    uint32_t seed_ = 0x9E3779B9u;
    Enemy enemies_[kMaxEnemies] = {};
    Shot shots_[kMaxShots] = {};
    Shot enemyShots_[kMaxEnemyShots] = {};
    Blast blasts_[kMaxBlasts] = {{0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1},
                                 {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}};
    float spikes_[kLanes] = {};
};

}  // namespace game
}  // namespace app
