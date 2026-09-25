#!/usr/bin/env python3
"""
sonos_probe – Sonos-Befehle vom PC aus testen (Testebene T3, siehe docs/TESTEN.md).

Schickt dieselben SOAP-Nachrichten wie die Firmware (byte-genau wie lib/sonos_core)
an einen Speaker und zeigt die Antwort. Mit --save werden Anfrage und Antwort als
Dateien abgelegt; daraus entstehen die Testdaten in test/test_sonos_core/fixtures.h.

Nur Python-Standardbibliothek, keine Installation nötig.

Beispiele:
  python3 tools/sonos_probe.py 192.168.1.50 info
  python3 tools/sonos_probe.py 192.168.1.50 volume get
  python3 tools/sonos_probe.py 192.168.1.50 volume set 25
  python3 tools/sonos_probe.py 192.168.1.50 transport info
  python3 tools/sonos_probe.py 192.168.1.50 transport pause
  python3 tools/sonos_probe.py 192.168.1.50 transport seek 0:01:30
  python3 tools/sonos_probe.py 192.168.1.50 --save probe-out transport info
  python3 tools/sonos_probe.py 192.168.1.50 nowplaying          # was läuft gerade?
  python3 tools/sonos_probe.py - discover                       # Speaker im Netz suchen (SSDP)
  python3 tools/sonos_probe.py 192.168.1.50 --save probe-topologie topology   # Räume und Gruppen
  python3 tools/sonos_probe.py 192.168.1.50 --save probe-out nowplaying
  python3 tools/sonos_probe.py 192.168.1.50 favorites list      # Sonos-Favoriten und wie sie starten
  python3 tools/sonos_probe.py 192.168.1.50 favorites play 3    # Favorit Nr. 3 abspielen (am Koordinator!)
  python3 tools/sonos_probe.py 192.168.1.50 favorites try 3     # Experiment: Verknüpfung als Container starten
"""

from __future__ import annotations  # Typangaben auch mit Python 3.8/3.9 (macOS-Standard)

import argparse
import html
import pathlib
import re
import socket
import sys
import time
import urllib.error
import urllib.request

PORT = 1400

SERVICES = {
    "RenderingControl": ("/MediaRenderer/RenderingControl/Control",
                         "urn:schemas-upnp-org:service:RenderingControl:1"),
    "AVTransport": ("/MediaRenderer/AVTransport/Control",
                    "urn:schemas-upnp-org:service:AVTransport:1"),
    "ContentDirectory": ("/MediaServer/ContentDirectory/Control",
                         "urn:schemas-upnp-org:service:ContentDirectory:1"),
}

# Wie sonos::favorites::playMethod (lib/sonos_core/src/Favorites.cpp)
STREAM_PREFIXES = ("x-sonosapi-stream:", "x-sonosapi-radio:", "x-sonosapi-hls:", "x-rincon-mp3radio:", "hls-radio:",
                   "aac:", "pndrradio:", "x-rincon-stream:", "x-sonos-htastream:")


def build_envelope(urn: str, action: str, args: list[tuple[str, str]]) -> str:
    """Muss exakt sonos::buildSoapRequest (lib/sonos_core/src/Soap.cpp) entsprechen."""
    inner = "".join(f"<{k}>{html.escape(v, quote=True).replace('&#x27;', '&apos;')}</{k}>" for k, v in args)
    return ('<?xml version="1.0" encoding="utf-8"?>'
            '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" '
            's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">'
            '<s:Body>'
            f'<u:{action} xmlns:u="{urn}">{inner}</u:{action}>'
            '</s:Body></s:Envelope>')


def soap(ip: str, service: str, action: str, args: list[tuple[str, str]], timeout: float = 3.0):
    path, urn = SERVICES[service]
    body = build_envelope(urn, action, args)
    req = urllib.request.Request(
        f"http://{ip}:{PORT}{path}",
        data=body.encode("utf-8"),
        method="POST",
        headers={
            "Content-Type": 'text/xml; charset="utf-8"',
            "SOAPACTION": f'"{urn}#{action}"',
        },
    )
    start = time.monotonic()
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            status, text = resp.status, resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        status, text = e.code, e.read().decode("utf-8", "replace")
    elapsed_ms = (time.monotonic() - start) * 1000
    return body, status, text, elapsed_ms


