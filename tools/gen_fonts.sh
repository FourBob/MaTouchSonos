#!/usr/bin/env bash
# Erzeugt die LVGL-Schriften in src/fonts/ neu.
#
# Warum eigene Schriften? Die in LVGL eingebauten Montserrat-Schriften enthalten nur
# ASCII – Umlaute (ä ö ü ß), „–“ oder „…“ würden fehlen. Diese Schriften enthalten
# Latin-1 (alle westeuropäischen Zeichen), typografische Zeichen und die LVGL-Symbole
# (LV_SYMBOL_PLAY, LV_SYMBOL_PAUSE, LV_SYMBOL_WIFI, …).
#
# Voraussetzungen: Node.js, LVGL-Quellen (für Montserrat + FontAwesome)
#   npm install -g lv_font_conv@1.5.2
#   ./tools/gen_fonts.sh [Pfad-zu-lvgl]
# Ohne Pfad wird .pio/libdeps/matouch/lvgl verwendet (nach dem ersten Build vorhanden).
set -euo pipefail

LVGL_DIR="${1:-.pio/libdeps/matouch/lvgl}"
FONT_DIR="$LVGL_DIR/scripts/built_in_font"
OUT_DIR="$(dirname "$0")/../src/fonts"

# Text: ASCII, Latin-1, – — ‘ ’ ‚ “ ” „ • … €
TEXT_RANGE="0x20-0x7E,0xA0-0xFF,0x2013-0x2014,0x2018-0x201E,0x2022,0x2026,0x20AC"
# LVGL-Symbole (identisch zu lvgl/scripts/built_in_font/built_in_font_gen.py)
SYMBOLS="61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650"

for SIZE in 14 20 28 48; do
  lv_font_conv --no-compress --no-prefilter --bpp 4 --size "$SIZE" \
    --font "$FONT_DIR/Montserrat-Medium.ttf" -r "$TEXT_RANGE" \
    --font "$FONT_DIR/FontAwesome5-Solid+Brands+Regular.woff" -r "$SYMBOLS" \
    --format lvgl --force-fast-kern-format --lv-include lvgl.h \
    -o "$OUT_DIR/font_de_${SIZE}.c"
  echo "src/fonts/font_de_${SIZE}.c erzeugt"
done
