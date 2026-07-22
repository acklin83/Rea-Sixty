#!/usr/bin/env python3
"""SSL→UF1 meter capture analyzer (USBPcap, PID_0025).

Parses host->device FF67 frames and helps find the CHANGE events in a meter
capture (param edits, page/screen switches, scale selectors) against the
continuous meter stream.

Usage:
  uf1_meter_capture_analyze.py <pcap> [--dev N] [--hist] [--timeline] [--elem 0xNNNN]
    --dev N     UF1 USB device_address (default: auto = the busiest OUT device)
    --hist      element histogram (id, count, distinct payloads)   [default if no mode]
    --timeline  non-streaming elements over time, ASCII, dedup consecutive repeats
    --elem      dump only this element id (hex), every occurrence

Frame: FF 67 <len> <elemHi> <elemLo> <payload...> <cksum>; elem = bytes 3-4.
OUT (SSL->UF1) = endpoint 0x02. See docs/session-2026-07-22-uf1-meter-capture.md
for the element meanings (0x010e labels, 0x011a scale selector, 0x0100/0x0102/
0x0104/0x010d/0x011f button+screen machinery, 0x0122 image/history, ...).
"""
import subprocess, binascii, sys, argparse
from collections import defaultdict, Counter

TSHARK = "/opt/homebrew/bin/tshark"

# High-rate content elements (per-screen); NOT setup/change events.
STREAM = {0x0122, 0x011c, 0x0009, 0x000a, 0x0015, 0x0016, 0x011d,
          0x0123, 0x0124, 0x0125, 0x0126, 0x0127, 0x0128, 0x012a}


def frames(pcap, dev):
    df = f"usb.device_address=={dev} && usb.endpoint_address==0x02 && usb.capdata"
    out = subprocess.run(
        [TSHARK, "-r", pcap, "-Y", df, "-T", "fields",
         "-e", "frame.time_relative", "-e", "usb.capdata"],
        capture_output=True, text=True).stdout
    for line in out.splitlines():
        p = line.split("\t")
        if len(p) < 2:
            continue
        try:
            ts = float(p[0]); b = binascii.unhexlify(p[1].strip().lower())
        except Exception:
            continue
        i = 0
        while i + 4 <= len(b):
            if b[i] != 0xFF:
                i += 1; continue
            ln = b[i + 2]; end = i + 3 + ln + 1
            if end > len(b):
                break
            f = b[i:end]
            if f[1] == 0x67 and len(f) >= 6:
                yield ts, (f[3] << 8) | f[4], f[5:-1]
            i = end


def busiest_out_dev(pcap):
    out = subprocess.run(
        [TSHARK, "-r", pcap, "-Y", "usb.endpoint_address==0x02 && usb.capdata",
         "-T", "fields", "-e", "usb.device_address"],
        capture_output=True, text=True).stdout
    c = Counter(x for x in out.split() if x)
    return int(c.most_common(1)[0][0]) if c else 0


def asc(pl):
    return "".join(chr(c) if 32 <= c < 127 else "." for c in pl)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pcap")
    ap.add_argument("--dev", type=int, default=None)
    ap.add_argument("--hist", action="store_true")
    ap.add_argument("--timeline", action="store_true")
    ap.add_argument("--elem", default=None)
    a = ap.parse_args()
    dev = a.dev if a.dev is not None else busiest_out_dev(a.pcap)
    print(f"# {a.pcap}  dev={dev}")
    elem = int(a.elem, 16) if a.elem else None
    if not (a.hist or a.timeline or elem):
        a.hist = True

    if a.hist:
        info = defaultdict(lambda: [0, set()])
        for _, e, pl in frames(a.pcap, dev):
            info[e][0] += 1; info[e][1].add(pl)
        print(" id      count  distinct")
        for e in sorted(info, key=lambda x: -info[x][0]):
            print(f"0x{e:04x}  {info[e][0]:7d}  {len(info[e][1]):5d}")

    if a.timeline:
        prev = None
        for ts, e, pl in frames(a.pcap, dev):
            if e in STREAM:
                continue
            if (e, pl) == prev:
                continue
            prev = (e, pl)
            print(f"{ts:8.3f} 0x{e:04x} l{len(pl):2d} {pl.hex():<44s} |{asc(pl)}|")

    if elem is not None:
        for ts, e, pl in frames(a.pcap, dev):
            if e == elem:
                print(f"{ts:8.3f} 0x{e:04x} l{len(pl):2d} {pl.hex():<44s} |{asc(pl)}|")


if __name__ == "__main__":
    main()