def element(xml: str, name: str) -> str | None:
    m = re.search(rf"<(?:\w+:)?{name}(?:\s[^>]*)?>(.*?)</(?:\w+:)?{name}>", xml, re.S)
    return m.group(1) if m else None


def report(action: str, request: str, status: int, response: str, elapsed_ms: float, save_dir: str | None):
    ok = status == 200 and element(response, "Fault") is None
    print(f"{action}: HTTP {status} in {elapsed_ms:.0f} ms – {'OK' if ok else 'FEHLER'}")
    if not ok:
        code = element(response, "errorCode")
        if code:
            print(f"  UPnP-Fehlercode: {code}")
    if save_dir:
        out = pathlib.Path(save_dir)
        out.mkdir(parents=True, exist_ok=True)
        (out / f"{action}_request.xml").write_text(request, encoding="utf-8")
        (out / f"{action}_response_{status}.xml").write_text(response, encoding="utf-8")
        print(f"  gespeichert in {out}/{action}_*.xml")
    return ok


def cmd_info(ip: str, _args) -> int:
    """Gerätebeschreibung: Raumname, Modell, Software-Version."""
    url = f"http://{ip}:{PORT}/xml/device_description.xml"
    try:
        with urllib.request.urlopen(url, timeout=3) as resp:
            xml = resp.read().decode("utf-8", "replace")
    except (urllib.error.URLError, TimeoutError) as e:
        print(f"Speaker unter {ip}:{PORT} nicht erreichbar: {e}")
        return 1
    for label, tag in [("Raum", "roomName"), ("Modell", "modelName"), ("Software", "softwareVersion"),
                       ("Hardware", "hardwareVersion"), ("Anzeige-Version", "displayVersion")]:
        value = element(xml, tag)
        if value:
            print(f"{label:16} {html.unescape(value)}")
    return 0


def cmd_volume(ip: str, args) -> int:
    if args.op == "get":
        req, status, resp, ms = soap(ip, "RenderingControl", "GetVolume",
                                     [("InstanceID", "0"), ("Channel", "Master")])
        ok = report("GetVolume", req, status, resp, ms, args.save)
        if ok:
            print(f"  Lautstärke: {element(resp, 'CurrentVolume')}")
        return 0 if ok else 1

    value = max(0, min(100, args.value))
    req, status, resp, ms = soap(ip, "RenderingControl", "SetVolume",
                                 [("InstanceID", "0"), ("Channel", "Master"), ("DesiredVolume", str(value))])
    ok = report("SetVolume", req, status, resp, ms, args.save)
    return 0 if ok else 1


def cmd_groupvolume(ip: str, args) -> int:
    """Gruppenlautstärke am Koordinator (GroupRenderingControl) – nur sinnvoll, wenn Räume gruppiert sind."""
    SERVICES["GroupRenderingControl"] = ("/MediaRenderer/GroupRenderingControl/Control",
                                         "urn:schemas-upnp-org:service:GroupRenderingControl:1")
    req, status, resp, ms = soap(ip, "GroupRenderingControl", "SnapshotGroupVolume", [("InstanceID", "0")])
    report("SnapshotGroupVolume", req, status, resp, ms, args.save)
    if args.op == "get":
        req, status, resp, ms = soap(ip, "GroupRenderingControl", "GetGroupVolume", [("InstanceID", "0")])
        ok = report("GetGroupVolume", req, status, resp, ms, args.save)
        if ok:
            print(f"  Gruppenlautstärke: {element(resp, 'CurrentVolume')}")
        return 0 if ok else 1
    value = max(0, min(100, args.value))
    req, status, resp, ms = soap(ip, "GroupRenderingControl", "SetGroupVolume",
                                 [("InstanceID", "0"), ("DesiredVolume", str(value))])
    return 0 if report("SetGroupVolume", req, status, resp, ms, args.save) else 1


