#pragma once

#include <string>

#include "Soap.h"

namespace sonos {

/** Wiedergabezustand laut AVTransport (CurrentTransportState). */
enum class TransportState {
    Unknown,
    Stopped,        ///< STOPPED oder NO_MEDIA_PRESENT
    Playing,        ///< PLAYING
    Paused,         ///< PAUSED_PLAYBACK
    Transitioning,  ///< TRANSITIONING (z. B. Puffern nach Play)
};

/**
 * AVTransport – Wiedergabe steuern.
 * Wichtig: Diese Befehle wirken nur am Koordinator einer Gruppe (sonst UPnP-Fehler 800).
 */
namespace avtransport {

SoapRequest play();
SoapRequest pause();
SoapRequest stop();
SoapRequest getTransportInfo();
SoapRequest getPositionInfo();  ///< Titel-Metadaten, Länge, Position (Auswertung: NowPlaying.h)
SoapRequest getMediaInfo();     ///< Quelle, z. B. Sendername bei Radio (Auswertung: NowPlaying.h)
SoapRequest next();
SoapRequest previous();
SoapRequest seek(int positionSec);  ///< Springt im aktuellen Titel (Unit REL_TIME)

/** Liest <CurrentTransportState>. @return false, wenn nicht vorhanden. */
bool parseTransportInfo(const std::string& body, TransportState& state);

/** Wandelt den Text aus CurrentTransportState um (unbekannte Werte → Unknown). */
TransportState parseTransportState(const std::string& text);

}  // namespace avtransport
}  // namespace sonos
