# MaTouchSonos – Entwicklungs- und Testplan

Sonos-Fernbedienung auf dem **Makerfabs MaTouch ESP32-S3 Rotary IPS 2.1" (ST7701)**.
Das Projekt ist in **vertikale Schritte (Slices)** geschnitten: Jeder Schritt liefert eine
Funktion, die man am Gerät **tatsächlich benutzen** kann – vom Drehring über Logik und Netzwerk
bis zum Sonos-Speaker und zurück aufs Display. Kein Schritt baut nur eine „Schicht“.

> **Stand:** Schritt 0 umgesetzt, wartet auf den Geräte-Test (T4).
> Wie getestet wird, steht ausführlich in [TESTEN.md](TESTEN.md).

---

## 1. Rahmenbedingungen

| Thema | Entscheidung |
|---|---|
| Hardware | ESP32-S3, 16 MB Flash, 8 MB OPI-PSRAM, 480×480 rund, ST7701S (RGB565), Touch CST826, Drehring mit Taster |
| Toolchain | PlatformIO, Arduino-Framework, LVGL 8.3 |
| Sonos-Anbindung | Direkt lokal: SSDP-Discovery + UPnP/SOAP über HTTP auf Port 1400, keine Cloud |
| Setup | 5+ Speaker, Räume und Gruppen, Sonos **S2** |
| WLAN | Fest im Code: `include/secrets.h` (in `.gitignore`), Vorlage `secrets.example.h` |
| Bedienung | Ring = Lautstärke · kurz drücken = Play/Pause · lang drücken = Ringmenü (Raum, Favoriten, Scrub) · Wischen = Titel vor/zurück |
| Scrubbing | Fortschrittsbogen am Rand; im Scrub-Modus bewegt der Ring die Position, Drücken springt dorthin |

## 2. Architektur (damit jeder Schritt testbar bleibt)

```
src/
  main.cpp              Setup + Hauptschleife
lib/
  hal/                  Display (ST7701 + LVGL-Treiber), Touch, Encoder, Backlight   [nur Hardware]
  net/                  WLAN, HTTP-Client, SSDP                                     [Hardware/Netz]
  sonos_core/           SOAP-Envelopes bauen, XML/DIDL parsen, Zeitformate,         [reines C++,
                        Topologie auswerten, Lautstärke-Drossel                       native testbar]
  app_core/             Zustandsmaschine der Bedienung (Modi, Menü, Scrub-Logik)    [reines C++,
                                                                                     native testbar]
  ui/                   LVGL-Screens: Lautstärkebogen, Now Playing, Ringmenü        [LVGL]
test/
  test_sonos_core/      Unit-Tests mit echten XML-Antworten als Fixtures
  test_app_core/        Unit-Tests der Bedienlogik
  fixtures/             Aufgezeichnete Sonos-Antworten (anonymisiert)
tools/
  sonos_probe.py        Sonos-Befehle vom PC aus testen und Antworten mitschneiden
```

**Grundregel:** Alles, was kein Hardware- oder Netzwerkzugriff braucht (Parser, Zustandsmaschine,
Umrechnungen), liegt in `*_core` **ohne Arduino-Abhängigkeit** und wird mit
`pio test -e native` auf dem PC getestet.

## 3. Teststrategie – vier Ebenen

| Ebene | Was | Wer / wo | Wann |
|---|---|---|---|
| **T1 Unit-Tests** | `sonos_core`, `app_core` mit Unity, `pio test -e native` | Claude, automatisch in CI | Bei jedem Commit |
| **T2 Build** | `pio run -e matouch` (Firmware kompiliert, Größe von Flash/RAM im Rahmen) | Claude, GitHub Actions | Bei jedem Commit |
| **T3 Protokoll-Test** | `tools/sonos_probe.py` schickt dieselben SOAP-Befehle vom PC an deine Speaker; die Antworten werden als Fixtures für T1 gespeichert | Du, im Heimnetz | Einmal pro Schritt mit neuen Sonos-Befehlen |
| **T4 Geräte-Test** | Checkliste am echten Board + serielles Log (`pio device monitor`) | Du, nach dem Flashen | Am Ende jedes Schritts |

