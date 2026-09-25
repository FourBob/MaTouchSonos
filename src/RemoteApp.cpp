// Fernbedienung – Stand Schritt 5: Now Playing, Lautstärke, Play/Pause, Titel wechseln,
// Ringmenü, Spulen und Raumwahl (Anlage wird automatisch gefunden).
//
// Ablauf pro Schleifendurchlauf (Kern 1):
//   Drehring/Taste -> ModeController (Normal / Menü / Spulen) entscheidet, was sie bedeuten:
//     Normal: Drehring -> VolumeController (optimistisch, Drossel) -> SonosLink.setVolume
//             Taste kurz -> PlaybackController (optimistisch)      -> SonosLink.transport(Play/Pause)
//             Taste lang -> Ringmenü
//     Menü:   Drehring wählt, kurz öffnet, lang schließt
//     Spulen: Drehring verschiebt die Zielposition, kurz -> SonosLink.seek, lang bricht ab
//     Raum:   Drehring wählt, kurz -> SonosLink.selectRoom (+ im NVS gemerkt), lang bricht ab
//   Wischen    -> rechts: nächster, links: vorheriger Titel              -> SonosLink.transport(Next/Previous)
//   SonosLink-Ereignisse + Now-Playing-Momentaufnahme        -> Controller -> Anzeige
// Das Netzwerk läuft in der SonosLink-Task auf Kern 0 und blockiert die UI nie.

#if !MTS_HWTEST

#include <Arduino.h>
#include <Preferences.h>
#include <lvgl.h>

#include <cstring>

#include "AVTransport.h"
#include "App.h"
#include "ButtonDetector.h"
#include "Diagnostics.h"
#include "Display.h"
#include "Input.h"
#include "ModeController.h"
#include "NowPlaying.h"
#include "NowPlayingScreen.h"
#include "PlaybackController.h"
#include "ProgressTracker.h"
#include "SonosLink.h"
#include "VolumeController.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "include/secrets.h fehlt. Anlegen mit: cp include/secrets.example.h include/secrets.h (dann WLAN eintragen)"
#endif
#ifndef SONOS_IP
#define SONOS_IP ""  // optional ab Schritt 5: die Anlage wird per SSDP gefunden
#endif
#ifndef SONOS_ROOM
#define SONOS_ROOM ""  // optional: bevorzugter Raum, mit dem das Gerät immer startet
#endif

