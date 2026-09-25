// Fernbedienung – Stand Schritt 3: Now Playing, Lautstärke, Play/Pause, Titel wechseln.
//
// Ablauf pro Schleifendurchlauf (Kern 1):
//   Drehring   -> VolumeController (optimistisch, Drossel)  -> SonosLink.setVolume
//   Taste kurz -> PlaybackController (optimistisch)         -> SonosLink.transport(Play/Pause)
//   Wischen    -> nächster / vorheriger Titel               -> SonosLink.transport(Next/Previous)
//   SonosLink-Ereignisse + Now-Playing-Momentaufnahme        -> Controller -> Anzeige
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
#include "NowPlaying.h"
#include "NowPlayingScreen.h"
#include "PlaybackController.h"
#include "ProgressTracker.h"
#include "SonosLink.h"
#include "VolumeController.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "include/secrets.h fehlt. Anlegen mit: cp include/secrets.example.h include/secrets.h (dann WLAN und Speaker-IP eintragen)"
#endif

namespace remote_app {
namespace {

constexpr uint32_t kMessageMs = 4000;      // Fehlermeldungen (z. B. „Nichts zum Abspielen“)
constexpr uint32_t kHintMs = 1500;         // kurze Hinweise (z. B. „Nächster Titel“)

app::ButtonDetector button;
app::VolumeController volume;
app::PlaybackController playback;
app::ProgressTracker progress;
NowPlayingScreen screen;
int32_t rawSinceLastDetent = 0;

net::NowPlayingInfo nowPlaying;  // letzte Momentaufnahme
bool hasNowPlaying = false;      // false nach Verbindungsverlust, bis eine neue Abfrage eintrifft
uint32_t nowPlayingVersion = 0;  // zuletzt übernommene Version (wird nie zurückgesetzt)
int lastShownSecond = -2;

// Statuszeile: dauerhafter Verbindungszustand + vorübergehende Meldung, die ihn kurz überdeckt.
char connectionText[64] = "";
NowPlayingScreen::Status connectionKind = NowPlayingScreen::Status::Info;
uint32_t messageUntil = 0;

bool secretsConfigured() {
    // Platzhalter aus secrets.example.h erkennen (z. B. CI-Build oder vergessen auszufüllen).
    return std::strcmp(WIFI_SSID, "MeinWLAN") != 0 && std::strlen(WIFI_SSID) > 0 && std::strlen(SONOS_IP) > 0;
}

// --- Statuszeile ---------------------------------------------------------------

void setConnectionStatus(const char* text, NowPlayingScreen::Status kind) {
    // Wird bei jeder Abfrage (alle 1,5 s) aufgerufen – nur bei Änderung neu zeichnen.
    if (kind == connectionKind && std::strcmp(text, connectionText) == 0) return;
    strlcpy(connectionText, text, sizeof(connectionText));
    connectionKind = kind;
    if (messageUntil == 0) screen.setStatus(connectionText, connectionKind);
}

void showMessage(const char* text, NowPlayingScreen::Status kind, uint32_t durationMs, uint32_t now) {
    screen.setStatus(text, kind);
    messageUntil = now + durationMs;
    if (messageUntil == 0) messageUntil = 1;
}

void expireMessage(uint32_t now) {
    if (messageUntil != 0 && static_cast<int32_t>(now - messageUntil) >= 0) {
        messageUntil = 0;
        screen.setStatus(connectionText, connectionKind);
    }
}

// --- Übersetzungen ---------------------------------------------------------------

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
        case 701: return "Nicht möglich – z. B. nichts in der Warteschlange";
        case 711: return "Kein weiterer Titel in der Warteschlange";
        case 800: return "Speaker ist Teil einer Gruppe – IP des Gruppen-Koordinators eintragen";
        default: return fallback;
    }
}

sonos::SourceKind currentSource() {
    return hasNowPlaying ? static_cast<sonos::SourceKind>(nowPlaying.kind) : sonos::SourceKind::None;
}

bool isPlaying() {
    return playback.state() == app::PlayState::Playing || playback.state() == app::PlayState::Transitioning;
}

// --- Anzeige aktualisieren -------------------------------------------------------

void showNowPlaying() {
    const sonos::SourceKind kind = currentSource();
    if (kind == sonos::SourceKind::None) {
        screen.setTrack("Nichts in der Warteschlange", "In der Sonos-App etwas starten", "");
    } else {
        screen.setTrack(nowPlaying.title, nowPlaying.subtitle, nowPlaying.album);
    }
    const bool withProgress = kind == sonos::SourceKind::Track && nowPlaying.durationSec > 0;
    progress.onRemote(nowPlaying.positionSec, withProgress ? nowPlaying.durationSec : 0, isPlaying(),
                      nowPlaying.fetchedAtMs);
    lastShownSecond = -2;  // Fortschritt beim nächsten Durchlauf neu zeichnen
}

void updateProgress(uint32_t now) {
    const int sec = progress.positionSec(now);
    if (sec == lastShownSecond) return;  // nur einmal pro Sekunde neu zeichnen
    lastShownSecond = sec;
    screen.setProgress(progress.known(), progress.permille(now), sec, progress.durationSec());
}

void showUnknownState() {
    volume.invalidate();
    playback.invalidate();
    progress.reset();
    hasNowPlaying = false;
    screen.setVolume(0, false);
    screen.setPlayState(app::PlayState::Unknown);
    screen.setTrack("", "", "");
    lastShownSecond = -2;
}

