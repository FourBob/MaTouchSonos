#pragma once

#include <string>
#include <vector>

#include "Soap.h"

namespace sonos {

/** Ein sichtbarer Speaker bzw. Raum (Stereopaare und Surround-Satelliten zählen als einer). */
struct ZoneMember {
    std::string uuid;  ///< z. B. "RINCON_000E58A0123401400"
    std::string ip;    ///< aus dem Location-Attribut
    std::string name;  ///< ZoneName, z. B. "Küche"
};

/** Eine Gruppe von Räumen, die gemeinsam abspielen. Einzelne Räume sind Gruppen mit einem Mitglied. */
struct ZoneGroup {
    std::string coordinatorUuid;
    std::string coordinatorIp;
    std::string name;                  ///< Name des Koordinators
    std::vector<ZoneMember> members;   ///< sichtbare Mitglieder, Koordinator zuerst

    /** Anzeigename: „Küche“ bzw. „Küche + 2“. */
    std::string displayName() const;
    bool isGroup() const { return members.size() > 1; }
    bool contains(const std::string& uuid) const;
};

namespace topology {

SoapRequest getZoneGroupState();

/**
 * Wertet eine GetZoneGroupState-Antwort aus.
 *  - Unsichtbare Geräte (Invisible="1": zweiter Lautsprecher eines Stereopaars, Sub,
 *    Surrounds) und Bridges/Boosts (IsZoneBridge="1") werden ausgelassen.
 *  - Satelliten (<Satellite>) werden ignoriert.
 *  - Gruppen sind alphabetisch nach Namen sortiert.
 * @return false bei unerwarteter Antwort.
 */
bool parseZoneGroupState(const std::string& body, std::vector<ZoneGroup>& out);

/** "http://192.168.1.50:1400/xml/device_description.xml" → "192.168.1.50" ("" bei Fehler). */
std::string ipFromLocation(const std::string& location);

/** Findet die Gruppe, in der `uuid` Mitglied ist (−1, wenn keine). */
int findGroupOf(const std::vector<ZoneGroup>& groups, const std::string& uuid);

/** Findet die Gruppe, zu der die IP gehört (−1, wenn keine). */
int findGroupByIp(const std::vector<ZoneGroup>& groups, const std::string& ip);

}  // namespace topology

/** SSDP-Suche nach Sonos-Speakern (UDP-Multicast 239.255.255.250:1900). */
namespace ssdp {

constexpr const char* kMulticastAddress = "239.255.255.250";
constexpr int kPort = 1900;

/** M-SEARCH-Anfrage nach ZonePlayern. */
std::string buildSearchRequest();

/**
 * Wertet eine SSDP-Antwort aus.
 * @return true, wenn es ein Sonos-ZonePlayer ist; `location` enthält dann die LOCATION-URL.
 */
bool parseSearchResponse(const std::string& response, std::string& location);

}  // namespace ssdp

/**
 * GroupRenderingControl – Lautstärke einer ganzen Gruppe (am Koordinator).
 * Sonos verteilt Änderungen proportional auf die Mitglieder, bezogen auf den letzten
 * „Snapshot“ der Einzellautstärken; deshalb vor dem Lesen SnapshotGroupVolume senden.
 */
namespace grouprendering {
SoapRequest snapshotGroupVolume();
SoapRequest getGroupVolume();
SoapRequest setGroupVolume(int volume);
bool parseGetGroupVolume(const std::string& body, int& volume);
}  // namespace grouprendering

}  // namespace sonos
