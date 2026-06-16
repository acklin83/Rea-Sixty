#!/usr/bin/env python3
"""Offline preview of the Learn-HUD ImGui mockup (renderMockup) so the SSL look
can be iterated without running REAPER. Mirrors the Lua drawing logic + the same
control geometry (kUc1Controls / mock). Output: /tmp/hud_cs.png + /tmp/hud_bc.png.

NOT shipped — a design tool for the build→compare→tune loop. Keep the draw
constants in sync with rea_sixty_assignment_hud_imgui.lua's renderMockup."""

import math
from PIL import Image, ImageDraw, ImageFont

# --- control table: idx, shape(0=knob 1=toggle 2=dynbtn), dom, name, section,
#     cx, cy, r, w, h, cap(RGBA), legend  (verbatim from the mock / kUc1Controls)
R, G, B, K, GY, BC = 0xC03038FF, 0x408840FF, 0x4070C0FF, 0x101418FF, 0x6A707CFF, 0x2A4870FF
NC = 0
CTRLS = [
    (0,0,"c","LPF","Filter",82,44,20,0,0,GY,"LPF"),
    (1,0,"c","HPF","Filter",174,70,20,0,0,GY,"HPF"),
    (2,1,"c","HF Type","EQ",204,146,0,34,18,NC,"BELL"),
    (3,0,"c","HF Gain","EQ",82,154,20,0,0,R,"GAIN"),
    (4,0,"c","HF Freq","EQ",174,182,20,0,0,R,"FREQ"),
    (5,0,"c","HMF Gain","EQ",82,232,20,0,0,G,"GAIN"),
    (6,0,"c","HMF Freq","EQ",174,260,20,0,0,G,"FREQ"),
    (7,0,"c","HMF Q","EQ",82,300,20,0,0,G,"Q"),
    (8,1,"c","EQ Type","EQ",204,356,0,34,18,NC,"TYPE"),
    (9,1,"c","EQ In","EQ",204,380,0,34,18,NC,"EQ"),
    (10,0,"c","LMF Gain","EQ",82,430,20,0,0,B,"GAIN"),
    (11,0,"c","LMF Freq","EQ",174,458,20,0,0,B,"FREQ"),
    (12,0,"c","LMF Q","EQ",82,498,20,0,0,B,"Q"),
    (13,0,"c","LF Freq","EQ",174,558,20,0,0,K,"FREQ"),
    (14,0,"c","LF Gain","EQ",82,598,20,0,0,K,"GAIN"),
    (15,1,"c","LF Type","EQ",204,600,0,34,18,NC,"BELL"),
    (16,0,"c","Input Trim","I/O & Channel",310,376,20,0,0,GY,"IN"),
    (17,0,"c","Fader Level","I/O & Channel",550,376,20,0,0,GY,"FDR"),
    (18,0,"b","Threshold","Bus Comp",370,172,20,0,0,BC,"THR"),
    (19,0,"b","Makeup","Bus Comp",490,172,20,0,0,BC,"MAKE"),
    (20,0,"b","Attack","Bus Comp",370,240,20,0,0,BC,"ATK"),
    (21,0,"b","Release","Bus Comp",490,240,20,0,0,BC,"REL"),
    (22,0,"b","Ratio","Bus Comp",370,308,20,0,0,BC,"RATIO"),
    (23,1,"b","Bypass","Bus Comp",476,294,0,28,28,NC,"IN"),
    (24,0,"b","S/C HPF","Bus Comp",370,376,20,0,0,BC,"HPF"),
    (25,0,"b","Mix","Bus Comp",490,376,20,0,0,BC,"MIX"),
    (26,2,"c","Comp F.Atk","Dynamics",772,24,0,66,22,NC,"F.ATK"),
    (27,2,"c","Comp Peak","Dynamics",772,50,0,66,22,NC,"PEAK"),
    (28,0,"c","Comp Ratio","Dynamics",678,96,20,0,0,GY,"RATIO"),
    (29,0,"c","Comp Thr","Dynamics",774,124,20,0,0,GY,"THR"),
    (30,0,"c","Comp Rel","Dynamics",678,158,20,0,0,GY,"REL"),
    (31,1,"c","Dynamics In","Dynamics",632,221,0,22,22,NC,"DYN"),
    (32,0,"c","Gate Range","Gate",678,308,20,0,0,GY,"RNG"),
    (33,0,"c","Gate Thr","Gate",774,332,20,0,0,GY,"THR"),
    (34,0,"c","Gate Rel","Gate",678,374,20,0,0,GY,"REL"),
    (35,0,"c","Gate Hold","Gate",774,398,20,0,0,GY,"HOLD"),
    (36,2,"c","Gate/Exp","Gate",632,430,0,66,22,NC,"G/E"),
    (37,2,"c","Gate F.Atk","Gate",632,456,0,66,22,NC,"F.ATK"),
    (38,2,"c","Polarity","I/O & Channel",632,510,0,66,22,NC,"POL"),
    (39,2,"c","S/C Listen","I/O & Channel",632,536,0,66,22,NC,"LST"),
    (40,1,"c","Bypass","I/O & Channel",793,523,0,40,40,NC,"IN"),
]
ASSIGN = {0:("LPF",0),3:("HF Gain",0),4:("HF Freq",0),5:("HMF Gain",0),
          28:("Comp Ratio",0),29:("Threshold",0),31:("Dynamics In",0),32:("Gate Range",1),
          18:("Threshold",0),19:("Make-Up Gain",0),22:("Ratio",0),23:("Comp In / Out",0)}

