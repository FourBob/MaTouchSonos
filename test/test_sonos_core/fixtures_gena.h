#pragma once

// GENA-Events (NOTIFY), nachgebaut nach dem UPnP-/Sonos-Format: LastChange ist ein XML-Dokument,
// das kodiert in <LastChange> steckt; darin sind Metadaten (CurrentTrackMetaData) nochmals kodiert.
// Eine echte Aufzeichnung folgt im Geräte-Test (Log-Zeilen EVENT …).

namespace fixtures {

static const char* kNotifyAvTransport =
    "NOTIFY /ev/av HTTP/1.1\r\nHOST: 192.168.1.20:3400\r\nCONTENT-TYPE: text/xml; charset=\"utf-8\"\r\nNT: upnp:event\r\nNTS:"
    " upnp:propchange\r\nSID: uuid:RINCON_000E58A0123401400_sub0000000123\r\nSEQ: 0\r\nContent-Length: 1087\r\nConnection: "
    "close\r\n\r\n<e:propertyset xmlns:e=\"urn:schemas-upnp-org:event-1-0\"><e:property><LastChange>&lt;Event xmlns=&quot"
    ";urn:schemas-upnp-org:metadata-1-0/AVT/&quot; xmlns:r=&quot;urn:schemas-rinconnetworks-com:metadata-1-0/&quot;"
    "&gt;&lt;InstanceID val=&quot;0&quot;&gt;&lt;TransportState val=&quot;PLAYING&quot;/&gt;&lt;CurrentPlayMode val"
    "=&quot;NORMAL&quot;/&gt;&lt;CurrentTrackMetaData val=&quot;&amp;lt;DIDL-Lite xmlns:dc=&amp;quot;http://purl.or"
    "g/dc/elements/1.1/&amp;quot; xmlns:upnp=&amp;quot;urn:schemas-upnp-org:metadata-1-0/upnp/&amp;quot; xmlns:r=&a"
    "mp;quot;urn:schemas-rinconnetworks-com:metadata-1-0/&amp;quot; xmlns=&amp;quot;urn:schemas-upnp-org:metadata-1"
    "-0/DIDL-Lite/&amp;quot;&amp;gt;&amp;lt;item id=&amp;quot;-1&amp;quot; parentID=&amp;quot;-1&amp;quot;&amp;gt;&"
    "amp;lt;dc:title&amp;gt;Stayin&amp;amp;apos; Alive&amp;lt;/dc:title&amp;gt;&amp;lt;dc:creator&amp;gt;Scary Pock"
    "ets&amp;lt;/dc:creator&amp;gt;&amp;lt;/item&amp;gt;&amp;lt;/DIDL-Lite&amp;gt;&quot;/&gt;&lt;CurrentTrackDurati"
    "on val=&quot;0:03:09&quot;/&gt;&lt;/InstanceID&gt;&lt;/Event&gt;</LastChange></e:property></e:propertyset>";

static const char* kNotifyRenderingControl =
    "NOTIFY /ev/rc HTTP/1.1\r\nHOST: 192.168.1.20:3400\r\nCONTENT-TYPE: text/xml; charset=\"utf-8\"\r\nNT: upnp:event\r\nNTS:"
    " upnp:propchange\r\nSID: uuid:RINCON_000E58A0123401400_sub0000000123\r\nSEQ: 3\r\nContent-Length: 496\r\nConnection: c"
    "lose\r\n\r\n<e:propertyset xmlns:e=\"urn:schemas-upnp-org:event-1-0\"><e:property><LastChange>&lt;Event xmlns=&quot;"
    "urn:schemas-upnp-org:metadata-1-0/RCS/&quot;&gt;&lt;InstanceID val=&quot;0&quot;&gt;&lt;Volume channel=&quot;L"
    "F&quot; val=&quot;100&quot;/&gt;&lt;Volume channel=&quot;Master&quot; val=&quot;27&quot;/&gt;&lt;Volume channe"
    "l=&quot;RF&quot; val=&quot;100&quot;/&gt;&lt;Mute channel=&quot;Master&quot; val=&quot;0&quot;/&gt;&lt;/Instan"
    "ceID&gt;&lt;/Event&gt;</LastChange></e:property></e:propertyset>";

static const char* kNotifyGroupRenderingControl =
    "NOTIFY /ev/grc HTTP/1.1\r\nHOST: 192.168.1.20:3400\r\nCONTENT-TYPE: text/xml; charset=\"utf-8\"\r\nNT: upnp:event\r\nNTS"
    ": upnp:propchange\r\nSID: uuid:RINCON_000E58A0123401400_sub0000000123\r\nSEQ: 1\r\nContent-Length: 175\r\nConnection: "
    "close\r\n\r\n<e:propertyset xmlns:e=\"urn:schemas-upnp-org:event-1-0\"><e:property><GroupVolume>31</GroupVolume></e:"
    "property><e:property><GroupMute>0</GroupMute></e:property></e:propertyset>";

}  // namespace fixtures
