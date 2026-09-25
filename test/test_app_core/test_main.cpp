// Unit-Tests für die hardwareunabhängige Bedienlogik.
// Ausführen mit:  pio test -e native

#include <unity.h>

#include <vector>

#include "ButtonDetector.h"
#include "DetentTracker.h"
#include "ModeController.h"
#include "PlaybackController.h"
#include "ProgressTracker.h"
#include "VolumeController.h"

using app::ButtonDetector;
using app::ButtonEvent;
using app::DetentTracker;
using app::ModeController;
using app::PlaybackController;
using app::ProgressTracker;
using app::PlayState;
using app::TransportCommand;
using app::VolumeController;

void setUp() {}
void tearDown() {}

// --- DetentTracker ------------------------------------------------------------
// Eingabe: fortlaufende Schrittzahl des Hardware-Zählers (PCNT) + „steht in Ruhelage“.
// Vollschritt-Encoder: 4 Schritte pro Rastung.

void test_detent_one_click_each_direction() {
    DetentTracker t;
    t.reset(0);
    TEST_ASSERT_EQUAL_INT(+1, t.update(4, true));
    TEST_ASSERT_EQUAL_INT(-1, t.update(0, true));
}

void test_detent_nothing_while_between_detents() {
    DetentTracker t;
    t.reset(0);
    TEST_ASSERT_EQUAL_INT(0, t.update(1, false));
    TEST_ASSERT_EQUAL_INT(0, t.update(3, false));
    TEST_ASSERT_EQUAL_INT(+1, t.update(4, true));
}

void test_detent_fast_turn_many_clicks_between_samples() {
    // Regression Schritt 1: Schnell gedreht kamen nur ~2 von 10 Klicks an.
    // Der Hardware-Zähler verliert nichts; selbst wenn die Hauptschleife nur selten
    // abfragt, kommen beim nächsten Einrasten alle Klicks auf einmal an.
    DetentTracker t;
    t.reset(0);
    TEST_ASSERT_EQUAL_INT(10, t.update(40, true));
    TEST_ASSERT_EQUAL_INT(-7, t.update(12, true));
}

void test_detent_back_and_forth_single_clicks() {
    // Fehlerbild aus dem Geräte-Test von Schritt 0: ein Klick rechts, ein Klick links.
    DetentTracker t;
    t.reset(0);
    for (int i = 0; i < 10; ++i) {
        TEST_ASSERT_EQUAL_INT(+1, t.update(4, true));
        TEST_ASSERT_EQUAL_INT(-1, t.update(0, true));
    }
}

void test_detent_lost_step_causes_no_permanent_offset() {
    // Ein verlorener Schritt (3 statt 4) zählt trotzdem als Klick; danach wird in der
    // Ruhelage neu synchronisiert, Hin-und-her funktioniert weiter.
    DetentTracker t;
    t.reset(0);
    TEST_ASSERT_EQUAL_INT(+1, t.update(3, true));
    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL_INT(-1, t.update(-1, true));
        TEST_ASSERT_EQUAL_INT(+1, t.update(3, true));
    }
}

void test_detent_half_turn_and_back_is_nothing() {
    DetentTracker t;
    t.reset(0);
    TEST_ASSERT_EQUAL_INT(0, t.update(2, false));
    TEST_ASSERT_EQUAL_INT(0, t.update(0, true));
}

void test_detent_bounce_at_rest_is_nothing() {
    DetentTracker t;
    t.reset(100);
    TEST_ASSERT_EQUAL_INT(0, t.update(101, true));  // ein Prell-Schritt
    TEST_ASSERT_EQUAL_INT(0, t.update(100, true));
}

void test_detent_rounding_full_step() {
    DetentTracker t;
    t.reset(0);
    TEST_ASSERT_EQUAL_INT(1, t.update(2, true));   // halbe Strecke zählt
    TEST_ASSERT_EQUAL_INT(1, t.update(7, true));   // 5 -> 1
    TEST_ASSERT_EQUAL_INT(2, t.update(13, true));  // 6 -> 2
    TEST_ASSERT_EQUAL_INT(-1, t.update(11, true)); // -2 -> -1
}

