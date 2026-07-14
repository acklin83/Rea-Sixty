#!/usr/bin/env python3
"""UF1 meter-screen setup-burst extractor.

Finds the meter SCREEN transitions in a capture (Overview/Analogue/RTA/Loudness)
and prints, per transition, the host->device FF67 frames SSL 360 sent to set the
screen up — byte-exact, in wire order.

Transitions are located by the 0x0104 soft-key label (index 0), which carries the
screen name (cap76). High-rate animated elements (the meter graphic + readouts)
are summarised rather than dumped, so what's left IS the setup burst.

Usage:
  uf1_meter_screen_burst.py <pcapng> [--dev N] [--window 0.6] [--all]
    --all      also dump the animated elements (0x0122/0x011c/...)
"""
import sys, subprocess, binascii
from collections import OrderedDict

TSHARK = "/opt/homebrew/bin/tshark"

# Elements that stream continuously with audio (cap76: ~600 frames/s). They are
# CONTENT, not setup — summarise unless --all.
ANIMATED = {0x0122, 0x011c, 0x0125, 0x0126, 0x0127, 0x000c, 0x0009, 0x000a}

SCREEN_NAMES = ("OVERVIEW", "ANALOGUE", "RTA", "LOUDNESS")


def out_frames(pcap, dev=None):
    """Yield (ts, addr, payload_bytes, full_hex) for each host->device FF67 frame."""
    dfilter = "usb.endpoint_address==0x02 && usb.capdata"
    if dev:
        dfilter = f"usb.device_address=={dev} && " + dfilter
    out = subprocess.run(
        [TSHARK, "-r", pcap, "-Y", dfilter, "-T", "fields",
         "-e", "frame.time_relative", "-e", "usb.capdata"],
        capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split("\t")
        if len(parts) < 2:
            continue
        try:
            ts = float(parts[0])
            b = binascii.unhexlify(parts[1].strip().lower())
        except Exception:
            continue
        i = 0
        while i + 4 <= len(b):
            if b[i] != 0xFF:
                i += 1
                continue
            ln = b[i + 2]
            end = i + 3 + ln + 1
            if end > len(b):
                break
            f = b[i:end]
            if f[1] == 0x67 and len(f) >= 6:
                addr = (f[3] << 8) | f[4]
                yield ts, addr, f[5:-1], binascii.hexlify(f).decode()
            i = end


def ascii_of(b):
    return "".join(chr(c) if 32 <= c < 127 else "." for c in b)


def screen_of(payload):
    """0x0104 index-0 payload -> screen name, or None."""
    if not payload or payload[0] != 0x00:
        return None
    txt = ascii_of(payload[1:]).strip(". ")
    return txt if txt in SCREEN_NAMES else None


def main():
    pcap = sys.argv[1]
    dump_all = "--all" in sys.argv
    dev = None
    window = 0.6
    for i, a in enumerate(sys.argv):
        if a == "--dev":
            dev = sys.argv[i + 1]
        if a == "--window":
            window = float(sys.argv[i + 1])

    frames = list(out_frames(pcap, dev))
    print(f"# {len(frames)} host->device FF67 frames\n")

    # Locate screen changes via the 0x0104 index-0 label.
    marks = []          # (ts, screen)
    cur = None
    for ts, addr, payload, _ in frames:
        if addr != 0x0104:
            continue
        s = screen_of(payload)
        if s and s != cur:
            marks.append((ts, s))
            cur = s

    if not marks:
        print("No 0x0104 screen labels found — wrong capture or wrong --dev?")
        return
    print("## Screen timeline (from 0x0104 label)")
    for ts, s in marks:
        print(f"  t={ts:8.3f}  {s}")
    print()

    # Per transition, dump the burst in WIRE ORDER. The burst lands within a few
    # ms of the 0x0104 label (cap76: all inside ~1.5ms), so a tight forward window
    # isolates it from the outgoing screen's steady-state traffic. Order matters:
    # this is meant to be replayed byte-for-byte.
    for ts, s in marks:
        lo, hi = ts - 0.005, ts + window
        print(f"\n{'='*72}\n## -> {s}   (burst from t={ts:.3f}, +{window}s, WIRE ORDER)\n{'='*72}")
        n_anim = OrderedDict()
        for fts, addr, payload, fhex in frames:
            if not (lo <= fts <= hi):
                continue
            if addr in ANIMATED and not dump_all:
                n_anim[addr] = n_anim.get(addr, 0) + 1
                continue
            hexs = binascii.hexlify(payload).decode()
            asc = ascii_of(payload)
            print(f"  [{addr:#06x}] t={fts:8.3f} len={len(payload):<3} "
                  f"{hexs}  '{asc}'")
        if n_anim:
            summ = "  ".join(f"{a:#06x} x{n}" for a, n in sorted(n_anim.items()))
            print(f"  -- animated (suppressed): {summ}")


if __name__ == "__main__":
    main()
