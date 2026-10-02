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
#include "Highscores.h"
#include "ScoreScreen.h"
#include "AsteroidsGame.h"
#include "AsteroidsScreen.h"
#include "TubeGame.h"
#include "TubeScreen.h"
#include "PongGame.h"
#include "PongScreen.h"
#include "MissileGame.h"
#include "MissileScreen.h"
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
int gameHighscore = 0;            // bester Eintrag der Bestenliste (Anzeige im Spiel)
uint32_t gameLastRingMs = 0;      // letzte Ringbewegung im Spiel
bool gameWasTouched = false;

// Easteregg „Boxenstopp“ (Rennspiel). Das Spiel (Strecke ~9 KB) liegt im PSRAM, angelegt beim ersten Start.
constexpr uint32_t kRaceStepMs = 16;
constexpr float kRaceBrakeRadius = 140.0f;  // Bremse: Finger in der Bildschirmmitte
app::game::RaceGame* race = nullptr;
RaceScreen raceScreen;
uint32_t raceNextStepMs = 0;
uint32_t raceBestLapMs = 0;       // NVS-Schlüssel "race_lap" (schnellste Runde)
int raceSeenLaps = 0;             // beendete Runden, die schon auf Rundenrekord geprüft sind

// Bestenlisten (Top 5 mit drei Buchstaben). NVS: "hs_ring" (Punkte), "hs_race" (Zeiten), "hs_name"
// (zuletzt eingegebene Buchstaben). Ältere Einzelrekorde ("game_hi", "race_best") werden übernommen.
constexpr uint32_t kBoardDelayMs = 2000;  // so lange bleibt „Game Over“/„Ziel“ stehen
enum class Board : uint8_t { None, Pending, Entry, Table };
Board board = Board::None;
uint32_t boardAtMs = 0;           // Pending: ab dann Eingabe bzw. Liste
uint32_t boardValue = 0;          // erreichte Punkte bzw. Zeit
int boardRank = -1;               // Platz in der Liste (−1 = nicht drin)
app::game::HighscoreTable ringTable(false);
app::game::HighscoreTable raceTable(true);
app::game::HighscoreTable astTable(false);
app::game::HighscoreTable tubeTable(false);
app::game::HighscoreTable pongTable(false);
app::game::HighscoreTable missileTable(false);
app::game::InitialsEntry initials;
char lastInitials[4] = "AAA";
ScoreScreen scoreScreen;

// Easteregg „Asteroiden“ und „Röhrensturm“ (Vektorgrafik)
constexpr uint32_t kVectorStepMs = 16;
constexpr float kAstDegPerStep = 6.0f;         // Asteroiden: eine Ringumdrehung ≈ eine Schiffsumdrehung
constexpr float kZapRadius = 110.0f;           // Röhrensturm: Superzapper = Mitte antippen
app::game::AsteroidsGame asteroids;
AsteroidsScreen asteroidsScreen;
app::game::TubeGame tube;
TubeScreen tubeScreen;
uint32_t vectorNextStepMs = 0;
bool vectorWasTouched = false;
uint32_t vectorLastRingMs = 0;                 // Ringpong: Antippen zählt erst kurz nach dem Drehen
constexpr float kPongDegPerStep = 4.5f;
constexpr float kAimDegPerStep = 4.5f;         // Raketenabwehr: Fadenkreuz je Rohschritt
app::game::PongGame pong;
PongScreen pongScreen;
app::game::MissileGame missiles;
MissileScreen missileScreen;

enum class ActiveGame : uint8_t { Breakout, Race, Asteroids, Tube, Pong, Missile };
constexpr int kGameCount = 6;
const char* const kGameNames[kGameCount] = {"Ringbrecher", "Boxenstopp",  "Asteroiden",
                                            "Röhrensturm", "Ringpong",    "Raketenabwehr"};
const char* const kGameDetails[kGameCount] = {"Ring dreht den Schläger", "Ring lenkt, Mitte bremst",
                                              "Ring dreht, Drücken schießt, Berühren = Schub",
                                              "Ring wechselt die Bahn, Mitte = Superzapper",
                                              "Du gegen den Computer, 7 Punkte gewinnen",
                                              "Antippen = Abwehrrakete, schütze die Städte"};
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

// --- Bestenliste ---