void test_detent_half_step_encoder() {
    DetentTracker t(/*halfStep=*/true);
    t.reset(0);
    TEST_ASSERT_EQUAL_INT(+1, t.update(2, true));
    TEST_ASSERT_EQUAL_INT(+3, t.update(8, true));
    TEST_ASSERT_EQUAL_INT(-1, t.update(6, true));
}

void test_detent_first_update_only_initializes() {
    DetentTracker t;
    TEST_ASSERT_EQUAL_INT(0, t.update(1234, true));
    TEST_ASSERT_EQUAL_INT(+1, t.update(1238, true));
}

// --- VolumeController ----------------------------------------------------------

void test_volume_ignores_input_until_speaker_value_known() {
    VolumeController vc;
    TEST_ASSERT_FALSE(vc.onUserDetents(3, 1000));
    int out;
    TEST_ASSERT_FALSE(vc.takeValueToSend(1000, out));
    TEST_ASSERT_TRUE(vc.onRemoteVolume(20, 1000));
    TEST_ASSERT_EQUAL_INT(20, vc.value());
    TEST_ASSERT_FALSE(vc.takeValueToSend(1000, out));  // nichts zu senden
}

void test_volume_slow_turn_one_step_per_detent() {
    VolumeController vc;
    vc.onRemoteVolume(20, 0);
    vc.onUserDetents(+1, 1000);
    vc.onUserDetents(+1, 1200);
    vc.onUserDetents(-1, 1400);
    TEST_ASSERT_EQUAL_INT(21, vc.value());
}

void test_volume_is_sent_immediately_then_throttled_latest_wins() {
    VolumeController vc;
    vc.onRemoteVolume(20, 0);
    int out = -1;

    vc.onUserDetents(+1, 1000);
    TEST_ASSERT_TRUE(vc.takeValueToSend(1000, out));
    TEST_ASSERT_EQUAL_INT(21, out);

    // Weitere Rastungen innerhalb der Drossel: nichts senden ...
    vc.onUserDetents(+1, 1100);
    vc.onUserDetents(+1, 1200);
    TEST_ASSERT_FALSE(vc.takeValueToSend(1120, out));
    // ... nach Ablauf genau einmal den neuesten Wert.
    TEST_ASSERT_TRUE(vc.takeValueToSend(1200, out));
    TEST_ASSERT_EQUAL_INT(23, out);
    TEST_ASSERT_FALSE(vc.takeValueToSend(1500, out));
}

void test_volume_many_fast_events_send_few_messages() {
    VolumeController vc;
    vc.onRemoteVolume(10, 0);
    int sends = 0, out = 0;
    for (uint32_t t = 1000; t < 2000; t += 5) {  // Hauptschleife alle 5 ms
        if (t % 40 == 0) vc.onUserDetents(+1, t);   // eine Rastung alle 40 ms
        if (vc.takeValueToSend(t, out)) ++sends;
    }
    for (uint32_t t = 2000; t < 2300; t += 5) {
        if (vc.takeValueToSend(t, out)) ++sends;
    }
    TEST_ASSERT_LESS_OR_EQUAL_INT(8, sends);       // 1 s / 150 ms + letzter Wert
    TEST_ASSERT_EQUAL_INT(vc.value(), out);        // der letzte gesendete ist der Endwert
}

void test_volume_acceleration() {
    VolumeController vc;
    vc.onRemoteVolume(10, 0);
    vc.onUserDetents(+1, 1000);  // erste Rastung: 1
    vc.onUserDetents(+1, 1040);  // 40 ms: 2
    vc.onUserDetents(+1, 1050);  // 10 ms: 4
    TEST_ASSERT_EQUAL_INT(17, vc.value());
    vc.onUserDetents(+1, 2000);  // wieder langsam: 1
    TEST_ASSERT_EQUAL_INT(18, vc.value());
}

