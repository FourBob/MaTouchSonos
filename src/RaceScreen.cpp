#include "RaceScreen.h"

#include <Arduino_GFX_Library.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "Display.h"

using app::game::RaceGame;

namespace {

constexpr int kCx = 240, kCy = 240;
constexpr int kHorizon = 205;
constexpr int kHudTop = 40, kHudBottom = 116;
constexpr int kMountainTop = 140;
constexpr float kCameraHeight = 1000.0f;
constexpr float kCameraDepth = 0.84f;   // 1/tan(halber Sichtwinkel ≈ 50°)
constexpr float kScreenScale = 240.0f;  // Welteinheiten → Pixel bei Maßstab 1

Arduino_GFX* gfx() { return hal::Display::gfx(); }
uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return gfx()->color565(r, g, b); }

// Farben (RGB565), einmal berechnet
struct Palette {
    uint16_t hud, skyLow, mountain, mountainSnow, grassLight, grassDark, grassFar, rumbleLight, rumbleDark,
        roadLight, roadDark, lane, pit, text, textDim, warn, good, player, playerRoof, wheel;
    uint16_t cars[5];
    uint16_t carRoofs[5];
};
Palette pal;
bool palReady = false;

void initPalette() {
    if (palReady) return;
    pal.hud = rgb(0x10, 0x18, 0x40);
    pal.skyLow = rgb(0x6A, 0xA8, 0xE8);
    pal.mountain = rgb(0x3A, 0x4E, 0x6E);
    pal.mountainSnow = rgb(0xD8, 0xE4, 0xF0);
    pal.grassLight = rgb(0x1C, 0xA0, 0x1C);
    pal.grassDark = rgb(0x12, 0x8A, 0x12);
    pal.grassFar = rgb(0x14, 0x80, 0x14);
    pal.rumbleLight = rgb(0xF0, 0xF0, 0xF0);
    pal.rumbleDark = rgb(0xD0, 0x20, 0x20);
    pal.roadLight = rgb(0x70, 0x70, 0x70);
    pal.roadDark = rgb(0x66, 0x66, 0x66);
    pal.lane = rgb(0xF0, 0xF0, 0xF0);
    pal.pit = rgb(0xF2, 0xC9, 0x4C);
    pal.text = WHITE;
    pal.textDim = rgb(0x9A, 0x9A, 0x9A);
    pal.warn = rgb(0xEB, 0x57, 0x57);
    pal.good = rgb(0x1D, 0xB9, 0x54);
    pal.player = rgb(0xE0, 0x20, 0x20);
    pal.playerRoof = rgb(0x80, 0x10, 0x10);
    pal.wheel = rgb(0x18, 0x18, 0x18);
    const uint8_t c[5][3] = {{0x20, 0x60, 0xE0}, {0xF2, 0xC9, 0x4C}, {0xF0, 0xF0, 0xF0}, {0x9B, 0x51, 0xE0}, {0x20, 0xC0, 0xC0}};
    for (int i = 0; i < 5; ++i) {
        pal.cars[i] = rgb(c[i][0], c[i][1], c[i][2]);
        pal.carRoofs[i] = rgb(c[i][0] / 2, c[i][1] / 2, c[i][2] / 2);
    }
    palReady = true;
}

/** Sichtbarer Bereich einer Bildschirmzeile (runder Bildschirm). */
inline bool chord(int y, int& left, int& right) {
    const float dy = y + 0.5f - kCy;
    const float h2 = 240.0f * 240.0f - dy * dy;
    if (h2 <= 0) return false;
    const int half = static_cast<int>(sqrtf(h2));
    left = kCx - half;
    right = kCx + half;  // einschließlich
    return true;
}

/** Waagerechte Linie [x0, x1) in Zeile y, auf [left, right] begrenzt. */
inline void span(int x0, int x1, int y, int left, int right, uint16_t color) {
    if (x0 < left) x0 = left;
    if (x1 > right + 1) x1 = right + 1;
    if (x1 > x0) gfx()->writeFastHLine(x0, y, x1 - x0, color);
}

