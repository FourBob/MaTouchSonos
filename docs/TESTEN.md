# Testen

Jeder Schritt aus dem [Entwicklungsplan](ENTWICKLUNGSPLAN.md) wird auf vier Ebenen geprüft.
T1 und T2 laufen automatisch, T3 und T4 finden bei dir zu Hause statt, weil nur dort die Sonos-Anlage
und das Board sind.

| Ebene | Was | Wie | Wer |
|---|---|---|---|
| **T1** | Unit-Tests der Logik | `pio test -e native` | automatisch (CI) |
| **T2** | Firmware baut | `pio run -e matouch` | automatisch (CI) |
| **T3** | Sonos-Befehle vom PC aus | `tools/sonos_probe.py` (ab Schritt 1) | du, im Heimnetz |
| **T4** | Checkliste am Gerät | flashen, ausprobieren, Log ansehen | du |

---

## T1 – Unit-Tests

```bash
pio test -e native
```

Die Tests liegen in `test/`. Sie prüfen den Code in `lib/app_core` (Bedienlogik) und `lib/sonos_core`
(Sonos-Protokoll). Die Sonos-Antworten, gegen die getestet wird, stehen in
`test/test_sonos_core/fixtures.h`. Dort ist markiert, welche nachgebaut und welche an echten
Speakern aufgezeichnet sind. Aufgezeichnete Antworten aus T3 ersetzen nach und nach die nachgebauten.

## T2 – Build in der CI

Bei jedem Push baut GitHub Actions die Firmware und führt die Unit-Tests aus.
Status: Reiter **Actions** im Repo. Die fertigen Binärdateien liegen dort als Artifact `firmware-matouch`.
Gebaut werden beide Firmware-Varianten: `matouch` (Fernbedienung) und `matouch_hwtest` (Hardware-Test).

## T3 – Protokoll-Test (ab Schritt 1)

`tools/sonos_probe.py` schickt vom PC aus **byte-genau dieselben** Nachrichten an deine Speaker wie
das Board. Das sichert ein Unit-Test ab. So sehen wir getrennt vom Board, ob ein Befehl funktioniert,
und bekommen echte Antworten als Testdaten.

Voraussetzung: Python 3.8 oder neuer (auf dem Mac vorinstalliert). Der PC muss im selben Netz sein wie die Speaker.

```bash
python3 tools/sonos_probe.py <Speaker-IP> info                 # Raumname, Modell, Software
python3 tools/sonos_probe.py <Speaker-IP> volume get           # Lautstärke lesen
python3 tools/sonos_probe.py <Speaker-IP> volume set 15        # Lautstärke setzen
python3 tools/sonos_probe.py <Speaker-IP> --save probe-out volume get   # zusätzlich XML speichern
```

Mit `--save` landen Anfrage und Antwort als `.xml`-Dateien im angegebenen Ordner. Schick mir den
Inhalt, dann werden daraus Testdaten. Die Dateien enthalten keine Passwörter, höchstens Raumnamen
und IP-Adressen aus deinem Heimnetz.

## T4 – Geräte-Test

### Ablauf

1. Aktuellen Stand holen: `git pull`
2. Flashen: `pio run -e matouch -t upload` (Hardware-Test: `-e matouch_hwtest`)
3. Log öffnen: `pio device monitor -e matouch`
4. Die Checkliste des Schritts durchgehen (unten bzw. im Entwicklungsplan)
5. Rückmelden (siehe unten)

### Was ich als Rückmeldung brauche

- Die Checkliste mit ✅ / ❌ je Punkt
- Bei ❌: was du erwartet hast und was stattdessen passiert ist
- Das **serielle Log** ab dem Start (einfach komplett in den Chat kopieren)
- Bei Anzeigefehlern gern ein Handyfoto vom Display

