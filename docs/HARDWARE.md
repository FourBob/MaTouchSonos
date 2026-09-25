# Hardware: MaTouch ESP32-S3 Rotary IPS Display 2.1" (ST7701)

Hersteller: Makerfabs · Board-Revision laut Schaltplan: **v1.1**
Quellen: [Makerfabs-Repo mit Schaltplan und Beispielen](https://github.com/Makerfabs/MaTouch-ESP32-S3-Rotary-IPS-Display-with-Touch-2.1-ST7701),
[Wiki](https://wiki.makerfabs.com/MaTouch_ESP32_S3_2.1_Rotary_TFT_with_Touch.html)

## Eckdaten

| Komponente | Details |
|---|---|
| SoC | ESP32-S3 (2× Xtensa LX7, 240 MHz), WLAN 2,4 GHz, Bluetooth LE 5 |
| Speicher | 16 MB Flash (QIO), 8 MB PSRAM (**OPI**) |
| Display | 2,1" IPS, rund, 480×480, Treiber ST7701S, RGB565 parallel, bis 75 FPS |
| Touch | kapazitiv, CST826, I2C-Adresse `0x15` |
| Eingabe | Drehring (Quadratur-Encoder) mit Drucktaster |
| USB | USB-C, nativer USB des ESP32-S3 (Flashen + serielles Log) |
| Tasten | BOOT (GPIO 0) und RESET |

## Pinbelegung

Alle Werte stehen zentral in [`include/board_config.h`](../include/board_config.h).

### Display: Initialisierung (3-Wire-SPI)

Der ST7701 wird beim Start einmal per SPI konfiguriert. Danach kommen die Bilddaten nur noch über den RGB-Bus.

| Signal | GPIO |
|---|---|
| CS | 1 |
| SCK | 46 |
| SDA | 0 |

### Display: RGB-Bus

| Signal | GPIO | | Signal | GPIO | | Signal | GPIO |
|---|---|---|---|---|---|---|---|
| R0 | 4 | | G0 | 39 | | B0 | 11 |
| R1 | 41 | | G1 | 7 | | B1 | 15 |
| R2 | 5 | | G2 | 47 | | B2 | 12 |
| R3 | 40 | | G3 | 8 | | B3 | 16 |
| R4 | 6 | | G4 | 48 | | B4 | 21 |
| | | | G5 | 9 | | | |

| Steuersignal | GPIO |
|---|---|
| DE | 2 |
| VSYNC | 42 |
| HSYNC | 3 |
| PCLK | 45 |
| Hintergrundbeleuchtung | 38 |

Timing: HSYNC Front/Pulse/Back Porch = 10/8/50, VSYNC = 10/8/20, beide Polaritäten 1.
Init-Sequenz: `st7701_type5_init_operations` aus Arduino_GFX 1.3.8.

> Hinweis: Das ältere Makerfabs-Beispiel `fw_test` nennt die R- und B-Pins in umgekehrter
> Reihenfolge und setzt dafür `BGR = true`. Beide Varianten ergeben dasselbe Bild. Dieses Projekt
> übernimmt die Belegung aus `fw_test_v2`. Die Farbbalken im Testbild von Schritt 0 prüfen das.

### Touch (I2C)

| Signal | GPIO |
|---|---|
| SDA | 17 |
| SCL | 18 |
| INT / RST | nicht verwendet |

Der Touch wird per Polling gelesen (Register `0x02`, 5 Bytes).

### Drehring und Taste

| Signal | GPIO | Hinweis |
|---|---|---|
| Encoder A (CLK) | 13 | interner Pull-up, gezählt vom Hardware-Pulszähler PCNT (Unit 0) |
| Encoder B (DT) | 10 | interner Pull-up, PCNT Unit 0 |
| Taste | 14 | aktiv LOW, interner Pull-up |

### Erweiterung

Auf der Platine gibt es je einen I2C- und einen UART-Anschluss. Beide werden in diesem Projekt nicht genutzt.

## Speicherbudget (Planung)

| Verbraucher | Größe | Ort |
|---|---|---|
| Framebuffer des Panels | 480×480×2 = 450 KB | PSRAM |
| LVGL-Zeichenpuffer | 2 × 45 KB | interner RAM (Fallback PSRAM) |
| Albumcover (ab Schritt 6) | ca. 450 KB dekodiert | PSRAM |
| WLAN-Stack, HTTP | ca. 60–80 KB | interner RAM |
