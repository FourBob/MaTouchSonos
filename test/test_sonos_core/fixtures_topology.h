#pragma once

// Topologie-Testdaten (GetZoneGroupState) und SSDP-Antworten.
// [synthetisch] nach dem bekannten Sonos-S2-Format nachgebaut – wird durch eine Aufzeichnung
// deiner Anlage ergänzt (tools/sonos_probe.py --save … topology).
//
// Nachgebaute Anlage:
//   Wohnzimmer   Soundbar mit Sub + Surround (Satelliten, unsichtbar)
//   Küche + Büro Gruppe, Koordinator Küche (steht in der Antwort absichtlich an zweiter Stelle)
//   Schlafzimmer Stereopaar (zweiter Lautsprecher Invisible="1")
//   BOOST        Bridge/Boost (IsZoneBridge="1") – darf nicht erscheinen
//   Bad & Flur   Name mit Sonderzeichen

namespace fixtures {

static const char* kZoneGroupState =
    R"~(<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/so)~"
    R"~(ap/encoding/"><s:Body><u:GetZoneGroupStateResponse xmlns:u="urn:schemas-upnp-org:service:ZoneGroupTopology:1">)~"
    R"~(<ZoneGroupState>&lt;ZoneGroupState&gt;&lt;ZoneGroups&gt;&lt;ZoneGroup Coordinator=&quot;RINCON_AAAAAAAAAAAA014)~"
    R"~(00&quot; ID=&quot;RINCON_AAAAAAAAAAAA01400:1&quot;&gt;&lt;ZoneGroupMember UUID=&quot;RINCON_AAAAAAAAAAAA01400&)~"
    R"~(quot; Location=&quot;http://192.168.1.10:1400/xml/device_description.xml&quot; ZoneName=&quot;Wohnzimmer&quot;)~"
    R"~( HTSatChanMapSet=&quot;x&quot;&gt;&lt;Satellite UUID=&quot;RINCON_SUB000000000001400&quot; Location=&quot;http)~"
    R"~(://192.168.1.11:1400/xml/device_description.xml&quot; ZoneName=&quot;Wohnzimmer&quot; Invisible=&quot;1&quot;/)~"
    R"~(&gt;&lt;Satellite UUID=&quot;RINCON_SURL00000000001400&quot; Location=&quot;http://192.168.1.12:1400/xml/devic)~"
    R"~(e_description.xml&quot; ZoneName=&quot;Wohnzimmer&quot; Invisible=&quot;1&quot;/&gt;&lt;/ZoneGroupMember&gt;&l)~"
    R"~(t;/ZoneGroup&gt;&lt;ZoneGroup Coordinator=&quot;RINCON_BBBBBBBBBBBB01400&quot; ID=&quot;RINCON_BBBBBBBBBBBB014)~"
    R"~(00:7&quot;&gt;&lt;ZoneGroupMember UUID=&quot;RINCON_CCCCCCCCCCCC01400&quot; Location=&quot;http://192.168.1.21)~"
    R"~(:1400/xml/device_description.xml&quot; ZoneName=&quot;Büro&quot;/&gt;&lt;ZoneGroupMember UUID=&quot;RINCON_BBB)~"
    R"~(BBBBBBBBB01400&quot; Location=&quot;http://192.168.1.20:1400/xml/device_description.xml&quot; ZoneName=&quot;K)~"
    R"~(üche&quot;/&gt;&lt;/ZoneGroup&gt;&lt;ZoneGroup Coordinator=&quot;RINCON_DDDDDDDDDDDD01400&quot; ID=&quot;RINCO)~"
    R"~(N_DDDDDDDDDDDD01400:3&quot;&gt;&lt;ZoneGroupMember UUID=&quot;RINCON_DDDDDDDDDDDD01400&quot; Location=&quot;ht)~"
    R"~(tp://192.168.1.30:1400/xml/device_description.xml&quot; ZoneName=&quot;Schlafzimmer&quot; ChannelMapSet=&quot;)~"
    R"~(a&quot;/&gt;&lt;ZoneGroupMember UUID=&quot;RINCON_EEEEEEEEEEEE01400&quot; Location=&quot;http://192.168.1.31:1)~"
    R"~(400/xml/device_description.xml&quot; ZoneName=&quot;Schlafzimmer&quot; ChannelMapSet=&quot;a&quot; Invisible=&)~"
    R"~(quot;1&quot;/&gt;&lt;/ZoneGroup&gt;&lt;ZoneGroup Coordinator=&quot;RINCON_FFFFFFFFFFFF01400&quot; ID=&quot;RIN)~"
    R"~(CON_FFFFFFFFFFFF01400:9&quot;&gt;&lt;ZoneGroupMember UUID=&quot;RINCON_FFFFFFFFFFFF01400&quot; Location=&quot;)~"
    R"~(http://192.168.1.40:1400/xml/device_description.xml&quot; ZoneName=&quot;BOOST&quot; IsZoneBridge=&quot;1&quot)~"
    R"~(;/&gt;&lt;/ZoneGroup&gt;&lt;ZoneGroup Coordinator=&quot;RINCON_GGGGGGGGGGGG01400&quot; ID=&quot;RINCON_GGGGGGG)~"
    R"~(GGGGG01400:4&quot;&gt;&lt;ZoneGroupMember UUID=&quot;RINCON_GGGGGGGGGGGG01400&quot; Location=&quot;http://192.)~"
    R"~(168.1.50:1400/xml/device_description.xml&quot; ZoneName=&quot;Bad &amp;amp; Flur&quot;/&gt;&lt;/ZoneGroup&gt;&)~"
    R"~(lt;/ZoneGroups&gt;&lt;VanishedDevices&gt;&lt;/VanishedDevices&gt;&lt;/ZoneGroupState&gt;</ZoneGroupState></u:G)~"
    R"~(etZoneGroupStateResponse></s:Body></s:Envelope>)~";

// [synthetisch] SSDP-Antwort eines Sonos-Speakers auf M-SEARCH
static const char* kSsdpSonos =
    "HTTP/1.1 200 OK\r\n"
    "CACHE-CONTROL: max-age = 1800\r\n"
    "EXT:\r\n"
    "LOCATION: http://192.168.1.20:1400/xml/device_description.xml\r\n"
    "SERVER: Linux UPnP/1.0 Sonos/80.1-55014 (ZPS27)\r\n"
    "ST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n"
    "USN: uuid:RINCON_BBBBBBBBBBBB01400::urn:schemas-upnp-org:device:ZonePlayer:1\r\n"
    "X-RINCON-HOUSEHOLD: Sonos_abc\r\n"
    "\r\n";

// [synthetisch] SSDP-Antwort eines anderen Geräts (z. B. Router)
static const char* kSsdpOther =
    "HTTP/1.1 200 OK\r\n"
    "Location: http://192.168.1.1:49000/igddesc.xml\r\n"
    "SERVER: FRITZ!Box UPnP/1.0\r\n"
    "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
    "\r\n";

}  // namespace fixtures
