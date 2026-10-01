// Fernbedienung – Stand Schritt 9: Now Playing mit Cover, Lautstärke, Play/Pause, Titel wechseln,
// Ringmenü, Spulen, Raumwahl (Anlage wird automatisch gefunden) und Favoriten.
//
// Ablauf pro Schleifendurchlauf (Kern 1):
//   Drehring/Taste -> ModeController (Normal / Menü / Spulen) entscheidet, was sie bedeuten:
//     Normal: Drehring -> VolumeController (optimistisch, Drossel) -> SonosLink.setVolume
//             Taste kurz -> PlaybackController (optimistisch)      -> SonosLink.transport(Play/Pause)
//             Taste lang -> Ringmenü
//     Menü:   Drehring wählt, kurz öffnet, lang schließt
//     Spulen: Drehring verschiebt die Zielposition, kurz -> SonosLink.seek, lang bricht ab
//     Raum:   Drehring wählt, kurz -> SonosLink.selectRoom (+ im NVS gemerkt), lang bricht ab
//     Favorit: Drehring wählt, kurz -> SonosLink.playFavorite, lang bricht ab
//   Wischen    -> rechts: nächster, links: vorheriger Titel              -> SonosLink.transport(Next/Previous)
//   SonosLink-Ereignisse + Now-Playing-Momentaufnahme        -> Controller -> Anzeige
// Das Netzwerk läuft in der SonosLink-Task auf Kern 0 und blockiert die UI nie.
// Energiesparen (IdleController): nach 30 s gedimmt, nach 2 min aus (nicht während Musik läuft).
// Eine Eingabe bei ausgeschaltetem Display weckt nur und wird verworfen.

#if !MTS_HWTEST

#include <Arduino.h>
#include <Preferences.h>
#include <lvgl.h>

#include <cmath>
#include <cstring>
#include <new>

#include "AVTransport.h"
#include "App.h"
#include "ButtonDetector.h"
#include "Diagnostics.h"
#include "CoverLoader.h"
#include "Display.h"
#include "GameScreen.h"
#include "IdleController.h"
#include "Input.h"
#include "ModeController.h"
#include "NowPlaying.h"
#include "NowPlayingScreen.h"
#include "PlaybackController.h"
#include "RaceGame.h"
#include "RaceScreen.h"
#include "ProgressTracker.h"
#include "RingBreakout.h"
#include "SonosLink.h"
#include "symbols.h"
#include "Touch.h"
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
constexpr float kGameDegPerStep = 4.5f;    // Spiel: Schläger-Drehung je Encoder-Rohschritt (18° je Rastung)
constexpr uint32_t kGameStepMs = 16;       // Spiel: ~60 Schritte/s
constexpr uint32_t kGameTouchAfterRingMs = 600;  // Spiel: Antippen zählt erst so lange nach dem Drehen
constexpr bool kEncoderDiagnostics = false; // true: jede Encoder-Bewegung loggen (ENC-DIAG), zur Fehlersuche

app::ButtonDetector button;
app::VolumeController volume;
app::PlaybackController playback;
app::ProgressTracker progress;
app::ModeController modes;
app::IdleController idle;        // Energiesparen: hell → gedimmt → aus
bool buttonWasPressed = false;   // Rohpegel im letzten Durchlauf (Flanke = Eingabe fürs Energiesparen)
bool buttonSwallow = false;      // dieser Tastendruck hat nur geweckt → Kurz/Lang verwerfen
NowPlayingScreen screen;
int32_t rawSinceLastDetent = 0;

net::RoomsInfo rooms;            // Räume/Gruppen der Anlage
uint32_t roomsVersion = 0;
char roomName[56] = "";          // aktiver Raum, steht in der Statuszeile
Preferences prefs;               // NVS: zuletzt gewählter Raum (Schlüssel "room")

uint32_t coverVersion = 0;       // zuletzt angezeigtes Cover

// Easteregg „Ringbrecher“ (Menüpunkt „Spiel“)
app::game::RingBreakout game;
GameScreen gameScreen;
uint32_t gameNextStepMs = 0;
int gameHighscore = 0;            // NVS-Schlüssel "game_hi"
uint32_t gameLastRingMs = 0;      // letzte Ringbewegung im Spiel
bool gameWasTouched = false;

