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
│                 VolumeScreen, TestScreen, fonts/          │
├──────────────────────────────────────────────────────────┤
│ (lib/ui/        Bildschirme wandern hierher, sobald es    │
│                 mehrere gibt – ab Schritt 3)              │
├───────────────────────────────┬──────────────────────────┤
│ lib/app_core/   Bedienlogik   │ lib/sonos_core/          │
│  QuadratureDecoder            │  SOAP-Envelopes, XML-    │
│  RotaryDetentDecoder          │  und DIDL-Parser,        │
│  ButtonDetector               │  RenderingControl        │
│  VolumeController             │  (Topologie ab Schritt 5)│
│  Zustandsmaschine (ab 2/4)    │                          │
│        ── reines C++, auf dem PC getestet ──             │
├───────────────────────────────┴──────────────────────────┤
│ lib/net/        WLAN, HTTP, SSDP (ab Schritt 1/5)         │
│ lib/hal/        Display+LVGL, Touch, Drehring/Taste       │
├──────────────────────────────────────────────────────────┤
│ Arduino-ESP32 2.0.x · Arduino_GFX 1.3.8 · LVGL 8.3.11     │
└──────────────────────────────────────────────────────────┘
```

Abhängigkeiten zeigen nur nach unten. `app_core` und `sonos_core` kennen weder Arduino noch LVGL.

## Datenfluss der Eingaben (Schritt 0)

```
Drehring ──Interrupt──▶ RotaryDetentDecoder ──Rastungen (atomic)──▶ loop() ──▶ Screen
                        (QuadratureDecoder +                                    │
                         Ruhelagen-Synchronisation)                             ▼
Taste ──Pegel──▶ ButtonDetector ──Short/Long──▶ Screen ──────────────────▶ LVGL ──▶ Display
Touch ◀──I2C-Polling── LVGL-Eingabetreiber ───────────────────────────────────▲
```

- Der **QuadratureDecoder** wertet beide Encoder-Signale per Übergangstabelle aus. Ungültige
  Sprünge durch Kontaktprellen werden verworfen, eine zeitliche Entprellung ist nicht nötig.
- Der **RotaryDetentDecoder** summiert die Rohschritte und wertet erst aus, wenn der Encoder
  wieder in seiner **Ruhelage** steht (A = B = 1). Dort meldet er eine Rastung, wenn mehr als
  die halbe Strecke zurückgelegt wurde, und setzt die Summe auf 0.
  *Warum so?* Ein reiner Zähler „alle 4 Rohschritte eine Rastung“ verschiebt sich dauerhaft um
  einen halben Klick, sobald durch Prellen ein Schritt verloren geht. In eine Richtung merkt man
  das nicht, aber beim Hin-und-her-Drehen kommt dann kein Klick mehr an. Genau das hat der
  Geräte-Test von Schritt 0 gezeigt. Die Synchronisation in der Ruhelage schließt diesen Fehler aus.
- Der **ButtonDetector** meldet `Long` schon beim Erreichen von 600 ms, nicht erst beim
  Loslassen. So öffnet sich das Menü, während der Finger noch drückt.

## Nebenläufigkeit (ab Schritt 1)

```
Kern 1: loop()                                   Kern 0: SonosLink-Task
─────────────────                                ─────────────────────
Drehring → VolumeController                      WLAN aufbauen / überwachen
   │  (Anzeige sofort, Drossel 150 ms)           GetVolume nach Start/Fehler
   └─ setVolume(v) ──▶ [Queue, Länge 1] ──▶ SetVolume an Speaker (HTTP, Port 1400)
                        (neuester Wert gewinnt)
Anzeige ◀── pollEvent() ◀── [Event-Queue] ◀── WLAN-/Speaker-Zustand, Lautstärke, Fehler
```

- LVGL wird nur von Kern 1 aus angefasst.
- Die UI wartet nie auf das Netz. Ein HTTP-Timeout (bis 2 s) bremst nur die Netzwerk-Task.
- Die Queue für die Lautstärke hat die Länge 1 und wird überschrieben. Beim schnellen Drehen
  sammeln sich deshalb keine veralteten Befehle an.
- Nach jedem Fehler gilt die Lautstärke als unbekannt (Anzeige „–“, Drehen gesperrt), bis
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