> **Hinweis zum Log:** Beim Start des Monitors erscheint immer
> `Please build project in debug configuration to get more details about an exception.`
> Das ist nur ein Hinweis des Filters `esp32_exception_decoder` und **kein Fehler**.
> Ein echter Absturz sieht anders aus: `Guru Meditation Error`, `Backtrace:` und danach ein Neustart.

---

## Checkliste Schritt 0 – Hardware-Test ✅

> Abgeschlossen. Zum erneuten Prüfen die Firmware `matouch_hwtest` flashen:
> `pio run -e matouch_hwtest -t upload`

Nach dem Flashen erscheint der Testbildschirm:
ein grauer Ring am Rand, drei Farbbalken oben, in der Mitte der Zähler `0`.

**Start**
- [ ] Das Log beginnt mit `=== MaTouchSonos – Schritt 0: Hardware-Test ===`
- [ ] Das Log zeigt `PSRAM: 7.9 MB` oder `8.0 MB` und `Flash: 16 MB`
- [ ] Im Log steht kein `FEHLER` und keine `Touch: CST826 antwortet nicht`-Warnung

**Display**
- [ ] Das Bild ist rund zentriert, keine Streifen, kein Flackern
- [ ] Die Farbbalken sind von links nach rechts **Rot – Grün – Blau**
- [ ] Die Schrift ist scharf und gut lesbar

**Drehring**
- [ ] Eine Rastung im Uhrzeigersinn: Zähler +1, grüner Bogen wächst um ein Segment, Log `ENC +1`
- [ ] Eine Rastung gegen den Uhrzeigersinn: Zähler −1
- [ ] 24 Rastungen ergeben eine volle Runde des Bogens
- [ ] Schnell drehen: Der Zähler springt nicht zurück und verliert keine Schritte (grob prüfen: 10 Rastungen = +10)
- [ ] Halbe Rastung und zurück: Der Zähler bleibt gleich
- [ ] Hin und her, also abwechselnd ein Klick rechts und ein Klick links: Jeder Klick zählt (+1, −1, +1, …)
- [ ] Im Log steht bei langsamem Drehen `ENC +1 (raw +4)` (raw = Rohschritte pro Rastung)

**Taste**
- [ ] Kurz drücken: Der Rand blitzt **blau** auf, Anzeige „Taste: kurz“, Log `BTN short`
- [ ] Gedrückt halten: Nach ca. 0,6 s blitzt der Rand **orange**, noch **während** du hältst, „Taste: lang“, Log `BTN long`
- [ ] Nach dem Langdruck kommt beim Loslassen **kein** zusätzliches „kurz“
- [ ] 10× schnell hintereinander kurz drücken: 10× `BTN short`

**Touch**
- [ ] Ein weißer Punkt erscheint unter dem Finger und folgt ihm
- [ ] Oben, unten, links und rechts am Rand: Der Punkt liegt dort, wo der Finger ist (Achsen nicht vertauscht oder gespiegelt)
- [ ] Die Koordinaten in der Mitte sind ungefähr `240 / 240`
- [ ] Das Log zeigt beim Berühren `TOUCH down x/y` und beim Loslassen `TOUCH up x/y`

**Stabilität**
- [ ] Die FPS-Anzeige unten am Rand zeigt beim Drehen ≥ 25 FPS
- [ ] 10 Minuten laufen lassen: Die `STATUS`-Zeilen im Log kommen weiter, `heap_frei` sinkt nicht stetig, kein Neustart

### Was tun, wenn …

| Beobachtung | Mögliche Ursache / Anpassung |
|---|---|
| Rot und Blau vertauscht | R- und B-Pins in `board_config.h` tauschen. Bitte melden, dann passe ich das an. |
| Nur jede zweite Rastung zählt, Log zeigt `raw +2` | `ENCODER_HALF_STEP 1` in `board_config.h` |
| Richtung falsch | `ENCODER_INVERT` in `board_config.h` umschalten (0 ↔ 1) |
| Touch gespiegelt | melden, dann bekommt der Touch-Treiber eine Achsenumkehr |

---