def image_info(data: bytes) -> str:
    """Format und Abmessungen aus den ersten Bytes (JPEG: SOF-Marker, PNG: IHDR)."""
    if data[:3] == b"\xff\xd8\xff":
        i = 2
        while i + 9 < len(data):
            if data[i] != 0xFF:
                i += 1
                continue
            marker = data[i + 1]
            length = int.from_bytes(data[i + 2:i + 4], "big")
            if marker in (0xC0, 0xC1, 0xC2):
                h = int.from_bytes(data[i + 5:i + 7], "big")
                w = int.from_bytes(data[i + 7:i + 9], "big")
                kind = "progressiv – das Gerät kann es nur unscharf (1/8) anzeigen" if marker == 0xC2 else "baseline"
                return f"JPEG {w}x{h} ({kind})"
            i += 2 + length
        return "JPEG (Größe unbekannt)"
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        w = int.from_bytes(data[16:20], "big")
        h = int.from_bytes(data[20:24], "big")
        return f"PNG {w}x{h}"
    return f"unbekanntes Format (erste Bytes {data[:8].hex()}) – das Gerät kann es nicht anzeigen"


def cmd_cover(_ip: str, args) -> int:
    """Lädt eine Cover-Adresse wie die Firmware und zeigt, ob sie darstellbar ist."""
    import ssl
    ctx = ssl.create_default_context()
    start = time.monotonic()
    try:
        req = urllib.request.Request(args.url, headers={"User-Agent": "MaTouchSonos/1.0"})
        with urllib.request.urlopen(req, timeout=5, context=ctx) as resp:
            data = resp.read(800 * 1024)
            ctype = resp.headers.get("Content-Type", "?")
            status = resp.status
    except urllib.error.HTTPError as e:
        print(f"HTTP {e.code} – Adresse liefert kein Bild")
        return 1
    except (urllib.error.URLError, TimeoutError) as e:
        print(f"Nicht erreichbar: {e}")
        return 1
    ms = (time.monotonic() - start) * 1000
    print(f"HTTP {status}, {len(data) // 1024} KB, {ctype}, {ms:.0f} ms")
    print(f"  {image_info(data)}")
    if len(data) > 700 * 1024:
        print("  Achtung: größer als 700 KB – das Gerät lädt es nicht")
    return 0


def cmd_transport(ip: str, args) -> int:
    actions = {
        "info": ("GetTransportInfo", [("InstanceID", "0")]),
        "play": ("Play", [("InstanceID", "0"), ("Speed", "1")]),
        "pause": ("Pause", [("InstanceID", "0")]),
        "stop": ("Stop", [("InstanceID", "0")]),
        "next": ("Next", [("InstanceID", "0")]),
        "previous": ("Previous", [("InstanceID", "0")]),
        "seek": ("Seek", [("InstanceID", "0"), ("Unit", "REL_TIME"), ("Target", args.position)]),
    }
    action, soap_args = actions[args.op]
    req, status, resp, ms = soap(ip, "AVTransport", action, soap_args)
    ok = report(action, req, status, resp, ms, args.save)
    if ok and args.op == "info":
        print(f"  Zustand: {element(resp, 'CurrentTransportState')}")
    elif not ok:
        code = element(resp, "errorCode")
        hints = {"701": "Nichts zum Abspielen bzw. Aktion im aktuellen Zustand nicht möglich",
                 "711": "Kein weiterer Titel in der Warteschlange",
                 "800": "Speaker ist Mitglied einer Gruppe – Befehl an den Gruppen-Koordinator senden"}
        if code in hints:
            print(f"  Hinweis: {hints[code]}")
    return 0 if ok else 1