// Easteregg „Boxenstopp“ (Rennspiel). Das Spiel (Strecke ~9 KB) liegt im PSRAM, angelegt beim ersten Start.
constexpr uint32_t kRaceStepMs = 16;
constexpr float kRaceBrakeRadius = 140.0f;  // Bremse: Finger in der Bildschirmmitte
app::game::RaceGame* race = nullptr;
RaceScreen raceScreen;
uint32_t raceNextStepMs = 0;
uint32_t raceBestMs = 0;          // NVS-Schlüssel "race_best"

enum class ActiveGame : uint8_t { Breakout, Race };
constexpr int kGameCount = 2;
const char* const kGameNames[kGameCount] = {"Ringbrecher", "Boxenstopp"};
const char* const kGameDetails[kGameCount] = {"Ring dreht den Schläger", "Ring lenkt, Mitte bremst"};
ActiveGame activeGame = ActiveGame::Breakout;

net::FavoritesInfo* favorites = nullptr;  // PSRAM (~10 KB), in setup() angelegt
uint32_t favoritesVersion = 0;

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
        case T::FavoriteStarted: {
            char text[80];
            snprintf(text, sizeof(text), LV_SYMBOL_PLAY "  %s", e.text);
            showMessage(text, S::Ok, kMessageMs, now);
            break;
        }
        case T::FavoriteFailed:
            showMessage(e.text, S::Error, kMessageMs, now);
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
    if (!idle.onInput(now)) {
        Serial.println(F("SWIPE weckt nur"));
        return;
    }
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
    c.favoriteCount = favorites ? favorites->count : 0;
    c.gameCount = kGameCount;
    return c;
}

void showRoomPicker(int index) {
    const char* names[net::kMaxRooms];
    for (int i = 0; i < rooms.count; ++i) names[i] = rooms.rooms[i].display;
    screen.showPicker(LV_SYMBOL_HOME "  Raum wählen", names, rooms.count, index, rooms.selected, "");
}

void showFavoritePicker(int index) {
    if (!favorites || favorites->count == 0) return;
    if (index >= favorites->count) index = favorites->count - 1;
    const char* names[net::kMaxFavorites];
    for (int i = 0; i < favorites->count; ++i) names[i] = favorites->items[i].title;
    screen.showPicker(LV_SYMBOL_AUDIO "  Favorit abspielen", names, favorites->count, index, -1,
                      favorites->items[index].detail);
}

void playFavorite(int index, uint32_t now) {
    if (!favorites || index < 0 || index >= favorites->count) return;
    const net::FavoriteEntry& f = favorites->items[index];
    net::SonosLink::playFavorite(index, f.title);
    char text[80];
    snprintf(text, sizeof(text), "Starte %s …", f.title);
    showMessage(text, NowPlayingScreen::Status::Info, kMessageMs, now);
    Serial.printf("FAVORIT gewählt: %s\n", f.title);
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
        case app::ModeController::MenuItem::Game: return "Spiel";
        case app::ModeController::MenuItem::Close: return "Schließen";
    }
    return "?";
}

// --- Spiel (Easteregg) -------------------------------------------------------------

void showGamePicker(int index) {
    if (index < 0 || index >= kGameCount) index = 0;
    screen.showPicker(MTS_SYMBOL_GAMEPAD "  Spiel wählen", kGameNames, kGameCount, index, -1, kGameDetails[index]);
}

void startBreakout(uint32_t now) {
    gameHighscore = prefs.isKey("game_hi") ? static_cast<int>(prefs.getUInt("game_hi", 0)) : 0;
    game.reset();
    gameScreen.enter(game, gameHighscore);
    gameNextStepMs = now;
    Serial.printf("SPIEL Ringbrecher gestartet (Rekord %d)\n", gameHighscore);
}

void saveHighscore() {
    if (game.score() > gameHighscore) {
        gameHighscore = game.score();
        prefs.putUInt("game_hi", static_cast<uint32_t>(gameHighscore));
        Serial.printf("SPIEL neuer Rekord: %d\n", gameHighscore);
    }
}