/** Rechteck, oberhalb des Horizonts abgeschnitten (dort wird nicht jedes Bild neu gezeichnet). */
void groundRect(int x, int y, int w, int h, uint16_t color) {
    if (y < kHorizon) {
        h -= kHorizon - y;
        y = kHorizon;
    }
    if (y + h > 480) h = 480 - y;
    if (w <= 0 || h <= 0) return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (x + w > 480) w = 480 - x;
    if (w > 0) gfx()->fillRect(x, y, w, h, color);
}

void centeredText(const char* text, int y, uint8_t size, uint16_t fg, uint16_t bg) {
    gfx()->setTextSize(size);
    gfx()->setTextColor(fg, bg);
    const int w = static_cast<int>(strlen(text)) * 6 * size;
    gfx()->setCursor(kCx - w / 2, y);
    gfx()->print(text);
}

void formatTime(uint32_t ms, char* out, size_t size) {
    const uint32_t tenths = ms / 100;
    snprintf(out, size, "%lu:%02lu.%lu", static_cast<unsigned long>(tenths / 600),
             static_cast<unsigned long>((tenths / 10) % 60), static_cast<unsigned long>(tenths % 10));
}

uint16_t wearColor(float w) {
    if (w >= 1.0f) return pal.warn;
    if (w >= 0.8f) return rgb(0xF2, 0x99, 0x4A);
    if (w >= 0.5f) return pal.pit;
    return pal.good;
}

/** Berge: Höhenprofil aus überlagerten Wellen, wiederholt sich nicht sichtbar. */
int mountainTop(int x) {
    const float fx = static_cast<float>(x);
    const float h = 22 * sinf(fx * 0.013f) + 14 * sinf(fx * 0.031f + 1.0f) + 8 * sinf(fx * 0.071f + 2.0f) + 44;
    return kHorizon - 8 - static_cast<int>(h * 0.65f);
}

}  // namespace

void RaceScreen::enter(const RaceGame& race, uint32_t bestMs) {
    initPalette();
    lastState_ = -1;
    frame_ = 0;
    skyOffset_ = 0;
    drawnSkyOffset_ = -100000;
    render(race, bestMs);
}

void RaceScreen::drawSky() {
    gfx()->fillRect(0, 0, 480, kHudBottom, pal.hud);
    // Verlauf vom dunklen Anzeigebereich zum hellen Himmel
    const int steps = kMountainTop - kHudBottom;
    for (int i = 0; i < steps; ++i) {
        const float t = static_cast<float>(i) / steps;
        const uint16_t c = rgb(static_cast<uint8_t>(0x10 + t * (0x6A - 0x10)), static_cast<uint8_t>(0x18 + t * (0xA8 - 0x18)),
                               static_cast<uint8_t>(0x40 + t * (0xE8 - 0x40)));
        gfx()->writeFastHLine(0, kHudBottom + i, 480, c);
    }
    drawnSkyOffset_ = -100000;  // Berge neu
}

void RaceScreen::drawMountains(int offset) {
    for (int x = 0; x < 480; ++x) {
        int top = mountainTop(x + offset);
        if (top < kMountainTop) top = kMountainTop;
        if (top > kMountainTop) gfx()->writeFastVLine(x, kMountainTop, top - kMountainTop, pal.skyLow);
        const int snow = top + 4 < kHorizon ? 4 : 0;
        if (snow && top < kHorizon - 40) gfx()->writeFastVLine(x, top, snow, pal.mountainSnow);
        const int start = (snow && top < kHorizon - 40) ? top + snow : top;
        if (kHorizon > start) gfx()->writeFastVLine(x, start, kHorizon - start, pal.mountain);
    }
    drawnSkyOffset_ = offset;
}

