#pragma once

#include <string>
#include <utility>
#include <vector>

namespace sonos {

/** Ein UPnP-Dienst eines Sonos-Players (Port 1400). */
struct Service {
    const char* controlPath;  ///< z. B. "/MediaRenderer/RenderingControl/Control"
    const char* urn;          ///< z. B. "urn:schemas-upnp-org:service:RenderingControl:1"
};

namespace services {
extern const Service RenderingControl;
extern const Service AVTransport;
}  // namespace services

/** Fertige SOAP-Anfrage – alles, was der HTTP-Client zum Senden braucht. */
struct SoapRequest {
    std::string path;        ///< HTTP-Pfad, an Port 1400 des Players
    std::string soapAction;  ///< Wert für den HTTP-Header SOAPACTION (inkl. Anführungszeichen)
    std::string body;        ///< XML-Envelope
};

using SoapArgs = std::vector<std::pair<std::string, std::string>>;

/**
 * Baut eine SOAP-Anfrage. Argumentwerte werden XML-escaped, die Reihenfolge bleibt
 * erhalten (Sonos erwartet die Reihenfolge aus der Dienstbeschreibung).
 */
SoapRequest buildSoapRequest(const Service& service, const std::string& action, const SoapArgs& args);

/** Ergebnis einer SOAP-Antwort. */
struct SoapResult {
    bool ok = false;
    int httpStatus = 0;
    int upnpErrorCode = 0;  ///< aus <errorCode> bei SOAP-Fault, sonst 0
    std::string error;      ///< lesbare Fehlerbeschreibung (Deutsch), leer bei Erfolg
};

/**
 * Bewertet HTTP-Status und Antworttext.
 * @param httpStatus HTTP-Statuscode; ≤ 0 = Verbindungsfehler (Codes des HTTPClient)
 */
SoapResult evaluateResponse(int httpStatus, const std::string& body);

}  // namespace sonos
