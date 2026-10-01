#pragma once

#include <stdint.h>
#include <string.h>

namespace app {
namespace game {

/**
 * Bestenliste wie am Spielautomaten: die besten 5 mit drei Buchstaben.
 *
 * Für Punkte (mehr ist besser, Ringbrecher) oder Zeiten (weniger ist besser, Boxenstopp).
 * Bei Gleichstand steht der ältere Eintrag vorn. Die Struktur ist ein fester Block ohne Zeiger
 * und wird so wie sie ist im NVS gespeichert (`data()`/`size()`, `load()` prüft die Version).
 */
class HighscoreTable {
public:
    static constexpr int kEntries = 5;
    static constexpr uint8_t kVersion = 1;

    struct Entry {
        char name[4];    ///< drei Buchstaben + '\0'
        uint32_t value;  ///< Punkte bzw. Millisekunden
    };

    explicit HighscoreTable(bool lowerIsBetter = false) { clear(lowerIsBetter); }

    void clear(bool lowerIsBetter) {
        memset(&block_, 0, sizeof(block_));
        block_.version = kVersion;
        block_.lowerIsBetter = lowerIsBetter ? 1 : 0;
    }

    int count() const { return block_.count; }
    const Entry& entry(int i) const { return block_.entries[i]; }
    bool lowerIsBetter() const { return block_.lowerIsBetter != 0; }
    /** Bester Wert (0 = Liste leer). */
    uint32_t best() const { return block_.count ? block_.entries[0].value : 0; }

    /** Platz (0 = erster), den `value` bekäme, oder −1, wenn er nicht in die Liste kommt. */
    int rankFor(uint32_t value) const {
        if (value == 0) return -1;  // 0 Punkte bzw. keine Zeit zählt nicht
        int rank = 0;
        while (rank < block_.count && !better(value, block_.entries[rank].value)) ++rank;
        return rank < kEntries ? rank : -1;
    }
    bool qualifies(uint32_t value) const { return rankFor(value) >= 0; }

    /** Einträgt `value` mit `name` (wird auf 3 Zeichen gekürzt/aufgefüllt). Liefert den Platz oder −1. */
    int insert(const char* name, uint32_t value) {
        const int rank = rankFor(value);
        if (rank < 0) return -1;
        const int last = block_.count < kEntries ? block_.count : kEntries - 1;
        for (int i = last; i > rank; --i) block_.entries[i] = block_.entries[i - 1];
        Entry& e = block_.entries[rank];
        bool ended = !name;
        for (int i = 0; i < 3; ++i) {
            if (!ended && name[i] == '\0') ended = true;
            e.name[i] = ended ? '-' : name[i];
        }
        e.name[3] = '\0';
        e.value = value;
        if (block_.count < kEntries) ++block_.count;
        return rank;
    }

    // --- Speichern im NVS ----------------------------------------------------------------------
    const void* data() const { return &block_; }
    static constexpr size_t size() { return sizeof(Block); }
    /** Übernimmt einen gespeicherten Block. false (und leere Liste) bei falscher Größe/Version/Art. */
    bool load(const void* bytes, size_t length) {
        const bool lower = lowerIsBetter();
        if (!bytes || length != sizeof(Block)) {
            clear(lower);
            return false;
        }
        Block b;
        memcpy(&b, bytes, sizeof(Block));
        if (b.version != kVersion || b.count > kEntries || (b.lowerIsBetter != 0) != lower) {
            clear(lower);
            return false;
        }
        for (int i = 0; i < b.count; ++i) b.entries[i].name[3] = '\0';
        block_ = b;
        return true;
    }

private:
    struct Block {
        uint8_t version;
        uint8_t lowerIsBetter;
        uint8_t count;
        uint8_t reserved;
        Entry entries[kEntries];
    };

    bool better(uint32_t a, uint32_t b) const { return lowerIsBetter() ? a < b : a > b; }

    Block block_;
};

/**
 * Eingabe von drei Buchstaben mit Drehring und Taste: Ring = Buchstabe wählen (A–Z, 0–9, -),
 * Drücken = nächste Stelle; nach der dritten ist die Eingabe fertig.
 */
class InitialsEntry {
public:
    static constexpr const char* kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
    static constexpr int kAlphabetSize = 37;

    /** Startet mit `preset` (z. B. die zuletzt eingegebenen Buchstaben), sonst „AAA“. */
    void start(const char* preset) {
        for (int i = 0; i < 3; ++i) {
            const char c = (preset && strlen(preset) > static_cast<size_t>(i)) ? preset[i] : 'A';
            letter_[i] = indexOf(c);
        }
        pos_ = 0;
    }

    void rotate(int detents) {
        if (done() || detents == 0) return;
        letter_[pos_] = ((letter_[pos_] + detents) % kAlphabetSize + kAlphabetSize) % kAlphabetSize;
    }
    void press() {
        if (!done()) ++pos_;
    }
    bool done() const { return pos_ >= 3; }
    int position() const { return pos_; }
    char letter(int i) const { return kAlphabet[letter_[i]]; }
    /** Die drei Buchstaben als Zeichenkette (out muss ≥ 4 Zeichen fassen). */
    void name(char* out) const {
        for (int i = 0; i < 3; ++i) out[i] = letter(i);
        out[3] = '\0';
    }

private:
    static int indexOf(char c) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        for (int i = 0; i < kAlphabetSize; ++i)
            if (kAlphabet[i] == c) return i;
        return 0;
    }

    int letter_[3] = {0, 0, 0};
    int pos_ = 0;
};

}  // namespace game
}  // namespace app
