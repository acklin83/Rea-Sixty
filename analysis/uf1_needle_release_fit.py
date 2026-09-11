#!/usr/bin/env python3
"""Linear fits of the second needle release (VU dB/s, PPM marks/s) and the main
needle fall: uf1_needle_release_fit.py <vu.pcap> <ppm.pcap>. 2026-09-11, cap130/131."""
import sys, subprocess, numpy as np
TS="/opt/homebrew/bin/tshark"
VU=[4,7,9,11,13,15,17,19,21,23,25,32,39,45,55,65,77,89,104,119,134,149,164,179]  # index = VU+20
def byte2vu(b):
    b=min(max(b,4),179)
    for i in range(23):
        if VU[i]<=b<=VU[i+1]: return -20+i+(b-VU[i])/(VU[i+1]-VU[i])
    return 3.0
def byte2mark(b): return 1+(b-9)/27.3
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
for pcap,mode,conv,unit in ((sys.argv[1],'VU',byte2vu,'dB'),(sys.argv[2],'PPM',byte2mark,'marks')):
    rows=frames(pcap); ndl=[(t,p[0]) for t,z,p in rows if z==0x125]; hold=[(t,p[0]) for t,z,p in rows if z==0x127]
    n=min(len(ndl),len(hold)); print("\n==", mode)
    k=0; rates=[]; plats=[]
    while k<n:
        if hold[k][1]>ndl[k][1]+2:
            t0=hold[k][0]; h0=hold[k][1]; kk=k
            while kk<n and hold[kk][1]>=h0-1: kk+=1
            plat=hold[kk-1][0]-t0
            seg=[]
            while kk<n and hold[kk][1]>ndl[kk][1]+2 and hold[kk][1]>=8: seg.append((hold[kk][0],conv(hold[kk][1]))); kk+=1
            if len(seg)>=3:
                ts=np.array([s[0] for s in seg]); vs=np.array([s[1] for s in seg]); slope=np.polyfit(ts,vs,1)[0]
                rates.append(slope); plats.append(plat)
                print("  t=%6.2f plateau %.2f s  release %6.1f %s/s over %.2f s (%d samples, %.1f -> %.1f)"%(t0,plat,slope,unit,ts[-1]-ts[0],len(seg),vs[0],vs[-1]))
            k=kk
        else: k+=1
    if rates: print("  median release %.1f %s/s   median plateau %.2f s"%(np.median(rates),unit,np.median(plats)))
    # main needle fall rate for comparison (VU: after tone stops)
    segs=[]; cur=[]
    for i in range(1,n):
        if ndl[i][1]<ndl[i-1][1] and ndl[i][1]>4: cur.append((ndl[i][0],conv(ndl[i][1])))
        else:
            if len(cur)>=5: segs.append(cur)
            cur=[]
    rs=[np.polyfit([s[0] for s in c],[s[1] for s in c],1)[0] for c in segs]
    if rs: print("  MAIN needle falls: median %.1f %s/s over %d segments"%(np.median(rs),unit,len(rs)))
