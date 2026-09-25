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

Die Tests liegen in `test/`. Sie prüfen den Code in `lib/app_core` und später in `lib/sonos_core`.
Ab Schritt 1 dienen echte Sonos-Antworten aus T3 als Testdaten in `test/fixtures/`.

## T2 – Build in der CI

Bei jedem Push baut GitHub Actions die Firmware und führt die Unit-Tests aus.
Status: Reiter **Actions** im Repo. Die fertigen Binärdateien liegen dort als Artifact `firmware-matouch`.

## T3 – Protokoll-Test (ab Schritt 1)

Ein kleines Python-Skript schickt dieselben Befehle an deine Speaker, die später das Board schickt.
Damit sehen wir getrennt vom Board, ob der Befehl stimmt, und bekommen echte Antworten als Testdaten.
Die genaue Anleitung kommt mit Schritt 1.

## T4 – Geräte-Test

### Ablauf

1. Aktuellen Stand holen: `git pull`
2. Flashen: `pio run -e matouch -t upload`
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

## Checkliste Schritt 0 – Hardware-Test

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