void RaceScreen::drawRoad(const RaceGame& race) {
    const float L = RaceGame::kSegmentLength;
    const float pos = race.position();
    baseSegment_ = race.segmentIndexAt(pos);
    offset_ = fmodf(pos, L);
    camX_ = race.playerX() * RaceGame::kRoadWidth;

    float x = 0;
    float dx = -(race.segment(baseSegment_).curve * (offset_ / L));
    int maxy = 480;
    for (int n = 0; n < kDrawDistance; ++n) {
        const RaceGame::Segment& seg = race.segment(baseSegment_ + n);
        Projected& p = proj_[n];
        float z1 = n * L - offset_;
        const float z2 = z1 + L;
        if (z1 <= kCameraDepth) z1 = kCameraDepth + 0.01f;  // ganz nah: liegt ohnehin unter dem Bildrand
        const float s1 = kCameraDepth / z1, s2 = kCameraDepth / z2;
        p.x1 = kCx + s1 * (x - camX_) * kScreenScale;
        p.y1 = kHorizon + s1 * kCameraHeight * kScreenScale;
        p.w1 = s1 * RaceGame::kRoadWidth * kScreenScale;
        p.x2 = kCx + s2 * (x + dx - camX_) * kScreenScale;
        p.y2 = kHorizon + s2 * kCameraHeight * kScreenScale;
        p.w2 = s2 * RaceGame::kRoadWidth * kScreenScale;
        x += dx;
        dx += seg.curve;
        p.visible = n > 0 && p.y2 < maxy;
        if (!p.visible) continue;

        const int top = static_cast<int>(ceilf(p.y2)) < kHorizon ? kHorizon : static_cast<int>(ceilf(p.y2));
        const int bottom = static_cast<int>(p.y1) + 1 < maxy ? static_cast<int>(p.y1) + 1 : maxy;
        const int absolute = baseSegment_ + n;
        const bool light = (absolute / 3) % 2 == 0;
        const uint16_t grass = light ? pal.grassLight : pal.grassDark;
        const uint16_t rumble = light ? pal.rumbleLight : pal.rumbleDark;
        const uint16_t road = (seg.flags & RaceGame::kStartLine) ? WHITE : (light ? pal.roadLight : pal.roadDark);
        const bool lanes = light && !(seg.flags & RaceGame::kStartLine);
        const bool pit = seg.flags & RaceGame::kPitStrip;
        const float spanY = p.y1 - p.y2;
        for (int y = top; y < bottom; ++y) {
            int left, right;
            if (!chord(y, left, right)) continue;
            const float t = spanY > 0.001f ? (y - p.y2) / spanY : 0;
            const float cx = p.x2 + (p.x1 - p.x2) * t;
            const float w = p.w2 + (p.w1 - p.w2) * t;
            const float r = w / 6;
            const int roadL = static_cast<int>(cx - w), roadR = static_cast<int>(cx + w);
            const int rumL = static_cast<int>(cx - w - r), rumR = static_cast<int>(cx + w + r);
            span(left, rumL, y, left, right, grass);
            span(rumL, roadL, y, left, right, rumble);
            if (lanes) {
                const float l = w / 32 < 1 ? 1 : w / 32;
                const int m1 = static_cast<int>(cx - w / 3), m2 = static_cast<int>(cx + w / 3);
                const int hl = static_cast<int>(l / 2 + 0.5f);
                span(roadL, m1 - hl, y, left, right, road);
                span(m1 - hl, m1 + hl + 1, y, left, right, pal.lane);
                span(m1 + hl + 1, m2 - hl, y, left, right, road);
                span(m2 - hl, m2 + hl + 1, y, left, right, pal.lane);
                span(m2 + hl + 1, roadR, y, left, right, road);
            } else {
                span(roadL, roadR, y, left, right, road);
            }
            span(roadR, rumR, y, left, right, rumble);
            if (pit) {
                const int pitR = static_cast<int>(cx + w + r + w * 0.4f);
                span(rumR, pitR, y, left, right, pal.pit);
                span(pitR, right + 1, y, left, right, grass);
            } else {
                span(rumR, right + 1, y, left, right, grass);
            }
        }
        maxy = top;
        if (maxy <= kHorizon) break;
    }
    // Rest bis zum Horizont (jenseits der Sichtweite): Gras
    for (int y = kHorizon; y < maxy; ++y) {
        int left, right;
        if (chord(y, left, right)) gfx()->writeFastHLine(left, y, right - left + 1, pal.grassFar);
    }
}

