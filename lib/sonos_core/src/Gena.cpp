#include "Gena.h"

#include <cctype>
#include <cstdlib>

#include "Xml.h"

namespace sonos {
namespace gena {

const EventService AVTransport{"AVTransport", "/MediaRenderer/AVTransport/Event", "/ev/av"};
const EventService RenderingControl{"RenderingControl", "/MediaRenderer/RenderingControl/Event", "/ev/rc"};
const EventService GroupRenderingControl{"GroupRenderingControl", "/MediaRenderer/GroupRenderingControl/Event",
                                         "/ev/grc"};
const EventService ZoneGroupTopology{"ZoneGroupTopology", "/ZoneGroupTopology/Event", "/ev/zgt"};

const char* const kNotifyResponse = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

namespace {

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

}  // namespace

const EventService* serviceForCallback(const std::string& path) {
    for (const EventService* s : {&AVTransport, &RenderingControl, &GroupRenderingControl, &ZoneGroupTopology}) {
        if (path == s->callbackPath) return s;
    }
    return nullptr;
}

int parseTimeout(const std::string& header) {
    const std::string h = upper(trim(header));
    if (h == "INFINITE") return 0;
    if (h.rfind("SECOND-", 0) != 0) return -1;
    const std::string digits = h.substr(7);
    if (digits.empty() || digits.find_first_not_of("0123456789") != std::string::npos) return -1;
    return std::atoi(digits.c_str());
}

std::string Request::header(const std::string& name) const {
    const std::string key = upper(name);
    for (const auto& h : headers) {
        if (h.first == key) return h.second;
    }
    return {};
}

bool parseRequestHead(const std::string& raw, Request& out, size_t& headBytes) {
    const size_t end = raw.find("\r\n\r\n");
    if (end == std::string::npos) return false;
    headBytes = end + 4;
    out = Request{};

    size_t lineEnd = raw.find("\r\n");
    const std::string requestLine = raw.substr(0, lineEnd);
    const size_t sp1 = requestLine.find(' ');
    const size_t sp2 = sp1 == std::string::npos ? std::string::npos : requestLine.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) return false;
    out.method = requestLine.substr(0, sp1);
    out.path = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);

    size_t pos = lineEnd + 2;
    while (pos < end) {
        lineEnd = raw.find("\r\n", pos);
        const std::string line = raw.substr(pos, lineEnd - pos);
        pos = lineEnd + 2;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        out.headers.emplace_back(upper(trim(line.substr(0, colon))), trim(line.substr(colon + 1)));
    }
    const std::string length = out.header("Content-Length");
    out.contentLength = length.empty() ? 0 : std::atoi(length.c_str());
    if (out.contentLength < 0) out.contentLength = 0;
    return true;
}

std::string lastChange(const std::string& body) { return xml::unescape(xml::findElement(body, "LastChange")); }

bool eventValue(const std::string& lastChangeXml, const std::string& element, std::string& value, const char* channel) {
    const std::string open = "<" + element + " ";
    size_t pos = 0;
    while ((pos = lastChangeXml.find(open, pos)) != std::string::npos) {
        const size_t tagEnd = lastChangeXml.find('>', pos);
        if (tagEnd == std::string::npos) return false;
        const std::string tag = lastChangeXml.substr(pos, tagEnd - pos);
        pos = tagEnd;
        if (channel && tag.find(std::string("channel=\"") + channel + "\"") == std::string::npos) continue;
        const size_t v = tag.find("val=\"");
        if (v == std::string::npos) continue;
        const size_t vEnd = tag.find('"', v + 5);
        if (vEnd == std::string::npos) continue;
        value = xml::unescape(tag.substr(v + 5, vEnd - v - 5));
        return true;
    }
    return false;
}

}  // namespace gena
}  // namespace sonos
