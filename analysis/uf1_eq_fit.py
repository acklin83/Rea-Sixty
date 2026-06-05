import json, math, statistics
from collections import defaultdict
pairs=json.load(open("/tmp/uf1_eq_pairs.json"))
def fv(s):
    try: return float(str(s).replace('+','').replace('kHz','').replace('dB','').strip())
    except: return None
def freq_hz(s):
    if s is None: return None
    v=fv(s)
    if v is None: return None
    if 'k' in str(s).lower(): return v*1000
    return v*1000 if 0<v<30 else v   # bare kHz heuristic
def colf(c): return 20.0*1000.0**(c/248.0)
# --- model pieces ---
def peakdb(f,f0,g,q):
    if g==0 or f0<=0 or q<=0: return 0.0
    A=10**(g/40); w2=f*f;w0=f0*f0;d=w0-w2
    return 20*math.log10(math.hypot(d,f*f0*A/q)/math.hypot(d,f*f0/(A*q)))
def lowsh(f,f0,g,sl): return 0.0 if(g==0 or f0<=0) else g/(1+(f/f0)**sl)
def highsh(f,f0,g,sl): return 0.0 if(g==0 or f0<=0) else g/(1+(f0/f)**sl)

VAR='4K B'
P=[p for p in pairs if VAR in p['fx']]
# group by band-param signature -> median curve
groups=defaultdict(list)
def bandsig(p):
    g=p['p']
    keys=['LF Frequency','LF Gain','LF Type','LMF Frequency','LMF Gain','LMF Q',
          'HMF Frequency','HMF Gain','HMF Q','HF Frequency','HF Gain','HF Type','EQ In']
    return tuple((k,g.get(k)) for k in keys)
for p in P: groups[bandsig(p)].append(p['curve'])
train=[]
for sig,cs in groups.items():

    med=[statistics.median(c[col] for c in cs) for col in range(249)] if len(cs)>=1 else None
    train.append((dict(sig),med))
print(f"{VAR}: {len(P)} pairs -> {len(train)} dwell-states (>=3 curves)")
# model: predict curve from state given constants (scale k, bellQ a,b, shelf slope)
def predict(s, k, qa, qb, sl):
    out=[0]*249
    lfg=fv(s.get('LF Gain')) or 0; lff=freq_hz(s.get('LF Frequency')); lftype=s.get('LF Type','Shelf')
    lmg=fv(s.get('LMF Gain')) or 0; lmf=freq_hz(s.get('LMF Frequency')); lmq=fv(s.get('LMF Q')) or 1.5
    hmg=fv(s.get('HMF Gain')) or 0; hmf=freq_hz(s.get('HMF Frequency')); hmq=fv(s.get('HMF Q')) or 1.5
    hfg=fv(s.get('HF Gain')) or 0; hff=freq_hz(s.get('HF Frequency')); hftype=s.get('HF Type','Shelf')
    for col in range(249):
        f=colf(col); db=0
        db+=peakdb(f,lmf,lmg,qa*lmq+qb)
        db+=peakdb(f,hmf,hmg,qa*hmq+qb)
        db+= (peakdb(f,hff,hfg,qa*1.5+qb) if 'ell' in hftype else highsh(f,hff,hfg,sl))
        db+= (peakdb(f,lff,lfg,qa*1.5+qb) if 'ell' in lftype else lowsh(f,lff,lfg,sl))
        h=100+db*k
        out[col]=max(0,min(200,round(h)))
    return out
def err(k,qa,qb,sl):
    tot=0;n=0;mx=0
    for s,med in train:
        pr=predict(s,k,qa,qb,sl)
        for col in range(249):
            e=abs(pr[col]-med[col]); tot+=e; n+=1; mx=max(mx,e)
    return tot/n, mx
# coordinate-descent over k, qa, qb, sl
k,qa,qb,sl=5.44,1.0,0.0,2.0
best=err(k,qa,qb,sl)
print(f"start k={k} qa={qa} qb={qb} sl={sl} -> mean={best[0]:.2f} max={best[1]}")
import itertools
for it in range(6):
    for (name) in ['k','qa','qb','sl']:
        cur={'k':k,'qa':qa,'qb':qb,'sl':sl}
        step={'k':0.1,'qa':0.05,'qb':0.05,'sl':0.1}[name]
        for d in [-step,step,-step*2,step*2]:
            t=dict(cur); t[name]+=d
            if t['k']<=0 or t['sl']<=0: continue
            e=err(t['k'],t['qa'],t['qb'],t['sl'])
            if e[0]<best[0]:
                best=e; k,qa,qb,sl=t['k'],t['qa'],t['qb'],t['sl']
print(f"fit k={k:.2f} qa={qa:.2f} qb={qb:.2f} sl={sl:.2f} -> mean={best[0]:.2f} max={best[1]}")
