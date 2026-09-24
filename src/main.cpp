// MaTouchSonos – Sonos-Fernbedienung für das Makerfabs MaTouch ESP32-S3 Rotary 2.1"
//
// Stand: Schritt 0 (Walking Skeleton) – Display, Touch, Drehring und Taste werden
// auf einem Testbildschirm angezeigt. Siehe docs/ENTWICKLUNGSPLAN.md.

#include <Arduino.h>
#include <lvgl.h>

#include "ButtonDetector.h"
#include "DetentAccumulator.h"
#include "Display.h"
#include "Input.h"
#include "TestScreen.h"
#include "board_config.h"

namespace {

app::DetentAccumulator detents(ENCODER_STEPS_PER_DETENT, ENCODER_INVERT != 0);
app::ButtonDetector button;
TestScreen screen;

constexpr uint32_t kStatusLogIntervalMs = 5000;
uint32_t lastStatusLog = 0;

void logBootInfo() {
    Serial.println();
    Serial.println(F("=== MaTouchSonos – Schritt 0: Hardware-Test ==="));
    Serial.printf("Chip: %s Rev %d, %d Kerne, %lu MHz\n", ESP.getChipModel(), ESP.getChipRevision(),
                  ESP.getChipCores(), static_cast<unsigned long>(ESP.getCpuFreqMHz()));
    Serial.printf("Flash: %lu MB\n", static_cast<unsigned long>(ESP.getFlashChipSize() / (1024 * 1024)));
    Serial.printf("PSRAM: %lu MB\n", static_cast<unsigned long>(ESP.getPsramSize() / (1024 * 1024)));
}

void logStatus(uint32_t now) {
    if (now - lastStatusLog < kStatusLogIntervalMs) return;
    lastStatusLog = now;
    Serial.printf("STATUS heap_frei=%lu psram_frei=%lu uptime_s=%lu\n",
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(ESP.getFreePsram()),
                  static_cast<unsigned long>(now / 1000));
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(500);  // USB-CDC Zeit geben, damit die ersten Zeilen nicht verloren gehen
    logBootInfo();

    if (!hal::Display::begin()) {
        Serial.println(F("FEHLER: Display-Initialisierung fehlgeschlagen"));
    }
    hal::Input::begin();
    screen.create();

    Serial.println(F("Bereit. Ring drehen, Taste drücken, Display berühren."));
}

void loop() {
    const uint32_t now = millis();

    const int32_t d = detents.add(hal::Input::takeEncoderSteps());
    if (d != 0) {
        screen.onDetents(d);
        Serial.printf("ENC %+ld\n", static_cast<long>(d));
    }

    switch (button.update(hal::Input::buttonRaw(), now)) {
        case app::ButtonEvent::Short:
            screen.onShortPress();
            Serial.println(F("BTN short"));
            break;
        case app::ButtonEvent::Long:
            screen.onLongPress();
            Serial.println(F("BTN long"));
            break;
        case app::ButtonEvent::None:
            break;
    }

    screen.tick(now);
    lv_timer_handler();
    logStatus(now);
    delay(5);
}