**Ablauf pro Schritt:**
1. Claude implementiert auf einem Branch `slice-N-…`, inkl. Unit-Tests; CI muss grün sein.
2. Falls neue Sonos-Befehle dazukommen: Du führst `sonos_probe.py` aus und schickst die Ausgabe (T3).
3. Du flashst (`pio run -e matouch -t upload`) und gehst die T4-Checkliste durch.
4. Rückmeldung: Checkliste abgehakt + Log bei Fehlern. Erst dann Merge nach `main` und nächster Schritt.

**Definition of Done (für jeden Schritt):**
- [ ] Alle Akzeptanzkriterien des Schritts erfüllt (T4 abgehakt)
- [ ] Unit-Tests für neue Logik vorhanden und grün (T1)
- [ ] Firmware baut in CI (T2)
- [ ] Keine Abstürze/Reboots in 10 Minuten Dauerbetrieb (Log prüfen)
- [ ] README/Plan aktualisiert, falls sich etwas geändert hat

---

## 4. Die Schritte

### Schritt 0 – Walking Skeleton: Board lebt

**Ziel:** Die Hardware ist komplett ansteuerbar. Noch kein Sonos.

**Sichtbar am Gerät:** Runder Testbildschirm; ein Bogen folgt dem Drehring, ein Punkt folgt dem Finger,
beim Drücken blinkt ein Kreis.

**Entwicklung**
- Repo-Grundgerüst: `platformio.ini` (Env `matouch` + Env `native`), `.gitignore`, `secrets.example.h`, README
- ST7701-Initialisierung (3-Wire-SPI) + RGB-Panel-Treiber; Pinbelegung und Init-Sequenz aus dem
  Makerfabs-Beispiel `fw_test` übernehmen
- LVGL 8.3 mit Framebuffer im PSRAM, Double Buffering
- Touch-Treiber CST826 (I2C) an LVGL anbinden
- Encoder per Interrupt mit Quadratur-Decoder, Taster mit Entprellung; Ereignisse: `Dreh(+/-n)`, `Kurz`, `Lang`
- GitHub Actions: `pio run -e matouch` und `pio test -e native`

**Tests**
- T1: Quadratur-Decoder (inkl. Prellen), Rastungs-Zählung, Tasten-Erkennung (Entprellung, Kurz/Lang ab 600 ms, millis()-Überlauf) in `app_core`
- T2: CI baut
- T4 Checkliste:
  - [ ] Display zeigt Testbild, keine Farbverschiebung (Rot/Blau vertauscht?), rund zentriert
  - [ ] Bogen folgt dem Ring in beide Richtungen, kein Springen/Doppelzählen
  - [ ] Kurz- und Langdruck werden korrekt erkannt (Log: `BTN short` / `BTN long`)
  - [ ] Touch-Punkt liegt unter dem Finger (auch am Rand, Achsen nicht gespiegelt)
  - [ ] Log zeigt `PSRAM: 8 MB`
  - [ ] FPS-Anzeige unten am Rand zeigt beim Drehen ≥ 25 FPS

**Risiko:** ST7701-Timing/Init. → Fallback: Makerfabs-Beispiel unverändert als Treiber-Basis nehmen.

---

### Schritt 1 – Lautstärke eines festen Speakers mit dem Ring regeln

**Ziel:** Der erste echte Durchstich: Ring → WLAN → Sonos → Display.

**Sichtbar am Gerät:** Beim Start wird die aktuelle Lautstärke des in `secrets.h` eingetragenen
Speakers als Bogen gezeigt. Drehen ändert die Lautstärke hörbar; die Zahl steht groß in der Mitte.

**Entwicklung**
- WLAN-Verbindung mit Status-Anzeige (verbinde… / verbunden / Fehler)
- `secrets.h`: `WIFI_SSID`, `WIFI_PASS`, `SONOS_IP` (vorläufig, fällt in Schritt 5 weg)
- `sonos_core`: SOAP-Envelope-Builder; Parser für `GetVolume`
- RenderingControl: `GetVolume`, `SetVolume` (Port 1400, `/MediaRenderer/RenderingControl/Control`)
- **Lautstärke-Drossel:** UI reagiert sofort (optimistisch), gesendet wird höchstens alle 150 ms
  der jeweils letzte Wert; Beschleunigung bei schnellem Drehen (1 → 2 → 4 Schritte)
