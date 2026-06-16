#!/usr/bin/env python3
"""Faithful offline port of the extension's `drawUc1Face_` (SettingsScreen.cpp) —
the unified UC1 schematic shown in the Settings/FX-Learn window. This is the
TARGET the Learn-HUD should match 1:1 (Frank, 2026-06-16). Renders the full
landscape face (860x660 design space) so we can compare the HUD against it
offline. `focus` dims the non-active domain like the settings dim-overlay.

Output: /tmp/face_full.png (no dim), /tmp/face_cs.png, /tmp/face_bc.png."""

import math
from PIL import Image, ImageDraw, ImageFont

W, H = 860, 660
S = 1.5                      # upscale the design space for a crisp preview
BG = (0x12, 0x14, 0x18)
FONT_FUDGE = 1.4             # ReaImGui renders text larger on-screen than PIL

# Caps / accents (verbatim from drawUc1Face_)
kRed, kGreen, kBlue, kGrey, kBlack = 0xC03038, 0x408840, 0x4070C0, 0x6A707C, 0x101418
kAccentBC, kAccentCC = 0x2A4870, 0x903030

def font(sz):
    sz = max(6, int(sz * S * FONT_FUDGE + 0.5))
    for p in ("/System/Library/Fonts/Supplemental/Arial.ttf",
              "/System/Library/Fonts/Helvetica.ttc"):
        try: return ImageFont.truetype(p, sz)
        except Exception: pass
    return ImageFont.load_default()

PF = None  # set in render

