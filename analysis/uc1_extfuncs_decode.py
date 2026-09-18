#!/usr/bin/env python3
"""Read SSL 360's EXT FUNCS menu out of a UC1 capture.

The hidden BACK-menu is a list SSL authored per channel strip, and the only
one we hold is the Harrison 32C's (cap137, walked by hand on 2026-09-11 and
transcribed into kExtFuncs32c). The other factory strips have no list at all,
and inventing one would mean calling our taste SSL's.

So: capture SSL 360 walking each strip's menu, and read the labels off the
wire. They arrive as LCD text inside FF 66 frames on the UC1's OUT endpoint.
This prints every text row with its timestamp, in order, collapsing immediate
repeats — a menu walk then reads as the menu.

Usage:
    uc1_extfuncs_decode.py <capture.pcap> [--from SEC] [--to SEC] [--min-len 2]

Written 2026-09-18 so the decode is ready BEFORE the capture, not after it
(Frank: "ich mag dort nicht 30min auf dich warten").
"""
import argparse
import re
import shutil
import subprocess
import sys
from collections import Counter

SSL_VID = 0x31E9
UC1_PID = 0x0023
PRINTABLE = re.compile(rb"[\x20-\x7e]{2,}")


def device_address(pcap):
    out = subprocess.run(
        ["tshark", "-r", pcap,
         "-Y", f"usb.idVendor == 0x{SSL_VID:04x} and usb.idProduct == 0x{UC1_PID:04x}",
         "-T", "fields", "-e", "usb.device_address"],
        capture_output=True, text=True, check=True)
    addrs = [a.strip() for a in out.stdout.splitlines() if a.strip()]
    return Counter(addrs).most_common(1)[0][0] if addrs else None


def rows(pcap, addr):
    cmd = ["tshark", "-r", pcap,
           "-Y", f"usb.device_address == {addr} and usb.transfer_type == 0x03",
           "-T", "fields",
           "-e", "frame.time_relative", "-e", "usb.endpoint_address",
           "-e", "usb.capdata"]
    for line in subprocess.run(cmd, capture_output=True, text=True,
                               check=True).stdout.splitlines():
        p = line.split("\t")
        if len(p) < 3 or not p[0] or not p[1] or not p[2]:
            continue
        try:
            ts = float(p[0])
            ep = int(p[1], 16) if p[1].startswith("0x") else int(p[1], 0)
            data = bytes.fromhex(p[2].replace(":", ""))
        except ValueError:
            continue
        if ep & 0x80:          # IN = the surface talking to us, not the LCD
            continue
        yield ts, data


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pcap")
    ap.add_argument("--from", dest="t0", type=float, default=0.0)
    ap.add_argument("--to", dest="t1", type=float, default=1e9)
    ap.add_argument("--min-len", type=int, default=2)
    # A capture that starts AFTER enumeration carries no descriptors, so the
    # VID/PID filter finds nothing — cap137 is exactly that. The address is in
    # the capture's own .md; pass it.
    ap.add_argument("--dev", default=None, help="USB device_address (see the capture .md)")
    a = ap.parse_args()

    if shutil.which("tshark") is None:
        sys.stderr.write("tshark not on PATH — install Wireshark and re-run.\n")
        return 2
    addr = a.dev or device_address(a.pcap)
    if addr is None:
        sys.stderr.write("No UC1 descriptors in this capture (it started after "
                         "enumeration). Pass --dev N; the address is in the "
                         "capture's .md.\n")
        return 1
    print(f"# {a.pcap}  UC1 device_address={addr}  window {a.t0}..{a.t1}s")

    last = None
    for ts, data in rows(a.pcap, addr):
        if ts < a.t0 or ts > a.t1:
            continue
        # Walk the frames in the URB; FF 66 is the LCD text opcode.
        i = 0
        while i + 1 < len(data):
            if data[i] != 0xFF:
                i += 1
                continue
            if data[i + 1] != 0x66:
                i += 1
                continue
            chunk = data[i:i + 64]
            for m in PRINTABLE.finditer(chunk):
                txt = m.group().decode("ascii", "replace").strip()
                if len(txt) < a.min_len:
                    continue
                if txt == last:
                    continue
                last = txt
                print(f"{ts:9.3f}  {txt!r}")
            i += 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
