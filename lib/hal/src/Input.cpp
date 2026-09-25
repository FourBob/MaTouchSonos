#include "Input.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/pcnt.h>

#include <atomic>

#include "DetentTracker.h"
#include "board_config.h"

namespace hal {
namespace {

// Der 16-Bit-Hardwarezähler läuft bei ±kLimit über und springt auf 0; der Überlauf
// wird im Interrupt aufsummiert. So entsteht eine fortlaufende 32-Bit-Position.
constexpr pcnt_unit_t kUnit = PCNT_UNIT_0;
constexpr int16_t kLimit = 10000;
// Glitch-Filter: Pulse kürzer als 1023 APB-Takte (≈ 12,8 µs bei 80 MHz) werden ignoriert.
constexpr uint16_t kFilterTicks = 1023;

std::atomic<int32_t> overflow{0};

app::DetentTracker tracker(ENCODER_HALF_STEP != 0);
int32_t lastRawPosition = 0;

void onPcntLimit(void*) {
    uint32_t status = 0;
    pcnt_get_event_status(kUnit, &status);
    if (status & PCNT_EVT_H_LIM) overflow.fetch_add(kLimit, std::memory_order_relaxed);
    else if (status & PCNT_EVT_L_LIM) overflow.fetch_add(-kLimit, std::memory_order_relaxed);
}

/** Fortlaufende Schrittposition (konsistent auch bei gleichzeitigem Überlauf). */
int32_t readPosition() {
    int32_t before, after;
    int16_t count;
    do {
        before = overflow.load(std::memory_order_relaxed);
        pcnt_get_counter_value(kUnit, &count);
        after = overflow.load(std::memory_order_relaxed);
    } while (before != after);
    return before + count;
}

bool encoderAtRest() {
    const bool a = gpio_get_level(static_cast<gpio_num_t>(ENCODER_PIN_A));
    const bool b = gpio_get_level(static_cast<gpio_num_t>(ENCODER_PIN_B));
    // Vollschritt: Ruhelage A=B=1 (Kontakte offen, Pull-ups). Halbschritt: auch A=B=0.
    return ENCODER_HALF_STEP ? (a == b) : (a && b);
}

void setupPcnt() {
    // Standard-Quadraturbeschaltung (x4): Jeder Kanal zählt die Flanken eines Signals,
    // das jeweils andere Signal bestimmt die Richtung.
    pcnt_config_t ch0 = {};
    ch0.pulse_gpio_num = ENCODER_PIN_A;
    ch0.ctrl_gpio_num = ENCODER_PIN_B;
    ch0.channel = PCNT_CHANNEL_0;
    ch0.unit = kUnit;
    ch0.pos_mode = PCNT_COUNT_DEC;
    ch0.neg_mode = PCNT_COUNT_INC;
    ch0.lctrl_mode = PCNT_MODE_REVERSE;
    ch0.hctrl_mode = PCNT_MODE_KEEP;
    ch0.counter_h_lim = kLimit;
    ch0.counter_l_lim = -kLimit;
    pcnt_unit_config(&ch0);

    pcnt_config_t ch1 = ch0;
    ch1.pulse_gpio_num = ENCODER_PIN_B;
    ch1.ctrl_gpio_num = ENCODER_PIN_A;
    ch1.channel = PCNT_CHANNEL_1;
    ch1.pos_mode = PCNT_COUNT_INC;
    ch1.neg_mode = PCNT_COUNT_DEC;
    pcnt_unit_config(&ch1);

    // Pull-ups sicherstellen (die Encoder-Kontakte schalten nach Masse).
    gpio_pullup_en(static_cast<gpio_num_t>(ENCODER_PIN_A));
    gpio_pullup_en(static_cast<gpio_num_t>(ENCODER_PIN_B));

    pcnt_set_filter_value(kUnit, kFilterTicks);
    pcnt_filter_enable(kUnit);

    pcnt_event_enable(kUnit, PCNT_EVT_H_LIM);
    pcnt_event_enable(kUnit, PCNT_EVT_L_LIM);
    pcnt_isr_service_install(0);
    pcnt_isr_handler_add(kUnit, onPcntLimit, nullptr);

    pcnt_counter_pause(kUnit);
    pcnt_counter_clear(kUnit);
    pcnt_counter_resume(kUnit);
}

}  // namespace

void Input::begin() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    setupPcnt();
    lastRawPosition = readPosition();
    tracker.reset(lastRawPosition);
}

int32_t Input::takeDetents() {
    const int32_t d = tracker.update(readPosition(), encoderAtRest());
    return ENCODER_INVERT ? -d : d;
}

int32_t Input::takeRawSteps() {
    const int32_t pos = readPosition();
    const int32_t s = pos - lastRawPosition;
    lastRawPosition = pos;
    return ENCODER_INVERT ? -s : s;
}

bool Input::buttonRaw() {
    return digitalRead(BUTTON_PIN) == LOW;
}

}  // namespace hal