void startRace(uint32_t now) {
    if (!race) {
        void* mem = ps_malloc(sizeof(app::game::RaceGame));
        if (!mem) mem = malloc(sizeof(app::game::RaceGame));
        if (!mem) {
            Serial.println(F("SPIEL Boxenstopp: kein Speicher"));
            return;
        }
        race = new (mem) app::game::RaceGame();
    }
    raceBestMs = prefs.isKey("race_best") ? prefs.getUInt("race_best", 0) : 0;
    race->reset();
    raceScreen.enter(*race, raceBestMs);
    raceNextStepMs = now;
    Serial.printf("SPIEL Boxenstopp gestartet (Bestzeit %lu ms)\n", static_cast<unsigned long>(raceBestMs));
}

void startGame(int index, uint32_t now) {
    activeGame = index == 1 ? ActiveGame::Race : ActiveGame::Breakout;
    if (activeGame == ActiveGame::Race) startRace(now);
    else startBreakout(now);
}

void gamePress() {
    if (activeGame == ActiveGame::Race) {
        if (race) race->press();
    } else {
        game.press();
    }
}

void endGame() {
    lv_obj_invalidate(lv_scr_act());  // Oberfläche komplett neu zeichnen
    lastShownSecond = -2;
    if (activeGame == ActiveGame::Race) {
        if (race) Serial.printf("SPIEL Boxenstopp beendet: Runde %d, %lu ms\n", race->lap(), static_cast<unsigned long>(race->raceMs()));
        return;
    }
    saveHighscore();
    Serial.printf("SPIEL beendet: %d Punkte, Level %d\n", game.score(), game.level());
}

/** Ein Durchlauf im Rennspiel: Ring = Lenkrad (in der Box: Auswahl), Mitte antippen = Bremse. */
void raceLoop(uint32_t now, int32_t rawSteps, int32_t detents) {
    if (!race) return;
    if (race->state() == app::game::RaceGame::State::Pit) race->pitSelect(static_cast<int>(detents));
    else race->steer(rawSteps);
    if (rawSteps != 0) idle.onInput(now);
    int16_t tx, ty;
    bool brake = false;
    if (hal::Touch::read(tx, ty)) {
        const float dx = tx - 240.0f, dy = ty - 240.0f;
        brake = dx * dx + dy * dy < kRaceBrakeRadius * kRaceBrakeRadius;
        idle.onInput(now);
    }
    race->setBrake(brake);
    const auto before = race->state();
    int steps = 0;
    while (static_cast<int32_t>(now - raceNextStepMs) >= 0 && steps < 4) {
        race->step(kRaceStepMs / 1000.0f);
        raceNextStepMs += kRaceStepMs;
        ++steps;
    }
    if (static_cast<int32_t>(now - raceNextStepMs) >= 0) raceNextStepMs = now + kRaceStepMs;  // nicht aufholen
    if (race->state() != app::game::RaceGame::State::Finished) idle.onInput(now);  // nicht dimmen beim Fahren
    if (before != race->state() && race->state() == app::game::RaceGame::State::Finished) {
        const uint32_t previous = raceBestMs;
        if (raceBestMs == 0 || race->raceMs() < raceBestMs) {
            raceBestMs = race->raceMs();
            prefs.putUInt("race_best", raceBestMs);
        }
        Serial.printf("SPIEL Boxenstopp im Ziel: %lu ms (Bestzeit vorher %lu ms)\n",
                      static_cast<unsigned long>(race->raceMs()), static_cast<unsigned long>(previous));
        raceScreen.render(*race, previous);  // „Neue Bestzeit!“ gegen die alte prüfen
        return;
    }
    raceScreen.render(*race, raceBestMs);
}

