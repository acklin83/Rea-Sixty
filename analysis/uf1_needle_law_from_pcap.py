#!/usr/bin/env python3
"""Second-needle (0x0127) plateau/fall episodes and 0x0128 LED transitions from an
Analogue-screen capture: uf1_needle_law_from_pcap.py <vu.pcap> <ppm.pcap>. Written
2026-09-11 for cap130/131."""
import sys, subprocess, collections
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
for pcap,mode in ((sys.argv[1],'VU'),(sys.argv[2],'PPM')):
    rows=frames(pcap)
    # pair by order: each cycle sends 0125 then 0127 then 0128 then 011c
    ndl=[(t,p[0]) for t,z,p in rows if z==0x125]; hold=[(t,p[0]) for t,z,p in rows if z==0x127]
    led=[(t,p[0]) for t,z,p in rows if z==0x128]; ro=[(t,p) for t,z,p in rows if z==0x11c]
    n=min(len(ndl),len(hold),len(led),len(ro))
    print("\n==================", mode, pcap, "frames", n)
    # readout L as float
    def rl(p):
        s=p.split(b'\x00')[0].decode('latin1','replace').strip()
        try: return float(s)
        except: return float('-inf')
    # hold episodes: hold > ndl
    print("-- second-needle episodes: t_start, hold byte, main byte at start, plateau s, then fall samples (t rel, hold byte) until hold<=main")
    k=0; shown=0
    while k<n and shown<8:
        t,h=hold[k]; m=ndl[k][1]
        if h>m+2:
            t0=t; h0=h; plat=0.0; kk=k
            while kk<n and hold[kk][1]>=h0-1: plat=hold[kk][0]-t0; kk+=1
            fall=[]
            while kk<n and hold[kk][1]>ndl[kk][1]+2: fall.append((round(hold[kk][0]-t0,2),hold[kk][1],ndl[kk][1])); kk+=1
            print("  t=%6.2f hold=%3d main=%3d  plateau %.2f s  fall(%d samples): %s"%(t0,h0,m,plat,len(fall),fall[:14]))
            shown+=1; k=kk
        else: k+=1
    # LED transitions
    print("-- LED mask transitions (t, mask, readout L)")
    prev=None
    for i in range(n):
        m=led[i][1]
        if m!=prev: print("  t=%6.2f mask=0x%02x readout=%s"%(led[i][0],m,rl(ro[i][1]))); prev=m
