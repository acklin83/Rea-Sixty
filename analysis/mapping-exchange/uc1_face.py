#!/usr/bin/env python3
"""
UC1 face -> SVG. A faithful transcription of drawUc1Face_
(extension/src/SettingsScreen.cpp:2271-2652), not a re-imagining.

This is what the plan calls Phase 2a, prototyped in Python: every rect_, circle_,
line_, drawText_ and drawTextCentered_ call from the C++ in the same order, in the
same 860x660 space, with the same colours. The shipping version gives VCanvas a
virtual sink and emits this from C++ so it can never drift; doing it here first
proves the output is worth building the sink for.

Earlier prototypes drew from kUc1Controls[] instead. That is the HIT-TEST table —
41 bare circles and rectangles — not the drawing code. It has no chassis, no
column panels, no band dividers, no GR meter, no LCD. It looked like a wiring
diagram because that is what it is.

Labels for mapped parameters are written ON the control, under its silkscreen
name. No leader lines.
"""
import html

W, H = 860, 660

# Colour constants, transcribed from the C++ (0xRRGGBBAA).
RED_CAP, GREEN_CAP, BLUE_CAP = 0xC03038FF, 0x408840FF, 0x4070C0FF
GREY_CAP, BLACK_CAP = 0x6A707CFF, 0x101418FF
ACCENT_BC, ACCENT_CC = 0x2A4870FF, 0x903030FF
VU_BLUE = 0x4499DDFF
DIV_COL = 0x9DA2AC99
MAPPED = 0xE0A63CFF          # our highlight for a bound control

COL_LX, COL_LW = 12, 230
COL_CX, COL_CW = COL_LX + COL_LW + 8, 360      # 250 / 360
COL_RX, COL_RW = COL_CX + COL_CW + 8, 230      # 618 / 230


def _c(v):
    """0xRRGGBBAA -> (css, alpha)."""
    return f"#{v >> 8:06x}", (v & 0xFF) / 255.0


