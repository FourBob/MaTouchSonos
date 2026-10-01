#include "TubeScreen.h"

#include <Arduino_GFX_Library.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "Display.h"

using app::game::TubeGame;

namespace {

constexpr float kCx = 240, kCy = 240;
constexpr float kRimR = 205;      // Rand der Röhre (Kreisform)
constexpr float kDepth = 6.0f;    // Perspektive: hinten = 1/(1+kDepth) der Randgröße
constexpr int kLanes = TubeGame::kLanes;

Arduino_GFX* gfx() { return hal::Display::gfx(); }
uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return gfx()->color565(r, g, b); }

void centeredText(const char* text, int y, uint8_t size, uint16_t fg) {
    gfx()->setTextSize(size);
    gfx()->setTextColor(fg, BLACK);
    const int w = static_cast<int>(strlen(text)) * 6 * size;
    gfx()->setCursor(static_cast<int>(kCx) - w / 2, y);
    gfx()->print(text);
}

/** Randradius der Form in Richtung `a` (Bogenmaß, Bildschirm: 0 = rechts, im Uhrzeigersinn). */
float rimRadius(int shape, float a) {
    switch (shape) {
        case 1: {  // Quadrat
            const float m = fabsf(cosf(a)) > fabsf(sinf(a)) ? fabsf(cosf(a)) : fabsf(sinf(a));
            return 146.0f / m;
        }
        case 2: return kRimR * (0.80f + 0.20f * cosf(5 * (a + 1.5707963f)));  // Stern, Spitze oben
        case 3: return kRimR * (0.90f + 0.10f * cosf(8 * a));                 // Blume
        case 4: return kRimR * (0.84f + 0.16f * cosf(3 * (a + 1.5707963f)));  // Dreieck, Spitze oben
        default: return kRimR;                                                  // Kreis
    }
}

}  // namespace

void TubeScreen::buildRim(int shape) {
    // Kante e liegt bei 90° + (e − ½)·22,5°: Bahn 0 ist unten, Bahnen zählen im Uhrzeigersinn
    for (int e = 0; e <= kLanes; ++e) {
        const float a = (90.0f + (e - 0.5f) * 360.0f / kLanes) * 0.017453292f;
        const float r = rimRadius(shape, a);
        rimX_[e] = cosf(a) * r;
        rimY_[e] = sinf(a) * r;
    }
    shape_ = shape;
}

TubeScreen::Pt TubeScreen::point(float edge, float z) const {
    while (edge < 0) edge += kLanes;
    while (edge >= kLanes) edge -= kLanes;
    const int e = static_cast<int>(edge);
    const float f = edge - e;
    const float x = rimX_[e] + (rimX_[e + 1] - rimX_[e]) * f;
    const float y = rimY_[e] + (rimY_[e + 1] - rimY_[e]) * f;
    const float d = (1.0f - z) - cam_;  // Abstand zur Kamera
    if (d < -0.001f) return Pt{0, 0, false};
    const float s = 1.0f / (1.0f + kDepth * (d < 0 ? 0 : d));
    return Pt{kCx + x * s, kCy + y * s, true};
}

void TubeScreen::enter(const TubeGame& game, uint32_t best) {
    state_ = -1;
    level_ = -1;
    render(game, best);
}

void TubeScreen::drawTube(const TubeGame& game, uint16_t color, uint16_t laneColor) {
    const float top = 1.0f - cam_;  // vorderste sichtbare Ebene
    const int pl = game.playerLane();
    for (int e = 0; e < kLanes; ++e) {
        const bool laneEdge = game.playerVisible() && (e == pl || e == (pl + 1) % kLanes);
        const Pt a = point(static_cast<float>(e), 0), b = point(static_cast<float>(e), top);
        const Pt c = point(static_cast<float>(e + 1), 0), d = point(static_cast<float>(e + 1), top);
        if (a.ok && b.ok) canvas_.line(a.x, a.y, b.x, b.y, laneEdge ? laneColor : color);   // Längskante
        if (b.ok && d.ok) canvas_.line(b.x, b.y, d.x, d.y, e == pl && game.playerVisible() ? laneColor : color);  // Rand
        if (a.ok && c.ok) canvas_.line(a.x, a.y, c.x, c.y, color);                            // hinten
    }
}

