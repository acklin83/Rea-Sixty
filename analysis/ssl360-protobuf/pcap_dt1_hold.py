#!/usr/bin/env python3
"""pcap_dt1_hold.py <pcap> [<dataType>]  — the TextVuPpm readout, laid bare.

One pass over a localhost pcap (linktype 0 loopback, IPv4/UDP, SSL efbc frames),
then per UDP flow (src port -> dst port): every CHANGE of the first channel of
the given DataType (default 1 = TextVuPpm) with timestamps, the HOLDS (a value
that stands >0.4 s and is then followed by a drop) with the implied underlying
fall rate, and the CONTINUOUS falls (>= 8 monotone steps < 0.1 s apart) with
their dB/s.  Written 2026-09-11 to settle the UF1 VU needle after Meter Pro 1.3.7
dropped VuPpm(0): dt=1 holds every local maximum ~0.5 s and then jumps; beneath
it the VU falls linearly in dB at ~43 dB/s.  Flows to 16008-16013/50881/50882
are OURS (the impersonator); anything else is SSL 360's own Core.  Both sides
carry the same frames from the same plug-in instance — separate them anyway.
Channel strips reuse the low type indices for their own meters (Input/Output/
CompGain…), so flows with tiny sub-dB "falls" are strips, not Meters; the Meter
Pro flow is the one whose dt=1 swings tens of dB and holds ~0.5 s.
"""
import struct, sys, collections
MAGIC = b'\xef\xbc\x51\x00'
OURS = {16008, 16009, 16010, 16011, 16012, 16013, 50881, 50882}

def varint(b, i):
    r = 0; s = 0
    while i < len(b):
        c = b[i]; r |= (c & 0x7f) << s; i += 1; s += 7
        if not c & 0x80: return r, i
        if s > 70: return None, i
    return None, i

def parse_meter(pb):
    """f2 = DataType, f3 = current (fixed32, single or packed), f4 = peak."""
    dt = None; cur = []; pk = []; i = 0
    while i < len(pb):
        tag, i = varint(pb, i)
        if tag is None: break
        fn = tag >> 3; wt = tag & 7
        if wt == 0:
            v, i = varint(pb, i)
            if v is None: break
            if fn == 2: dt = v
        elif wt == 1: i += 8
        elif wt == 5:
            if i + 4 > len(pb): break
            v = struct.unpack('<f', pb[i:i+4])[0]; i += 4
            if fn == 3: cur.append(v)
            elif fn == 4: pk.append(v)
        elif wt == 2:
            ln, i = varint(pb, i)
            if ln is None or i + ln > len(pb): break
            b2 = pb[i:i+ln]; i += ln
            if fn in (3, 4):
                vv = [struct.unpack('<f', b2[q:q+4])[0] for q in range(0, len(b2) - 3, 4)]
                (cur if fn == 3 else pk).extend(vv)
        else: break
    return dt, cur, pk

def read(path, want):
    rows = collections.defaultdict(list)          # (sp,dp) -> [(t, c0, k0)]
    f = open(path, 'rb'); f.read(24)
    while True:
        ph = f.read(16)
        if len(ph) < 16: break
        ts, tu, il, _ = struct.unpack('<IIII', ph); d = f.read(il)
        if len(d) < il: break
        ip = d[4:]
        if len(ip) < 20 or ip[0] >> 4 != 4 or ip[9] != 17: continue
        l4 = ip[(ip[0] & 0xf) * 4:]
        sp, dp, ln = struct.unpack('>HHH', l4[:6]); body = l4[8:ln]
        i = 0
        while True:
            j = body.find(MAGIC, i)
            if j < 0 or j + 8 > len(body): break
            flen = struct.unpack('<I', body[j+4:j+8])[0]
            if flen < 28 or j + 8 + flen > len(body): i = j + 4; continue
            fb = body[j+8:j+8+flen]
            if struct.unpack('<I', fb[16:20])[0] == 3:
                dt, cur, pk = parse_meter(fb[28:])
                if dt == want and cur and cur[0] == cur[0]:     # finite only
                    rows[(sp, dp)].append((ts + tu / 1e6, cur[0], pk[0] if pk else float('nan')))
            i = j + 8 + flen
    return rows

def analyse(label, rows):
    t0 = rows[0][0]
    changes = []; prev = None
    for t, c, k in rows:
        if prev is None or c != prev: changes.append((t - t0, c, k)); prev = c
    print("\n== %s: %d frames, %d changes, %.1f s" % (label, len(rows), len(changes), rows[-1][0] - t0))
    holds = []
    for a, b in zip(changes, changes[1:]):
        if b[0] - a[0] > 0.4 and b[1] < a[1] - 1.0:
            holds.append((a[0], a[1], b[0] - a[0], b[1], (a[1] - b[1]) / (b[0] - a[0])))
    print("  HOLDS  t_start   held      for      then     (implied dB/s underneath)")
    for h in holds[:15]: print("         %7.2f %8.2f  %.3f s %8.2f  (%.1f)" % h)
    seg = [changes[0]]; segs = []
    for r in changes[1:]:
        if r[1] < seg[-1][1] and r[0] - seg[-1][0] < 0.1: seg.append(r)
        else:
            if len(seg) >= 8: segs.append(seg)
            seg = [r]
    if len(seg) >= 8: segs.append(seg)
    print("  FALLS  from      to       over      dB/s   steps  t")
    for s in segs[:15]:
        dur = s[-1][0] - s[0][0]
        print("         %7.2f %8.2f  %.3f s %6.1f  %4d  %.2f" % (s[0][1], s[-1][1], dur, (s[0][1] - s[-1][1]) / dur, len(s), s[0][0]))

if __name__ == '__main__':
    want = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    rows = read(sys.argv[1], want)
    for (sp, dp), r in sorted(rows.items(), key=lambda x: -len(x[1])):
        if len(r) < 50: continue
        analyse("%d -> %d %s dt=%d" % (sp, dp, 'OURS' if dp in OURS else '360', want), r)
