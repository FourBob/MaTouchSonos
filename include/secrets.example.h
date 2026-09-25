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

// Nur für Schritt 1–4: IP-Adresse eines Sonos-Speakers (Router-Geräteliste oder Sonos-App;
// prüfen mit: python3 tools/sonos_probe.py <IP> info). Ist der Speaker Teil einer Gruppe,
// die IP des Gruppen-Koordinators nehmen – nur dort funktioniert Play/Pause.
// Ab Schritt 5 werden die Speaker automatisch gefunden.
#define SONOS_IP "192.168.1.50"
