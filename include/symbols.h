#pragma once

// Zusätzliche Symbole aus Font Awesome 5, die LVGL nicht mitbringt.
// Sie sind in unseren Schriften enthalten (tools/gen_fonts.sh, Liste SYMBOLS).
// Verwendung wie LV_SYMBOL_…, z. B. lv_label_set_text(l, MTS_SYMBOL_BACKWARD MTS_SYMBOL_FORWARD);

#define MTS_SYMBOL_BACKWARD "\xEF\x81\x8A" /* U+F04A zurückspulen */
#define MTS_SYMBOL_FORWARD  "\xEF\x81\x8E" /* U+F04E vorspulen */
