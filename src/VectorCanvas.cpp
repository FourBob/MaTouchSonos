#include "VectorCanvas.h"

#include <Arduino_GFX_Library.h>

#include <cmath>

#include "Display.h"

VectorCanvas& VectorCanvas::shared() {
    static VectorCanvas canvas;
    return canvas;
}

void VectorCanvas::clear() {
    curCount_ = 0;
    prevCount_ = 0;
}

void VectorCanvas::begin() { curCount_ = 0; }

void VectorCanvas::line(float x0, float y0, float x1, float y1, uint16_t color) {
    if (curCount_ >= kMaxLines) return;
    cur_[curCount_++] = Line{static_cast<int16_t>(lroundf(x0)), static_cast<int16_t>(lroundf(y0)),
                             static_cast<int16_t>(lroundf(x1)), static_cast<int16_t>(lroundf(y1)), color};
}

void VectorCanvas::polyline(const float* xy, int points, bool closed, uint16_t color) {
    for (int i = 0; i + 1 < points; ++i) line(xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], color);
    if (closed && points > 2) line(xy[2 * points - 2], xy[2 * points - 1], xy[0], xy[1], color);
}

void VectorCanvas::finish() {
    Arduino_GFX* gfx = hal::Display::gfx();
    // Die Listen entstehen jedes Bild in derselben Reihenfolge – meist steht dieselbe Linie an
    // derselben Stelle. Nur wenn nicht, wird gesucht.
    for (int i = 0; i < prevCount_; ++i) {
        const Line& p = prev_[i];
        bool still = i < curCount_ && cur_[i] == p;
        for (int j = 0; !still && j < curCount_; ++j) still = cur_[j] == p;
        if (!still) gfx->drawLine(p.x0, p.y0, p.x1, p.y1, BLACK);
    }
    for (int i = 0; i < curCount_; ++i) {
        const Line& c = cur_[i];
        gfx->drawLine(c.x0, c.y0, c.x1, c.y1, c.color);
    }
    Line* t = prev_;
    prev_ = cur_;
    cur_ = t;
    prevCount_ = curCount_;
    curCount_ = 0;
}
