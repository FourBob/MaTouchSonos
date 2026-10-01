#pragma once

#include <math.h>
#include <stdint.h>

namespace app {
namespace game {

/**
 * „Boxenstopp“ – Pseudo-3D-Rennen im Stil der 80er (Solo-Modus), als Easteregg.
 *
 *   Start ─▶ Countdown ─▶ Rennen (5 Runden) ─▶ Ziel (Zeit, Bestzeit)
 *                           │  ▲
 *            rechts in die  ▼  │ „LOS“
 *            Boxengasse ── Box: Reifen wechseln, tanken (die Uhr läuft weiter)
 *
 * Die Strecke besteht aus Segmenten (Länge kSegmentLength) mit einer Krümmung. Die Darstellung
 * projiziert sie wie die klassischen Rennspiele (siehe RaceScreen); hier steckt nur die Logik:
 * Lenkrad (Drehring), Fliehkraft in Kurven, Bremsen, neben der Straße langsamer, Gegner zum
 * Überholen, Reifenverschleiß (Grip, Platten) und Sprit.
 *
 * Straßenquerlage x: −1 … +1 = Straße, darüber hinaus Gras. Reine Logik, auf dem PC getestet.
 */
class RaceGame {
public:
    // --- Strecke und Fahrzeug -----------------------------------------------------------------------
    static constexpr float kSegmentLength = 200.0f;
    static constexpr float kRoadWidth = 1000.0f;          // halbe Straßenbreite in Welteinheiten
    static constexpr float kMaxSpeed = kSegmentLength * 60.0f;  // ein Segment pro Bild bei 60 Bildern/s
    static constexpr int kMaxSegments = 1100;
    static constexpr int kLaps = 5;
    static constexpr int kCars = 10;
    static constexpr int kPitEntrySegment = 18;           // hier rechts raus in die Box
    static constexpr int kPitExitSegment = 45;            // hier geht es nach der Box weiter
    static constexpr float kCarHalfWidth = 0.22f;         // in Straßenbreiten (x-Einheiten)

    enum class State : uint8_t { Countdown, Racing, Pit, Finished };
    enum PitItem : uint8_t { FrontLeft, FrontRight, RearLeft, RearRight, Fuel, Go, kPitItems };

    struct Segment {
        float curve;      ///< Krümmung (+ = rechts), wie in den klassischen Spielen etwa −6 … +6
        uint8_t flags;    ///< kStartLine, kPitStrip, kPitSign
    };
    static constexpr uint8_t kStartLine = 1, kPitStrip = 2, kPitSign = 4;

    struct Car {
        float z;          ///< Position auf der Strecke (Welteinheiten)
        float x;          ///< Querlage
        float speed;
        uint8_t color;    ///< Index in die Farbtabelle der Anzeige
    };

    RaceGame() { buildTrack(); reset(); }

    /** Neues Rennen: Countdown, volle Reifen und Tank. */
    void reset() {
        state_ = State::Countdown;
        countdownMs_ = 3000;
        position_ = 0;
        speed_ = 0;
        playerX_ = 0;
        steer_ = 0;
        wheelIdleMs_ = 0;
        lap_ = 1;
        raceMs_ = 0;
        lastLapMs_ = 0;
        fuel_ = 1.0f;
        for (float& w : wear_) w = 0;
        pitSel_ = Go;
        pitWorkMs_ = 0;
        fueling_ = false;
        brake_ = false;
        collisionCooldownMs_ = 0;
        uint32_t seed = 12345;
        for (int i = 0; i < kCars; ++i) {
            seed = seed * 1103515245u + 12345u;
            Car& c = cars_[i];
            c.z = trackLength() * (0.08f + 0.9f * i / kCars);
            c.x = (static_cast<int>((seed >> 16) % 3) - 1) * 0.55f;
            c.speed = kMaxSpeed * (0.42f + 0.04f * ((seed >> 8) % 8));
            c.color = static_cast<uint8_t>(i % 5);
        }
    }

