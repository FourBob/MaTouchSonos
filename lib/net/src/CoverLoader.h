#pragma once

#include <stdint.h>

#include <string>
#include <vector>

namespace net {

/** Kantenlänge des fertigen Covers in Pixeln (volles Display). */
constexpr int kCoverSize = 480;

/** Fertiges Cover für die Anzeige. */
struct CoverFrame {
    uint32_t version = 0;
    const uint16_t* pixels = nullptr;  ///< kCoverSize² RGB565-Pixel (PSRAM) oder nullptr = kein Cover
};

/**
 * Lädt Albumcover im Hintergrund (eigene FreeRTOS-Task, Kern 0, niedrige Priorität).
 *
 *   request(Kandidaten) ─▶ nacheinander probieren: herunterladen (HTTP oder HTTPS, max. 700 KB)
 *                          → JPEG oder PNG dekodieren → auf 480×480 zuschneiden → abdunkeln
 *                          ─▶ takeCover()
 *
 * Zwei Bildpuffer im PSRAM (Doppelpufferung): Die UI zeigt den einen an, während der
 * nächste in den anderen geladen wird. Bevor ein Puffer überschrieben wird, wartet die
 * Task, bis die UI mit acknowledge() bestätigt hat, dass sie das neue Bild übernommen hat.
 *
 * HTTPS-Adressen (Spotify, TuneIn …) werden ohne Zertifikatsprüfung geladen: Es werden
 * nur öffentliche Bilder abgerufen, keine Zugangsdaten übertragen.
 */
class CoverLoader {
public:
    static void begin();

    /**
     * Neues Cover anfordern. Gleiche Kandidaten wie beim letzten Mal werden ignoriert,
     * eine leere Liste entfernt das Cover. Ein noch laufender Auftrag wird ersetzt.
     */
    static void request(const std::vector<std::string>& candidates);

    /** Neues Cover abholen, falls es seit `lastVersion` eines gibt. */
    static bool takeCover(uint32_t lastVersion, CoverFrame& out);

    /** Die UI zeigt jetzt Version `version` an – der andere Puffer darf überschrieben werden. */
    static void acknowledge(uint32_t version);
};

}  // namespace net