## Checkliste Schritt 1 – Lautstärke eines Speakers ✅

**Vorbereitung**
1. `include/secrets.h` anlegen und ausfüllen (siehe README, Abschnitt 3): WLAN-Name, Passwort,
   IP eines Speakers.
2. **T3 zuerst:** Vom PC aus prüfen, ob der Speaker antwortet:
   ```bash
   python3 tools/sonos_probe.py <Speaker-IP> info
   python3 tools/sonos_probe.py <Speaker-IP> --save probe-out volume get
   python3 tools/sonos_probe.py <Speaker-IP> --save probe-out volume set 10
   ```
   - [ ] `info` zeigt den Raumnamen
   - [ ] `volume get` zeigt `HTTP 200 … OK` und die Lautstärke wie in der Sonos-App
   - [ ] `volume set 10` ändert die Lautstärke hörbar bzw. sichtbar in der App
   - [ ] Den Inhalt von `probe-out/` mitschicken
3. Flashen: `pio run -e matouch -t upload`, dann `pio device monitor -e matouch`

**Start**
- [ ] Auf dem Display steht „Lautstärke“, in der Mitte zunächst „–“, unten „WLAN verbinden …“
- [ ] Innerhalb weniger Sekunden: Das Log zeigt `WLAN verbunden, IP …` und `SONOS GetVolume = …`
- [ ] Die Zahl und der grüne Bogen zeigen dieselbe Lautstärke wie die Sonos-App
- [ ] Umlaute werden korrekt angezeigt („Lautstärke“)

**Drehen**
- [ ] Ein Klick nach rechts: +1, hörbar lauter, Log `ENC +1 -> Lautstärke …` und `SONOS SetVolume … ok`
- [ ] Ein Klick nach links: −1
- [ ] Die Anzeige reagiert sofort. Der Speaker folgt spürbar schnell (Ziel: unter 0,5 s)
- [ ] 10 Klicks **schnell** am Stück: Die Lautstärke steigt um **mindestens 10** (durch die Beschleunigung eher mehr).
      Im Log steht `raw` ungefähr 4 × Klicks.
- [ ] Schnell drehen: Die Werte springen in größeren Schritten (Beschleunigung), nichts stottert.
- [ ] `loop_max_ms` in den `STATUS`-Zeilen notieren (während du drehst)
      Danach zeigt die Sonos-App denselben Endwert wie das Display.
- [ ] 0 und 100 sind erreichbar, aber nicht mehr (vorsichtig mit der Lautstärke 😉)

**Robustheit**
- [ ] WLAN-Router kurz aus- und wieder einschalten: Das Display zeigt „WLAN getrennt …“, danach verbindet
      das Gerät sich selbst neu und zeigt wieder die Lautstärke
- [ ] Speaker vom Strom trennen: Nach einem Dreh oder spätestens beim nächsten Abfragen erscheint eine
      rote Fehlermeldung, kein Absturz. Speaker wieder einstecken: Nach ca. 1 Minute (Speaker-Start)
      erscheint die Lautstärke wieder
- [ ] 10 Minuten laufen lassen: `heap_frei` in den `STATUS`-Zeilen bleibt stabil, kein Neustart

**Bekannt und gewollt in Schritt 1**
- Ändert man die Lautstärke in der Sonos-App, folgt das Display noch **nicht**. Das kommt in Schritt 2.
- Die Taste hat noch keine Funktion (Log: `BTN short (Play/Pause folgt in Schritt 2)`).

---

## Checkliste Schritt 2 – Play/Pause und Abgleich mit der Sonos-App ✅

**T3 zuerst** (am Mac, Speaker-IP wie in `secrets.h`):
```bash
python3 tools/sonos_probe.py <IP> --save probe-out transport info
python3 tools/sonos_probe.py <IP> --save probe-out transport pause
python3 tools/sonos_probe.py <IP> --save probe-out transport play
```
- [ ] `transport info` zeigt `Zustand: PLAYING` bzw. `PAUSED_PLAYBACK`
- [ ] `pause` und `play` wirken hörbar
- [ ] Die Dateien `probe-out/GetTransportInfo_*.xml`, `Play_*.xml` und `Pause_*.xml` mitschicken

