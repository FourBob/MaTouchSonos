#include "MainScreen.h"

#include <lvgl.h>

namespace {

constexpr uint32_t kAccent = 0x1DB954;
constexpr uint32_t kInactive = 0x3A3A3A;
constexpr uint32_t kTextDim = 0x9A9A9A;
constexpr uint32_t kPausedArc = 0x6E6E6E;
constexpr uint32_t kError = 0xEB5757;

lv_obj_t* arc;
lv_obj_t* valueLabel;
lv_obj_t* stateLabel;
lv_obj_t* statusLabel;

bool volumeKnown = false;
app::PlayState playState = app::PlayState::Unknown;

void applyColors() {
    const bool paused = playState == app::PlayState::Paused || playState == app::PlayState::Stopped;
    const uint32_t indicator = !volumeKnown ? kInactive : (paused ? kPausedArc : kAccent);
    lv_obj_set_style_arc_color(arc, lv_color_hex(indicator), LV_PART_INDICATOR);
    lv_obj_set_style_text_color(valueLabel, paused ? lv_color_hex(kTextDim) : lv_color_white(), 0);
}

}  // namespace

void MainScreen::create(const char* speakerLabel) {
    lv_obj_t* scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // 270°-Bogen: beginnt links unten (135°), endet rechts unten (45°).
    arc = lv_arc_create(scr);
    lv_obj_set_size(arc, 450, 450);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, 22, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 22, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x1E1E1E), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

    lv_obj_t* caption = lv_label_create(scr);
    lv_label_set_text(caption, "Lautstärke");
    lv_obj_set_style_text_color(caption, lv_color_hex(kTextDim), 0);
    lv_obj_align(caption, LV_ALIGN_CENTER, 0, -90);

    valueLabel = lv_label_create(scr);
    lv_obj_set_style_text_font(valueLabel, &font_num_96, 0);
    lv_obj_align(valueLabel, LV_ALIGN_CENTER, 0, 0);

    stateLabel = lv_label_create(scr);
    lv_obj_set_style_text_color(stateLabel, lv_color_hex(kTextDim), 0);
    lv_obj_align(stateLabel, LV_ALIGN_CENTER, 0, 72);

    lv_obj_t* speaker = lv_label_create(scr);
    lv_label_set_text(speaker, speakerLabel);
    lv_obj_set_style_text_font(speaker, &font_inter_14, 0);
    lv_obj_set_style_text_color(speaker, lv_color_hex(kTextDim), 0);
    lv_obj_align(speaker, LV_ALIGN_CENTER, 0, 105);

    // Statuszeile in der Lücke des Bogens unten.
    statusLabel = lv_label_create(scr);
    lv_obj_set_width(statusLabel, 260);
    lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(statusLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(statusLabel, &font_inter_14, 0);
    lv_obj_align(statusLabel, LV_ALIGN_BOTTOM_MID, 0, -40);

    setVolume(0, false);
    setPlayState(app::PlayState::Unknown);
    setStatus("", Status::Info);
}

void MainScreen::setVolume(int volume, bool known) {
    volumeKnown = known;
    if (known) {
        lv_arc_set_value(arc, static_cast<int16_t>(volume));
        lv_label_set_text_fmt(valueLabel, "%d", volume);
    } else {
        lv_arc_set_value(arc, 0);
        lv_label_set_text(valueLabel, "–");
    }
    applyColors();
}

void MainScreen::setPlayState(app::PlayState state) {
    playState = state;
    switch (state) {
        case app::PlayState::Playing: lv_label_set_text(stateLabel, LV_SYMBOL_PLAY "  Wiedergabe"); break;
        case app::PlayState::Paused: lv_label_set_text(stateLabel, LV_SYMBOL_PAUSE "  Pausiert"); break;
        case app::PlayState::Stopped: lv_label_set_text(stateLabel, LV_SYMBOL_STOP "  Gestoppt"); break;
        case app::PlayState::Transitioning: lv_label_set_text(stateLabel, "Lädt …"); break;
        case app::PlayState::Unknown: lv_label_set_text(stateLabel, ""); break;
    }
    applyColors();
}

void MainScreen::setStatus(const char* text, Status kind) {
    lv_label_set_text(statusLabel, text);
    const uint32_t color = kind == Status::Error ? kError : (kind == Status::Ok ? kAccent : kTextDim);
    lv_obj_set_style_text_color(statusLabel, lv_color_hex(color), 0);
}
