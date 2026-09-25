// MaTouchSonos – Sonos-Fernbedienung für das Makerfabs MaTouch ESP32-S3 Rotary 2.1"
//
// Einstiegspunkt. Die eigentliche Logik steckt in den Betriebsarten (siehe App.h):
//   RemoteApp.cpp  Fernbedienung           pio run -e matouch -t upload
//   HwTestApp.cpp  Hardware-Test (Schritt 0) pio run -e matouch_hwtest -t upload
//
// Entwicklungsstand und Fahrplan: docs/ENTWICKLUNGSPLAN.md

#include <Arduino.h>

#include "App.h"

void setup() {
    Serial.begin(115200);
    delay(500);  // USB-CDC Zeit geben, damit die ersten Zeilen nicht verloren gehen

#if MTS_HWTEST
    hwtest_app::setup();
#else
    remote_app::setup();
#endif
}

void loop() {
#if MTS_HWTEST
    hwtest_app::loop();
#else
    remote_app::loop();
#endif
}
