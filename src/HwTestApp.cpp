// Hardware-Testbildschirm (Schritt 0): Display, Touch, Drehring und Taste prüfen.
// Bauen mit:  pio run -e matouch_hwtest -t upload

#if MTS_HWTEST

#include <Arduino.h>
#include <lvgl.h>

#include "App.h"
#include "ButtonDetector.h"
#include "Diagnostics.h"
#include "Display.h"
#include "Input.h"
#include "TestScreen.h"

namespace hwtest_app {
namespace {
app::ButtonDetector button;
TestScreen screen;
int32_t rawSinceLastDetent = 0;
}  // namespace

void setup() {
    diag::logBootInfo("Hardware-Test");

    if (!hal::Display::begin()) {
        Serial.println(F("FEHLER: Display-Initialisierung fehlgeschlagen"));
    }
    hal::Input::begin();
    screen.create();

    Serial.println(F("Bereit. Ring drehen, Taste drücken, Display berühren."));
}

void loop() {
    const uint32_t now = millis();

    rawSinceLastDetent += hal::Input::takeRawSteps();
    const int32_t d = hal::Input::takeDetents();
    if (d != 0) {
        screen.onDetents(d);
        // raw = Rohschritte seit der letzten Meldung; bei langsamem Drehen ±4 pro Rastung
        // (Vollschritt-Encoder) bzw. ±2 (Halbschritt). Nur zur Diagnose.
        Serial.printf("ENC %+ld (raw %+ld)\n", static_cast<long>(d), static_cast<long>(rawSinceLastDetent));
        rawSinceLastDetent = 0;
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
    diag::logStatusPeriodically(millis(), now);
    delay(5);
}

}  // namespace hwtest_app

#endif  // MTS_HWTEST
