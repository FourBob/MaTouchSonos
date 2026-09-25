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

SoapRequest seekTrack(int trackNumber) {
    return buildSoapRequest(services::AVTransport, "Seek",
                            {{"InstanceID", "0"}, {"Unit", "TRACK_NR"}, {"Target", std::to_string(trackNumber)}});
}

SoapRequest setAVTransportURI(const std::string& uri, const std::string& metadata) {
    return buildSoapRequest(services::AVTransport, "SetAVTransportURI",
                            {{"InstanceID", "0"}, {"CurrentURI", uri}, {"CurrentURIMetaData", metadata}});
}

SoapRequest removeAllTracksFromQueue() {
    return buildSoapRequest(services::AVTransport, "RemoveAllTracksFromQueue", {{"InstanceID", "0"}});
}

SoapRequest addURIToQueue(const std::string& uri, const std::string& metadata) {
    return buildSoapRequest(services::AVTransport, "AddURIToQueue",
                            {{"InstanceID", "0"},
                             {"EnqueuedURI", uri},
                             {"EnqueuedURIMetaData", metadata},
                             {"DesiredFirstTrackNumberEnqueued", "0"},
                             {"EnqueueAsNext", "0"}});
}

std::string queueUri(const std::string& coordinatorUuid) { return "x-rincon-queue:" + coordinatorUuid + "#0"; }

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
