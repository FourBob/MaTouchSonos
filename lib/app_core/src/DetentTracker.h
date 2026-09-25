#pragma once

#include <stdint.h>

namespace app {

/**
 * Rechnet die Position eines Hardware-Encoderzählers in Rastungen („Klicks“) um.
 *
 * Eingabe ist die fortlaufende Schrittzahl (4 Schritte pro Rastung bei Vollschritt-
 * Encodern, 2 bei Halbschritt) und die Information, ob der Encoder gerade in einer
 * Ruhelage steht (dort, wo er mechanisch einrastet).
 *
 * Ausgewertet wird nur in der Ruhelage: Die Schritte seit der letzten Ruhelage werden
 * auf ganze Rastungen gerundet, danach wird neu synchronisiert. Dadurch
 *  - zählt ein halb gedrehter und zurückgedrehter Knopf nicht,
 *  - kann sich kein Versatz ansammeln (einzelne verlorene Schritte werden beim nächsten
 *    Einrasten korrigiert),
 *  - ist es egal, wie selten abgefragt wird: Der Hardware-Zähler verliert nichts, und
 *    beim nächsten Abfragen in Ruhelage kommen alle Klicks auf einmal an.
 *
 * Reine Logik ohne Hardwarezugriff – auf dem PC unit-testbar.
 */
class DetentTracker {
public:
    explicit DetentTracker(bool halfStep = false) : stepsPerDetent_(halfStep ? 2 : 4) {}

    /** Startposition setzen (Encoder steht beim Start in Ruhelage). */
    void reset(int32_t position) {
        restPosition_ = position;
        initialized_ = true;
    }

    /**
     * @param position Fortlaufende Schrittzahl des Zählers.
     * @param atRest   true, wenn beide Signale gerade die Ruhelage anzeigen.
     * @return Rastungen seit dem letzten Aufruf (positiv = eine Richtung).
     */
    int32_t update(int32_t position, bool atRest) {
        if (!initialized_) {
            reset(position);
            return 0;
        }
        if (!atRest) return 0;

        const int32_t delta = position - restPosition_;
        restPosition_ = position;  // in jeder Ruhelage neu synchronisieren

        // Auf die nächste ganze Rastung runden (bei Vollschritt: ab 2 von 4 Schritten).
        const int32_t half = stepsPerDetent_ / 2;
        return delta >= 0 ? (delta + half) / stepsPerDetent_ : -((-delta + half) / stepsPerDetent_);
    }

private:
    int32_t stepsPerDetent_;
    int32_t restPosition_ = 0;
    bool initialized_ = false;
};

}  // namespace app
