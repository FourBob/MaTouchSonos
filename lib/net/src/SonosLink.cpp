#include "SonosLink.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include <string>

#include "AVTransport.h"
#include "RenderingControl.h"
#include "Soap.h"

namespace net {
namespace {

constexpr uint16_t kSonosPort = 1400;
constexpr uint32_t kHttpConnectTimeoutMs = 1500;
constexpr uint16_t kHttpTimeoutMs = 2000;
constexpr uint32_t kPollIntervalMs = 1500;     // Zustand + Lautstärke abfragen
constexpr uint32_t kPollAfterCommandMs = 400;  // nach Play/Pause schnell bestätigen
constexpr uint32_t kRetryIntervalMs = 5000;    // nach Fehlern
constexpr uint32_t kWifiCheckMs = 500;
constexpr int kUpnpTransitionNotAvailable = 701;

const char* gSsid = nullptr;
const char* gPassword = nullptr;
const char* gSpeakerIp = nullptr;

QueueHandle_t gVolumeQueue = nullptr;     // int, Länge 1 (xQueueOverwrite)
QueueHandle_t gTransportQueue = nullptr;  // Transport, Länge 4
QueueHandle_t gEventQueue = nullptr;      // Event, Länge 16

void post(Event::Type type, int value = 0, const char* text = "") {
    Event e{};
    e.type = type;
    e.value = value;
    strlcpy(e.text, text, sizeof(e.text));
    if (xQueueSend(gEventQueue, &e, 0) != pdTRUE) {
        log_w("Event-Queue voll, Meldung verworfen");
    }
}

/** Führt eine SOAP-Anfrage aus. body enthält die Antwort (auch im Fehlerfall). */
sonos::SoapResult call(const sonos::SoapRequest& req, std::string& body) {
    HTTPClient http;
    http.setConnectTimeout(kHttpConnectTimeoutMs);
    http.setTimeout(kHttpTimeoutMs);
    http.setReuse(false);

    const String url = String("http://") + gSpeakerIp + ":" + kSonosPort + req.path.c_str();
    if (!http.begin(url)) {
        body.clear();
        return sonos::evaluateResponse(-1, body);
    }
    http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
    http.addHeader("SOAPACTION", req.soapAction.c_str());

    const uint32_t start = millis();
    const int status = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(req.body.data())), req.body.size());
    body = status > 0 ? std::string(http.getString().c_str()) : std::string();
    http.end();

    const sonos::SoapResult result = sonos::evaluateResponse(status, body);
    log_d("SOAP %s -> %d (%lu ms)", req.soapAction.c_str(), status, static_cast<unsigned long>(millis() - start));
    return result;
}

/** Zustand der Verbindung zum Speaker innerhalb der Task. */
struct Link {
    bool synced = false;          // Zustand und Lautstärke sind bekannt
    bool speakerFailing = false;  // letzter Kontakt fehlgeschlagen
    uint32_t nextPollAt = 0;

    void failed(const char* what, const std::string& error) {
        synced = false;
        speakerFailing = true;
        nextPollAt = millis() + kRetryIntervalMs;
        post(Event::Type::SpeakerError, 0, error.c_str());
        Serial.printf("SONOS %s FEHLER: %s\n", what, error.c_str());
    }
};

/** Wiedergabezustand und Lautstärke abfragen. */
void poll(Link& link) {
    std::string body;
    sonos::SoapResult r = call(sonos::avtransport::getTransportInfo(), body);
    sonos::TransportState state = sonos::TransportState::Unknown;
    if (!r.ok || !sonos::avtransport::parseTransportInfo(body, state)) {
        link.failed("GetTransportInfo", r.ok ? std::string("Unerwartete Antwort vom Speaker") : r.error);
        return;
    }

    r = call(sonos::rendering::getVolume(), body);
    int volume = 0;
    if (!r.ok || !sonos::rendering::parseGetVolume(body, volume)) {
        link.failed("GetVolume", r.ok ? std::string("Unerwartete Antwort vom Speaker") : r.error);
        return;
    }

    if (link.speakerFailing) {
        link.speakerFailing = false;
        post(Event::Type::SpeakerOk);
    }
    if (!link.synced) {
        Serial.printf("SONOS verbunden: Zustand %d, Lautstärke %d\n", static_cast<int>(state), volume);
    }
    link.synced = true;
    link.nextPollAt = millis() + kPollIntervalMs;
    post(Event::Type::TransportState, static_cast<int>(state));
    post(Event::Type::SpeakerVolume, volume);
}

