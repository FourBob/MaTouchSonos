#pragma once

#include <stdint.h>

namespace app {

/**
 * Energiesparen: Display hell → gedimmt → aus, und Aufwachen ohne Nebenwirkung.
 *
 *   Aktiv ──30 s ohne Eingabe──▶ Gedimmt ──2 min ohne Eingabe, nichts spielt──▶ Aus
 *     ▲                            │                                              │
 *     └──────── Eingabe ───────────┘ (Eingabe wird ausgeführt)                    │
 *     └──────── Eingabe ──────────────────────────────────────────────────────────┘
 *               (weckt nur – diese und alle Eingaben der nächsten `wakeGraceMs` werden verworfen,
 *                damit der Weck-Dreh nicht die Lautstärke ändert)
 *
 * Solange Musik spielt, geht das Display nicht aus, sondern bleibt gedimmt: Das Cover bleibt auf dem
 * Couchtisch sichtbar. Startet die Wiedergabe (z. B. aus der Sonos-App), während es aus ist, wird es
 * gedimmt eingeschaltet.
 *
 * Reine Logik, auf dem PC getestet. Der Aufrufer setzt level() als Helligkeit.
 */
class IdleController {
public:
    enum class State : uint8_t { Active, Dimmed, Off };

    struct Config {
        uint32_t dimAfterMs = 30000;
        uint32_t offAfterMs = 120000;
        uint32_t wakeGraceMs = 600;
        uint8_t activeLevel = 255;
        uint8_t dimLevel = 40;
    };

    IdleController() : IdleController(Config{}) {}
    explicit IdleController(const Config& config) : cfg_(config) {}

    /**
     * Eine Eingabe (Ring, Taste, Touch) ist eingegangen.
     * @return true = ausführen; false = sie hat nur geweckt (oder kam kurz nach dem Wecken) → verwerfen
     */
    bool onInput(uint32_t nowMs) {
        lastInputMs_ = nowMs;
        if (state_ == State::Off) {
            state_ = State::Active;
            wokeAtMs_ = nowMs;
            waking_ = true;
            changed_ = true;
            return false;
        }
        if (waking_ && nowMs - wokeAtMs_ < cfg_.wakeGraceMs) return false;
        waking_ = false;
        if (state_ != State::Active) {
            state_ = State::Active;
            changed_ = true;
        }
        return true;
    }

    /**
     * Regelmäßig aufrufen. @param playing Musik läuft (dann höchstens gedimmt, nie aus)
     * @return true, wenn sich die Helligkeit geändert hat (seit dem letzten Aufruf, auch durch onInput)
     */
    bool tick(uint32_t nowMs, bool playing) {
        const uint32_t idle = nowMs - lastInputMs_;
        State target = State::Active;
        if (idle >= cfg_.dimAfterMs) target = State::Dimmed;
        if (idle >= cfg_.offAfterMs && !playing) target = State::Off;
        if (target != state_) {
            state_ = target;
            changed_ = true;
        }
        const bool changed = changed_;
        changed_ = false;
        return changed;
    }

    State state() const { return state_; }
    uint8_t level() const {
        return state_ == State::Active ? cfg_.activeLevel : (state_ == State::Dimmed ? cfg_.dimLevel : 0);
    }

private:
    Config cfg_;
    State state_ = State::Active;
    uint32_t lastInputMs_ = 0;
    uint32_t wokeAtMs_ = 0;
    bool waking_ = false;
    bool changed_ = false;
};

}  // namespace app
