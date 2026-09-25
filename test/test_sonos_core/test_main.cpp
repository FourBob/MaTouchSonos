// Unit-Tests für das Sonos-Protokoll (lib/sonos_core).
// Ausführen mit:  pio test -e native

#include <unity.h>

#include <string>

#include "AVTransport.h"
#include "RenderingControl.h"
#include "Soap.h"
#include "Xml.h"
#include "fixtures.h"

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

void test_avtransport_parse_rejects_fault() {
    TransportState st = TransportState::Playing;
    TEST_ASSERT_FALSE(avtransport::parseTransportInfo(fixtures::kFault402, st));
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
    RUN_TEST(test_avtransport_parse_rejects_fault);
    return UNITY_END();
}
