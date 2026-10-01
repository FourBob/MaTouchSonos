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
│  ButtonDetector               │  RenderingControl, Group…│
│  VolumeController             │  AVTransport             │
│  PlaybackController           │  NowPlaying (DIDL-Lite)  │
│  ProgressTracker              │  Topology + SSDP         │
│  ModeController (Menü/Spulen) │  AlbumArt (Cover-Quellen)│
│  ImageOps (Zuschnitt, Dimmen) │                          │
│        ── reines C++, auf dem PC getestet ──             │
├───────────────────────────────┴──────────────────────────┤
│ lib/net/        WLAN, HTTP, SSDP, CoverLoader             │
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

| Modus   | Ring drehen          | kurz drücken         | lang drücken |
|---------|----------------------|----------------------|--------------|
| Normal  | Lautstärke           | Play/Pause           | Menü öffnen  |
| Menü    | Eintrag wählen       | Eintrag ausführen    | schließen    |
| Spulen  | Zielposition ändern  | dorthin springen     | abbrechen    |
| Raum    | Raum wählen          | Raum übernehmen      | abbrechen    |
| Favorit | Favorit wählen       | abspielen            | abbrechen    |

Menü, Spulen und Auswahllisten schließen sich nach 10 s ohne Eingabe. Raum- und Favoritenliste sind
dasselbe Drehrad (`NowPlayingScreen::showPicker`); die Liste läuft an den Enden nicht um.

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

## Anlage finden und Raum wählen (ab Schritt 5)

```
WLAN verbunden
   │
   ▼
Anlage suchen ──▶ bekannte IPs zuerst (aktiver Raum, SONOS_IP, alle zuletzt gesehenen Speaker)
   │              sonst SSDP: M-SEARCH an 239.255.255.250:1900, 1,5 s auf Antworten warten
   ▼
GetZoneGroupState (an irgendeinen Speaker) ──▶ Gruppen, Koordinatoren, Namen
   │
   ▼
Raum auflösen: beim Start SONOS_ROOM (Name) → sonst gewählte UUID (NVS) → Gruppe, in der sie
   │            steckt → deren Koordinator-IP (sonst die Gruppe von SONOS_IP, sonst die erste alphabetisch)
   ▼
Abfragen alle 1,5 s an den Koordinator · Topologie alle 30 s neu · nach 3 Fehlschlägen neu suchen
```

- **Befehle gehen immer an den Koordinator.** Tritt der gewählte Raum einer Gruppe bei, folgt die
  Fernbedienung automatisch und zeigt z. B. „Küche + 1“.
- **Lautstärke:** Einzelraum über `RenderingControl`, Gruppe über `GroupRenderingControl`
  (mit `SnapshotGroupVolume` vor dem Lesen, damit Sonos Änderungen proportional verteilt).
- **Stereopaare, Sub, Surrounds und Boost** werden ausgeblendet. Das ist an einer echten Anlage mit
  13 Geräten getestet (`test/test_sonos_core/fixtures_topology.h`, anonymisiert).
- Gemerkt wird die **UUID**, nicht die IP. IP-Wechsel nach einem Router-Neustart sind so kein Problem.

## Albumcover (ab Schritt 6)

Jeder Musikdienst liefert Cover anders. `sonos::art::candidates()` (sonos_core, getestet mit echten
Aufzeichnungen) erstellt je Titel eine **Liste von Bildadressen**, die der Reihe nach probiert werden:

| Quelle | Adresse aus den Metadaten | 1. Versuch | weitere Versuche |
|---|---|---|---|
| Warteschlange (Spotify, Apple Music, Amazon, Bibliothek …) | relativ `/getaa?…` | Bild-Proxy des Speakers (HTTP, Port 1400) | Speaker sucht Cover zur Titel-URI |
| Spotify Connect (`x-sonos-vli:`) | `https://i.scdn.co/…` | direkt per HTTPS (640 px JPEG) | – |
| Apple Music / Deezer mit absoluter URL | `…/3000x3000bb.webp`, `…/1000x1000-…` | auf ~480 px (JPEG) umgeschrieben | Originaladresse, dann Speaker-Suche |
| Radio (TuneIn u. a.) | Senderlogo aus GetMediaInfo | direkt (HTTPS, oft PNG) | – |
| TV, Line-In, leer | – | kein Cover | – |