- Netzwerkaufrufe in eigener FreeRTOS-Task, damit die UI nie hängt

**Tests**
- T1: SOAP-Envelope exakt wie erwartet; `GetVolume`-Antwort parsen (Fixture); Drossel-Logik
  (viele schnelle Ereignisse → wenige Sendungen, letzter Wert gewinnt); Grenzen 0–100
- T3: `sonos_probe.py volume get|set <ip> <wert>` → Antwort als Fixture speichern
- T4 Checkliste:
  - [ ] Startwert stimmt mit der Sonos-App überein
  - [ ] Drehen ändert die Lautstärke hörbar, Verzögerung < 0,5 s
  - [ ] Schnelles Drehen: kein Stottern, Endwert stimmt mit der App überein
  - [ ] Lautstärke 0 und 100 sind erreichbar, nicht darüber hinaus
  - [ ] WLAN-Router aus/an → Gerät verbindet sich neu, Anzeige zeigt den Zustand
  - [ ] Speaker vom Strom → verständliche Fehlermeldung statt Absturz

---

### Schritt 2 – Play/Pause mit Statusanzeige

**Ziel:** Kurz drücken startet/pausiert; das Display zeigt den echten Zustand.

**Sichtbar am Gerät:** Play-/Pause-Symbol in der Mitte; wird die Musik in der Sonos-App pausiert,
ändert sich das Symbol am Gerät innerhalb von ~2 s.

**Entwicklung**
- AVTransport: `Play`, `Pause`, `GetTransportInfo`
- Polling-Schleife (alle 1–2 s) für Transportzustand und Lautstärke (Änderungen aus der App übernehmen)
- Konflikt-Regel: Während der Nutzer dreht, gewinnt der lokale Wert; Polling-Werte werden
  erst 1 s nach der letzten Drehung übernommen
- `app_core`: erste Zustandsmaschine `Idle ↔ Playing ↔ Paused ↔ Error`

**Tests**
- T1: `GetTransportInfo` parsen (PLAYING, PAUSED_PLAYBACK, STOPPED, TRANSITIONING);
  Konflikt-Regel Polling vs. lokale Eingabe
- T3: Probe `transport play|pause|info`
- T4 Checkliste:
  - [ ] Kurz drücken startet/pausiert zuverlässig (10× hintereinander)
  - [ ] Pause in der Sonos-App → Symbol am Gerät folgt
  - [ ] Lautstärke in der App ändern → Bogen am Gerät folgt, ohne gegen den Ring zu „kämpfen“
  - [ ] Langdruck löst **nicht** Play/Pause aus

---

### Schritt 3 – Now Playing (Text + Fortschrittsbogen) und Titel wechseln

**Ziel:** Man sieht, was läuft, und kann per Wischen Titel wechseln.

**Sichtbar am Gerät:** Titel und Interpret (bei Radio: Sendername + aktueller Titel), außen ein
Fortschrittsbogen, der mitläuft. Wischen nach links/rechts = nächster/vorheriger Titel.
Der Lautstärkebogen erscheint nur noch beim Drehen und blendet nach 2 s aus.

**Entwicklung**
- AVTransport: `GetPositionInfo`, `GetMediaInfo`, `Next`, `Previous`
- `sonos_core`: DIDL-Lite-Metadaten parsen (Titel, Interpret, Album, `albumArtURI`),
  XML-Entities dekodieren, Zeitformat `H:MM:SS` ↔ Sekunden
- Radio-Streams erkennen (keine Dauer → Bogen ausgeblendet, `streamContent` als Titel)
- Fortschritt lokal interpolieren, per Polling korrigieren
- Lange Texte: Laufschrift, Schrift mit Umlauten (UTF-8, eigene LVGL-Schrift mit ÄÖÜß)
- Wisch-Gesten via LVGL-Gesture-Events