void TubeScreen::drawEnemies(const TubeGame& game) {
    const uint16_t flipper = rgb(0xEB, 0x30, 0x60), tanker = rgb(0xB0, 0x60, 0xF0), spiker = rgb(0x30, 0xE0, 0x60);
    // Stacheln
    for (int l = 0; l < kLanes; ++l) {
        const float h = game.spike(l);
        if (h <= 0) continue;
        const Pt a = point(l + 0.5f, 0), b = point(l + 0.5f, h);
        if (a.ok && b.ok) canvas_.line(a.x, a.y, b.x, b.y, spiker);
    }
    for (int i = 0; i < TubeGame::kMaxEnemies; ++i) {
        const TubeGame::Enemy& e = game.enemy(i);
        if (!e.alive) continue;
        const float l = game.enemyLane(e), z = e.z;
        switch (e.type) {
            case TubeGame::Type::Flipper: {
                // „Fliege“: zwei Dreiecke, Spitze an Spitze
                const Pt a = point(l, z - 0.03f), b = point(l + 1, z + 0.03f);
                const Pt c = point(l + 1, z - 0.03f), d = point(l, z + 0.03f);
                if (!(a.ok && b.ok && c.ok && d.ok)) break;
                canvas_.line(a.x, a.y, b.x, b.y, flipper);
                canvas_.line(b.x, b.y, c.x, c.y, flipper);
                canvas_.line(c.x, c.y, d.x, d.y, flipper);
                canvas_.line(d.x, d.y, a.x, a.y, flipper);
                break;
            }
            case TubeGame::Type::Tanker: {
                const Pt t = point(l + 0.5f, z + 0.045f), r = point(l + 0.85f, z), b = point(l + 0.5f, z - 0.045f),
                         lf = point(l + 0.15f, z);
                if (!(t.ok && r.ok && b.ok && lf.ok)) break;
                canvas_.line(t.x, t.y, r.x, r.y, tanker);
                canvas_.line(r.x, r.y, b.x, b.y, tanker);
                canvas_.line(b.x, b.y, lf.x, lf.y, tanker);
                canvas_.line(lf.x, lf.y, t.x, t.y, tanker);
                canvas_.line(t.x, t.y, b.x, b.y, tanker);
                canvas_.line(lf.x, lf.y, r.x, r.y, tanker);
                break;
            }
            case TubeGame::Type::Spiker: {
                const Pt c = point(l + 0.5f, z), r = point(l + 0.8f, z);
                if (!(c.ok && r.ok)) break;
                const float s = fabsf(r.x - c.x) + fabsf(r.y - c.y) + 2;
                const float a = (frame_ % 24) * 0.26f;
                for (int k = 0; k < 4; ++k) {  // drehendes Kreuz
                    const float aa = a + k * 1.5707963f;
                    canvas_.line(c.x, c.y, c.x + cosf(aa) * s, c.y + sinf(aa) * s, spiker);
                }
                break;
            }
        }
    }
}

void TubeScreen::drawPlayer(const TubeGame& game) {
    if (!game.playerVisible()) return;
    const uint16_t yellow = rgb(0xF2, 0xE0, 0x30);
    const float l = static_cast<float>(game.playerLane()), z = game.playerZ();
    const Pt L = point(l, z), R = point(l + 1, z), m1 = point(l + 0.2f, z - 0.07f), c = point(l + 0.5f, z - 0.025f),
             m2 = point(l + 0.8f, z - 0.07f);
    if (!(L.ok && R.ok && m1.ok && c.ok && m2.ok)) return;
    canvas_.line(L.x, L.y, m1.x, m1.y, yellow);
    canvas_.line(m1.x, m1.y, c.x, c.y, yellow);
    canvas_.line(c.x, c.y, m2.x, m2.y, yellow);
    canvas_.line(m2.x, m2.y, R.x, R.y, yellow);
}

void TubeScreen::drawShots(const TubeGame& game) {
    const uint16_t yellow = rgb(0xF2, 0xE0, 0x30), white = WHITE;
    for (int i = 0; i < TubeGame::kMaxShots; ++i) {
        const TubeGame::Shot& s = game.shot(i);
        if (!s.alive) continue;
        const Pt a = point(s.lane + 0.5f, s.z), b = point(s.lane + 0.5f, s.z + 0.04f > 1 ? 1 : s.z + 0.04f);
        if (a.ok && b.ok) canvas_.line(a.x, a.y, b.x, b.y, yellow);
    }
    for (int i = 0; i < TubeGame::kMaxEnemyShots; ++i) {
        const TubeGame::Shot& s = game.enemyShot(i);
        if (!s.alive) continue;
        const Pt c = point(s.lane + 0.5f, s.z), r = point(s.lane + 0.65f, s.z);
        if (!(c.ok && r.ok)) continue;
        const float k = fabsf(r.x - c.x) + fabsf(r.y - c.y) + 1.5f;
        canvas_.line(c.x - k, c.y, c.x + k, c.y, white);
        canvas_.line(c.x, c.y - k, c.x, c.y + k, white);
    }
}

