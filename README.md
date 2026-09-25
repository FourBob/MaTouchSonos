# MaTouchSonos

**Eine Sonos-Fernbedienung mit Drehring und rundem Touch-Display**, gebaut auf dem
[Makerfabs MaTouch ESP32-S3 Rotary IPS Display 2.1" (ST7701)](https://www.makerfabs.com/matouch-esp32-s3-rotary-ips-display-with-touch-2-1-st7701.html).

Die Fernbedienung spricht **direkt im Heimnetz** mit den Sonos-Speakern (UPnP/SOAP auf Port 1400).
Sie braucht keine Cloud, kein Konto und keinen Zusatzserver.

[![CI](https://github.com/FourBob/MaTouchSonos/actions/workflows/ci.yml/badge.svg)](https://github.com/FourBob/MaTouchSonos/actions/workflows/ci.yml)

> **Projektstand:** Schritt 5 von 9: Now Playing, Lautstärke, Play/Pause, Titelwechsel, Ringmenü, Spulen und Raumwahl. Die Anlage wird automatisch gefunden.
> Die Sonos-Funktionen entstehen Schritt für Schritt, siehe [Entwicklungsplan](docs/ENTWICKLUNGSPLAN.md).

---

## Bedienkonzept

Die Bedienung ist auf die runde Form und den Drehring ausgelegt:

| Aktion | Wirkung |
|---|---|
| **Ring drehen** | Lautstärke (Bogen am Rand zeigt den Wert) |
| **Kurz drücken** | Play / Pause |
| **Lang drücken** | Ringmenü: **Spulen**, **Räume**, **Favoriten** (ab Schritt 7), **Schließen**. Der Ring blättert, Drücken wählt, Langdruck schließt. |
| **Räume** | Drehrad mit allen Räumen und Gruppen („Küche + 2“). Drücken schaltet um, der Raum wird für den nächsten Start gemerkt. |
| **Nach rechts / links wischen** | Nächster / vorheriger Titel |
| **Spulen** | Der Ring verschiebt die Zielposition auf dem Fortschrittsring (1 % pro Klick, mindestens 5 s), Drücken springt dorthin, Langdruck bricht ab |

Auf dem **Now-Playing-Bildschirm** stehen Titel und Interpret vor dem runden Albumcover.
Außen läuft der Fortschrittsbogen.

## Funktionen und Fahrplan

| Schritt | Funktion | Status |
|---|---|---|
| 0 | Hardware läuft: Display, Touch, Drehring, Taste | ✅ getestet |
| 1 | Lautstärke eines Speakers mit dem Ring regeln | ✅ getestet |
| 2 | Play/Pause mit Statusanzeige | ✅ getestet |
| 3 | Now Playing (Titel, Fortschritt) und Titelwechsel per Wischen | ✅ getestet |
| 4 | Ringmenü und Scrubbing | ✅ getestet |
| 5 | Räume automatisch finden und wählen | 🧪 wartet auf Geräte-Test |
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

### 3. WLAN-Zugangsdaten eintragen

```bash
cp include/secrets.example.h include/secrets.h
# include/secrets.h öffnen und WLAN-Name und Passwort eintragen
```

`include/secrets.h` steht in `.gitignore` und wird **nie** committet.

Die Sonos-Anlage findet die Fernbedienung selbst (SSDP). Den Raum wählst du am Gerät im Ringmenü.
Optional in `secrets.h`:
- `SONOS_ROOM "Wohnzimmer"`: **bevorzugter Raum**. Mit ihm startet das Gerät immer. Leer lassen, um mit
  dem zuletzt im Menü gewählten Raum zu starten.
- `SONOS_IP`: IP eines Speakers als schneller Startpunkt für die Suche. Welche Speaker es gibt, zeigt
`python3 tools/sonos_probe.py - discover`.

> Das Board kann nur **2,4-GHz-WLAN**. Bei getrennten 2,4-/5-GHz-Netzen den 2,4-GHz-Namen eintragen.

### 4. Bauen und flashen

Board per USB-C anschließen, dann:

```bash
pio run -e matouch -t upload      # bauen + flashen
pio device monitor -e matouch     # serielles Log ansehen (Beenden: Strg+C)
```

Für den Hardware-Testbildschirm aus Schritt 0 (Display, Touch, Drehring, Taste prüfen):

```bash
pio run -e matouch_hwtest -t upload
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

Die CI-Firmware kennt dein WLAN nicht: Sie zeigt nur den Hinweis, `secrets.h` auszufüllen.
Für die Fernbedienung also selbst bauen. Für den Hardware-Test reicht die CI-Firmware.

## Tests

```bash
pio test -e native                                  # Unit-Tests auf dem PC, kein Board nötig
pio run  -e matouch                                 # Firmware bauen
python3 tools/sonos_probe.py <Speaker-IP> volume get  # Sonos-Befehl vom PC aus testen
python3 tools/sonos_probe.py <Speaker-IP> transport info
python3 tools/sonos_probe.py <Speaker-IP> nowplaying
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
  app_core/             Bedienlogik (Encoder, Taste, Lautstärke-Drossel), auf dem PC getestet
  sonos_core/           Sonos-Protokoll (SOAP, XML), auf dem PC getestet
  hal/                  Hardware: Display + LVGL, Touch, Drehring/Taste
  net/                  WLAN und Sonos-Kommunikation in eigener Task
src/
  main.cpp              Einstieg, wählt die Betriebsart
  RemoteApp.cpp         Fernbedienung
  HwTestApp.cpp         Hardware-Test (Schritt 0)
  NowPlayingScreen.*    Now Playing mit Lautstärke-Einblendung
  TestScreen.*          Hardware-Testbildschirm
  fonts/                Schrift Inter mit Umlauten (erzeugt mit tools/gen_fonts.sh)
test/
  test_app_core/        Unit-Tests Bedienlogik
  test_sonos_core/      Unit-Tests Sonos-Protokoll, inkl. Testdaten (fixtures.h)
tools/
  sonos_probe.py        Sonos-Befehle vom PC aus testen (T3)
  gen_fonts.sh          Schriften neu erzeugen
assets/
  fonts/                Schrift-Quellen (Inter, Symbole) mit Lizenzen
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
| Display bleibt schwarz | Im Log nach `FEHLER: Display` suchen. PSRAM muss als OPI erkannt werden (`PSRAM: 7.9 MB` im Log). |
| Build-Fehler `include/secrets.h fehlt` | `cp include/secrets.example.h include/secrets.h` und Werte eintragen. |
| Anzeige „include/secrets.h ausfüllen“ | Die Datei enthält noch den Platzhalter `MeinWLAN`. |
| „Keine Sonos-Anlage gefunden“ | Gerät und Speaker müssen im selben Netz sein. Manche Router blockieren Multicast zwischen 2,4 und 5 GHz oder im Gast-WLAN. Abhilfe: `SONOS_IP` in `secrets.h` eintragen, dann wird die Anlage über diese IP gefunden. |
| Bleibt bei „WLAN verbinden …“ | SSID/Passwort prüfen. Nur 2,4 GHz wird unterstützt. |
| Wischen wechselt den Titel nicht | Bei Radio, TV und Line-In gibt es keinen nächsten Titel (Hinweis „Bei dieser Quelle nicht möglich“). Sonst: zügig über mindestens ein Drittel des Displays wischen. |
| „Nichts zum Abspielen“ | Die Warteschlange des Speakers ist leer. Erst in der Sonos-App etwas starten. Ab Schritt 7 geht das mit Favoriten direkt am Gerät. |
| „Speaker nicht erreichbar“ | Speaker-IP prüfen: `python3 tools/sonos_probe.py <IP> info` muss den Raumnamen zeigen. |
| Farben vertauscht (Rot ↔ Blau) | Die drei Farbbalken oben auf dem Testbild prüfen und das Ergebnis melden. Die Pins in `board_config.h` werden dann angepasst. |
| Kein serielles Log | Der USB-C-Port ist der native USB des ESP32-S3. Nach dem Flashen einmal RESET drücken und den Monitor neu verbinden. |
| Nur jede zweite Rastung zählt | In `include/board_config.h` `ENCODER_HALF_STEP` auf `1` setzen. Das Log zeigt dann `raw +2` statt `raw +4`. |
| Ring läuft falsch herum | In `include/board_config.h` `ENCODER_INVERT` umschalten (0 ↔ 1). |
| Upload schlägt fehl | Board in den Bootloader bringen (BOOT halten, RESET drücken), siehe oben. |

## Danksagung

- [Makerfabs](https://github.com/Makerfabs/MaTouch-ESP32-S3-Rotary-IPS-Display-with-Touch-2.1-ST7701) für Beispielcode und Schaltplan
- [LVGL](https://lvgl.io/) und [Arduino_GFX](https://github.com/moononournation/Arduino_GFX)
- Schrift [Inter](https://rsms.me/inter/) von Rasmus Andersson (SIL Open Font License), Symbole von Font Awesome Free
- Die Sonos-Community, die die lokale UPnP-Schnittstelle dokumentiert hat, z. B. [SoCo](https://github.com/SoCo/SoCo) und [sonos.svrooij.io](https://sonos.svrooij.io/)

Dieses Projekt ist nicht mit Sonos, Inc. verbunden. „Sonos“ ist eine Marke von Sonos, Inc.

## Lizenz

[MIT](LICENSE)