**Am Gerät** (`pio run -e matouch -t upload`):
- [ ] Nach dem Start steht unter der Zahl der richtige Zustand: `▶ Wiedergabe`, `❚❚ Pausiert` oder `■ Gestoppt`
- [ ] Kurz drücken bei laufender Musik: sofort `❚❚ Pausiert`, Zahl und Bogen werden grau, die Musik stoppt,
      Log `BTN short -> Pause` und `SONOS Pause ok`
- [ ] Nochmal kurz drücken: `▶ Wiedergabe`, die Musik läuft weiter, Log `SONOS Play ok`
- [ ] 5× schnell hintereinander drücken: Am Ende stimmen Anzeige und Speaker überein
- [ ] **In der Sonos-App** pausieren/starten: Das Display folgt innerhalb von ca. 2 s
- [ ] **In der Sonos-App** die Lautstärke ändern: Zahl und Bogen folgen innerhalb von ca. 2 s
- [ ] Während du am Ring drehst, springt die Zahl nicht zurück
- [ ] Radiosender (falls vorhanden): Drücken pausiert bzw. stoppt, dann `■ Gestoppt` oder `❚❚ Pausiert`
- [ ] Langdruck löst **kein** Play/Pause aus (Log `BTN long (Menü folgt in Schritt 4)`)

**Fehlerfälle** (falls einfach herstellbar):
- [ ] Speaker in der Sonos-App zu einer Gruppe hinzufügen, in der ein **anderer** Speaker führt, dann drücken:
      rote Meldung „Speaker ist Teil einer Gruppe …“ für ca. 4 s, Anzeige springt auf den alten Zustand zurück
- [ ] 10 Minuten laufen lassen: `heap_frei` stabil, kein Neustart

---

## Checkliste Schritt 3 – Now Playing und Titel wechseln ✅

**T3 zuerst:** Für jede Quelle, die du nutzt, einmal in der Sonos-App starten und aufzeichnen.
Die Ordnernamen helfen mir beim Zuordnen:
```bash
python3 tools/sonos_probe.py <IP> --save probe-spotify nowplaying     # Musikdienst (Spotify o. Ä.)
python3 tools/sonos_probe.py <IP> --save probe-radio nowplaying       # Radiosender
python3 tools/sonos_probe.py <IP> --save probe-bibliothek nowplaying  # eigene Musik/NAS (falls genutzt)
python3 tools/sonos_probe.py <IP> --save probe-tv nowplaying          # TV/Line-In (falls vorhanden)
```
- [ ] Die Ausgabe zeigt jeweils sinnvolle Werte für Titel, Interpret und Position
- [ ] Die `probe-*`-Ordner mitschicken. Daraus werden echte Testdaten für alle Quellen.

**Anzeige** (`pio run -e matouch -t upload`)
- [ ] Titel, Interpret und Album stimmen mit der Sonos-App überein
- [ ] Umlaute und Sonderzeichen (&, ’, é …) werden korrekt dargestellt
- [ ] Lange Titel laufen als Laufschrift durch, nichts wird vom runden Rand abgeschnitten
- [ ] Der Fortschrittsring außen läuft flüssig, die Zeit (z. B. `1:02 / 3:45`) zählt jede Sekunde hoch
- [ ] Bei Pause bleiben Ring und Zeit stehen, der Titel wird grau
- [ ] Nach einem Titelwechsel springt der Ring auf den Anfang
- [ ] In der Sonos-App im Titel spulen: Ring und Zeit folgen innerhalb von ca. 2 s
- [ ] **Radio:** Sendername und laufender Titel werden angezeigt, kein Ring, keine Zeit
- [ ] **TV/Line-In** (falls vorhanden): „TV“ bzw. „Line-In“
- [ ] **Leere Warteschlange:** „Nichts in der Warteschlange“

