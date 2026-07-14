#!/usr/bin/env python3
"""Enumerate SSL 360 Core->plugin CONTROL frames (register/subscribe) from a
loopback capture, to answer: how many distinct meter OBJECTS did the real Core
register/subscribe, and which object's stream carried RTA (DataType 8/9)?

SSL efbc frame body layout (28-byte header, from decode_capture.py + observed
subscribe frames):
    off0  u32 = 16
    off4  u32 = 1
    off8  u32 = seq/time id
    off12 u32 = inner_len
    off16 u32 = TYPE      (2 = register-object, 18 = subscribe, 3 = data, 12 = announce)
    off20 u32 = objId lo  }  8-byte object id for register/subscribe frames
    off24 u32 = objId hi  }  (269/0x10d at off24 for DATA frames instead)
    off28 protobuf
We scan BOTH directions on every TCP + UDP stream, tallying frame TYPEs and the
object ids referenced by type-2 / type-18 frames.  No reassembly beyond
single-segment efbc frames (control frames are small; we scan each payload).
"""
import sys, struct
from collections import defaultdict, Counter

MAGIC = b'\xef\xbc\x51\x00'

def read_packets(path):
    data = open(path, "rb").read()
    magic = struct.unpack_from("<I", data, 0)[0]
    if magic in (0xa1b2c3d4, 0xd4c3b2a1):
        end = "<" if magic == 0xa1b2c3d4 else ">"
        linktype = struct.unpack_from(end + "I", data, 20)[0]; i = 24
        while i + 16 <= len(data):
            _, _, caplen, _ = struct.unpack_from(end + "IIII", data, i); i += 16
            yield linktype, data[i:i+caplen]; i += caplen
    elif magic == 0x0a0d0d0a:
        i = 0; linktype = 1
        while i + 12 <= len(data):
            btype, blen = struct.unpack_from("<II", data, i)
            if blen == 0: break
            block = data[i:i+blen]
            if btype == 1: linktype = struct.unpack_from("<H", block, 8)[0]
            elif btype == 6:
                caplen = struct.unpack_from("<I", block, 20)[0]; yield linktype, block[28:28+caplen]
            elif btype == 3:
                caplen = struct.unpack_from("<I", block, 8)[0]; yield linktype, block[12:12+caplen]
            i += blen

def l4(linktype, pkt):
    off = 4 if linktype == 0 else (14 if linktype == 1 else 4)
    if len(pkt) < off + 20 or pkt[off] >> 4 != 4: return None
    proto = pkt[off+9]
    ihl = (pkt[off] & 0xf) * 4; t = off + ihl
    if proto == 17:   # UDP
        if len(pkt) < t + 8: return None
        sport, dport, ln = struct.unpack_from(">HHH", pkt, t)
        return "UDP", sport, dport, pkt[t+8:t+ln]
    if proto == 6:    # TCP
        if len(pkt) < t + 20: return None
        sport, dport = struct.unpack_from(">HH", pkt, t)
        doff = (pkt[t+12] >> 4) * 4
        return "TCP", sport, dport, pkt[t+doff:]
    return None

def scan_frames(payload):
    """yield (ftype, objid_or_None, inner_len, framelen) for each efbc frame."""
    p = 0
    while p + 8 <= len(payload) and payload[p:p+4] == MAGIC:
        flen = struct.unpack_from("<I", payload, p+4)[0]
        body = payload[p+8:p+8+flen]
        if len(body) < 28:
            break
        ftype = struct.unpack_from("<I", body, 16)[0]
        objid = None
        if ftype in (2, 18):
            objid = "%08x%08x" % (struct.unpack_from("<I", body, 24)[0],
                                  struct.unpack_from("<I", body, 20)[0])
        yield ftype, objid, struct.unpack_from("<I", body, 12)[0], flen
        p += 8 + flen

def main():
    path = sys.argv[1]
    # Per (proto,sport,dport) stream: type counts + object ids seen
    type_by_stream = defaultdict(Counter)
    objs_by_stream = defaultdict(Counter)
    total_types = Counter()
    for lt, pkt in read_packets(path):
        r = l4(lt, pkt)
        if not r: continue
        proto, sp, dp, pl = r
        if pl[:4] != MAGIC: continue
        key = (proto, sp, dp)
        for ftype, objid, ilen, flen in scan_frames(pl):
            type_by_stream[key][ftype] += 1
            total_types[ftype] += 1
            if objid is not None:
                objs_by_stream[key][objid] += 1

    print("=== efbc frame TYPE totals (all streams) ===")
    for ftype, n in sorted(total_types.items()):
        name = {2:"register-object", 3:"DATA", 12:"announce", 18:"subscribe"}.get(ftype, "?")
        print(f"  type {ftype:>3} ({name:<15}) : {n}")

    print("\n=== control streams carrying register(2)/subscribe(18) frames ===")
    for key in sorted(type_by_stream):
        tc = type_by_stream[key]
        if tc.get(2,0)+tc.get(18,0) == 0: continue
        proto, sp, dp = key
        print(f"\n  {proto} {sp}->{dp}  types={dict(tc)}")
        oc = objs_by_stream[key]
        print(f"    distinct objects: {len(oc)}")
        for oid, n in oc.most_common():
            print(f"      obj {oid}  x{n}")

if __name__ == "__main__":
    main()