void loadTable(const char* key, const char* legacyKey, app::game::HighscoreTable& table) {
    uint8_t buf[app::game::HighscoreTable::size()];
    bool ok = false;
    if (prefs.isKey(key) && prefs.getBytesLength(key) == sizeof(buf)) {
        prefs.getBytes(key, buf, sizeof(buf));
        ok = table.load(buf, sizeof(buf));
    }
    if (!ok) {
        table.clear(table.lowerIsBetter());
        if (legacyKey && *legacyKey && prefs.isKey(legacyKey)) table.insert("---", prefs.getUInt(legacyKey, 0));  // alter Einzelrekord
    }
    if (prefs.isKey("hs_name")) {
        String n = prefs.getString("hs_name", "AAA");
        strlcpy(lastInitials, n.c_str(), sizeof(lastInitials));
    }
}

void saveTable(const char* key, const app::game::HighscoreTable& table) {
    prefs.putBytes(key, table.data(), app::game::HighscoreTable::size());
}

app::game::HighscoreTable& activeTable() {
    switch (activeGame) {
        case ActiveGame::Race: return raceTable;
        case ActiveGame::Asteroids: return astTable;
        case ActiveGame::Tube: return tubeTable;
        case ActiveGame::Pong: return pongTable;
        case ActiveGame::Missile: return missileTable;
        case ActiveGame::Breakout: break;
    }
    return ringTable;
}
const char* activeTableKey() {
    switch (activeGame) {
        case ActiveGame::Race: return "hs_race";
        case ActiveGame::Asteroids: return "hs_ast";
        case ActiveGame::Tube: return "hs_tube";
        case ActiveGame::Pong: return "hs_pong";
        case ActiveGame::Missile: return "hs_mis";
        case ActiveGame::Breakout: break;
    }
    return "hs_ring";
}
const char* activeTitle() {
    switch (activeGame) {
        case ActiveGame::Race: return "BOXENSTOPP";
        case ActiveGame::Asteroids: return "ASTEROIDEN";
        case ActiveGame::Tube: return "ROEHRENSTURM";
        case ActiveGame::Pong: return "RINGPONG";
        case ActiveGame::Missile: return "RAKETENABWEHR";
        case ActiveGame::Breakout: break;
    }
    return "RINGBRECHER";
}
bool activeIsTime() { return activeGame == ActiveGame::Race; }

/** Ergebnis mit `name` eintragen und speichern. Liefert den Platz (−1 = nicht drin). */
int commitScore(const char* name, uint32_t value) {
    const int rank = activeTable().insert(name, value);
    if (rank >= 0) {
        saveTable(activeTableKey(), activeTable());
        Serial.printf("SPIEL Bestenliste: Platz %d für %s (%lu)\n", rank + 1, name, static_cast<unsigned long>(value));
    }
    return rank;
}

/** Spiel vorbei: Ergebnis kurz stehen lassen, danach Eingabe (falls in der Liste) bzw. Liste. */
void boardBegin(uint32_t now, uint32_t value) {
    board = Board::Pending;
    boardAtMs = now + kBoardDelayMs;
    boardValue = value;
    boardRank = -1;
}

void boardShow() {
    if (board == Board::Entry) {
        scoreScreen.showEntry(initials, boardValue, activeIsTime(), boardRank);
    } else if (board == Board::Table) {
        scoreScreen.showTable(activeTitle(), activeTable(), boardValue, activeIsTime(), boardRank);
    }
}

/** Aus „Pending“ weiter: in die Liste → Buchstaben eingeben, sonst gleich die Liste. */
void boardAdvance() {
    scoreScreen.reset();
    boardRank = activeTable().rankFor(boardValue);
    if (boardRank >= 0) {
        board = Board::Entry;
        initials.start(lastInitials);
    } else {
        board = Board::Table;
    }
    boardShow();
}

void finishEntry() {
    char name[4];
    initials.name(name);
    strlcpy(lastInitials, name, sizeof(lastInitials));
    prefs.putString("hs_name", name);
    boardRank = commitScore(name, boardValue);
    if (activeGame == ActiveGame::Breakout) gameHighscore = static_cast<int>(ringTable.best());
    board = Board::Table;
    boardShow();
}

void startGame(int index, uint32_t now);