def cmd_nowplaying(ip: str, args) -> int:
    """GetPositionInfo + GetMediaInfo – die Rohdaten für den Now-Playing-Bildschirm."""
    req, status, pos, ms = soap(ip, "AVTransport", "GetPositionInfo", [("InstanceID", "0")])
    if not report("GetPositionInfo", req, status, pos, ms, args.save):
        return 1
    req, status, media, ms = soap(ip, "AVTransport", "GetMediaInfo", [("InstanceID", "0")])
    if not report("GetMediaInfo", req, status, media, ms, args.save):
        return 1

    def didl_field(escaped_didl: str | None, name: str) -> str:
        if not escaped_didl or escaped_didl == "NOT_IMPLEMENTED":
            return ""
        return html.unescape(element(html.unescape(escaped_didl), name) or "")

    meta = element(pos, "TrackMetaData")
    print(f"  Track-URI:  {html.unescape(element(pos, 'TrackURI') or '')}")
    print(f"  Titel:      {didl_field(meta, 'title')}")
    print(f"  Interpret:  {didl_field(meta, 'creator')}")
    print(f"  Album:      {didl_field(meta, 'album')}")
    print(f"  Stream:     {didl_field(meta, 'streamContent')}")
    print(f"  Position:   {element(pos, 'RelTime')} / {element(pos, 'TrackDuration')}")
    print(f"  Quelle:     {didl_field(element(media, 'CurrentURIMetaData'), 'title')}")
    art = didl_field(meta, 'albumArtURI') or didl_field(element(media, 'CurrentURIMetaData'), 'albumArtURI')
    if art.startswith("/"):
        art = f"http://{ip}:{PORT}{art}"
    print(f"  Cover:      {art or '(keins)'}")
    if art:
        print(f"              prüfen mit: python3 tools/sonos_probe.py - cover '{art}'")
    return 0


def cmd_discover(_ip: str, _args) -> int:
    """SSDP-Suche wie die Firmware: M-SEARCH an 239.255.255.250:1900, 2 s auf Antworten warten."""
    request = ("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\n"
               "MX: 1\r\nST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n\r\n")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
    sock.settimeout(0.3)
    found = {}
    start = time.monotonic()
    for _ in range(2):  # zweimal senden, UDP kann verloren gehen
        sock.sendto(request.encode(), ("239.255.255.250", 1900))
    while time.monotonic() - start < 2.0:
        try:
            data, addr = sock.recvfrom(2048)
        except socket.timeout:
            continue
        text = data.decode("utf-8", "replace")
        m = re.search(r"^location:\s*(\S+)", text, re.I | re.M)
        if m and ("zoneplayer" in text.lower() or "sonos" in text.lower()):
            found[addr[0]] = m.group(1)
    sock.close()
    if not found:
        print("Keine Sonos-Speaker gefunden (Mac im selben Netz? Firewall/WLAN-Isolation?)")
        return 1
    print(f"{len(found)} Sonos-Speaker gefunden:")
    for ip in sorted(found, key=lambda a: tuple(int(x) for x in a.split("."))):
        print(f"  {ip:15}  {found[ip]}")
    return 0


def cmd_topology(ip: str, args) -> int:
    """GetZoneGroupState – Räume, Gruppen, Koordinatoren (wie die Firmware sie auswertet)."""
    path, urn = "/ZoneGroupTopology/Control", "urn:schemas-upnp-org:service:ZoneGroupTopology:1"
    SERVICES["ZoneGroupTopology"] = (path, urn)
    req, status, resp, ms = soap(ip, "ZoneGroupTopology", "GetZoneGroupState", [])
    if not report("GetZoneGroupState", req, status, resp, ms, args.save):
        return 1
    inner = html.unescape(element(resp, "ZoneGroupState") or "")
    for group in re.finditer(r"<ZoneGroup\s([^>]*)>(.*?)</ZoneGroup>", inner, re.S):
        coord = re.search(r'Coordinator="([^"]+)"', group.group(1))
        members = []
        for m in re.finditer(r"<ZoneGroupMember\s([^>]*?)/?>", group.group(2)):
            attrs = dict(re.findall(r'(\w+)="([^"]*)"', m.group(1)))
            hidden = attrs.get("Invisible") == "1" or attrs.get("IsZoneBridge") == "1"
            loc = re.search(r"//([^:/]+)", attrs.get("Location", ""))
            mark = "*" if coord and attrs.get("UUID") == coord.group(1) else " "
            members.append(f"{mark} {html.unescape(attrs.get('ZoneName', '?')):20} {loc.group(1) if loc else '?':15}"
                           f"{'  (ausgeblendet)' if hidden else ''}")
        print("Gruppe:")
        for line in members:
            print("   " + line)
    print("(* = Koordinator)")
    return 0


