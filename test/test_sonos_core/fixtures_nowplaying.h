#pragma once

// Now-Playing-Testdaten (GetPositionInfo / GetMediaInfo).
//
// [synthetisch]   nach dem bekannten Sonos-Format nachgebaut
// [aufgezeichnet] mit tools/sonos_probe.py --save … nowplaying an einem echten Speaker
// Synthetische Daten werden nach und nach durch Aufzeichnungen ergänzt bzw. ersetzt.
// Wichtig: TrackMetaData ist XML *in* XML, also doppelt kodiert (&lt; … &amp;amp;).

namespace fixtures {

#define SONOS_ENV_OPEN                                                                                         \
    R"(<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/"><s:Body>)"
#define SONOS_ENV_CLOSE R"(</s:Body></s:Envelope>)"
#define DIDL_OPEN                                                                                               \
    R"(&lt;DIDL-Lite xmlns:dc=&quot;http://purl.org/dc/elements/1.1/&quot; xmlns:upnp=&quot;urn:schemas-upnp-org:metadata-1-0/upnp/&quot; )" \
    R"(xmlns:r=&quot;urn:schemas-rinconnetworks-com:metadata-1-0/&quot; xmlns=&quot;urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/&quot;&gt;)"

// [synthetisch] Spotify-Titel, Position 1:02 von 3:45, mit Umlauten und & im Titel
static const char* kPositionSpotify =
    SONOS_ENV_OPEN
    R"(<u:GetPositionInfoResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">)"
    R"(<Track>3</Track><TrackDuration>0:03:45</TrackDuration><TrackMetaData>)"
    DIDL_OPEN
    R"(&lt;item id=&quot;-1&quot; parentID=&quot;-1&quot; restricted=&quot;true&quot;&gt;)"
    R"(&lt;res protocolInfo=&quot;sonos.com-spotify:*:audio/x-spotify:*&quot; duration=&quot;0:03:45&quot;&gt;x-sonos-spotify:spotify%3atrack%3a123?sid=12&amp;amp;flags=8224&amp;amp;sn=1&lt;/res&gt;)"
    R"(&lt;r:streamContent&gt;&lt;/r:streamContent&gt;)"
    R"(&lt;upnp:albumArtURI&gt;/getaa?s=1&amp;amp;u=x-sonos-spotify%3aspotify%253atrack%253a123&lt;/upnp:albumArtURI&gt;)"
    R"(&lt;dc:title&gt;Zu spät &amp;amp; zu laut&lt;/dc:title&gt;&lt;upnp:class&gt;object.item.audioItem.musicTrack&lt;/upnp:class&gt;)"
    R"(&lt;dc:creator&gt;Die Ärzte&lt;/dc:creator&gt;&lt;upnp:album&gt;Debil&lt;/upnp:album&gt;&lt;/item&gt;&lt;/DIDL-Lite&gt;)"
    R"(</TrackMetaData><TrackURI>x-sonos-spotify:spotify%3atrack%3a123?sid=12&amp;flags=8224&amp;sn=1</TrackURI>)"
    R"(<RelTime>0:01:02</RelTime><AbsTime>NOT_IMPLEMENTED</AbsTime><RelCount>2147483647</RelCount><AbsCount>2147483647</AbsCount>)"
    R"(</u:GetPositionInfoResponse>)"
    SONOS_ENV_CLOSE;