**Tests**
- T1: DIDL-Parser mit Fixtures für **Spotify, Radio (TuneIn/Sonos Radio), Bibliothek, TV/Line-In**;
  Sonderzeichen/Emoji; Zeitformat-Umrechnung
- T3: Probe `nowplaying` bei verschiedenen Quellen → Fixtures
- T4 Checkliste:
  - [ ] Titel/Interpret stimmen mit der App überein (Spotify, Radio, mindestens eine weitere Quelle)
  - [ ] Umlaute korrekt, lange Titel lesbar (Laufschrift, nichts vom runden Rand abgeschnitten)
  - [ ] Fortschrittsbogen läuft flüssig, springt nach Titelwechsel auf 0
  - [ ] Wischen links/rechts wechselt den Titel; bei Radio passiert nichts Schlimmes
  - [ ] TV/Line-In zeigt eine sinnvolle Anzeige

---

### Schritt 4 – Ringmenü (Grundgerüst) + Scrubbing

**Ziel:** Langdruck öffnet das Ringmenü; erster Eintrag ist **Scrub**.

**Sichtbar am Gerät:** Langdruck → Menüpunkte liegen im Kreis; der Ring bewegt die Auswahl,
Drücken bestätigt. Im Scrub-Modus leuchtet der Fortschrittsbogen auf, ein Marker folgt dem Ring,
die Zielzeit steht in der Mitte; Drücken springt dorthin, Langdruck bricht ab.

**Entwicklung**
- `app_core`: Modi `Normal`, `Menü`, `Scrub` inkl. Timeout (10 s ohne Eingabe → zurück zu Normal)
- UI: kreisförmiges Menü (Einträge auf dem Rand, Auswahl hervorgehoben), später erweiterbar
- AVTransport: `Seek` (Unit `REL_TIME`)
- Scrub-Schrittweite abhängig von Titellänge (z. B. 1 Rastung = 1 % bzw. mind. 5 s)
- Scrub bei Radio/Line-In deaktiviert (Menüpunkt ausgegraut)

**Tests**
- T1: Zustandsmaschine (alle Übergänge inkl. Timeout und Abbruch); Scrub-Schrittweite;
  Seek-Envelope; Scrub nicht verfügbar bei Streams
- T4 Checkliste:
  - [ ] Langdruck öffnet Menü, Ring wählt, Drücken bestätigt, Langdruck/Timeout schließt
  - [ ] Scrub springt an die gewählte Stelle (±2 s)
  - [ ] Während Scrub ändert der Ring **nicht** die Lautstärke
  - [ ] Bei Radio ist Scrub ausgegraut
  - [ ] Bedienung fühlt sich ohne Anleitung verständlich an (Eindruck notieren)

---

### Schritt 5 – Räume finden und wählen

**Ziel:** Keine feste IP mehr. Alle Räume werden gefunden und sind im Ringmenü wählbar.

**Sichtbar am Gerät:** Menüpunkt **Raum** zeigt alle Räume (Gruppen als „Küche + 2“); die Auswahl
wechselt Now Playing und Lautstärke auf diesen Raum. Nach Neustart ist der letzte Raum wieder aktiv.

**Entwicklung**
- SSDP-Discovery (M-SEARCH `urn:schemas-upnp-org:device:ZonePlayer:1`)
- ZoneGroupTopology: `GetZoneGroupState` → Räume, Gruppen, Koordinator, unsichtbare Geräte
  (Sub, Surrounds, Boost) ausfiltern
- Transportbefehle immer an den **Gruppen-Koordinator**
- Letzten Raum (UUID, nicht IP) in NVS speichern; IP bei Bedarf neu auflösen
- `SONOS_IP` aus `secrets.h` entfernen (optional als Fallback behalten)

**Tests**
- T1: Topologie-Parser mit Fixture **deines** Setups (5+ Speaker, mit Gruppe, mit Stereopaar/Sub);
  Koordinator-Ermittlung; Filterung unsichtbarer Geräte; Sortierung