void RaceScreen::drawCars(const RaceGame& race) {
    const float L = RaceGame::kSegmentLength;
    const float len = race.trackLength();
    const float pos = race.position();

    // Schilder „Box“ rechts neben der Straße (gelbe Tafeln)
    for (int n = kDrawDistance - 1; n >= 1; --n) {
        const Projected& p = proj_[n];
        if (!p.visible) continue;
        const RaceGame::Segment& seg = race.segment(baseSegment_ + n);
        if (!(seg.flags & RaceGame::kPitSign) || (baseSegment_ + n) % 4 != 0) continue;
        const float w = p.w1;
        const int sx = static_cast<int>(p.x1 + w * 1.45f);
        const int sw = static_cast<int>(w * 0.22f) + 1;
        const int sh = static_cast<int>(w * 0.16f) + 1;
        const int sy = static_cast<int>(p.y1);
        groundRect(sx, sy - sh * 3, sw, sh, pal.pit);
        groundRect(sx + sw / 2 - sw / 10, sy - sh * 2, sw / 5 + 1, sh * 2, pal.wheel);
        if (sh > 6) groundRect(sx + sw / 2 + 2, sy - sh * 3 + sh / 3, sw / 3, sh / 3 + 1, pal.wheel);  // Pfeil-Andeutung
    }

    // Gegner von hinten nach vorn
    int order[RaceGame::kCars];
    float dist[RaceGame::kCars];
    int count = 0;
    for (int i = 0; i < RaceGame::kCars; ++i) {
        float dz = race.car(i).z - pos;
        while (dz < 0) dz += len;
        while (dz >= len) dz -= len;
        if (dz < RaceGame::kPlayerZ * 0.6f || dz > (kDrawDistance - 2) * L) continue;
        int k = count++;
        while (k > 0 && dist[k - 1] < dz) {
            order[k] = order[k - 1];
            dist[k] = dist[k - 1];
            --k;
        }
        order[k] = i;
        dist[k] = dz;
    }
    for (int k = 0; k < count; ++k) {
        const RaceGame::Car& c = race.car(order[k]);
        const float rel = dist[k] + offset_;
        const int n = static_cast<int>(rel / L);
        if (n < 1 || n >= kDrawDistance || !proj_[n].visible) continue;
        const Projected& p = proj_[n];
        const float f = (rel - n * L) / L;
        const float sx = p.x1 + (p.x2 - p.x1) * f;
        const float sy = p.y1 + (p.y2 - p.y1) * f;
        const float sw = p.w1 + (p.w2 - p.w1) * f;
        const float hw = sw * RaceGame::kCarHalfWidth;
        if (hw < 1.0f || sy <= kHorizon) continue;
        const int cx = static_cast<int>(sx + c.x * sw);
        const int y = static_cast<int>(sy);
        const int w = static_cast<int>(hw * 2) + 1;
        const int h = static_cast<int>(hw * 0.8f) + 1;
        const int x0 = cx - w / 2;
        groundRect(x0 - w / 12, y - h / 3, w / 5 + 1, h / 3 + 1, pal.wheel);          // Räder
        groundRect(x0 + w - w / 5 + w / 12, y - h / 3, w / 5 + 1, h / 3 + 1, pal.wheel);
        groundRect(x0, y - h, w, h * 2 / 3 + 1, pal.cars[c.color % 5]);                   // Karosserie
        groundRect(x0 + w / 5, y - h - h / 2, w * 3 / 5 + 1, h / 2 + 1, pal.carRoofs[c.color % 5]);  // Dach
        if (w >= 12) {
            groundRect(x0 + 2, y - h + 2, w / 6 + 1, h / 5 + 1, pal.warn);                // Rücklichter
            groundRect(x0 + w - 2 - w / 6, y - h + 2, w / 6 + 1, h / 5 + 1, pal.warn);
        }
    }
}