    // --- Eingaben -------------------------------------------------------------------------------------
    /** Drehring als Lenkrad: Rohschritte drehen das Lenkrad (−1 … +1). */
    void steer(int32_t rawSteps) {
        if (rawSteps == 0) return;
        steer_ += rawSteps * kSteerPerStep;
        if (steer_ > 1) steer_ = 1;
        if (steer_ < -1) steer_ = -1;
        wheelIdleMs_ = 0;
    }
    void setBrake(bool on) { brake_ = on; }
    /**
     * In der Box: Auswahl um `delta` Einträge weiterdrehen – im Uhrzeigersinn, so wie die Einträge
     * auf dem runden Bildschirm liegen: vorne links, vorne rechts, LOS (rechts), hinten rechts,
     * hinten links, Tank (links).
     */
    void pitSelect(int delta) {
        if (state_ != State::Pit || pitWorkMs_ > 0 || delta == 0) return;
        int pos = 0;
        while (kPitOrder[pos] != pitSel_) ++pos;
        pos = ((pos + delta) % kPitItems + kPitItems) % kPitItems;
        pitSel_ = kPitOrder[pos];
    }
    static constexpr uint8_t kPitOrder[kPitItems] = {FrontLeft, FrontRight, Go, RearRight, RearLeft, Fuel};
    /** Taste: in der Box ausführen, im Ziel neues Rennen. */
    void press() {
        if (state_ == State::Finished) {
            reset();
            return;
        }
        if (state_ != State::Pit || pitWorkMs_ > 0) return;
        if (pitSel_ <= RearRight) {
            pitWorkMs_ = kTireChangeMs;
            workTire_ = pitSel_;
        } else if (pitSel_ == Fuel) {
            fueling_ = !fueling_;
        } else {
            fueling_ = false;
            state_ = State::Racing;
            position_ = kPitExitSegment * kSegmentLength;
            playerX_ = 0.7f;
            speed_ = 0;
        }
    }

    /** Einen Zeitschritt rechnen (dt in Sekunden, normal 1/60). */
    void step(float dt) {
        const uint32_t dtMs = static_cast<uint32_t>(dt * 1000.0f + 0.5f);
        moveCars(dt);
        switch (state_) {
            case State::Countdown:
                countdownMs_ = countdownMs_ > dtMs ? countdownMs_ - dtMs : 0;
                if (countdownMs_ == 0) state_ = State::Racing;
                return;
            case State::Pit:
                raceMs_ += dtMs;
                stepPit(dt, dtMs);
                return;
            case State::Finished:
                return;
            case State::Racing:
                raceMs_ += dtMs;
                stepRacing(dt, dtMs);
                return;
        }
    }

    // --- Abfragen ----------------------------------------------------------------------------------------
    State state() const { return state_; }
    int countdownSeconds() const { return static_cast<int>((countdownMs_ + 999) / 1000); }
    float position() const { return position_; }
    float speed() const { return speed_; }
    int speedKmh() const { return static_cast<int>(speed_ / kMaxSpeed * 280.0f + 0.5f); }
    float playerX() const { return playerX_; }
    float steering() const { return steer_; }
    int lap() const { return lap_; }
    uint32_t raceMs() const { return raceMs_; }
    uint32_t lastLapMs() const { return lastLapMs_; }
    float fuel() const { return fuel_; }
    float tireWear(int i) const { return wear_[i]; }
    bool flatTire() const { return maxWear() >= 1.0f; }
    uint8_t pitSelection() const { return pitSel_; }
    bool pitWorking() const { return pitWorkMs_ > 0; }
    float pitWorkProgress() const { return pitWorkMs_ > 0 ? 1.0f - pitWorkMs_ / static_cast<float>(kTireChangeMs) : 0; }
    int pitWorkTire() const { return workTire_; }
    bool fueling() const { return fueling_; }
    int segmentCount() const { return segmentCount_; }
    float trackLength() const { return segmentCount_ * kSegmentLength; }
    const Segment& segment(int i) const { return segments_[((i % segmentCount_) + segmentCount_) % segmentCount_]; }
    int segmentIndexAt(float z) const { return static_cast<int>(floorf(z / kSegmentLength)) % segmentCount_; }
    const Car& car(int i) const { return cars_[i]; }

    // --- für Tests --------------------------------------------------------------------------------------
    void setPosition(float z) { position_ = z; }
    void setPlayerX(float x) { playerX_ = x; }
    void setSpeed(float s) { speed_ = s; }
    void setFuel(float f) { fuel_ = f; }
    void setWear(int i, float w) { wear_[i] = w; }
    void setLap(int lap) { lap_ = lap; }
    void startRacing() { state_ = State::Racing; countdownMs_ = 0; }
    void parkCarsFarAway() { for (Car& c : cars_) { c.z = trackLength() * 0.5f; c.speed = 0; } }
    void placeCar(int i, float z, float x, float speed) { cars_[i] = Car{z, x, speed, cars_[i].color}; }

private:
    static constexpr float kSteerPerStep = 0.06f;       // ~17 Rohschritte = voller Einschlag
    static constexpr uint32_t kSteerCenterDelayMs = 250;
    static constexpr float kSteerCenterPerSec = 1.6f;   // Lenkrad läuft danach langsam in die Mitte
    static constexpr float kCentrifugal = 0.3f;
    static constexpr float kOffRoadLimit = kMaxSpeed / 4;
    static constexpr uint32_t kTireChangeMs = 1500;
    static constexpr float kFuelPerLap = 0.3f;          // ein voller Tank reicht für gut 3 Runden
    static constexpr float kFuelFillPerSec = 0.25f;

