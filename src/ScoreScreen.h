#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Highscores.h"

/**
 * Bestenliste der Spiele (Easteregg): Eingabe der drei Buchstaben und Anzeige der Top 5.
 * Zeichnet direkt über Arduino_GFX, wie die Spiele selbst.
 */
class ScoreScreen {
public:
    /** Nächster Aufruf zeichnet den ganzen Bildschirm neu. */
    void reset() { mode_ = Mode::None; }

    /**
     * Buchstaben eingeben. `value` = erreichte Punkte bzw. Zeit, `rank` = Platz (0 = erster).
     * Zeichnet beim ersten Aufruf alles, danach nur die Buchstaben, wenn sie sich ändern.
     */
    void showEntry(const app::game::InitialsEntry& entry, uint32_t value, bool isTime, int rank);

    /** Top 5 mit dem eigenen Ergebnis darunter; `highlight` = gerade eingetragener Platz (−1 = keiner). */
    void showTable(const char* title, const app::game::HighscoreTable& table, uint32_t value, bool isTime,
                   int highlight);

    /** Punkte bzw. Zeit (m:ss.z) als Text. */
    static void formatValue(uint32_t value, bool isTime, char* out, size_t size);

private:
    enum class Mode : uint8_t { None, Entry, Table };
    Mode mode_ = Mode::None;
    char shown_[4] = {0, 0, 0, 0};
    int shownPos_ = -1;
};
