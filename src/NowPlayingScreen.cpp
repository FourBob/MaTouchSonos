#include "NowPlayingScreen.h"

#include <lvgl.h>

#include <cstdio>
#include <cstring>

#include "ModeController.h"
#include "NowPlaying.h"
#include "symbols.h"

namespace {

constexpr uint32_t kAccent = 0x1DB954;
constexpr uint32_t kTrackBg = 0x1E1E1E;
constexpr uint32_t kInactive = 0x3A3A3A;
constexpr uint32_t kText = 0xFFFFFF;
constexpr uint32_t kTextDim = 0x9A9A9A;
constexpr uint32_t kTextFaint = 0x6E6E6E;
constexpr uint32_t kError = 0xEB5757;
constexpr uint32_t kOverlayMs = 2000;

NowPlayingScreen::SwipeHandler swipeHandler = nullptr;

// Now Playing
lv_obj_t* progressArc;
lv_obj_t* stateLabel;
lv_obj_t* titleLabel;
lv_obj_t* subtitleLabel;
lv_obj_t* albumLabel;
lv_obj_t* timeLabel;
lv_obj_t* statusLabel;

// Lautstärke-Einblendung
lv_obj_t* overlay;
lv_obj_t* volumeArc;
lv_obj_t* volumeLabel;
uint32_t overlayUntil = 0;

// Ringmenü
struct MenuEntry {
    const char* symbol;
    const char* name;
};
// Reihenfolge wie app::ModeController::MenuItem, im Uhrzeigersinn ab oben
const MenuEntry kMenu[app::ModeController::kMenuItemCount] = {
    {MTS_SYMBOL_BACKWARD MTS_SYMBOL_FORWARD, "Spulen"},  // ⏪⏩ – LV_SYMBOL_LOOP sähe aus wie „Wiederholen“
    {LV_SYMBOL_HOME, "Räume"},
    {LV_SYMBOL_AUDIO, "Favoriten"},
    {LV_SYMBOL_CLOSE, "Schließen"},
};
lv_obj_t* menuLayer;
lv_obj_t* menuBubbles[app::ModeController::kMenuItemCount];
lv_obj_t* menuName;
lv_obj_t* menuHint;

// Raumwahl (Drehrad mit 5 sichtbaren Zeilen)
constexpr int kPickerRows = 5;
lv_obj_t* pickerLayer;
lv_obj_t* pickerRows[kPickerRows];
lv_obj_t* pickerCounter;

// Spulen
lv_obj_t* scrubTimeLabel;
lv_obj_t* scrubHintLabel;
bool scrubbing = false;

bool volumeKnown = false;
app::PlayState playState = app::PlayState::Unknown;

/** Setzt Text nur, wenn er sich ändert – verhindert unnötiges Neuzeichnen und Neustart der Laufschrift. */
void setTextIfChanged(lv_obj_t* label, const char* text) {
    if (std::strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

lv_obj_t* makeLabel(lv_obj_t* parent, const lv_font_t* font, uint32_t color, int width, int y, bool scroll) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, width);
    lv_label_set_long_mode(l, scroll ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_DOT);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
    lv_label_set_text(l, "");
    return l;
}

lv_obj_t* makeArc(lv_obj_t* parent, int size, int width, int rotation, int sweep, int rangeMax) {
    lv_obj_t* a = lv_arc_create(parent);
    lv_obj_set_size(a, size, size);
    lv_obj_center(a);
    lv_arc_set_rotation(a, rotation);
    lv_arc_set_bg_angles(a, 0, sweep);
    lv_arc_set_range(a, 0, rangeMax);
    lv_arc_set_value(a, 0);
    lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, lv_color_hex(kTrackBg), LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, lv_color_hex(kAccent), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    return a;
}

void applyPlayStateColors() {
    const bool active = playState == app::PlayState::Playing || playState == app::PlayState::Transitioning;
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(active ? kText : kTextDim), 0);
    lv_obj_set_style_arc_color(progressArc, lv_color_hex(active ? kAccent : kTextFaint), LV_PART_INDICATOR);
    const uint32_t vol = !volumeKnown ? kInactive : (active ? kAccent : kTextFaint);
    lv_obj_set_style_arc_color(volumeArc, lv_color_hex(vol), LV_PART_INDICATOR);
}

