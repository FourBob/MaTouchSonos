# Architektur

## Leitgedanken

1. **Testbar ohne Hardware.** Die Logik liegt in reinem C++ ohne Arduino-Abhängigkeit: Protokoll,
   Parser und Bedienlogik. Sie wird auf dem PC mit `pio test -e native` getestet. Der
   hardwarenahe Code bleibt dünn.
2. **Vertikale Schritte.** Jede Erweiterung liefert eine Funktion, die man am Gerät benutzen kann
   (siehe [ENTWICKLUNGSPLAN.md](ENTWICKLUNGSPLAN.md)).
3. **Die UI hängt nie.** Netzwerkzugriffe laufen später in einer eigenen FreeRTOS-Task. Die UI
   reagiert sofort („optimistisch“) und gleicht sich danach mit dem echten Zustand ab.
4. **Lokal statt Cloud.** Sonos wird direkt per UPnP/SOAP auf Port 1400 angesprochen.

## Schichten

```
┌──────────────────────────────────────────────────────────┐
│ src/            main.cpp → RemoteApp / HwTestApp          │
│                 NowPlayingScreen, TestScreen, fonts/      │
├──────────────────────────────────────────────────────────┤
│ (lib/ui/        Bildschirme wandern hierher, sobald es    │
│                 mehrere gibt – ab Schritt 3)              │
├───────────────────────────────┬──────────────────────────┤
│ lib/app_core/   Bedienlogik   │ lib/sonos_core/          │
│  DetentTracker                │  SOAP-Envelopes, XML-    │
│                               │  und DIDL-Parser,        │
│  ButtonDetector               │  RenderingControl        │
│  VolumeController             │  AVTransport             │
│  PlaybackController           │  NowPlaying (DIDL-Lite)  │
│  ProgressTracker              │  (Topologie ab Schritt 5)│
│  ModeController (Menü/Spulen) │                          │
│        ── reines C++, auf dem PC getestet ──             │
├───────────────────────────────┴──────────────────────────┤
│ lib/net/        WLAN, HTTP, SSDP (ab Schritt 1/5)         │
│ lib/hal/        Display+LVGL, Touch, Drehring (PCNT), Taste│
├──────────────────────────────────────────────────────────┤
│ Arduino-ESP32 2.0.x · Arduino_GFX 1.3.8 · LVGL 8.3.11     │
└──────────────────────────────────────────────────────────┘
```

Abhängigkeiten zeigen nur nach unten. `app_core` und `sonos_core` kennen weder Arduino noch LVGL.

## Datenfluss der Eingaben

```
Drehring ──A/B──▶ PCNT (Hardware-Zähler,       ──Position──▶ DetentTracker ──Rastungen──▶ loop()
                  Glitch-Filter 12,8 µs)         + Ruhelage     (app_core)
Taste ──Pegel──▶ ButtonDetector ──Short/Long──▶ loop()
Touch ◀──I2C-Polling── LVGL-Eingabetreiber
```

- Der **PCNT** (Pulse Counter) des ESP32-S3 zählt jede Flanke beider Encoder-Signale in
  Hardware (Quadratur x4) und filtert Prellimpulse heraus. Er verliert keine Schritte, egal
  wie beschäftigt die CPU ist.
- Der **DetentTracker** fragt in jedem Schleifendurchlauf Position und Pin-Pegel ab. Nur wenn
  der Encoder in der **Ruhelage** steht (A = B = 1), rechnet er die Schritte seit der letzten
  Ruhelage in ganze Rastungen um (auf die nächste Rastung gerundet) und synchronisiert neu.
  Einzelne verlorene Schritte erzeugen so keinen dauerhaften Versatz, und Hin-und-her-Drehen
  kommt vollständig an.
- Der **ButtonDetector** meldet `Long` schon beim Erreichen von 600 ms, nicht erst beim
  Loslassen. So öffnet sich das Menü, während der Finger noch drückt.

**Vorgeschichte, aus den Geräte-Tests gelernt:**
1. Schritt 0: Ein reiner Schrittzähler („4 Schritte = 1 Klick“) verschob sich nach einem
   verlorenen Schritt dauerhaft, Hin-und-her-Drehen kam nicht mehr an.
   Abhilfe: Synchronisation in der Ruhelage.
2. Schritt 1: Die Auswertung per GPIO-Interrupt verlor bei schnellem Drehen Schritte, sobald
   WLAN und Bildaufbau liefen (10 Klicks ergaben nur 2).
   Abhilfe: Zählen im Hardware-Zähler PCNT.

## Bedienmodi (ab Schritt 4)

`ModeController` (app_core) entscheidet, was Drehring und Taste gerade bedeuten. Er führt nichts
selbst aus, sondern gibt eine `Action` zurück, die `RemoteApp::apply()` umsetzt. So ist die ganze
Bedienlogik mit Timeouts und Sonderfällen auf dem PC testbar.

| Modus  | Ring drehen          | kurz drücken         | lang drücken |
|--------|----------------------|----------------------|--------------|
| Normal | Lautstärke           | Play/Pause           | Menü öffnen  |
| Menü   | Eintrag wählen       | Eintrag ausführen    | schließen    |
| Spulen | Zielposition ändern  | dorthin springen     | abbrechen    |

Menü und Spulen schließen sich nach 10 s ohne Eingabe. Noch nicht verfügbare Menüpunkte (Räume,
Favoriten) werden angezeigt, aber beim Drehen übersprungen.

## Nebenläufigkeit (ab Schritt 1)