void test_volume_is_clamped_to_0_and_100() {
    VolumeController vc;
    vc.onRemoteVolume(98, 0);
    vc.onUserDetents(+5, 1000);
    TEST_ASSERT_EQUAL_INT(100, vc.value());
    TEST_ASSERT_FALSE(vc.onUserDetents(+1, 2000));  // keine Änderung mehr
    vc.onUserDetents(-200, 3000);
    TEST_ASSERT_EQUAL_INT(0, vc.value());
}

void test_volume_remote_value_ignored_while_user_turns() {
    VolumeController vc;
    vc.onRemoteVolume(20, 0);
    int out;
    vc.onUserDetents(+1, 1000);
    vc.takeValueToSend(1000, out);
    // Speaker meldet (veraltet) 20, während der Nutzer gerade dreht -> ignorieren
    TEST_ASSERT_FALSE(vc.onRemoteVolume(20, 1300));
    TEST_ASSERT_EQUAL_INT(21, vc.value());
    // Nach der Ruhezeit wird ein neuer Wert (z. B. aus der Sonos-App) übernommen
    TEST_ASSERT_TRUE(vc.onRemoteVolume(35, 2100));
    TEST_ASSERT_EQUAL_INT(35, vc.value());
}

void test_volume_remote_value_ignored_while_send_pending() {
    VolumeController vc;
    vc.onRemoteVolume(20, 0);
    vc.onUserDetents(+1, 1000);  // noch nicht gesendet
    TEST_ASSERT_FALSE(vc.onRemoteVolume(30, 5000));
    TEST_ASSERT_EQUAL_INT(21, vc.value());
}

void test_volume_invalidate_blocks_input_until_new_value() {
    VolumeController vc;
    vc.onRemoteVolume(20, 0);
    vc.invalidate();
    TEST_ASSERT_FALSE(vc.hasValue());
    TEST_ASSERT_FALSE(vc.onUserDetents(+1, 1000));
    TEST_ASSERT_TRUE(vc.onRemoteVolume(22, 2000));
    TEST_ASSERT_EQUAL_INT(22, vc.value());
}

// --- PlaybackController -------------------------------------------------------

void test_playback_no_toggle_while_unknown() {
    PlaybackController pc;
    TransportCommand cmd;
    TEST_ASSERT_FALSE(pc.toggle(0, cmd));
}

void test_playback_toggle_pauses_when_playing_and_plays_when_paused() {
    PlaybackController pc;
    TransportCommand cmd;
    pc.onRemoteState(PlayState::Playing, 0);
    TEST_ASSERT_TRUE(pc.toggle(1000, cmd));
    TEST_ASSERT_TRUE(cmd == TransportCommand::Pause);
    TEST_ASSERT_TRUE(pc.state() == PlayState::Paused);  // sofort (optimistisch)
    pc.onRemoteState(PlayState::Paused, 1200);          // Bestätigung
    TEST_ASSERT_TRUE(pc.toggle(2000, cmd));
    TEST_ASSERT_TRUE(cmd == TransportCommand::Play);
    TEST_ASSERT_TRUE(pc.state() == PlayState::Playing);
}

void test_playback_stopped_and_transitioning() {
    PlaybackController pc;
    TransportCommand cmd;
    pc.onRemoteState(PlayState::Stopped, 0);
    pc.toggle(10, cmd);
    TEST_ASSERT_TRUE(cmd == TransportCommand::Play);
    PlaybackController pc2;
    pc2.onRemoteState(PlayState::Transitioning, 0);
    pc2.toggle(10, cmd);
    TEST_ASSERT_TRUE(cmd == TransportCommand::Pause);
}

void test_playback_stale_state_ignored_during_holdoff() {
    PlaybackController pc(2500);
    TransportCommand cmd;
    pc.onRemoteState(PlayState::Paused, 0);
    pc.toggle(1000, cmd);                                        // -> Playing
    TEST_ASSERT_FALSE(pc.onRemoteState(PlayState::Paused, 1300));        // veraltet
    TEST_ASSERT_FALSE(pc.onRemoteState(PlayState::Transitioning, 1600)); // Puffern
    TEST_ASSERT_TRUE(pc.state() == PlayState::Playing);
}