void sendVolume(Link& link, int volume) {
    std::string body;
    const sonos::SoapResult r = call(sonos::rendering::setVolume(volume), body);
    if (r.ok) {
        Serial.printf("SONOS SetVolume %d ok\n", volume);
    } else {
        link.failed("SetVolume", r.error);
    }
}

void sendTransport(Link& link, Transport command) {
    const char* name = command == Transport::Play ? "Play" : "Pause";
    std::string body;
    sonos::SoapResult r =
        call(command == Transport::Play ? sonos::avtransport::play() : sonos::avtransport::pause(), body);

    // Manche Quellen (z. B. Radio-Streams) können nicht pausieren – dann stoppen.
    if (!r.ok && command == Transport::Pause && r.upnpErrorCode == kUpnpTransitionNotAvailable) {
        Serial.println(F("SONOS Pause nicht möglich – sende Stop"));
        name = "Stop";
        r = call(sonos::avtransport::stop(), body);
    }

    if (r.ok) {
        Serial.printf("SONOS %s ok\n", name);
        link.nextPollAt = millis() + kPollAfterCommandMs;
    } else if (r.httpStatus <= 0) {
        post(Event::Type::TransportError, 0, r.error.c_str());
        link.failed(name, r.error);  // Verbindungsproblem
    } else {
        // Der Speaker ist erreichbar, lehnt aber ab (z. B. Gruppenmitglied, nichts in der Warteschlange).
        post(Event::Type::TransportError, r.upnpErrorCode, r.error.c_str());
        Serial.printf("SONOS %s abgelehnt: %s\n", name, r.error.c_str());
        link.nextPollAt = millis() + kPollAfterCommandMs;
    }
}

void task(void*) {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("MaTouchSonos");
    WiFi.setSleep(false);  // geringere Latenz; Gerät hängt ohnehin am USB-Netzteil
    WiFi.setAutoReconnect(true);

    post(Event::Type::WifiConnecting);
    WiFi.begin(gSsid, gPassword);

    bool wifiUp = false;
    uint32_t lastWifiCheck = 0;
    Link link;

    for (;;) {
        const uint32_t now = millis();

        // --- WLAN-Zustand überwachen -----------------------------------------
        if (now - lastWifiCheck >= kWifiCheckMs) {
            lastWifiCheck = now;
            const bool connected = WiFi.status() == WL_CONNECTED;
            if (connected && !wifiUp) {
                wifiUp = true;
                link.synced = false;
                link.nextPollAt = now;
                post(Event::Type::WifiConnected, 0, WiFi.localIP().toString().c_str());
                Serial.printf("WLAN verbunden, IP %s, RSSI %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
            } else if (!connected && wifiUp) {
                wifiUp = false;
                link.synced = false;
                post(Event::Type::WifiLost);
                Serial.println(F("WLAN verloren – verbinde neu"));
            }
        }

        if (!wifiUp) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // --- Befehle der UI (Lautstärke zuerst: sie fühlt sich am direktesten an) ----
        int volume;
        if (xQueueReceive(gVolumeQueue, &volume, 0) == pdTRUE && link.synced) {
            sendVolume(link, volume);
        }
        Transport command;
        if (xQueueReceive(gTransportQueue, &command, pdMS_TO_TICKS(20)) == pdTRUE && link.synced) {
            sendTransport(link, command);
        }

        // --- Regelmäßig abfragen (Start, Änderungen aus der App, Erholung nach Fehlern) --
        if (static_cast<int32_t>(millis() - link.nextPollAt) >= 0) {
            poll(link);
        }
    }
}

}  // namespace

void SonosLink::begin(const char* ssid, const char* password, const char* speakerIp) {
    gSsid = ssid;
    gPassword = password;
    gSpeakerIp = speakerIp;
    gVolumeQueue = xQueueCreate(1, sizeof(int));
    gTransportQueue = xQueueCreate(4, sizeof(Transport));
    gEventQueue = xQueueCreate(16, sizeof(Event));
    // Kern 0 (dort läuft auch der WLAN-Stack), UI bleibt auf Kern 1.
    xTaskCreatePinnedToCore(task, "sonos", 8192, nullptr, 1, nullptr, 0);
}

void SonosLink::setVolume(int volume) {
    if (gVolumeQueue) xQueueOverwrite(gVolumeQueue, &volume);
}

void SonosLink::transport(Transport command) {
    if (gTransportQueue && xQueueSend(gTransportQueue, &command, 0) != pdTRUE) {
        log_w("Transport-Queue voll, Befehl verworfen");
    }
}

bool SonosLink::pollEvent(Event& out) {
    return gEventQueue && xQueueReceive(gEventQueue, &out, 0) == pdTRUE;
}

}  // namespace net
