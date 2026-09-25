#pragma once

#include <stdint.h>

namespace net {

/** Meldung der Netzwerk-Task an die UI. */
struct Event {
    enum class Type : uint8_t {
        WifiConnecting,  ///< Verbindungsaufbau läuft
        WifiConnected,   ///< text = eigene IP
        WifiLost,        ///< Verbindung weg, automatischer Neuaufbau läuft
        Discovering,     ///< Sonos-Anlage wird gesucht (SSDP + Topologie)
        NoSpeakers,      ///< keine Sonos-Anlage gefunden, neuer Versuch folgt
        RoomChanged,     ///< aktiver Raum gewechselt: value = Index in RoomsInfo, text = Anzeigename
        SpeakerVolume,   ///< value = Lautstärke 0..100 (bei Gruppen: Gruppenlautstärke)
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

/** Räume bzw. Gruppen der Anlage – Momentaufnahme der letzten Topologie-Abfrage. */
constexpr int kMaxRooms = 16;
struct RoomEntry {
    char uuid[40];      ///< Koordinator der Gruppe
    char name[48];      ///< Raumname des Koordinators
    char display[56];   ///< „Küche“ bzw. „Küche + 2“
    uint8_t memberCount;
};
struct RoomsInfo {
    uint32_t version = 0;
    int count = 0;
    int selected = -1;  ///< aktiver Raum, −1 = noch keiner
    RoomEntry rooms[kMaxRooms];
};

/**
 * Verbindung zur Sonos-Anlage (ab Schritt 5 mit automatischer Suche und Raumwahl).
 *
 * Läuft als eigene FreeRTOS-Task auf Kern 0, damit Netzwerk-Wartezeiten die
 * Oberfläche (Kern 1) nie blockieren. Kommunikation nur über Queues bzw.
 * mutex-geschützte Momentaufnahmen:
 *
 *   UI  ── setVolume() / transport() / seek() / selectRoom() ──▶  Task
 *   UI  ◀── pollEvent() / takeNowPlaying() / takeRooms() ───────  Task
 *
 * Ablauf der Task: WLAN verbinden → Anlage suchen (bekannte IPs, sonst SSDP) →
 * Topologie lesen → Raum auflösen (Befehle immer an den Gruppen-Koordinator) →
 * alle 1,5 s Zustand/Titel/Lautstärke abfragen, alle 30 s die Topologie.
 * Einzelne Aussetzer werden still wiederholt; nach mehreren Fehlschlägen in Folge
 * wird die Anlage neu gesucht (z. B. wenn ein Speaker eine neue IP bekommen hat).
 */
class SonosLink {
public:
    /**
     * @param preferredRoomUuid zuletzt gewählter Raum (Koordinator-UUID) oder "" für automatisch
     * @param fallbackIp        optional (secrets.h SONOS_IP): Startpunkt für die Suche und
     *                          Raum für den allerersten Start; "" = nur SSDP
     */
    static void begin(const char* ssid, const char* password, const char* preferredRoomUuid,
                      const char* fallbackIp);

    /** Lautstärke setzen (nicht blockierend). Ein noch nicht gesendeter Wert wird ersetzt. */
    static void setVolume(int volume);

    /** Play, Pause, Nächster, Vorheriger (nicht blockierend). */
    static void transport(Transport command);

    /** Im aktuellen Titel an `positionSec` springen (nicht blockierend). */
    static void seek(int positionSec);

    /** Auf einen anderen Raum umschalten (UUID aus RoomsInfo, nicht blockierend). */
    static void selectRoom(const char* uuid);

    /** Nächste Meldung abholen (nicht blockierend). @return false, wenn keine vorliegt. */
    static bool pollEvent(Event& out);

    /** Titel-Momentaufnahme kopieren, falls es seit `lastVersion` eine neue gibt. */
    static bool takeNowPlaying(uint32_t lastVersion, NowPlayingInfo& out);

    /** Räume kopieren, falls es seit `lastVersion` eine neue Liste gibt. */
    static bool takeRooms(uint32_t lastVersion, RoomsInfo& out);
};

}  // namespace net