/** Taste, solange Ergebnis/Eingabe/Liste zu sehen ist. */
void boardPress(uint32_t now) {
    switch (board) {
        case Board::Pending: boardAdvance(); break;
        case Board::Entry:
            initials.press();
            if (initials.done()) finishEntry();
            else boardShow();
            break;
        case Board::Table:  // nochmal
            board = Board::None;
            startGame(static_cast<int>(activeGame), now);
            break;
        case Board::None: break;
    }
}

/** Ein Durchlauf, solange die Bestenliste dran ist (das Spiel selbst ruht). */
void boardLoop(uint32_t now, int32_t detents) {
    if (board == Board::Pending && static_cast<int32_t>(now - boardAtMs) >= 0) boardAdvance();
    if (board == Board::Entry && detents != 0) {
        initials.rotate(static_cast<int>(detents));
        idle.onInput(now);
        boardShow();
    }
}

// --- Spiele ---

void startBreakout(uint32_t now) {
    loadTable("hs_ring", "game_hi", ringTable);
    gameHighscore = static_cast<int>(ringTable.best());
    game.reset();
    gameScreen.enter(game, gameHighscore);
    gameNextStepMs = now;
    board = Board::None;
    Serial.printf("SPIEL Ringbrecher gestartet (Rekord %d)\n", gameHighscore);
}

/**
 * Ringbrecher: Punkte vorläufig sichern (Ball verloren, Spiel abgebrochen) – mit den zuletzt
 * benutzten Buchstaben. So geht ein Rekord nicht verloren, wenn das Gerät mitten im Spiel ausgeht.
 * Die Liste im Speicher bleibt dabei unverändert; eingetragen wird erst am Ende.
 */
void saveProvisional(uint32_t score) {
    if (!activeTable().qualifies(score)) return;
    app::game::HighscoreTable copy = activeTable();
    copy.insert(lastInitials, score);
    saveTable(activeTableKey(), copy);
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
    loadTable("hs_race", "race_best", raceTable);
    raceBestLapMs = prefs.isKey("race_lap") ? prefs.getUInt("race_lap", 0) : 0;
    race->reset();
    raceSeenLaps = 0;
    raceScreen.enter(*race, raceTable.best(), raceBestLapMs);
    raceNextStepMs = now;
    board = Board::None;
    Serial.printf("SPIEL Boxenstopp gestartet (Bestzeit %lu ms)\n", static_cast<unsigned long>(raceTable.best()));
}

void startAsteroids(uint32_t now) {
    loadTable("hs_ast", "", astTable);
    asteroids.reset();
    asteroidsScreen.enter(asteroids, astTable.best());
    vectorNextStepMs = now;
    board = Board::None;
    Serial.printf("SPIEL Asteroiden gestartet (Rekord %lu)\n", static_cast<unsigned long>(astTable.best()));
}

void startTube(uint32_t now) {
    loadTable("hs_tube", "", tubeTable);
    tube.reset();
    tubeScreen.enter(tube, tubeTable.best());
    vectorNextStepMs = now;
    board = Board::None;
    Serial.printf("SPIEL Röhrensturm gestartet (Rekord %lu)\n", static_cast<unsigned long>(tubeTable.best()));
}

void startPong(uint32_t now) {
    loadTable("hs_pong", "", pongTable);
    pong.reset();
    pongScreen.enter(pong, pongTable.best());
    vectorNextStepMs = now;
    board = Board::None;
    Serial.printf("SPIEL Ringpong gestartet (Rekord %lu)\n", static_cast<unsigned long>(pongTable.best()));
}

void startMissile(uint32_t now) {
    loadTable("hs_mis", "", missileTable);
    missiles.reset();
    missileScreen.enter(missiles, missileTable.best());
    vectorNextStepMs = now;
    board = Board::None;
    Serial.printf("SPIEL Raketenabwehr gestartet (Rekord %lu)\n", static_cast<unsigned long>(missileTable.best()));
}

void startGame(int index, uint32_t now) {
    activeGame = index >= 0 && index < kGameCount ? static_cast<ActiveGame>(index) : ActiveGame::Breakout;
    switch (activeGame) {
        case ActiveGame::Race: startRace(now); break;
        case ActiveGame::Asteroids: startAsteroids(now); break;
        case ActiveGame::Tube: startTube(now); break;
        case ActiveGame::Pong: startPong(now); break;
        case ActiveGame::Missile: startMissile(now); break;
        case ActiveGame::Breakout: startBreakout(now); break;
    }
}