- T3: Probe `discover` und `topology` → Fixture
- T4 Checkliste:
  - [ ] Alle Räume erscheinen, keine doppelten, keine Sub/Surround-Einträge
  - [ ] Gruppen werden als Gruppe angezeigt
  - [ ] Raumwechsel wechselt Now Playing + Lautstärke korrekt
  - [ ] Neustart → letzter Raum aktiv
  - [ ] Speaker bekommt neue IP (Router-Neustart) → wird wiedergefunden

---

### Schritt 6 – Albumcover

**Ziel:** Now Playing zeigt das Cover als runden Hintergrund.

**Sichtbar am Gerät:** Cover füllt den Kreis (abgedunkelt, damit Text lesbar bleibt);
Wechsel mit kurzer Überblendung; Platzhalter, wenn es kein Cover gibt.

**Entwicklung**
- Cover über den Sonos-Proxy laden (`http://<speaker>:1400/getaa?…`) – vermeidet HTTPS auf dem ESP32
- JPEG-Dekodierung (JPEGDEC) direkt in PSRAM-Puffer, skaliert auf 480×480, runde Maske
- Abdunkelung/Verlauf für Lesbarkeit
- Kleiner Cache (letzte 3 Cover), Laden im Hintergrund – UI bleibt bedienbar
- Größen-/Zeitlimit; bei Fehler Platzhalter

**Tests**
- T1: URL-Aufbau (relativ/absolut, Escaping); Cache-Logik (LRU)
- T2: Speicherbudget im Build-Report prüfen
- T4 Checkliste:
  - [ ] Cover erscheint bei Spotify und Bibliothek, Radio zeigt Senderlogo oder Platzhalter
  - [ ] Text bleibt auf hellen Covern lesbar
  - [ ] Ring/Touch reagieren auch während ein Cover lädt
  - [ ] 30 Titelwechsel hintereinander → kein Absturz, freier Heap stabil (Log)

---

### Schritt 7 – Favoriten und Radio starten

**Ziel:** Sonos-Favoriten aus dem Ringmenü abspielen.

**Sichtbar am Gerät:** Menüpunkt **Favoriten** → Liste, Ring blättert, Drücken spielt ab.

**Entwicklung**
- ContentDirectory: `Browse` mit ObjectID `FV:2` (Sonos-Favoriten)
- Abspielen: `SetAVTransportURI` mit den mitgelieferten Metadaten (`res` + `resMD`), dann `Play`;
  Container (Playlists/Alben) über Queue: `RemoveAllTracksFromQueue`, `AddURIToQueue`, Queue abspielen
- Liste cachen, beim Öffnen im Hintergrund aktualisieren
- Optional: Favoriten-Cover als kleine Icons

**Tests**
- T1: Browse-Antwort parsen (Radio, Playlist, Album, Einzeltitel); Entscheidung Direkt-URI vs. Queue
- T3: Probe `favorites list|play <n>` – **wichtig**, weil Favoriten je nach Dienst unterschiedlich sind
- T4 Checkliste:
  - [ ] Liste stimmt mit der Sonos-App überein
  - [ ] Je ein Radiosender, eine Playlist und ein Album starten korrekt
  - [ ] Now Playing aktualisiert sich nach dem Start

---

### Schritt 8 – Gruppen verwalten und Gruppenlautstärke

**Ziel:** Räume zur aktuellen Gruppe hinzufügen/entfernen; Ring regelt die ganze Gruppe.

**Sichtbar am Gerät:** Im Raum-Menü Häkchen pro Raum; bei einer Gruppe regelt der Ring die
Gruppenlautstärke, im Menü lassen sich einzelne Räume nachjustieren.

**Entwicklung**
- Beitreten: `SetAVTransportURI` mit `x-rincon:<Koordinator-UUID>` am beitretenden Speaker
- Verlassen: `BecomeCoordinatorOfStandaloneGroup`
- GroupRenderingControl: `GetGroupVolume`, `SetGroupVolume`, `SetRelativeGroupVolume`
- Topologie nach Änderungen neu laden

**Tests**
- T1: Envelopes für Join/Leave; Gruppenlautstärke-Drossel (Wiederverwendung aus Schritt 1)
- T4 Checkliste:
  - [ ] Raum hinzufügen/entfernen funktioniert und stimmt mit der App überein
  - [ ] Gruppenlautstärke ändert alle Räume proportional
  - [ ] Koordinator verlässt Gruppe → Gerät folgt dem neuen Koordinator

