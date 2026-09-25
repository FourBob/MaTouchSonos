#include "Soap.h"

#include <cstdlib>

#include "Xml.h"

namespace sonos {

namespace services {
const Service RenderingControl{"/MediaRenderer/RenderingControl/Control",
                               "urn:schemas-upnp-org:service:RenderingControl:1"};
const Service AVTransport{"/MediaRenderer/AVTransport/Control", "urn:schemas-upnp-org:service:AVTransport:1"};
}  // namespace services

SoapRequest buildSoapRequest(const Service& service, const std::string& action, const SoapArgs& args) {
    SoapRequest req;
    req.path = service.controlPath;
    req.soapAction = std::string("\"") + service.urn + "#" + action + "\"";

    std::string body;
    body.reserve(400);
    body += "<?xml version=\"1.0\" encoding=\"utf-8\"?>";
    body += "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
            "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">";
    body += "<s:Body>";
    body += "<u:" + action + " xmlns:u=\"" + service.urn + "\">";
    for (const auto& arg : args) {
        body += "<" + arg.first + ">" + xml::escape(arg.second) + "</" + arg.first + ">";
    }
    body += "</u:" + action + ">";
    body += "</s:Body></s:Envelope>";
    req.body = std::move(body);
    return req;
}

namespace {

// Bekannte UPnP-/Sonos-Fehlercodes mit verständlicher Beschreibung.
const char* describeUpnpError(int code) {
    switch (code) {
        case 401: return "Unbekannte Aktion";
        case 402: return "Ungültige Argumente";
        case 501: return "Aktion fehlgeschlagen";
        case 701: return "Nicht möglich im aktuellen Zustand";
        case 800: return "Nur am Gruppen-Koordinator möglich";
        default: return nullptr;
    }
}

// Negative Codes des Arduino-HTTPClient.
const char* describeHttpClientError(int code) {
    switch (code) {
        case -1: return "Verbindung abgelehnt – Speaker nicht erreichbar";
        case -4: return "Keine Verbindung";
        case -5: return "Verbindung unterbrochen";
        case -11: return "Zeitüberschreitung – Speaker antwortet nicht";
        default: return "Netzwerkfehler";
    }
}

}  // namespace

SoapResult evaluateResponse(int httpStatus, const std::string& body) {
    SoapResult r;
    r.httpStatus = httpStatus;

    if (httpStatus <= 0) {
        r.error = describeHttpClientError(httpStatus);
        return r;
    }

    bool hasFault = false;
    xml::findElement(body, "Fault", &hasFault);
    if (hasFault || httpStatus >= 400) {
        bool hasCode = false;
        const std::string code = xml::findElement(body, "errorCode", &hasCode);
        if (hasCode) r.upnpErrorCode = std::atoi(code.c_str());

        const char* known = describeUpnpError(r.upnpErrorCode);
        if (known) {
            r.error = std::string(known) + " (UPnP " + std::to_string(r.upnpErrorCode) + ")";
        } else if (r.upnpErrorCode != 0) {
            r.error = "Sonos-Fehler " + std::to_string(r.upnpErrorCode);
        } else {
            r.error = "HTTP-Fehler " + std::to_string(httpStatus);
        }
        return r;
    }

    r.ok = true;
    return r;
}

}  // namespace sonos
