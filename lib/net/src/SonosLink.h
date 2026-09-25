#pragma once

#include <stdint.h>

namespace net {

/** Meldung der Netzwerk-Task an die UI. */
struct Event {
    enum class Type : uint8_t {
        WifiConnecting,  ///< Verbindungsaufbau läuft
        WifiConnected,   ///< text = eigene IP
        WifiLost,        ///< Verbindung weg, automatischer Neuaufbau läuft
        SpeakerVolume,   ///< value = Lautstärke 0..100 (Antwort auf GetVolume)
        SpeakerOk,       ///< Speaker antwortet wieder
        SpeakerError,    ///< text = Fehlerbeschreibung
    };
    Type type;
    int value;
    char text[64];
};

/**
 * Verbindung zu genau einem Sonos-Speaker (Schritt 1–4, IP aus secrets.h).
 *
 * Läuft als eigene FreeRTOS-Task auf Kern 0, damit Netzwerk-Wartezeiten die
 * Oberfläche (Kern 1) nie blockieren. Kommunikation nur über Queues:
 *
 *   UI  ── setVolume() ──▶  Task    (Queue der Länge 1: neuester Wert gewinnt)
 *   UI  ◀── pollEvent() ──  Task    (Ereignis-Queue)
 *
 * Die Task baut die WLAN-Verbindung auf, liest danach die aktuelle Lautstärke
 * (GetVolume) und wiederholt das nach Fehlern oder WLAN-Abbrüchen automatisch.
 */
class SonosLink {
public:
    static void begin(const char* ssid, const char* password, const char* speakerIp);

    /** Lautstärke setzen (nicht blockierend). Ein noch nicht gesendeter Wert wird ersetzt. */
    static void setVolume(int volume);

    /** Nächste Meldung abholen (nicht blockierend). @return false, wenn keine vorliegt. */
    static bool pollEvent(Event& out);
};

}  // namespace net