def browse_favorites(ip: str, start: int, count: int, save: str | None):
    """Browse FV:2 – liefert (ok, Liste von dicts, Gesamtzahl). Wie sonos::favorites::parseBrowse."""
    req, status, resp, ms = soap(ip, "ContentDirectory", "Browse",
                                 [("ObjectID", "FV:2"), ("BrowseFlag", "BrowseDirectChildren"), ("Filter", "*"),
                                  ("StartingIndex", str(start)), ("RequestedCount", str(count)),
                                  ("SortCriteria", "")], timeout=5.0)
    if not report("Browse", req, status, resp, ms, save):
        return False, [], 0
    didl = html.unescape(element(resp, "Result") or "")
    favs = []
    for m in re.finditer(r"<item[\s>](.*?)</item>", didl, re.S):
        item = m.group(1)
        meta = html.unescape(element(item, "resMD") or "")
        favs.append({
            "title": html.unescape(element(item, "title") or ""),
            "description": html.unescape(element(item, "description") or ""),
            "uri": html.unescape(element(item, "res") or ""),
            "metadata": meta,
            "class": html.unescape(element(meta, "class") or "") if meta else "",
            "art": html.unescape(element(item, "albumArtURI") or ""),
            "type": html.unescape(element(item, "type") or ""),
        })
    return True, favs, int(element(resp, "TotalMatches") or 0)


def play_method(fav: dict) -> str:
    if not fav["uri"]:
        return "nicht abspielbar"
    if fav["uri"].startswith(STREAM_PREFIXES) or fav["class"].startswith("object.item.audioItem.audioBroadcast"):
        return "direkt"
    return "Warteschlange"


def device_uuid(ip: str) -> str:
    with urllib.request.urlopen(f"http://{ip}:{PORT}/xml/device_description.xml", timeout=3) as resp:
        xml = resp.read().decode("utf-8", "replace")
    return (element(xml, "UDN") or "").replace("uuid:", "")


def service_account_serial(ip: str, service_type: str) -> str | None:
    """Kontonummer (sn) eines Dienstes aus /status/accounts – nicht jede Firmware liefert das noch."""
    try:
        with urllib.request.urlopen(f"http://{ip}:{PORT}/status/accounts", timeout=3) as resp:
            xml = resp.read().decode("utf-8", "replace")
    except (urllib.error.URLError, TimeoutError):
        return None
    m = re.search(rf'<Account[^>]*Type="{service_type}"[^>]*SerialNum="(\d+)"', xml) or \
        re.search(rf'<Account[^>]*SerialNum="(\d+)"[^>]*Type="{service_type}"', xml)
    return m.group(1) if m else None