**Lautstärke-Einblendung**
- [ ] Beim Drehen erscheinen Lautstärkebogen und Zahl und verschwinden 2 s nach dem letzten Klick
- [ ] Lautstärke in der Sonos-App ändern: Die Einblendung erscheint kurz mit dem neuen Wert

**Wischen**
- [ ] Nach rechts wischen: Hinweis „Nächster Titel“, der Titel wechselt, Log `SWIPE rechts -> Next` und `SONOS Next ok`
- [ ] Nach links wischen: vorheriger Titel (bzw. Sprung an den Anfang des Titels, so macht es Sonos)
- [ ] Bei Radio: Hinweis „Bei dieser Quelle nicht möglich“, keine Fehlermeldung
- [ ] **Spotify Connect** (Musik aus der Spotify-App auf den Speaker geschickt): Funktionieren Wischen und
      Play/Pause auch dann? Das ist bei dieser Betriebsart noch ungeprüft, bitte Log mitschicken
- [ ] Ein normales Antippen (ohne Wischen) löst nichts aus

**Robustheit**
- [ ] 15 Minuten laufen lassen: Im Log dürfen vereinzelt `SONOS … FEHLER (1/3)` oder `Aussetzer` stehen,
      aber auf dem Display erscheint **keine** Fehlermeldung, solange der Speaker erreichbar ist
- [ ] `heap_frei` bleibt stabil, kein Neustart

---

## Checkliste Schritt 4 – Ringmenü und Spulen ✅

**T3** (optional, ein Titel mit bekannter Länge muss laufen):
```bash
python3 tools/sonos_probe.py <IP> --save probe-seek transport seek 0:01:30
```
- [ ] Die Wiedergabe springt auf 1:30

**Ringmenü** (`pio run -e matouch -t upload`)
- [ ] Lang drücken: Das Menü erscheint mit vier Symbolen im Kreis. „Spulen“ oben ist grün, der Name steht in der Mitte.
      Log `MENU Spulen`
- [ ] Ring drehen: Die Auswahl springt zwischen „Spulen“ und „Schließen“. „Räume“ und „Favoriten“ sind grau und
      werden übersprungen
- [ ] Während das Menü offen ist, ändert der Ring **nicht** die Lautstärke
- [ ] „Schließen“ wählen und kurz drücken: Das Menü schließt sich
- [ ] Menü öffnen und lang drücken: Das Menü schließt sich
- [ ] Menü öffnen und 10 s nichts tun: Das Menü schließt sich von selbst

**Spulen** (bei einem Titel mit bekannter Länge, z. B. Spotify)
- [ ] Menü → „Spulen“ → kurz drücken: Der Ring wird dicker, ein weißer Punkt steht an der aktuellen Position,
      die Zeit steht groß in der Mitte
- [ ] Ring drehen: Punkt und Zeit wandern (1 % pro Klick, mindestens 5 s, schnell gedreht dreifach)
- [ ] Kurz drücken: Die Musik springt an die gewählte Stelle (±2 s), Log `SCRUB -> Seek …` und `SONOS Seek … ok`
- [ ] Erneut spulen und lang drücken: Abbruch, die Musik läuft unverändert weiter
- [ ] Während des Spulens ändert der Ring **nicht** die Lautstärke
- [ ] Bei **Radio**: Menü → „Spulen“ → Hinweis „Spulen geht nur bei Titeln mit bekannter Länge“

**Allgemein**
- [ ] Im normalen Modus funktionieren Lautstärke, Play/Pause und Wischen wie bisher
- [ ] Ist die Bedienung ohne Anleitung verständlich? Dein Eindruck als Notiz

---

## Checkliste Schritt 5 – Räume finden und wählen ✅

**Vorbereitung:** Die Speaker-IP in `include/secrets.h` ist jetzt optional. Für den ersten Test
**leer lassen** (`#define SONOS_IP ""`), damit die automatische Suche geprüft wird.

