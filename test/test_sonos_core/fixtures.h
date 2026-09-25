#pragma once

// Sonos-Antworten als Testdaten.
//
// Herkunft:
//   [synthetisch]   nach dem bekannten UPnP-Format nachgebaut
//   [aufgezeichnet] mit tools/sonos_probe.py an einem echten Speaker mitgeschnitten
//
// Aufgezeichnete Antworten ersetzen nach und nach die synthetischen (T3, siehe docs/TESTEN.md).

namespace fixtures {

// [aufgezeichnet] GetVolume, Antwort HTTP 200 – Sonos S2, 2026-09-25 (sonos_probe.py --save)
static const char* kGetVolumeResponse =
    R"(<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">)"
    R"(<s:Body><u:GetVolumeResponse xmlns:u="urn:schemas-upnp-org:service:RenderingControl:1">)"
    R"(<CurrentVolume>12</CurrentVolume></u:GetVolumeResponse></s:Body></s:Envelope>)";

// [aufgezeichnet] GetVolume, Anfrage wie von sonos_probe.py gesendet – die Firmware muss byte-gleich senden.
static const char* kGetVolumeRequest =
    R"(<?xml version="1.0" encoding="utf-8"?>)"
    R"(<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">)"
    R"(<s:Body><u:GetVolume xmlns:u="urn:schemas-upnp-org:service:RenderingControl:1">)"
    R"(<InstanceID>0</InstanceID><Channel>Master</Channel></u:GetVolume></s:Body></s:Envelope>)";

// [synthetisch] SetVolume, Antwort HTTP 200
static const char* kSetVolumeResponse =
    R"(<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">)"
    R"(<s:Body><u:SetVolumeResponse xmlns:u="urn:schemas-upnp-org:service:RenderingControl:1"></u:SetVolumeResponse>)"
    R"(</s:Body></s:Envelope>)";

// [synthetisch] SOAP-Fault, Antwort HTTP 500
static const char* kFault402 =
    R"(<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">)"
    R"(<s:Body><s:Fault><faultcode>s:Client</faultcode><faultstring>UPnPError</faultstring>)"
    R"(<detail><UPnPError xmlns="urn:schemas-upnp-org:control-1-0"><errorCode>402</errorCode></UPnPError></detail>)"
    R"(</s:Fault></s:Body></s:Envelope>)";

// Erwartete Anfrage für SetVolume(25) – muss byte-genau mit tools/sonos_probe.py übereinstimmen.
static const char* kSetVolume25Request =
    R"(<?xml version="1.0" encoding="utf-8"?>)"
    R"(<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">)"
    R"(<s:Body><u:SetVolume xmlns:u="urn:schemas-upnp-org:service:RenderingControl:1">)"
    R"(<InstanceID>0</InstanceID><Channel>Master</Channel><DesiredVolume>25</DesiredVolume>)"
    R"(</u:SetVolume></s:Body></s:Envelope>)";

// [synthetisch] GetTransportInfo, Antwort HTTP 200 (Format laut UPnP AVTransport:1)
static const char* kGetTransportInfoPlaying =
    R"(<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">)"
    R"(<s:Body><u:GetTransportInfoResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">)"
    R"(<CurrentTransportState>PLAYING</CurrentTransportState><CurrentTransportStatus>OK</CurrentTransportStatus>)"
    R"(<CurrentSpeed>1</CurrentSpeed></u:GetTransportInfoResponse></s:Body></s:Envelope>)";

// Erwartete Anfrage für Play – muss byte-genau mit tools/sonos_probe.py übereinstimmen.
static const char* kPlayRequest =
    R"(<?xml version="1.0" encoding="utf-8"?>)"
    R"(<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">)"
    R"(<s:Body><u:Play xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">)"
    R"(<InstanceID>0</InstanceID><Speed>1</Speed></u:Play></s:Body></s:Envelope>)";

}  // namespace fixtures
