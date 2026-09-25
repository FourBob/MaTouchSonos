#pragma once

#include <functional>
#include <string>
#include <vector>

#include "Soap.h"

namespace sonos {

namespace services {
extern const Service ContentDirectory;
}  // namespace services

/** Ein Sonos-Favorit (wie in der Sonos-App unter „Meine Sonos“ → Favoriten). */
struct Favorite {
    std::string title;        ///< Name des Favoriten
    std::string description;  ///< z. B. „TuneIn“, „Spotify-Playlist“ – je nach Dienst, oft leer
    std::string uri;          ///< <res>: was abgespielt wird
    std::string metadata;     ///< <r:resMD>: DIDL-Lite (dekodiert), geht unverändert an den Speaker zurück
    std::string upnpClass;    ///< Art des Inhalts aus den Metadaten, z. B. object.container.playlistContainer
    std::string albumArtUri;
};

/** Wie ein Favorit gestartet wird. */
enum class PlayMethod {
    Direct,       ///< Radio, Line-In, TV: SetAVTransportURI + Play
    Queue,        ///< Playlist, Album, Einzeltitel: Warteschlange ersetzen, dann abspielen
    Unsupported,  ///< ohne abspielbare Adresse (z. B. reine Verknüpfungen in der App)
};

/**
 * Favoriten lesen und einordnen (ContentDirectory, Objekt „FV:2“).
 *
 * Aufbau der Antwort: <Result> enthält DIDL-Lite (einfach kodiert), darin je Favorit ein
 * <item> mit <res> (Adresse) und <r:resMD> (Metadaten, nochmals kodiert). Die Metadaten
 * enthalten u. a. das Anmelde-Token des Dienstes (<desc>) – ohne sie spielt z. B. Spotify
 * nicht ab. Sie werden deshalb unverändert zurückgegeben.
 */
namespace favorites {

constexpr int kPageSize = 100;  ///< Favoriten je Anfrage

SoapRequest browse(int startIndex, int count = kPageSize);

/**
 * Ruft `visit(position, favorit)` für jeden Favoriten einer Browse-Antwort auf – einzeln,
 * ohne alle gleichzeitig im Speicher zu halten (auf dem Gerät wichtig: Die Metadaten sind
 * je Favorit ~0,5–2 KB). `position` zählt ab 0 innerhalb dieser Antwort; zusammen mit dem
 * StartingIndex der Anfrage ergibt das die Position für ein späteres browse(position, 1).
 * @param totalMatches Gesamtzahl laut Speaker (für weitere Seiten)
 * @return false, wenn die Antwort kein <Result> enthält
 */
bool parseBrowse(const std::string& body, const std::function<void(int, Favorite&&)>& visit, int& totalMatches);

/** Wie oben, hängt alle Favoriten an `out` an (für Tests und das Probe-Werkzeug). */
bool parseBrowse(const std::string& body, std::vector<Favorite>& out, int& totalMatches);

/** Entscheidet anhand von Adresse und Inhaltsart, wie der Favorit gestartet wird. */
PlayMethod playMethod(const Favorite& favorite);

}  // namespace favorites
}  // namespace sonos