**Suche** (`pio run -e matouch -t upload`)
- [ ] Nach dem Start: „Suche Sonos-Anlage …“, dann im Log `SSDP: 13 Sonos-Speaker gefunden` und `RAUM …`
- [ ] Beim allerersten Start wird der alphabetisch erste Raum gewählt (bei dir „Bad Kinder“),
      die Statuszeile unten zeigt den Raumnamen
- [ ] Now Playing, Lautstärke und Play/Pause funktionieren für diesen Raum wie bisher

**Raumwahl**
- [ ] Lang drücken → Ring auf „Räume“ (Haus-Symbol) → kurz drücken: Drehrad mit deinen 7 Räumen,
      der aktive hat ein Häkchen, unten steht z. B. `1 / 7`
- [ ] Drehen blättert durch die Räume, an den Enden bleibt die Auswahl stehen
- [ ] Keine Satelliten, Subs oder doppelten Einträge (z. B. nur **ein** „Esszimmer“ trotz Stereopaar)
- [ ] Kurz drücken auf „Wohnzimmer“: „Verbinde mit Wohnzimmer …“, danach Titel/Lautstärke des Wohnzimmers
- [ ] Langdruck bricht die Raumwahl ab, ohne umzuschalten
- [ ] **Neustart** (Reset-Taste): Das Gerät startet wieder mit dem zuletzt gewählten Raum
- [ ] `#define SONOS_ROOM "Wohnzimmer"` eintragen, flashen, im Menü einen anderen Raum wählen, Reset:
      Das Gerät startet im Wohnzimmer. Log `Bevorzugter Raum: Wohnzimmer`

**Gruppen** (in der Sonos-App zwei Räume gruppieren, z. B. Wohnzimmer + Esszimmer)
- [ ] Innerhalb von 30 s erscheint der Raum im Drehrad als „Wohnzimmer + 1“ (bzw. mit dem Koordinator vorne)
- [ ] Ist ein Raum der Gruppe aktiv, gilt die Lautstärke für die **ganze Gruppe**, Log `SetGroupVolume … ok`
- [ ] Play/Pause wirkt auf die ganze Gruppe
- [ ] Gruppe in der App wieder auflösen: Das Gerät folgt, die Anzeige wird wieder zu „Wohnzimmer“
- [ ] T3 dazu (während gruppiert):
      `python3 tools/sonos_probe.py <Koordinator-IP> --save probe-gruppe groupvolume get`

**Robustheit**
- [ ] Router neu starten: Das Gerät findet die Anlage wieder, der Raum bleibt derselbe
- [ ] 10 Minuten laufen lassen: `heap_frei` stabil, kein Neustart

---

## Checkliste Schritt 6 – Albumcover ✅

Jeder Dienst liefert Cover anders, deshalb bitte **mit allen Quellen testen, die du nutzt**.

**T3 zuerst**, für jede Quelle: in der Sonos-App starten, dann
```bash
python3 tools/sonos_probe.py <IP> nowplaying          # zeigt die Zeile „Cover: …“
python3 tools/sonos_probe.py - cover '<Cover-Adresse>'  # Format, Größe, darstellbar?
```
Schick mir je Quelle die beiden Ausgaben, dann decken die Tests auch diese Dienste ab.

**Am Gerät** (`pio run -e matouch -t upload`), je Quelle:

| Quelle | Cover erscheint? | Log `COVER ok: …` (Zeiten) | Bemerkung |
|---|---|---|---|
| Spotify über die Sonos-App (Warteschlange) | | | |
| Spotify Connect (aus der Spotify-App) | | | |
| Radio (TuneIn) | | | Senderlogo |
| Weitere Dienste (Apple Music, Amazon, Deezer, Bibliothek …) | | | |
| TV / Line-In | kein Cover erwartet | | |

