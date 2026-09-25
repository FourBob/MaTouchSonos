#pragma once

#include <stdint.h>

/** Serielle Diagnose-Ausgaben, gemeinsam für alle Betriebsarten. */
namespace diag {

/** Kopfzeile, Chip, Flash, PSRAM. */
void logBootInfo(const char* mode);

/** Alle 5 s eine STATUS-Zeile mit freiem Speicher und Laufzeit. */
void logStatusPeriodically(uint32_t nowMs);

}  // namespace diag
