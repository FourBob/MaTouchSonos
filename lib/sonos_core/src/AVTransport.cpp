#include "AVTransport.h"

#include "NowPlaying.h"
#include "Xml.h"

namespace sonos {
namespace avtransport {

SoapRequest play() {
    return buildSoapRequest(services::AVTransport, "Play", {{"InstanceID", "0"}, {"Speed", "1"}});
}

SoapRequest pause() {
    return buildSoapRequest(services::AVTransport, "Pause", {{"InstanceID", "0"}});
}

SoapRequest stop() {
    return buildSoapRequest(services::AVTransport, "Stop", {{"InstanceID", "0"}});
}

SoapRequest getTransportInfo() {
    return buildSoapRequest(services::AVTransport, "GetTransportInfo", {{"InstanceID", "0"}});
}

SoapRequest getPositionInfo() {
    return buildSoapRequest(services::AVTransport, "GetPositionInfo", {{"InstanceID", "0"}});
}

SoapRequest getMediaInfo() {
    return buildSoapRequest(services::AVTransport, "GetMediaInfo", {{"InstanceID", "0"}});
}

SoapRequest next() {
    return buildSoapRequest(services::AVTransport, "Next", {{"InstanceID", "0"}});
}

SoapRequest previous() {
    return buildSoapRequest(services::AVTransport, "Previous", {{"InstanceID", "0"}});
}

SoapRequest seek(int positionSec) {
    return buildSoapRequest(services::AVTransport, "Seek",
                            {{"InstanceID", "0"}, {"Unit", "REL_TIME"}, {"Target", time::toUpnp(positionSec)}});
}

TransportState parseTransportState(const std::string& text) {
    if (text == "PLAYING") return TransportState::Playing;
    if (text == "PAUSED_PLAYBACK") return TransportState::Paused;
    if (text == "STOPPED" || text == "NO_MEDIA_PRESENT") return TransportState::Stopped;
    if (text == "TRANSITIONING") return TransportState::Transitioning;
    return TransportState::Unknown;
}

bool parseTransportInfo(const std::string& body, TransportState& state) {
    bool found = false;
    const std::string text = xml::findElement(body, "CurrentTransportState", &found);
    if (!found) return false;
    state = parseTransportState(text);
    return state != TransportState::Unknown;
}

}  // namespace avtransport
}  // namespace sonos