/** Punkte des laufenden Spiels (Punktespiele; 0 beim Rennen). */
uint32_t runningScore() {
    switch (activeGame) {
        case ActiveGame::Breakout: return static_cast<uint32_t>(game.score());
        case ActiveGame::Asteroids: return asteroids.state() == app::game::AsteroidsGame::State::Playing ? asteroids.score() : 0;
        case ActiveGame::Tube: return tube.state() != app::game::TubeGame::State::Ready ? tube.score() : 0;
        case ActiveGame::Pong: return pong.state() != app::game::PongGame::State::Ready ? pong.score() : 0;
        case ActiveGame::Missile: return missiles.state() != app::game::MissileGame::State::Ready ? missiles.score() : 0;
        case ActiveGame::Race: break;
    }
    return 0;
}

void gamePress(uint32_t now) {
    if (board != Board::None) {
        boardPress(now);
        return;
    }
    switch (activeGame) {
        case ActiveGame::Race:
            if (race) race->press();
            break;
        case ActiveGame::Asteroids: asteroids.press(); break;  // nur Start; geschossen wird beim Drücken
        case ActiveGame::Tube: tube.press(); break;            // nur Start; Feuer solange gedrückt
        case ActiveGame::Pong: pong.press(); break;
        case ActiveGame::Missile: missiles.press(); break;     // nur Start; geschossen wird beim Drücken
        case ActiveGame::Breakout: game.press(); break;
    }
}

void endGame() {
    lv_obj_invalidate(lv_scr_act());  // Oberfläche komplett neu zeichnen
    lastShownSecond = -2;
    // Beim Beenden nichts verlieren: laufende Eingabe übernehmen, sonst mit den letzten Buchstaben eintragen
    if (board == Board::Entry) {
        finishEntry();
    } else if (board == Board::Pending) {
        commitScore(lastInitials, boardValue);
    } else if (board == Board::None && runningScore() > 0) {
        commitScore(lastInitials, runningScore());  // mitten im Spiel beendet
    }
    board = Board::None;
    switch (activeGame) {
        case ActiveGame::Race:
            if (race) Serial.printf("SPIEL Boxenstopp beendet: Runde %d, %lu ms\n", race->lap(), static_cast<unsigned long>(race->raceMs()));
            break;
        case ActiveGame::Asteroids:
            Serial.printf("SPIEL Asteroiden beendet: %lu Punkte, Welle %d\n", static_cast<unsigned long>(asteroids.score()), asteroids.wave());
            break;
        case ActiveGame::Pong:
            Serial.printf("SPIEL Ringpong beendet: %lu Punkte, Level %d\n", static_cast<unsigned long>(pong.score()), pong.level());
            break;
        case ActiveGame::Missile:
            Serial.printf("SPIEL Raketenabwehr beendet: %lu Punkte, Welle %d\n", static_cast<unsigned long>(missiles.score()), missiles.wave());
            break;
        case ActiveGame::Tube:
            Serial.printf("SPIEL Röhrensturm beendet: %lu Punkte, Level %d\n", static_cast<unsigned long>(tube.score()), tube.level());
            break;
        case ActiveGame::Breakout:
            Serial.printf("SPIEL beendet: %d Punkte, Level %d\n", game.score(), game.level());
            break;
    }
}

/** Ein Durchlauf im Rennspiel: Ring = Lenkrad (in der Box: Auswahl), Mitte antippen = Bremse. */
void raceLoop(uint32_t now, int32_t rawSteps, int32_t detents) {
    if (!race) return;
    if (board != Board::None) {
        boardLoop(now, detents);
        return;
    }
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
    if (race->completedLaps() > raceSeenLaps) {
        // Rundenrekord sofort sichern – nicht erst im Ziel
        raceSeenLaps = race->completedLaps();
        const uint32_t lapMs = race->lastLapTimeMs();
        if (lapMs > 0 && (raceBestLapMs == 0 || lapMs < raceBestLapMs)) {
            raceBestLapMs = lapMs;
            prefs.putUInt("race_lap", raceBestLapMs);
            Serial.printf("SPIEL Boxenstopp neue beste Runde: %lu ms\n", static_cast<unsigned long>(lapMs));
        }
    }
    if (before != race->state() && race->state() == app::game::RaceGame::State::Finished) {
        Serial.printf("SPIEL Boxenstopp im Ziel: %lu ms (Bestzeit bisher %lu ms)\n",
                      static_cast<unsigned long>(race->raceMs()), static_cast<unsigned long>(raceTable.best()));
        raceScreen.render(*race, raceTable.best(), raceBestLapMs);  // „Neue Bestzeit!“ gegen die bisherige
        boardBegin(now, race->raceMs());
        return;
    }
    raceScreen.render(*race, raceTable.best(), raceBestLapMs);
}

