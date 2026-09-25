#include "AlbumArt.h"

#include <cctype>
#include <cstdio>

namespace sonos {
namespace art {
namespace {

bool startsWith(const std::string& s, const char* prefix) {
    return s.compare(0, std::char_traits<char>::length(prefix), prefix) == 0;
}

void addUnique(std::vector<std::string>& list, const std::string& url) {
    if (url.empty()) return;
    for (const auto& u : list) {
        if (u == url) return;
    }
    list.push_back(url);
}

/** Ersetzt in `s` ab `pos` eine Größenangabe „<Zahl>x<Zahl>“ durch `replacement`. */
bool replaceSizeAt(std::string& s, size_t pos, const std::string& replacement) {
    size_t i = pos;
    const size_t start = i;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    if (i == start || i >= s.size() || s[i] != 'x') return false;
    ++i;
    const size_t second = i;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    if (i == second) return false;
    s.replace(start, i - start, replacement);
    return true;
}

}  // namespace

std::string urlEncode(const std::string& text) {
    std::string out;
    out.reserve(text.size() * 3);
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

std::string preferredSize(const std::string& url) {
    std::string u = url;

    // Apple Music: …/image/thumb/…/600x600bb.jpg (auch 3000x3000bb.webp) → 480x480bb.jpg
    if (u.find("mzstatic.com/") != std::string::npos) {
        const size_t slash = u.rfind('/');
        if (slash != std::string::npos && replaceSizeAt(u, slash + 1, "480x480")) {
            // WebP kann der Decoder nicht – Apple liefert unter .jpg dasselbe Bild als JPEG.
            const size_t dot = u.rfind('.');
            if (dot != std::string::npos && dot > slash) u = u.substr(0, dot) + ".jpg";
        }
        return u;
    }

    // Deezer: …/images/cover/<id>/1000x1000-000000-80-0-0.jpg → 500x500-…
    if (u.find("dzcdn.net/") != std::string::npos) {
        const size_t slash = u.rfind('/');
        if (slash != std::string::npos) replaceSizeAt(u, slash + 1, "500x500");
        return u;
    }

    return u;
}

std::vector<std::string> candidates(const NowPlaying& np, const std::string& speakerIp) {
    std::vector<std::string> list;
    if (np.kind == SourceKind::None || np.kind == SourceKind::TV || np.kind == SourceKind::LineIn) return list;

    const std::string speaker = "http://" + speakerIp + ":1400";
    const std::string& uri = np.albumArtUri;

    if (!uri.empty()) {
        if (startsWith(uri, "/")) {
            addUnique(list, speaker + uri);  // Bild-Proxy des Speakers (HTTP)
        } else if (startsWith(uri, "http://") || startsWith(uri, "https://")) {
            addUnique(list, preferredSize(uri));
            addUnique(list, uri);  // falls die umgeschriebene Größe nicht existiert
        }
    }

    // Letzter Versuch bei Titeln aus der Warteschlange: den Speaker das Cover zum Titel
    // suchen lassen. Bei Spotify Connect (x-sonos-vli) und Streams ohne Titel-URI zwecklos.
    if (np.kind == SourceKind::Track && !np.trackUri.empty() && !startsWith(np.trackUri, "x-sonos-vli:")) {
        addUnique(list, speaker + "/getaa?s=1&u=" + urlEncode(np.trackUri));
    }
    return list;
}

}  // namespace art
}  // namespace sonos