// [synthetisch] Radio (TuneIn): Länge 0, dc:title enthält die Stream-URI, streamContent den laufenden Titel
static const char* kPositionRadio =
    SONOS_ENV_OPEN
    R"(<u:GetPositionInfoResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">)"
    R"(<Track>1</Track><TrackDuration>0:00:00</TrackDuration><TrackMetaData>)"
    DIDL_OPEN
    R"(&lt;item id=&quot;-1&quot; parentID=&quot;-1&quot; restricted=&quot;true&quot;&gt;)"
    R"(&lt;res protocolInfo=&quot;x-rincon-mp3radio:*:*:*&quot;&gt;x-rincon-mp3radio://example.org/stream.mp3&lt;/res&gt;)"
    R"(&lt;r:streamContent&gt;Coldplay - Yellow&lt;/r:streamContent&gt;)"
    R"(&lt;dc:title&gt;x-sonosapi-stream:s24896?sid=254&amp;amp;flags=8224&amp;amp;sn=0&lt;/dc:title&gt;)"
    R"(&lt;upnp:class&gt;object.item&lt;/upnp:class&gt;&lt;/item&gt;&lt;/DIDL-Lite&gt;)"
    R"(</TrackMetaData><TrackURI>x-sonosapi-stream:s24896?sid=254&amp;flags=8224&amp;sn=0</TrackURI>)"
    R"(<RelTime>0:12:34</RelTime><AbsTime>NOT_IMPLEMENTED</AbsTime><RelCount>2147483647</RelCount><AbsCount>2147483647</AbsCount>)"
    R"(</u:GetPositionInfoResponse>)"
    SONOS_ENV_CLOSE;

// [synthetisch] Radio beim Verbinden (Sonos-Platzhalter)
static const char* kPositionRadioConnecting =
    SONOS_ENV_OPEN
    R"(<u:GetPositionInfoResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">)"
    R"(<Track>1</Track><TrackDuration>0:00:00</TrackDuration><TrackMetaData>)"
    DIDL_OPEN
    R"(&lt;item id=&quot;-1&quot; parentID=&quot;-1&quot; restricted=&quot;true&quot;&gt;)"
    R"(&lt;r:streamContent&gt;ZPSTR_CONNECTING&lt;/r:streamContent&gt;&lt;dc:title&gt;ZPSTR_CONNECTING&lt;/dc:title&gt;)"
    R"(&lt;/item&gt;&lt;/DIDL-Lite&gt;)"
    R"(</TrackMetaData><TrackURI>x-sonosapi-stream:s24896?sid=254</TrackURI>)"
    R"(<RelTime>0:00:00</RelTime><AbsTime>NOT_IMPLEMENTED</AbsTime>)"
    R"(</u:GetPositionInfoResponse>)"
    SONOS_ENV_CLOSE;

// [synthetisch] GetMediaInfo bei Radio: Sendername in CurrentURIMetaData
static const char* kMediaRadio =
    SONOS_ENV_OPEN
    R"(<u:GetMediaInfoResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">)"
    R"(<NrTracks>1</NrTracks><MediaDuration>NOT_IMPLEMENTED</MediaDuration>)"
    R"(<CurrentURI>x-sonosapi-stream:s24896?sid=254&amp;flags=8224&amp;sn=0</CurrentURI><CurrentURIMetaData>)"
    DIDL_OPEN
    R"(&lt;item id=&quot;-1&quot; parentID=&quot;-1&quot; restricted=&quot;true&quot;&gt;&lt;dc:title&gt;1LIVE&lt;/dc:title&gt;)"
    R"(&lt;upnp:class&gt;object.item.audioItem.audioBroadcast&lt;/upnp:class&gt;&lt;/item&gt;&lt;/DIDL-Lite&gt;)"
    R"(</CurrentURIMetaData><NextURI></NextURI><NextURIMetaData></NextURIMetaData>)"
    R"(<PlayMedium>NETWORK</PlayMedium><RecordMedium>NOT_IMPLEMENTED</RecordMedium><WriteStatus>NOT_IMPLEMENTED</WriteStatus>)"
    R"(</u:GetMediaInfoResponse>)"
    SONOS_ENV_CLOSE;

// [synthetisch] TV-Eingang einer Soundbar
static const char* kPositionTv =
    SONOS_ENV_OPEN
    R"(<u:GetPositionInfoResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">)"
    R"(<Track>1</Track><TrackDuration>NOT_IMPLEMENTED</TrackDuration><TrackMetaData>NOT_IMPLEMENTED</TrackMetaData>)"
    R"(<TrackURI>x-sonos-htastream:RINCON_000E58000000001400:spdif</TrackURI><RelTime>NOT_IMPLEMENTED</RelTime>)"
    R"(<AbsTime>NOT_IMPLEMENTED</AbsTime></u:GetPositionInfoResponse>)"
    SONOS_ENV_CLOSE;

