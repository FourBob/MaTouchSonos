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

// Optional: IP-Adresse eines Sonos-Speakers. Seit Schritt 5 findet die Fernbedienung die
// Anlage selbst (SSDP). Diese IP dient nur als schneller Startpunkt für die Suche und wählt
// beim allerersten Start den Raum aus. Leer lassen, wenn nicht gebraucht.
#define SONOS_IP ""
