// Unit-Tests für die hardwareunabhängige Bedienlogik.
// Ausführen mit:  pio test -e native

#include <unity.h>

#include <vector>

#include "ButtonDetector.h"
#include "DetentAccumulator.h"
#include "QuadratureDecoder.h"

using app::ButtonDetector;
using app::ButtonEvent;
using app::DetentAccumulator;
using app::QuadratureDecoder;

void setUp() {}
void tearDown() {}

// --- QuadratureDecoder ------------------------------------------------------

// Eine volle Rastung im Uhrzeigersinn: 11 -> 01 -> 00 -> 10 -> 11 (A eilt B voraus,
// Ruhelage 11 wegen Pull-ups).
static const bool kCwA[] = {false, false, true, true};
static const bool kCwB[] = {true, false, false, true};

static int feed(QuadratureDecoder& dec, const bool* a, const bool* b, int n) {
    int sum = 0;
    for (int i = 0; i < n; ++i) sum += dec.update(a[i], b[i]);
    return sum;
}

void test_quadrature_clockwise_detent_gives_plus_four() {
    QuadratureDecoder dec(true, true);
    TEST_ASSERT_EQUAL_INT(4, feed(dec, kCwA, kCwB, 4));
}

void test_quadrature_counter_clockwise_detent_gives_minus_four() {
    QuadratureDecoder dec(true, true);
    // Rückwärts durch dieselbe Folge: 11 -> 10 -> 00 -> 01 -> 11
    const bool a[] = {true, false, false, true};
    const bool b[] = {false, false, true, true};
    TEST_ASSERT_EQUAL_INT(-4, feed(dec, a, b, 4));
}

void test_quadrature_same_state_gives_zero() {
    QuadratureDecoder dec(true, true);
    TEST_ASSERT_EQUAL_INT(0, dec.update(true, true));
    TEST_ASSERT_EQUAL_INT(0, dec.update(true, true));
}

void test_quadrature_invalid_jump_is_ignored() {
    QuadratureDecoder dec(true, true);
    TEST_ASSERT_EQUAL_INT(0, dec.update(false, false));  // beide Bits gleichzeitig
    TEST_ASSERT_EQUAL_INT(0, dec.update(true, true));
}

void test_quadrature_contact_bounce_cancels_out() {
    QuadratureDecoder dec(true, true);
    // Prellen an einer Flanke: 11 -> 01 -> 11 -> 01 hin und her, dann weiter.
    int sum = 0;
    sum += dec.update(false, true);
    sum += dec.update(true, true);
    sum += dec.update(false, true);
    sum += dec.update(false, false);
    sum += dec.update(true, false);
    sum += dec.update(true, true);
    TEST_ASSERT_EQUAL_INT(4, sum);
}

// --- DetentAccumulator ------------------------------------------------------

void test_detent_needs_full_steps() {
    DetentAccumulator acc(4);
    TEST_ASSERT_EQUAL_INT(0, acc.add(3));
    TEST_ASSERT_EQUAL_INT(1, acc.add(1));
    TEST_ASSERT_EQUAL_INT(0, acc.pending());
}

void test_detent_half_turn_back_is_nothing() {
    DetentAccumulator acc(4);
    TEST_ASSERT_EQUAL_INT(0, acc.add(2));
    TEST_ASSERT_EQUAL_INT(0, acc.add(-2));
    TEST_ASSERT_EQUAL_INT(0, acc.pending());
}

void test_detent_many_steps_at_once() {
    DetentAccumulator acc(4);
    TEST_ASSERT_EQUAL_INT(3, acc.add(13));
    TEST_ASSERT_EQUAL_INT(1, acc.pending());
    TEST_ASSERT_EQUAL_INT(-2, acc.add(-9));  // 1 - 9 = -8 -> -2 Rastungen
}

void test_detent_invert() {
    DetentAccumulator acc(4, /*invert=*/true);
    TEST_ASSERT_EQUAL_INT(-1, acc.add(4));
}

void test_detent_two_steps_per_detent() {
    DetentAccumulator acc(2);
    TEST_ASSERT_EQUAL_INT(2, acc.add(4));
}

void test_detent_clear() {
    DetentAccumulator acc(4);
    acc.add(3);
    acc.clear();
    TEST_ASSERT_EQUAL_INT(0, acc.add(1));
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
    RUN_TEST(test_quadrature_clockwise_detent_gives_plus_four);
    RUN_TEST(test_quadrature_counter_clockwise_detent_gives_minus_four);
    RUN_TEST(test_quadrature_same_state_gives_zero);
    RUN_TEST(test_quadrature_invalid_jump_is_ignored);
    RUN_TEST(test_quadrature_contact_bounce_cancels_out);
    RUN_TEST(test_detent_needs_full_steps);
    RUN_TEST(test_detent_half_turn_back_is_nothing);
    RUN_TEST(test_detent_many_steps_at_once);
    RUN_TEST(test_detent_invert);
    RUN_TEST(test_detent_two_steps_per_detent);
    RUN_TEST(test_detent_clear);
    RUN_TEST(test_button_short_press);
    RUN_TEST(test_button_long_press_fires_while_held_and_no_short_after);
    RUN_TEST(test_button_bounce_is_ignored);
    RUN_TEST(test_button_two_short_presses);
    RUN_TEST(test_button_millis_wraparound);
    return UNITY_END();
}
