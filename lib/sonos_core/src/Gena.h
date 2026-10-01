#pragma once

#include <string>
#include <utility>
#include <vector>

namespace sonos {

/**
 * UPnP-Events (GENA): Der Speaker meldet Änderungen von sich aus, statt dass wir ständig fragen.
 *
 *   Gerät ── SUBSCRIBE  CALLBACK: <http://<Gerät>:3400/ev/av>, NT: upnp:event, TIMEOUT: Second-1800 ──▶ Speaker
 *         ◀── 200 OK    SID: uuid:…, TIMEOUT: Second-1800
 *   Speaker ── NOTIFY /ev/av  SID: uuid:…, SEQ: 0, Body: <e:propertyset>…<LastChange>…</LastChange> ──▶ Gerät
 *   Gerät ── SUBSCRIBE  SID: uuid:…, TIMEOUT: Second-1800 (Erneuern vor Ablauf) ──▶ Speaker
 *
 * Die Firmware nutzt ein Event vor allem als Signal „jetzt abfragen“ – die eigentlichen Werte holt
 * die bewährte Abfrage. Ausgewertet wird nur, was fürs Log hilft (Zustand, Lautstärke).
 */
namespace gena {

/** Ein Dienst, dessen Events abonniert werden. */
struct EventService {
    const char* name;          ///< fürs Log
    const char* eventPath;     ///< am Speaker (Port 1400)
    const char* callbackPath;  ///< bei uns (Port kCallbackPort)
};

constexpr int kCallbackPort = 3400;
extern const EventService AVTransport;            ///< Titel, Zustand, Quelle
extern const EventService RenderingControl;       ///< Lautstärke eines einzelnen Speakers
extern const EventService GroupRenderingControl;  ///< Gruppenlautstärke (am Koordinator)
extern const EventService ZoneGroupTopology;      ///< Räume/Gruppen geändert

/** Dienst zu einem Callback-Pfad, nullptr wenn unbekannt. */
const EventService* serviceForCallback(const std::string& path);

/** "Second-1800" → 1800; "infinite" → 0; sonst −1. */
int parseTimeout(const std::string& header);

/** Kopf einer HTTP-Anfrage (NOTIFY). */
struct Request {
    std::string method;
    std::string path;
    std::vector<std::pair<std::string, std::string>> headers;  ///< Namen in Großbuchstaben
    int contentLength = 0;

    /** Wert eines Kopffelds (Name egal in welcher Schreibweise), leer wenn nicht vorhanden. */
    std::string header(const std::string& name) const;
};

/**
 * Kopf einer HTTP-Anfrage auswerten (alles bis zur Leerzeile).
 * @param headBytes Länge des Kopfes inklusive "\r\n\r\n" – danach beginnt der Body
 * @return false, wenn der Kopf noch unvollständig oder ungültig ist
 */
bool parseRequestHead(const std::string& raw, Request& out, size_t& headBytes);

/** Inhalt von <LastChange> aus einem NOTIFY-Body, dekodiert (leer, wenn nicht vorhanden). */
std::string lastChange(const std::string& body);

/**
 * Attribut val="…" eines Elements im LastChange-XML, z. B. TransportState → "PLAYING".
 * @param channel nur Elemente mit diesem channel-Attribut (Lautstärke: "Master"), nullptr = egal
 */
bool eventValue(const std::string& lastChangeXml, const std::string& element, std::string& value,
                const char* channel = nullptr);

/** Antwort, die der Speaker auf ein NOTIFY erwartet. */
extern const char* const kNotifyResponse;

}  // namespace gena
}  // namespace sonos
