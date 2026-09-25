// Unit-Tests für die hardwareunabhängige Bedienlogik.
// Ausführen mit:  pio test -e native

#include <unity.h>

#include <vector>

#include "ButtonDetector.h"
#include "QuadratureDecoder.h"
#include "RotaryDetentDecoder.h"

using app::ButtonDetector;
using app::ButtonEvent;
using app::QuadratureDecoder;
using app::RotaryDetentDecoder;

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

// --- RotaryDetentDecoder ----------------------------------------------------

// Vollschritt-Encoder, Ruhelage 11. Eine Rastung:
//   im Uhrzeigersinn:        11 -> 01 -> 00 -> 10 -> 11
//   gegen den Uhrzeigersinn: 11 -> 10 -> 00 -> 01 -> 11
struct Level { bool a, b; };
static const Level kClickCw[]  = {{false, true}, {false, false}, {true, false}, {true, true}};
static const Level kClickCcw[] = {{true, false}, {false, false}, {false, true}, {true, true}};

static int feedLevels(RotaryDetentDecoder& dec, const Level* seq, int n) {
    int sum = 0;
    for (int i = 0; i < n; ++i) sum += dec.update(seq[i].a, seq[i].b);
    return sum;
}

void test_detent_one_click_each_direction() {
    RotaryDetentDecoder dec;
    dec.reset(true, true);
    TEST_ASSERT_EQUAL_INT(+1, feedLevels(dec, kClickCw, 4));
    TEST_ASSERT_EQUAL_INT(-1, feedLevels(dec, kClickCcw, 4));
}

void test_detent_reported_only_at_rest_position() {
    RotaryDetentDecoder dec;
    dec.reset(true, true);
    // Die ersten drei Zwischenschritte melden noch nichts, erst das Einrasten.
    for (int i = 0; i < 3; ++i) TEST_ASSERT_EQUAL_INT(0, dec.update(kClickCw[i].a, kClickCw[i].b));
    TEST_ASSERT_EQUAL_INT(+1, dec.update(kClickCw[3].a, kClickCw[3].b));
}

void test_detent_back_and_forth_single_clicks() {
    // Fehlerbild aus dem Geräte-Test von Schritt 0: ein Klick links, ein Klick rechts.
    RotaryDetentDecoder dec;
    dec.reset(true, true);
    for (int i = 0; i < 10; ++i) {
        TEST_ASSERT_EQUAL_INT(+1, feedLevels(dec, kClickCw, 4));
        TEST_ASSERT_EQUAL_INT(-1, feedLevels(dec, kClickCcw, 4));
    }
}

void test_detent_back_and_forth_after_lost_step() {
    // Regression: Ein durch Prellen verlorener Zwischenschritt darf keinen dauerhaften
    // Versatz erzeugen (der alte Schrittzähler meldete danach beim Hin-und-her nichts mehr).
    RotaryDetentDecoder dec;
    dec.reset(true, true);
    // Klick im Uhrzeigersinn, bei dem der Zustand 00 verpasst wird: 11 -> 01 -> 10 -> 11
    const Level lossy[] = {{false, true}, {true, false}, {true, true}};
    TEST_ASSERT_EQUAL_INT(+1, feedLevels(dec, lossy, 3));
    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL_INT(-1, feedLevels(dec, kClickCcw, 4));
        TEST_ASSERT_EQUAL_INT(+1, feedLevels(dec, kClickCw, 4));
    }
}

void test_detent_half_turn_and_back_is_nothing() {
    RotaryDetentDecoder dec;
    dec.reset(true, true);
    // Halb hin (11 -> 01 -> 00) und wieder zurück (00 -> 01 -> 11)
    const Level seq[] = {{false, true}, {false, false}, {false, true}, {true, true}};
    TEST_ASSERT_EQUAL_INT(0, feedLevels(dec, seq, 4));
}

void test_detent_bounce_at_rest_is_nothing() {
    RotaryDetentDecoder dec;
    dec.reset(true, true);
    const Level seq[] = {{false, true}, {true, true}, {false, true}, {true, true}};
    TEST_ASSERT_EQUAL_INT(0, feedLevels(dec, seq, 4));
}

void test_detent_many_clicks_fast() {
    RotaryDetentDecoder dec;
    dec.reset(true, true);
    int sum = 0;
    for (int i = 0; i < 25; ++i) sum += feedLevels(dec, kClickCw, 4);
    TEST_ASSERT_EQUAL_INT(25, sum);
}

void test_detent_half_step_encoder() {
    // Halbschritt-Encoder: Ruhelagen 11 und 00, je 2 Zustandswechsel pro Rastung.
    RotaryDetentDecoder dec(/*halfStep=*/true);
    dec.reset(true, true);
    TEST_ASSERT_EQUAL_INT(+1, feedLevels(dec, kClickCw, 2));      // 11 -> 01 -> 00
    TEST_ASSERT_EQUAL_INT(+1, feedLevels(dec, kClickCw + 2, 2));  // 00 -> 10 -> 11
    TEST_ASSERT_EQUAL_INT(-1, feedLevels(dec, kClickCcw, 2));     // 11 -> 10 -> 00
}

void test_detent_last_step_for_diagnostics() {
    RotaryDetentDecoder dec;
    dec.reset(true, true);
    dec.update(false, true);
    TEST_ASSERT_EQUAL_INT(+1, dec.lastStep());
    dec.update(false, true);
    TEST_ASSERT_EQUAL_INT(0, dec.lastStep());
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
    RUN_TEST(test_detent_one_click_each_direction);
    RUN_TEST(test_detent_reported_only_at_rest_position);
    RUN_TEST(test_detent_back_and_forth_single_clicks);
    RUN_TEST(test_detent_back_and_forth_after_lost_step);
    RUN_TEST(test_detent_half_turn_and_back_is_nothing);
    RUN_TEST(test_detent_bounce_at_rest_is_nothing);
    RUN_TEST(test_detent_many_clicks_fast);
    RUN_TEST(test_detent_half_step_encoder);
    RUN_TEST(test_detent_last_step_for_diagnostics);
    RUN_TEST(test_button_short_press);
    RUN_TEST(test_button_long_press_fires_while_held_and_no_short_after);
    RUN_TEST(test_button_bounce_is_ignored);
    RUN_TEST(test_button_two_short_presses);
    RUN_TEST(test_button_millis_wraparound);
    return UNITY_END();
}
