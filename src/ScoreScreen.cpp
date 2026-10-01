#include "ScoreScreen.h"

#include <Arduino_GFX_Library.h>

#include <cstdio>
#include <cstring>

#include "Display.h"

using app::game::HighscoreTable;
using app::game::InitialsEntry;

namespace {

constexpr int kCx = 240;
constexpr int kLetterSize = 7;                    // 42 × 56 Pixel je Buchstabe
constexpr int kLetterW = 6 * kLetterSize;
constexpr int kLetterY = 236;
constexpr int kLetterStep = 76;

Arduino_GFX* gfx() { return hal::Display::gfx(); }
uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return gfx()->color565(r, g, b); }
uint16_t yellow() { return rgb(0xF2, 0xC9, 0x4C); }
uint16_t dim() { return rgb(0x9A, 0x9A, 0x9A); }

void centeredText(const char* text, int y, uint8_t size, uint16_t color) {
    gfx()->setTextSize(size);
    gfx()->setTextColor(color, BLACK);
    const int w = static_cast<int>(strlen(text)) * 6 * size;
    gfx()->setCursor(kCx - w / 2, y);
    gfx()->print(text);
}

int letterX(int i) { return kCx + (i - 1) * kLetterStep - kLetterW / 2; }

}  // namespace

void ScoreScreen::formatValue(uint32_t value, bool isTime, char* out, size_t size) {
    if (!isTime) {
        snprintf(out, size, "%lu", static_cast<unsigned long>(value));
        return;
    }
    const uint32_t tenths = value / 100;
    snprintf(out, size, "%lu:%02lu.%lu", static_cast<unsigned long>(tenths / 600),
             static_cast<unsigned long>((tenths / 10) % 60), static_cast<unsigned long>(tenths % 10));
}

void ScoreScreen::showEntry(const InitialsEntry& entry, uint32_t value, bool isTime, int rank) {
    gfx()->startWrite();
    if (mode_ != Mode::Entry) {
        gfx()->fillScreen(BLACK);
        centeredText(rank == 0 ? "NEUER REKORD!" : "BESTENLISTE!", 92, 3, yellow());
        char v[16], line[32];
        formatValue(value, isTime, v, sizeof(v));
        centeredText(v, 140, 3, WHITE);
        snprintf(line, sizeof(line), "PLATZ %d - DEIN NAME?", rank + 1);
        centeredText(line, 186, 2, dim());
        centeredText("RING: BUCHSTABE", 340, 2, dim());
        centeredText("DRUECKEN: WEITER", 364, 2, dim());
        mode_ = Mode::Entry;
        memset(shown_, 0, sizeof(shown_));
        shownPos_ = -1;
    }
    for (int i = 0; i < 3; ++i) {
        const char c = entry.letter(i);
        const bool current = i == entry.position();
        const bool wasCurrent = i == shownPos_;
        if (c == shown_[i] && current == wasCurrent) continue;
        const int x = letterX(i);
        gfx()->fillRect(x - 4, kLetterY - 4, kLetterW + 8, 8 * kLetterSize + 18, BLACK);
        gfx()->setTextSize(kLetterSize);
        gfx()->setTextColor(current ? yellow() : WHITE, BLACK);
        gfx()->setCursor(x, kLetterY);
        const char s[2] = {c, '\0'};
        gfx()->print(s);
        if (current) gfx()->fillRect(x - 2, kLetterY + 8 * kLetterSize + 6, kLetterW + 4, 5, yellow());  // Strich darunter
        shown_[i] = c;
    }
    shownPos_ = entry.position();
    gfx()->endWrite();
}

void ScoreScreen::showTable(const char* title, const HighscoreTable& table, uint32_t value, bool isTime,
                            int highlight) {
    gfx()->startWrite();
    gfx()->fillScreen(BLACK);
    centeredText(title, 66, 3, yellow());
    centeredText("BESTENLISTE", 104, 2, dim());
    char v[16], line[32];
    for (int i = 0; i < HighscoreTable::kEntries; ++i) {
        const int y = 144 + i * 40;
        if (i < table.count()) {
            const HighscoreTable::Entry& e = table.entry(i);
            formatValue(e.value, isTime, v, sizeof(v));
            snprintf(line, sizeof(line), "%d %s %8s", i + 1, e.name, v);
        } else {
            snprintf(line, sizeof(line), "%d --- %8s", i + 1, "-");
        }
        centeredText(line, y, 3, i == highlight ? yellow() : WHITE);
    }
    if (value > 0) {
        formatValue(value, isTime, v, sizeof(v));
        snprintf(line, sizeof(line), "DU: %s", v);
        centeredText(line, 352, 2, highlight >= 0 ? yellow() : dim());
    }
    centeredText("DRUECKEN: NOCHMAL", 384, 2, dim());
    centeredText("LANG: ENDE", 408, 2, dim());
    mode_ = Mode::Table;
    gfx()->endWrite();
}
