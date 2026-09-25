#include "TestScreen.h"

#include <Arduino.h>
#include <lvgl.h>

namespace {

constexpr int kArcSegments = 24;  // eine Rastung = 1/24 Umdrehung am Bildschirm
constexpr uint32_t kFlashMs = 300;

lv_obj_t* arc;
lv_obj_t* flashRing;
lv_obj_t* counterLabel;
lv_obj_t* buttonLabel;
lv_obj_t* touchDot;
lv_obj_t* touchLabel;

void onScreenTouch(lv_event_t* e) {
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        lv_obj_clear_flag(touchDot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(touchDot, p.x - 15, p.y - 15);
        lv_label_set_text_fmt(touchLabel, "Touch %d / %d", p.x, p.y);
    } else if (code == LV_EVENT_RELEASED) {
        lv_obj_add_flag(touchDot, LV_OBJ_FLAG_HIDDEN);
    }
    // Nur Aufsetzen und Loslassen loggen – PRESSING käme alle 20 ms.
    if (code == LV_EVENT_PRESSED) Serial.printf("TOUCH down %d/%d\n", p.x, p.y);
    if (code == LV_EVENT_RELEASED) Serial.printf("TOUCH up   %d/%d\n", p.x, p.y);
}

lv_obj_t* colorBar(lv_obj_t* parent, uint32_t color, int x) {
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 50, 24);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(color), 0);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, x, 90);
    return bar;
}

}  // namespace

void TestScreen::create() {
    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Ring, der beim Tastendruck aufblitzt.
    flashRing = lv_obj_create(scr);
    lv_obj_remove_style_all(flashRing);
    lv_obj_set_size(flashRing, 480, 480);
    lv_obj_center(flashRing);
    lv_obj_set_style_radius(flashRing, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(flashRing, 12, 0);
    lv_obj_set_style_border_opa(flashRing, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(flashRing, LV_OBJ_FLAG_CLICKABLE);

    // Bogen für den Drehring.
    arc = lv_arc_create(scr);
    lv_obj_set_size(arc, 440, 440);
    lv_obj_center(arc);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_rotation(arc, 270);  // 0 = oben
    lv_arc_set_range(arc, 0, kArcSegments);
    lv_arc_set_value(arc, 0);
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, 18, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 18, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x1DB954), LV_PART_INDICATOR);

    // Farbbalken: müssen links Rot, Mitte Grün, rechts Blau sein.
    colorBar(scr, 0xFF0000, -60);
    colorBar(scr, 0x00FF00, 0);
    colorBar(scr, 0x0000FF, 60);

    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "MaTouchSonos\nHardware-Test");
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 130);

    counterLabel = lv_label_create(scr);
    lv_obj_set_style_text_font(counterLabel, &lv_font_montserrat_48, 0);
    lv_obj_align(counterLabel, LV_ALIGN_CENTER, 0, 0);

    buttonLabel = lv_label_create(scr);
    lv_obj_align(buttonLabel, LV_ALIGN_CENTER, 0, 50);

    touchLabel = lv_label_create(scr);
    lv_label_set_text(touchLabel, "Touch -");
    lv_obj_set_style_text_font(touchLabel, &lv_font_montserrat_14, 0);
    lv_obj_align(touchLabel, LV_ALIGN_CENTER, 0, 85);

    touchDot = lv_obj_create(scr);
    lv_obj_remove_style_all(touchDot);
    lv_obj_set_size(touchDot, 30, 30);
    lv_obj_set_style_radius(touchDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(touchDot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(touchDot, lv_color_white(), 0);
    lv_obj_add_flag(touchDot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(touchDot, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(scr, onScreenTouch, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(scr, onScreenTouch, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(scr, onScreenTouch, LV_EVENT_RELEASED, nullptr);

    updateLabels();
}

void TestScreen::onDetents(int32_t detents) {
    position_ += detents;
    // Bogen läuft im Kreis: 0..24, danach wieder von vorn.
    int32_t v = position_ % kArcSegments;
    if (v < 0) v += kArcSegments;
    lv_arc_set_value(arc, static_cast<int16_t>(v == 0 && position_ != 0 ? kArcSegments : v));
    updateLabels();
}

void TestScreen::onShortPress() {
    lastButton_ = "Taste: kurz";
    flash(0x2F80ED, millis());
    updateLabels();
}

void TestScreen::onLongPress() {
    lastButton_ = "Taste: lang";
    flash(0xF2994A, millis());
    updateLabels();
}

void TestScreen::tick(uint32_t nowMs) {
    if (flashUntil_ != 0 && static_cast<int32_t>(nowMs - flashUntil_) >= 0) {
        lv_obj_set_style_border_opa(flashRing, LV_OPA_TRANSP, 0);
        flashUntil_ = 0;
    }
}

void TestScreen::flash(uint32_t color, uint32_t nowMs) {
    lv_obj_set_style_border_color(flashRing, lv_color_hex(color), 0);
    lv_obj_set_style_border_opa(flashRing, LV_OPA_COVER, 0);
    flashUntil_ = nowMs + kFlashMs;
    if (flashUntil_ == 0) flashUntil_ = 1;
}

void TestScreen::updateLabels() {
    lv_label_set_text_fmt(counterLabel, "%ld", static_cast<long>(position_));
    lv_label_set_text(buttonLabel, lastButton_);
}
