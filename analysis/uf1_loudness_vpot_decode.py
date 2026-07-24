#!/usr/bin/env python3
"""UF1 Meter V-Pot page decoder — reads SSL->UF1 0x010e label groups from a USBPcap
capture and prints the per-page V2/V3/V4 parameter assignment, classified against the
SSL Meter Pro param dump. Built to finish the Loudness V-Pot table (pages 8-10) without
re-deriving the tooling every session.

Usage:
  uf1_loudness_vpot_decode.py <pcap>            # chronological distinct V-Pot pages
  uf1_loudness_vpot_decode.py <pcap> --count    # just count FF67 frames (preflight)
  uf1_loudness_vpot_decode.py <pcap> --raw      # every distinct (index,value), unclassified

Frame model: OUT ep 0x02, FF 67 <len> <b3> <b4> <payload..> <ck>; V-Pot labels are
b3b4 = 0x010e, payload = [index 0..3][ label ]. index 0 = instance (track name); 1/2/3
= V-Pot2/3/4. name+value slots pad the name to 8 bytes with NULs then the value;
name-only slots carry the value alone. SSL leads every group with index 0.
"""
import sys, subprocess, binascii, re
from collections import OrderedDict

TSHARK = "/opt/homebrew/bin/tshark"

# SSL Meter Pro param dump (docs/ssl-native-params/VST3__SSL_Meter_Pro_(SSL).md).
# key -> (paramIdx, humanName). key = the 8-byte name field (name+value slots) or the
# value's leading token (name-only slots). Confirmed from cap109 for the 7 known pages;
# the trailing block is the EXPECTED vocabulary for the not-yet-captured params 40-45/50
# so a fresh pages-8-10 capture auto-identifies them (verify, don't trust blindly).
KNOWN = {
    # --- name-only (value is the label) ---
    "DAW Sync": 24, "SystemClk": 24, "Continuous": 24, "Auto P+R": 24, "Auto Pause": 24,
    "Gate Off": 30, "Prog": 30,                       # Int Loudness Gating Mode
    "-18 to +9": 47, "-36 to +18": 47, "-54 to +27": 47,
    "Absolute": 48, "Relative": 48,
    "LU(FS)": 49, "LK(FS)": 49,
    "AutoScroll": 26,
    "Scroll Timeline": -100,                          # GUI scroll action — NO param
    # --- name+value (8-byte name field) ---
    "Var": 37, "Short": 33, "Mom": 32, "MinDia": 29, "Mode": 34, "Surr": 35,
    "TrueP": 27, "Ovrlp": 31, "Dial": 28, "LdIntA": 38, "IDAlrt": 39, "TPMax": 46,
    # --- EXPECTED on pages 8-10 (unconfirmed abbreviations — update when captured) ---
    # 40 Short Term Max Alert, 41 Momentary Max Alert, 42/43 Loudness Range Min/Max,
    # 44/45 Dialogue Range Min/Max, 50 Play/Pause ("Play"/"Pause").
    "Play": 50, "Pause": 50,
}
# value-vocabulary fallbacks for name-only numeric enums (History Window / Int Target).
def classify(value):
    v = value.strip("\x00").strip()
    if not v:
        return (None, "<blank>")
    # name+value: 8-byte name field then value -> split on first NUL
    if "\x00" in value:
        nm = value.split("\x00", 1)[0]
        return (KNOWN.get(nm), nm)
    # name-only exact hits
    if v in KNOWN:
        return (KNOWN[v], v)
    for k in ("Prog", "AutoScroll"):
        if v.startswith(k):
            return (KNOWN[k], k)
    # numeric name-only enums
    if re.match(r"^\d+(\.\d+)?\s*(secs?|mins?|hrs?)$", v) or v in ("1 day", "Auto Condense"):
        return (25, "HistWindow")                     # Loudness History Window Size
    if re.match(r"^-?\d+(\.\d+)?\s*L[KU]FS$", v):
        return (36, "IntTarget")                      # Loudness Integrated Target
    if re.match(r"^-?\d+(\.\d+)?\s*LU$", v):
        return (None, "?RangeAlert(LU)")              # likely 42-45 — CONFIRM
    return (None, "?" + re.sub(r"[\d.\-]+.*$", "", v).strip())


def frames(pcap):
    out = subprocess.run(
        [TSHARK, "-r", pcap, "-Y", "usb.endpoint_address==0x02 && usb.capdata",
         "-T", "fields", "-e", "frame.time_relative", "-e", "usb.capdata"],
        capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split("\t")
        if len(parts) < 2:
            continue
        try:
            t = float(parts[0]); b = binascii.unhexlify(parts[1].strip())
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
                yield t, f
            i = end


def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    pcap = sys.argv[1]
    mode = sys.argv[2] if len(sys.argv) > 2 else ""

    if mode == "--count":
        n = sum(1 for _ in frames(pcap))
        print(f"FF67 frames: {n}")
        return

    def asc(bs):
        return "".join(chr(c) if c else "\x00" for c in bs)

    # 0x010e groups (index-0-led)
    groups = []; cur = {}
    for _, f in frames(pcap):
        if f[3] == 0x01 and f[4] == 0x0e:
            idx = f[5]; val = asc(f[6:-1])
            if idx == 0 and cur:
                groups.append(cur); cur = {}
            if idx in (1, 2, 3):
                cur[idx] = val
    if cur:
        groups.append(cur)

    if mode == "--raw":
        seen = {1: OrderedDict(), 2: OrderedDict(), 3: OrderedDict()}
        for g in groups:
            for k in (1, 2, 3):
                if k in g:
                    seen[k][g[k].replace("\x00", "·")] = 1
        for k in (1, 2, 3):
            print(f"\n=== V-Pot{k+1} distinct values ===")
            for v in seen[k]:
                print("  ", repr(v))
        return

    # chronological distinct page-triples (by param identity, not value)
    prev = {1: None, 2: None, 3: None}; pages = []
    for g in groups:
        st = {k: g.get(k, prev[k]) for k in (1, 2, 3)}
        pid = {k: (classify(st[k])[0] if st[k] is not None else None) for k in (1, 2, 3)}
        trip = (pid[1], pid[2], pid[3])
        if not pages or pages[-1][0] != trip:
            pages.append((trip, {k: st[k] for k in (1, 2, 3)}))
        prev = st
    distinct = list(OrderedDict((p[0], p[1]) for p in pages).items())
    print(f"{len(groups)} label groups -> {len(distinct)} distinct V-Pot pages (chronological):\n")
    for i, (trip, sample) in enumerate(distinct):
        cells = []
        for k in (1, 2, 3):
            p = trip[k - 1]
            _, nm = classify(sample[k]) if sample[k] is not None else (None, "-")
            tag = f"param {p}" if isinstance(p, int) and p >= 0 else \
                  ("NO-PARAM" if p == -100 else "?? UNKNOWN — map me")
            v = (sample[k] or "").replace("\x00", "·")
            cells.append(f"V{k+1}={nm:<10}[{tag}]  e.g.'{v}'")
        print(f"  page {i}:  " + "  |  ".join(cells))
    print("\n'?? UNKNOWN' slots = params not in the known map (expected on pages 8-10: "
          "40 ShortTermMax, 41 MomentaryMax, 42/43 LoudRange, 44/45 DialogueRange, 50 Play/Pause).")


if __name__ == "__main__":
    main()
