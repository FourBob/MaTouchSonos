#pragma once

#include <stdint.h>

/**
 * Vektorgrafik wie an den alten Automaten: Jedes Bild ist eine Liste von Linien.
 *
 * Pro Bild werden nur die Linien des letzten Bildes gelöscht, die es jetzt nicht mehr gibt, und
 * danach alle aktuellen Linien gezeichnet (auch die unveränderten – falls das Löschen einer
 * kreuzenden Linie Lücken gerissen hat). Unveränderte Linien flackern dadurch nicht.
 * Zeichnet direkt über Arduino_GFX; Hintergrund schwarz.
 */
class VectorCanvas {
public:
    static constexpr int kMaxLines = 480;

    /** Gemeinsame Linienliste aller Vektorspiele (es läuft immer nur eines; spart Speicher). */
    static VectorCanvas& shared();

    /** Alles vergessen (nach fillScreen o. Ä.): das nächste Bild zeichnet nur neu, löscht nichts. */
    void clear();

    void begin();  ///< neues Bild beginnen
    void line(float x0, float y0, float x1, float y1, uint16_t color);
    void polyline(const float* xy, int points, bool closed, uint16_t color);
    void finish();  ///< löschen, was weg ist, und zeichnen (innerhalb startWrite/endWrite)

private:
    struct Line {
        int16_t x0, y0, x1, y1;
        uint16_t color;
        bool operator==(const Line& o) const {
            return x0 == o.x0 && y0 == o.y0 && x1 == o.x1 && y1 == o.y1 && color == o.color;
        }
    };
    Line a_[kMaxLines];
    Line b_[kMaxLines];
    Line* cur_ = a_;
    Line* prev_ = b_;
    int curCount_ = 0;
    int prevCount_ = 0;
};
