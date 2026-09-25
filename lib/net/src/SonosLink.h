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
        TransportError,  ///< Play/Pause abgelehnt: value = UPnP-Fehlercode (0 = unbekannt), text = Beschreibung
        SpeakerOk,       ///< Speaker antwortet wieder
        SpeakerError,    ///< Speaker nicht erreichbar o. Ä.: text = Fehlerbeschreibung
    };
    Type type;
    int value;
    char text[64];
};

/** Wiedergabebefehl an den Speaker. */
enum class Transport : uint8_t { Play, Pause };

/**
 * Verbindung zu genau einem Sonos-Speaker (Schritt 1–4, IP aus secrets.h).
 *
 * Läuft als eigene FreeRTOS-Task auf Kern 0, damit Netzwerk-Wartezeiten die
 * Oberfläche (Kern 1) nie blockieren. Kommunikation nur über Queues:
 *
 *   UI  ── setVolume() ──▶  Task    (Queue der Länge 1: neuester Wert gewinnt)
 *   UI  ── transport() ──▶  Task    (Queue, Befehle in Reihenfolge)
 *   UI  ◀── pollEvent() ──  Task    (Ereignis-Queue)
 *
 * Die Task baut die WLAN-Verbindung auf und fragt danach alle 1,5 s Wiedergabezustand
 * und Lautstärke ab – so kommen auch Änderungen aus der Sonos-App an. Nach Fehlern
 * oder WLAN-Abbrüchen wird automatisch neu synchronisiert.
 */
class SonosLink {
public:
    static void begin(const char* ssid, const char* password, const char* speakerIp);

    /** Lautstärke setzen (nicht blockierend). Ein noch nicht gesendeter Wert wird ersetzt. */
    static void setVolume(int volume);

    /** Play oder Pause senden (nicht blockierend). */
    static void transport(Transport command);

    /** Nächste Meldung abholen (nicht blockierend). @return false, wenn keine vorliegt. */
    static bool pollEvent(Event& out);
};

}  // namespace net
