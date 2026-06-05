import subprocess, binascii, re, bisect, json
TSHARK="/opt/homebrew/bin/tshark"
out=subprocess.run([TSHARK,"-r","captures/cap86_uf1_eqsweep.pcapng","-Y",'usb.device_address==18 && usb.endpoint_address==0x02 && usb.capdata',
                    "-T","fields","-e","frame.time_epoch","-e","usb.capdata"],capture_output=True,text=True).stdout
curves=[]
for line in out.splitlines():
    p=line.split('\t')
    if len(p)<2: continue
    try: t=float(p[0]); b=binascii.unhexlify(p[1].strip().lower())
    except: continue
    i=0
    while i+4<=len(b):
        if b[i]!=0xFF:i+=1;continue
        ln=b[i+2];end=i+3+ln+1
        if end>len(b):break
        if b[i+1]==0x67 and ln>=2 and (b[i+3]<<8|b[i+4])==0x0122:
            pay=b[i+5:end-1]
            if len(pay)==251 and pay[0]==0x00 and pay[1]==0x01: curves.append((t,tuple(pay[2:251])))
        i=end
states=[]; curfx=None
for line in open("captures/cap86_eqparams.log"):
    if line.startswith("FX:"):
        m=re.search(r'FX: "([^"]+)" nparams=\d+ \| (.*)', line); names={}
        for tok in m.group(2).split(" ; "):
            mm=re.match(r'\[(\d+)\](.*)', tok.strip());
            if mm: names[int(mm.group(1))]=mm.group(2)
        curfx={'name':m.group(1),'names':names}
    elif line.startswith("T:") and curfx:
        m=re.match(r'T:([\d.]+) \| (.*)', line); t=float(m.group(1)); vals={}
        for tok in m.group(2).split(" ; "):
            mm=re.match(r'\[(\d+)\](.*)', tok.strip())
            if mm: vals[curfx['names'].get(int(mm.group(1)))]=mm.group(2)
        states.append((t,curfx['name'],vals))
st=[s[0] for s in states]
def sig(s):
    p=s[2]; ks=[k for k in p if any(w in k for w in('Gain','Frequency','Q','Type','EQ In','Pass'))]
    return tuple((k,p[k]) for k in sorted(ks))
sub=curves[::3]
def score(delta):
    grp={}
    for t,c in sub:
        j=bisect.bisect_right(st,t-delta)-1
        if j<0: continue
        grp=grp if False else grp  # noop
        grp_key=sig(states[j]); grp.setdefault(grp_key,[]).append(c) if False else None
    # rebuild properly
    grp={}
    for t,c in sub:
        j=bisect.bisect_right(st,t-delta)-1
        if j<0: continue
        grp.setdefault(sig(states[j]),[]).append(c)
    tot=0;n=0
    for k,cs in grp.items():
        if len(cs)<2: continue
        for col in range(0,249,16):
            vals=sorted(cc[col] for cc in cs); med=vals[len(vals)//2]
            tot+=sum(abs(cc[col]-med) for cc in cs); n+=len(cs)
    return tot/max(1,n)
lo,hi=1780668700,1780668790
best=None
d=lo
while d<=hi:
    s=score(d)
    if best is None or s<best[0]: best=(s,d)
    d+=0.5
print(f"coarse global min: delta={best[1]:.1f} score={best[0]:.2f}")
b=best[1]
d=b-1
while d<=b+1:
    s=score(d)
    if s<best[0]: best=(s,d)
    d+=0.05
print(f"fine: delta={best[1]:.2f} score={best[0]:.2f}")