void RaceScreen::drawPlayer(const RaceGame& race) {
    // Eigenes Auto in der Mitte unten (die Straße bewegt sich darunter)
    const int bounce = (fabsf(race.playerX()) > 1.0f && race.speed() > 0 && (frame_ & 2)) ? 2 : 0;
    const int lean = static_cast<int>(race.steering() * 5);
    const int base = 446 + bounce;
    const int hw = 54, h = 44;
    const int x0 = kCx - hw + lean;
    groundRect(x0 - 8, base - 22, 20, 24, pal.wheel);                 // Hinterräder
    groundRect(x0 + 2 * hw - 12, base - 22, 20, 24, pal.wheel);
    groundRect(x0, base - h, 2 * hw, h - 6, pal.player);              // Karosserie
    groundRect(x0 + 18 + lean, base - h - 22, 2 * hw - 36, 24, pal.playerRoof);  // Cockpit/Fahrer
    groundRect(x0 + 6, base - h + 6, 16, 8, pal.warn);                // Rücklichter
    groundRect(x0 + 2 * hw - 22, base - h + 6, 16, 8, pal.warn);
    groundRect(x0 - 4, base - h - 4, 2 * hw + 8, 6, pal.wheel);       // Heckflügel
}

void RaceScreen::drawHud(const RaceGame& race, bool force) {
    if (!force && frame_ % 6 != 0) return;
    char line[24], t[12];
    formatTime(race.raceMs(), t, sizeof(t));
    snprintf(line, sizeof(line), "R%d/%d  %s", race.lap(), RaceGame::kLaps, t);
    centeredText(line, 52, 2, pal.text, pal.hud);

    const int seg = race.segmentIndexAt(race.position() + RaceGame::kPlayerZ);
    const bool boxAhead = race.segment(seg).flags & (RaceGame::kPitSign | RaceGame::kPitStrip);
    if (race.fuel() <= 0) snprintf(line, sizeof(line), " TANK LEER ");
    else if (race.flatTire()) snprintf(line, sizeof(line), "  PLATTEN  ");
    else if (boxAhead) snprintf(line, sizeof(line), "  BOX  >>  ");
    else snprintf(line, sizeof(line), " %3d KM/H  ", race.speedKmh());
    const bool alarm = race.fuel() <= 0 || race.flatTire();
    centeredText(line, 74, 2, alarm ? pal.warn : (boxAhead ? pal.pit : pal.text), pal.hud);

    // Sprit (Balken) und Reifen (2 × 2 Kästchen)
    const int fx = 168, fy = 98, fw = 70, fh = 9;
    gfx()->drawRect(fx, fy, fw, fh, pal.textDim);
    const int fill = static_cast<int>((fw - 2) * race.fuel());
    gfx()->fillRect(fx + 1, fy + 1, fill, fh - 2, race.fuel() < 0.15f ? pal.warn : pal.good);
    gfx()->fillRect(fx + 1 + fill, fy + 1, fw - 2 - fill, fh - 2, pal.hud);
    const int tx = 262, ty = 94;
    gfx()->fillRect(tx, ty, 8, 7, wearColor(race.tireWear(RaceGame::FrontLeft)));
    gfx()->fillRect(tx + 14, ty, 8, 7, wearColor(race.tireWear(RaceGame::FrontRight)));
    gfx()->fillRect(tx, ty + 10, 8, 7, wearColor(race.tireWear(RaceGame::RearLeft)));
    gfx()->fillRect(tx + 14, ty + 10, 8, 7, wearColor(race.tireWear(RaceGame::RearRight)));
}

void RaceScreen::drawCountdown(const RaceGame& race) {
    const int s = race.countdownSeconds();
    if (s == lastCountdown_) return;
    lastCountdown_ = s;
    gfx()->fillRect(120, kHudTop + 4, 240, kHudBottom - kHudTop - 8, pal.hud);
    char text[12];
    snprintf(text, sizeof(text), "%d", s);
    centeredText(text, 52, 6, s == 1 ? pal.pit : pal.text, pal.hud);
}

// --- Box ----------------------------------------------------------------------------------------