class Svg:
    """Mirrors VCanvas's five primitives so the transcription reads like the C++."""

    def __init__(self):
        self.p = []

    def rect(self, x, y, w, h, fill, border, rounding=3.0):
        f, fa = _c(fill)
        b, ba = _c(border)
        a = (f'fill="{f}" fill-opacity="{fa:.3f}"' if fill else 'fill="none"')
        s = (f' stroke="{b}" stroke-opacity="{ba:.3f}" stroke-width="1"' if border else '')
        self.p.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" '
                      f'rx="{rounding:.1f}" {a}{s}/>')

    def circle(self, cx, cy, r, fill, border, cls=""):
        f, fa = _c(fill)
        a = (f'fill="{f}" fill-opacity="{fa:.3f}"' if fill else 'fill="none"')
        s = ""
        if border:
            b, ba = _c(border)
            s = f' stroke="{b}" stroke-opacity="{ba:.3f}" stroke-width="1"'
        k = f' class="{cls}"' if cls else ""
        self.p.append(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{r:.1f}" {a}{s}{k}/>')

    def line(self, x1, y1, x2, y2, col, thickness=1.0):
        c, a = _c(col)
        self.p.append(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" '
                      f'stroke="{c}" stroke-opacity="{a:.3f}" stroke-width="{thickness}"/>')

    def text(self, x, y, col, s, size=9, anchor="start", baseline="hanging",
             weight=None, cls=""):
        c, a = _c(col)
        w = f' font-weight="{weight}"' if weight else ""
        k = f' class="{cls}"' if cls else ""
        self.p.append(f'<text x="{x:.1f}" y="{y:.1f}" fill="{c}" fill-opacity="{a:.3f}" '
                      f'font-size="{size}" text-anchor="{anchor}" '
                      f'dominant-baseline="{baseline}"{w}{k}>{html.escape(s)}</text>')

    def centered(self, cx, cy, col, s, size=9, **kw):
        self.text(cx, cy, col, s, size=size, anchor="middle", baseline="central", **kw)


def render(bound_labels, dim_domain=None):
    """bound_labels: {silkscreen-ish key -> parameter name} for controls this map
    binds. Keys are the (section, label) pairs used below. dim_domain in
    {None, 'ChannelStrip', 'BusComp'} drives the off-domain mask, exactly as the
    C++ dimDomain argument does."""
    c = Svg()
    mark = dict(bound_labels)

    def knob(cx, cy, r, cap, label=None, key=None):
        """Transcribed from the C++ knob lambda: outer ring, inner cap,
        indicator at 12 o'clock, label below."""
        hit = key in mark
        c.circle(cx, cy, r, 0x14181EFF, MAPPED if hit else 0x4A5060FF,
                 cls="k-hit" if hit else "")
        c.circle(cx, cy, r * 0.78, cap, 0x80808055)
        c.line(cx, cy - r * 0.85, cx, cy - r * 0.45, 0xE8E8E8FF, 1.6)
        if label:
            c.centered(cx, cy + r + 13, MAPPED if hit else 0xB8BCC4FF, label, size=9)
        if hit:
            c.centered(cx, cy + r + 24, MAPPED, mark[key][:15], size=8.5, cls="pname")

    def btn(x, y, w, h, label, key=None, accent=0x252A33FF):
        hit = key in mark
        c.rect(x, y, w, h, accent, MAPPED if hit else 0x4A5060FF, 3.0)
        c.centered(x + w / 2, y + h / 2, 0xD0D4DAFF, label)
        if hit:
            c.centered(x + w / 2, y + h + 9, MAPPED, mark[key][:15], size=8.5, cls="pname")

    def small_toggle(x, y, label, key=None, drop=18):
        """`drop` staggers the parameter label: the EQ IN / TYPE toggles sit
        38 px apart on the same row, so their labels overlap unless one is
        pushed further down."""
        hit = key in mark
        c.rect(x, y, 34, 18, 0x252A33FF, MAPPED if hit else 0x4A5060FF, 2.5)
        c.centered(x + 17, y + 9, 0xC0C4CCFF, label)
        if hit:
            c.centered(x + 17, y + drop + 9, MAPPED, mark[key][:12], size=8.5, cls="pname")

    def dyn_btn(x, y, label, key=None, side="right"):
        hit = key in mark
        c.rect(x, y, 66, 22, 0x252A33FF, MAPPED if hit else 0x4A5060FF, 3.0)
        c.centered(x + 33, y + 11, 0xC0C4CCFF, label)
        if hit:
            if side == "right":
                c.text(x + 72, y + 11, MAPPED, mark[key][:14], size=8.5,
                       baseline="central", cls="pname")
            else:
                c.text(x - 6, y + 11, MAPPED, mark[key][:12], size=8.5,
                       anchor="end", baseline="central", cls="pname")

    def section_label(x, y, t):
        c.text(x, y, 0x9CA0AAFF, t, size=10, weight=600)

    # ---- chassis -----------------------------------------------------------
    c.rect(4, 4, W - 8, H - 8, 0x14181EFF, 0x2A3038FF, 8.0)

    # ---- left column: Filters + EQ ----------------------------------------
    c.rect(COL_LX, 12, COL_LW, H - 24, 0x1A1E24FF, 0x2A3038FF, 6.0)
    c1, c2 = COL_LX + 70, COL_LX + 150          # 82 / 162
    bx = c2 - 17                                # 145
    gxR, fxR = c1 + 20, c2 + 20                 # 102 / 182

    def band_label(cy, t):
        c.text(COL_LX + 14, cy, 0xB8BCC4FF, t, size=10, baseline="central", weight=600)

    # Stepped band dividers, following the SSL 4000E silk.
    c.line(COL_LX + 50, 210, gxR, 210, DIV_COL)
    c.line(gxR, 210, gxR + 28, 238, DIV_COL)
    c.line(gxR + 28, 238, fxR, 238, DIV_COL)
    c.line(COL_LX + 50, 379, bx - 2, 379, DIV_COL)
    c.line(COL_LX + 50, 548, gxR, 548, DIV_COL)
    c.line(gxR, 548, gxR + 40, 508, DIV_COL)
    c.line(gxR + 40, 508, fxR, 508, DIV_COL)

    knob(c1, 44, 20, GREY_CAP, "LO-PASS", "LPF")
    knob(c2, 70, 20, GREY_CAP, "HI-PASS", "HPF")
    section_label(COL_LX + 14, 104, "FILTERS")
    c.line(c1, 122, COL_LX + COL_LW - 8, 122, DIV_COL)

    band_label(188, "HF")
    knob(c1, 160, 20, RED_CAP, "GAIN", "HFGN")
    knob(c2, 188, 20, RED_CAP, "FREQ", "HFFQ")
    small_toggle(bx, 140, "BELL", "HFBL")

    band_label(288, "HMF")
    knob(c1, 260, 20, GREEN_CAP, "GAIN", "HMFG")
    knob(c2, 288, 20, GREEN_CAP, "FREQ", "HMFF")
    knob(c1, 328, 20, GREEN_CAP, "Q", "HMFQ")

    band_label(379, "EQ")
    small_toggle(bx, 370, "IN", "EQIN", drop=18)
    small_toggle(bx + 38, 370, "TYPE", "EQTY", drop=30)

    band_label(458, "LMF")
    knob(c1, 430, 20, BLUE_CAP, "GAIN", "LMFG")
    knob(c2, 458, 20, BLUE_CAP, "FREQ", "LMFF")
    knob(c1, 498, 20, BLUE_CAP, "Q", "LMFQ")

    band_label(558, "LF")
    knob(c2, 558, 20, BLACK_CAP, "FREQ", "LFFQ")
    knob(c1, 598, 20, BLACK_CAP, "GAIN", "LFGN")
    small_toggle(bx, 620, "BELL", "LFBL")

    # ---- centre column: Bus Comp ------------------------------------------
    c.rect(COL_CX, 12, COL_CW, 420, 0x1A1E24FF, ACCENT_BC, 6.0)
    c.centered(COL_CX + COL_CW / 2, 22, 0x9CA0AAFF, "BUS COMPRESSOR", size=10, weight=600)

    # GR meter: LCD-black face, blue scale, 0 left -> 20 right.
    import math
    mw, mh = 196.0, 80.0
    mx, my = COL_CX + (COL_CW - mw) / 2, 44.0
    c.rect(mx, my, mw, mh, 0x141416FF, 0x282A2EFF, 4.0)
    c.rect(mx + 4, my + 4, mw - 8, mh - 8, 0x080C12FF, 0x444A55FF, 2.0)
    mcx, mcy, ra = mx + mw / 2, my + mh - 3, 70
    a0, a1 = -math.pi * 130 / 180, -math.pi * 50 / 180
    dB_to_a = lambda dB: a0 + (dB / 20.0) * (a1 - a0)
    for dB, lab in ((0, "0"), (1, ""), (2, ""), (3, ""), (5, "5"),
                    (7, ""), (10, "10"), (15, "15"), (20, "20")):
        a = dB_to_a(dB)
        tlen = 8.0 if lab else 5.0
        c.line(mcx + math.cos(a) * ra, mcy + math.sin(a) * ra,
               mcx + math.cos(a) * (ra - tlen), mcy + math.sin(a) * (ra - tlen), VU_BLUE, 1.6)
        if lab:
            c.centered(mcx + math.cos(a) * (ra - 16), mcy + math.sin(a) * (ra - 16),
                       VU_BLUE, lab, size=8)
    aN = dB_to_a(7.0)
    c.line(mcx, mcy, mcx + math.cos(aN) * (ra - 4), mcy + math.sin(aN) * (ra - 4), VU_BLUE, 2.0)
    c.circle(mcx, mcy, 3, VU_BLUE, 0)
    c.centered(mcx, my + mh - 12, VU_BLUE, "GR", size=8)

    c1x, c2x = COL_CX + 120, COL_CX + 240
    ry = (172, 240, 308, 376)
    knob(c1x, ry[0], 20, ACCENT_BC, "THR", "THR")
    knob(c2x, ry[0], 20, ACCENT_BC, "MAKE-UP", "MAKE")
    knob(c1x, ry[1], 20, ACCENT_BC, "ATTACK", "ATK")
    knob(c2x, ry[1], 20, ACCENT_BC, "RELEASE", "REL")
    knob(c1x, ry[2], 20, ACCENT_BC, "RATIO", "RAT")
    _bc_in = "IN" in mark
    c.rect(c2x - 20, ry[2] - 20, 40, 40, 0xE0E0E0FF, MAPPED if _bc_in else 0x808088FF, 3.0)
    c.centered(c2x, ry[2], 0x303338FF, "IN")
    if _bc_in:
        c.centered(c2x, ry[2] + 31, MAPPED, mark["IN"][:15], size=8.5, cls="pname")
    knob(c1x, ry[3], 20, ACCENT_BC, "S/C HPF", "S/C")
    knob(c2x, ry[3], 20, ACCENT_BC, "MIX", "MIX")

    # ---- Central Control Panel --------------------------------------------
    ccp_y, ccp_h = 440.0, 208.0
    c.rect(COL_CX, ccp_y, COL_CW, ccp_h, 0x1A1E24FF, ACCENT_CC, 6.0)
    c.rect(COL_CX + 14, ccp_y + 14, 56, 30, 0x1A0408FF, 0x401818FF, 3.0)
    c.centered(COL_CX + 42, ccp_y + 28, 0xFF3030FF, "001", size=13)
    lcd_x, lcd_w = COL_CX + 78, COL_CW - 138
    lcd_cx = lcd_x + lcd_w / 2
    c.rect(lcd_x, ccp_y + 12, lcd_w, 76, 0x05080CFF, 0x444A55FF, 3.0)
    c.centered(lcd_cx, ccp_y + 26, 0x808088FF, "MAIN", size=8)
    c.centered(lcd_cx, ccp_y + 46, 0xE0E0E0FF, "Track Name", size=11)
    c.centered(lcd_cx, ccp_y + 66, 0x4488DDFF, "Stereo Bus", size=9)
    total = 2 * 80 + 20
    x0, y0 = COL_CX + (COL_CW - total) / 2, ccp_y + 100
    for i, lab in enumerate(("360", "MAGNIFY")):
        bxx = x0 + i * 100
        c.rect(bxx, y0, 80, 22, 0x252A33FF, 0x4A5060FF, 3.0)
        c.centered(bxx + 40, y0 + 11, 0xC0C4CCFF, lab)
    mid_x = COL_CX + COL_CW / 2
    knob(mid_x - 40, ccp_y + 145, 18, GREY_CAP, "CS Encoder")
    knob(mid_x + 40, ccp_y + 145, 18, GREY_CAP, "BC Encoder")

    # ---- right column: Dynamics + Channel ----------------------------------
    c.rect(COL_RX, 12, COL_RW, H - 24, 0x1A1E24FF, 0x2A3038FF, 6.0)
    section_label(COL_RX + 14, 26, "COMPRESSOR")
    dyn_btn(COL_RX + COL_RW - 76, 24, "FAST ATK", "C ATK", side="left")
    dyn_btn(COL_RX + COL_RW - 76, 50, "PEAK", "C PK", side="left")
    rc1, rc2 = COL_RX + 60, COL_RX + 156
    knob(rc1, 100, 20, GREY_CAP, "RATIO", "C RAT")
    knob(rc2, 128, 20, GREY_CAP, "THRESHOLD", "C THR")
    knob(rc1, 168, 20, GREY_CAP, "RELEASE", "C REL")
    btn(rc1 - 17, 235, 34, 22, "IN", "DYN")
    for i, step in enumerate(("20", "14", "10", "6", "3")):
        ly = 220 + i * 13
        c.centered(rc2 - 14, ly, 0x808088FF, step, size=8)
        c.circle(rc2 + 6, ly, 3, 0x9A5A2AFF, 0)
        c.circle(rc2 + 20, ly, 3, 0x2E8040FF, 0)
    knob(rc1, 324, 20, GREY_CAP, "RANGE", "G RNG")
    knob(rc2, 352, 20, GREY_CAP, "THRESHOLD", "G THR")
    knob(rc1, 392, 20, GREY_CAP, "RELEASE", "G REL")
    knob(rc2, 420, 20, GREY_CAP, "HOLD", "G HLD")
    dyn_btn(rc1 - 33, 462, "EXPAND", "G/E")
    dyn_btn(rc1 - 33, 488, "FAST ATK", "G ATK")
    c.text(COL_RX + COL_RW - 14, 492, 0x9CA0AAFF, "GATE /", size=10, anchor="end", weight=600)
    c.text(COL_RX + COL_RW - 14, 510, 0x9CA0AAFF, "EXPANDER", size=10, anchor="end", weight=600)

    section_label(COL_RX + 14, 518, "CHANNEL")
    btn(COL_RX + 14, 540, 96, 26, "\u00d8", "POL")
    btn(COL_RX + 14, 574, 96, 26, "S/C LISTEN", "S/C L")
    _cin = "IN" in mark
    c.rect(COL_RX + 158, 547, 40, 40, 0xE0E0E0FF, MAPPED if _cin else 0x808088FF, 3.0)
    c.centered(COL_RX + 178, 567, 0x303338FF, "IN")
    if _cin:
        c.centered(COL_RX + 178, 598, MAPPED, mark["IN"][:14], size=8.5, cls="pname")

    # CS Input / Output gain — drawn before the dim mask, as in the C++, then
    # re-drawn after it so they stay bright (they are CS, not BC).
    def in_out():
        knob(COL_CX + 60, 376, 20, GREY_CAP, "INPUT", "IN G")
        knob(COL_CX + 300, 376, 20, GREY_CAP, "OUTPUT", "OUT G")

    c.centered(W / 2, ccp_y + ccp_h - 14, 0x9CA0AAFF, "Rea-Sixty / UC1", size=9)

    # ---- off-domain dim mask ----------------------------------------------
    DIM = 0x000000A0
    if dim_domain == "ChannelStrip":
        c.rect(COL_CX, 12, COL_CW, 420, DIM, 0, 6.0)
    elif dim_domain == "BusComp":
        c.rect(COL_LX, 12, COL_LW, H - 24, DIM, 0, 6.0)
        c.rect(COL_RX, 12, COL_RW, H - 24, DIM, 0, 6.0)
    in_out()

    return (f'<svg viewBox="0 0 {W} {H}" class="uc1face" '
            f'font-family="ui-sans-serif, system-ui, sans-serif">{"".join(c.p)}</svg>')


# Keys are the kUc1Controls[] `label` strings, verbatim. Callers build
# {label: parameter-name} by joining a map's slots[].linkIdx against that table
# for the map's own domain — the labels are NOT to be written out by hand here.
# An earlier draft did guess them and got most of the dynamics section wrong
# (22 is DYN, not C RAT; 5 is POL; 36 is S/C L).
