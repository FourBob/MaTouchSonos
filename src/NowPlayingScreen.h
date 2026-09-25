#pragma once

#include <stdint.h>

#include "PlaybackController.h"

/**
 * Now-Playing-Bildschirm (Stand Schritt 3).
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

    /** Lautstärke setzen (Ring + Zahl in der Einblendung). */
    void setVolume(int volume, bool known);
    /** Lautstärke-Einblendung zeigen; verschwindet nach 2 s ohne weitere Änderung. */
    void showVolumeOverlay(uint32_t nowMs);

    enum class Status { Info, Ok, Error };
    void setStatus(const char* text, Status kind);

    /** Regelmäßig aufrufen (blendet die Lautstärke nach Ablauf aus). */
    void tick(uint32_t nowMs);
};