namespace {
struct PitSpot {
    int x, y;
};
// Lage der Einträge (Mittelpunkte), Index = RaceGame::PitItem
const PitSpot kPitSpots[RaceGame::kPitItems] = {
    {176, 178},  // vorne links
    {304, 178},  // vorne rechts
    {176, 302},  // hinten links
    {304, 302},  // hinten rechts
    {92, 240},   // Tank
    {388, 240},  // LOS
};
constexpr int kTireW = 30, kTireH = 50;

void pitFrame(int item, uint16_t color) {
    const PitSpot& s = kPitSpots[item];
    if (item <= RaceGame::RearRight) {
        for (int i = 0; i < 3; ++i)
            gfx()->drawRect(s.x - kTireW / 2 - 5 - i, s.y - kTireH / 2 - 5 - i, kTireW + 10 + 2 * i, kTireH + 10 + 2 * i, color);
    } else {
        for (int i = 0; i < 3; ++i) gfx()->drawCircle(s.x, s.y, 40 + i, color);
    }
}
}  // namespace

void RaceScreen::drawPitStatic(const RaceGame& race) {
    gfx()->fillScreen(BLACK);
    centeredText("BOX", 34, 3, pal.pit, BLACK);
    // Auto von oben
    gfx()->fillRect(kCx - 56, 140, 112, 14, rgb(0x50, 0x50, 0x50));       // Frontflügel
    gfx()->fillRoundRect(kCx - 34, 150, 68, 175, 20, pal.player);          // Rumpf
    gfx()->fillRoundRect(kCx - 18, 228, 36, 42, 10, pal.playerRoof);       // Cockpit
    gfx()->fillCircle(kCx, 246, 10, rgb(0xF0, 0xF0, 0xF0));               // Helm
    gfx()->fillRect(kCx - 52, 318, 104, 16, rgb(0x50, 0x50, 0x50));       // Heckflügel
    // Tank-Symbol und LOS
    const PitSpot& f = kPitSpots[RaceGame::Fuel];
    gfx()->fillRoundRect(f.x - 14, f.y - 30, 28, 34, 4, pal.warn);
    gfx()->fillRect(f.x + 8, f.y - 38, 6, 10, pal.warn);
    const PitSpot& g = kPitSpots[RaceGame::Go];
    gfx()->fillCircle(g.x, g.y, 30, pal.good);
    gfx()->setTextSize(2);
    gfx()->setTextColor(BLACK, pal.good);
    gfx()->setCursor(g.x - 18, g.y - 7);
    gfx()->print("LOS");
    centeredText("RING: WAEHLEN", 392, 2, pal.textDim, BLACK);
    centeredText("DRUECKEN: OK", 414, 2, pal.textDim, BLACK);
    lastPitSel_ = -1;
    for (float& w : lastPitWear_) w = -1;
    lastPitWorking_ = false;
    (void)race;
}

void RaceScreen::drawPitItems(const RaceGame& race) {
    // Reifen in der Farbe ihres Zustands
    for (int i = 0; i <= RaceGame::RearRight; ++i) {
        const float w = race.tireWear(i);
        if (w == lastPitWear_[i]) continue;
        lastPitWear_[i] = w;
        const PitSpot& s = kPitSpots[i];
        gfx()->fillRoundRect(s.x - kTireW / 2, s.y - kTireH / 2, kTireW, kTireH, 6, wearColor(w));
        gfx()->drawRoundRect(s.x - kTireW / 2, s.y - kTireH / 2, kTireW, kTireH, 6, pal.wheel);
    }
    const int sel = race.pitSelection();
    if (sel != lastPitSel_) {
        if (lastPitSel_ >= 0) pitFrame(lastPitSel_, BLACK);
        pitFrame(sel, WHITE);
        lastPitSel_ = sel;
    }
}

