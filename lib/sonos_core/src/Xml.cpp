#include "Xml.h"

#include <cstdlib>

namespace sonos {
namespace xml {
namespace {

// Prüft, ob an Position `pos` (direkt hinter '<' bzw. '</') ein Tag mit dem lokalen
// Namen `name` beginnt – mit beliebigem oder keinem Namespace-Präfix.
// Gibt die Position hinter dem Namen zurück oder npos.
size_t matchTagName(const std::string& doc, size_t pos, const std::string& name) {
    size_t end = pos;
    while (end < doc.size() && doc[end] != '>' && doc[end] != ' ' && doc[end] != '/' && doc[end] != '\t' &&
           doc[end] != '\r' && doc[end] != '\n') {
        ++end;
    }
    const std::string tag = doc.substr(pos, end - pos);
    const size_t colon = tag.find(':');
    const std::string local = colon == std::string::npos ? tag : tag.substr(colon + 1);
    return local == name ? end : std::string::npos;
}

}  // namespace

std::string findElement(const std::string& doc, const std::string& name, bool* found) {
    if (found) *found = false;
    size_t pos = 0;
    while ((pos = doc.find('<', pos)) != std::string::npos) {
        ++pos;
        if (pos >= doc.size() || doc[pos] == '/' || doc[pos] == '?' || doc[pos] == '!') continue;

        const size_t afterName = matchTagName(doc, pos, name);
        if (afterName == std::string::npos) continue;

        const size_t tagEnd = doc.find('>', afterName);
        if (tagEnd == std::string::npos) return {};
        if (doc[tagEnd - 1] == '/') {  // <Name/>
            if (found) *found = true;
            return {};
        }

        // Passendes schließendes Tag suchen: </prefix:Name> oder </Name>
        size_t search = tagEnd + 1;
        size_t close;
        while ((close = doc.find("</", search)) != std::string::npos) {
            if (matchTagName(doc, close + 2, name) != std::string::npos) {
                if (found) *found = true;
                return doc.substr(tagEnd + 1, close - tagEnd - 1);
            }
            search = close + 2;
        }
        return {};
    }
    return {};
}

std::string escape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += c;
        }
    }
    return out;
}

namespace {

void appendUtf8(std::string& out, unsigned long cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x110000) {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

}  // namespace

std::string unescape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            out += text[i];
            continue;
        }
        const size_t semi = text.find(';', i);
        if (semi == std::string::npos || semi - i > 10) {
            out += text[i];
            continue;
        }
        const std::string ent = text.substr(i + 1, semi - i - 1);
        if (ent == "amp") out += '&';
        else if (ent == "lt") out += '<';
        else if (ent == "gt") out += '>';
        else if (ent == "quot") out += '"';
        else if (ent == "apos") out += '\'';
        else if (ent.size() > 1 && ent[0] == '#') {
            const bool hex = ent[1] == 'x' || ent[1] == 'X';
            const unsigned long cp = std::strtoul(ent.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10);
            appendUtf8(out, cp);
        } else {
            out += text.substr(i, semi - i + 1);  // unbekannt: unverändert lassen
        }
        i = semi;
    }
    return out;
}

}  // namespace xml
}  // namespace sonos