    void buildTrack() {
        segmentCount_ = 0;
        addStraight(50);
        addCurve(25, 25, 25, 2.0f);
        addStraight(30);
        addCurve(25, 50, 25, -4.0f);
        addCurve(15, 20, 15, 6.0f);
        addStraight(40);
        addCurve(20, 20, 20, -3.0f);
        addCurve(20, 20, 20, 3.0f);
        addCurve(50, 100, 50, 2.0f);
        addStraight(60);
        addCurve(25, 30, 25, -6.0f);
        addCurve(20, 40, 20, 4.0f);
        addStraight(25);
        addCurve(20, 25, 20, -2.5f);
        addStraight(60);
        for (int i = 0; i < 3; ++i) segments_[i].flags |= kStartLine;
        for (int i = 4; i < kPitEntrySegment - 2; ++i) segments_[i].flags |= kPitSign;
        for (int i = kPitEntrySegment - 6; i <= kPitEntrySegment; ++i) segments_[i].flags |= kPitStrip;
    }
    void addSegment(float curve) {
        if (segmentCount_ < kMaxSegments) segments_[segmentCount_++] = Segment{curve, 0};
    }
    void addStraight(int n) { for (int i = 0; i < n; ++i) addSegment(0); }
    void addCurve(int enter, int hold, int leave, float curve) {
        // weich hinein und heraus (wie die Originale)
        for (int i = 0; i < enter; ++i) addSegment(curve * easeIn(static_cast<float>(i) / enter));
        for (int i = 0; i < hold; ++i) addSegment(curve);
        for (int i = 0; i < leave; ++i) addSegment(curve * easeInOut(1.0f - static_cast<float>(i) / leave));
    }
    static float easeIn(float t) { return t * t; }
    static float easeInOut(float t) { return -cosf(t * 3.14159265f) / 2 + 0.5f; }

    float maxWear() const {
        float m = 0;
        for (float w : wear_) m = w > m ? w : m;
        return m;
    }

    /** Höchstgeschwindigkeit je nach Reifen und Sprit. */
    float topSpeed() const {
        if (fuel_ <= 0) return kMaxSpeed * 0.15f;      // tropft nur noch zur Box
        if (flatTire()) return kMaxSpeed * 0.4f;       // Platten
        return kMaxSpeed * (1.0f - 0.15f * maxWear()); // abgefahren: etwas langsamer
    }
    float grip() const { return 1.0f - 0.45f * maxWear(); }

    void moveCars(float dt) {
        const float len = trackLength();
        for (Car& c : cars_) {
            c.z += c.speed * dt;
            if (c.z >= len) c.z -= len;
        }
    }