---

### Schritt 9 – Live-Updates, Energiesparen, Feinschliff

**Ziel:** Alltagstauglich: schnell, stromsparend, robust.

**Entwicklung**
- UPnP-Events (GENA `SUBSCRIBE` auf AVTransport/RenderingControl) statt Dauer-Polling;
  Polling nur noch als Fallback; Abo-Erneuerung
- Display dimmen nach 30 s, aus nach 2 min; Aufwachen per Ring/Touch/Taste
  (erste Eingabe weckt nur, ändert nichts)
- Fehlerbilder: WLAN weg, Speaker weg, Gruppe aufgelöst – jeweils klare Anzeige + automatische Erholung
- Animationen/Übergänge glätten, Farben aus dem Cover ableiten (optional)
- Optional: OTA-Update über WLAN

**Tests**
- T1: GENA-Event-Parser (LastChange-XML, doppelt kodiert); Dimm-/Weck-Logik
- T4 Checkliste:
  - [ ] Änderungen in der App erscheinen < 1 s am Gerät
  - [ ] Dimmen/Aus/Aufwachen funktioniert, Aufwach-Dreh ändert nicht die Lautstärke
  - [ ] **24-h-Dauertest:** Gerät läuft, reagiert danach sofort, kein Reboot im Log
  - [ ] Router-Neustart, Speaker-Neustart, Gruppenänderung während des Betriebs → Erholung ohne Eingriff

---

## 5. Übersicht

| # | Schritt | Neue Sonos-Befehle | Braucht T3? | Ergebnis für dich |
|---|---|---|---|---|
| 0 | Board lebt | – | nein | Hardware verifiziert |
| 1 | Lautstärke fester Speaker | Get/SetVolume | ja | Erste echte Fernbedienung |
| 2 | Play/Pause + Status | Play, Pause, GetTransportInfo | ja | Grundbedienung komplett |
| 3 | Now Playing + Wischen | GetPositionInfo, GetMediaInfo, Next, Previous | ja | Man sieht, was läuft |
| 4 | Ringmenü + Scrub | Seek | nein | Spulen |
| 5 | Räume | SSDP, GetZoneGroupState | ja | Alle Räume, keine feste IP |
| 6 | Albumcover | /getaa | nein | Schön |
| 7 | Favoriten | Browse FV:2, SetAVTransportURI, Queue | ja | Musik starten ohne Handy |
| 8 | Gruppen | x-rincon, GroupRenderingControl | ja | Multiroom |
| 9 | Live-Updates + Feinschliff | GENA Subscribe | nein | Alltagstauglich |

Nach **Schritt 3** ist das Gerät bereits für einen Raum alltagstauglich; nach **Schritt 5** für alle Räume.
Die Reihenfolge von 6–9 kann nach Wunsch getauscht werden.

## 6. Offene Punkte / Risiken

- **ST7701-Treiber:** Makerfabs nutzt eine angepasste LVGL 8.3.2. Schritt 0 klärt, ob wir deren Treiber
  übernehmen oder Arduino_GFX/esp_lcd_panel_rgb direkt nutzen.
- **Speicher:** Framebuffer (2 × 460 KB) + Cover (460 KB) + LVGL im PSRAM – passt in 8 MB, muss aber
  in Schritt 6 gemessen werden.
- **Favoriten-Vielfalt:** Je nach Musikdienst unterschiedliche Metadaten – deshalb T3 mit deinen echten Favoriten.
- **Sonos S2:** Die lokale UPnP-API (Port 1400) ist auch unter S2 verfügbar. Sonos hat sie nie offiziell
  dokumentiert – Firmware-Updates könnten Details ändern. Deshalb landen echte Antworten als Test-Fixtures im Repo.
- **Firmware-Build nur in CI:** Die Entwicklungsumgebung von Claude kann die PlatformIO-Registry nicht erreichen.
  Unit-Tests laufen lokal, die Firmware wird von GitHub Actions gebaut (T2).
