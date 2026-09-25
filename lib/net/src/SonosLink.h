#pragma once

#include <stdint.h>

namespace net {

/** Meldung der Netzwerk-Task an die UI. */
struct Event {
    enum class Type : uint8_t {
        WifiConnecting,  ///< Verbindungsaufbau läuft
        WifiConnected,   ///< text = eigene IP
        WifiLost,        ///< Verbindung weg, automatischer Neuaufbau läuft
        SpeakerVolume,   ///< value = Lautstärke 0..100 (GetVolume, regelmäßig abgefragt)
        TransportState,  ///< value = sonos::TransportState (GetTransportInfo, regelmäßig abgefragt)
        TransportError,  ///< Befehl abgelehnt: value = UPnP-Fehlercode (0 = unbekannt), text = Beschreibung
        SpeakerOk,       ///< Speaker antwortet wieder
        SpeakerError,    ///< Speaker wiederholt nicht erreichbar: text = Fehlerbeschreibung
    };
    Type type;
    int value;
    char text[64];
};

/** Befehl an den Speaker. */
enum class Transport : uint8_t { Play, Pause, Next, Previous, Seek };

/**
 * Was gerade läuft – Momentaufnahme der letzten Abfrage.
 * Feste Puffergrößen, damit die Übergabe zwischen den Tasks ohne Speicherverwaltung auskommt.
 */
struct NowPlayingInfo {
    uint32_t version = 0;   ///< steigt mit jeder erfolgreichen Abfrage
    uint8_t kind = 0;       ///< sonos::SourceKind
    char title[128] = "";
    char subtitle[96] = "";  ///< Interpret bzw. Sendername
    char album[96] = "";
    char albumArtUri[256] = "";
    int durationSec = -1;
    int positionSec = -1;
    uint32_t fetchedAtMs = 0;  ///< millis() der Abfrage – Bezugspunkt für die Position
};

/**
 * Verbindung zu genau einem Sonos-Speaker (Schritt 1–4, IP aus secrets.h).
 *
 * Läuft als eigene FreeRTOS-Task auf Kern 0, damit Netzwerk-Wartezeiten die
 * Oberfläche (Kern 1) nie blockieren. Kommunikation nur über Queues bzw. eine
 * mutex-geschützte Momentaufnahme:
 *
 *   UI  ── setVolume() ──────▶  Task   (Queue der Länge 1: neuester Wert gewinnt)
 *   UI  ── transport() ──────▶  Task   (Queue, Befehle in Reihenfolge)
 *   UI  ◀── pollEvent() ──────  Task   (Ereignis-Queue)
 *   UI  ◀── takeNowPlaying() ─  Task   (Titel, Interpret, Position, …)
 *
 * Die Task fragt alle 1,5 s Wiedergabezustand, Titel/Position und Lautstärke ab.
 * Einzelne Aussetzer (schwaches WLAN) werden still wiederholt; erst nach mehreren
 * Fehlschlägen in Folge gilt der Speaker als nicht erreichbar.
 */
class SonosLink {
public:
    static void begin(const char* ssid, const char* password, const char* speakerIp);

    /** Lautstärke setzen (nicht blockierend). Ein noch nicht gesendeter Wert wird ersetzt. */
    static void setVolume(int volume);

    /** Play, Pause, Nächster, Vorheriger (nicht blockierend). */
    static void transport(Transport command);

    /** Im aktuellen Titel an `positionSec` springen (nicht blockierend). */
    static void seek(int positionSec);

    /** Nächste Meldung abholen (nicht blockierend). @return false, wenn keine vorliegt. */
    static bool pollEvent(Event& out);

    /**
     * Aktuelle Momentaufnahme kopieren, falls es seit `lastVersion` eine neue gibt.
     * @return true, wenn `out` aktualisiert wurde.
     */
    static bool takeNowPlaying(uint32_t lastVersion, NowPlayingInfo& out);
};

}  // namespace net
