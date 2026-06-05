import subprocess, binascii, re, json, math
TSHARK="/opt/homebrew/bin/tshark"; DWELL=0.4
# ---- load curves (epoch) ----
out=subprocess.run([TSHARK,"-r","captures/cap86_uf1_eqsweep.pcapng","-Y",'usb.device_address==18 && usb.endpoint_address==0x02 && usb.capdata',
                    "-T","fields","-e","frame.time_epoch","-e","usb.capdata"],capture_output=True,text=True).stdout
curves=[]
for line in out.splitlines():
    parts=line.split('\t')
    if len(parts)<2: continue
    try: t=float(parts[0]); b=binascii.unhexlify(parts[1].strip().lower())
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
# curve dwells (ordered): consecutive identical curve spanning >=DWELL
cd=[]; i=0
while i<len(curves):
    j=i
    while j+1<len(curves) and curves[j+1][1]==curves[i][1]: j+=1
    span=(curves[j+1][0]-curves[i][0]) if j+1<len(curves) else (curves[j][0]-curves[i][0])
    if span>=DWELL: cd.append(list(curves[i][1]))
    i=j+1
# ---- load state dwells (ordered) ----
states=[]; curfx=None
for line in open("captures/cap86_eqparams.log"):
    if line.startswith("FX:"):
        m=re.search(r'FX: "([^"]+)" nparams=\d+ \| (.*)', line); names={}
        for tok in m.group(2).split(" ; "):
            mm=re.match(r'\[(\d+)\](.*)', tok.strip())
            if mm: names[int(mm.group(1))]=mm.group(2)
        curfx={'name':m.group(1),'names':names}
    elif line.startswith("T:") and curfx:
        m=re.match(r'T:([\d.]+) \| (.*)', line); t=float(m.group(1)); vals={}
        for tok in m.group(2).split(" ; "):
            mm=re.match(r'\[(\d+)\](.*)', tok.strip())
            if mm: vals[curfx['names'].get(int(mm.group(1)))]=mm.group(2)
        states.append({'t':t,'fx':curfx['name'],'p':vals})
sd=[]
for i in range(len(states)):
    tn=states[i+1]['t'] if i+1<len(states) else states[i]['t']+1
    if tn-states[i]['t']>=DWELL: sd.append(states[i])
print(f"state-dwells {len(sd)}  curve-dwells {len(cd)}")
# ---- approximate model for alignment scoring ----
def fv(s):
    try: return float(str(s).replace('+','').replace('kHz','').replace('dB','').strip())
    except: return None
def fhz(s):
    v=fv(s);
    if v is None: return None
    return v*1000 if ('k' in str(s).lower() or 0<v<30) else v
def colf(c): return 20.0*1000.0**(c/248.0)
def peakdb(f,f0,g,q):
    if not g or not f0 or q<=0: return 0.0
    A=10**(g/40);w2=f*f;w0=f0*f0;d=w0-w2
    return 20*math.log10(math.hypot(d,f*f0*A/q)/math.hypot(d,f*f0/(A*q)))
def predict(p):
    out=[0.0]*249
    def gg(n): return fv(p.get(n)) or 0.0
    bands=[('LF',gg('LF Gain'),fhz(p.get('LF Frequency')),1.0,p.get('LF Type','Shelf')),
           ('LMF',gg('LMF Gain'),fhz(p.get('LMF Frequency')),fv(p.get('LMF Q')) or 1.5,'Bell'),
           ('HMF',gg('HMF Gain'),fhz(p.get('HMF Frequency')),fv(p.get('HMF Q')) or 1.5,'Bell'),
           ('HF',gg('HF Gain'),fhz(p.get('HF Frequency')),1.0,p.get('HF Type','Shelf'))]
    for col in range(249):
        f=colf(col); db=0
        for nm,g,f0,q,ty in bands:
            if not f0: continue
            if 'ell' in ty: db+=peakdb(f,f0,g,q)
            else:
                db+= (g/(1+(f0/f)**2) if nm=='HF' else g/(1+(f/f0)**2))
        out[col]=100+db*5.0
    return out
pred=[predict(s['p']) for s in sd]
def dist(a,b): return sum(abs(a[c]-b[c]) for c in range(0,249,6))/42.0
# ---- Needleman-Wunsch align pred (sd) vs cd ----
n,m=len(pred),len(cd); GAP=15.0
import numpy as np
D=np.full((n+1,m+1),1e9); D[0,0]=0
for i in range(1,n+1): D[i,0]=i*GAP
for j in range(1,m+1): D[0,j]=j*GAP
bt=np.zeros((n+1,m+1),dtype=int)
for i in range(1,n+1):
    pi=pred[i-1]
    for j in range(1,m+1):
        md=D[i-1,j-1]+dist(pi,cd[j-1])
        dd=D[i-1,j]+GAP; ii=D[i,j-1]+GAP
        best=min(md,dd,ii); D[i,j]=best
        bt[i,j]=0 if best==md else (1 if best==dd else 2)
# backtrack
i,j=n,m; pairs=[]
while i>0 and j>0:
    if bt[i,j]==0: pairs.append((sd[i-1],cd[j-1])); i-=1;j-=1
    elif bt[i,j]==1: i-=1
    else: j-=1
pairs.reverse()
print(f"aligned matched pairs: {len(pairs)}  (NW cost {D[n,m]:.0f})")
json.dump([{'fx':s['fx'],'p':s['p'],'curve':c} for s,c in pairs], open("/tmp/uf1_eq_pairs.json","w"))
# consistency check
from collections import defaultdict; import statistics
g=defaultdict(list)
for s,c in pairs:
    key=tuple((k,s['p'].get(k)) for k in ['LF Gain','LMF Gain','HMF Gain','HF Gain'])
    g[key].append(c)
sp=[max(c[col] for c in cs)-min(c[col] for c in cs) for cs in g.values() if len(cs)>1 for col in range(0,249,20)]
print(f"within-state spread after alignment: mean={statistics.mean(sp):.1f} max={max(sp)}" if sp else "n/a")
