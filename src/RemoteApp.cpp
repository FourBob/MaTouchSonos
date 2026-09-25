// Fernbedienung – Stand Schritt 1: Lautstärke eines festen Speakers mit dem Drehring.
//
// Ablauf pro Schleifendurchlauf (Kern 1):
//   Drehring -> VolumeController (optimistische Anzeige, Drossel) -> SonosLink.setVolume
//   SonosLink-Ereignisse (WLAN, Speaker-Werte, Fehler) -> Anzeige
// Das Netzwerk läuft in der SonosLink-Task auf Kern 0 und blockiert die UI nie.

#if !MTS_HWTEST

#include <Arduino.h>
#include <lvgl.h>

#include <cstring>

#include "App.h"
#include "ButtonDetector.h"
#include "Diagnostics.h"
#include "Display.h"
#include "Input.h"
#include "SonosLink.h"
#include "VolumeController.h"
#include "VolumeScreen.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "include/secrets.h fehlt. Anlegen mit: cp include/secrets.example.h include/secrets.h (dann WLAN und Speaker-IP eintragen)"
#endif

namespace remote_app {
namespace {

app::ButtonDetector button;
app::VolumeController volume;
VolumeScreen screen;
int32_t rawSinceLastDetent = 0;

bool secretsConfigured() {
    // Platzhalter aus secrets.example.h erkennen (z. B. CI-Build oder vergessen auszufüllen).
    return std::strcmp(WIFI_SSID, "MeinWLAN") != 0 && std::strlen(WIFI_SSID) > 0 && std::strlen(SONOS_IP) > 0;
}

void showUnknownVolume() {
    volume.invalidate();
    screen.setVolume(0, false);
}

void handleNetEvent(const net::Event& e, uint32_t now) {
    using T = net::Event::Type;
    switch (e.type) {
        case T::WifiConnecting:
            screen.setStatus("WLAN verbinden …", VolumeScreen::Status::Info);
            break;
        case T::WifiConnected:
            screen.setStatus("WLAN verbunden – frage Speaker …", VolumeScreen::Status::Info);
            break;
        case T::WifiLost:
            showUnknownVolume();
            screen.setStatus("WLAN getrennt – verbinde neu …", VolumeScreen::Status::Error);
            break;
        case T::SpeakerVolume:
            if (volume.onRemoteVolume(e.value, now)) screen.setVolume(volume.value(), true);
            screen.setStatus("", VolumeScreen::Status::Ok);
            break;
        case T::SpeakerOk:
            break;
        case T::SpeakerError:
            showUnknownVolume();
            screen.setStatus(e.text, VolumeScreen::Status::Error);
            break;
    }
}

}  // namespace

void setup() {
    diag::logBootInfo("Fernbedienung (Schritt 1)");

    if (!hal::Display::begin()) {
        Serial.println(F("FEHLER: Display-Initialisierung fehlgeschlagen"));
    }
    hal::Input::begin();

    static char label[48];
    snprintf(label, sizeof(label), "Sonos %s", SONOS_IP);
    screen.create(label);

    if (!secretsConfigured()) {
        screen.setStatus("include/secrets.h ausfüllen: WLAN und Speaker-IP", VolumeScreen::Status::Error);
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
            Serial.println(F("BTN short (Play/Pause folgt in Schritt 2)"));
            break;
        case app::ButtonEvent::Long:
            Serial.println(F("BTN long (Menü folgt in Schritt 4)"));
            break;
        case app::ButtonEvent::None:
            break;
    }

    net::Event e;
    while (net::SonosLink::pollEvent(e)) handleNetEvent(e, now);

    lv_timer_handler();
    diag::logStatusPeriodically(millis(), now);
    delay(5);
}

}  // namespace remote_app

#endif  // !MTS_HWTEST
