import subprocess, binascii, re, json
TSHARK="/opt/homebrew/bin/tshark"; DELTA=1780668711.4; DWELL=0.4
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
# curve as step function of epoch: list of (t_start, curve) where curve changes
csteps=[]; last=None
for t,c in curves:
    if c!=last: csteps.append([t,c]); last=c
ct=[s[0] for s in csteps]
import bisect
def curve_at(epoch):
    j=bisect.bisect_right(ct,epoch)-1
    return csteps[j][1] if j>=0 else None
# state dwells -> sample the curve in the MIDDLE of the (shifted) dwell window
pairs=[]
for i in range(len(states)):
    ts=states[i]['t']; tn=states[i+1]['t'] if i+1<len(states) else ts+0.5
    if tn-ts<DWELL: continue
    mid=(ts+tn)/2 + DELTA
    c=curve_at(mid)
    if c: pairs.append({'fx':states[i]['fx'],'p':states[i]['p'],'curve':list(c)})
json.dump(pairs, open("/tmp/uf1_eq_pairs.json","w"))
from collections import Counter
print(f"clean dwell-pairs: {len(pairs)}  per fx:", dict(Counter(p['fx'] for p in pairs)))
# quick consistency: group by full band sig, check curve variance
from collections import defaultdict
import statistics
g=defaultdict(list)
for p in pairs:
    key=tuple((k,p['p'].get(k)) for k in ['LF Gain','LMF Gain','HMF Gain','HF Gain','LF Frequency','LMF Frequency','HMF Frequency','HF Frequency'])
    g[key].append(p['curve'])
var=[]
for k,cs in g.items():
    if len(cs)<2: continue
    for col in range(0,249,20):
        vals=[c[col] for c in cs]; var.append(max(vals)-min(vals))
print(f"within-identical-state curve spread: mean={statistics.mean(var):.1f} max={max(var)} (low=good pairing)")
