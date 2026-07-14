#!/usr/bin/env python3
"""Dump the protobuf payload of SSL control frames by TYPE, with ASCII + field
breakdown, to learn how a meter OBJECT declares which DataType it is (so the
impersonator can subscribe to the RTA object dynamically instead of replaying a
stale id)."""
import sys, struct
from collections import defaultdict, Counter

MAGIC = b'\xef\xbc\x51\x00'

def read_packets(path):
    data = open(path, "rb").read()
    magic = struct.unpack_from("<I", data, 0)[0]
    end = "<" if magic == 0xa1b2c3d4 else ">"
    linktype = struct.unpack_from(end + "I", data, 20)[0]; i = 24
    while i + 16 <= len(data):
        _, _, caplen, _ = struct.unpack_from(end + "IIII", data, i); i += 16
        yield linktype, data[i:i+caplen]; i += caplen

def l4(lt, pkt):
    off = 4 if lt == 0 else (14 if lt == 1 else 4)
    if len(pkt) < off + 20 or pkt[off] >> 4 != 4: return None
    proto = pkt[off+9]; ihl = (pkt[off]&0xf)*4; t = off+ihl
    if proto == 6:
        if len(pkt) < t+20: return None
        sp, dp = struct.unpack_from(">HH", pkt, t)
        doff = (pkt[t+12] >> 4)*4
        return sp, dp, pkt[t+doff:]
    if proto == 17:
        if len(pkt) < t+8: return None
        sp, dp, ln = struct.unpack_from(">HHH", pkt, t)
        return sp, dp, pkt[t+8:t+ln]
    return None

def rv(b,i):
    r=0;s=0
    while i<len(b):
        x=b[i];i+=1;r|=(x&0x7f)<<s
        if not(x&0x80):return r,i
        s+=7
    return r,i

def parse_pb(b, depth=0):
    out=[]; i=0
    while i<len(b):
        try: tag,i=rv(b,i)
        except: break
        fn=tag>>3; wt=tag&7
        if fn==0: break
        if wt==0:
            v,i=rv(b,i); out.append((fn,0,v))
        elif wt==5:
            if i+4>len(b):break
            out.append((fn,5,struct.unpack_from("<f",b,i)[0])); i+=4
        elif wt==1:
            if i+8>len(b):break
            out.append((fn,1,struct.unpack_from("<Q",b,i)[0])); i+=8
        elif wt==2:
            ln,i=rv(b,i)
            if i+ln>len(b):break
            out.append((fn,2,b[i:i+ln])); i+=ln
        else: break
    return out

def ascii_of(b):
    return "".join(chr(c) if 32<=c<127 else "." for c in b)

def main():
    path=sys.argv[1]
    want=set(int(x) for x in sys.argv[2:]) or {2,17,19}
    shown=Counter()
    strings_by_type=defaultdict(Counter)
    for lt,pkt in read_packets(path):
        r=l4(lt,pkt)
        if not r: continue
        sp,dp,pl=r
        if pl[:4]!=MAGIC: continue
        p=0
        while p+8<=len(pl) and pl[p:p+4]==MAGIC:
            flen=struct.unpack_from("<I",pl,p+4)[0]
            body=pl[p+8:p+8+flen]; p+=8+flen
            if len(body)<28: continue
            ftype=struct.unpack_from("<I",body,16)[0]
            if ftype not in want: continue
            pb=body[28:]
            # collect any embedded ASCII strings (len>=3) from length-delim fields
            for fn,wt,v in parse_pb(pb):
                if wt==2 and isinstance(v,(bytes,bytearray)):
                    s=ascii_of(v)
                    if sum(1 for c in v if 32<=c<127)>=3 and len(v)>=3:
                        strings_by_type[ftype][s]+=1
                    # one level deeper
                    for fn2,wt2,v2 in parse_pb(v):
                        if wt2==2 and isinstance(v2,(bytes,bytearray)) and sum(1 for c in v2 if 32<=c<127)>=3:
                            strings_by_type[ftype][ascii_of(v2)]+=1
            if shown[ftype]<4:
                shown[ftype]+=1
                objid="%08x%08x"%(struct.unpack_from("<I",body,24)[0],struct.unpack_from("<I",body,20)[0]) if ftype in (2,18) else "-"
                print(f"\n--- type {ftype} sp={sp} dp={dp} flen={flen} obj={objid} ---")
                print("  hdr:", body[:28].hex())
                print("  pb :", pb.hex()[:200])
                print("  fields:", [(fn,wt,(v.hex() if isinstance(v,(bytes,bytearray)) else round(v,3) if wt==5 else v)) for fn,wt,v in parse_pb(pb)][:14])
                print("  ascii:", ascii_of(pb)[:120])
    print("\n\n=== embedded strings by frame type ===")
    for ft in sorted(strings_by_type):
        print(f"\ntype {ft}:")
        for s,n in strings_by_type[ft].most_common(30):
            print(f"   x{n:<5} {s!r}")

if __name__=="__main__":
    main()
