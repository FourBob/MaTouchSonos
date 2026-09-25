#pragma once

// ============================================================================
// Vorlage für WLAN-Zugangsdaten.
//
// 1. Diese Datei kopieren nach  include/secrets.h
// 2. Werte eintragen
//
// include/secrets.h steht in .gitignore und wird NICHT committet.
// Wird ab Schritt 1 benötigt.
// ============================================================================

#define WIFI_SSID "MeinWLAN"
#define WIFI_PASS "geheim"

// Optional: bevorzugter Raum – mit ihm startet das Gerät immer, egal welcher Raum zuletzt im
// Menü gewählt war. Name genau wie in der Sonos-App (Groß-/Kleinschreibung egal).
// Leer lassen = mit dem zuletzt gewählten Raum starten.
#define SONOS_ROOM ""

// Optional: IP-Adresse eines Sonos-Speakers. Seit Schritt 5 findet die Fernbedienung die
// Anlage selbst (SSDP). Diese IP dient nur als schneller Startpunkt für die Suche. Leer lassen, wenn nicht gebraucht.
#define SONOS_IP ""