/** Ein Durchlauf im Spiel: Eingaben, Spielschritte im 60-Hz-Takt, Anzeige. */
void gameLoop(uint32_t now, int32_t rawSteps) {
    if (rawSteps != 0) {
        game.movePaddle(rawSteps * kGameDegPerStep);
        gameLastRingMs = now;
        idle.onInput(now);
    }
    // Antippen: Schläger springt zum Finger – nur beim Aufsetzen und nicht, während am Ring gedreht wird.
    // Wer dreht, berührt oft den Glasrand; ein liegender Finger würde den Schläger sonst festhalten.
    int16_t tx, ty;
    const bool touched = hal::Touch::read(tx, ty);
    if (touched && !gameWasTouched && now - gameLastRingMs > kGameTouchAfterRingMs) {
        const float dx = tx - 240.0f, dy = ty - 240.0f;
        if (dx * dx + dy * dy > 80.0f * 80.0f) game.setPaddleAngle(atan2f(dy, dx) * 57.29578f);
    }
    if (touched) idle.onInput(now);
    gameWasTouched = touched;
    const auto before = game.state();
    int steps = 0;
    while (static_cast<int32_t>(now - gameNextStepMs) >= 0 && steps < 4) {
        game.step();
        gameNextStepMs += kGameStepMs;
        ++steps;
    }
    if (static_cast<int32_t>(now - gameNextStepMs) >= 0) gameNextStepMs = now + kGameStepMs;  // nicht aufholen
    if (game.state() == app::game::RingBreakout::State::Playing) idle.onInput(now);  // nicht dimmen beim Spielen
    if (before != game.state() && game.state() == app::game::RingBreakout::State::GameOver) {
        const int previous = gameHighscore;
        saveHighscore();
        gameScreen.render(game, previous);  // „Neuer Rekord!“ gegen den alten Rekord prüfen
        return;
    }
    gameScreen.render(game, gameHighscore);
}

/** Führt aus, was der ModeController entschieden hat. *//** Führt aus, was der ModeController entschieden hat. */
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
            net::SonosLink::refreshFavorites();  // im Hintergrund – bis „Favoriten“ gewählt ist, meist fertig
            screen.showMenu(a.value);
            Serial.printf("MENU %s\n", menuItemName(a.value));
            break;
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
            screen.hidePicker();
            selectRoom(a.value, now);
            break;
        case T::RoomPickerCancelled:
            screen.hidePicker();
            break;
        case T::FavoritePickerOpened:
        case T::FavoritePickerMoved:
            showFavoritePicker(a.value);
            break;
        case T::FavoriteSelected:
            screen.hidePicker();
            playFavorite(a.value, now);
            break;
        case T::FavoritePickerCancelled:
            screen.hidePicker();
            break;
        case T::GamePickerOpened:
            screen.hideMenu();
            showGamePicker(a.value);
            break;
        case T::GamePickerMoved:
            showGamePicker(a.value);
            break;
        case T::GamePickerCancelled:
            screen.hidePicker();
            break;
        case T::GameStarted:
            screen.hidePicker();
            startGame(a.value, now);
            break;
        case T::GameButton:
            gamePress();
            break;
        case T::GameEnded:
            endGame();
            break;
        case T::NotAvailable: {
            screen.hideMenu();
            const auto item = static_cast<app::ModeController::MenuItem>(a.value);
            const char* text = "";
            switch (item) {
                case app::ModeController::MenuItem::Scrub: text = "Spulen geht nur bei Titeln mit bekannter Länge"; break;
                case app::ModeController::MenuItem::Rooms: text = "Noch keine Räume gefunden"; break;
                case app::ModeController::MenuItem::Favorites:
                    text = favorites && favorites->loaded ? "Keine Favoriten – in der Sonos-App anlegen"
                                                          : "Favoriten werden noch geladen …";
                    break;
                case app::ModeController::MenuItem::Game:
                case app::ModeController::MenuItem::Close: break;
            }
            showMessage(text, NowPlayingScreen::Status::Info, kMessageMs, now);
            break;
        }
    }
}

}  // namespace

void setup() {
    diag::logBootInfo("Fernbedienung (Schritt 9)");

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
    favorites = static_cast<net::FavoritesInfo*>(ps_calloc(1, sizeof(net::FavoritesInfo)));
    net::CoverLoader::begin();
    net::SonosLink::begin(WIFI_SSID, WIFI_PASS, SONOS_ROOM, savedRoom.c_str(), SONOS_IP);
}

