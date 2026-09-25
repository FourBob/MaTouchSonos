#include "Diagnostics.h"

#include <Arduino.h>

namespace diag {
namespace {
constexpr uint32_t kStatusLogIntervalMs = 5000;
uint32_t lastStatusLog = 0;
}  // namespace

void logBootInfo(const char* mode) {
    Serial.println();
    Serial.printf("=== MaTouchSonos – %s ===\n", mode);
    Serial.printf("Chip: %s Rev %d, %d Kerne, %lu MHz\n", ESP.getChipModel(), ESP.getChipRevision(),
                  ESP.getChipCores(), static_cast<unsigned long>(ESP.getCpuFreqMHz()));
    Serial.printf("Flash: %lu MB\n", static_cast<unsigned long>(ESP.getFlashChipSize() / (1024 * 1024)));
    // Ein kleiner Teil des 8-MB-PSRAM ist reserviert, daher mit Nachkommastelle statt abgerundet.
    Serial.printf("PSRAM: %.1f MB\n", ESP.getPsramSize() / (1024.0 * 1024.0));
}

void logStatusPeriodically(uint32_t nowMs) {
    if (nowMs - lastStatusLog < kStatusLogIntervalMs) return;
    lastStatusLog = nowMs;
    Serial.printf("STATUS heap_frei=%lu psram_frei=%lu uptime_s=%lu\n",
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(ESP.getFreePsram()),
                  static_cast<unsigned long>(nowMs / 1000));
}

}  // namespace diag
