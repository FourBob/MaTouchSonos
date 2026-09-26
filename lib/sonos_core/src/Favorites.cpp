#include "Favorites.h"

#include <cstdlib>

#include "Xml.h"

namespace sonos {

namespace services {
const Service ContentDirectory{"/MediaServer/ContentDirectory/Control",
                               "urn:schemas-upnp-org:service:ContentDirectory:1"};
}  // namespace services

namespace favorites {
namespace {

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

std::string field(const std::string& doc, const char* name) { return xml::unescape(xml::findElement(doc, name)); }

/** Wert von id="…" im ersten <item> der Metadaten. */
std::string metadataItemId(const std::string& metadata) {
    const size_t item = metadata.find("<item ");
    if (item == std::string::npos) return {};
    const size_t tagEnd = metadata.find('>', item);
    const size_t id = metadata.find(" id=\"", item);
    if (id == std::string::npos || id > tagEnd) return {};
    const size_t start = id + 5;
    const size_t end = metadata.find('"', start);
    return end == std::string::npos ? std::string() : xml::unescape(metadata.substr(start, end - start));
}

}  // namespace

SoapRequest browse(int startIndex, int count) {
    return buildSoapRequest(services::ContentDirectory, "Browse",
                            {{"ObjectID", "FV:2"},
                             {"BrowseFlag", "BrowseDirectChildren"},
                             {"Filter", "*"},
                             {"StartingIndex", std::to_string(startIndex)},
                             {"RequestedCount", std::to_string(count)},
                             {"SortCriteria", ""}});
}

bool parseBrowse(const std::string& body, const std::function<void(int, Favorite&&)>& visit, int& totalMatches) {
    bool found = false;
    const std::string didl = xml::unescape(xml::findElement(body, "Result", &found));
    if (!found) return false;
    totalMatches = std::atoi(xml::findElement(body, "TotalMatches").c_str());

    // Die Metadaten jedes Favoriten sind nochmals kodiert – ein rohes „<item“ gehört also
    // immer zur äußeren Liste.
    size_t pos = 0;
    int position = 0;
    while ((pos = didl.find("<item", pos)) != std::string::npos) {
        const size_t afterName = pos + 5;
        if (afterName < didl.size() && didl[afterName] != ' ' && didl[afterName] != '>') {
            pos = afterName;
            continue;
        }
        const size_t end = didl.find("</item>", afterName);
        if (end == std::string::npos) break;
        const std::string item = didl.substr(pos, end - pos);
        pos = end + 7;

        Favorite f;
        f.title = field(item, "title");
        f.description = field(item, "description");
        f.uri = field(item, "res");
        f.metadata = field(item, "resMD");
        f.albumArtUri = field(item, "albumArtURI");
        f.upnpClass = f.metadata.empty() ? std::string() : field(f.metadata, "class");
        visit(position++, std::move(f));
    }
    return true;
}

bool parseBrowse(const std::string& body, std::vector<Favorite>& out, int& totalMatches) {
    return parseBrowse(body, [&out](int, Favorite&& f) { out.push_back(std::move(f)); }, totalMatches);
}

PlayMethod playMethod(const Favorite& favorite) {
    const std::string& uri = favorite.uri;
    if (uri.empty()) {
        // Verknüpfung ohne Adresse: Ordner eines Dienstes lassen sich als Container anhängen.
        std::string container;
        if (startsWith(favorite.upnpClass, "object.container") && containerUri(favorite, 1, container)) {
            return PlayMethod::QueueContainer;
        }
        return PlayMethod::Unsupported;
    }

    // Ströme: Radio (TuneIn, Sonos Radio, Senderlisten der Dienste), Line-In, TV.
    static const char* const kStreams[] = {
        "x-sonosapi-stream:", "x-sonosapi-radio:", "x-sonosapi-hls:", "x-rincon-mp3radio:", "hls-radio:",
        "aac:",               "pndrradio:",        "x-rincon-stream:", "x-sonos-htastream:",
    };
    for (const char* prefix : kStreams) {
        if (startsWith(uri, prefix)) return PlayMethod::Direct;
    }
    if (startsWith(favorite.upnpClass, "object.item.audioItem.audioBroadcast")) return PlayMethod::Direct;

    // Alles andere (Playlists, Alben, Sonos-Playlists, einzelne Titel) über die Warteschlange –
    // so funktionieren danach auch „Nächster“/„Vorheriger“ und Spulen.
    return PlayMethod::Queue;
}

int serviceId(const Favorite& favorite) {
    const size_t pos = favorite.metadata.find("SA_RINCON");
    if (pos == std::string::npos) return -1;
    const char* digits = favorite.metadata.c_str() + pos + 9;
    char* end = nullptr;
    const long type = std::strtol(digits, &end, 10);
    if (end == digits || *end != '_' || type < 7 || (type - 7) % 256 != 0) return -1;
    return static_cast<int>((type - 7) / 256);
}

bool containerUri(const Favorite& favorite, int serial, std::string& uri) {
    const std::string id = metadataItemId(favorite.metadata);
    const int sid = serviceId(favorite);
    if (id.empty() || sid < 0) return false;
    uri = "x-rincon-cpcontainer:" + id + "?sid=" + std::to_string(sid) + "&flags=8300&sn=" + std::to_string(serial);
    return true;
}

}  // namespace favorites
}  // namespace sonos
