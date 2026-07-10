#!/usr/bin/env python3
"""Decode SSL 360° plugin->Core METER data from a loopback UDP capture.

Usage:  python3 decode_capture.py <file.pcap> [--datatype N] [--values]

WIRE FORMAT (reverse-engineered 2026-07-10, ssl360_meter_loopback.pcap):

  Meter data flows plugin -> 360°Core over **UDP** (one Core data port per meter
  instance — e.g. 16010, 50881; the port is negotiated via ServerConfigMessage /
  the announcement, see README). It is **plain protobuf, unencrypted**.

  Each UDP datagram = one or more SSL frames:
      [magic 'ef bc 51 00'] [uint32 LE frame_len] [frame_body ...]
  frame_body = a 28-byte SSL/Lgx header then ONE protobuf PluginMeterDataMessage:
      off 0  uint32 = 16          (message class)
      off 4  uint32 = 1
      off 8  uint32 = seq/time id
      off12  uint32 = inner_len
      off16  uint32 = 3
      off20  uint32 = stream id
      off24  uint32 = 269 (0x10d, "meter data" msg-type marker)
      off28  protobuf PluginMeterDataMessage:
             field1 PluginType, field2 DataType (MeterPluginDataType),
             field3 repeated fixed32 CurrentMeterValues (tag 0x1d),
             field4 repeated fixed32 PeakValues,
             field5/6 repeated bool Overload*, field7 MaxValueCount, ...
  Silence sentinel float = 0x ff c0 00 00 (-NaN) => treat as -inf.

Values are ALREADY-COMPUTED floats (dBFS for bars/text, -1..1 correlation, 0..1
Lissajous/history). No DSP, no pixel-reverse: switch on DataType, encode to the
UF1 codec.  Full schema: AssignerArgsTypes.reconstructed.proto.txt.
"""
import sys, struct, math
from collections import defaultdict

MAGIC = b'\xef\xbc\x51\x00'
HDR   = 28

MET = {0:"VuPpm",1:"TextVuPpm",2:"BarPeak",3:"BarRms",4:"TextPeak",5:"TextRms",
6:"Correlation",7:"StereoBalance",8:"Rta31Band",9:"TextRta",10:"Lissajous",
11:"Loud_Momentary",12:"Loud_ShortTerm",13:"Loud_RangeLow",14:"Loud_RangeHigh",
15:"Loud_Readout1",16:"Loud_Readout2",17:"Loud_Readout3",18:"Loud_Readout4",
19:"Loud_Readout5",20:"Loud_Readout6",21:"Loud_Readout7",22:"Loud_Readout8",
23:"Loud_Readout9",24:"Loud_Readout10",25:"Loud_CompleteHistory",
26:"Loud_ScrollableHistory",27:"Loud_Histogram"}

# ---- varint + protobuf ----------------------------------------------------
def rv(b,i):
    r=0;s=0
    while True:
        x=b[i];i+=1;r|=(x&0x7f)<<s
        if not (x&0x80):return r,i
        s+=7

def parse_pb(b):
    i=0;L=len(b);d=defaultdict(list)
    while i<L:
        tag,i=rv(b,i);fn=tag>>3;wt=tag&7
        if fn==0 or wt in (3,4,6,7):break
        if wt==0:v,i=rv(b,i)
        elif wt==2:
            ln,i=rv(b,i)
            if i+ln>L:break
            v=b[i:i+ln];i+=ln
        elif wt==5:
            if i+4>L:break
            v=struct.unpack_from("<f",b,i)[0];i+=4
        elif wt==1:
            if i+8>L:break
            v=struct.unpack_from("<d",b,i)[0];i+=8
        d[(fn,wt)].append(v)
    return d