void test_playback_speaker_wins_after_holdoff() {
    PlaybackController pc(2500);
    TransportCommand cmd;
    pc.onRemoteState(PlayState::Paused, 0);
    pc.toggle(1000, cmd);
    TEST_ASSERT_TRUE(pc.onRemoteState(PlayState::Paused, 4000));  // Speaker spielt nicht
    TEST_ASSERT_TRUE(pc.state() == PlayState::Paused);
}

void test_playback_follows_app_changes_when_idle() {
    PlaybackController pc;
    pc.onRemoteState(PlayState::Playing, 0);
    TEST_ASSERT_TRUE(pc.onRemoteState(PlayState::Paused, 5000));  // in der Sonos-App pausiert
    TEST_ASSERT_FALSE(pc.onRemoteState(PlayState::Paused, 6000)); // keine Änderung
}

void test_playback_command_failed_reverts() {
    PlaybackController pc;
    TransportCommand cmd;
    pc.onRemoteState(PlayState::Stopped, 0);
    pc.toggle(100, cmd);
    TEST_ASSERT_TRUE(pc.state() == PlayState::Playing);
    pc.onCommandFailed();
    TEST_ASSERT_TRUE(pc.state() == PlayState::Stopped);
}

void test_playback_invalidate() {
    PlaybackController pc;
    TransportCommand cmd;
    pc.onRemoteState(PlayState::Playing, 0);
    pc.invalidate();
    TEST_ASSERT_TRUE(pc.state() == PlayState::Unknown);
    TEST_ASSERT_FALSE(pc.toggle(10, cmd));
}

// --- ProgressTracker ----------------------------------------------------------

void test_progress_unknown_until_remote_value() {
    ProgressTracker p;
    TEST_ASSERT_FALSE(p.known());
    TEST_ASSERT_EQUAL_INT(-1, p.positionSec(1000));
    TEST_ASSERT_EQUAL_INT(0, p.permille(1000));
}

void test_progress_interpolates_while_playing() {
    ProgressTracker p;
    p.onRemote(62, 225, true, 10000);
    TEST_ASSERT_EQUAL_INT(62, p.positionSec(10000));
    TEST_ASSERT_EQUAL_INT(63, p.positionSec(11000));
    TEST_ASSERT_EQUAL_INT(65, p.positionSec(13400));
    TEST_ASSERT_EQUAL_INT(288, p.permille(13000));  // 65/225
}

void test_progress_frozen_while_paused() {
    ProgressTracker p;
    p.onRemote(62, 225, false, 10000);
    TEST_ASSERT_EQUAL_INT(62, p.positionSec(20000));
}

void test_progress_pause_and_resume_locally() {
    ProgressTracker p;
    p.onRemote(10, 100, true, 0);
    p.setPlaying(false, 5000);                   // Pause bei 15 s
    TEST_ASSERT_EQUAL_INT(15, p.positionSec(9000));
    p.setPlaying(true, 9000);                    // weiter
    TEST_ASSERT_EQUAL_INT(17, p.positionSec(11000));
}

void test_progress_clamped_to_duration() {
    ProgressTracker p;
    p.onRemote(220, 225, true, 0);
    TEST_ASSERT_EQUAL_INT(225, p.positionSec(60000));
    TEST_ASSERT_EQUAL_INT(1000, p.permille(60000));
}

void test_progress_remote_value_corrects() {
    ProgressTracker p;
    p.onRemote(10, 100, true, 0);
    p.onRemote(30, 100, true, 1500);             // z. B. in der App gespult
    TEST_ASSERT_EQUAL_INT(30, p.positionSec(1500));
}

void test_progress_no_duration_means_unknown() {
    ProgressTracker p;
    p.onRemote(754, 0, true, 0);                 // Radio
    TEST_ASSERT_FALSE(p.known());
    TEST_ASSERT_EQUAL_INT(0, p.permille(1000));
}

void test_progress_jump_to() {
    ProgressTracker p;
    p.onRemote(10, 100, true, 0);
    p.jumpTo(50, 1000);
    TEST_ASSERT_EQUAL_INT(51, p.positionSec(2000));
}

