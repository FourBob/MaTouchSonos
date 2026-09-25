#include "Input.h"

#include <Arduino.h>

#include <atomic>

#include "RotaryDetentDecoder.h"
#include "board_config.h"

namespace hal {
namespace {

app::RotaryDetentDecoder decoder(ENCODER_HALF_STEP != 0);  // nur in der ISR verwendet
std::atomic<int32_t> pendingDetents{0};                     // ISR -> Hauptschleife
std::atomic<int32_t> pendingRawSteps{0};

void ARDUINO_ISR_ATTR onEncoderEdge() {
    const int8_t detent = decoder.update(digitalRead(ENCODER_PIN_A), digitalRead(ENCODER_PIN_B));
    if (decoder.lastStep() != 0) pendingRawSteps.fetch_add(decoder.lastStep(), std::memory_order_relaxed);
    if (detent != 0) pendingDetents.fetch_add(detent, std::memory_order_relaxed);
}

}  // namespace

void Input::begin() {
    pinMode(ENCODER_PIN_A, INPUT_PULLUP);
    pinMode(ENCODER_PIN_B, INPUT_PULLUP);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    decoder.reset(digitalRead(ENCODER_PIN_A), digitalRead(ENCODER_PIN_B));
    attachInterrupt(ENCODER_PIN_A, onEncoderEdge, CHANGE);
    attachInterrupt(ENCODER_PIN_B, onEncoderEdge, CHANGE);
}

int32_t Input::takeDetents() {
    const int32_t d = pendingDetents.exchange(0, std::memory_order_relaxed);
    return ENCODER_INVERT ? -d : d;
}

int32_t Input::takeRawSteps() {
    const int32_t s = pendingRawSteps.exchange(0, std::memory_order_relaxed);
    return ENCODER_INVERT ? -s : s;
}

bool Input::buttonRaw() {
    return digitalRead(BUTTON_PIN) == LOW;
}

}  // namespace hal
