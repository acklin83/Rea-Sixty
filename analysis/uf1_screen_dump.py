#!/usr/bin/env python3
"""UF1 screen (FF67) frame dump + address-map miner.

Parses OUT 0x02 FF67 frames from a UF1 pcapng and groups them by their
(b3,b4) header address. Frame model (working): FF 67 <len> <b3> <b4> <payload..> <ck>.
Prints, per address: the len, the distinct ASCII renderings seen, and a sample hex.

Usage:
  uf1_screen_dump.py <pcapng> [dev_addr]      # default dev auto (any FF67)
  uf1_screen_dump.py <pcapng> --ca            # only the big CA text-row frames, ASCII
"""
import sys, subprocess, re, binascii
from collections import OrderedDict, defaultdict

TSHARK = "/opt/homebrew/bin/tshark"

def frames(pcap, dev=None):
    dfilter = 'usb.endpoint_address==0x02 && usb.capdata'
    if dev:
        dfilter = f'usb.device_address=={dev} && ' + dfilter
    out = subprocess.run([TSHARK, "-r", pcap, "-Y", dfilter,
                          "-T", "fields", "-e", "usb.capdata"],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        h = line.strip().lower()
        # a URB may concatenate frames; split on ff67 boundaries is unsafe (ff67 can
        # appear in payload). Instead only take URBs that START with ff67 (the screen
        # frames are sent as their own URBs in practice).
        if h.startswith("ff67") and len(h) >= 10:
            yield h

def ascii_of(b):
    return ''.join(chr(c) if 32 <= c < 127 else '.' for c in b)

def main():
    pcap = sys.argv[1]
    ca_only = "--ca" in sys.argv
    dev = next((a for a in sys.argv[2:] if a.isdigit()), None)
    addrs = OrderedDict()  # b3b4 -> dict(len, asciis set, sample)
    for h in frames(pcap, dev):
        b = binascii.unhexlify(h)
        ln, b3, b4 = b[2], b[3], b[4]
        addr = f"{b3:02x}{b4:02x}"
        payload = b[5:-1]
        asc = ascii_of(payload)
        if addr not in addrs:
            addrs[addr] = {"len": ln, "asciis": OrderedDict(), "sample": h}
        # keep distinct non-empty ascii renderings (strip trailing dots)
        key = asc.rstrip('.')
        if key:
            addrs[addr]["asciis"][key] = addrs[addr]["asciis"].get(key, 0) + 1

    if ca_only:
        for addr, d in addrs.items():
            if d["len"] >= 0x40:
                for asc, n in d["asciis"].items():
                    print(f"[{addr}] len={d['len']:#x}  x{n}  '{asc}'")
        return

    print(f"{'addr':6} {'len':>4}  distinct-text")
    for addr, d in sorted(addrs.items()):
        texts = list(d["asciis"].keys())
        shown = "  |  ".join(texts[:4])
        more = f"  (+{len(texts)-4})" if len(texts) > 4 else ""
        print(f"{addr:6} {d['len']:>#4x}  {shown}{more}")

if __name__ == "__main__":
    main()
