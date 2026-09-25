#pragma once

#include <string>

namespace sonos {

/** Rohdaten aus GetPositionInfo. */
struct PositionInfo {
    std::string trackUri;       ///< <TrackURI>, z. B. "x-sonos-spotify:…", "x-sonosapi-stream:…"
    std::string trackMetaData;  ///< <TrackMetaData>, bereits einmal entschlüsselt (DIDL-Lite-XML)
    int durationSec = -1;       ///< <TrackDuration>; −1 = unbekannt, 0 = Stream ohne Länge
    int positionSec = -1;       ///< <RelTime>; −1 = unbekannt
};

/** Rohdaten aus GetMediaInfo (nur der Teil, der für Radio gebraucht wird). */
struct MediaInfo {
    std::string currentUri;          ///< <CurrentURI>
    std::string currentUriMetaData;  ///< <CurrentURIMetaData>, bereits einmal entschlüsselt
};

/** Aus DIDL-Lite gelesene Titelangaben (Texte bereits vollständig entschlüsselt). */
struct TrackMeta {
    std::string title;          ///< dc:title
    std::string artist;         ///< dc:creator
    std::string album;          ///< upnp:album
    std::string albumArtUri;    ///< upnp:albumArtURI (oft relativ: "/getaa?…")
    std::string streamContent;  ///< r:streamContent (Radio: „Interpret - Titel“)
};

/** Art der Quelle – bestimmt, was angezeigt und was bedienbar ist. */
enum class SourceKind { None, Track, Radio, TV, LineIn };

/** Das, was der Now-Playing-Bildschirm anzeigt. */
struct NowPlaying {
    SourceKind kind = SourceKind::None;
    std::string title;        ///< Hauptzeile
    std::string subtitle;     ///< Interpret bzw. Sendername
    std::string album;
    std::string albumArtUri;
    std::string trackUri;     ///< für die Cover-Suche über den Speaker (AlbumArt.h)
    int durationSec = -1;     ///< > 0 nur bei Titeln mit bekannter Länge
    int positionSec = -1;

    /** Spulen und Fortschritt nur bei Titeln mit bekannter Länge. */
    bool hasProgress() const { return kind == SourceKind::Track && durationSec > 0; }
    /** Nächster/vorheriger Titel ergibt nur bei Titeln (Warteschlange) Sinn. */
    bool canSkip() const { return kind == SourceKind::Track; }
};

namespace time {
/** "H:MM:SS" bzw. "HH:MM:SS" → Sekunden; "NOT_IMPLEMENTED", leer o. Ä. → −1. */
int parseDuration(const std::string& text);
/** Sekunden → "m:ss" bzw. "h:mm:ss"; negative Werte → "–:––". */
std::string format(int seconds);
/** Sekunden → "H:MM:SS" für Seek-Befehle. */
std::string toUpnp(int seconds);
}  // namespace time

/** Liest die Felder aus einer GetPositionInfo-Antwort. @return false bei unerwarteter Antwort. */
bool parsePositionInfo(const std::string& body, PositionInfo& out);

/** Liest die Felder aus einer GetMediaInfo-Antwort. @return false bei unerwarteter Antwort. */
bool parseMediaInfo(const std::string& body, MediaInfo& out);

/** Liest Titel, Interpret usw. aus einem DIDL-Lite-Dokument. Leeres/ungültiges XML → leere Felder. */
TrackMeta parseDidl(const std::string& didl);

/**
 * Bestimmt Quelle und Anzeigetexte.
 * @param media darf nullptr sein; wird für den Sendernamen bei Radio verwendet.
 *
 * Regeln:
 *  - TV (x-sonos-htastream:) → "TV", Line-In (x-rincon-stream:) → "Line-In" (bei leerer TrackURI aus CurrentURI)
 *  - Radio (Länge 0 bzw. Stream-URI): Titel = r:streamContent (falls sinnvoll), sonst
 *    Sendername; Untertitel = Sendername; Cover = Senderlogo aus GetMediaInfo.
 *    Sonos-Platzhalter wie „ZPSTR_CONNECTING“ werden zu „Verbinde …“, Stream-Adressen
 *    (die Sonos bei Radio oft als dc:title schickt) werden nie angezeigt.
 *  - Spotify Connect (x-sonos-vli:) verhält sich wie ein normaler Titel; das Cover ist
 *    dann eine absolute HTTPS-Adresse.
 *  - Titel: Titel/Interpret/Album aus DIDL
 *  - Leere Warteschlange → SourceKind::None
 */
NowPlaying buildNowPlaying(const PositionInfo& pos, const MediaInfo* media);

}  // namespace sonos