// [synthetisch] Line-In
static const char* kPositionLineIn =
    SONOS_ENV_OPEN
    R"(<u:GetPositionInfoResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">)"
    R"(<Track>1</Track><TrackDuration>0:00:00</TrackDuration><TrackMetaData></TrackMetaData>)"
    R"(<TrackURI>x-rincon-stream:RINCON_000E58000000001400</TrackURI><RelTime>0:00:00</RelTime>)"
    R"(<AbsTime>NOT_IMPLEMENTED</AbsTime></u:GetPositionInfoResponse>)"
    SONOS_ENV_CLOSE;

// [synthetisch] Leere Warteschlange
static const char* kPositionEmpty =
    SONOS_ENV_OPEN
    R"(<u:GetPositionInfoResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">)"
    R"(<Track>0</Track><TrackDuration>NOT_IMPLEMENTED</TrackDuration><TrackMetaData>NOT_IMPLEMENTED</TrackMetaData>)"
    R"(<TrackURI></TrackURI><RelTime>NOT_IMPLEMENTED</RelTime><AbsTime>NOT_IMPLEMENTED</AbsTime>)"
    R"(</u:GetPositionInfoResponse>)"
    SONOS_ENV_CLOSE;

// GetPositionInfo, Radio – [aufgezeichnet] Sonos S2, 2026-09-25, Deutschlandfunk über TuneIn
// Token/Sitzungskennungen der Stream-URL geschwärzt (öffentliches Repo).
static const char* kPositionRadioDlf =
    R"(<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">)"
    R"(<s:Body><u:GetPositionInfoResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"><Track>1</Track>)"
    R"(<TrackDuration>0:00:00</TrackDuration>)"
    R"(<TrackMetaData>&lt;DIDL-Lite xmlns:dc=&quot;http://purl.org/dc/elements/1.1/&quot; xmlns:upnp=&quot;urn:schemas-upnp-org:metadata-1-0/upnp/&quot; xmlns:r=&quot;urn:schemas-rinconnetworks-com:metadata-1-0/&quot; xmlns=&quot;urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/&quot;&gt;)"
    R"(&lt;item id=&quot;-1&quot; parentID=&quot;-1&quot; restricted=&quot;true&quot;&gt;)"
    R"(&lt;res protocolInfo=&quot;aac:*:application/octet-stream:*&quot;&gt;aac://https://d121.rndfnk.com/ard/dlf/01/aac/192/stream.aac?aggregator=tunein&amp;amp;cid=REDACTED&amp;amp;sid=REDACTED&amp;amp;token=REDACTED&amp;amp;tvf=REDACTED&lt;/res&gt;)"
    R"(&lt;r:streamContent&gt;Microsoft verlängert Windows 10 Updates noch um ein Jahr, Kai Rüsberg&lt;/r:streamContent&gt;)"
    R"(&lt;dc:title&gt;stream.aac?aggregator=tunein&amp;amp;cid=REDACTED&amp;amp;sid=REDACTED&amp;amp;token=REDACTED&amp;amp;tvf=REDACTED&lt;/dc:title&gt;)"
    R"(&lt;upnp:class&gt;object.item&lt;/upnp:class&gt;&lt;/item&gt;&lt;/DIDL-Lite&gt;</TrackMetaData>)"
    R"(<TrackURI>aac://https://d121.rndfnk.com/ard/dlf/01/aac/192/stream.aac?aggregator=tunein&amp;cid=REDACTED&amp;sid=REDACTED&amp;token=REDACTED&amp;tvf=REDACTED</TrackURI>)"
    R"(<RelTime>0:00:00</RelTime><AbsTime>NOT_IMPLEMENTED</AbsTime><RelCount>2147483647</RelCount>)"
    R"(<AbsCount>2147483647</AbsCount></u:GetPositionInfoResponse></s:Body></s:Envelope>)";

