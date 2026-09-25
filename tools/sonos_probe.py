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
  python3 tools/sonos_probe.py 192.168.1.50 --save probe-out transport info
"""

from __future__ import annotations  # Typangaben auch mit Python 3.8/3.9 (macOS-Standard)

import argparse
import html
import pathlib
import re
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
}


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


def cmd_transport(ip: str, args) -> int:
    actions = {
        "info": ("GetTransportInfo", [("InstanceID", "0")]),
        "play": ("Play", [("InstanceID", "0"), ("Speed", "1")]),
        "pause": ("Pause", [("InstanceID", "0")]),
        "stop": ("Stop", [("InstanceID", "0")]),
    }
    action, soap_args = actions[args.op]
    req, status, resp, ms = soap(ip, "AVTransport", action, soap_args)
    ok = report(action, req, status, resp, ms, args.save)
    if ok and args.op == "info":
        print(f"  Zustand: {element(resp, 'CurrentTransportState')}")
    elif not ok:
        code = element(resp, "errorCode")
        hints = {"701": "Nichts zum Abspielen bzw. Aktion im aktuellen Zustand nicht möglich",
                 "800": "Speaker ist Mitglied einer Gruppe – Befehl an den Gruppen-Koordinator senden"}
        if code in hints:
            print(f"  Hinweis: {hints[code]}")
    return 0 if ok else 1


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

    tr = sub.add_parser("transport", help="Wiedergabe: Zustand lesen, Play, Pause, Stop")
    tr.add_argument("op", choices=["info", "play", "pause", "stop"])

    args = p.parse_args()
    try:
        return {"info": cmd_info, "volume": cmd_volume, "transport": cmd_transport}[args.command](args.ip, args)
    except (urllib.error.URLError, TimeoutError, ConnectionError) as e:
        print(f"Speaker unter {args.ip}:{PORT} nicht erreichbar: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