```
SonosLink (neuer Titel) ──Kandidaten──▶ CoverLoader-Task (Kern 0, niedrige Priorität)
                                          herunterladen (HTTP/HTTPS, ≤ 700 KB, chunked-fähig)
                                          JPEG (JPEGDEC, ggf. 1/2–1/8 verkleinert) oder PNG (PNGdec)
                                          auf 480×480 zuschneiden (bilinear) und auf ~41 % abdunkeln
                                          in den freien der beiden PSRAM-Puffer
UI ◀── takeCover() / acknowledge() ──────  (Doppelpuffer: überschrieben wird erst nach Bestätigung)
```

- Das fertige Bild liegt in Displaygröße vor. Beim Zeichnen skaliert LVGL nichts, das Cover
  kostet also keine Bildrate.
- HTTPS ohne Zertifikatsprüfung: Es werden nur öffentliche Bilder geladen, keine Zugangsdaten gesendet.
- Progressive JPEGs (z. B. Amazon-Music-Cover vom Speaker, 300×300) kann JPEGDEC nur als 1/8-Vorschau
  dekodieren. Bis 640×640 übernimmt deshalb **stb_image** (`lib/net/src/third_party`, public domain/MIT,
  nur JPEG, Puffer im PSRAM, ~10 Byte je Pixel); größere progressive Bilder bleiben bei der Vorschau.
- Grenzen: WebP und GIF werden nicht unterstützt. Deshalb gibt es mehrere Kandidaten, und
  `sonos_probe.py cover` zeigt vorab, was ein Dienst liefert.
- Tempo: Je eine HTTP- und HTTPS-Verbindung bleibt bis zu 60 s offen und wird für das nächste Cover
  vom selben Server wiederverwendet (spart bei HTTPS den TLS-Handshake). Eine offene Verbindung zu
  einem anderen Server wird vorher geschlossen; Weiterleitungen verfolgt der Lader deshalb selbst.
  Skalieren und Abdunkeln laufen in einem Durchlauf ohne 64-Bit-Divisionen.

## Favoriten (ab Schritt 7)

```
nach dem Verbinden / beim Öffnen des Menüs (max. alle 10 s):
  Browse FV:2 (seitenweise) ──▶ je Favorit nur Name, Dienst, Position ──▶ FavoritesInfo (PSRAM) ──▶ UI

Favorit gewählt (Index + Name):
  Browse FV:2 ab Position, 1 Eintrag ──▶ Name gleich? ──▶ playMethod()
     direkt (Radio, Line-In, TV):           SetAVTransportURI(res, resMD) → Play
     Warteschlange (Playlist, Album, Titel): RemoveAllTracksFromQueue → AddURIToQueue(res, resMD)
                                              → SetAVTransportURI(x-rincon-queue:<Koordinator>#0) → Seek Titel 1 → Play
     Ordner-Verknüpfung (ohne res, z. B. Pocket Casts „In Progress“):
                                              wie Warteschlange, aber AddURIToQueue(x-rincon-cpcontainer:<ID>?sid=…&sn=…)
                                              – sid aus der Dienst-Kennung (SA_RINCON<sid·256+7>), sn durchprobiert und gemerkt
```

- `resMD` enthält das Anmelde-Token des Dienstes und geht unverändert zurück an den Speaker.
- Alle Befehle gehen an den Koordinator des aktiven Raums, der Favorit spielt also in der ganzen Gruppe.
- Playlists und Alben ersetzen die Warteschlange (wie „Jetzt abspielen“ in der Sonos-App).

## Energiesparen (ab Schritt 9)

`app::IdleController` (app_core, getestet) entscheidet die Helligkeit, `hal::Display::setBacklight()` setzt sie
per PWM (LEDC, 20 kHz).

| Zustand | wann | Helligkeit |
|---|---|---|
| hell | Eingabe in den letzten 30 s | 255 |
| gedimmt | 30 s ohne Eingabe, oder Musik läuft | 40 |
| aus | 2 min ohne Eingabe **und** nichts spielt | 0 |