// GetMediaInfo, Radio – [aufgezeichnet] Sonos S2, 2026-09-25, Deutschlandfunk über TuneIn
static const char* kMediaRadioDlf =
    R"(<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">)"
    R"(<s:Body><u:GetMediaInfoResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"><NrTracks>4</NrTracks>)"
    R"(<MediaDuration>NOT_IMPLEMENTED</MediaDuration>)"
    R"(<CurrentURI>x-sonosapi-stream:s55737?sid=333&amp;flags=8232&amp;sn=11</CurrentURI>)"
    R"(<CurrentURIMetaData>&lt;DIDL-Lite xmlns:dc=&quot;http://purl.org/dc/elements/1.1/&quot; xmlns:upnp=&quot;urn:schemas-upnp-org:metadata-1-0/upnp/&quot; xmlns:r=&quot;urn:schemas-rinconnetworks-com:metadata-1-0/&quot; xmlns=&quot;urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/&quot;&gt;)"
    R"(&lt;item id=&quot;-1&quot; parentID=&quot;-1&quot; restricted=&quot;true&quot;&gt;)"
    R"(&lt;dc:title&gt;Deutschlandfunk Radio&lt;/dc:title&gt;)"
    R"(&lt;upnp:class&gt;object.item.audioItem.audioBroadcast&lt;/upnp:class&gt;)"
    R"(&lt;desc id=&quot;cdudn&quot; nameSpace=&quot;urn:schemas-rinconnetworks-com:metadata-1-0/&quot;&gt;SA_RINCON85255_X_#Svc85255-0-Token&lt;/desc&gt;)"
    R"(&lt;upnp:albumArtURI&gt;https://cdn-profiles.tunein.com/s42828/images/logoq.png?t=1&lt;/upnp:albumArtURI&gt;)"
    R"(&lt;/item&gt;&lt;/DIDL-Lite&gt;</CurrentURIMetaData><NextURI></NextURI><NextURIMetaData></NextURIMetaData>)"
    R"(<PlayMedium>NETWORK</PlayMedium><RecordMedium>NOT_IMPLEMENTED</RecordMedium>)"
    R"(<WriteStatus>NOT_IMPLEMENTED</WriteStatus></u:GetMediaInfoResponse></s:Body></s:Envelope>)";

// GetPositionInfo, Anfrage – [aufgezeichnet] Sonos S2, 2026-09-25, Deutschlandfunk über TuneIn
static const char* kGetPositionInfoRequest =
    R"(<?xml version="1.0" encoding="utf-8"?>)"
    R"(<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">)"
    R"(<s:Body><u:GetPositionInfo xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"><InstanceID>0</InstanceID>)"
    R"(</u:GetPositionInfo></s:Body></s:Envelope>)";

// GetMediaInfo, Anfrage – [aufgezeichnet] Sonos S2, 2026-09-25, Deutschlandfunk über TuneIn
static const char* kGetMediaInfoRequest =
    R"(<?xml version="1.0" encoding="utf-8"?>)"
    R"(<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">)"
    R"(<s:Body><u:GetMediaInfo xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"><InstanceID>0</InstanceID>)"
    R"(</u:GetMediaInfo></s:Body></s:Envelope>)";

