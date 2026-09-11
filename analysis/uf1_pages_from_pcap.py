#!/usr/bin/env python3
"""Per-page V-Pot labels and soft keys as SSL 360 sends them to the UF1 (0x010e /
0x0104 bursts), chronologically: uf1_pages_from_pcap.py <pcap> [<pcap>…]. Written
2026-09-11 for cap133-136 (4K E/G/B, 32C on 2.1.12)."""
import sys, subprocess, re
TS="/opt/homebrew/bin/tshark"
def frames(pcap, dev=18):
    out=subprocess.run([TS,"-r",pcap,"-Y",f"usb.device_address=={dev} && usb.endpoint_address==0x02 && usb.data_len>0","-T","fields","-e","frame.time_relative","-e","usb.capdata"],capture_output=True,text=True).stdout
    rows=[]
    for line in out.splitlines():
        t,hx=line.split('\t'); b=bytes.fromhex(hx.strip()); i=0; t=float(t)
        while i+3<=len(b):
            if b[i]!=0xff: i+=1; continue
            op=b[i+1]; ln=b[i+2]; end=i+3+ln+1
            if op==0x67 and ln>=2: rows.append((t,(b[i+3]<<8)|b[i+4],bytes(b[i+5:end-1])))
            i=end
    return rows
def txt(p): return p.decode('latin1','replace').replace('\x00','').strip()
for pcap in sys.argv[1:]:
    rows=[r for r in frames(pcap) if r[1] in (0x104,0x10e,0x17,0x102)]
    print("\n========", pcap)
    # group into bursts: gap > 0.08 s starts a new burst
    bursts=[]; cur=[]
    for r in rows:
        if cur and r[0]-cur[-1][0]>0.08: bursts.append(cur); cur=[]
        cur.append(r)
    if cur: bursts.append(cur)
    lastpage=None
    for b in bursts:
        sk={}; vp={}; other=[]
        for t,z,p in b:
            if z==0x104 and p: sk[p[0]]=txt(p[1:])
            elif z==0x10e and p: vp[p[0]]=re.split(r'\s{2,}',txt(p[1:]))[0] if txt(p[1:]) else ''
            elif z in (0x17,0x102): other.append('%04x=%s'%(z,txt(p) or p.hex()))
        if not sk and len(vp)<2: continue   # value-only updates
        page=(tuple(sorted(vp.items())),tuple(sorted(sk.items())))
        if page==lastpage: continue
        lastpage=page
        print("  t=%6.2f  V-Pots %-52s  SoftKeys %s  %s"%(b[0][0], [vp.get(i,'·') for i in range(4)], [sk.get(i,'·') for i in range(4)], ' '.join(other)))