Eine Eingabe bei ausgeschaltetem Display weckt nur. Sie und alles in den folgenden 600 ms wird verworfen,
damit der Weck-Dreh nicht die Lautstärke ändert. Bei der Taste wird der ganze Druck verworfen (auch ein
Langdruck). Gedimmt ist das Display noch sichtbar, dort werden Eingaben ausgeführt.

## Live-Updates (ab Schritt 9)

```
SonosLink-Task                                   Koordinator (Port 1400)
  SUBSCRIBE /MediaRenderer/AVTransport/Event  ─────▶   (CALLBACK <http://Gerät:3400/ev/av>)
  SUBSCRIBE …/RenderingControl/Event           ─────▶   bzw. GroupRenderingControl bei Gruppen
  SUBSCRIBE /ZoneGroupTopology/Event           ─────▶
          ◀───── NOTIFY /ev/av  (SID, LastChange: TransportState, Metadaten …)
  WiFiServer :3400 → 200 OK → „bald abfragen“ (150 ms sammeln, höchstens alle 400 ms)
```

- Ein Event ist nur das Signal, die Werte holt die bewährte Abfrage. So bleibt es einfach und robust,
  auch wenn ein Event verloren geht.
- Mit aktiven Abos: Abfrage alle 10 s statt 1,5 s, Topologie alle 2 min statt 30 s.
- Abos werden nach der Hälfte der Laufzeit (1800 s) erneuert. Schlägt das fehl, wird neu abonniert,
  sonst nach 60 s erneut versucht. Bei Raumwechsel werden die alten Abos gekündigt, bei WLAN-Verlust
  verworfen und danach neu angelegt.
- Events von alten Abos (falsche SID) werden ignoriert.
- Auswertung (`sonos::gena`, getestet): Kopf der NOTIFY-Anfrage, `LastChange` (kodiertes XML, darin
  Metadaten nochmals kodiert), Lautstärke nur Kanal `Master`.

## Easteregg: „Ringbrecher“

Breakout für das runde Display (Menüpunkt **Spiel**, später versteckbar). Der Schläger läuft außen am Rand,
die Steine liegen als 4 Ringe um die Mitte (10/14/18/22 Segmente), die Mitte zeigt Punkte und Leben.

- **Logik:** `app::game::RingBreakout` (app_core, getestet), fester Zeitschritt 1/60 s mit zwei Teilschritten,
  Abprallwinkel am Schläger je nach Trefferpunkt, Level werden schneller und der Schläger schmaler.
- **Anzeige:** `GameScreen` zeichnet **direkt über Arduino_GFX** (`hal::Display::gfx()`), nur Ball, Schläger,
  getroffene Steine und geänderte Zahlen. LVGL pausiert solange, beim Beenden wird die Oberfläche neu gezeichnet.
- **Eingabe:** Ring = Schläger (Rohschritte, 4,5° je Schritt = 18° je Rastung), Antippen = Schläger zum Finger
  (nur beim Aufsetzen und frühestens 0,6 s nach dem Drehen – beim Drehen liegen oft Finger am Glasrand),
  kurz drücken = Ball abschießen/neu starten, lang drücken = Ende. Kein Menü-Timeout, Display dimmt nicht beim Spielen.
- **Rekord:** im NVS (`game_hi`). Musik und Live-Updates laufen währenddessen normal weiter.

## Easteregg: „Boxenstopp“

Rennspiel nach dem Vorbild von *Pitstop II* (Solo), zweiter Eintrag der Spielauswahl (Menü **Spiel** → Ring
wählt, Drücken startet). 5 Runden gegen 10 Gegner auf einer Strecke mit Kurven; Sprit und Reifen verschleißen,
deshalb muss man in die Box.

- **Logik:** `app::game::RaceGame` (app_core, getestet): Strecke aus ~1000 Segmenten zu 200 Einheiten, jede mit
  Krümmung und Merkern (Startlinie, Boxenschild, Boxenspur). Fester Zeitschritt 1/60 s. Tempo, Fliehkraft in
  Kurven, Gras bremst, Auffahren auf einen Gegner kostet Tempo. Sprit reicht für gut 3 Runden; die Reifen auf der
  Kurvenaußenseite verschleißen schneller, abgefahrene Reifen kosten Grip, ein Platten kostet Tempo.
