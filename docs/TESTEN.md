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

## Checkliste Schritt 1 – Lautstärke eines Speakers

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
- [ ] Schnell drehen: Die Werte springen in größeren Schritten (Beschleunigung), nichts stottert.
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
