// Unit-Tests für das Sonos-Protokoll (lib/sonos_core).
// Ausführen mit:  pio test -e native

#include <unity.h>

#include <string>

#include "AVTransport.h"
#include "RenderingControl.h"
#include "Soap.h"
#include "Xml.h"
#include "fixtures.h"
#include "fixtures_nowplaying.h"
#include "fixtures_topology.h"
#include "Topology.h"
#include "AlbumArt.h"
#include "NowPlaying.h"

using namespace sonos;

void setUp() {}
void tearDown() {}

// --- xml ---------------------------------------------------------------------

void test_xml_find_element_ignores_namespace_prefix() {
    bool found = false;
    TEST_ASSERT_EQUAL_STRING("23", xml::findElement("<u:A><CurrentVolume>23</CurrentVolume></u:A>", "CurrentVolume", &found).c_str());
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_EQUAL_STRING("x", xml::findElement("<s:Body><s:Fault>x</s:Fault></s:Body>", "Fault").c_str());
}

void test_xml_find_element_missing() {
    bool found = true;
    TEST_ASSERT_EQUAL_STRING("", xml::findElement("<a><b>1</b></a>", "c", &found).c_str());
    TEST_ASSERT_FALSE(found);
}

void test_xml_find_element_does_not_match_prefix_of_longer_name() {
    bool found = false;
    const std::string doc = "<CurrentVolumeX>9</CurrentVolumeX><CurrentVolume>4</CurrentVolume>";
    TEST_ASSERT_EQUAL_STRING("4", xml::findElement(doc, "CurrentVolume", &found).c_str());
}

void test_xml_find_element_with_attributes_and_self_closing() {
    bool found = false;
    TEST_ASSERT_EQUAL_STRING("v", xml::findElement("<a x=\"1\"><b y='2'>v</b></a>", "b", &found).c_str());
    TEST_ASSERT_TRUE(found);
    found = false;
    TEST_ASSERT_EQUAL_STRING("", xml::findElement("<a><b/></a>", "b", &found).c_str());
    TEST_ASSERT_TRUE(found);
}

void test_xml_escape_and_unescape_roundtrip() {
    const std::string raw = "Tom & Jerry <\"Live\"> 'Mix'";
    TEST_ASSERT_EQUAL_STRING("Tom &amp; Jerry &lt;&quot;Live&quot;&gt; &apos;Mix&apos;", xml::escape(raw).c_str());
    TEST_ASSERT_EQUAL_STRING(raw.c_str(), xml::unescape(xml::escape(raw)).c_str());
}

void test_xml_unescape_numeric_entities_to_utf8() {
    TEST_ASSERT_EQUAL_STRING("It's", xml::unescape("It&#39;s").c_str());
    TEST_ASSERT_EQUAL_STRING("\xC3\x84rzte", xml::unescape("&#xC4;rzte").c_str());  // Ä
    TEST_ASSERT_EQUAL_STRING("&unknown;", xml::unescape("&unknown;").c_str());
}

// --- SOAP --------------------------------------------------------------------

void test_soap_set_volume_request_is_exact() {
    const SoapRequest req = rendering::setVolume(25);
    TEST_ASSERT_EQUAL_STRING("/MediaRenderer/RenderingControl/Control", req.path.c_str());
    TEST_ASSERT_EQUAL_STRING("\"urn:schemas-upnp-org:service:RenderingControl:1#SetVolume\"", req.soapAction.c_str());
    TEST_ASSERT_EQUAL_STRING(fixtures::kSetVolume25Request, req.body.c_str());
}

