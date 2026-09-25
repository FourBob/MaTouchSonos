#pragma once

/**
 * Lautstärke-Bildschirm (Schritt 1).
 *
 * - Bogen über 270° am Rand zeigt die Lautstärke (unten offen, wie ein Drehregler)
 * - Große Zahl in der Mitte
 * - Statuszeile unten: WLAN-/Speaker-Zustand, Fehler in Rot
 *
 * Solange die Lautstärke des Speakers unbekannt ist, wird „–“ angezeigt und der
 * Bogen ist ausgegraut.
 */
class VolumeScreen {
public:
    void create(const char* speakerLabel);

    /** Wert anzeigen; `known == false` zeigt „–“ und graut den Bogen aus. */
    void setVolume(int volume, bool known);

    enum class Status { Info, Ok, Error };
    void setStatus(const char* text, Status kind);
};
