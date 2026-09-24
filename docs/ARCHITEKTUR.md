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
│ src/            main.cpp: Start, Hauptschleife            │
│                 Screens (Schritt 0: TestScreen)           │
├──────────────────────────────────────────────────────────┤
│ lib/ui/         LVGL-Bildschirme (ab Schritt 1)           │
├───────────────────────────────┬──────────────────────────┤
│ lib/app_core/   Bedienlogik   │ lib/sonos_core/          │
│  QuadratureDecoder            │  SOAP-Envelopes, XML-    │
│  DetentAccumulator            │  und DIDL-Parser,        │
│  ButtonDetector               │  Topologie (ab Schritt 1)│
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
Drehring ──Interrupt──▶ QuadratureDecoder ──Rohschritte (atomic)──▶ loop()
                                                                     │
                                          DetentAccumulator ◀────────┘
                                                 │ Rastungen
Taste ──Pegel──▶ ButtonDetector ──Short/Long──▶ Screen ──▶ LVGL ──▶ Display
Touch ◀──I2C-Polling── LVGL-Eingabetreiber ────────────────▲
```

- Der **Quadratur-Decoder** wertet beide Encoder-Signale per Übergangstabelle aus. Ungültige
  Sprünge durch Kontaktprellen werden verworfen, eine zeitliche Entprellung ist nicht nötig.
- Der **DetentAccumulator** meldet erst volle Rastungen. Ein angefangener und wieder
  zurückgedrehter Schritt zählt nicht.
- Der **ButtonDetector** meldet `Long` schon beim Erreichen von 600 ms, nicht erst beim
  Loslassen. So öffnet sich das Menü, während der Finger noch drückt.

## Designentscheidungen

| Entscheidung | Begründung |
|---|---|
| PlatformIO + Arduino statt ESP-IDF | Einfacher Einstieg, Makerfabs-Beispiele passen direkt, ESP-IDF-Funktionen bleiben erreichbar |
| Arduino_GFX 1.3.8 fest gepinnt | Mit genau dieser Version hat Makerfabs das Panel getestet. Neuere Versionen haben die API der RGB-Panels geändert. |
| LVGL 8.3 statt 9.x | Stabil, gut dokumentiert, Makerfabs nutzt 8.3. Ein Umstieg ist später möglich. |
| Ein Thread für die UI | LVGL ist nicht threadsicher. Alle `lv_*`-Aufrufe laufen in `loop()`, das Netzwerk bekommt später eine eigene Task mit Nachrichten-Queue. |
| WLAN fest im Code (`secrets.h`) | Wunsch des Projekts: einfach und ohne Setup-Portal. Die Datei ist per `.gitignore` geschützt. |
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
