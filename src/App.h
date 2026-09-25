#pragma once

// Betriebsarten. main.cpp wählt per Build-Flag MTS_HWTEST genau eine davon aus:
//   pio run -e matouch          -> Fernbedienung (RemoteApp.cpp)
//   pio run -e matouch_hwtest   -> Hardware-Testbildschirm aus Schritt 0 (HwTestApp.cpp)

namespace remote_app {
void setup();
void loop();
}  // namespace remote_app

namespace hwtest_app {
void setup();
void loop();
}  // namespace hwtest_app