namespace remote_app {
namespace {

constexpr uint32_t kMessageMs = 4000;      // Fehlermeldungen (z. B. „Nichts zum Abspielen“)
constexpr uint32_t kHintMs = 1500;         // kurze Hinweise (z. B. „Nächster Titel“)

app::ButtonDetector button;
app::VolumeController volume;
app::PlaybackController playback;
app::ProgressTracker progress;
app::ModeController modes;
NowPlayingScreen screen;
int32_t rawSinceLastDetent = 0;

net::RoomsInfo rooms;            // Räume/Gruppen der Anlage
uint32_t roomsVersion = 0;
char roomName[56] = "";          // aktiver Raum, steht in der Statuszeile
Preferences prefs;               // NVS: zuletzt gewählter Raum (Schlüssel "room")

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
    return std::strcmp(WIFI_SSID, "MeinWLAN") != 0 && std::strlen(WIFI_SSID) > 0;
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
        case 711: return "Anfang bzw. Ende der Warteschlange erreicht";
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
        case T::Discovering:
            setConnectionStatus("Suche Sonos-Anlage …", S::Info);
            break;
        case T::NoSpeakers:
            setConnectionStatus("Keine Sonos-Anlage gefunden – suche weiter …", S::Error);
            break;
        case T::RoomChanged: {
            showUnknownState();
            strlcpy(roomName, e.text, sizeof(roomName));
            char text[80];
            snprintf(text, sizeof(text), "Verbinde mit %s …", roomName);
            setConnectionStatus(text, S::Info);
            break;
        }
        case T::SpeakerVolume: {
            const bool wasKnown = volume.hasValue();
            if (volume.onRemoteVolume(e.value, now)) {
                screen.setVolume(volume.value(), true);
                if (wasKnown) screen.showVolumeOverlay(now);  // z. B. in der Sonos-App geändert
            }
            setConnectionStatus(roomName, S::Info);  // im Normalbetrieb: Name des aktiven Raums
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

void togglePlayPause(uint32_t now) {
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
    // Nach rechts wischen = weiter (wie Blättern nach vorn), nach links = zurück.
    // Im Geräte-Test von Schritt 3 als intuitiver empfunden als umgekehrt.
    const bool next = dir == NowPlayingScreen::Swipe::Right;
    Serial.printf("SWIPE %s\n", next ? "rechts -> Next" : "links -> Previous");

    if (!hasNowPlaying || modes.mode() != app::ModeController::Mode::Normal) return;
    if (currentSource() != sonos::SourceKind::Track) {
        showMessage("Bei dieser Quelle nicht möglich", NowPlayingScreen::Status::Info, kHintMs, now);
        return;
    }
    net::SonosLink::transport(next ? net::Transport::Next : net::Transport::Previous);
    showMessage(next ? LV_SYMBOL_NEXT "  Nächster Titel" : LV_SYMBOL_PREV "  Vorheriger Titel",
                NowPlayingScreen::Status::Info, kHintMs, now);
}

/** Was der aktuelle Titel für Menü und Spulen zulässt. */
app::ModeController::Context modeContext(uint32_t now) {
    app::ModeController::Context c;
    c.canScrub = hasNowPlaying && progress.known();
    c.positionSec = progress.positionSec(now);
    c.durationSec = progress.durationSec();
    c.roomCount = rooms.count;
    c.currentRoom = rooms.selected;
    return c;
}

void showRoomPicker(int index) {
    const char* names[net::kMaxRooms];
    for (int i = 0; i < rooms.count; ++i) names[i] = rooms.rooms[i].display;
    screen.showRoomPicker(names, rooms.count, index, rooms.selected);
}

void selectRoom(int index, uint32_t now) {
    if (index < 0 || index >= rooms.count) return;
    if (index == rooms.selected) return;  // schon aktiv
    const net::RoomEntry& r = rooms.rooms[index];
    net::SonosLink::selectRoom(r.uuid);
    prefs.putString("room", r.uuid);  // für den nächsten Start merken
    Serial.printf("RAUM gewählt: %s (%s)\n", r.display, r.uuid);
    (void)now;
}

const char* menuItemName(int item) {
    switch (static_cast<app::ModeController::MenuItem>(item)) {
        case app::ModeController::MenuItem::Scrub: return "Spulen";
        case app::ModeController::MenuItem::Rooms: return "Räume";
        case app::ModeController::MenuItem::Favorites: return "Favoriten";
        case app::ModeController::MenuItem::Close: return "Schließen";
    }
    return "?";
}

/** Führt aus, was der ModeController entschieden hat. */
void apply(const app::ModeController::Action& a, uint32_t now) {
    using T = app::ModeController::Action::Type;
    switch (a.type) {
        case T::None:
            break;
        case T::Volume:
            if (volume.onUserDetents(a.value, now)) screen.setVolume(volume.value(), true);
            if (volume.hasValue()) screen.showVolumeOverlay(now);
            break;
        case T::TogglePlayPause:
            togglePlayPause(now);
            break;
        case T::MenuOpened:
        case T::MenuMoved:
            screen.showMenu(a.value);
            Serial.printf("MENU %s\n", menuItemName(a.value));
            break;
        case T::MenuClosed:
            screen.hideMenu();
            Serial.println(F("MENU geschlossen"));
            break;
        case T::ScrubStarted:
            screen.hideMenu();
            screen.showScrub(a.value, progress.durationSec());
            Serial.printf("SCRUB Start bei %s\n", sonos::time::format(a.value).c_str());
            break;
        case T::ScrubMoved:
            screen.showScrub(a.value, progress.durationSec());
            break;
        case T::ScrubCommitted:
            screen.hideScrub();
            progress.jumpTo(a.value, now);  // sofort anzeigen, der Speaker bestätigt beim nächsten Abfragen
            lastShownSecond = -2;
            net::SonosLink::seek(a.value);
            Serial.printf("SCRUB -> Seek %s\n", sonos::time::format(a.value).c_str());
            break;
        case T::ScrubCancelled:
            screen.hideScrub();
            lastShownSecond = -2;
            Serial.println(F("SCRUB abgebrochen"));
            break;
        case T::RoomPickerOpened:
        case T::RoomPickerMoved:
            showRoomPicker(a.value);
            break;
        case T::RoomSelected:
            screen.hideRoomPicker();
            selectRoom(a.value, now);
            break;
        case T::RoomPickerCancelled:
            screen.hideRoomPicker();
            break;
        case T::NotAvailable: {
            screen.hideMenu();
            const auto item = static_cast<app::ModeController::MenuItem>(a.value);
            const char* text = item == app::ModeController::MenuItem::Scrub
                                   ? "Spulen geht nur bei Titeln mit bekannter Länge"
                                   : (item == app::ModeController::MenuItem::Rooms ? "Noch keine Räume gefunden"
                                                                                   : "Favoriten kommen in Schritt 7");
            showMessage(text, NowPlayingScreen::Status::Info, kMessageMs, now);
            break;
        }
    }
}

}  // namespace

void setup() {
    diag::logBootInfo("Fernbedienung (Schritt 5)");

    if (!hal::Display::begin()) {
        Serial.println(F("FEHLER: Display-Initialisierung fehlgeschlagen"));
    }
    hal::Input::begin();
    screen.create(onSwipe);

    if (!secretsConfigured()) {
        setConnectionStatus("include/secrets.h ausfüllen: WLAN-Name und Passwort", NowPlayingScreen::Status::Error);
        Serial.println(F("FEHLER: include/secrets.h enthält noch die Platzhalter – siehe README"));
        return;
    }

    prefs.begin("matouchsonos", false);
    // isKey() vorab: getString() auf einen fehlenden Schlüssel schreibt sonst eine [E]-Zeile ins Log.
    static String savedRoom = prefs.isKey("room") ? prefs.getString("room", "") : String("");
    Serial.printf("Bevorzugter Raum: %s, gemerkter Raum: %s, Start-IP: %s\n",
                  std::strlen(SONOS_ROOM) ? SONOS_ROOM : "(keiner)",
                  savedRoom.length() ? savedRoom.c_str() : "(keiner)",
                  std::strlen(SONOS_IP) ? SONOS_IP : "(keine, nur SSDP)");
    net::SonosLink::begin(WIFI_SSID, WIFI_PASS, SONOS_ROOM, savedRoom.c_str(), SONOS_IP);
}

void loop() {
    const uint32_t now = millis();

    // Drehring – Bedeutung je nach Modus (Lautstärke, Menüauswahl, Zielposition)
    rawSinceLastDetent += hal::Input::takeRawSteps();
    const int32_t d = hal::Input::takeDetents();
    if (d != 0) {
        const auto mode = modes.mode();
        apply(modes.onDetents(d, now, modeContext(now)), now);
        if (mode == app::ModeController::Mode::Normal) {
            // raw = Hardware-Zählerschritte (4 pro Rastung) – zeigt, ob Klicks verloren gehen.
            Serial.printf("ENC %+ld (raw %+ld) -> Lautstärke %d%s\n", static_cast<long>(d),
                          static_cast<long>(rawSinceLastDetent), volume.value(),
                          volume.hasValue() ? "" : " (Speaker noch unbekannt, ignoriert)");
        }
        rawSinceLastDetent = 0;
    }
    int toSend;
    if (volume.takeValueToSend(now, toSend)) net::SonosLink::setVolume(toSend);

    // Taste – ebenfalls je nach Modus
    switch (button.update(hal::Input::buttonRaw(), now)) {
        case app::ButtonEvent::Short: apply(modes.onShortPress(now, modeContext(now)), now); break;
        case app::ButtonEvent::Long: apply(modes.onLongPress(now), now); break;
        case app::ButtonEvent::None: break;
    }
    apply(modes.tick(now), now);  // Menü/Spulen nach 10 s ohne Eingabe schließen

    // Netzwerk
    net::Event e;
    while (net::SonosLink::pollEvent(e)) handleNetEvent(e, now);
    if (net::SonosLink::takeRooms(roomsVersion, rooms)) {
        roomsVersion = rooms.version;
        if (modes.mode() == app::ModeController::Mode::RoomPicker) {
            int idx = modes.roomPickerIndex();
            if (idx >= rooms.count) idx = rooms.count - 1;
            if (idx >= 0) showRoomPicker(idx);
        }
    }
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
