#include "VolumeScreen.h"

#include <lvgl.h>

namespace {

constexpr uint32_t kAccent = 0x1DB954;
constexpr uint32_t kInactive = 0x3A3A3A;
constexpr uint32_t kTextDim = 0x9A9A9A;
constexpr uint32_t kError = 0xEB5757;

lv_obj_t* arc;
lv_obj_t* valueLabel;
lv_obj_t* statusLabel;

}  // namespace

void VolumeScreen::create(const char* speakerLabel) {
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
    lv_obj_align(caption, LV_ALIGN_CENTER, 0, -70);

    valueLabel = lv_label_create(scr);
    lv_obj_set_style_text_font(valueLabel, &font_de_48, 0);
    lv_obj_set_style_text_color(valueLabel, lv_color_white(), 0);
    lv_obj_align(valueLabel, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t* speaker = lv_label_create(scr);
    lv_label_set_text(speaker, speakerLabel);
    lv_obj_set_style_text_font(speaker, &font_de_14, 0);
    lv_obj_set_style_text_color(speaker, lv_color_hex(kTextDim), 0);
    lv_obj_align(speaker, LV_ALIGN_CENTER, 0, 45);

    // Statuszeile in der Lücke des Bogens unten.
    statusLabel = lv_label_create(scr);
    lv_obj_set_width(statusLabel, 260);
    lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(statusLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(statusLabel, &font_de_14, 0);
    lv_obj_align(statusLabel, LV_ALIGN_BOTTOM_MID, 0, -40);

    setVolume(0, false);
    setStatus("", Status::Info);
}

void VolumeScreen::setVolume(int volume, bool known) {
    if (known) {
        lv_arc_set_value(arc, static_cast<int16_t>(volume));
        lv_label_set_text_fmt(valueLabel, "%d", volume);
        lv_obj_set_style_arc_color(arc, lv_color_hex(kAccent), LV_PART_INDICATOR);
    } else {
        lv_arc_set_value(arc, 0);
        lv_label_set_text(valueLabel, "–");
        lv_obj_set_style_arc_color(arc, lv_color_hex(kInactive), LV_PART_INDICATOR);
    }
}

void VolumeScreen::setStatus(const char* text, Status kind) {
    lv_label_set_text(statusLabel, text);
    const uint32_t color = kind == Status::Error ? kError : (kind == Status::Ok ? kAccent : kTextDim);
    lv_obj_set_style_text_color(statusLabel, lv_color_hex(color), 0);
}