// GetPositionInfo – [aufgezeichnet] Sonos S2, 2026-09-25, Spotify Connect (x-sonos-vli)
// Geräte-ID (RINCON_…) und Spotify-Sitzungs-ID geschwärzt.
static const char* kPositionSpotifyConnect =
    R"(<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">)"
    R"(<s:Body><u:GetPositionInfoResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"><Track>0</Track>)"
    R"(<TrackDuration>0:04:44</TrackDuration>)"
    R"(<TrackMetaData>&lt;DIDL-Lite xmlns:dc=&quot;http://purl.org/dc/elements/1.1/&quot; xmlns:upnp=&quot;urn:schemas-upnp-org:metadata-1-0/upnp/&quot; xmlns:r=&quot;urn:schemas-rinconnetworks-com:metadata-1-0/&quot; xmlns=&quot;urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/&quot;&gt;)"
    R"(&lt;item id=&quot;-1&quot; parentID=&quot;-1&quot;&gt;)"
    R"(&lt;res duration=&quot;0:04:44&quot;&gt;x-sonos-spotify:spotify:track:6iOQNALdG1fTirepgS0rmm?sid=9&amp;amp;flags=0&amp;amp;sn=2&lt;/res&gt;)"
    R"(&lt;upnp:albumArtURI&gt;https://i.scdn.co/image/ab67616d0000b273bdf478d2cbd63f86bed753bd&lt;/upnp:albumArtURI&gt;)"
    R"(&lt;upnp:class&gt;object.item.audioItem.musicTrack&lt;/upnp:class&gt;)"
    R"(&lt;dc:title&gt;Carry Me Back to Old Virginny&lt;/dc:title&gt;&lt;dc:creator&gt;Don Shirley&lt;/dc:creator&gt;)"
    R"(&lt;upnp:album&gt;The Don Shirley Point Of View&lt;/upnp:album&gt;)"
    R"(&lt;r:streamInfo&gt;bd:16,sr:44100,c:0,l:0,d:0&lt;/r:streamInfo&gt;&lt;/item&gt;)"
    R"(&lt;/DIDL-Lite&gt;</TrackMetaData>)"
    R"(<TrackURI>x-sonos-vli:RINCON_00000000000001400:2,@000000000000000000000000000000</TrackURI>)"
    R"(<RelTime>0:00:11</RelTime><AbsTime>NOT_IMPLEMENTED</AbsTime><RelCount>2147483647</RelCount>)"
    R"(<AbsCount>2147483647</AbsCount></u:GetPositionInfoResponse></s:Body></s:Envelope>)";

// GetMediaInfo – [aufgezeichnet] Sonos S2, 2026-09-25, Spotify Connect (x-sonos-vli)
static const char* kMediaSpotifyConnect =
    R"(<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">)"
    R"(<s:Body><u:GetMediaInfoResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"><NrTracks>1</NrTracks>)"
    R"(<MediaDuration>NOT_IMPLEMENTED</MediaDuration>)"
    R"(<CurrentURI>x-sonos-vli:RINCON_00000000000001400:2,@000000000000000000000000000000</CurrentURI>)"
    R"(<CurrentURIMetaData>&lt;DIDL-Lite xmlns:dc=&quot;http://purl.org/dc/elements/1.1/&quot; xmlns:upnp=&quot;urn:schemas-upnp-org:metadata-1-0/upnp/&quot; xmlns:r=&quot;urn:schemas-rinconnetworks-com:metadata-1-0/&quot; xmlns=&quot;urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/&quot;&gt;)"
    R"(&lt;item id=&quot;spotify&quot; parentID=&quot;0&quot; restricted=&quot;false&quot;&gt;)"
    R"(&lt;dc:title&gt;Spotify&lt;/dc:title&gt;&lt;upnp:class&gt;object.item.audioItem.linein&lt;/upnp:class&gt;)"
    R"(&lt;res protocolInfo=&quot;x-sonos-vli:*:audio:*&quot;&gt;x-sonos-vli:RINCON_00000000000001400:2,@000000000000000000000000000000&lt;/res&gt;)"
    R"(&lt;vli cookie=&quot;3&quot; group=&quot;&quot;&gt;&lt;/vli&gt;&lt;/item&gt;)"
    R"(&lt;/DIDL-Lite&gt;</CurrentURIMetaData><NextURI></NextURI><NextURIMetaData></NextURIMetaData>)"
    R"(<PlayMedium>NETWORK</PlayMedium><RecordMedium>NOT_IMPLEMENTED</RecordMedium>)"
    R"(<WriteStatus>NOT_IMPLEMENTED</WriteStatus></u:GetMediaInfoResponse></s:Body></s:Envelope>)";

#undef SONOS_ENV_OPEN
#undef SONOS_ENV_CLOSE
#undef DIDL_OPEN

}  // namespace fixtures
