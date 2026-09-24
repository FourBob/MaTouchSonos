# MaTouchSonos

**Eine Sonos-Fernbedienung mit Drehring und rundem Touch-Display**, gebaut auf dem
[Makerfabs MaTouch ESP32-S3 Rotary IPS Display 2.1" (ST7701)](https://www.makerfabs.com/matouch-esp32-s3-rotary-ips-display-with-touch-2-1-st7701.html).

Die Fernbedienung spricht **direkt im Heimnetz** mit den Sonos-Speakern (UPnP/SOAP auf Port 1400).
Sie braucht keine Cloud, kein Konto und keinen Zusatzserver.

[![CI](https://github.com/FourBob/MaTouchSonos/actions/workflows/ci.yml/badge.svg)](https://github.com/FourBob/MaTouchSonos/actions/workflows/ci.yml)

> **Projektstand:** Schritt 0 von 9, der Hardware-Test.
> Die Sonos-Funktionen entstehen Schritt für Schritt, siehe [Entwicklungsplan](docs/ENTWICKLUNGSPLAN.md).

---

## Bedienkonzept

Die Bedienung ist auf die runde Form und den Drehring ausgelegt:

| Aktion | Wirkung |
|---|---|
| **Ring drehen** | Lautstärke (Bogen am Rand zeigt den Wert) |
| **Kurz drücken** | Play / Pause |
| **Lang drücken** | Ringmenü: **Raum**, **Favoriten**, **Scrub** – der Ring blättert, Drücken wählt |
| **Wischen links/rechts** | Nächster / vorheriger Titel |
| **Scrub-Modus** | Der Ring verschiebt die Position auf dem Fortschrittsbogen, Drücken springt dorthin |

Auf dem **Now-Playing-Bildschirm** stehen Titel und Interpret vor dem runden Albumcover.
Außen läuft der Fortschrittsbogen.

## Funktionen und Fahrplan

| Schritt | Funktion | Status |
|---|---|---|
| 0 | Hardware läuft: Display, Touch, Drehring, Taste | 🧪 wartet auf Geräte-Test |
| 1 | Lautstärke eines Speakers mit dem Ring regeln | ⏳ |
| 2 | Play/Pause mit Statusanzeige | ⏳ |
| 3 | Now Playing (Titel, Fortschritt) und Titelwechsel per Wischen | ⏳ |
| 4 | Ringmenü und Scrubbing | ⏳ |
| 5 | Räume automatisch finden und wählen | ⏳ |
| 6 | Albumcover | ⏳ |
| 7 | Sonos-Favoriten und Radio starten | ⏳ |
| 8 | Gruppen verwalten, Gruppenlautstärke | ⏳ |
| 9 | Live-Updates, Energiesparen, Feinschliff | ⏳ |

Details zu jedem Schritt mit Akzeptanzkriterien und Tests stehen in [docs/ENTWICKLUNGSPLAN.md](docs/ENTWICKLUNGSPLAN.md).

## Hardware

- **Makerfabs MaTouch ESP32-S3 Rotary IPS Display with Touch 2.1" ST7701** (z. B. RobotShop RB-Mkf-69)
  - ESP32-S3, 16 MB Flash, 8 MB PSRAM, WLAN
  - 480×480 rundes IPS-Display, ST7701S, RGB565
  - Kapazitiver Touch CST826
  - Drehring mit Drucktaster
- USB-C-Kabel (Daten + Strom)
- Sonos-Anlage im selben WLAN (getestet mit **Sonos S2**)

Die Pinbelegung steht in [docs/HARDWARE.md](docs/HARDWARE.md).

## Schnellstart

### 1. Voraussetzungen

- Git
- **PlatformIO**: entweder als VS-Code-Erweiterung oder als Kommandozeilen-Werkzeug `pio`

**PlatformIO für die Kommandozeile installieren:**

| System | Befehl |
|---|---|
| macOS (Homebrew) | `brew install platformio` |
| Linux / macOS ohne Homebrew | offizieller Installer, siehe unten |
| Windows | [Installer-Anleitung](https://docs.platformio.org/en/latest/core/installation/methods/installer-script.html) oder VS-Code-Erweiterung |

```bash
# Offizieller Installer (macOS/Linux)
curl -fsSL -o get-platformio.py https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py
python3 get-platformio.py
# pio in den Suchpfad aufnehmen (zsh; bei bash ~/.bashrc statt ~/.zshrc)
echo 'export PATH="$PATH:$HOME/.platformio/penv/bin"' >> ~/.zshrc
source ~/.zshrc
```

Prüfen mit `pio --version`. Meldet die Shell `command not found: pio`, obwohl die
VS-Code-Erweiterung installiert ist, fehlt nur die `export PATH=…`-Zeile von oben.

**Alternativ VS Code:** [VS Code](https://code.visualstudio.com/) installieren und dort die Erweiterung
**PlatformIO IDE** hinzufügen. Das Projekt öffnen und über die PlatformIO-Leiste unten bauen und flashen.

Beim ersten Build lädt PlatformIO die ESP32-Toolchain herunter (ca. 500 MB, einige Minuten).

### 2. Projekt holen

```bash
git clone https://github.com/FourBob/MaTouchSonos.git
cd MaTouchSonos
```

### 3. WLAN-Zugangsdaten eintragen (ab Schritt 1)

```bash
cp include/secrets.example.h include/secrets.h
# include/secrets.h öffnen und WLAN-Name, Passwort und Speaker-IP eintragen
```

`include/secrets.h` steht in `.gitignore` und wird **nie** committet.

### 4. Bauen und flashen

Board per USB-C anschließen, dann:

```bash
pio run -e matouch -t upload      # bauen + flashen
pio device monitor -e matouch     # serielles Log ansehen (Beenden: Strg+C)
```

In VS Code geht das über die PlatformIO-Leiste unten: **→ Upload**, danach **🔌 Serial Monitor**.

Wenn das Flashen nicht startet: **BOOT**-Taste gedrückt halten, **RESET** kurz drücken,
BOOT loslassen. Danach Upload erneut starten und anschließend RESET drücken.

### Alternative: fertige Firmware aus der CI

Jeder Push baut die Firmware automatisch. Unter **Actions → letzter Lauf → Artifacts →
`firmware-matouch`** liegen `bootloader.bin`, `partitions.bin` und `firmware.bin`.
Flashen z. B. mit dem [ESP Web Flasher](https://espressif.github.io/esptool-js/):

| Adresse | Datei |
|---|---|
| `0x0000` | bootloader.bin |
| `0x8000` | partitions.bin |
| `0x10000` | firmware.bin |

(Für Schritt 0 reicht das. Ab Schritt 1 enthält die CI-Firmware nur Platzhalter-WLAN-Daten,
dann also selbst bauen.)

## Tests

```bash
pio test -e native     # Unit-Tests der Logik auf dem PC, kein Board nötig
pio run  -e matouch    # Firmware bauen
```

Das Projekt wird auf vier Ebenen getestet: Unit-Tests, CI-Build, Protokoll-Tests gegen die
echten Speaker und Checklisten am Gerät. Die Anleitung dazu steht in [docs/TESTEN.md](docs/TESTEN.md).

## Projektstruktur

```
include/
  board_config.h        Pinbelegung und Hardware-Parameter
  lv_conf.h             LVGL-Konfiguration
  secrets.example.h     Vorlage für WLAN-Zugangsdaten
lib/
  app_core/             Hardwareunabhängige Logik (Encoder, Taste, Bedienlogik), auf dem PC getestet
  hal/                  Hardware: Display + LVGL, Touch, Drehring/Taste
src/
  main.cpp              Start und Hauptschleife
  TestScreen.*          Hardware-Testbildschirm (Schritt 0)
test/
  test_app_core/        Unit-Tests
docs/
  ENTWICKLUNGSPLAN.md   Schritte, Akzeptanzkriterien, Teststrategie
  ARCHITEKTUR.md        Aufbau der Software und Designentscheidungen
  HARDWARE.md           Pinbelegung, Board-Details
  TESTEN.md             So wird jeder Schritt getestet und zurückgemeldet
```

Mehr dazu in [docs/ARCHITEKTUR.md](docs/ARCHITEKTUR.md).

## Fehlerbehebung

| Problem | Lösung |
|---|---|
| Display bleibt schwarz | Im Log nach `FEHLER: Display` suchen. PSRAM muss als OPI erkannt werden (`PSRAM: 8 MB` im Log). |
| Farben vertauscht (Rot ↔ Blau) | Die drei Farbbalken oben auf dem Testbild prüfen und das Ergebnis melden. Die Pins in `board_config.h` werden dann angepasst. |
| Kein serielles Log | Der USB-C-Port ist der native USB des ESP32-S3. Nach dem Flashen einmal RESET drücken und den Monitor neu verbinden. |
| Ring zählt doppelt oder halb | In `include/board_config.h` `ENCODER_STEPS_PER_DETENT` anpassen (2, 4 oder 8). |
| Ring läuft falsch herum | In `include/board_config.h` `ENCODER_INVERT` auf `1` setzen. |
| Upload schlägt fehl | Board in den Bootloader bringen (BOOT halten, RESET drücken), siehe oben. |

## Danksagung

- [Makerfabs](https://github.com/Makerfabs/MaTouch-ESP32-S3-Rotary-IPS-Display-with-Touch-2.1-ST7701) für Beispielcode und Schaltplan
- [LVGL](https://lvgl.io/) und [Arduino_GFX](https://github.com/moononournation/Arduino_GFX)
- Die Sonos-Community, die die lokale UPnP-Schnittstelle dokumentiert hat, z. B. [SoCo](https://github.com/SoCo/SoCo) und [sonos.svrooij.io](https://sonos.svrooij.io/)

Dieses Projekt ist nicht mit Sonos, Inc. verbunden. „Sonos“ ist eine Marke von Sonos, Inc.

## Lizenz

[MIT](LICENSE)