def try_shortcut(ip: str, fav: dict, save: str | None) -> int:
    """
    Experiment für Verknüpfungen ohne Adresse (z. B. Pocket Casts „In Progress“): aus den Metadaten eine
    Container-Adresse bauen (x-rincon-cpcontainer:<id>?sid=…&flags=…&sn=…) und in die Warteschlange legen.
    Ob der Dienst das mitmacht, zeigt nur der Versuch.
    """
    meta = fav["metadata"]
    item_id = re.search(r'<item id="([^"]+)"', meta)
    desc = re.search(r"SA_RINCON(\d+)_", meta)
    if not item_id or not desc:
        print("  Keine Container-ID oder Dienst-Kennung in den Metadaten – geht nicht.")
        return 1
    service_type = desc.group(1)
    sid = (int(service_type) - 7) // 256
    print(f"  Container-ID: {item_id.group(1)}   Dienst: sid={sid} (Typ {service_type})")

    serial = service_account_serial(ip, service_type)
    if serial:
        print(f"  Kontonummer laut /status/accounts: sn={serial}")
        serials = [serial]
    else:
        print("  /status/accounts liefert keine Kontonummer – probiere sn=1…20 und ohne sn")
        serials = [str(n) for n in range(1, 21)] + [None]
    metas = [meta, meta.replace("<upnp:class>object.container</upnp:class>",
                                "<upnp:class>object.container.playlistContainer</upnp:class>")]
    if metas[1] == metas[0]:
        metas.pop()

    print("  ACHTUNG: Die Warteschlange wird geleert.")
    req, status, resp, ms = soap(ip, "AVTransport", "RemoveAllTracksFromQueue", [("InstanceID", "0")])
    if not report("RemoveAllTracksFromQueue", req, status, resp, ms, None):
        return 1
    tried = 0
    for meta_variant in metas:
        for flags in ("8300", "0"):
            for sn in serials:
                uri = f"x-rincon-cpcontainer:{item_id.group(1)}?sid={sid}&flags={flags}" + (f"&sn={sn}" if sn else "")
                req, status, resp, ms = soap(ip, "AVTransport", "AddURIToQueue",
                                             [("InstanceID", "0"), ("EnqueuedURI", uri),
                                              ("EnqueuedURIMetaData", meta_variant),
                                              ("DesiredFirstTrackNumberEnqueued", "0"), ("EnqueueAsNext", "0")])
                tried += 1
                added = element(resp, "NumTracksAdded")
                if status == 200 and element(resp, "Fault") is None and added not in (None, "0"):
                    print(f"  ERFOLG nach {tried} Versuchen: {added} Folgen in der Warteschlange")
                    print(f"  Adresse: {uri}")
                    if meta_variant is not meta:
                        print("  (mit Inhaltsart object.container.playlistContainer)")
                    report("AddURIToQueue", req, status, resp, ms, save)
                    queue = f"x-rincon-queue:{device_uuid(ip)}#0"
                    for action, a in [("SetAVTransportURI", [("InstanceID", "0"), ("CurrentURI", queue),
                                                             ("CurrentURIMetaData", "")]),
                                      ("Play", [("InstanceID", "0"), ("Speed", "1")])]:
                        req, status, resp, ms = soap(ip, "AVTransport", action, a)
                        if not report(action, req, status, resp, ms, save):
                            return 1
                    return 0
    code = element(resp, "errorCode")
    print(f"  Kein Erfolg nach {tried} Versuchen (letzter UPnP-Fehler: {code or '–'}).")
    print("  Pocket Casts lässt diesen Ordner offenbar nicht direkt in die Warteschlange legen.")
    return 1


def cmd_favorites(ip: str, args) -> int:
    """Sonos-Favoriten lesen bzw. einen abspielen – genau wie die Firmware (Schritt 7)."""
    ok, favs, total = browse_favorites(ip, 0, 100, args.save)
    if not ok:
        return 1
    if args.op == "list":
        print(f"  {total} Favoriten")
        for i, f in enumerate(favs):
            print(f"  {i:3}  {f['title']:40.40} {f['description']:22.22} {play_method(f)}")
            print(f"       {f['uri'][:100]}")
            print(f"       Art: {f['class'] or '(keine Metadaten)'}  Typ: {f['type']}")
        return 0

    if not 0 <= args.index < len(favs):
        print(f"Favorit {args.index} gibt es nicht (0..{len(favs) - 1})")
        return 1
    fav = favs[args.index]
    if args.op == "try":
        return try_shortcut(ip, fav, args.save)
    method = play_method(fav)
    print(f"  Starte „{fav['title']}“ – {method}")
    steps = []
    if method == "direkt":
        steps.append(("SetAVTransportURI", [("InstanceID", "0"), ("CurrentURI", fav["uri"]),
                                            ("CurrentURIMetaData", fav["metadata"])]))
    elif method == "Warteschlange":
        queue = f"x-rincon-queue:{device_uuid(ip)}#0"
        steps += [("RemoveAllTracksFromQueue", [("InstanceID", "0")]),
                  ("AddURIToQueue", [("InstanceID", "0"), ("EnqueuedURI", fav["uri"]),
                                     ("EnqueuedURIMetaData", fav["metadata"]),
                                     ("DesiredFirstTrackNumberEnqueued", "0"), ("EnqueueAsNext", "0")]),
                  ("SetAVTransportURI", [("InstanceID", "0"), ("CurrentURI", queue), ("CurrentURIMetaData", "")])]
    else:
        print("  Dieser Favorit hat keine abspielbare Adresse.")
        return 1
    steps.append(("Play", [("InstanceID", "0"), ("Speed", "1")]))
    for action, soap_args in steps:
        req, status, resp, ms = soap(ip, "AVTransport", action, soap_args)
        if not report(action, req, status, resp, ms, args.save):
            if element(resp, "errorCode") == "800":
                print("  Hinweis: Speaker ist Mitglied einer Gruppe – IP des Gruppen-Koordinators angeben")
            return 1
    return 0