// --- Ereignisse ------------------------------------------------------------------

void handleNetEvent(const net::Event& e, uint32_t now) {
    using T = net::Event::Type;
    using S = NowPlayingScreen::Status;
    switch (e.type) {
        case T::WifiConnecting:
            setConnectionStatus("WLAN verbinden …", S::Info);
            break;
        case T::WifiConnected:
            setConnectionStatus("WLAN verbunden – frage Speaker …", S::Info);
            break;
        case T::WifiLost:
            showUnknownState();
            setConnectionStatus("WLAN getrennt – verbinde neu …", S::Error);
            break;
        case T::SpeakerVolume: {
            const bool wasKnown = volume.hasValue();
            if (volume.onRemoteVolume(e.value, now)) {
                screen.setVolume(volume.value(), true);
                if (wasKnown) screen.showVolumeOverlay(now);  // z. B. in der Sonos-App geändert
            }
            setConnectionStatus("", S::Ok);
            break;
        }
        case T::TransportState:
            if (playback.onRemoteState(toPlayState(e.value), now)) {
                screen.setPlayState(playback.state());
                progress.setPlaying(isPlaying(), now);
            }
            break;
        case T::TransportError:
            playback.onCommandFailed();
            screen.setPlayState(playback.state());
            progress.setPlaying(isPlaying(), now);
            showMessage(describeTransportError(e.value, e.text), S::Error, kMessageMs, now);
            break;
        case T::SpeakerOk:
            break;
        case T::SpeakerError:
            showUnknownState();
            setConnectionStatus(e.text, S::Error);
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
    progress.setPlaying(isPlaying(), now);
    net::SonosLink::transport(cmd == app::TransportCommand::Play ? net::Transport::Play : net::Transport::Pause);
    Serial.printf("BTN short -> %s\n", cmd == app::TransportCommand::Play ? "Play" : "Pause");
}

void onSwipe(NowPlayingScreen::Swipe dir) {
    const uint32_t now = millis();
    const bool next = dir == NowPlayingScreen::Swipe::Left;
    Serial.printf("SWIPE %s\n", next ? "links -> Next" : "rechts -> Previous");

    if (!hasNowPlaying) return;
    if (currentSource() != sonos::SourceKind::Track) {
        showMessage("Bei dieser Quelle nicht möglich", NowPlayingScreen::Status::Info, kHintMs, now);
        return;
    }
    net::SonosLink::transport(next ? net::Transport::Next : net::Transport::Previous);
    showMessage(next ? LV_SYMBOL_NEXT "  Nächster Titel" : LV_SYMBOL_PREV "  Vorheriger Titel",
                NowPlayingScreen::Status::Info, kHintMs, now);
}

}  // namespace

void setup() {
    diag::logBootInfo("Fernbedienung (Schritt 3)");

    if (!hal::Display::begin()) {
        Serial.println(F("FEHLER: Display-Initialisierung fehlgeschlagen"));
    }
    hal::Input::begin();
    screen.create(onSwipe);

    if (!secretsConfigured()) {
        setConnectionStatus("include/secrets.h ausfüllen: WLAN und Speaker-IP", NowPlayingScreen::Status::Error);
        Serial.println(F("FEHLER: include/secrets.h enthält noch die Platzhalter – siehe README"));
        return;
    }

    Serial.printf("Speaker: %s\n", SONOS_IP);
    net::SonosLink::begin(WIFI_SSID, WIFI_PASS, SONOS_IP);
}

void loop() {
    const uint32_t now = millis();

    // Drehring -> Lautstärke
    rawSinceLastDetent += hal::Input::takeRawSteps();
    const int32_t d = hal::Input::takeDetents();
    if (d != 0) {
        if (volume.onUserDetents(d, now)) screen.setVolume(volume.value(), true);
        if (volume.hasValue()) screen.showVolumeOverlay(now);
        // raw = Hardware-Zählerschritte (4 pro Rastung) – zeigt, ob Klicks verloren gehen.
        Serial.printf("ENC %+ld (raw %+ld) -> Lautstärke %d%s\n", static_cast<long>(d),
                      static_cast<long>(rawSinceLastDetent), volume.value(),
                      volume.hasValue() ? "" : " (Speaker noch unbekannt, ignoriert)");
        rawSinceLastDetent = 0;
    }
    int toSend;
    if (volume.takeValueToSend(now, toSend)) net::SonosLink::setVolume(toSend);

    // Taste
    switch (button.update(hal::Input::buttonRaw(), now)) {
        case app::ButtonEvent::Short: onShortPress(now); break;
        case app::ButtonEvent::Long: Serial.println(F("BTN long (Menü folgt in Schritt 4)")); break;
        case app::ButtonEvent::None: break;
    }

    // Netzwerk
    net::Event e;
    while (net::SonosLink::pollEvent(e)) handleNetEvent(e, now);
    if (net::SonosLink::takeNowPlaying(nowPlayingVersion, nowPlaying)) {
        nowPlayingVersion = nowPlaying.version;
        hasNowPlaying = true;
        showNowPlaying();
    }

    updateProgress(now);
    expireMessage(now);
    screen.tick(now);
    lv_timer_handler();
    diag::logStatusPeriodically(millis(), now);
    delay(5);
}

}  // namespace remote_app

#endif  // !MTS_HWTEST