void onGesture(lv_event_t*) {
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev || !swipeHandler) return;
    switch (lv_indev_get_gesture_dir(indev)) {
        case LV_DIR_LEFT: swipeHandler(NowPlayingScreen::Swipe::Left); break;
        case LV_DIR_RIGHT: swipeHandler(NowPlayingScreen::Swipe::Right); break;
        default: break;
    }
}

}  // namespace

void NowPlayingScreen::create(SwipeHandler onSwipe) {
    swipeHandler = onSwipe;

    lv_obj_t* scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, onGesture, LV_EVENT_GESTURE, nullptr);

    // --- Now Playing ---------------------------------------------------------
    progressArc = makeArc(scr, 464, 8, 270, 360, 1000);

    stateLabel = makeLabel(scr, &font_inter_28, kTextDim, 200, -135, false);
    titleLabel = makeLabel(scr, &font_inter_28, kText, 340, -45, true);
    subtitleLabel = makeLabel(scr, &font_inter_20, kTextDim, 330, 0, true);
    albumLabel = makeLabel(scr, &font_inter_14, kTextFaint, 300, 35, false);
    timeLabel = makeLabel(scr, &font_inter_14, kTextDim, 200, 100, false);

    statusLabel = makeLabel(scr, &font_inter_14, kTextDim, 300, 0, false);
    lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_align(statusLabel, LV_ALIGN_BOTTOM_MID, 0, -45);

    // --- Lautstärke-Einblendung (liegt darüber, zunächst unsichtbar) ------------
    overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, 480, 480);
    lv_obj_center(overlay);
    lv_obj_set_style_radius(overlay, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);  // deckend: schneller als Überblenden
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    volumeArc = makeArc(overlay, 440, 22, 135, 270, 100);
    lv_obj_t* caption = makeLabel(overlay, &font_inter_20, kTextDim, 200, -90, false);
    lv_label_set_text(caption, "Lautstärke");
    volumeLabel = lv_label_create(overlay);
    lv_obj_set_style_text_font(volumeLabel, &font_num_96, 0);
    lv_obj_set_style_text_color(volumeLabel, lv_color_white(), 0);
    lv_obj_align(volumeLabel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);

    // --- Spulen: große Zielzeit + Hinweis (auf dem Now-Playing-Bildschirm) --------
    scrubTimeLabel = makeLabel(scr, &font_inter_48, kText, 300, 105, false);
    scrubHintLabel = makeLabel(scr, &font_inter_14, kTextDim, 300, 150, false);
    lv_label_set_text(scrubHintLabel, "Drücken: springen · Lang: abbrechen");
    lv_obj_add_flag(scrubTimeLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(scrubHintLabel, LV_OBJ_FLAG_HIDDEN);

    // --- Ringmenü (deckend, über allem) --------------------------------------------
    menuLayer = lv_obj_create(scr);
    lv_obj_remove_style_all(menuLayer);
    lv_obj_set_size(menuLayer, 480, 480);
    lv_obj_center(menuLayer);
    lv_obj_set_style_radius(menuLayer, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(menuLayer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(menuLayer, LV_OPA_COVER, 0);
    lv_obj_clear_flag(menuLayer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(menuLayer, LV_OBJ_FLAG_SCROLLABLE);

    // Einträge auf einem Kreis mit Radius 150 px: oben, rechts, unten, links
    const int16_t offsets[app::ModeController::kMenuItemCount][2] = {{0, -150}, {150, 0}, {0, 150}, {-150, 0}};
    for (int i = 0; i < app::ModeController::kMenuItemCount; ++i) {
        lv_obj_t* b = lv_obj_create(menuLayer);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 96, 96);
        lv_obj_align(b, LV_ALIGN_CENTER, offsets[i][0], offsets[i][1]);
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t* sym = lv_label_create(b);
        lv_obj_set_style_text_font(sym, &font_inter_28, 0);
        lv_label_set_text(sym, kMenu[i].symbol);
        lv_obj_center(sym);
        menuBubbles[i] = b;
    }
    menuName = makeLabel(menuLayer, &font_inter_28, kText, 200, -10, false);
    menuHint = makeLabel(menuLayer, &font_inter_14, kTextDim, 200, 25, false);
    lv_obj_add_flag(menuLayer, LV_OBJ_FLAG_HIDDEN);

    // --- Raumwahl (deckend, über allem) ---------------------------------------------
    pickerLayer = lv_obj_create(scr);
    lv_obj_remove_style_all(pickerLayer);
    lv_obj_set_size(pickerLayer, 480, 480);
    lv_obj_center(pickerLayer);
    lv_obj_set_style_radius(pickerLayer, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pickerLayer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(pickerLayer, LV_OPA_COVER, 0);
    lv_obj_clear_flag(pickerLayer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(pickerLayer, LV_OBJ_FLAG_SCROLLABLE);
    // Markierung hinter der mittleren Zeile
    lv_obj_t* band = lv_obj_create(pickerLayer);
    lv_obj_remove_style_all(band);
    lv_obj_set_size(band, 400, 64);
    lv_obj_center(band);
    lv_obj_set_style_radius(band, 32, 0);
    lv_obj_set_style_bg_color(band, lv_color_hex(kTrackBg), 0);
    lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);
    // Zeilen: außen klein und blass, innen groß und hell
    const lv_font_t* fonts[kPickerRows] = {&font_inter_14, &font_inter_20, &font_inter_28, &font_inter_20, &font_inter_14};
    const uint32_t colors[kPickerRows] = {kTextFaint, kTextDim, kText, kTextDim, kTextFaint};
    const int ys[kPickerRows] = {-130, -72, 0, 72, 130};
    const int widths[kPickerRows] = {260, 330, 370, 330, 260};
    for (int i = 0; i < kPickerRows; ++i) {
        pickerRows[i] = makeLabel(pickerLayer, fonts[i], colors[i], widths[i], ys[i], false);
    }
    lv_obj_t* pickerTitle = makeLabel(pickerLayer, &font_inter_14, kAccent, 200, -185, false);
    lv_label_set_text(pickerTitle, LV_SYMBOL_HOME "  Raum wählen");
    pickerCounter = makeLabel(pickerLayer, &font_inter_14, kTextDim, 200, 185, false);
    lv_obj_add_flag(pickerLayer, LV_OBJ_FLAG_HIDDEN);

    setTrack("", "", "");
    setProgress(false, 0, -1, -1);
    setVolume(0, false);
    setPlayState(app::PlayState::Unknown);
}

void NowPlayingScreen::setTrack(const char* title, const char* subtitle, const char* album) {
    setTextIfChanged(titleLabel, title);
    setTextIfChanged(subtitleLabel, subtitle);
    setTextIfChanged(albumLabel, album);
}

void NowPlayingScreen::setProgress(bool showProgress, int permille, int positionSec, int durationSec) {
    if (scrubbing) return;  // beim Spulen zeigt der Ring die Zielposition
    if (!showProgress) {
        lv_arc_set_value(progressArc, 0);
        setTextIfChanged(timeLabel, "");
        return;
    }
    lv_arc_set_value(progressArc, static_cast<int16_t>(permille));
    const std::string text = sonos::time::format(positionSec) + " / " + sonos::time::format(durationSec);
    setTextIfChanged(timeLabel, text.c_str());
}

void NowPlayingScreen::setPlayState(app::PlayState state) {
    playState = state;
    switch (state) {
        case app::PlayState::Playing: setTextIfChanged(stateLabel, LV_SYMBOL_PLAY); break;
        case app::PlayState::Paused: setTextIfChanged(stateLabel, LV_SYMBOL_PAUSE); break;
        case app::PlayState::Stopped: setTextIfChanged(stateLabel, LV_SYMBOL_STOP); break;
        case app::PlayState::Transitioning: setTextIfChanged(stateLabel, "…"); break;
        case app::PlayState::Unknown: setTextIfChanged(stateLabel, ""); break;
    }
    applyPlayStateColors();
}

void NowPlayingScreen::setVolume(int volume, bool known) {
    volumeKnown = known;
    if (known) {
        lv_arc_set_value(volumeArc, static_cast<int16_t>(volume));
        lv_label_set_text_fmt(volumeLabel, "%d", volume);
    } else {
        lv_arc_set_value(volumeArc, 0);
        lv_label_set_text(volumeLabel, "–");
    }
    applyPlayStateColors();
}

void NowPlayingScreen::showVolumeOverlay(uint32_t nowMs) {
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    overlayUntil = nowMs + kOverlayMs;
    if (overlayUntil == 0) overlayUntil = 1;
}

void NowPlayingScreen::setStatus(const char* text, Status kind) {
    setTextIfChanged(statusLabel, text);
    const uint32_t color = kind == Status::Error ? kError : (kind == Status::Ok ? kAccent : kTextDim);
    lv_obj_set_style_text_color(statusLabel, lv_color_hex(color), 0);
}

void NowPlayingScreen::showMenu(int selection) {
    for (int i = 0; i < app::ModeController::kMenuItemCount; ++i) {
        const bool enabled = app::ModeController::isEnabled(static_cast<app::ModeController::MenuItem>(i));
        const bool selected = i == selection;
        lv_obj_set_style_bg_color(menuBubbles[i], lv_color_hex(selected ? kAccent : kTrackBg), 0);
        lv_obj_t* sym = lv_obj_get_child(menuBubbles[i], 0);
        const uint32_t symColor = selected ? 0x000000 : (enabled ? kText : kInactive);
        lv_obj_set_style_text_color(sym, lv_color_hex(symColor), 0);
    }
    setTextIfChanged(menuName, kMenu[selection].name);
    setTextIfChanged(menuHint, "Drücken: öffnen");
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);  // Lautstärke-Einblendung weg
    overlayUntil = 0;
    lv_obj_clear_flag(menuLayer, LV_OBJ_FLAG_HIDDEN);
}

void NowPlayingScreen::hideMenu() { lv_obj_add_flag(menuLayer, LV_OBJ_FLAG_HIDDEN); }

void NowPlayingScreen::showRoomPicker(const char* const* names, int count, int index, int active) {
    for (int row = 0; row < kPickerRows; ++row) {
        const int i = index + row - kPickerRows / 2;
        if (i < 0 || i >= count) {
            setTextIfChanged(pickerRows[row], "");
            continue;
        }
        char text[80];
        if (i == active) snprintf(text, sizeof(text), LV_SYMBOL_OK " %s", names[i]);
        else snprintf(text, sizeof(text), "%s", names[i]);
        setTextIfChanged(pickerRows[row], text);
    }
    char counter[16];
    snprintf(counter, sizeof(counter), "%d / %d", index + 1, count);
    setTextIfChanged(pickerCounter, counter);
    lv_obj_add_flag(menuLayer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(pickerLayer, LV_OBJ_FLAG_HIDDEN);
}

void NowPlayingScreen::hideRoomPicker() { lv_obj_add_flag(pickerLayer, LV_OBJ_FLAG_HIDDEN); }

void NowPlayingScreen::showScrub(int targetSec, int durationSec) {
    if (!scrubbing) {
        scrubbing = true;
        // Ring dicker, mit Punkt an der Zielposition
        lv_obj_set_style_arc_width(progressArc, 14, LV_PART_MAIN);
        lv_obj_set_style_arc_width(progressArc, 14, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(progressArc, lv_color_hex(kAccent), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(progressArc, lv_color_white(), LV_PART_KNOB);
        lv_obj_set_style_bg_opa(progressArc, LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_radius(progressArc, LV_RADIUS_CIRCLE, LV_PART_KNOB);
        lv_obj_set_style_pad_all(progressArc, 5, LV_PART_KNOB);
        lv_obj_add_flag(timeLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(scrubTimeLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(scrubHintLabel, LV_OBJ_FLAG_HIDDEN);
    }
    const int permille = durationSec > 0 ? static_cast<int>(static_cast<int64_t>(targetSec) * 1000 / durationSec) : 0;
    lv_arc_set_value(progressArc, static_cast<int16_t>(permille));
    setTextIfChanged(scrubTimeLabel, sonos::time::format(targetSec).c_str());
}

void NowPlayingScreen::hideScrub() {
    if (!scrubbing) return;
    scrubbing = false;
    lv_obj_set_style_arc_width(progressArc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(progressArc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(progressArc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_add_flag(scrubTimeLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(scrubHintLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(timeLabel, LV_OBJ_FLAG_HIDDEN);
    applyPlayStateColors();
}

void NowPlayingScreen::tick(uint32_t nowMs) {
    if (overlayUntil != 0 && static_cast<int32_t>(nowMs - overlayUntil) >= 0) {
        overlayUntil = 0;
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    }
}
