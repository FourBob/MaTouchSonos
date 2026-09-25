#pragma once

#include <stdint.h>

/** Serielle Diagnose-Ausgaben, gemeinsam für alle Betriebsarten. */
namespace diag {

/** Kopfzeile, Chip, Flash, PSRAM. */
void logBootInfo(const char* mode);

/**
 * Alle 5 s eine STATUS-Zeile mit freiem Speicher, Laufzeit und der längsten
 * Schleifendauer seit der letzten Zeile (loop_max_ms). Am Ende jedes loop() aufrufen,
 * `loopStartMs` = millis() vom Anfang des Durchlaufs.
 */
void logStatusPeriodically(uint32_t nowMs, uint32_t loopStartMs);

}  // namespace diag
