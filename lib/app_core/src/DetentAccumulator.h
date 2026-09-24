#pragma once

#include <stdint.h>

namespace app {

/**
 * Fasst Einzelschritte des Quadratur-Decoders zu Rastungen („Klicks“) zusammen.
 *
 * Die meisten Encoder erzeugen pro spürbarer Rastung 4 Zustandswechsel. Erst wenn
 * eine volle Rastung zusammengekommen ist, wird ein Ereignis gemeldet – so zählt
 * ein halb gedrehter und zurückgedrehter Knopf nicht.
 */
class DetentAccumulator {
public:
    /**
     * @param stepsPerDetent Zustandswechsel pro Rastung (typisch 4, manche Encoder 2).
     * @param invert         Drehrichtung umkehren, falls der Encoder anders verdrahtet ist.
     */
    explicit DetentAccumulator(int8_t stepsPerDetent = 4, bool invert = false)
        : stepsPerDetent_(stepsPerDetent > 0 ? stepsPerDetent : 1), invert_(invert) {}

    /**
     * Nimmt Rohschritte auf (z. B. die seit dem letzten Aufruf gezählten).
     * @return Anzahl vollständiger Rastungen (positiv = Uhrzeigersinn).
     */
    int32_t add(int32_t steps) {
        pending_ += invert_ ? -steps : steps;
        const int32_t detents = pending_ / stepsPerDetent_;  // rundet Richtung 0
        pending_ -= detents * stepsPerDetent_;
        return detents;
    }

    /** Verwirft angefangene Rastungen (z. B. beim Moduswechsel). */
    void clear() { pending_ = 0; }

    int32_t pending() const { return pending_; }

private:
    int32_t stepsPerDetent_;
    bool invert_;
    int32_t pending_ = 0;
};

}  // namespace app
