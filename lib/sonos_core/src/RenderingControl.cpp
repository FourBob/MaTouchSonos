#include "RenderingControl.h"

#include <cctype>
#include <cstdlib>

#include "Xml.h"

namespace sonos {
namespace rendering {

SoapRequest getVolume() {
    return buildSoapRequest(services::RenderingControl, "GetVolume", {{"InstanceID", "0"}, {"Channel", "Master"}});
}

SoapRequest setVolume(int volume) {
    if (volume < kMinVolume) volume = kMinVolume;
    if (volume > kMaxVolume) volume = kMaxVolume;
    return buildSoapRequest(services::RenderingControl, "SetVolume",
                            {{"InstanceID", "0"}, {"Channel", "Master"}, {"DesiredVolume", std::to_string(volume)}});
}

bool parseGetVolume(const std::string& body, int& volume) {
    bool found = false;
    const std::string text = xml::findElement(body, "CurrentVolume", &found);
    if (!found || text.empty()) return false;
    for (char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    const int v = std::atoi(text.c_str());
    if (v < kMinVolume || v > kMaxVolume) return false;
    volume = v;
    return true;
}

}  // namespace rendering
}  // namespace sonos
