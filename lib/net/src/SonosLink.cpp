#include "SonosLink.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include <string>

#include "AVTransport.h"
#include "NowPlaying.h"
#include "RenderingControl.h"
#include "Soap.h"

namespace net {
namespace {

constexpr uint16_t kSonosPort = 1400;
constexpr uint32_t kHttpConnectTimeoutMs = 1000;
constexpr uint16_t kHttpTimeoutMs = 1200;      // Speaker antwortet normal in 30–60 ms
constexpr uint32_t kPollIntervalMs = 1500;     // Zustand, Titel, Lautstärke abfragen
constexpr uint32_t kPollAfterCommandMs = 400;  // nach einem Befehl schnell bestätigen
constexpr uint32_t kQuickRetryMs = 500;        // nach einem einzelnen Aussetzer
constexpr uint32_t kRetryIntervalMs = 5000;    // wenn der Speaker als nicht erreichbar gilt
constexpr int kFailuresBeforeError = 3;        // so viele Aussetzer in Folge bis zur Fehlermeldung
constexpr uint32_t kWifiCheckMs = 500;
constexpr int kUpnpTransitionNotAvailable = 701;

const char* gSsid = nullptr;
const char* gPassword = nullptr;
const char* gSpeakerIp = nullptr;

QueueHandle_t gVolumeQueue = nullptr;     // int, Länge 1 (xQueueOverwrite)
QueueHandle_t gTransportQueue = nullptr;  // Transport, Länge 4
QueueHandle_t gEventQueue = nullptr;      // Event, Länge 16

SemaphoreHandle_t gNowPlayingMutex = nullptr;
NowPlayingInfo gNowPlaying;  // geschützt durch gNowPlayingMutex

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

/** Wie call(), aber bei einem Verbindungsfehler (Aussetzer) sofort ein zweites Mal. */
sonos::SoapResult callWithRetry(const sonos::SoapRequest& req, std::string& body) {
    sonos::SoapResult r = call(req, body);
    if (!r.ok && r.httpStatus <= 0) {
        Serial.printf("SONOS Aussetzer (%s) – wiederhole\n", r.error.c_str());
        r = call(req, body);
    }
    return r;
}

/** Zustand der Verbindung zum Speaker innerhalb der Task. */
struct Link {
    bool synced = false;            // Zustand und Lautstärke sind bekannt
    bool speakerFailing = false;    // Fehler wurde an die UI gemeldet
    int consecutiveFailures = 0;
    uint32_t nextPollAt = 0;
    std::string lastTrackUri;       // für GetMediaInfo nur bei Quellenwechsel
    sonos::MediaInfo media;
    bool hasMedia = false;

    /** Kontakt fehlgeschlagen. Einzelne Aussetzer still wiederholen, erst dann melden. */
    void failed(const char* what, const std::string& error) {
        ++consecutiveFailures;
        Serial.printf("SONOS %s FEHLER (%d/%d): %s\n", what, consecutiveFailures, kFailuresBeforeError,
                      error.c_str());
        if (consecutiveFailures < kFailuresBeforeError) {
            nextPollAt = millis() + kQuickRetryMs;
            return;
        }
        synced = false;
        nextPollAt = millis() + kRetryIntervalMs;
        if (!speakerFailing) {
            speakerFailing = true;
            post(Event::Type::SpeakerError, 0, error.c_str());
        }
    }

