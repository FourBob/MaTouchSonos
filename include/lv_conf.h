/**
 * LVGL 8.3 – Konfiguration für MaTouchSonos.
 *
 * Nur Abweichungen von den LVGL-Standardwerten stehen hier; alles andere
 * ergänzt LVGL selbst (lv_conf_internal.h). Vollständige Liste der Optionen:
 * .pio/libdeps/matouch/lvgl/lv_conf_template.h
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* --- Farben --------------------------------------------------------------- */
#define LV_COLOR_DEPTH 16      /* RGB565, wie das Panel */
#define LV_COLOR_16_SWAP 0

/* --- Speicher --------------------------------------------------------------
 * LVGL nutzt malloc/free. Im Arduino-Core landen größere Blöcke automatisch im
 * PSRAM, kleine bleiben im schnellen internen RAM. */
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC malloc
#define LV_MEM_CUSTOM_FREE free
#define LV_MEM_CUSTOM_REALLOC realloc

/* --- Zeitbasis ------------------------------------------------------------ */
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_DISP_DEF_REFR_PERIOD 16   /* ~60 Hz Bildaufbau anstreben */
#define LV_INDEV_DEF_READ_PERIOD 20  /* Touch alle 20 ms abfragen */

/* --- Diagnose ------------------------------------------------------------- */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

/* FPS/CPU-Anzeige am unteren Rand (auf dem runden Display sichtbar).
 * Wird ab Schritt 1 per Build-Flag abgeschaltet. */
#ifndef MTS_SHOW_PERF
#define MTS_SHOW_PERF 1
#endif
#define LV_USE_PERF_MONITOR MTS_SHOW_PERF
#define LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_MID

/* --- Schriften ------------------------------------------------------------
 * Eigene Schriften mit Umlauten und LVGL-Symbolen (src/fonts/, erzeugt mit
 * tools/gen_fonts.sh). Die eingebauten Montserrat-Schriften haben nur ASCII und
 * sind deshalb abgeschaltet. Verwendung: &font_de_14, &font_de_20, &font_de_28, &font_de_48 */
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_CUSTOM_DECLARE \
    LV_FONT_DECLARE(font_de_14) LV_FONT_DECLARE(font_de_20) LV_FONT_DECLARE(font_de_28) LV_FONT_DECLARE(font_de_48)
#define LV_FONT_DEFAULT &font_de_20

/* --- Theme ---------------------------------------------------------------- */
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

#endif /* LV_CONF_H */