def rgb(c): return ((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF)
def dim(c, f):                       # blend colour toward bg by factor f (1=full)
    r, g, b = rgb(c)
    return (int(r*f + BG[0]*(1-f)), int(g*f + BG[1]*(1-f)), int(b*f + BG[2]*(1-f)))

def render(focus=None):              # focus: 'c' / 'b' / None
    global PF
    img = Image.new("RGB", (int(W*S), int(H*S)), BG)
    d = ImageDraw.Draw(img)
    PF = font(11)
    def X(v): return v * S
    def Y(v): return v * S

    def alpha_for(domain):           # dim the non-focused domain
        if focus is None or domain is None: return 1.0
        return 1.0 if domain == focus else 0.32

    def rect(x, y, w, h, fill, outline, r=3, f=1.0):
        d.rounded_rectangle([X(x), Y(y), X(x+w), Y(y+h)], radius=r*S,
                            fill=dim(fill, f), outline=dim(outline, f), width=max(1, int(S)))
    def line(x1, y1, x2, y2, col, w=1.0, f=1.0):
        d.line([X(x1), Y(y1), X(x2), Y(y2)], fill=dim(col, f), width=max(1, int(w*S*0.8)))
    def text_c(cx, cy, col, s, f=1.0, fnt=None):
        fnt = fnt or PF
        b = d.textbbox((0, 0), s, font=fnt); tw, th = b[2]-b[0], b[3]-b[1]
        d.text((X(cx)-tw/2, Y(cy)-th/2-b[1]), s, fill=dim(col, f), font=fnt)
    def text_l(x, y, col, s, f=1.0, fnt=None):
        fnt = fnt or PF
        d.text((X(x), Y(y)), s, fill=dim(col, f), font=fnt)

    def knob(cx, cy, r, cap, label=None, f=1.0):
        d.ellipse([X(cx-r), Y(cy-r), X(cx+r), Y(cy+r)], fill=dim(0x14181E, f),
                  outline=dim(0x4A5060, f), width=max(1, int(S)))
        rr = r*0.78
        d.ellipse([X(cx-rr), Y(cy-rr), X(cx+rr), Y(cy+rr)], fill=dim(cap, f))
        line(cx, cy-r*0.85, cx, cy-r*0.45, 0xE8E8E8, 1.6, f)
        if label: text_c(cx, cy+r+13, 0xB8BCC4, label, f)
    def btn(x, y, w, h, label, fill=0x252A33, tcol=0xD0D4DA, f=1.0):
        rect(x, y, w, h, fill, 0x4A5060, 3, f)
        text_c(x+w/2, y+h/2, tcol, label, f)
    def section(x, y, s, f=1.0):
        text_l(x, y, 0x9CA0AA, s, f)

    cE = alpha_for('c'); cB = alpha_for('b')

    # ===== Chassis =====
    rect(4, 4, W-8, H-8, 0x14181E, 0x2A3038, 8)

    # ===== LEFT column: Filters + EQ (CS) =====
    kColLx, kColLw = 12, 230
    rect(kColLx, 12, kColLw, H-24, 0x1A1E24, 0x2A3038, 6, cE)
    knob(kColLx+70, 44, 20, kGrey, "LO-PASS", cE)
    knob(kColLx+162, 70, 20, kGrey, "HI-PASS", cE)
    section(kColLx+14, 104, "FILTERS", cE)
    line(kColLx+70, 122, kColLx+kColLw-8, 122, 0x383C44, 1.0, cE)
    text_l(kColLx+14, 146, 0xB8BCC4, "HF", cE)
    knob(kColLx+70, 154, 20, kRed, "GAIN", cE)
    knob(kColLx+162, 182, 20, kRed, "FREQ", cE)
    btn(kColLx+192, 146, 34, 18, "BELL", 0x252A33, 0xC0C4CC, cE)
    text_l(kColLx+14, 224, 0xB8BCC4, "HMF", cE)
    knob(kColLx+70, 232, 20, kGreen, "GAIN", cE)
    knob(kColLx+162, 260, 20, kGreen, "FREQ", cE)
    knob(kColLx+70, 300, 20, kGreen, "Q", cE)
    btn(kColLx+192, 356, 34, 18, "TYPE", 0x252A33, 0xC0C4CC, cE)
    btn(kColLx+192, 380, 34, 18, "IN", 0x252A33, 0xC0C4CC, cE)
    text_l(kColLx+14, 422, 0xB8BCC4, "LMF", cE)
    knob(kColLx+70, 430, 20, kBlue, "GAIN", cE)
    knob(kColLx+162, 458, 20, kBlue, "FREQ", cE)
    knob(kColLx+70, 498, 20, kBlue, "Q", cE)
    text_l(kColLx+14, 550, 0xB8BCC4, "LF", cE)
    knob(kColLx+162, 558, 20, kBlack, "FREQ", cE)
    knob(kColLx+70, 598, 20, kBlack, "GAIN", cE)
    btn(kColLx+192, 600, 34, 18, "BELL", 0x252A33, 0xC0C4CC, cE)

    # ===== CENTRE column: Bus Comp (top) + Central Control (bottom) =====
    kColCx, kColCw = kColLx+kColLw+8, 360
    rect(kColCx, 12, kColCw, 420, 0x1A1E24, kAccentBC, 6, cB)
    text_c(kColCx+kColCw/2, 22, 0x9CA0AA, "BUS COMPRESSOR", cB)
    # GR meter (simplified: bezel + LCD face + 90deg arc + needle + GR badge)
    mw, mh = 196, 80; mx = kColCx+(kColCw-mw)/2; my = 44
    rect(mx, my, mw, mh, 0x141416, 0x282A2E, 4, cB)
    rect(mx+4, my+4, mw-8, mh-8, 0x080C12, 0x444A55, 2, cB)
    mcx, mcy, ra = mx+mw/2, my+mh-3, 70
    a0, a1 = math.radians(-130), math.radians(-50)
    def dBtoA(db): return a0 + (db/20.0)*(a1-a0)
    for db, lab in [(0,"0"),(5,"5"),(10,"10"),(15,"15"),(20,"20")]:
        a = dBtoA(db)
        line(mcx+math.cos(a)*ra, mcy+math.sin(a)*ra,
             mcx+math.cos(a)*(ra-8), mcy+math.sin(a)*(ra-8), 0x4499DD, 1.6, cB)
        text_c(mcx+math.cos(a)*(ra-16), mcy+math.sin(a)*(ra-16), 0x4499DD, lab, cB)
    aN = dBtoA(7)
    line(mcx, mcy, mcx+math.cos(aN)*(ra-4), mcy+math.sin(aN)*(ra-4), 0x4499DD, 2.0, cB)
    text_c(mcx, my+mh-12, 0x4499DD, "GR", cB)
    # BC knob grid 4x2 + IN toggle
    c1x, c2x = kColCx+120, kColCx+240
    ry = [172, 240, 308, 376]
    knob(c1x, ry[0], 20, kAccentBC, "THR", cB)
    knob(c2x, ry[0], 20, kAccentBC, "MAKE-UP", cB)
    knob(c1x, ry[1], 20, kAccentBC, "ATTACK", cB)
    knob(c2x, ry[1], 20, kAccentBC, "RELEASE", cB)
    knob(c1x, ry[2], 20, kAccentBC, "RATIO", cB)
    rect(c2x-14, ry[2]-14, 28, 28, 0xE0E0E0, 0x808088, 3, cB)
    text_c(c2x, ry[2]+26, 0x9CA0AA, "IN", cB)
    knob(c1x, ry[3], 20, kAccentBC, "S/C HPF", cB)
    knob(c2x, ry[3], 20, kAccentBC, "MIX", cB)
    # CS Input / Output gain (inside BC chassis, but CS domain → dim with CS)
    knob(kColCx+60, 376, 20, kGrey, "INPUT", cE)
    knob(kColCx+300, 376, 20, kGrey, "OUTPUT", cE)
    # Central Control Panel
    kCcpY, kCcpH = 440, 208
    rect(kColCx, kCcpY, kColCw, kCcpH, 0x1A1E24, kAccentCC, 6)
    rect(kColCx+14, kCcpY+14, 56, 30, 0x1A0408, 0x401818, 3)
    text_c(kColCx+42, kCcpY+28, 0xFF3030, "001")
    lcdX, lcdW = kColCx+78, kColCw-138; lcdCx = lcdX+lcdW/2
    rect(lcdX, kCcpY+12, lcdW, 76, 0x05080C, 0x444A55, 3)
    text_c(lcdCx, kCcpY+26, 0x808088, "MAIN")
    text_c(lcdCx, kCcpY+46, 0xE0E0E0, "Track Name")
    text_c(lcdCx, kCcpY+66, 0x4488DD, "Stereo Bus")
    bw, bh, gap = 80, 22, 20; total = 2*bw+gap; x0 = kColCx+(kColCw-total)/2; y0 = kCcpY+100
    for i, lab in enumerate(["360", "MAGNIFY"]):
        btn(x0+i*(bw+gap), y0, bw, bh, lab, 0x252A33, 0xC0C4CC)
    midX = kColCx+kColCw/2
    knob(midX-40, kCcpY+145, 18, kGrey, "CS Encoder")
    knob(midX+40, kCcpY+145, 18, kGrey, "BC Encoder")
    text_c(W/2, kCcpY+kCcpH-14, 0x707880, "Rea-Sixty")

    # ===== RIGHT column: Compressor + Gate + Channel (CS) =====
    kColRx, kColRw = kColCx+kColCw+8, 230
    rect(kColRx, 12, kColRw, H-24, 0x1A1E24, 0x2A3038, 6, cE)
    section(kColRx+14, 26, "COMPRESSOR", cE)
    btn(kColRx+kColRw-76, 24, 66, 22, "FAST ATK", 0x252A33, 0xC0C4CC, cE)
    btn(kColRx+kColRw-76, 50, 66, 22, "PEAK", 0x252A33, 0xC0C4CC, cE)
    rc1x, rc2x = kColRx+60, kColRx+156
    knob(rc1x, 96, 20, kGrey, "RATIO", cE)
    knob(rc2x, 124, 20, kGrey, "THRESHOLD", cE)
    knob(rc1x, 158, 20, kGrey, "RELEASE", cE)
    rect(kColRx+14, 221, 22, 22, 0xE0E0E0, 0x808088, 3, cE)
    text_c(kColRx+25, 251, 0x9CA0AA, "IN", cE)
    gx, gy = kColRx+kColRw-26, 208
    for i, st in enumerate(["20", "14", "9", "6", "3"]):
        ly = gy+i*12
        d.ellipse([X(gx)-2, Y(ly)-2, X(gx)+2, Y(ly)+2], fill=dim(0x404448, cE))
        text_l(gx-18, ly-5, 0x808088, st, cE)
    knob(rc1x, 308, 20, kGrey, "RANGE", cE)
    knob(rc2x, 332, 20, kGrey, "THRESHOLD", cE)
    knob(rc1x, 374, 20, kGrey, "RELEASE", cE)
    knob(rc2x, 398, 20, kGrey, "HOLD", cE)
    # GATE / EXPANDER — section label above its two dyn buttons (clear of the
    # HOLD knob label above).
    section(kColRx+14, 446, "GATE / EXPANDER", cE)
    btn(kColRx+14, 466, 96, 22, "EXPAND", 0x252A33, 0xC0C4CC, cE)
    btn(kColRx+118, 466, 96, 22, "FAST ATK", 0x252A33, 0xC0C4CC, cE)
    # CHANNEL — only the bindable controls (Ø / S/C LISTEN / IN); the
    # non-bindable hardware keys (SOLO / CUT / FINE / SOLO CLR) are dropped so
    # the bottom breathes (Frank 2026-06-16).
    section(kColRx+14, 518, "CHANNEL", cE)
    big = 40
    btn(kColRx+14, 540, 96, 26, "Ø", 0x252A33, 0xC0C4CC, cE)
    btn(kColRx+14, 574, 96, 26, "S/C LISTEN", 0x252A33, 0xC0C4CC, cE)
    btn(kColRx+158, 547, big, big, "IN", 0xE0E0E0, 0x303338, cE)

    return img

if __name__ == "__main__":
    render(None).save("/tmp/face_full.png")
    render("c").save("/tmp/face_cs.png")
    render("b").save("/tmp/face_bc.png")
    print("wrote /tmp/face_full.png /tmp/face_cs.png /tmp/face_bc.png")