CS_RGB, BC_RGB = 0xFFFF00, 0xFF0000
BG = (0x1F,0x1F,0x21)

def rgb(c): return ((c>>16)&0xFF,(c>>8)&0xFF,c&0xFF)
def blend(c, a, bg=BG):
    r,g,b = rgb(c); return (int(r*a+bg[0]*(1-a)), int(g*a+bg[1]*(1-a)), int(b*a+bg[2]*(1-a)))

# ReaImGui renders text larger on-screen than the nominal px (retina/DPI), so the
# PIL preview must scale fonts up to be faithful — else layouts that fit here
# collide in the real HUD. Empirically ~1.4x. Tune against real screenshots.
FONT_FUDGE = 1.4
def font(sz):
    sz = max(6, int(sz*FONT_FUDGE+0.5))
    for p in ("/System/Library/Fonts/Supplemental/Arial.ttf","/Library/Fonts/Arial.ttf",
              "/System/Library/Fonts/Helvetica.ttc"):
        try: return ImageFont.truetype(p, sz)
        except Exception: pass
    return ImageFont.load_default()

def tw(d, s, f):
    b = d.textbbox((0,0), s, font=f); return b[2]-b[0], b[3]-b[1]

def render(dom, W=900, H=700, fontScale=1.0):
    img = Image.new("RGB", (W,H), BG); d = ImageDraw.Draw(img)
    domrgb = CS_RGB if dom=="c" else BC_RGB
    items = [c for c in CTRLS if c[2]==dom]
    # CS: close the dead horizontal band where the BC/meters sit on the hardware,
    # so the strip fills a portrait window. Left cluster stays; the gap (incl.
    # IN/FDR) is compressed; the right cluster shifts left by the saved amount.
    def cxmap(cx):
        if dom!="c": return cx
        L,Rs,NG = 250.0, 600.0, 150.0
        if cx<=L: return cx
        if cx>=Rs: return cx-(Rs-L-NG)
        return L+(cx-L)/(Rs-L)*NG
    def ecx(c):
        x=cxmap(c[5])
        if c[3]=="Input Trim": x+=28      # IN/FDR nudged toward the centre
        elif c[3]=="Fader Level": x-=28
        return x
    def ecy(c):                            # IN/FDR pushed down, clear of EQ toggles
        if c[3] in ("Input Trim","Fader Level"): return c[6]+70
        return c[6]
    items=[(c[0],c[1],c[2],c[3],c[4],ecx(c),ecy(c),c[7],c[8],c[9],c[10],c[11]) for c in items]
    TAB_H = int(22*fontScale)+16
    d.rectangle([0,0,W,TAB_H], fill=blend(0x171719,1))
    d.text((12,8), "CS" if dom=="c" else "BC", fill=blend(domrgb,1), font=font(int(17*fontScale)))
    M=10; GUT=(70 if dom=="c" else 8); top=TAB_H+10
    availW=W-2*M-GUT; availH=H-top-8
    minx=min((c[5]-c[7] if c[1]==0 else c[5]) for c in items)
    maxx=max((c[5]+c[7] if c[1]==0 else c[5]+c[8]) for c in items)
    miny=min((c[6]-c[7] if c[1]==0 else c[6]) for c in items)
    maxy=max((c[6]+c[7] if c[1]==0 else c[6]+c[9]) for c in items)
    domW,domH=maxx-minx,maxy-miny
    # per-domain scale (fill the window with THIS domain's bbox) — BC fills big,
    # CS uses its own width. Capped so BC knobs don't get absurd.
    scale=min(min(availW/domW, availH/domH), 2.6)
    ox=M+GUT+(availW-domW*scale)/2-minx*scale
    oy=top+(availH-domH*scale)/2-miny*scale
    pf=font(int(9*fontScale+.5)); bpf=font(int(11*fontScale+.5)); spf=font(int(11*fontScale+.5))
    present=True
    def sxf(cx): return ox+cx*scale
    def syf(cy): return oy+cy*scale


    # band connectors (through EQ band knob centres, behind controls)
    def bandOf(nm):
        p=nm.split(" ")[0]
        return p if p in ("HF","HMF","LMF","LF") else None
    bands={}
    for c in items:
        if c[1]==0:
            b=bandOf(c[3])
            if b: bands.setdefault(b,[]).append((sxf(c[5]), syf(c[6]), max(3,c[7]*scale)))
    for bn,lst in bands.items():
        lst.sort(key=lambda g:(round(g[1]), g[0]))
        for i in range(len(lst)-1):
            d.line([lst[i][0],lst[i][1],lst[i+1][0],lst[i+1][1]], fill=blend(0x6A6E78,0.5), width=max(1,int(lst[i][2]*0.10)))

    # LEFT gutter labels (right-aligned): FILTERS / HF / HMF / EQ / LMF / LF, each
    # at its band/section vertical centre — distinct y's, no overlap (CS only).
    xR=M+GUT-8
    if dom=="c":
        rows=[]
        fy=[c[6] for c in items if c[3] in ("LPF","HPF")]
        if fy: rows.append(("FILTERS", sum(fy)/len(fy)))
        for bn in ("HF","HMF","LMF","LF"):
            ys=[c[6] for c in items if c[3].startswith(bn+" ")]
            if ys: rows.append((bn, sum(ys)/len(ys)))
        eqy=[c[6] for c in items if c[3]=="EQ In"]
        if eqy: rows.append(("EQ", eqy[0]))
        for txt,cyf in rows:
            w,h=tw(d,txt,bpf)
            d.text((xR-w, syf(cyf)-h/2), txt, fill=blend(0xB8BCC4,0.9), font=bpf)

    # RIGHT-cluster section headers + rule. side="above" (room above) or "left"
    # (place to the left of the cluster when the space above is taken).
    def header(cset, label, side="above"):
        if not cset: return
        x0=min((sxf(c[5])-c[7]*scale) if c[1]==0 else sxf(c[5]) for c in cset)
        x1=max((sxf(c[5])+c[7]*scale) if c[1]==0 else sxf(c[5])+c[8]*scale for c in cset)
        w,h=tw(d,label,spf)
        if side=="left":
            yc=sum(syf(c[6]) for c in cset)/len(cset)
            hx=max(2,x0-w-10); hy=yc-h/2
            d.text((hx,hy), label, fill=blend(0xCED2DA,0.95), font=spf)
            return
        y0=min((syf(c[6])-c[7]*1.3*scale) if c[1]==0 else syf(c[6]) for c in cset)
        hy=max(TAB_H+2, y0-h-6); hx=min(max(2,x0), W-2-w)
        d.text((hx,hy), label, fill=blend(0xCED2DA,0.95), font=spf)
        ry=hy+h/2; rx0=hx+w+6; rx1=min(W-2,x1)
        if rx1>rx0: d.line([rx0,ry,rx1,ry], fill=blend(0x4A4E58,1), width=1)
    if dom=="c":
        header([c for c in items if c[4]=="Dynamics"], "COMPRESSOR")
        header([c for c in items if c[4]=="Gate"], "GATE/EXP")
        header([c for c in items if c[4]=="I/O & Channel" and c[1]!=0], "CHANNEL", "left")
    else:
        header(items, "BUS COMP")

    def glyph(kind,gx,gy,sz,col):
        L=lambda x1,y1,x2,y2: d.line([x1,y1,x2,y2], fill=col, width=1)
        if kind=="lpf": L(gx,gy,gx+sz*.55,gy); L(gx+sz*.55,gy,gx+sz,gy+sz*.6)
        elif kind=="hpf": L(gx,gy+sz*.6,gx+sz*.45,gy); L(gx+sz*.45,gy,gx+sz,gy)
        else:
            L(gx,gy+sz*.5,gx+sz*.35,gy); L(gx+sz*.35,gy,gx+sz*.65,gy); L(gx+sz*.65,gy,gx+sz,gy+sz*.5)

    # controls
    for c in items:
        idx,shape,_,name,sec,cx,cy,r,w,h,cap,legend=c
        a=ASSIGN.get(idx); mapped=a is not None
        capc=(cap>>8)&0xFFFFFF if cap>0 else domrgb
        ringA=1.0 if mapped else 0.55
        ringcol=capc
        fillc=blend(capc,0.20) if mapped else blend(0x202227,1)
        sx,sy=ox+cx*scale,oy+cy*scale
        if shape==0:
            rr=max(3,r*scale)
            d.ellipse([sx-rr,sy-rr,sx+rr,sy+rr], fill=fillc, outline=blend(ringcol,ringA), width=max(1,int(rr*0.12)))
            ringR=rr*1.28; dotR=max(0.7,rr*0.10)
            for di in range(11):
                ang=2.356+(7.069-2.356)*(di/10.0)
                dx=sx+math.cos(ang)*ringR; dy=sy+math.sin(ang)*ringR
                d.ellipse([dx-dotR,dy-dotR,dx+dotR,dy+dotR], fill=blend(capc,0.55))
            d.line([sx,sy-rr*0.82,sx,sy-rr*0.34], fill=blend(0xE8E8E8,0.9), width=max(1,int(rr*0.12)))
            gk="lpf" if name=="LPF" else "hpf" if name=="HPF" else None
            if gk: glyph(gk, sx+rr+3, sy-rr*0.3, rr*0.7, blend(0x9DA2AC,0.8))
            celltop=sy+ringR+dotR+2; cellcx=sx; cellw=max(60,rr*4)
        else:
            sw,sh=w*scale,h*scale
            d.rectangle([sx,sy,sx+sw,sy+sh], fill=fillc, outline=blend(ringcol,ringA), width=1)
        # fixed label = legend. Knobs: centred below. Buttons/toggles: centred
        # INSIDE the box (settings-schematic style).
        if mapped: tcol=blend(capc,1.0)
        elif present: tcol=blend(0xC6CDD6,0.85)
        else: tcol=blend(0x707880,0.5)
        lw,lh=tw(d,legend,pf)
        if shape==0:
            tx=max(2,min(sx-lw/2, W-2-lw)); ty=sy+rr*1.28+max(0.7,rr*0.10)+2
        else:
            tx=sx+sw/2-lw/2; ty=sy+sh/2-lh/2
        d.text((tx,ty), legend, fill=tcol, font=pf)
    return img

render("c").save("/tmp/hud_cs.png")
render("b").save("/tmp/hud_bc.png")
print("wrote /tmp/hud_cs.png /tmp/hud_bc.png")
