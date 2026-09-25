#include "SonosLink.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include <string>

#include "RenderingControl.h"
#include "Soap.h"

namespace net {
namespace {

constexpr uint16_t kSonosPort = 1400;
constexpr uint32_t kHttpConnectTimeoutMs = 1500;
constexpr uint16_t kHttpTimeoutMs = 2000;
constexpr uint32_t kRetryIntervalMs = 5000;  // GetVolume erneut nach Fehler
constexpr uint32_t kWifiCheckMs = 500;

const char* gSsid = nullptr;
const char* gPassword = nullptr;
const char* gSpeakerIp = nullptr;

QueueHandle_t gVolumeQueue = nullptr;  // int, Länge 1 (xQueueOverwrite)
QueueHandle_t gEventQueue = nullptr;   // Event, Länge 16

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

void task(void*) {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("MaTouchSonos");
    WiFi.setSleep(false);  // geringere Latenz; Gerät hängt ohnehin am USB-Netzteil
    WiFi.setAutoReconnect(true);

    post(Event::Type::WifiConnecting);
    WiFi.begin(gSsid, gPassword);

    bool wifiUp = false;
    bool needVolume = true;      // GetVolume ausstehend
    bool speakerFailing = false;
    uint32_t nextVolumeTry = 0;
    uint32_t lastWifiCheck = 0;

    for (;;) {
        const uint32_t now = millis();

        // --- WLAN-Zustand überwachen -----------------------------------------
        if (now - lastWifiCheck >= kWifiCheckMs) {
            lastWifiCheck = now;
            const bool connected = WiFi.status() == WL_CONNECTED;
            if (connected && !wifiUp) {
                wifiUp = true;
                needVolume = true;
                nextVolumeTry = now;
                post(Event::Type::WifiConnected, 0, WiFi.localIP().toString().c_str());
                Serial.printf("WLAN verbunden, IP %s, RSSI %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
            } else if (!connected && wifiUp) {
                wifiUp = false;
                post(Event::Type::WifiLost);
                Serial.println(F("WLAN verloren – verbinde neu"));
            }
        }

        if (!wifiUp) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // --- Lautstärke vom Speaker lesen (Start, nach Fehlern, nach WLAN-Abbruch) --
        if (needVolume && static_cast<int32_t>(now - nextVolumeTry) >= 0) {
            std::string body;
            const sonos::SoapResult r = call(sonos::rendering::getVolume(), body);
            int volume = 0;
            if (r.ok && sonos::rendering::parseGetVolume(body, volume)) {
                needVolume = false;
                if (speakerFailing) {
                    speakerFailing = false;
                    post(Event::Type::SpeakerOk);
                }
                post(Event::Type::SpeakerVolume, volume);
                Serial.printf("SONOS GetVolume = %d\n", volume);
            } else {
                speakerFailing = true;
                nextVolumeTry = now + kRetryIntervalMs;
                const char* msg = r.ok ? "Unerwartete Antwort vom Speaker" : r.error.c_str();
                post(Event::Type::SpeakerError, 0, msg);
                Serial.printf("SONOS GetVolume FEHLER: %s\n", msg);
            }
        }

        // --- Auf Lautstärke-Befehl der UI warten -------------------------------
        int volume;
        if (xQueueReceive(gVolumeQueue, &volume, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (needVolume) continue;  // Speaker gerade nicht erreichbar: verwerfen, UI ist gesperrt
            std::string body;
            const sonos::SoapResult r = call(sonos::rendering::setVolume(volume), body);
            if (r.ok) {
                Serial.printf("SONOS SetVolume %d ok\n", volume);
            } else {
                speakerFailing = true;
                needVolume = true;  // danach neu synchronisieren
                nextVolumeTry = millis() + kRetryIntervalMs;
                post(Event::Type::SpeakerError, 0, r.error.c_str());
                Serial.printf("SONOS SetVolume %d FEHLER: %s\n", volume, r.error.c_str());
            }
        }
    }
}

}  // namespace

void SonosLink::begin(const char* ssid, const char* password, const char* speakerIp) {
    gSsid = ssid;
    gPassword = password;
    gSpeakerIp = speakerIp;
    gVolumeQueue = xQueueCreate(1, sizeof(int));
    gEventQueue = xQueueCreate(16, sizeof(Event));
    // Kern 0 (dort läuft auch der WLAN-Stack), UI bleibt auf Kern 1.
    xTaskCreatePinnedToCore(task, "sonos", 8192, nullptr, 1, nullptr, 0);
}

void SonosLink::setVolume(int volume) {
    if (gVolumeQueue) xQueueOverwrite(gVolumeQueue, &volume);
}

bool SonosLink::pollEvent(Event& out) {
    return gEventQueue && xQueueReceive(gEventQueue, &out, 0) == pdTRUE;
}

}  // namespace net
