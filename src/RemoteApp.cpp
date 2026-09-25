// Fernbedienung – Stand Schritt 2: Lautstärke und Play/Pause eines festen Speakers.
//
// Ablauf pro Schleifendurchlauf (Kern 1):
//   Drehring -> VolumeController (optimistische Anzeige, Drossel) -> SonosLink.setVolume
//   Taste kurz -> PlaybackController (optimistische Anzeige)     -> SonosLink.transport
//   SonosLink-Ereignisse (WLAN, Zustand, Lautstärke, Fehler)       -> Controller -> Anzeige
// Das Netzwerk läuft in der SonosLink-Task auf Kern 0 und blockiert die UI nie.

#if !MTS_HWTEST

#include <Arduino.h>
#include <lvgl.h>

#include <cstring>

#include "AVTransport.h"
#include "App.h"
#include "ButtonDetector.h"
#include "Diagnostics.h"
#include "Display.h"
#include "Input.h"
#include "MainScreen.h"
#include "PlaybackController.h"
#include "SonosLink.h"
#include "VolumeController.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "include/secrets.h fehlt. Anlegen mit: cp include/secrets.example.h include/secrets.h (dann WLAN und Speaker-IP eintragen)"
#endif

namespace remote_app {
namespace {

constexpr uint32_t kMessageMs = 4000;  // Dauer vorübergehender Meldungen (z. B. „Nichts zum Abspielen“)

app::ButtonDetector button;
app::VolumeController volume;
app::PlaybackController playback;
MainScreen screen;
int32_t rawSinceLastDetent = 0;

// Statuszeile: dauerhafter Verbindungszustand + vorübergehende Meldung, die ihn kurz überdeckt.
char connectionText[64] = "";
MainScreen::Status connectionKind = MainScreen::Status::Info;
uint32_t messageUntil = 0;

bool secretsConfigured() {
    // Platzhalter aus secrets.example.h erkennen (z. B. CI-Build oder vergessen auszufüllen).
    return std::strcmp(WIFI_SSID, "MeinWLAN") != 0 && std::strlen(WIFI_SSID) > 0 && std::strlen(SONOS_IP) > 0;
}

void setConnectionStatus(const char* text, MainScreen::Status kind) {
    // Wird bei jeder Abfrage (alle 1,5 s) aufgerufen – nur bei Änderung neu zeichnen.
    if (kind == connectionKind && std::strcmp(text, connectionText) == 0) return;
    strlcpy(connectionText, text, sizeof(connectionText));
    connectionKind = kind;
    if (messageUntil == 0) screen.setStatus(connectionText, connectionKind);
}

void showMessage(const char* text, uint32_t now) {
    screen.setStatus(text, MainScreen::Status::Error);
    messageUntil = now + kMessageMs;
    if (messageUntil == 0) messageUntil = 1;
}

void expireMessage(uint32_t now) {
    if (messageUntil != 0 && static_cast<int32_t>(now - messageUntil) >= 0) {
        messageUntil = 0;
        screen.setStatus(connectionText, connectionKind);
    }
}

app::PlayState toPlayState(int transportState) {
    switch (static_cast<sonos::TransportState>(transportState)) {
        case sonos::TransportState::Stopped: return app::PlayState::Stopped;
        case sonos::TransportState::Playing: return app::PlayState::Playing;
        case sonos::TransportState::Paused: return app::PlayState::Paused;
        case sonos::TransportState::Transitioning: return app::PlayState::Transitioning;
        case sonos::TransportState::Unknown: break;
    }
    return app::PlayState::Unknown;
}

const char* describeTransportError(int upnpCode, const char* fallback) {
    switch (upnpCode) {
        case 701: return "Nichts zum Abspielen – erst in der Sonos-App etwas starten";
        case 800: return "Speaker ist Teil einer Gruppe – IP des Gruppen-Koordinators eintragen";
        default: return fallback;
    }
}

void showUnknownState() {
    volume.invalidate();
    playback.invalidate();
    screen.setVolume(0, false);
    screen.setPlayState(app::PlayState::Unknown);
}

void handleNetEvent(const net::Event& e, uint32_t now) {
    using T = net::Event::Type;
    switch (e.type) {
        case T::WifiConnecting:
            setConnectionStatus("WLAN verbinden …", MainScreen::Status::Info);
            break;
        case T::WifiConnected:
            setConnectionStatus("WLAN verbunden – frage Speaker …", MainScreen::Status::Info);
            break;
        case T::WifiLost:
            showUnknownState();
            setConnectionStatus("WLAN getrennt – verbinde neu …", MainScreen::Status::Error);
            break;
        case T::SpeakerVolume:
            if (volume.onRemoteVolume(e.value, now)) screen.setVolume(volume.value(), true);
            setConnectionStatus("", MainScreen::Status::Ok);
            break;
        case T::TransportState:
            if (playback.onRemoteState(toPlayState(e.value), now)) screen.setPlayState(playback.state());
            break;
        case T::TransportError:
            playback.onCommandFailed();
            screen.setPlayState(playback.state());
            showMessage(describeTransportError(e.value, e.text), now);
            break;
        case T::SpeakerOk:
            break;
        case T::SpeakerError:
            showUnknownState();
            setConnectionStatus(e.text, MainScreen::Status::Error);
            break;
    }
}

void onShortPress(uint32_t now) {
    app::TransportCommand cmd;
    if (!playback.toggle(now, cmd)) {
        Serial.println(F("BTN short – Zustand noch unbekannt, ignoriert"));
        return;
    }
    screen.setPlayState(playback.state());
    net::SonosLink::transport(cmd == app::TransportCommand::Play ? net::Transport::Play : net::Transport::Pause);
    Serial.printf("BTN short -> %s\n", cmd == app::TransportCommand::Play ? "Play" : "Pause");
}

}  // namespace

void setup() {
    diag::logBootInfo("Fernbedienung (Schritt 2)");

    if (!hal::Display::begin()) {
        Serial.println(F("FEHLER: Display-Initialisierung fehlgeschlagen"));
    }
    hal::Input::begin();

    static char label[48];
    snprintf(label, sizeof(label), "Sonos %s", SONOS_IP);
    screen.create(label);

    if (!secretsConfigured()) {
        setConnectionStatus("include/secrets.h ausfüllen: WLAN und Speaker-IP", MainScreen::Status::Error);
        Serial.println(F("FEHLER: include/secrets.h enthält noch die Platzhalter – siehe README"));
        return;
    }

    Serial.printf("Speaker: %s\n", SONOS_IP);
    net::SonosLink::begin(WIFI_SSID, WIFI_PASS, SONOS_IP);
}

void loop() {
    const uint32_t now = millis();

    rawSinceLastDetent += hal::Input::takeRawSteps();
    const int32_t d = hal::Input::takeDetents();
    if (d != 0) {
        if (volume.onUserDetents(d, now)) {
            screen.setVolume(volume.value(), true);
        }
        // raw = Hardware-Zählerschritte (4 pro Rastung) – zeigt, ob Klicks verloren gehen.
        Serial.printf("ENC %+ld (raw %+ld) -> Lautstärke %d%s\n", static_cast<long>(d),
                      static_cast<long>(rawSinceLastDetent), volume.value(),
                      volume.hasValue() ? "" : " (Speaker noch unbekannt, ignoriert)");
        rawSinceLastDetent = 0;
    }

    int toSend;
    if (volume.takeValueToSend(now, toSend)) {
        net::SonosLink::setVolume(toSend);
    }

    switch (button.update(hal::Input::buttonRaw(), now)) {
        case app::ButtonEvent::Short:
            onShortPress(now);
            break;
        case app::ButtonEvent::Long:
            Serial.println(F("BTN long (Menü folgt in Schritt 4)"));
            break;
        case app::ButtonEvent::None:
            break;
    }

    net::Event e;
    while (net::SonosLink::pollEvent(e)) handleNetEvent(e, now);
    expireMessage(now);

    lv_timer_handler();
    diag::logStatusPeriodically(millis(), now);
    delay(5);
}

}  // namespace remote_app

#endif  // !MTS_HWTEST
