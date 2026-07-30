#!/usr/bin/env python3
"""Decode 0x0122 sub-frames in a Loudness capture. Find the LIVE left-bar region.

The two left level bars live in 0x0122 SUB-FRAME 0 (the "axis" sf): idx 26 = Momentary
(DataType 11), idx 27 = Short-Term (DataType 12). Both use the history's plot Y law
  byte = clamp((LUFS - target - rangeBottom)*180/span, 0, 180)
(target=param36, range=param47). Proven 2026-07-29 from cap120/121/122: idx27 fits the
0x011c Short-Term readout to <0.6 byte across all 3 ranges; idx26 leads it = Momentary.

Modes:
  sub      : list sub-frame selectors (byte0), lengths, count, distinct payloads
  var SF   : per-byte variance map for sub-frame SF (min/max/distinct over time)
  track SF I0 I1 : dump byte indices I0..I1 of sub-frame SF over time (dedup consec)
  readout  : dump 0x011c field-1 (Short-Term) readout text over time
  fit      : least-squares fit of sf0 idx26/idx27 vs the Short-Term readout
"""
import subprocess, binascii, sys
from collections import defaultdict, Counter

TSHARK = "/opt/homebrew/bin/tshark"

def busiest_out_dev(pcap):
    out = subprocess.run([TSHARK, "-r", pcap, "-Y",
        "usb.endpoint_address==0x02 && usb.capdata", "-T", "fields",
        "-e", "usb.device_address"], capture_output=True, text=True).stdout
    c = Counter(x for x in out.split() if x)
    return int(c.most_common(1)[0][0]) if c else 0

def frames(pcap, dev):
    df = f"usb.device_address=={dev} && usb.endpoint_address==0x02 && usb.capdata"
    out = subprocess.run([TSHARK, "-r", pcap, "-Y", df, "-T", "fields",
        "-e", "frame.time_relative", "-e", "usb.capdata"],
        capture_output=True, text=True).stdout
    for line in out.splitlines():
        p = line.split("\t")
        if len(p) < 2: continue
        try:
            ts = float(p[0]); b = binascii.unhexlify(p[1].strip().lower())
        except Exception:
            continue
        i = 0
        while i + 4 <= len(b):
            if b[i] != 0xFF:
                i += 1; continue
            ln = b[i + 2]; end = i + 3 + ln + 1
            if end > len(b): break
            f = b[i:end]
            if f[1] == 0x67 and len(f) >= 6:
                yield ts, (f[3] << 8) | f[4], f[5:-1]
            i = end

def asc(pl):
    return "".join(chr(c) if 32 <= c < 127 else "." for c in pl)

def main():
    pcap = sys.argv[1]; mode = sys.argv[2] if len(sys.argv) > 2 else "sub"
    dev = busiest_out_dev(pcap)
    sys.stderr.write(f"# {pcap} dev={dev}\n")

    if mode == "sub":
        # group 0x0122 by selector byte0
        by = defaultdict(lambda: [0, set(), set()])  # count, lengths, distinct payloads
        for ts, el, pl in frames(pcap, dev):
            if el != 0x0122 or not pl: continue
            g = by[pl[0]]
            g[0] += 1; g[1].add(len(pl)); g[2].add(bytes(pl))
        for sel in sorted(by):
            c, lens, dist = by[sel]
            print(f"sf sel=0x{sel:02x} count={c} lens={sorted(lens)} distinct={len(dist)}")
        return

    if mode == "var":
        SF = int(sys.argv[3])
        cols = defaultdict(list)  # idx -> list of values
        n = 0
        for ts, el, pl in frames(pcap, dev):
            if el != 0x0122 or not pl or pl[0] != SF: continue
            n += 1
            for i, v in enumerate(pl):
                cols[i].append(v)
        print(f"# sf={SF} frames={n}")
        # summarise runs of behaviour: for each idx print min,max,distinct if varies
        L = max(cols) + 1 if cols else 0
        print("idx  min  max  dist   (span=max-min)")
        for i in range(L):
            vs = cols[i]
            mn, mx = min(vs), max(vs); d = len(set(vs))
            flag = ""
            if d > 3 and (mx - mn) > 8: flag = " <== LIVE?"
            if d == 1: flag = " (const)"
            print(f"{i:3d}  {mn:3d}  {mx:3d}  {d:4d}   span={mx-mn:3d}{flag}")
        return

    if mode == "track":
        SF = int(sys.argv[3]); i0 = int(sys.argv[4]); i1 = int(sys.argv[5])
        prev = None
        for ts, el, pl in frames(pcap, dev):
            if el != 0x0122 or not pl or pl[0] != SF: continue
            seg = tuple(pl[i0:i1+1])
            if seg != prev:
                prev = seg
                print(f"{ts:7.2f}  " + " ".join(f"{v:3d}" for v in seg))
        return

    if mode == "readout":
        prev = None
        for ts, el, pl in frames(pcap, dev):
            if el != 0x011c: continue
            # 4x25 fields; field1 = Short-Term
            f1 = asc(pl[25:50]) if len(pl) >= 50 else asc(pl)
            if f1 != prev:
                prev = f1
                print(f"{ts:7.2f}  ST=[{f1}]  I=[{asc(pl[0:25])}]")
        return

    if mode == "fit":
        fit_mode()
        return

def fit_mode():
    import re
    pcap = sys.argv[1]; dev = busiest_out_dev(pcap)
    # walk frames in time order, keep latest ST readout, emit (b26,b27,b28,ST)
    lastST = None
    pairs = []
    numre = re.compile(r"-?\d+\.\d+")
    for ts, el, pl in frames(pcap, dev):
        if el == 0x011c and len(pl) >= 50:
            m = numre.search(asc(pl[25:50]))
            if m: lastST = float(m.group())
        elif el == 0x0122 and pl and pl[0] == 0 and lastST is not None:
            pairs.append((ts, pl[26], pl[27], pl[28], lastST))
    # least squares b = a*ST + c, only for non-saturated (0<b<180)
    def fit(col):
        xs = [(p[4], p[col]) for p in pairs if 2 < p[col] < 179]
        if len(xs) < 10: return None
        n=len(xs); sx=sum(x for x,_ in xs); sy=sum(y for _,y in xs)
        sxx=sum(x*x for x,_ in xs); sxy=sum(x*y for x,y in xs)
        a=(n*sxy-sx*sy)/(n*sxx-sx*sx); c=(sy-a*sx)/n
        # residual
        res=max(abs(y-(a*x+c)) for x,y in xs)
        return a,c,res,n
    print(f"# pairs={len(pairs)}")
    for col,name in [(1,'b26'),(2,'b27')]:
        r=fit(col)
        if r: print(f"{name}: byte = {r[0]:.3f}*ST + {r[1]:.2f}   (maxres={r[2]:.1f}, n={r[3]})")
    # also print a sample of pairs during decay (unsaturated)
    print("# sample (ts b26 b27 b28 ST):")
    for p in pairs:
        if 5 < p[1] < 178 and 5 < p[2] < 178:
            print(f"  {p[0]:6.2f}  {p[1]:3d} {p[2]:3d} {p[3]:3d}  ST={p[4]}")

if __name__ == "__main__":
    main()