def main() -> int:
    p = argparse.ArgumentParser(description="Sonos-Befehle vom PC testen (T3).")
    p.add_argument("ip", help="IP-Adresse des Speakers")
    p.add_argument("--save", metavar="ORDNER", help="Anfrage und Antwort als XML-Dateien speichern")
    sub = p.add_subparsers(dest="command", required=True)

    sub.add_parser("info", help="Raumname, Modell und Software-Version anzeigen")

    vol = sub.add_parser("volume", help="Lautstärke lesen oder setzen")
    vol_sub = vol.add_subparsers(dest="op", required=True)
    vol_sub.add_parser("get")
    vset = vol_sub.add_parser("set")
    vset.add_argument("value", type=int, help="0..100")

    tr = sub.add_parser("transport", help="Wiedergabe: Zustand lesen, Play, Pause, Stop, Next, Previous, Seek")
    tr.add_argument("op", choices=["info", "play", "pause", "stop", "next", "previous", "seek"])
    tr.add_argument("position", nargs="?", default="0:00:30", help="nur für seek: Zielposition H:MM:SS")

    sub.add_parser("nowplaying", help="Was läuft gerade? (GetPositionInfo + GetMediaInfo)")
    sub.add_parser("discover", help="Sonos-Speaker im Netz suchen (SSDP); als IP '-' angeben")
    sub.add_parser("topology", help="Räume und Gruppen (GetZoneGroupState)")

    cv = sub.add_parser("cover", help="Cover-Adresse laden und prüfen (Format, Größe); als IP '-' angeben")
    cv.add_argument("url")

    fv = sub.add_parser("favorites", help="Sonos-Favoriten anzeigen oder einen abspielen")
    fv_sub = fv.add_subparsers(dest="op", required=True)
    fv_sub.add_parser("list")
    fvp = fv_sub.add_parser("play")
    fvp.add_argument("index", type=int, help="Nummer aus „favorites list“")
    fvt = fv_sub.add_parser("try", help="Experiment: Verknüpfung ohne Adresse als Container starten")
    fvt.add_argument("index", type=int, help="Nummer aus „favorites list“")

    gv = sub.add_parser("groupvolume", help="Gruppenlautstärke am Koordinator lesen/setzen")
    gv_sub = gv.add_subparsers(dest="op", required=True)
    gv_sub.add_parser("get")
    gvs = gv_sub.add_parser("set")
    gvs.add_argument("value", type=int, help="0..100")

    args = p.parse_args()
    try:
        return {"info": cmd_info, "volume": cmd_volume, "transport": cmd_transport,
                "nowplaying": cmd_nowplaying, "discover": cmd_discover, "topology": cmd_topology,
                "groupvolume": cmd_groupvolume, "cover": cmd_cover,
                "favorites": cmd_favorites}[args.command](args.ip, args)
    except (urllib.error.URLError, TimeoutError, ConnectionError) as e:
        print(f"Speaker unter {args.ip}:{PORT} nicht erreichbar: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