void test_progress_millis_wraparound() {
    ProgressTracker p;
    p.onRemote(10, 100, true, 0xFFFFFC18u);      // 1 s vor dem Überlauf
    TEST_ASSERT_EQUAL_INT(12, p.positionSec(1000u));
}

// --- ModeController -----------------------------------------------------------

using Act = ModeController::Action::Type;
using Mode = ModeController::Mode;
using Item = ModeController::MenuItem;

static ModeController::Context trackCtx(int pos = 60, int dur = 200) {
    ModeController::Context c;
    c.canScrub = true;
    c.positionSec = pos;
    c.durationSec = dur;
    return c;
}

void test_mode_normal_maps_ring_and_button() {
    ModeController m;
    TEST_ASSERT_TRUE(m.onDetents(+2, 100, trackCtx()).type == Act::Volume);
    TEST_ASSERT_EQUAL_INT(2, m.onDetents(+2, 200, trackCtx()).value);
    TEST_ASSERT_TRUE(m.onShortPress(300, trackCtx()).type == Act::TogglePlayPause);
    TEST_ASSERT_TRUE(m.mode() == Mode::Normal);
}

void test_mode_long_press_opens_and_closes_menu() {
    ModeController m;
    auto a = m.onLongPress(100);
    TEST_ASSERT_TRUE(a.type == Act::MenuOpened);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Item::Scrub), a.value);
    TEST_ASSERT_TRUE(m.mode() == Mode::Menu);
    TEST_ASSERT_TRUE(m.onLongPress(200).type == Act::MenuClosed);
    TEST_ASSERT_TRUE(m.mode() == Mode::Normal);
}

void test_mode_menu_selection_skips_disabled_and_wraps() {
    ModeController m;
    m.onLongPress(0);  // Spulen
    // Favoriten sind noch deaktiviert: Spulen -> Räume -> Schließen -> Spulen
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Item::Rooms), m.onDetents(+1, 10, trackCtx()).value);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Item::Close), m.onDetents(+1, 20, trackCtx()).value);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Item::Scrub), m.onDetents(+1, 30, trackCtx()).value);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Item::Close), m.onDetents(-1, 40, trackCtx()).value);
    TEST_ASSERT_TRUE(m.mode() == Mode::Menu);  // Drehen im Menü ändert keine Lautstärke
}

void test_mode_menu_close_item() {
    ModeController m;
    m.onLongPress(0);
    m.onDetents(-1, 10, trackCtx());  // rückwärts von Spulen: Schließen
    TEST_ASSERT_TRUE(m.onShortPress(20, trackCtx()).type == Act::MenuClosed);
    TEST_ASSERT_TRUE(m.mode() == Mode::Normal);
}

void test_mode_menu_times_out() {
    ModeController m(10000);
    m.onLongPress(1000);
    TEST_ASSERT_TRUE(m.tick(10999).type == Act::None);
    TEST_ASSERT_TRUE(m.tick(11000).type == Act::MenuClosed);
    TEST_ASSERT_TRUE(m.mode() == Mode::Normal);
    TEST_ASSERT_TRUE(m.tick(30000).type == Act::None);
}

void test_mode_scrub_starts_at_current_position() {
    ModeController m;
    m.onLongPress(0);
    auto a = m.onShortPress(10, trackCtx(62, 225));
    TEST_ASSERT_TRUE(a.type == Act::ScrubStarted);
    TEST_ASSERT_EQUAL_INT(62, a.value);
    TEST_ASSERT_TRUE(m.mode() == Mode::Scrub);
}

void test_mode_scrub_step_and_commit() {
    ModeController m;
    m.onLongPress(0);
    m.onShortPress(10, trackCtx(60, 1000));                                  // Schritt = 1 % = 10 s
    TEST_ASSERT_EQUAL_INT(70, m.onDetents(+1, 1000, trackCtx(60, 1000)).value);
    TEST_ASSERT_EQUAL_INT(60, m.onDetents(-1, 2000, trackCtx(60, 1000)).value);
    auto a = m.onShortPress(3000, trackCtx(60, 1000));
    TEST_ASSERT_TRUE(a.type == Act::ScrubCommitted);
    TEST_ASSERT_EQUAL_INT(60, a.value);
    TEST_ASSERT_TRUE(m.mode() == Mode::Normal);
}