/** Feste 60-Hz-Schritte für die Vektorspiele (holt höchstens 4 Schritte nach). */
template <typename F>
void vectorSteps(uint32_t now, F stepFn) {
    int steps = 0;
    while (static_cast<int32_t>(now - vectorNextStepMs) >= 0 && steps < 4) {
        stepFn();
        vectorNextStepMs += kVectorStepMs;
        ++steps;
    }
    if (static_cast<int32_t>(now - vectorNextStepMs) >= 0) vectorNextStepMs = now + kVectorStepMs;
}

/** Asteroiden: Ring dreht, Drücken schießt (sofort beim Drücken), Berühren = Schub. */
void asteroidsLoop(uint32_t now, int32_t rawSteps, int32_t detents, bool pressEdge) {
    if (board != Board::None) {
        boardLoop(now, detents);
        return;
    }
    using S = app::game::AsteroidsGame::State;
    if (rawSteps != 0) {
        asteroids.rotate(rawSteps * kAstDegPerStep);
        idle.onInput(now);
    }
    if (pressEdge) asteroids.fire();
    int16_t tx, ty;
    const bool touched = hal::Touch::read(tx, ty);
    asteroids.setThrust(touched);
    if (touched) idle.onInput(now);
    const int livesBefore = asteroids.lives();
    const auto before = asteroids.state();
    vectorSteps(now, [] { asteroids.step(kVectorStepMs / 1000.0f); });
    if (asteroids.state() == S::Playing) idle.onInput(now);
    if (asteroids.lives() < livesBefore && asteroids.state() == S::Playing) saveProvisional(asteroids.score());
    asteroidsScreen.render(asteroids, astTable.best());
    if (before != asteroids.state() && asteroids.state() == S::GameOver) boardBegin(now, asteroids.score());
}

/** Röhrensturm: Ring wechselt die Bahn, Taste gedrückt halten = Dauerfeuer, Mitte antippen = Superzapper. */
void tubeLoop(uint32_t now, int32_t rawSteps, int32_t detents, bool buttonHeld) {
    if (board != Board::None) {
        boardLoop(now, detents);
        return;
    }
    using S = app::game::TubeGame::State;
    if (rawSteps != 0) {
        tube.rotate(rawSteps);
        idle.onInput(now);
    }
    tube.setFire(buttonHeld && tube.state() != S::Ready);
    int16_t tx, ty;
    const bool touched = hal::Touch::read(tx, ty);
    if (touched && !vectorWasTouched) {
        const float dx = tx - 240.0f, dy = ty - 240.0f;
        if (dx * dx + dy * dy < kZapRadius * kZapRadius) tube.zap();
    }
    if (touched) idle.onInput(now);
    vectorWasTouched = touched;
    const int livesBefore = tube.lives();
    const auto before = tube.state();
    vectorSteps(now, [] { tube.step(kVectorStepMs / 1000.0f); });
    if (tube.state() != S::Ready && tube.state() != S::GameOver) idle.onInput(now);
    if (tube.lives() < livesBefore && tube.state() != S::GameOver) saveProvisional(tube.score());
    tubeScreen.render(tube, tubeTable.best());
    if (before != tube.state() && tube.state() == S::GameOver) boardBegin(now, tube.score());
}