void TubeScreen::drawBlasts(const TubeGame& game) {
    const uint16_t orange = rgb(0xF2, 0x99, 0x4A);
    for (int i = 0; i < TubeGame::kMaxBlasts; ++i) {
        const TubeGame::Blast& b = game.blast(i);
        if (b.age < 0) continue;
        const Pt c = point(b.lane + 0.5f, b.z), r = point(b.lane + 1.0f, b.z);
        if (!(c.ok && r.ok)) continue;
        const float size = (fabsf(r.x - c.x) + fabsf(r.y - c.y)) * (0.4f + b.age * 3.0f);
        for (int k = 0; k < 8; ++k) {
            const float a = k * 0.785398f + b.age * 2;
            canvas_.line(c.x + cosf(a) * size * 0.3f, c.y + sinf(a) * size * 0.3f, c.x + cosf(a) * size,
                         c.y + sinf(a) * size, b.age < 0.25f ? orange : rgb(0xEB, 0x57, 0x57));
        }
    }
}

void TubeScreen::drawLives(const TubeGame& game) {
    const uint16_t yellow = rgb(0xF2, 0xE0, 0x30);
    const int n = game.lives() < 6 ? game.lives() : 6;
    for (int i = 0; i < n; ++i) {
        const float x = kCx - (n - 1) * 10 + i * 20, y = 458;
        canvas_.line(x - 7, y - 4, x - 3, y + 4, yellow);
        canvas_.line(x - 3, y + 4, x, y, yellow);
        canvas_.line(x, y, x + 3, y + 4, yellow);
        canvas_.line(x + 3, y + 4, x + 7, y - 4, yellow);
    }
    // Superzapper-Ladungen als grüne Punkte links/rechts
    for (int i = 0; i < game.zapsLeft(); ++i) {
        const float x = kCx + (i == 0 ? -90 : 90), y = 440;
        canvas_.line(x - 3, y, x + 3, y, rgb(0x30, 0xE0, 0x60));
        canvas_.line(x, y - 3, x, y + 3, rgb(0x30, 0xE0, 0x60));
    }
}

void TubeScreen::drawReady(uint32_t best) {
    const uint16_t dim = rgb(0x9A, 0x9A, 0x9A);
    centeredText("ROEHRENSTURM", 120, 3, rgb(0xF2, 0xE0, 0x30));
    char line[32];
    if (best > 0) {
        snprintf(line, sizeof(line), "REKORD %lu", static_cast<unsigned long>(best));
        centeredText(line, 166, 2, dim);
    }
    centeredText("RING: BAHN WECHSELN", 220, 2, dim);
    centeredText("DRUECKEN: FEUER", 244, 2, dim);
    centeredText("MITTE TIPPEN:", 268, 2, dim);
    centeredText("SUPERZAPPER", 292, 2, dim);
    centeredText("DRUECKEN: START", 346, 2, rgb(0xF2, 0xE0, 0x30));
}

void TubeScreen::render(const TubeGame& game, uint32_t best) {
    ++frame_;
    const int state = static_cast<int>(game.state());
    const bool changed = state != state_;
    gfx()->startWrite();
    if (changed) {
        gfx()->fillScreen(BLACK);
        canvas_.clear();
        state_ = state;
        if (game.state() == TubeGame::State::Ready) drawReady(best);
    }
    if (game.state() == TubeGame::State::Ready) {
        gfx()->endWrite();
        return;
    }
    if (game.shape() != shape_) buildRim(game.shape());
    cam_ = game.state() == TubeGame::State::Warp ? 1.0f - game.playerZ() : 0.0f;
    if (cam_ > 0.86f) cam_ = 0.86f;  // nicht ganz bis hinten: die Röhre bleibt erkennbar

    canvas_.begin();
    const uint16_t tube = game.zapFlash() ? WHITE : rgb(0x30, 0x50, 0xF0);
    drawTube(game, tube, rgb(0xF2, 0xE0, 0x30));
    drawEnemies(game);
    drawShots(game);
    drawPlayer(game);
    drawBlasts(game);
    drawLives(game);
    canvas_.finish();

    // Punkte oben; Level-Anzeige kurz in der Mitte nach jedem Levelwechsel
    char line[24];
    snprintf(line, sizeof(line), " %lu ", static_cast<unsigned long>(game.score()));
    centeredText(line, 8, 2, WHITE);
    if (game.level() != level_) {
        level_ = game.level();
        levelShownUntil_ = frame_ + 90;
    }
    if (frame_ < levelShownUntil_ && game.state() == TubeGame::State::Playing) {
        snprintf(line, sizeof(line), "LEVEL %d", game.level());
        centeredText(line, 232, 2, WHITE);
        levelText_ = true;
    } else if (levelText_) {
        centeredText("          ", 232, 2, WHITE);  // weg damit; die Linien kommen im nächsten Bild zurück
        levelText_ = false;
    }
    if (game.state() == TubeGame::State::GameOver) centeredText("GAME OVER", 226, 3, rgb(0xEB, 0x57, 0x57));
    gfx()->endWrite();
}