```
Kern 1: loop()                                   Kern 0: SonosLink-Task
─────────────────                                ─────────────────────
Drehring → VolumeController                      WLAN aufbauen / überwachen
   │  (Anzeige sofort, Drossel 150 ms)           alle 1,5 s: TransportInfo, PositionInfo, Volume
   └─ setVolume(v) ──▶ [Queue, Länge 1] ──▶ SetVolume an Speaker (HTTP, Port 1400)
                        (neuester Wert gewinnt)
Taste → PlaybackController
   │  (Anzeige sofort)
   └─ transport(cmd) ─▶ [Queue, Länge 4] ──▶ Play / Pause (Pause abgelehnt → Stop)
Wischen ── transport(Next/Previous) ───────▶ Next / Previous
Anzeige ◀── pollEvent() ◀── [Event-Queue] ◀── WLAN-/Speaker-Zustand, Lautstärke, Fehler
Anzeige ◀── takeNowPlaying() ◀── [Momentaufnahme, Mutex] ◀── GetPositionInfo (+ GetMediaInfo
                                                              bei Quellenwechsel)
```

- LVGL wird nur von Kern 1 aus angefasst.
- Die UI wartet nie auf das Netz. Ein HTTP-Timeout (bis 2 s) bremst nur die Netzwerk-Task.
- Die Queue für die Lautstärke hat die Länge 1 und wird überschrieben. Beim schnellen Drehen
  sammeln sich deshalb keine veralteten Befehle an.
- **Konfliktregeln** (beide in `app_core`, getestet): Werte vom Speaker überschreiben die Anzeige
  nicht, solange der Nutzer gerade dreht (1 s) bzw. ein Play/Pause noch unbestätigt ist (2,5 s).
  Danach gewinnt der Speaker. So folgen Änderungen aus der Sonos-App, ohne dass die Anzeige springt.
- **Titelinfos** (Titel, Interpret, Album, Cover-URL) sind zu groß für die Event-Queue. Die Task
  legt sie als Momentaufnahme mit fester Puffergröße ab (Mutex-geschützt, Versionszähler), die UI
  holt sie sich ab, wenn sich die Version geändert hat.
- **Fortschritt:** Zwischen den Abfragen zählt `ProgressTracker` lokal weiter, damit der Ring
  flüssig läuft. Neu gezeichnet wird nur einmal pro Sekunde.
- **Aussetzer-Toleranz** (seit Schritt 3, aus dem Geräte-Test gelernt): Bei schwachem WLAN
  antwortet der Speaker gelegentlich nicht innerhalb des Timeouts. Einzelne Aussetzer werden
  still nach 0,5 s wiederholt, Befehle sofort ein zweites Mal gesendet. Erst nach 3 Fehlschlägen
  in Folge gilt der Speaker als nicht erreichbar.
- Nach wiederholten Fehlern gilt die Lautstärke als unbekannt (Anzeige „–“, Drehen gesperrt), bis
  GetVolume wieder eine Antwort liefert. So zeigt das Display nie einen Wert an, den der Speaker nicht hat.

## Designentscheidungen

| Entscheidung | Begründung |
|---|---|
| PlatformIO + Arduino statt ESP-IDF | Einfacher Einstieg, Makerfabs-Beispiele passen direkt, ESP-IDF-Funktionen bleiben erreichbar |
| Arduino_GFX 1.3.8 fest gepinnt | Mit genau dieser Version hat Makerfabs das Panel getestet. Neuere Versionen haben die API der RGB-Panels geändert. |
| LVGL 8.3 statt 9.x | Stabil, gut dokumentiert, Makerfabs nutzt 8.3. Ein Umstieg ist später möglich. |
| Ein Thread für die UI | LVGL ist nicht threadsicher. Alle `lv_*`-Aufrufe laufen in `loop()`, das Netzwerk bekommt später eine eigene Task mit Nachrichten-Queue. |
| WLAN fest im Code (`secrets.h`) | Wunsch des Projekts: einfach und ohne Setup-Portal. Die Datei ist per `.gitignore` geschützt. |
| Schrift Inter (`src/fonts/`) | Klar und sehr gut lesbar, besonders bei Zahlen. Die eingebauten LVGL-Schriften haben nur ASCII. Die eigenen enthalten Latin-1 (Umlaute, ß) und die LVGL-Symbole. Medium für Text, SemiBold für Überschriften, eine 96-px-Ziffernschrift für die Lautstärke. Neu erzeugen mit `tools/gen_fonts.sh`. |
| Zwei Firmware-Varianten | `matouch` (Fernbedienung) und `matouch_hwtest` (Hardware-Test) teilen sich `hal` und `app_core`. So bleibt der Hardware-Test jederzeit verfügbar. |
| C++17 | Für `constexpr`/`inline`-Member und bessere Typsicherheit. Wird in `platformio.ini` gesetzt. |

## Sonos-Schnittstelle (Ausblick)

| Dienst | Pfad (Port 1400) | Verwendet für |
|---|---|---|
| RenderingControl | `/MediaRenderer/RenderingControl/Control` | Lautstärke eines Speakers |
| GroupRenderingControl | `/MediaRenderer/GroupRenderingControl/Control` | Gruppenlautstärke |
| AVTransport | `/MediaRenderer/AVTransport/Control` | Play, Pause, Next, Seek, Now Playing, Favorit abspielen |
| ZoneGroupTopology | `/ZoneGroupTopology/Control` | Räume, Gruppen, Koordinator |
| ContentDirectory | `/MediaServer/ContentDirectory/Control` | Favoriten (`FV:2`) |

Transportbefehle gehen immer an den **Koordinator** der Gruppe.