void loop() {
    const uint32_t now = millis();

    // Drehring – Bedeutung je nach Modus (Lautstärke, Menüauswahl, Zielposition)
    const int32_t raw = hal::Input::takeRawSteps();
    rawSinceLastDetent += raw;
    const int32_t d = hal::Input::takeDetents();
    if (kEncoderDiagnostics && raw != 0) {
        // Diagnose (Fehlersuche „erstes Menü reagiert nicht aufs Drehen“): Rohschritte, Kontakte, Rastungen
        bool a, b;
        hal::Input::encoderLevels(a, b);
        Serial.printf("ENC-DIAG roh %+ld A=%d B=%d -> Rastungen %+ld, Modus %d\n", static_cast<long>(raw), a, b,
                      static_cast<long>(d), static_cast<int>(modes.mode()));
    }
    if (d != 0 && !idle.onInput(now)) {
        Serial.printf("ENC %+ld weckt nur\n", static_cast<long>(d));  // Weck-Dreh ändert nichts
        rawSinceLastDetent = 0;
    } else if (d != 0) {
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

    // Taste – ebenfalls je nach Modus. Ein Druck, der das Display weckt, löst nichts aus.
    const bool buttonRaw = hal::Input::buttonRaw();
    if (buttonRaw && !buttonWasPressed) buttonSwallow = !idle.onInput(now);
    buttonWasPressed = buttonRaw;
    const app::ButtonEvent buttonEvent = button.update(buttonRaw, now);
    if (buttonEvent != app::ButtonEvent::None && buttonSwallow) {
        Serial.println(F("BTN weckt nur"));
    } else {
        switch (buttonEvent) {
            case app::ButtonEvent::Short: apply(modes.onShortPress(now, modeContext(now)), now); break;
            case app::ButtonEvent::Long: apply(modes.onLongPress(now), now); break;
            case app::ButtonEvent::None: break;
        }
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
    if (favorites && net::SonosLink::takeFavorites(favoritesVersion, *favorites)) {
        favoritesVersion = favorites->version;
        if (modes.mode() == app::ModeController::Mode::FavoritePicker) {
            if (favorites->count > 0) showFavoritePicker(modes.favoritePickerIndex());
            else apply(modes.onLongPress(now), now);  // Liste leer geworden: Auswahl schließen
        }
    }
    if (net::SonosLink::takeNowPlaying(nowPlayingVersion, nowPlaying)) {
        nowPlayingVersion = nowPlaying.version;
        hasNowPlaying = true;
        showNowPlaying();
    }

    net::CoverFrame cover;
    if (net::CoverLoader::takeCover(coverVersion, cover)) {
        coverVersion = cover.version;
        screen.setCover(cover.pixels);
        net::CoverLoader::acknowledge(cover.version);  // alter Puffer darf überschrieben werden
    }

    updateProgress(now);
    expireMessage(now);
    if (modes.mode() == app::ModeController::Mode::Game) {
        // Das Spiel zeichnet selbst (am LVGL vorbei) – LVGL pausiert so lange
        if (activeGame == ActiveGame::Race) raceLoop(now, raw, d);
        else gameLoop(now, raw);
        if (idle.tick(now, true)) hal::Display::setBacklight(idle.level());
        diag::logStatusPeriodically(millis(), now);
        delay(2);
        return;
    }

    screen.tick(now);
    lv_timer_handler();  // liest auch den Touch (Wischen → onSwipe)

    // Energiesparen: jede Berührung zählt als Eingabe; Helligkeit nachführen
    if (hal::Display::takeTouchActivity()) idle.onInput(now);
    if (idle.tick(now, isPlaying())) {
        hal::Display::setBacklight(idle.level());
        const auto st = idle.state();
        Serial.printf("DISPLAY %s\n", st == app::IdleController::State::Active ? "hell"
                                       : st == app::IdleController::State::Dimmed ? "gedimmt" : "aus");
    }
    diag::logStatusPeriodically(millis(), now);
    delay(5);
}

}  // namespace remote_app

#endif  // !MTS_HWTEST
