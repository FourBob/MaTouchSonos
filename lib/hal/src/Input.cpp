#include "Input.h"

#include <Arduino.h>

#include <atomic>

#include "QuadratureDecoder.h"
#include "board_config.h"

namespace hal {
namespace {

app::QuadratureDecoder decoder;       // nur in der ISR verwendet
std::atomic<int32_t> pendingSteps{0};  // ISR -> Hauptschleife

void ARDUINO_ISR_ATTR onEncoderEdge() {
    const int8_t delta = decoder.update(digitalRead(ENCODER_PIN_A), digitalRead(ENCODER_PIN_B));
    if (delta != 0) pendingSteps.fetch_add(delta, std::memory_order_relaxed);
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

int32_t Input::takeEncoderSteps() {
    return pendingSteps.exchange(0, std::memory_order_relaxed);
}

bool Input::buttonRaw() {
    return digitalRead(BUTTON_PIN) == LOW;
}

}  // namespace hal