void RaceScreen::drawPitGauges(const RaceGame& race) {
    // Tankanzeige unter dem Kanister
    const PitSpot& f = kPitSpots[RaceGame::Fuel];
    const int gx = f.x - 10, gy = f.y + 12, gw = 20, gh = 64;
    gfx()->drawRect(gx, gy, gw, gh, pal.textDim);
    const int fill = static_cast<int>((gh - 2) * race.fuel());
    gfx()->fillRect(gx + 1, gy + 1, gw - 2, gh - 2 - fill, BLACK);
    gfx()->fillRect(gx + 1, gy + gh - 1 - fill, gw - 2, fill, race.fueling() ? pal.pit : pal.good);

    // Arbeit am Reifen bzw. Tanken, Zeit
    char line[24];
    if (race.pitWorking()) snprintf(line, sizeof(line), " WECHSEL %3d%% ", static_cast<int>(race.pitWorkProgress() * 100));
    else if (race.fueling()) snprintf(line, sizeof(line), "  TANKEN %3d%% ", static_cast<int>(race.fuel() * 100));
    else snprintf(line, sizeof(line), "              ");
    centeredText(line, 352, 2, pal.pit, BLACK);
    char t[12];
    formatTime(race.raceMs(), t, sizeof(t));
    snprintf(line, sizeof(line), " ZEIT %s ", t);
    centeredText(line, 74, 2, pal.text, BLACK);
}

void RaceScreen::drawFinish(const RaceGame& race, uint32_t bestMs) {
    gfx()->fillScreen(BLACK);
    centeredText("ZIEL!", 130, 4, pal.good, BLACK);
    char t[16];
    formatTime(race.raceMs(), t, sizeof(t));
    centeredText(t, 190, 4, pal.text, BLACK);
    char line[32];
    if (bestMs == 0 || race.raceMs() <= bestMs) {
        centeredText("NEUE BESTZEIT!", 255, 2, pal.pit, BLACK);
    } else {
        formatTime(bestMs, t, sizeof(t));
        snprintf(line, sizeof(line), "BESTZEIT %s", t);
        centeredText(line, 255, 2, pal.textDim, BLACK);
    }
    centeredText("DRUECKEN: NOCHMAL", 320, 2, pal.textDim, BLACK);
    centeredText("LANG: ENDE", 344, 2, pal.textDim, BLACK);
}

void RaceScreen::render(const RaceGame& race, uint32_t bestMs) {
    ++frame_;
    gfx()->startWrite();
    const int state = static_cast<int>(race.state());
    const bool changed = state != lastState_;
    if (changed) {
        switch (race.state()) {
            case RaceGame::State::Countdown:
            case RaceGame::State::Racing:
                drawSky();
                lastCountdown_ = -1;
                break;
            case RaceGame::State::Pit:
                drawPitStatic(race);
                break;
            case RaceGame::State::Finished:
                finishDrawn_ = false;
                break;
        }
        lastState_ = state;
    }

    switch (race.state()) {
        case RaceGame::State::Countdown:
        case RaceGame::State::Racing: {
            const int seg = race.segmentIndexAt(race.position() + RaceGame::kPlayerZ);
            skyOffset_ += race.segment(seg).curve * (race.speed() / RaceGame::kMaxSpeed) * 1.2f;
            const int off = static_cast<int>(skyOffset_);
            if (off != drawnSkyOffset_) drawMountains(off);
            drawRoad(race);
            drawCars(race);
            drawPlayer(race);
            if (race.state() == RaceGame::State::Countdown) drawCountdown(race);
            else drawHud(race, changed || lastCountdown_ >= 0);
            if (race.state() == RaceGame::State::Racing && lastCountdown_ >= 0) {
                lastCountdown_ = -1;
                gfx()->fillRect(120, kHudTop + 4, 240, kHudBottom - kHudTop - 8, pal.hud);
                drawHud(race, true);
            }
            break;
        }
        case RaceGame::State::Pit:
            drawPitItems(race);
            if (changed || race.pitWorking() != lastPitWorking_ || frame_ % 6 == 0) drawPitGauges(race);
            lastPitWorking_ = race.pitWorking();
            break;
        case RaceGame::State::Finished:
            if (!finishDrawn_) {
                drawFinish(race, bestMs);
                finishDrawn_ = true;
            }
            break;
    }
    gfx()->endWrite();
}