    void stepRacing(float dt, uint32_t dtMs) {
        const float speedPercent = speed_ / kMaxSpeed;
        const float dx = dt * 2.0f * speedPercent;
        const int segIndex = segmentIndexAt(position_ + kPlayerZ);
        const Segment& seg = segments_[segIndex];

        // Lenkrad läuft ohne Eingabe langsam in die Mitte zurück
        wheelIdleMs_ += dtMs;
        if (wheelIdleMs_ > kSteerCenterDelayMs) {
            const float back = kSteerCenterPerSec * dt;
            steer_ = fabsf(steer_) <= back ? 0 : steer_ - (steer_ > 0 ? back : -back);
        }

        // Lenken (mit Grip) und Fliehkraft
        playerX_ += steer_ * dx * 1.3f * grip();
        playerX_ -= dx * speedPercent * seg.curve * kCentrifugal;
        if (playerX_ > 2.5f) playerX_ = 2.5f;
        if (playerX_ < -2.5f) playerX_ = -2.5f;

        // Gas (automatisch) bzw. Bremse
        const float top = topSpeed();
        if (brake_) speed_ -= kMaxSpeed * 1.2f * dt;
        else if (speed_ < top) speed_ += kMaxSpeed / 4.5f * dt;
        if (speed_ > top) speed_ -= kMaxSpeed / 2 * dt;  // z. B. nach Platten: ausrollen
        const bool offRoad = playerX_ < -1.0f || playerX_ > 1.0f;
        if (offRoad && speed_ > kOffRoadLimit) speed_ -= kMaxSpeed * 0.9f * dt;
        if (speed_ < 0) speed_ = 0;

        // Verschleiß: Grundlast je Strecke, mehr in Kurven (außen) und neben der Straße
        const float dist = speed_ * dt / kSegmentLength;  // Segmente
        const float lateral = fabsf(seg.curve) * speedPercent * speedPercent;
        const float base = dist * 0.00022f * (offRoad ? 4.0f : 1.0f);
        const float curveWear = dist * lateral * 0.00012f;
        const bool rightTurn = seg.curve > 0;
        wear_[FrontLeft] += base + (rightTurn ? curveWear : 0) + dist * fabsf(steer_) * 0.00006f;
        wear_[FrontRight] += base + (!rightTurn ? curveWear : 0) + dist * fabsf(steer_) * 0.00006f;
        wear_[RearLeft] += base * 0.8f + (rightTurn ? curveWear : 0) * 0.7f;
        wear_[RearRight] += base * 0.8f + (!rightTurn ? curveWear : 0) * 0.7f;
        for (float& w : wear_) w = w > 1.0f ? 1.0f : w;

        // Sprit
        fuel_ -= speed_ * dt / trackLength() * kFuelPerLap;
        if (fuel_ < 0) fuel_ = 0;

        // Vorwärts, Runden zählen
        const float before = position_;
        position_ += speed_ * dt;
        const float len = trackLength();
        if (position_ >= len) {
            position_ -= len;
            lastLapMs_ = raceMs_;
            ++lap_;
            if (lap_ > kLaps) {
                lap_ = kLaps;
                state_ = State::Finished;
                return;
            }
        }

        collide(dtMs);

        // Boxeneinfahrt: rechts auf dem gelben Streifen über das Einfahrtsegment
        const float entry = kPitEntrySegment * kSegmentLength;
        if (before < entry && position_ >= entry && playerX_ > 0.85f) {
            state_ = State::Pit;
            speed_ = 0;
            pitSel_ = FrontLeft;
            fueling_ = false;
        }
    }

    void collide(uint32_t dtMs) {
        if (collisionCooldownMs_ > dtMs) {
            collisionCooldownMs_ -= dtMs;
            return;
        }
        collisionCooldownMs_ = 0;
        const float len = trackLength();
        const float playerZ = position_ + kPlayerZ;
        for (Car& c : cars_) {
            float gap = c.z - playerZ;
            if (gap < -len / 2) gap += len;
            if (gap > len / 2) gap -= len;
            if (gap < 0 || gap > kSegmentLength * 0.8f) continue;     // nur direkt vor uns
            if (fabsf(c.x - playerX_) > 2 * kCarHalfWidth) continue;
            if (speed_ <= c.speed) continue;
            // Auffahrunfall: abbremsen, leicht zur Seite, Reifen leiden
            speed_ = c.speed * 0.6f;
            playerX_ += playerX_ >= c.x ? 0.12f : -0.12f;
            wear_[FrontLeft] += 0.04f;
            wear_[FrontRight] += 0.04f;
            collisionCooldownMs_ = 600;
            return;
        }
    }

    void stepPit(float dt, uint32_t dtMs) {
        if (pitWorkMs_ > 0) {
            pitWorkMs_ = pitWorkMs_ > dtMs ? pitWorkMs_ - dtMs : 0;
            if (pitWorkMs_ == 0) wear_[workTire_] = 0;
        }
        if (fueling_) {
            fuel_ += kFuelFillPerSec * dt;
            if (fuel_ >= 1.0f) {
                fuel_ = 1.0f;
                fueling_ = false;
            }
        }
    }

public:
    /** Abstand der Kamera zum eigenen Auto (Welteinheiten) – die Anzeige zeichnet es dort. */
    static constexpr float kPlayerZ = 840.0f;

private:
    Segment segments_[kMaxSegments] = {};
    int segmentCount_ = 0;
    Car cars_[kCars] = {};
    State state_ = State::Countdown;
    uint32_t countdownMs_ = 3000;
    float position_ = 0, speed_ = 0, playerX_ = 0, steer_ = 0;
    uint32_t wheelIdleMs_ = 0;
    int lap_ = 1;
    uint32_t raceMs_ = 0, lastLapMs_ = 0;
    float fuel_ = 1.0f;
    float wear_[4] = {};
    uint8_t pitSel_ = Go;
    uint32_t pitWorkMs_ = 0;
    int workTire_ = 0;
    bool fueling_ = false;
    bool brake_ = false;
    uint32_t collisionCooldownMs_ = 0;
};

}  // namespace game
}  // namespace app