void test_mode_scrub_minimum_step_and_fast_turn() {
    ModeController m;
    m.onLongPress(0);
    m.onShortPress(10, trackCtx(60, 200));                                   // 1 % = 2 s -> min. 5 s
    TEST_ASSERT_EQUAL_INT(65, m.onDetents(+1, 1000, trackCtx(60, 200)).value);
    TEST_ASSERT_EQUAL_INT(80, m.onDetents(+1, 1030, trackCtx(60, 200)).value);  // schnell: 3 × 5 s
}

void test_mode_scrub_clamped() {
    ModeController m;
    m.onLongPress(0);
    m.onShortPress(10, trackCtx(190, 200));
    TEST_ASSERT_EQUAL_INT(200, m.onDetents(+5, 1000, trackCtx(190, 200)).value);
    TEST_ASSERT_EQUAL_INT(0, m.onDetents(-100, 2000, trackCtx(190, 200)).value);
}

void test_mode_scrub_cancel_by_long_press_and_timeout() {
    ModeController m(10000);
    m.onLongPress(0);
    m.onShortPress(10, trackCtx());
    TEST_ASSERT_TRUE(m.onLongPress(100).type == Act::ScrubCancelled);
    TEST_ASSERT_TRUE(m.mode() == Mode::Normal);

    m.onLongPress(200);
    m.onShortPress(210, trackCtx());
    TEST_ASSERT_TRUE(m.tick(10210).type == Act::ScrubCancelled);
    TEST_ASSERT_TRUE(m.mode() == Mode::Normal);
}

void test_mode_scrub_not_available_for_radio() {
    ModeController m;
    m.onLongPress(0);
    ModeController::Context radio;  // canScrub = false
    auto a = m.onShortPress(10, radio);
    TEST_ASSERT_TRUE(a.type == Act::NotAvailable);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Item::Scrub), a.value);
    TEST_ASSERT_TRUE(m.mode() == Mode::Normal);
}

void test_mode_input_resets_timeout() {
    ModeController m(10000);
    m.onLongPress(0);
    m.onDetents(+1, 9000, trackCtx());
    TEST_ASSERT_TRUE(m.tick(15000).type == Act::None);  // erst 10 s nach der letzten Eingabe
    TEST_ASSERT_TRUE(m.tick(19000).type == Act::MenuClosed);
}

static ModeController::Context roomsCtx(int count, int current) {
    ModeController::Context c = trackCtx();
    c.roomCount = count;
    c.currentRoom = current;
    return c;
}

void test_mode_room_picker_opens_at_current_room() {
    ModeController m;
    m.onLongPress(0);
    m.onDetents(+1, 10, roomsCtx(7, 3));  // Räume
    auto a = m.onShortPress(20, roomsCtx(7, 3));
    TEST_ASSERT_TRUE(a.type == Act::RoomPickerOpened);
    TEST_ASSERT_EQUAL_INT(3, a.value);
    TEST_ASSERT_TRUE(m.mode() == Mode::RoomPicker);
}

void test_mode_room_picker_moves_clamped_and_selects() {
    ModeController m;
    m.onLongPress(0);
    m.onDetents(+1, 10, roomsCtx(7, 5));
    m.onShortPress(20, roomsCtx(7, 5));
    TEST_ASSERT_EQUAL_INT(6, m.onDetents(+1, 1000, roomsCtx(7, 5)).value);
    TEST_ASSERT_TRUE(m.onDetents(+3, 2000, roomsCtx(7, 5)).type == Act::None);  // Ende erreicht
    TEST_ASSERT_EQUAL_INT(0, m.onDetents(-20, 3000, roomsCtx(7, 5)).value);
    auto a = m.onShortPress(4000, roomsCtx(7, 5));
    TEST_ASSERT_TRUE(a.type == Act::RoomSelected);
    TEST_ASSERT_EQUAL_INT(0, a.value);
    TEST_ASSERT_TRUE(m.mode() == Mode::Normal);
}

