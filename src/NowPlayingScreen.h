#pragma once

#include <stdint.h>

#include "PlaybackController.h"

/**
 * Now-Playing-Bildschirm (Stand Schritt 5).
 *
 *        ┌──── Fortschrittsring (360°, beginnt oben) ────┐
 *        │              ▶ / ❚❚  (Zustand)                │
 *        │          Titel (Laufschrift)                  │
 *        │          Interpret bzw. Sender                │
 *        │          Album                                │
 *        │              1:02 / 3:45                      │
 *        │          Statuszeile (Fehler, Hinweise)       │
 *        └───────────────────────────────────────────────┘
 *
 * Beim Drehen blendet sich darüber die Lautstärke ein (270°-Bogen + große Zahl) und
 * verschwindet 2 s nach der letzten Änderung wieder.
 * Wischen nach links/rechts meldet der Bildschirm über den Swipe-Callback.
 *
 * Ringmenü (Langdruck): Einträge liegen im Kreis (oben, rechts, unten, links), der
 * gewählte ist grün hinterlegt, sein Name steht in der Mitte.
 * Spulen: Der Fortschrittsring wird dicker und bekommt einen Punkt an der Zielposition,
 * die Zielzeit steht groß in der Mitte.
 */
class NowPlayingScreen {
public:
    enum class Swipe { Left, Right };
    using SwipeHandler = void (*)(Swipe);

    void create(SwipeHandler onSwipe);

    /** Texte setzen; unveränderte Texte werden nicht neu gezeichnet. */
    void setTrack(const char* title, const char* subtitle, const char* album);

    /** Fortschritt: permille 0..1000, Zeiten in Sekunden; showProgress=false blendet Ring und Zeit aus. */
    void setProgress(bool showProgress, int permille, int positionSec, int durationSec);

    void setPlayState(app::PlayState state);

    /** Cover als Hintergrund (480×480 RGB565, bereits abgedunkelt) oder nullptr = schwarz. */
    void setCover(const uint16_t* pixels);

    /** Lautstärke setzen (Ring + Zahl in der Einblendung). */
    void setVolume(int volume, bool known);
    /** Lautstärke-Einblendung zeigen; verschwindet nach 2 s ohne weitere Änderung. */
    void showVolumeOverlay(uint32_t nowMs);

    enum class Status { Info, Ok, Error };
    void setStatus(const char* text, Status kind);

    /** Ringmenü öffnen bzw. Auswahl aktualisieren (Index = app::ModeController::MenuItem). */
    void showMenu(int selection);
    void hideMenu();

    /** Spulen-Ansicht zeigen bzw. aktualisieren. Solange sie aktiv ist, ignoriert setProgress den Ring. */
    void showScrub(int targetSec, int durationSec);
    void hideScrub();

    /**
     * Raumwahl als Drehrad: markierter Raum groß in der Mitte, Nachbarn darüber/darunter.
     * @param names   Anzeigenamen aller Räume
     * @param active  aktuell aktiver Raum (bekommt ein Häkchen)
     */
    void showRoomPicker(const char* const* names, int count, int index, int active);
    void hideRoomPicker();

    /** Regelmäßig aufrufen (blendet die Lautstärke nach Ablauf aus). */
    void tick(uint32_t nowMs);
};