void test_soap_set_volume_is_clamped() {
    TEST_ASSERT_NOT_EQUAL(std::string::npos, rendering::setVolume(150).body.find("<DesiredVolume>100</DesiredVolume>"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, rendering::setVolume(-3).body.find("<DesiredVolume>0</DesiredVolume>"));
}

void test_soap_get_volume_request_matches_recorded() {
    const SoapRequest req = rendering::getVolume();
    TEST_ASSERT_EQUAL_STRING("\"urn:schemas-upnp-org:service:RenderingControl:1#GetVolume\"", req.soapAction.c_str());
    TEST_ASSERT_EQUAL_STRING(fixtures::kGetVolumeRequest, req.body.c_str());
}

void test_soap_arguments_are_escaped() {
    const SoapRequest req = buildSoapRequest(services::AVTransport, "X", {{"Name", "A&B"}});
    TEST_ASSERT_NOT_EQUAL(std::string::npos, req.body.find("<Name>A&amp;B</Name>"));
}

void test_soap_evaluate_ok() {
    const SoapResult r = evaluateResponse(200, fixtures::kSetVolumeResponse);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_STRING("", r.error.c_str());
}

void test_soap_evaluate_fault_with_upnp_code() {
    const SoapResult r = evaluateResponse(500, fixtures::kFault402);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_INT(402, r.upnpErrorCode);
    TEST_ASSERT_EQUAL_STRING("Ungültige Argumente (UPnP 402)", r.error.c_str());
}

void test_soap_evaluate_http_error_without_body() {
    const SoapResult r = evaluateResponse(404, "");
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("HTTP-Fehler 404", r.error.c_str());
}

void test_soap_evaluate_connection_errors() {
    TEST_ASSERT_EQUAL_STRING("Verbindung abgelehnt – Speaker nicht erreichbar", evaluateResponse(-1, "").error.c_str());
    TEST_ASSERT_EQUAL_STRING("Zeitüberschreitung – Speaker antwortet nicht", evaluateResponse(-11, "").error.c_str());
    TEST_ASSERT_FALSE(evaluateResponse(-7, "").ok);
}

// --- RenderingControl --------------------------------------------------------

void test_parse_get_volume() {
    int v = -1;
    TEST_ASSERT_TRUE(rendering::parseGetVolume(fixtures::kGetVolumeResponse, v));
    TEST_ASSERT_EQUAL_INT(12, v);
}

void test_parse_get_volume_rejects_garbage() {
    int v = 7;
    TEST_ASSERT_FALSE(rendering::parseGetVolume("<CurrentVolume></CurrentVolume>", v));
    TEST_ASSERT_FALSE(rendering::parseGetVolume("<CurrentVolume>abc</CurrentVolume>", v));
    TEST_ASSERT_FALSE(rendering::parseGetVolume("<CurrentVolume>101</CurrentVolume>", v));
    TEST_ASSERT_FALSE(rendering::parseGetVolume(fixtures::kFault402, v));
    TEST_ASSERT_EQUAL_INT(7, v);  // unverändert
}

// --- AVTransport ---------------------------------------------------------------

void test_avtransport_play_request_is_exact() {
    const SoapRequest req = avtransport::play();
    TEST_ASSERT_EQUAL_STRING("/MediaRenderer/AVTransport/Control", req.path.c_str());
    TEST_ASSERT_EQUAL_STRING("\"urn:schemas-upnp-org:service:AVTransport:1#Play\"", req.soapAction.c_str());
    TEST_ASSERT_EQUAL_STRING(fixtures::kPlayRequest, req.body.c_str());
}

void test_avtransport_pause_stop_info_requests() {
    TEST_ASSERT_EQUAL_STRING("\"urn:schemas-upnp-org:service:AVTransport:1#Pause\"", avtransport::pause().soapAction.c_str());
    TEST_ASSERT_NOT_EQUAL(std::string::npos, avtransport::pause().body.find("<u:Pause xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"><InstanceID>0</InstanceID></u:Pause>"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, avtransport::stop().body.find("<InstanceID>0</InstanceID></u:Stop>"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, avtransport::getTransportInfo().body.find("<InstanceID>0</InstanceID></u:GetTransportInfo>"));
}

void test_avtransport_parse_transport_info() {
    TransportState st = TransportState::Unknown;
    TEST_ASSERT_TRUE(avtransport::parseTransportInfo(fixtures::kGetTransportInfoPlaying, st));
    TEST_ASSERT_TRUE(st == TransportState::Playing);
}

void test_avtransport_parse_all_states() {
    TEST_ASSERT_TRUE(avtransport::parseTransportState("PLAYING") == TransportState::Playing);
    TEST_ASSERT_TRUE(avtransport::parseTransportState("PAUSED_PLAYBACK") == TransportState::Paused);
    TEST_ASSERT_TRUE(avtransport::parseTransportState("STOPPED") == TransportState::Stopped);
    TEST_ASSERT_TRUE(avtransport::parseTransportState("NO_MEDIA_PRESENT") == TransportState::Stopped);
    TEST_ASSERT_TRUE(avtransport::parseTransportState("TRANSITIONING") == TransportState::Transitioning);
    TEST_ASSERT_TRUE(avtransport::parseTransportState("QUATSCH") == TransportState::Unknown);
}

void test_avtransport_requests_match_recorded() {
    TEST_ASSERT_EQUAL_STRING(fixtures::kGetTransportInfoRequest, avtransport::getTransportInfo().body.c_str());
    TEST_ASSERT_EQUAL_STRING(fixtures::kPauseRequest, avtransport::pause().body.c_str());
}

void test_avtransport_play_pause_responses_are_ok() {
    TEST_ASSERT_TRUE(evaluateResponse(200, fixtures::kPauseResponse).ok);
    TEST_ASSERT_TRUE(evaluateResponse(200, fixtures::kPlayResponse).ok);
}

void test_avtransport_parse_rejects_fault() {
    TransportState st = TransportState::Playing;
    TEST_ASSERT_FALSE(avtransport::parseTransportInfo(fixtures::kFault402, st));
}

// --- Now Playing ---------------------------------------------------------------

void test_time_parse_and_format() {
    TEST_ASSERT_EQUAL_INT(225, time::parseDuration("0:03:45"));
    TEST_ASSERT_EQUAL_INT(3723, time::parseDuration("01:02:03"));
    TEST_ASSERT_EQUAL_INT(62, time::parseDuration("0:01:02.000"));
    TEST_ASSERT_EQUAL_INT(-1, time::parseDuration("NOT_IMPLEMENTED"));
    TEST_ASSERT_EQUAL_INT(-1, time::parseDuration(""));
    TEST_ASSERT_EQUAL_INT(-1, time::parseDuration("0:61:00"));
    TEST_ASSERT_EQUAL_STRING("3:45", time::format(225).c_str());
    TEST_ASSERT_EQUAL_STRING("0:07", time::format(7).c_str());
    TEST_ASSERT_EQUAL_STRING("1:02:03", time::format(3723).c_str());
    TEST_ASSERT_EQUAL_STRING("0:01:02", time::toUpnp(62).c_str());
}

void test_nowplaying_spotify_track() {
    PositionInfo pos;
    TEST_ASSERT_TRUE(parsePositionInfo(fixtures::kPositionSpotify, pos));
    TEST_ASSERT_EQUAL_INT(225, pos.durationSec);
    TEST_ASSERT_EQUAL_INT(62, pos.positionSec);
    TEST_ASSERT_EQUAL_STRING("x-sonos-spotify:spotify%3atrack%3a123?sid=12&flags=8224&sn=1", pos.trackUri.c_str());

    const NowPlaying np = buildNowPlaying(pos, nullptr);
    TEST_ASSERT_TRUE(np.kind == SourceKind::Track);
    TEST_ASSERT_EQUAL_STRING("Zu spät & zu laut", np.title.c_str());  // doppelt kodiertes & korrekt
    TEST_ASSERT_EQUAL_STRING("Die Ärzte", np.subtitle.c_str());
    TEST_ASSERT_EQUAL_STRING("Debil", np.album.c_str());
    TEST_ASSERT_EQUAL_STRING("/getaa?s=1&u=x-sonos-spotify%3aspotify%253atrack%253a123", np.albumArtUri.c_str());
    TEST_ASSERT_TRUE(np.hasProgress());
    TEST_ASSERT_TRUE(np.canSkip());
}

void test_nowplaying_radio_with_station_name() {
    PositionInfo pos;
    MediaInfo media;
    TEST_ASSERT_TRUE(parsePositionInfo(fixtures::kPositionRadio, pos));
    TEST_ASSERT_TRUE(parseMediaInfo(fixtures::kMediaRadio, media));
    const NowPlaying np = buildNowPlaying(pos, &media);
    TEST_ASSERT_TRUE(np.kind == SourceKind::Radio);
    TEST_ASSERT_EQUAL_STRING("Coldplay - Yellow", np.title.c_str());
    TEST_ASSERT_EQUAL_STRING("1LIVE", np.subtitle.c_str());
    TEST_ASSERT_FALSE(np.hasProgress());
    TEST_ASSERT_FALSE(np.canSkip());
}

void test_nowplaying_radio_without_media_hides_uri_title() {
    PositionInfo pos;
    TEST_ASSERT_TRUE(parsePositionInfo(fixtures::kPositionRadio, pos));
    const NowPlaying np = buildNowPlaying(pos, nullptr);
    TEST_ASSERT_EQUAL_STRING("Coldplay - Yellow", np.title.c_str());
    TEST_ASSERT_EQUAL_STRING("", np.subtitle.c_str());  // keine Stream-URI als Sendername
}

void test_nowplaying_radio_connecting_placeholder() {
    PositionInfo pos;
    MediaInfo media;
    TEST_ASSERT_TRUE(parsePositionInfo(fixtures::kPositionRadioConnecting, pos));
    TEST_ASSERT_TRUE(parseMediaInfo(fixtures::kMediaRadio, media));
    const NowPlaying np = buildNowPlaying(pos, &media);
    TEST_ASSERT_EQUAL_STRING("Verbinde …", np.title.c_str());
    TEST_ASSERT_EQUAL_STRING("1LIVE", np.subtitle.c_str());
}

void test_nowplaying_tv_linein_and_empty() {
    PositionInfo pos;
    TEST_ASSERT_TRUE(parsePositionInfo(fixtures::kPositionTv, pos));
    NowPlaying np = buildNowPlaying(pos, nullptr);
    TEST_ASSERT_TRUE(np.kind == SourceKind::TV);
    TEST_ASSERT_EQUAL_STRING("TV", np.title.c_str());
    TEST_ASSERT_FALSE(np.canSkip());

    TEST_ASSERT_TRUE(parsePositionInfo(fixtures::kPositionLineIn, pos));
    np = buildNowPlaying(pos, nullptr);
    TEST_ASSERT_TRUE(np.kind == SourceKind::LineIn);
    TEST_ASSERT_EQUAL_STRING("Line-In", np.title.c_str());

    TEST_ASSERT_TRUE(parsePositionInfo(fixtures::kPositionEmpty, pos));
    np = buildNowPlaying(pos, nullptr);
    TEST_ASSERT_TRUE(np.kind == SourceKind::None);
}

void test_nowplaying_tv_from_media_when_track_uri_empty() {
    PositionInfo pos;
    TEST_ASSERT_TRUE(parsePositionInfo(fixtures::kPositionEmpty, pos));
    MediaInfo media;
    media.currentUri = "x-sonos-htastream:RINCON_000000000000001400:spdif";
    NowPlaying np = buildNowPlaying(pos, &media);
    TEST_ASSERT_TRUE(np.kind == SourceKind::TV);
    TEST_ASSERT_EQUAL_STRING("TV", np.title.c_str());

    media.currentUri = "x-rincon-stream:RINCON_000000000000001400";
    np = buildNowPlaying(pos, &media);
    TEST_ASSERT_TRUE(np.kind == SourceKind::LineIn);

    // Eine gefüllte TrackURI hat Vorrang vor einer veralteten CurrentURI.
    TEST_ASSERT_TRUE(parsePositionInfo(fixtures::kPositionTv, pos));
    media.currentUri = "x-rincon-queue:RINCON_000000000000001400#0";
    np = buildNowPlaying(pos, &media);
    TEST_ASSERT_TRUE(np.kind == SourceKind::TV);
}

void test_art_split_url() {
    art::UrlParts u;
    TEST_ASSERT_TRUE(art::splitUrl("https://i.scdn.co/image/ab67616d0000b273", u));
    TEST_ASSERT_TRUE(u.https);
    TEST_ASSERT_EQUAL_STRING("i.scdn.co", u.host.c_str());
    TEST_ASSERT_EQUAL_INT(443, u.port);

    TEST_ASSERT_TRUE(art::splitUrl("http://192.168.1.10:1400/getaa?s=1&u=x", u));
    TEST_ASSERT_FALSE(u.https);
    TEST_ASSERT_EQUAL_STRING("192.168.1.10", u.host.c_str());
    TEST_ASSERT_EQUAL_INT(1400, u.port);

    TEST_ASSERT_TRUE(art::splitUrl("http://example.com?x=1", u));
    TEST_ASSERT_EQUAL_STRING("example.com", u.host.c_str());
    TEST_ASSERT_EQUAL_INT(80, u.port);

    TEST_ASSERT_FALSE(art::splitUrl("/getaa?s=1", u));
    TEST_ASSERT_FALSE(art::splitUrl("ftp://host/x", u));
    TEST_ASSERT_FALSE(art::splitUrl("http:///pfad", u));
    TEST_ASSERT_FALSE(art::splitUrl("http://host:99999/", u));
    TEST_ASSERT_FALSE(art::splitUrl("http://host:ab/", u));
}

void test_art_resolve_redirect() {
    TEST_ASSERT_EQUAL_STRING("https://cdn.example.com/b.jpg",
                             art::resolveRedirect("http://a.example.com/a.jpg", "https://cdn.example.com/b.jpg").c_str());
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.10:1400/img/b.png",
                             art::resolveRedirect("http://192.168.1.10:1400/getaa?s=1", "/img/b.png").c_str());
    TEST_ASSERT_EQUAL_STRING("https://a.example.com/b.jpg",
                             art::resolveRedirect("https://a.example.com/a.jpg", "/b.jpg").c_str());
    TEST_ASSERT_EQUAL_STRING("", art::resolveRedirect("https://a.example.com/a.jpg", "b.jpg").c_str());
    TEST_ASSERT_EQUAL_STRING("", art::resolveRedirect("https://a.example.com/a.jpg", "//evil/b.jpg").c_str());
    TEST_ASSERT_EQUAL_STRING("", art::resolveRedirect("https://a.example.com/a.jpg", "").c_str());
}

void test_nowplaying_rejects_unexpected_response() {
    PositionInfo pos;
    TEST_ASSERT_FALSE(parsePositionInfo(fixtures::kFault402, pos));
    MediaInfo media;
    TEST_ASSERT_FALSE(parseMediaInfo(fixtures::kFault402, media));
}

void test_avtransport_next_previous_seek_requests() {
    TEST_ASSERT_EQUAL_STRING("\"urn:schemas-upnp-org:service:AVTransport:1#Next\"", avtransport::next().soapAction.c_str());
    TEST_ASSERT_EQUAL_STRING("\"urn:schemas-upnp-org:service:AVTransport:1#Previous\"", avtransport::previous().soapAction.c_str());
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
        avtransport::seek(125).body.find("<InstanceID>0</InstanceID><Unit>REL_TIME</Unit><Target>0:02:05</Target></u:Seek>"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, avtransport::getPositionInfo().body.find("<InstanceID>0</InstanceID></u:GetPositionInfo>"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, avtransport::getMediaInfo().body.find("<InstanceID>0</InstanceID></u:GetMediaInfo>"));
}

void test_nowplaying_recorded_radio_dlf() {
    PositionInfo pos;
    MediaInfo media;
    TEST_ASSERT_TRUE(parsePositionInfo(fixtures::kPositionRadioDlf, pos));
    TEST_ASSERT_TRUE(parseMediaInfo(fixtures::kMediaRadioDlf, media));
    const NowPlaying np = buildNowPlaying(pos, &media);
    TEST_ASSERT_TRUE(np.kind == SourceKind::Radio);
    TEST_ASSERT_EQUAL_STRING("Microsoft verlängert Windows 10 Updates noch um ein Jahr, Kai Rüsberg", np.title.c_str());
    TEST_ASSERT_EQUAL_STRING("Deutschlandfunk Radio", np.subtitle.c_str());
    TEST_ASSERT_EQUAL_STRING("https://cdn-profiles.tunein.com/s42828/images/logoq.png?t=1", np.albumArtUri.c_str());
    TEST_ASSERT_FALSE(np.hasProgress());
}

void test_nowplaying_recorded_radio_dlf_without_media_hides_stream_filename() {
    // Regression: dc:title ist hier „stream.aac?aggregator=tunein&…“ – darf nie als Sender erscheinen.
    PositionInfo pos;
    TEST_ASSERT_TRUE(parsePositionInfo(fixtures::kPositionRadioDlf, pos));
    const NowPlaying np = buildNowPlaying(pos, nullptr);
    TEST_ASSERT_EQUAL_STRING("", np.subtitle.c_str());
    TEST_ASSERT_EQUAL_STRING("Microsoft verlängert Windows 10 Updates noch um ein Jahr, Kai Rüsberg", np.title.c_str());
}

void test_nowplaying_recorded_spotify_connect() {
    PositionInfo pos;
    MediaInfo media;
    TEST_ASSERT_TRUE(parsePositionInfo(fixtures::kPositionSpotifyConnect, pos));
    TEST_ASSERT_TRUE(parseMediaInfo(fixtures::kMediaSpotifyConnect, media));
    const NowPlaying np = buildNowPlaying(pos, &media);
    TEST_ASSERT_TRUE(np.kind == SourceKind::Track);
    TEST_ASSERT_EQUAL_STRING("Carry Me Back to Old Virginny", np.title.c_str());
    TEST_ASSERT_EQUAL_STRING("Don Shirley", np.subtitle.c_str());
    TEST_ASSERT_EQUAL_STRING("The Don Shirley Point Of View", np.album.c_str());
    TEST_ASSERT_EQUAL_STRING("https://i.scdn.co/image/ab67616d0000b273bdf478d2cbd63f86bed753bd", np.albumArtUri.c_str());
    TEST_ASSERT_EQUAL_INT(284, np.durationSec);
    TEST_ASSERT_EQUAL_INT(11, np.positionSec);
    TEST_ASSERT_TRUE(np.hasProgress());
}

void test_nowplaying_requests_match_recorded() {
    TEST_ASSERT_EQUAL_STRING(fixtures::kGetPositionInfoRequest, avtransport::getPositionInfo().body.c_str());
    TEST_ASSERT_EQUAL_STRING(fixtures::kGetMediaInfoRequest, avtransport::getMediaInfo().body.c_str());
}

// --- Topologie, SSDP, Gruppenlautstärke --------------------------------------------

void test_topology_groups_filtered_and_sorted() {
    std::vector<ZoneGroup> groups;
    TEST_ASSERT_TRUE(topology::parseZoneGroupState(fixtures::kZoneGroupState, groups));
    TEST_ASSERT_EQUAL_size_t(4, groups.size());  // ohne BOOST
    TEST_ASSERT_EQUAL_STRING("Bad & Flur", groups[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("Küche", groups[1].name.c_str());
    TEST_ASSERT_EQUAL_STRING("Schlafzimmer", groups[2].name.c_str());
    TEST_ASSERT_EQUAL_STRING("Wohnzimmer", groups[3].name.c_str());
}

void test_topology_group_with_member() {
    std::vector<ZoneGroup> groups;
    topology::parseZoneGroupState(fixtures::kZoneGroupState, groups);
    const ZoneGroup& kitchen = groups[1];
    TEST_ASSERT_EQUAL_STRING("RINCON_BBBBBBBBBBBB01400", kitchen.coordinatorUuid.c_str());
    TEST_ASSERT_EQUAL_STRING("192.168.1.20", kitchen.coordinatorIp.c_str());
    TEST_ASSERT_EQUAL_size_t(2, kitchen.members.size());
    TEST_ASSERT_EQUAL_STRING("Küche", kitchen.members[0].name.c_str());  // Koordinator zuerst
    TEST_ASSERT_EQUAL_STRING("Büro", kitchen.members[1].name.c_str());
    TEST_ASSERT_TRUE(kitchen.isGroup());
    TEST_ASSERT_EQUAL_STRING("Küche + 1", kitchen.displayName().c_str());
}

void test_topology_stereo_pair_and_satellites_count_as_one() {
    std::vector<ZoneGroup> groups;
    topology::parseZoneGroupState(fixtures::kZoneGroupState, groups);
    TEST_ASSERT_EQUAL_size_t(1, groups[2].members.size());  // Schlafzimmer (Stereopaar)
    TEST_ASSERT_EQUAL_size_t(1, groups[3].members.size());  // Wohnzimmer (Sub/Surround)
    TEST_ASSERT_FALSE(groups[3].isGroup());
    TEST_ASSERT_EQUAL_STRING("Wohnzimmer", groups[3].displayName().c_str());
}

void test_topology_find_group() {
    std::vector<ZoneGroup> groups;
    topology::parseZoneGroupState(fixtures::kZoneGroupState, groups);
    TEST_ASSERT_EQUAL_INT(1, topology::findGroupOf(groups, "RINCON_CCCCCCCCCCCC01400"));  // Büro -> Küche
    TEST_ASSERT_EQUAL_INT(-1, topology::findGroupOf(groups, "RINCON_UNBEKANNT"));
    TEST_ASSERT_EQUAL_INT(3, topology::findGroupByIp(groups, "192.168.1.10"));
    TEST_ASSERT_EQUAL_INT(-1, topology::findGroupByIp(groups, "192.168.1.11"));  // Satellit
}

void test_topology_rejects_fault() {
    std::vector<ZoneGroup> groups;
    TEST_ASSERT_FALSE(topology::parseZoneGroupState(fixtures::kFault402, groups));
}

void test_topology_ip_from_location() {
    TEST_ASSERT_EQUAL_STRING("192.168.1.50", topology::ipFromLocation("http://192.168.1.50:1400/xml/device_description.xml").c_str());
    TEST_ASSERT_EQUAL_STRING("10.0.0.2", topology::ipFromLocation("http://10.0.0.2/x").c_str());
    TEST_ASSERT_EQUAL_STRING("", topology::ipFromLocation("kaputt").c_str());
}

void test_topology_request() {
    const SoapRequest req = topology::getZoneGroupState();
    TEST_ASSERT_EQUAL_STRING("/ZoneGroupTopology/Control", req.path.c_str());
    TEST_ASSERT_EQUAL_STRING("\"urn:schemas-upnp-org:service:ZoneGroupTopology:1#GetZoneGroupState\"", req.soapAction.c_str());
}

void test_ssdp_request_and_responses() {
    const std::string req = ssdp::buildSearchRequest();
    TEST_ASSERT_NOT_EQUAL(std::string::npos, req.find("M-SEARCH * HTTP/1.1\r\n"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, req.find("ST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n"));
    TEST_ASSERT_EQUAL_STRING("\r\n\r\n", req.substr(req.size() - 4).c_str());

    std::string loc;
    TEST_ASSERT_TRUE(ssdp::parseSearchResponse(fixtures::kSsdpSonos, loc));
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.20:1400/xml/device_description.xml", loc.c_str());
    TEST_ASSERT_FALSE(ssdp::parseSearchResponse(fixtures::kSsdpOther, loc));
}

void test_group_volume_requests() {
    TEST_ASSERT_EQUAL_STRING("/MediaRenderer/GroupRenderingControl/Control", grouprendering::getGroupVolume().path.c_str());
    TEST_ASSERT_EQUAL_STRING("\"urn:schemas-upnp-org:service:GroupRenderingControl:1#SnapshotGroupVolume\"",
                             grouprendering::snapshotGroupVolume().soapAction.c_str());
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
        grouprendering::setGroupVolume(130).body.find("<InstanceID>0</InstanceID><DesiredVolume>100</DesiredVolume></u:SetGroupVolume>"));
    int v = 0;
    TEST_ASSERT_TRUE(grouprendering::parseGetGroupVolume(
        "<u:GetGroupVolumeResponse><CurrentVolume>33</CurrentVolume></u:GetGroupVolumeResponse>", v));
    TEST_ASSERT_EQUAL_INT(33, v);
}

void test_topology_recorded_installation() {
    std::vector<ZoneGroup> groups;
    TEST_ASSERT_TRUE(topology::parseZoneGroupState(fixtures::kZoneGroupStateRecorded, groups));
    TEST_ASSERT_EQUAL_size_t(7, groups.size());  // 13 Geräte, 7 Räume
    const char* expected[] = {"Bad Kinder", "Elternschlafzimmer", "Esszimmer", "Kinderzimmer 1",
                              "Kinderzimmer 2", "Kinderzimmer 3", "Wohnzimmer"};
    for (size_t i = 0; i < 7; ++i) {
        TEST_ASSERT_EQUAL_STRING(expected[i], groups[i].displayName().c_str());
        TEST_ASSERT_EQUAL_size_t(1, groups[i].members.size());  // keine Satelliten/Stereo-Zweitgeräte
    }
    TEST_ASSERT_EQUAL_STRING("192.168.178.118", groups[6].coordinatorIp.c_str());  // Wohnzimmer
    TEST_ASSERT_EQUAL_STRING("192.168.178.69", groups[2].coordinatorIp.c_str());   // Esszimmer (Stereopaar)
    TEST_ASSERT_EQUAL_INT(-1, topology::findGroupByIp(groups, "192.168.178.110"));  // Sub Mini
    TEST_ASSERT_EQUAL_INT(6, topology::findGroupByIp(groups, "192.168.178.118"));
}

void test_topology_find_group_by_name() {
    std::vector<ZoneGroup> groups;
    topology::parseZoneGroupState(fixtures::kZoneGroupStateRecorded, groups);
    TEST_ASSERT_EQUAL_INT(6, topology::findGroupByName(groups, "Wohnzimmer"));
    TEST_ASSERT_EQUAL_INT(6, topology::findGroupByName(groups, "  wohnzimmer "));
    TEST_ASSERT_EQUAL_INT(-1, topology::findGroupByName(groups, "Garage"));
    TEST_ASSERT_EQUAL_INT(-1, topology::findGroupByName(groups, ""));
    // Raum in einer Gruppe: gefunden wird die Gruppe (synthetische Anlage: Büro gehört zur Küche)
    std::vector<ZoneGroup> synthetic;
    topology::parseZoneGroupState(fixtures::kZoneGroupState, synthetic);
    TEST_ASSERT_EQUAL_INT(1, topology::findGroupByName(synthetic, "Büro"));
}

void test_topology_request_matches_recorded() {
    TEST_ASSERT_EQUAL_STRING(fixtures::kGetZoneGroupStateRequest, topology::getZoneGroupState().body.c_str());
}

void test_topology_invisible_coordinator_keeps_room() {
    // Stereopaar, bei dem der unsichtbare Lautsprecher Koordinator ist
    const std::string inner =
        "<ZoneGroupState><ZoneGroups><ZoneGroup Coordinator=\"RINCON_B\">"
        "<ZoneGroupMember UUID=\"RINCON_A\" Location=\"http://10.0.0.1:1400/x\" ZoneName=\"Esszimmer\"/>"
        "<ZoneGroupMember UUID=\"RINCON_B\" Location=\"http://10.0.0.2:1400/x\" ZoneName=\"Esszimmer\" Invisible=\"1\"/>"
        "</ZoneGroup></ZoneGroups></ZoneGroupState>";
    const std::string body = "<ZoneGroupState>" + xml::escape(inner) + "</ZoneGroupState>";
    std::vector<ZoneGroup> groups;
    TEST_ASSERT_TRUE(topology::parseZoneGroupState(body, groups));
    TEST_ASSERT_EQUAL_size_t(1, groups.size());
    TEST_ASSERT_EQUAL_STRING("Esszimmer", groups[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("10.0.0.2", groups[0].coordinatorIp.c_str());  // Befehle an den Koordinator
}

// --- Albumcover: Kandidaten je Dienst ------------------------------------------------

void test_art_queue_track_uses_speaker_proxy() {
    PositionInfo pos;
    TEST_ASSERT_TRUE(parsePositionInfo(fixtures::kPositionSpotify, pos));  // Warteschlange, relatives /getaa
    const auto urls = art::candidates(buildNowPlaying(pos, nullptr), "192.168.178.118");
    TEST_ASSERT_TRUE(urls.size() >= 1);
    TEST_ASSERT_EQUAL_STRING("http://192.168.178.118:1400/getaa?s=1&u=x-sonos-spotify%3aspotify%253atrack%253a123",
                             urls[0].c_str());
    // zweiter Versuch: Speaker sucht das Cover zur Titel-URI
    TEST_ASSERT_EQUAL_size_t(2, urls.size());
    TEST_ASSERT_EQUAL_STRING(
        "http://192.168.178.118:1400/getaa?s=1&u=x-sonos-spotify%3Aspotify%253atrack%253a123%3Fsid%3D12%26flags%3D8224%26sn%3D1",
        urls[1].c_str());
}

void test_art_spotify_connect_uses_https_directly() {
    PositionInfo pos;
    MediaInfo media;
    TEST_ASSERT_TRUE(parsePositionInfo(fixtures::kPositionSpotifyConnect, pos));
    TEST_ASSERT_TRUE(parseMediaInfo(fixtures::kMediaSpotifyConnect, media));
    const auto urls = art::candidates(buildNowPlaying(pos, &media), "192.168.178.118");
    TEST_ASSERT_EQUAL_size_t(1, urls.size());  // kein /getaa-Versuch bei x-sonos-vli
    TEST_ASSERT_EQUAL_STRING("https://i.scdn.co/image/ab67616d0000b273bdf478d2cbd63f86bed753bd", urls[0].c_str());
}

void test_art_radio_uses_station_logo() {
    PositionInfo pos;
    MediaInfo media;
    TEST_ASSERT_TRUE(parsePositionInfo(fixtures::kPositionRadioDlf, pos));
    TEST_ASSERT_TRUE(parseMediaInfo(fixtures::kMediaRadioDlf, media));
    const auto urls = art::candidates(buildNowPlaying(pos, &media), "192.168.178.118");
    TEST_ASSERT_EQUAL_size_t(1, urls.size());
    TEST_ASSERT_EQUAL_STRING("https://cdn-profiles.tunein.com/s42828/images/logoq.png?t=1", urls[0].c_str());
}

void test_art_tv_linein_empty_have_no_cover() {
    PositionInfo pos;
    TEST_ASSERT_TRUE(parsePositionInfo(fixtures::kPositionTv, pos));
    TEST_ASSERT_EQUAL_size_t(0, art::candidates(buildNowPlaying(pos, nullptr), "10.0.0.1").size());
    TEST_ASSERT_TRUE(parsePositionInfo(fixtures::kPositionLineIn, pos));
    TEST_ASSERT_EQUAL_size_t(0, art::candidates(buildNowPlaying(pos, nullptr), "10.0.0.1").size());
    TEST_ASSERT_TRUE(parsePositionInfo(fixtures::kPositionEmpty, pos));
    TEST_ASSERT_EQUAL_size_t(0, art::candidates(buildNowPlaying(pos, nullptr), "10.0.0.1").size());
}

void test_art_preferred_size_rewrites() {
    // Apple Music: Größe + WebP -> 480 px JPEG
    TEST_ASSERT_EQUAL_STRING(
        "https://is1-ssl.mzstatic.com/image/thumb/Music126/v4/ab/cd/ef/abc.jpg/480x480bb.jpg",
        art::preferredSize("https://is1-ssl.mzstatic.com/image/thumb/Music126/v4/ab/cd/ef/abc.jpg/3000x3000bb.webp").c_str());
    // Deezer
    TEST_ASSERT_EQUAL_STRING("https://e-cdns-images.dzcdn.net/images/cover/0123abcd/500x500-000000-80-0-0.jpg",
        art::preferredSize("https://e-cdns-images.dzcdn.net/images/cover/0123abcd/1000x1000-000000-80-0-0.jpg").c_str());
    // Unbekannte Dienste bleiben unverändert
    TEST_ASSERT_EQUAL_STRING("https://i.scdn.co/image/ab67616d0000b273x", art::preferredSize("https://i.scdn.co/image/ab67616d0000b273x").c_str());
}

void test_art_rewritten_url_keeps_original_as_fallback() {
    NowPlaying np;
    np.kind = SourceKind::Track;
    np.albumArtUri = "https://is1-ssl.mzstatic.com/image/thumb/x/1200x1200bb.jpg";
    np.trackUri = "x-sonos-http:song%3a1.mp4?sid=204";
    const auto urls = art::candidates(np, "10.0.0.5");
    TEST_ASSERT_EQUAL_size_t(3, urls.size());
    TEST_ASSERT_EQUAL_STRING("https://is1-ssl.mzstatic.com/image/thumb/x/480x480bb.jpg", urls[0].c_str());
    TEST_ASSERT_EQUAL_STRING("https://is1-ssl.mzstatic.com/image/thumb/x/1200x1200bb.jpg", urls[1].c_str());
    TEST_ASSERT_EQUAL_STRING("http://10.0.0.5:1400/getaa?s=1&u=x-sonos-http%3Asong%253a1.mp4%3Fsid%3D204", urls[2].c_str());
}

void test_art_url_encode() {
    TEST_ASSERT_EQUAL_STRING("a-b_c.d~e", art::urlEncode("a-b_c.d~e").c_str());
    TEST_ASSERT_EQUAL_STRING("x%3Ay%3Fz%3D1%261%20%C3%A4", art::urlEncode("x:y?z=1&1 \xC3\xA4").c_str());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_xml_find_element_ignores_namespace_prefix);
    RUN_TEST(test_xml_find_element_missing);
    RUN_TEST(test_xml_find_element_does_not_match_prefix_of_longer_name);
    RUN_TEST(test_xml_find_element_with_attributes_and_self_closing);
    RUN_TEST(test_xml_escape_and_unescape_roundtrip);
    RUN_TEST(test_xml_unescape_numeric_entities_to_utf8);
    RUN_TEST(test_soap_set_volume_request_is_exact);
    RUN_TEST(test_soap_set_volume_is_clamped);
    RUN_TEST(test_soap_get_volume_request_matches_recorded);
    RUN_TEST(test_soap_arguments_are_escaped);
    RUN_TEST(test_soap_evaluate_ok);
    RUN_TEST(test_soap_evaluate_fault_with_upnp_code);
    RUN_TEST(test_soap_evaluate_http_error_without_body);
    RUN_TEST(test_soap_evaluate_connection_errors);
    RUN_TEST(test_parse_get_volume);
    RUN_TEST(test_parse_get_volume_rejects_garbage);
    RUN_TEST(test_avtransport_play_request_is_exact);
    RUN_TEST(test_avtransport_pause_stop_info_requests);
    RUN_TEST(test_avtransport_parse_transport_info);
    RUN_TEST(test_avtransport_parse_all_states);
    RUN_TEST(test_avtransport_requests_match_recorded);
    RUN_TEST(test_avtransport_play_pause_responses_are_ok);
    RUN_TEST(test_avtransport_parse_rejects_fault);
    RUN_TEST(test_time_parse_and_format);
    RUN_TEST(test_nowplaying_spotify_track);
    RUN_TEST(test_nowplaying_radio_with_station_name);
    RUN_TEST(test_nowplaying_radio_without_media_hides_uri_title);
    RUN_TEST(test_nowplaying_radio_connecting_placeholder);
    RUN_TEST(test_nowplaying_tv_linein_and_empty);
    RUN_TEST(test_nowplaying_tv_from_media_when_track_uri_empty);
    RUN_TEST(test_nowplaying_rejects_unexpected_response);
    RUN_TEST(test_art_split_url);
    RUN_TEST(test_art_resolve_redirect);
    RUN_TEST(test_avtransport_next_previous_seek_requests);
    RUN_TEST(test_nowplaying_recorded_radio_dlf);
    RUN_TEST(test_nowplaying_recorded_radio_dlf_without_media_hides_stream_filename);
    RUN_TEST(test_nowplaying_recorded_spotify_connect);
    RUN_TEST(test_nowplaying_requests_match_recorded);
    RUN_TEST(test_topology_groups_filtered_and_sorted);
    RUN_TEST(test_topology_group_with_member);
    RUN_TEST(test_topology_stereo_pair_and_satellites_count_as_one);
    RUN_TEST(test_topology_find_group);
    RUN_TEST(test_topology_rejects_fault);
    RUN_TEST(test_topology_ip_from_location);
    RUN_TEST(test_topology_request);
    RUN_TEST(test_ssdp_request_and_responses);
    RUN_TEST(test_group_volume_requests);
    RUN_TEST(test_topology_recorded_installation);
    RUN_TEST(test_topology_find_group_by_name);
    RUN_TEST(test_topology_request_matches_recorded);
    RUN_TEST(test_topology_invisible_coordinator_keeps_room);
    RUN_TEST(test_art_queue_track_uses_speaker_proxy);
    RUN_TEST(test_art_spotify_connect_uses_https_directly);
    RUN_TEST(test_art_radio_uses_station_logo);
    RUN_TEST(test_art_tv_linein_empty_have_no_cover);
    RUN_TEST(test_art_preferred_size_rewrites);
    RUN_TEST(test_art_rewritten_url_keeps_original_as_fallback);
    RUN_TEST(test_art_url_encode);
    return UNITY_END();
}