- [ ] Das Cover füllt den runden Hintergrund, Titel und Interpret bleiben gut lesbar
- [ ] Beim Titelwechsel erscheint das neue Cover nach kurzer Zeit. Die Bedienung (Ring, Taste, Wischen)
      reagiert währenddessen normal
- [ ] Menü, Raumwahl und Lautstärke-Einblendung liegen weiterhin sauber darüber
- [ ] 30 Titelwechsel hintereinander (Wischen): kein Absturz, `heap_frei` im Log bleibt stabil
- [ ] Log bei fehlenden Covern: `COVER … Download fehlgeschlagen/nicht darstellbar: <Grund>`. Bitte mitschicken

Nachtrag (schnellere Cover): Das Log schlüsselt jetzt auf:
`COVER ok: 88 KB, 640x640, Verbindung 850 ms, Übertragung 2100 ms, Dekodieren 300 ms, Skalieren 150 ms`.
Beim nächsten Cover vom selben Server innerhalb von 60 s steht dort `Verbindung wiederverwendet`.

## Checkliste Schritt 7 – Favoriten

Favoriten funktionieren je nach Dienst verschieden, deshalb **zuerst T3**:
```bash
python3 tools/sonos_probe.py <IP> favorites list              # alle Favoriten + wie sie starten
python3 tools/sonos_probe.py <IP> --save probe-fav favorites list  # dazu die Rohdaten (für Testdaten)
python3 tools/sonos_probe.py <IP> favorites play <Nr>         # IP des Gruppen-Koordinators nehmen!
```
Achtung: `play` einer Playlist/eines Albums **ersetzt die Warteschlange** – genau wie die Firmware.
Die Ausgabe von `favorites list` bitte mitschicken (Namen der Kinder vorher ersetzen, das Repo ist öffentlich).

**Am Gerät** (`pio run -e matouch -t upload`):

- [ ] Nach dem Start steht im Log `FAVORITEN: n geladen, m nicht abspielbar ausgeblendet (… ms)`;
      n + m stimmt mit der Sonos-App überein (ausgeblendet werden reine Verknüpfungen wie Podcast-Seiten)
- [ ] Langdruck → Menü → „Favoriten“ → Liste als Drehrad; unten steht der Dienst (z. B. „TuneIn · 3 / 12“)
- [ ] Je Art einmal abspielen und prüfen, dass Now Playing (Titel, Cover) nachzieht:

| Favorit | Log `FAVORIT … (direkt / über die Warteschlange)` | startet? | Now Playing + Cover? |
|---|---|---|---|
| Radiosender (TuneIn / Sonos Radio) | direkt | | |
| Spotify-Playlist | Warteschlange | | |
| Album | Warteschlange | | |
| Einzeltitel (Spotify, Amazon Music) | Warteschlange | | |
| Sonos-Playlist (falls vorhanden) | Warteschlange | | |

- [ ] Im gruppierten Raum (z. B. „Esszimmer + 1“) spielt der Favorit in der ganzen Gruppe
- [ ] Favorit in der Sonos-App hinzufügen, Menü öffnen, nach ein paar Sekunden erneut „Favoriten“ → neuer Eintrag ist da
- [ ] Beim nächsten Öffnen steht die Auswahl auf dem zuletzt gespielten Favoriten
- [ ] Fehlerfall: Meldung in der Statuszeile (rot) und im Log `FAVORIT „…“ FEHLER: …` – bitte mitschicken

**Experiment Pocket Casts** (Verknüpfung „In Progress“ ohne Adresse):
```bash
python3 tools/sonos_probe.py <IP des Koordinators> favorites try <Nr>
```
Baut aus den Metadaten eine Container-Adresse (`x-rincon-cpcontainer:<id>?sid=233&flags=…&sn=…`) und
probiert, sie in die Warteschlange zu legen (Warteschlange wird geleert). Bei Erfolg zeigt es die
funktionierende Adresse – dann kann die Firmware solche Verknüpfungen ebenfalls starten.