# ---- pcap/pcapng reader ---------------------------------------------------
def read_packets(path):
    data=open(path,"rb").read()
    magic=struct.unpack_from("<I",data,0)[0]
    if magic in (0xa1b2c3d4,0xd4c3b2a1):
        end="<" if magic==0xa1b2c3d4 else ">"
        linktype=struct.unpack_from(end+"I",data,20)[0];i=24
        while i+16<=len(data):
            _,_,caplen,_=struct.unpack_from(end+"IIII",data,i);i+=16
            yield linktype,data[i:i+caplen];i+=caplen
    elif magic==0x0a0d0d0a:
        i=0;linktype=1
        while i+12<=len(data):
            btype,blen=struct.unpack_from("<II",data,i)
            if blen==0:break
            block=data[i:i+blen]
            if btype==1: linktype=struct.unpack_from("<H",block,8)[0]
            elif btype==6:
                caplen=struct.unpack_from("<I",block,20)[0]; yield linktype,block[28:28+caplen]
            elif btype==3:
                caplen=struct.unpack_from("<I",block,8)[0]; yield linktype,block[12:12+caplen]
            i+=blen
    else:
        raise SystemExit("unknown capture magic %08x"%magic)

def udp(linktype,pkt):
    off=4 if linktype==0 else (14 if linktype==1 else 4)
    if len(pkt)<off+28 or pkt[off]>>4!=4 or pkt[off+9]!=17: return None
    ihl=(pkt[off]&0xf)*4; t=off+ihl
    sport,dport,ln=struct.unpack_from(">HHH",pkt,t)
    return sport,dport,pkt[t+8:t+ln]

def frames(payload):
    p=0
    while p+8<=len(payload) and payload[p:p+4]==MAGIC:
        ln=struct.unpack_from("<I",payload,p+4)[0]
        yield payload[p+8:p+8+ln]; p+=8+ln

def meter_msgs(path):
    """yield (dport, DataType, CurrentValues, PeakValues, PluginType)."""
    for lt,pkt in read_packets(path):
        r=udp(lt,pkt)
        if not r:continue
        _,dp,pl=r
        if pl[:4]!=MAGIC:continue
        for body in frames(pl):
            if len(body)<HDR+2:continue
            d=parse_pb(body[HDR:])
            f2=d.get((2,0),[None])[0]
            if not isinstance(f2,int) or f2 not in MET:continue
            pt=d.get((1,0),[None])[0]
            yield dp,f2,d.get((3,5),[]),d.get((4,5),[]),pt

def main():
    if len(sys.argv)<2: raise SystemExit(__doc__)
    path=sys.argv[1]
    want=None; show=False
    if "--datatype" in sys.argv: want=int(sys.argv[sys.argv.index("--datatype")+1])
    if "--values" in sys.argv: show=True
    agg=defaultdict(lambda:{"n":0,"cur":set(),"pk":set(),"lo":9e9,"hi":-9e9,"pt":set()})
    shown=0
    for dp,f2,cur,pk,pt in meter_msgs(path):
        a=agg[(dp,f2)];a["n"]+=1;a["cur"].add(len(cur));a["pk"].add(len(pk))
        if pt is not None:a["pt"].add(pt)
        for v in cur+pk:
            if math.isfinite(v) and -200<v<50:a["lo"]=min(a["lo"],v);a["hi"]=max(a["hi"],v)
        if show and want is not None and f2==want and shown<8:
            fin=[round(v,2) for v in cur if math.isfinite(v)]
            print(f"  port{dp} {MET[f2]}: cur[:12]={fin[:12]}");shown+=1
    print(f"\n{'port':>5} {'DataType':>22} {'#msgs':>7} {'curN':>12} {'peakN':>7} {'pluginType':>11} {'value-range':>16}")
    for (dp,f2),a in sorted(agg.items()):
        rng=f"{a['lo']:.2f}..{a['hi']:.2f}" if a['hi']>-9e8 else "(-inf only)"
        print(f"{dp:>5} {MET[f2]+'('+str(f2)+')':>22} {a['n']:>7} {str(sorted(a['cur'])):>12} "
              f"{str(sorted(a['pk'])):>7} {str(sorted(a['pt'])):>11} {rng:>16}")

if __name__=="__main__":
    main()