void test_mode_room_picker_cancel_and_timeout() {
    ModeController m(10000);
    m.onLongPress(0);
    m.onDetents(+1, 10, roomsCtx(3, 0));
    m.onShortPress(20, roomsCtx(3, 0));
    TEST_ASSERT_TRUE(m.onLongPress(100).type == Act::RoomPickerCancelled);
    m.onLongPress(200);
    m.onDetents(+1, 210, roomsCtx(3, 0));
    m.onShortPress(220, roomsCtx(3, 0));
    TEST_ASSERT_TRUE(m.tick(10220).type == Act::RoomPickerCancelled);
    TEST_ASSERT_TRUE(m.mode() == Mode::Normal);
}

void test_mode_rooms_not_available_without_rooms() {
    ModeController m;
    m.onLongPress(0);
    m.onDetents(+1, 10, roomsCtx(0, 0));
    auto a = m.onShortPress(20, roomsCtx(0, 0));
    TEST_ASSERT_TRUE(a.type == Act::NotAvailable);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Item::Rooms), a.value);
}

// --- ButtonDetector ---------------------------------------------------------

// Hilfsfunktion: hält einen Pegel über eine Zeitspanne und sammelt Ereignisse.
static std::vector<ButtonEvent> hold(ButtonDetector& btn, bool pressed, uint32_t& t, uint32_t durationMs) {
    std::vector<ButtonEvent> events;
    // Zählschleife statt `t < end`, damit auch der Überlauf von millis() getestet werden kann.
    for (uint32_t elapsed = 0; elapsed < durationMs; elapsed += 5, t += 5) {
        ButtonEvent e = btn.update(pressed, t);
        if (e != ButtonEvent::None) events.push_back(e);
    }
    return events;
}

void test_button_short_press() {
    ButtonDetector btn(30, 600);
    uint32_t t = 1000;
    TEST_ASSERT_EQUAL_size_t(0, hold(btn, true, t, 200).size());
    auto events = hold(btn, false, t, 100);
    TEST_ASSERT_EQUAL_size_t(1, events.size());
    TEST_ASSERT_TRUE(events[0] == ButtonEvent::Short);
}

void test_button_long_press_fires_while_held_and_no_short_after() {
    ButtonDetector btn(30, 600);
    uint32_t t = 1000;
    auto held = hold(btn, true, t, 800);
    TEST_ASSERT_EQUAL_size_t(1, held.size());
    TEST_ASSERT_TRUE(held[0] == ButtonEvent::Long);
    TEST_ASSERT_EQUAL_size_t(0, hold(btn, false, t, 100).size());
}

void test_button_bounce_is_ignored() {
    ButtonDetector btn(30, 600);
    uint32_t t = 1000;
    // 10 ms-Störimpulse erzeugen nichts.
    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT_TRUE(btn.update(true, t) == ButtonEvent::None);
        t += 10;
        TEST_ASSERT_TRUE(btn.update(false, t) == ButtonEvent::None);
        t += 10;
    }
    TEST_ASSERT_EQUAL_size_t(0, hold(btn, false, t, 100).size());
    TEST_ASSERT_FALSE(btn.isPressed());
}

void test_button_two_short_presses() {
    ButtonDetector btn(30, 600);
    uint32_t t = 0;
    int shorts = 0;
    for (int i = 0; i < 2; ++i) {
        hold(btn, true, t, 150);
        for (auto e : hold(btn, false, t, 150)) shorts += (e == ButtonEvent::Short);
    }
    TEST_ASSERT_EQUAL_INT(2, shorts);
}