    void succeeded() {
        consecutiveFailures = 0;
        if (speakerFailing) {
            speakerFailing = false;
            post(Event::Type::SpeakerOk);
        }
    }
};

void publishNowPlaying(const sonos::NowPlaying& np, uint32_t fetchedAt) {
    xSemaphoreTake(gNowPlayingMutex, portMAX_DELAY);
    gNowPlaying.version++;
    gNowPlaying.kind = static_cast<uint8_t>(np.kind);
    strlcpy(gNowPlaying.title, np.title.c_str(), sizeof(gNowPlaying.title));
    strlcpy(gNowPlaying.subtitle, np.subtitle.c_str(), sizeof(gNowPlaying.subtitle));
    strlcpy(gNowPlaying.album, np.album.c_str(), sizeof(gNowPlaying.album));
    strlcpy(gNowPlaying.albumArtUri, np.albumArtUri.c_str(), sizeof(gNowPlaying.albumArtUri));
    gNowPlaying.durationSec = np.durationSec;
    gNowPlaying.positionSec = np.positionSec;
    gNowPlaying.fetchedAtMs = fetchedAt;
    xSemaphoreGive(gNowPlayingMutex);
}

/** Wiedergabezustand, Titel und Lautstärke abfragen. */
void poll(Link& link) {
    std::string body;

    sonos::SoapResult r = call(sonos::avtransport::getTransportInfo(), body);
    sonos::TransportState state = sonos::TransportState::Unknown;
    if (!r.ok || !sonos::avtransport::parseTransportInfo(body, state)) {
        link.failed("GetTransportInfo", r.ok ? std::string("Unerwartete Antwort vom Speaker") : r.error);
        return;
    }

    r = call(sonos::avtransport::getPositionInfo(), body);
    const uint32_t fetchedAt = millis();
    sonos::PositionInfo pos;
    if (!r.ok || !sonos::parsePositionInfo(body, pos)) {
        link.failed("GetPositionInfo", r.ok ? std::string("Unerwartete Antwort vom Speaker") : r.error);
        return;
    }

    // Quelle gewechselt: GetMediaInfo liefert u. a. den Sendernamen bei Radio.
    if (pos.trackUri != link.lastTrackUri || !link.hasMedia) {
        r = call(sonos::avtransport::getMediaInfo(), body);
        link.hasMedia = r.ok && sonos::parseMediaInfo(body, link.media);
        link.lastTrackUri = pos.trackUri;
    }

    r = call(sonos::rendering::getVolume(), body);
    int volume = 0;
    if (!r.ok || !sonos::rendering::parseGetVolume(body, volume)) {
        link.failed("GetVolume", r.ok ? std::string("Unerwartete Antwort vom Speaker") : r.error);
        return;
    }

    link.succeeded();
    const sonos::NowPlaying np = sonos::buildNowPlaying(pos, link.hasMedia ? &link.media : nullptr);
    if (!link.synced) {
        Serial.printf("SONOS verbunden: Zustand %d, Lautstärke %d, „%s“\n", static_cast<int>(state), volume,
                      np.title.c_str());
    }
    link.synced = true;
    link.nextPollAt = millis() + kPollIntervalMs;
    publishNowPlaying(np, fetchedAt);
    post(Event::Type::TransportState, static_cast<int>(state));
    post(Event::Type::SpeakerVolume, volume);
}

void sendVolume(Link& link, int volume) {
    std::string body;
    const sonos::SoapResult r = callWithRetry(sonos::rendering::setVolume(volume), body);
    if (r.ok) {
        Serial.printf("SONOS SetVolume %d ok\n", volume);
    } else {
        link.failed("SetVolume", r.error);
    }
}

sonos::SoapRequest requestFor(Transport command) {
    switch (command) {
        case Transport::Play: return sonos::avtransport::play();
        case Transport::Pause: return sonos::avtransport::pause();
        case Transport::Next: return sonos::avtransport::next();
        case Transport::Previous: return sonos::avtransport::previous();
    }
    return sonos::avtransport::play();
}

const char* nameOf(Transport command) {
    switch (command) {
        case Transport::Play: return "Play";
        case Transport::Pause: return "Pause";
        case Transport::Next: return "Next";
        case Transport::Previous: return "Previous";
    }
    return "?";
}

void sendTransport(Link& link, Transport command) {
    const char* name = nameOf(command);
    std::string body;
    sonos::SoapResult r = callWithRetry(requestFor(command), body);

    // Manche Quellen (z. B. Radio-Streams) können nicht pausieren – dann stoppen.
    if (!r.ok && command == Transport::Pause && r.upnpErrorCode == kUpnpTransitionNotAvailable) {
        Serial.println(F("SONOS Pause nicht möglich – sende Stop"));
        name = "Stop";
        r = callWithRetry(sonos::avtransport::stop(), body);
    }

    if (r.ok) {
        Serial.printf("SONOS %s ok\n", name);
        link.nextPollAt = millis() + kPollAfterCommandMs;
    } else if (r.httpStatus <= 0) {
        post(Event::Type::TransportError, 0, r.error.c_str());
        link.failed(name, r.error);  // Verbindungsproblem
    } else {
        // Der Speaker ist erreichbar, lehnt aber ab (z. B. Gruppenmitglied, Radio ohne „Nächster“).
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
                link.consecutiveFailures = 0;
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
    gNowPlayingMutex = xSemaphoreCreateMutex();
    // Kern 0 (dort läuft auch der WLAN-Stack), UI bleibt auf Kern 1.
    // 12 KB Stack: XML-Auswertung der Titel-Metadaten braucht etwas mehr als in Schritt 2.
    xTaskCreatePinnedToCore(task, "sonos", 12288, nullptr, 1, nullptr, 0);
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

bool SonosLink::takeNowPlaying(uint32_t lastVersion, NowPlayingInfo& out) {
    if (!gNowPlayingMutex) return false;
    bool changed = false;
    xSemaphoreTake(gNowPlayingMutex, portMAX_DELAY);
    if (gNowPlaying.version != lastVersion) {
        out = gNowPlaying;
        changed = true;
    }
    xSemaphoreGive(gNowPlayingMutex);
    return changed;
}

}  // namespace net
