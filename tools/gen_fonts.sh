#!/usr/bin/env bash
# Erzeugt die LVGL-Schriften in src/fonts/ aus den Quellen in assets/fonts/.
#
# Schrift: Inter (https://rsms.me/inter/, SIL Open Font License 1.1)
#   - Inter Medium  (500) für Text bis 20 px – bleibt klein gut lesbar
#   - Inter SemiBold (600) für Überschriften und große Zahlen
# Dazu die LVGL-Symbole (FontAwesome 5 Free, wie in LVGL selbst): LV_SYMBOL_PLAY,
# LV_SYMBOL_PAUSE, LV_SYMBOL_WIFI, …
#
# Die eingebauten LVGL-Schriften enthalten nur ASCII. Diese hier enthalten Latin-1
# (ä ö ü ß é …) und typografische Zeichen („ “ – … €).
#
# Voraussetzung: Node.js
#   npm install -g lv_font_conv@1.5.2
#   ./tools/gen_fonts.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="assets/fonts"   # relativ zu ROOT, damit keine lokalen Pfade in den Dateiköpfen landen
mkdir -p "$ROOT/src/fonts"

# Text: ASCII, Latin-1, – — ‘ ’ ‚ “ ” „ • … €
TEXT_RANGE="0x20-0x7E,0xA0-0xFF,0x2013-0x2014,0x2018-0x201E,0x2022,0x2026,0x20AC"
# Nur Ziffern und Zeichen für große Zahlen (Lautstärke, Zeiten): Leerzeichen % + - . 0-9 : –
NUM_RANGE="0x20,0x25,0x2B,0x2D,0x2E,0x30-0x3A,0x2013"
# LVGL-Symbole (identisch zu lvgl/scripts/built_in_font/built_in_font_gen.py)
SYMBOLS="61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650"

gen() {  # gen <name> <size> <woff> <range> [mit Symbolen: yes|no]
  local name=$1 size=$2 woff=$3 range=$4 symbols=${5:-yes}
  local args=(--no-compress --no-prefilter --bpp 4 --size "$size"
              --font "$SRC/$woff" -r "$range")
  if [[ $symbols == yes ]]; then
    args+=(--font "$SRC/FontAwesome5-Solid+Brands+Regular.woff" -r "$SYMBOLS")
  fi
  (cd "$ROOT" && lv_font_conv "${args[@]}" --format lvgl --force-fast-kern-format \
      --lv-include lvgl.h -o "src/fonts/$name.c")
  echo "src/fonts/$name.c"
}

gen font_inter_14 14 Inter-Medium.woff   "$TEXT_RANGE"
gen font_inter_20 20 Inter-Medium.woff   "$TEXT_RANGE"
gen font_inter_28 28 Inter-SemiBold.woff "$TEXT_RANGE"
gen font_inter_48 48 Inter-SemiBold.woff "$TEXT_RANGE"
gen font_num_96   96 Inter-SemiBold.woff "$NUM_RANGE" no