void test_button_millis_wraparound() {
    ButtonDetector btn(30, 600);
    uint32_t t = 0xFFFFFF00u;  // kurz vor dem Überlauf von millis()
    auto held = hold(btn, true, t, 800);
    TEST_ASSERT_EQUAL_size_t(1, held.size());
    TEST_ASSERT_TRUE(held[0] == ButtonEvent::Long);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_detent_one_click_each_direction);
    RUN_TEST(test_detent_nothing_while_between_detents);
    RUN_TEST(test_detent_fast_turn_many_clicks_between_samples);
    RUN_TEST(test_detent_back_and_forth_single_clicks);
    RUN_TEST(test_detent_lost_step_causes_no_permanent_offset);
    RUN_TEST(test_detent_half_turn_and_back_is_nothing);
    RUN_TEST(test_detent_bounce_at_rest_is_nothing);
    RUN_TEST(test_detent_rounding_full_step);
    RUN_TEST(test_detent_half_step_encoder);
    RUN_TEST(test_detent_first_update_only_initializes);
    RUN_TEST(test_volume_ignores_input_until_speaker_value_known);
    RUN_TEST(test_volume_slow_turn_one_step_per_detent);
    RUN_TEST(test_volume_is_sent_immediately_then_throttled_latest_wins);
    RUN_TEST(test_volume_many_fast_events_send_few_messages);
    RUN_TEST(test_volume_acceleration);
    RUN_TEST(test_volume_is_clamped_to_0_and_100);
    RUN_TEST(test_volume_remote_value_ignored_while_user_turns);
    RUN_TEST(test_volume_remote_value_ignored_while_send_pending);
    RUN_TEST(test_volume_invalidate_blocks_input_until_new_value);
    RUN_TEST(test_playback_no_toggle_while_unknown);
    RUN_TEST(test_playback_toggle_pauses_when_playing_and_plays_when_paused);
    RUN_TEST(test_playback_stopped_and_transitioning);
    RUN_TEST(test_playback_stale_state_ignored_during_holdoff);
    RUN_TEST(test_playback_speaker_wins_after_holdoff);
    RUN_TEST(test_playback_follows_app_changes_when_idle);
    RUN_TEST(test_playback_command_failed_reverts);
    RUN_TEST(test_playback_invalidate);
    RUN_TEST(test_progress_unknown_until_remote_value);
    RUN_TEST(test_progress_interpolates_while_playing);
    RUN_TEST(test_progress_frozen_while_paused);
    RUN_TEST(test_progress_pause_and_resume_locally);
    RUN_TEST(test_progress_clamped_to_duration);
    RUN_TEST(test_progress_remote_value_corrects);
    RUN_TEST(test_progress_no_duration_means_unknown);
    RUN_TEST(test_progress_jump_to);
    RUN_TEST(test_progress_millis_wraparound);
    RUN_TEST(test_mode_normal_maps_ring_and_button);
    RUN_TEST(test_mode_long_press_opens_and_closes_menu);
    RUN_TEST(test_mode_menu_selection_skips_disabled_and_wraps);
    RUN_TEST(test_mode_menu_close_item);
    RUN_TEST(test_mode_menu_times_out);
    RUN_TEST(test_mode_scrub_starts_at_current_position);
    RUN_TEST(test_mode_scrub_step_and_commit);
    RUN_TEST(test_mode_scrub_minimum_step_and_fast_turn);
    RUN_TEST(test_mode_scrub_clamped);
    RUN_TEST(test_mode_scrub_cancel_by_long_press_and_timeout);
    RUN_TEST(test_mode_scrub_not_available_for_radio);
    RUN_TEST(test_mode_input_resets_timeout);
    RUN_TEST(test_mode_room_picker_opens_at_current_room);
    RUN_TEST(test_mode_room_picker_moves_clamped_and_selects);
    RUN_TEST(test_mode_room_picker_cancel_and_timeout);
    RUN_TEST(test_mode_rooms_not_available_without_rooms);
    RUN_TEST(test_button_short_press);
    RUN_TEST(test_button_long_press_fires_while_held_and_no_short_after);
    RUN_TEST(test_button_bounce_is_ignored);
    RUN_TEST(test_button_two_short_presses);
    RUN_TEST(test_button_millis_wraparound);
    return UNITY_END();
}
