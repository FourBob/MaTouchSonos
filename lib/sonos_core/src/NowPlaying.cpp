#include "NowPlaying.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "Xml.h"

namespace sonos {

namespace time {

int parseDuration(const std::string& text) {
    // Erwartet [H]H:MM:SS, optional mit Bruchteilen (".000").
    int parts[3] = {0, 0, 0};
    int n = 0;
    size_t i = 0;
    while (i < text.size() && n < 3) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) return -1;
        int v = 0;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) v = v * 10 + (text[i++] - '0');
        parts[n++] = v;
        if (i < text.size() && text[i] == ':') ++i;
        else break;
    }
    if (n != 3) return -1;
    if (parts[1] > 59 || parts[2] > 59) return -1;
    return parts[0] * 3600 + parts[1] * 60 + parts[2];
}

std::string format(int seconds) {
    if (seconds < 0) return "–:––";
    char buf[16];
    const int h = seconds / 3600, m = (seconds / 60) % 60, s = seconds % 60;
    if (h > 0) std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    else std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

std::string toUpnp(int seconds) {
    if (seconds < 0) seconds = 0;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", seconds / 3600, (seconds / 60) % 60, seconds % 60);
    return buf;
}

}  // namespace time

namespace {

bool startsWith(const std::string& s, const char* prefix) {
    return s.compare(0, std::char_traits<char>::length(prefix), prefix) == 0;
}

std::string field(const std::string& xmlDoc, const char* name) {
    return xml::unescape(xml::findElement(xmlDoc, name));
}

bool isStreamUri(const std::string& uri) {
    return startsWith(uri, "x-sonosapi-stream:") || startsWith(uri, "x-sonosapi-radio:") ||
           startsWith(uri, "x-rincon-mp3radio:") || startsWith(uri, "aac:") || startsWith(uri, "hls-radio:") ||
           startsWith(uri, "x-sonosapi-hls:");
}

/** Sonos setzt bei Radio oft die Stream-URI als dc:title – die ist nicht anzeigbar. */
bool isUriLike(const std::string& s) {
    if (s.find("://") != std::string::npos || startsWith(s, "x-") || startsWith(s, "aac:") ||
        startsWith(s, "hls-radio:")) {
        return true;
    }
    // Dateiname mit Parametern, z. B. „stream.aac?aggregator=tunein&cid=…“ (TuneIn):
    // keine Leerzeichen, aber '?' und '='.
    bool hasSpace = false;
    for (char c : s) hasSpace |= std::isspace(static_cast<unsigned char>(c)) != 0;
    return !hasSpace && s.find('?') != std::string::npos && s.find('=') != std::string::npos;
}

/** Sonos-interne Platzhalter im streamContent. */
bool isPlaceholder(const std::string& s) { return startsWith(s, "ZPSTR_"); }

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

}  // namespace

bool parsePositionInfo(const std::string& body, PositionInfo& out) {
    bool hasUri = false, hasDuration = false;
    const std::string uri = xml::findElement(body, "TrackURI", &hasUri);
    const std::string duration = xml::findElement(body, "TrackDuration", &hasDuration);
    if (!hasUri || !hasDuration) return false;

    out.trackUri = xml::unescape(uri);
    out.trackMetaData = xml::unescape(xml::findElement(body, "TrackMetaData"));
    if (out.trackMetaData == "NOT_IMPLEMENTED") out.trackMetaData.clear();
    out.durationSec = time::parseDuration(duration);
    out.positionSec = time::parseDuration(xml::findElement(body, "RelTime"));
    return true;
}

bool parseMediaInfo(const std::string& body, MediaInfo& out) {
    bool found = false;
    const std::string uri = xml::findElement(body, "CurrentURI", &found);
    if (!found) return false;
    out.currentUri = xml::unescape(uri);
    out.currentUriMetaData = xml::unescape(xml::findElement(body, "CurrentURIMetaData"));
    if (out.currentUriMetaData == "NOT_IMPLEMENTED") out.currentUriMetaData.clear();
    return true;
}

TrackMeta parseDidl(const std::string& didl) {
    TrackMeta m;
    if (didl.empty()) return m;
    m.title = trim(field(didl, "title"));
    m.artist = trim(field(didl, "creator"));
    m.album = trim(field(didl, "album"));
    m.albumArtUri = trim(field(didl, "albumArtURI"));
    m.streamContent = trim(field(didl, "streamContent"));
    return m;
}

NowPlaying buildNowPlaying(const PositionInfo& pos, const MediaInfo* media) {
    NowPlaying np;
    const TrackMeta meta = parseDidl(pos.trackMetaData);

    if (startsWith(pos.trackUri, "x-sonos-htastream:")) {
        np.kind = SourceKind::TV;
        np.title = "TV";
        return np;
    }
    if (startsWith(pos.trackUri, "x-rincon-stream:")) {
        np.kind = SourceKind::LineIn;
        np.title = "Line-In";
        return np;
    }
    if (pos.trackUri.empty() && meta.title.empty()) {
        np.kind = SourceKind::None;
        return np;
    }

    const bool radio = isStreamUri(pos.trackUri) || pos.durationSec == 0;
    np.albumArtUri = meta.albumArtUri;

    if (radio) {
        np.kind = SourceKind::Radio;
        std::string station;
        if (media) {
            const TrackMeta source = parseDidl(media->currentUriMetaData);
            station = source.title;
            if (np.albumArtUri.empty()) np.albumArtUri = source.albumArtUri;  // Senderlogo
        }
        if (station.empty() && !isPlaceholder(meta.title)) station = meta.title;
        if (isUriLike(station)) station.clear();

        if (isPlaceholder(meta.streamContent) || isPlaceholder(meta.title)) {
            np.title = "Verbinde …";
        } else if (!meta.streamContent.empty()) {
            np.title = meta.streamContent;
        } else if (!station.empty()) {
            np.title = station;
        } else {
            np.title = "Radio";
        }
        np.subtitle = station == np.title ? "" : station;
        return np;
    }

    np.kind = SourceKind::Track;
    np.title = meta.title;
    np.subtitle = meta.artist;
    np.album = meta.album;
    np.durationSec = pos.durationSec;
    np.positionSec = pos.positionSec;
    return np;
}

}  // namespace sonos
