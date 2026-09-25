#include "Topology.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>

#include "RenderingControl.h"
#include "Xml.h"

namespace sonos {

namespace services {
const Service ZoneGroupTopology{"/ZoneGroupTopology/Control", "urn:schemas-upnp-org:service:ZoneGroupTopology:1"};
const Service GroupRenderingControl{"/MediaRenderer/GroupRenderingControl/Control",
                                    "urn:schemas-upnp-org:service:GroupRenderingControl:1"};
}  // namespace services

std::string ZoneGroup::displayName() const {
    if (members.size() <= 1) return name;
    return name + " + " + std::to_string(members.size() - 1);
}

bool ZoneGroup::contains(const std::string& uuid) const {
    for (const auto& m : members) {
        if (m.uuid == uuid) return true;
    }
    return false;
}

namespace {

using Attributes = std::map<std::string, std::string>;

/** Liest die Attribute eines Start-Tags ab `pos` (hinter dem Tag-Namen) bis '>' oder '/>'. */
Attributes parseAttributes(const std::string& doc, size_t pos, size_t end) {
    Attributes attrs;
    while (pos < end) {
        while (pos < end && std::isspace(static_cast<unsigned char>(doc[pos]))) ++pos;
        const size_t nameStart = pos;
        while (pos < end && doc[pos] != '=' && !std::isspace(static_cast<unsigned char>(doc[pos])) && doc[pos] != '/')
            ++pos;
        const std::string name = doc.substr(nameStart, pos - nameStart);
        if (pos >= end || doc[pos] != '=') {
            ++pos;
            continue;
        }
        ++pos;  // '='
        if (pos >= end || (doc[pos] != '"' && doc[pos] != '\'')) continue;
        const char quote = doc[pos++];
        const size_t valueEnd = doc.find(quote, pos);
        if (valueEnd == std::string::npos || valueEnd > end) break;
        attrs[name] = xml::unescape(doc.substr(pos, valueEnd - pos));
        pos = valueEnd + 1;
    }
    return attrs;
}

/** Ruft `fn(attributes, tagStart)` für jedes Start-Tag mit genau diesem Namen auf. */
template <typename Fn>
void forEachTag(const std::string& doc, const std::string& tagName, size_t from, size_t to, Fn fn) {
    const std::string open = "<" + tagName;
    size_t pos = from;
    while ((pos = doc.find(open, pos)) != std::string::npos && pos < to) {
        const size_t afterName = pos + open.size();
        if (afterName < doc.size() && (std::isspace(static_cast<unsigned char>(doc[afterName])) ||
                                       doc[afterName] == '>' || doc[afterName] == '/')) {
            const size_t tagEnd = doc.find('>', afterName);
            if (tagEnd == std::string::npos) return;
            fn(parseAttributes(doc, afterName, tagEnd), pos, tagEnd);
        }
        pos = afterName;
    }
}

std::string attr(const Attributes& a, const char* key) {
    const auto it = a.find(key);
    return it == a.end() ? std::string() : it->second;
}

}  // namespace

namespace topology {

SoapRequest getZoneGroupState() { return buildSoapRequest(services::ZoneGroupTopology, "GetZoneGroupState", {}); }

std::string ipFromLocation(const std::string& location) {
    const size_t scheme = location.find("://");
    if (scheme == std::string::npos) return {};
    const size_t start = scheme + 3;
    const size_t end = location.find_first_of(":/", start);
    return location.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

bool parseZoneGroupState(const std::string& body, std::vector<ZoneGroup>& out) {
    bool found = false;
    // Die Topologie steckt als (einfach kodiertes) XML im Element <ZoneGroupState>.
    // Bei S2 ist darin nochmals ein <ZoneGroupState>-Wurzelelement – egal, gesucht wird <ZoneGroups>.
    const std::string inner = xml::unescape(xml::findElement(body, "ZoneGroupState", &found));
    if (!found) return false;
    const size_t groupsStart = inner.find("<ZoneGroups");
    if (groupsStart == std::string::npos) return false;
    const size_t groupsEnd = inner.find("</ZoneGroups>", groupsStart);
    const size_t limit = groupsEnd == std::string::npos ? inner.size() : groupsEnd;

    std::vector<ZoneGroup> groups;
    forEachTag(inner, "ZoneGroup", groupsStart, limit, [&](const Attributes& ga, size_t groupPos, size_t) {
        ZoneGroup g;
        g.coordinatorUuid = attr(ga, "Coordinator");
        const size_t groupClose = inner.find("</ZoneGroup>", groupPos);
        const size_t groupLimit = groupClose == std::string::npos ? limit : groupClose;

        forEachTag(inner, "ZoneGroupMember", groupPos, groupLimit, [&](const Attributes& ma, size_t, size_t) {
            if (attr(ma, "Invisible") == "1" || attr(ma, "IsZoneBridge") == "1") return;
            ZoneMember m;
            m.uuid = attr(ma, "UUID");
            m.ip = ipFromLocation(attr(ma, "Location"));
            m.name = attr(ma, "ZoneName");
            if (m.uuid.empty() || m.ip.empty()) return;
            if (m.uuid == g.coordinatorUuid) {
                g.coordinatorIp = m.ip;
                g.name = m.name;
                g.members.insert(g.members.begin(), m);
            } else {
                g.members.push_back(m);
            }
        });

        // Gruppen ohne sichtbaren Koordinator (z. B. nur eine Bridge) weglassen.
        if (!g.coordinatorIp.empty()) groups.push_back(std::move(g));
    });

    std::sort(groups.begin(), groups.end(), [](const ZoneGroup& a, const ZoneGroup& b) { return a.name < b.name; });
    out = std::move(groups);
    return true;
}

int findGroupOf(const std::vector<ZoneGroup>& groups, const std::string& uuid) {
    for (size_t i = 0; i < groups.size(); ++i) {
        if (groups[i].contains(uuid)) return static_cast<int>(i);
    }
    return -1;
}

int findGroupByIp(const std::vector<ZoneGroup>& groups, const std::string& ip) {
    for (size_t i = 0; i < groups.size(); ++i) {
        for (const auto& m : groups[i].members) {
            if (m.ip == ip) return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace topology

namespace ssdp {

std::string buildSearchRequest() {
    return "M-SEARCH * HTTP/1.1\r\n"
           "HOST: 239.255.255.250:1900\r\n"
           "MAN: \"ssdp:discover\"\r\n"
           "MX: 1\r\n"
           "ST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n"
           "\r\n";
}

bool parseSearchResponse(const std::string& response, std::string& location) {
    // Header-Namen sind laut HTTP nicht case-sensitiv.
    std::string lower = response;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    if (lower.find("zoneplayer") == std::string::npos && lower.find("sonos") == std::string::npos) return false;

    const size_t key = lower.find("\nlocation:");
    if (key == std::string::npos) return false;
    size_t start = key + 10;
    while (start < response.size() && (response[start] == ' ' || response[start] == '\t')) ++start;
    const size_t end = response.find_first_of("\r\n", start);
    location = response.substr(start, end == std::string::npos ? std::string::npos : end - start);
    return !location.empty();
}

}  // namespace ssdp

namespace grouprendering {

SoapRequest snapshotGroupVolume() {
    return buildSoapRequest(services::GroupRenderingControl, "SnapshotGroupVolume", {{"InstanceID", "0"}});
}

SoapRequest getGroupVolume() {
    return buildSoapRequest(services::GroupRenderingControl, "GetGroupVolume", {{"InstanceID", "0"}});
}

SoapRequest setGroupVolume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    return buildSoapRequest(services::GroupRenderingControl, "SetGroupVolume",
                            {{"InstanceID", "0"}, {"DesiredVolume", std::to_string(volume)}});
}

bool parseGetGroupVolume(const std::string& body, int& volume) {
    // Antwortfeld heißt ebenfalls <CurrentVolume> – gleiche Auswertung wie bei GetVolume.
    return rendering::parseGetVolume(body, volume);
}

}  // namespace grouprendering

}  // namespace sonos
