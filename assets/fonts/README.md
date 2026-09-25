# Schrift-Quellen

Aus diesen Dateien erzeugt `tools/gen_fonts.sh` die LVGL-Schriften in `src/fonts/`.

| Datei | Schrift | Lizenz |
|---|---|---|
| `Inter-Medium.woff` | [Inter](https://rsms.me/inter/) 500, Latin-Teilmenge (über [Fontsource](https://fontsource.org/fonts/inter)) | SIL Open Font License 1.1, siehe `LICENSE-Inter.txt` |
| `Inter-SemiBold.woff` | Inter 600, Latin-Teilmenge | SIL Open Font License 1.1 |
| `FontAwesome5-Solid+Brands+Regular.woff` | Font Awesome 5 Free, aus LVGL 8.3 übernommen (die Quelle der `LV_SYMBOL_*`-Zeichen) | Icons CC BY 4.0, Schrift SIL OFL 1.1 ([fontawesome.com/license/free](https://fontawesome.com/license/free)) |

Eine andere Schrift ausprobieren: `.woff`- oder `.ttf`-Datei hier ablegen, in `tools/gen_fonts.sh`
eintragen und das Skript ausführen.