- **Box:** Rechts auf der gelben Boxenspur hinter der Startlinie halten. Dann zeigt der Bildschirm das Auto von oben,
  rundherum vier Reifen (Farbe = Zustand), Tank und LOS, im Uhrzeigersinn so angeordnet, wie man sie mit dem Ring
  erreicht. Ein Reifenwechsel dauert 1,5 s, Tanken läuft, bis man es stoppt. Die Uhr läuft weiter.
- **Anzeige:** `RaceScreen` zeichnet direkt über Arduino_GFX. Die Straße ist klassisches Pseudo-3D: Segmente von
  vorn nach hinten projiziert, Krümmung als aufsummierter Querversatz, jede Bildzeile genau einmal und nur
  innerhalb des runden Bildschirms. Himmel und Anzeigen werden nur bei Änderung gezeichnet, die Berge verschieben
  sich in Kurven.
- **Eingabe:** Ring = Lenkrad (Rohschritte, das Lenkrad stellt sich von selbst zurück), Bildschirmmitte berühren =
  Bremse, in der Box: Ring wählt, kurz drücken führt aus. Lang drücken = Ende.
- **Bestzeit:** im NVS (`race_best`). Das Spielobjekt (~9 KB Strecke) liegt im PSRAM.

## Designentscheidungen

| Entscheidung | Begründung |
|---|---|
| PlatformIO + Arduino statt ESP-IDF | Einfacher Einstieg, Makerfabs-Beispiele passen direkt, ESP-IDF-Funktionen bleiben erreichbar |
| Arduino_GFX 1.3.8 fest gepinnt | Mit genau dieser Version hat Makerfabs das Panel getestet. Neuere Versionen haben die API der RGB-Panels geändert. |
| LVGL 8.3 statt 9.x | Stabil, gut dokumentiert, Makerfabs nutzt 8.3. Ein Umstieg ist später möglich. |
| Ein Thread für die UI | LVGL ist nicht threadsicher. Alle `lv_*`-Aufrufe laufen in `loop()`, das Netzwerk bekommt später eine eigene Task mit Nachrichten-Queue. |
| WLAN fest im Code (`secrets.h`) | Wunsch des Projekts: einfach und ohne Setup-Portal. Die Datei ist per `.gitignore` geschützt. Die Speaker-IP ist seit Schritt 5 optional. |
| Schrift Inter (`src/fonts/`) | Klar und sehr gut lesbar, besonders bei Zahlen. Die eingebauten LVGL-Schriften haben nur ASCII. Die eigenen enthalten Latin-1 (Umlaute, ß) und die LVGL-Symbole. Medium für Text, SemiBold für Überschriften, eine 96-px-Ziffernschrift für die Lautstärke. Neu erzeugen mit `tools/gen_fonts.sh`. |
| Zwei Firmware-Varianten | `matouch` (Fernbedienung) und `matouch_hwtest` (Hardware-Test) teilen sich `hal` und `app_core`. So bleibt der Hardware-Test jederzeit verfügbar. |
| C++17 | Für `constexpr`/`inline`-Member und bessere Typsicherheit. Wird in `platformio.ini` gesetzt. |

## Sonos-Schnittstelle (Ausblick)

| Dienst | Pfad (Port 1400) | Verwendet für |
|---|---|---|
| RenderingControl | `/MediaRenderer/RenderingControl/Control` | Lautstärke eines Speakers |
| GroupRenderingControl | `/MediaRenderer/GroupRenderingControl/Control` | Gruppenlautstärke |
| Events (GENA) | `…/Event` der Dienste, Rückruf an Port 3400 | Live-Updates (Schritt 9) |
| AVTransport | `/MediaRenderer/AVTransport/Control` | Play, Pause, Next, Seek, Now Playing, Favorit abspielen |
| ZoneGroupTopology | `/ZoneGroupTopology/Control` | Räume, Gruppen, Koordinator |
| ContentDirectory | `/MediaServer/ContentDirectory/Control` | Favoriten (`FV:2`) |

Transportbefehle gehen immer an den **Koordinator** der Gruppe.
