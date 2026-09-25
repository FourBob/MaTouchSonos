#pragma once

#include <string>
#include <vector>

#include "NowPlaying.h"

namespace sonos {

/**
 * Welche Bildadressen für das Cover probiert werden – abhängig davon, woher die Musik kommt.
 *
 * Jeder Dienst liefert Cover anders (aus echten Aufzeichnungen, siehe Tests):
 *  - Warteschlange (Spotify, Apple Music, Amazon, eigene Bibliothek …): relative Adresse
 *    „/getaa?…“ – der Speaker holt das Bild selbst und liefert es per HTTP aus (Port 1400).
 *  - Spotify Connect (x-sonos-vli): absolute HTTPS-Adresse bei Spotify (i.scdn.co, 640 px JPEG).
 *  - Radio (TuneIn u. a.): Senderlogo aus GetMediaInfo, HTTPS, oft PNG.
 *  - Apple Music/Deezer liefern teils sehr große Bilder – deren URLs enthalten die Größe und
 *    werden auf ~480 px umgeschrieben (spart Download, Speicher und Rechenzeit).
 *  - TV, Line-In, leere Warteschlange: kein Cover.
 *
 * Die Liste wird der Reihe nach probiert, bis ein Bild geladen und dekodiert werden konnte.
 */
namespace art {

/** Kandidaten in Reihenfolge, ohne Duplikate. Leer = kein Cover für diese Quelle. */
std::vector<std::string> candidates(const NowPlaying& np, const std::string& speakerIp);

/** Größen in bekannten Bild-URLs auf ca. 480 px umschreiben (sonst unverändert). */
std::string preferredSize(const std::string& url);

/** Prozent-Kodierung für URL-Parameter (RFC 3986, unreserviert: A–Z a–z 0–9 - _ . ~). */
std::string urlEncode(const std::string& text);

}  // namespace art
}  // namespace sonos
