#pragma once

#include "PlaybackController.h"

/**
 * Hauptbildschirm (Stand Schritt 2).
 *
 * - Bogen über 270° am Rand zeigt die Lautstärke (unten offen, wie ein Drehregler)
 * - Große Zahl in der Mitte
 * - Darunter der Wiedergabezustand (▶ Wiedergabe / ❚❚ Pausiert / ■ Gestoppt)
 * - Statuszeile unten: WLAN-/Speaker-Zustand, Fehler in Rot
 *
 * Solange die Lautstärke unbekannt ist, wird „–“ angezeigt und der Bogen ist ausgegraut.
 * Bei Pause werden Zahl und Bogen gedimmt – so erkennt man den Zustand auch aus dem
 * Augenwinkel. In Schritt 3 wird daraus der Now-Playing-Bildschirm.
 */
class MainScreen {
public:
    void create(const char* speakerLabel);

    /** Wert anzeigen; `known == false` zeigt „–“ und graut den Bogen aus. */
    void setVolume(int volume, bool known);

    void setPlayState(app::PlayState state);

    enum class Status { Info, Ok, Error };
    void setStatus(const char* text, Status kind);
};