/** Ringpong: Ring = Schläger, Antippen am Rand = Schläger dorthin (nicht während gedreht wird). */
void pongLoop(uint32_t now, int32_t rawSteps, int32_t detents) {
    if (board != Board::None) {
        boardLoop(now, detents);
        return;
    }
    using S = app::game::PongGame::State;
    if (rawSteps != 0) {
        pong.movePaddle(rawSteps * kPongDegPerStep);
        vectorLastRingMs = now;
        idle.onInput(now);
    }
    int16_t tx, ty;
    const bool touched = hal::Touch::read(tx, ty);
    if (touched && !vectorWasTouched && now - vectorLastRingMs > kGameTouchAfterRingMs) {
        const float dx = tx - 240.0f, dy = ty - 240.0f;
        if (dx * dx + dy * dy > 80.0f * 80.0f) pong.setPaddle(atan2f(dy, dx) * 57.29578f);
    }
    if (touched) idle.onInput(now);
    vectorWasTouched = touched;
    const int cpuBefore = pong.cpuPoints();
    const auto before = pong.state();
    vectorSteps(now, [] { pong.step(kVectorStepMs / 1000.0f); });
    if (pong.state() != S::Ready && pong.state() != S::GameOver) idle.onInput(now);
    if (pong.cpuPoints() > cpuBefore && pong.state() != S::GameOver) saveProvisional(pong.score());
    pongScreen.render(pong, pongTable.best());
    if (before != pong.state() && pong.state() == S::GameOver) boardBegin(now, pong.score());
}

/** Raketenabwehr: Antippen = Abwehrrakete dorthin; Ring dreht das Fadenkreuz, Drücken feuert. */
void missileLoop(uint32_t now, int32_t rawSteps, int32_t detents, bool pressEdge) {
    if (board != Board::None) {
        boardLoop(now, detents);
        return;
    }
    using S = app::game::MissileGame::State;
    if (rawSteps != 0) {
        missiles.rotateAim(rawSteps * kAimDegPerStep);
        idle.onInput(now);
    }
    if (pressEdge) missiles.fireAtAim();
    int16_t tx, ty;
    const bool touched = hal::Touch::read(tx, ty);
    if (touched && !vectorWasTouched) missiles.fireAt(tx - 240.0f, ty - 240.0f);
    if (touched) idle.onInput(now);
    vectorWasTouched = touched;
    const int citiesBefore = missiles.citiesLeft();
    const auto before = missiles.state();
    vectorSteps(now, [] { missiles.step(kVectorStepMs / 1000.0f); });
    if (missiles.state() != S::Ready && missiles.state() != S::GameOver) idle.onInput(now);
    if (missiles.citiesLeft() < citiesBefore) saveProvisional(missiles.score());
    missileScreen.render(missiles, missileTable.best());
    if (before != missiles.state() && missiles.state() == S::GameOver) boardBegin(now, missiles.score());
}

/** Ein Durchlauf im Spiel: Eingaben, Spielschritte im 60-Hz-Takt, Anzeige. */
void gameLoop(uint32_t now, int32_t rawSteps, int32_t detents) {
    if (board != Board::None) {
        boardLoop(now, detents);
        return;
    }
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
    if (before == app::game::RingBreakout::State::Playing && game.state() == app::game::RingBreakout::State::Serving) {
        saveProvisional(static_cast<uint32_t>(game.score()));  // Ball verloren: Punkte schon jetzt sichern (falls das Gerät ausgeht)
    }
    if (before != game.state() && game.state() == app::game::RingBreakout::State::GameOver) {
        gameScreen.render(game, gameHighscore);  // „Neuer Rekord!“ gegen den bisherigen Rekord
        boardBegin(now, static_cast<uint32_t>(game.score()));
        return;
    }
    gameScreen.render(game, gameHighscore);
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
            gamePress(now);
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
    const bool buttonPressEdge = buttonRaw && !buttonWasPressed;  // Spiele: Schuss schon beim Drücken
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
        switch (activeGame) {
            case ActiveGame::Race: raceLoop(now, raw, d); break;
            case ActiveGame::Asteroids: asteroidsLoop(now, raw, d, buttonPressEdge); break;
            case ActiveGame::Tube: tubeLoop(now, raw, d, buttonRaw); break;
            case ActiveGame::Pong: pongLoop(now, raw, d); break;
            case ActiveGame::Missile: missileLoop(now, raw, d, buttonPressEdge); break;
            case ActiveGame::Breakout: gameLoop(now, raw, d); break;
        }
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
