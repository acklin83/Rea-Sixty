#!/usr/bin/env python3
"""
Mapping Exchange — prototype, built for VOLUME.

Correction over the previous version, which listed MAPS on the index. The unit
of the exchange is the PLUG-IN carrying N maps; a flat map list means "FabFilter
Pro-Q 3" appearing fourteen times down the page. At a few hundred maps that is
not a list, it is noise.

  Screen 1  /mappings                 PLUG-IN index. Dense rows, search-first,
                                      vendor facet, paginated.
  Screen 1b                           the same index at 180 rows — density test.
  Screen 2  /mappings/plugin/<slug>   one plug-in -> its maps, compact, with
                                      select-two-to-diff. A diff table stops
                                      being readable past ~4 columns, so
                                      comparison is a selection, not the default.
  Screen 3  /mappings/<id>/<slug>     one map. The faceplate lives here only.

Honest data notes:
  - Coverage, sections, surface scope, bindings: REAL, from the 29-map catalog.
  - Author / date / "works for me" / vendor: NOT in user_plugins.json. Vendor is
    recoverable from only 8 of 29 match strings and unreliably even then
    ("(SSL) (mono)" yields "mono"), which is why the plan already says it must be
    captured at upload. All render as explicit placeholders.
  - Screen 1b repeats the real plug-in names to reach 180 rows with synthetic
    map counts. Labelled on the page. It exists to judge scanning density, not
    to claim a corpus.
"""
import json
import re
import html
from pathlib import Path

REPO = Path("/Users/stoersender/Documents/dev/Rea-Sixty")
SRC = REPO / "extension/src/SettingsScreen.cpp"
CATALOG = Path.home() / "Library/Application Support/REAPER/rea_sixty/user_plugins.json"
OUT = Path(__file__).parent / "exchange.html"

FACE_W, FACE_H, MARGIN = 860, 660, 250
src_text = SRC.read_text()
body = re.search(r"constexpr Uc1Control kUc1Controls\[\] = \{(.*?)\n\};", src_text, re.S).group(1)
ENTRY = re.compile(
    r"\{\s*Uc1Control::(\w+)\s*,\s*(\d+)\s*,\s*uf8::Domain::(\w+)\s*,\s*"
    r"([\d.]+)f?\s*,\s*([\d.]+)f?\s*,\s*([\d.]+)f?\s*,\s*([\d.]+)f?\s*,\s*([\d.]+)f?\s*,\s*"
    r"(\w+)\s*,\s*\"([^\"]*)\"\s*\}", re.S)
CONTROLS = [dict(kind=m.group(1), linkIdx=int(m.group(2)), domain=m.group(3),
                 cx=float(m.group(4)), cy=float(m.group(5)), r=float(m.group(6)),
                 w=float(m.group(7)), h=float(m.group(8)), cap=m.group(9), label=m.group(10))
            for m in ENTRY.finditer(body)]
assert len(CONTROLS) == 41
caps = dict(re.findall(r"constexpr uint32_t (kCap\w+)\s*=\s*0x([0-9A-Fa-f]{8})", src_text))
cap_css = lambda n: f"#{caps[n][:6]}" if n in caps else "#6A707C"
PH = '<span class="ph" title="Created at upload; not in the local catalog">&mdash;</span>'


def section_of(l):
    l = l.strip()
    if l in ("LPF", "HPF"):
        return "Filters"
    for pre, name in (("HMF", "HMF"), ("HF", "HF"), ("LMF", "LMF"), ("LF", "LF"), ("EQ", "EQ")):
        if l.startswith(pre):
            return name
    if l.startswith("C ") or l == "DYN":
        return "Comp"
    if l.startswith("G ") or l == "G/E":
        return "Gate"
    if l in ("THR", "MAKE", "ATK", "REL", "RAT", "MIX", "S/C", "IN"):
        return "Bus Comp"
    return "Channel"


SECTIONS = ["Filters", "HF", "HMF", "EQ", "LMF", "LF", "Comp", "Gate", "Bus Comp", "Channel"]
MAPS = json.load(CATALOG.open())["plugins"]


UF8_VPOT_SLOTS = 2 * 8 * 8   # banksByFaderBank: 2 fader banks x 8 V-Pot banks x 8 strips
UF8_STRIPS = 2 * 8           # stripsByFaderBank


def uf8_coverage(m):
    """UF8-mode maps have their own denominator: 128 V-Pot slots (2 fader banks
    x 8 V-Pot banks x 8 strips) plus 16 strip bindings.

    Two traps, both hit while building this:
    1. The on-disk keys are banksByFaderBank / stripsByFaderBank, NOT the
       banks / strips of the C++ struct. Reading the struct names yields zero
       and every UF8 row renders as 0/0.
    2. Do NOT count by recursing for any "vst3Param" you can find. Some slots
       carry nested structures with their own vst3Param, so recursion returned
       130 bound slots out of a possible 128 — an impossible number is what
       exposed it. A slot is a slot: walk the fixed 3-level shape.
    """
    u = m.get("uf8") or {}
    vpots = 0
    for fader_bank in u.get("banksByFaderBank") or []:
        for vpot_bank in fader_bank or []:
            for slot in vpot_bank or []:
                if isinstance(slot, dict) and slot.get("vst3Param", -1) >= 0:
                    vpots += 1
    # A strip binds up to four things; count the strip as covered if any is set.
    strips = 0
    for fader_bank in u.get("stripsByFaderBank") or []:
        for st in fader_bank or []:
            if isinstance(st, dict) and any(
                    st.get(k, -1) >= 0 for k in
                    ("faderVst3Param", "soloVst3Param", "cutVst3Param", "selVst3Param")):
                strips += 1
    assert vpots <= UF8_VPOT_SLOTS, f"{vpots} > {UF8_VPOT_SLOTS} in {m['match']}"
    assert strips <= UF8_STRIPS, f"{strips} > {UF8_STRIPS} in {m['match']}"
    return vpots, strips


def analyse(m):
    dom = m.get("domain")
    names = {p["vst3Param"]: p.get("name", "") for p in m.get("paramSnapshot", [])}
    bound = {s["linkIdx"]: s for s in m.get("slots", []) if s.get("vst3Param", -1) >= 0}
    in_dom = [c for c in CONTROLS if c["domain"] == dom]
    covered = [c for c in in_dom if c["linkIdx"] in bound]
    secs = {}
    for c in in_dom:
        s = secs.setdefault(section_of(c["label"]), {"total": 0, "hit": 0, "cells": []})
        s["total"] += 1
        hit = c["linkIdx"] in bound
        s["hit"] += hit
        s["cells"].append((c, hit))
    vpots, strips_bound = uf8_coverage(m)
    is_uf8_only = dom == "None"
    return dict(m=m, dom=dom, names=names, bound=bound, in_dom=in_dom, covered=covered,
                uf8_vpots=vpots, uf8_strips=strips_bound, is_uf8_only=is_uf8_only,
                cov_n=vpots if is_uf8_only else len(covered),
                cov_d=UF8_VPOT_SLOTS if is_uf8_only else len(in_dom),
                extras=[li for li in bound if li not in {c["linkIdx"] for c in in_dom}],
                secs=secs,
                surface=("UC1+UF8" if dom != "None" and m.get("uf8Mode")
                         else ("UF8" if dom == "None" else "UC1")),
                pct=(round(100 * vpots / UF8_VPOT_SLOTS) if is_uf8_only
                     else (round(100 * len(covered) / len(in_dom)) if in_dom else 0)),
                title=re.sub(r"^(VST3|VST|JS|AU): ", "", m["match"]).split(" (")[0].split(" [")[0].strip())


def strip(a, cell=4, gap=1, sgap=4, h=2.4):
    parts, x = [], 0
    for name in SECTIONS:
        s = a["secs"].get(name)
        if not s:
            continue
        for c, hit in s["cells"]:
            parts.append(f'<rect x="{x}" y="0" width="{cell}" height="{cell*h:.1f}" rx="1.2" '
                         f'class="cell {"on" if hit else "off"}">'
                         f'<title>{html.escape(c["label"])}</title></rect>')
            x += cell + gap
        x += sgap
    return (f'<svg class="strip" viewBox="0 0 {max(x,1)} {cell*h:.1f}" '
            f'width="{max(x,1):.0f}" height="{cell*h:.0f}">{"".join(parts)}</svg>')


def caps_of(a):
    m, slots, out = a["m"], a["m"].get("slots", []), []
    if any(s.get("modLayers") for s in slots): out.append("modifier layers")
    if any(s.get("pushSteps") for s in slots): out.append("push-cycles")
    if any(s.get("curvePoints") for s in slots): out.append("custom curves")
    if any(s.get("inverted") for s in slots): out.append("inverted")
    if m.get("extFuncs"): out.append("EXT FUNCS")
    if m.get("uf8Mode"): out.append("UF8 strips")
    if a["extras"]: out.append("off-face")
    return out


ANALYSED = [analyse(m) for m in MAPS]
GROUPS = {}
for a in ANALYSED:
    GROUPS.setdefault(a["title"], []).append(a)
ordered = sorted(GROUPS.items(), key=lambda kv: kv[0].lower())


def plugin_row(title, group, count=None):
    best = max(group, key=lambda a: a["pct"])
    surfaces = sorted({a["surface"] for a in group})
    n = count if count is not None else len(group)
    return (f'<tr><td class="pname"><a href="#s2">{html.escape(title)}</a></td>'
            f'<td class="pvendor">{PH}</td>'
            f'<td class="pcount{" one" if n == 1 else ""}"><b>{n}</b></td>'
            f'<td class="psurf">{"".join(f"<span class=badge>{s}</span>" for s in surfaces)}</td>'
            f'<td class="pcov"><b>{best["cov_n"]}</b><span class="of">/{best["cov_d"]}</span>'
            f'<span class="covbar"><i style="width:{best["pct"]}%"></i></span></td>'
            f'<td class="pworks">{PH}</td></tr>')


plugin_rows = "".join(plugin_row(t, g) for t, g in ordered[:18])
scale_rows = "".join(plugin_row(t, g, count=1 + (i * 7) % 9)
                     for i, (t, g) in enumerate(ordered[i % len(ordered)] for i in range(180)))

PAIR = [a for a in ANALYSED if a["title"] in ("FabFilter Pro-C 2", "Pro-C 2")]


def map_row(a, checked=False):
    return (f'<li class="mrow"><label class="mpick">'
            f'<input type=checkbox {"checked" if checked else ""}></label>'
            f'<span class="mid"><b>by {PH}</b>'
            f'<span class="msub">{html.escape(a["m"]["match"][:42])}</span></span>'
            f'<span class="mstrip">{strip(a)}</span>'
            f'<span class="mcov"><b>{a["cov_n"]}</b>/{a["cov_d"]}</span>'
            f'<span class="mcaps">'
            f'{"".join(f"<span class=cap>{c}</span>" for c in caps_of(a)) or "<span class=\'cap none\'>&mdash;</span>"}'
            f'</span><span class="mworks">{PH}</span>'
            f'<a class="btn-sm" href="#s3">Open</a></li>')


def diff_table(group):
    order = {c["linkIdx"]: i for i, c in enumerate(CONTROLS)}
    lbl = {c["linkIdx"]: c["label"] for c in CONTROLS if c["domain"] == group[0]["dom"]}
    keys = sorted({li for a in group for li in a["bound"]}, key=lambda li: order.get(li, 999))
    head = "".join(f'<th><span class="ct">by {PH}</span>'
                   f'<span class="cs"><b>{len(a["covered"])}</b>/{len(a["in_dom"])}</span></th>'
                   for a in group)
    rows, same = [], 0
    for li in keys:
        vals = [a["names"].get(a["bound"][li]["vst3Param"], "?") if li in a["bound"] else None
                for a in group]
        differs = len(set(vals)) > 1
        same += not differs
        name = lbl.get(li)
        ctrl = (f'<code>{html.escape(name)}</code>' if name
                else f'<span class="offface">no UC1 control</span> <code>#{li}</code>')
        cells = "".join(f'<td class="{"diff" if differs else ""}">'
                        f'{html.escape(v) if v else "<span class=none>&mdash;</span>"}</td>'
                        for v in vals)
        rows.append(f'<tr class="{"rdiff" if differs else ""}">'
                    f'<td class="ctrlcell">{ctrl}</td>{cells}</tr>')
    return (f'<p class="diffsum">{len(keys)} bound controls between them &mdash; '
            f'<b>{same} identical</b>, <b class="hl">{len(keys)-same} different</b>.</p>'
            f'<div class="cmptable"><table class="diff">'
            f'<thead><tr><th class="ctrlcell">Control</th>{head}</tr></thead>'
            f'<tbody>{"".join(rows)}</tbody></table></div>')


# The UC1 faceplate is GONE, on purpose. Measured: the face is an 860x660
# drawing; inside a phone's ~342px of usable width it scales to 0.40, which puts
# the 9px silkscreen at 3.6px and the parameter names at 3.4px. Even on a 980px
# tablet the silkscreen is already 6.6px. There is no label strategy that fixes
# an unreadable font size, and a horizontally-scrolling faceplate on a phone is
# worse than no picture at all.
#
# What replaces it is what was underneath it the whole time: the control ->
# parameter table, grouped by section. It reflows to any width, it is what a
# screen reader and a search engine read, and the section grouping carries the
# shape of the mapping ("all of the EQ, none of the gate") without a drawing.


def section_table(a):
    """Control -> parameter, grouped by the sections a user thinks in. This is
    the detail view now; there is no faceplate."""
    by_sec = {}
    for c in a["in_dom"]:
        by_sec.setdefault(section_of(c["label"]), []).append(c)
    blocks = []
    for name in SECTIONS:
        cols = by_sec.get(name)
        if not cols:
            continue
        hit = [c for c in cols if c["linkIdx"] in a["bound"]]
        rows = "".join(
            f'<tr class="{"on" if c["linkIdx"] in a["bound"] else "off"}">'
            f'<td class="ctl"><code>{html.escape(c["label"])}</code></td>'
            f'<td class="par">'
            f'{html.escape(a["names"].get(a["bound"][c["linkIdx"]]["vst3Param"], "?")) if c["linkIdx"] in a["bound"] else "<span class=none>not mapped</span>"}'
            f'</td></tr>' for c in cols)
        state = "full" if len(hit) == len(cols) else ("part" if hit else "none")
        blocks.append(
            f'<section class="secblock {state}">'
            f'<h4>{name} <span class="seccount">{len(hit)}/{len(cols)}</span></h4>'
            f'<table class="sectbl"><tbody>{rows}</tbody></table></section>')
    extra = ""
    if a["extras"]:
        rows = "".join(
            f'<tr class="on"><td class="ctl"><code>#{li}</code></td>'
            f'<td class="par">{html.escape(a["names"].get(a["bound"][li]["vst3Param"], "?"))}</td></tr>'
            for li in sorted(a["extras"]))
        extra = (f'<section class="secblock part"><h4>No UC1 control '
                 f'<span class="seccount">{len(a["extras"])}</span></h4>'
                 f'<table class="sectbl"><tbody>{rows}</tbody></table></section>')
    return f'<div class="secgrid">{"".join(blocks)}{extra}</div>'


D = next(a for a in ANALYSED if a["title"].startswith("bx_console SSL 4000 G"))
drows = "".join(f'<tr><td><code>{html.escape(c["label"])}</code></td>'
                f'<td>{html.escape(D["names"].get(D["bound"][c["linkIdx"]]["vst3Param"], "?"))}</td></tr>'
                for c in D["covered"])
# Real vendor list, extracted from REAPER's own plug-in scan caches on this
# machine (see the plan): 82 distinct vendors across 1817 plug-ins. That count
# is the whole argument for a searchable combobox — a checkbox rail cannot hold
# 82 rows, and a real corpus will carry several hundred.
VENDORS = json.load(open(Path(__file__).parent / "vendors.json"))
ALL_CAPS = sorted({c for a in ANALYSED for c in caps_of(a)})
VENDOR_TOTAL = sum(c for _, c in VENDORS)

CSS = """
:root{--bg:#14161a;--raised:#1c1f25;--sunken:#0f1114;--fg:#e6e8ec;--muted:#9aa1ad;
--faint:#6b727d;--border:#2b2f37;--accent:#e0a63c}
*{box-sizing:border-box}
body{background:var(--bg);color:var(--fg);font:15px/1.5 ui-sans-serif,system-ui,sans-serif;margin:0;padding:0 0 5rem}
.wrap{max-width:1120px;margin:0 auto;padding:0 1.5rem}
.top{border-bottom:1px solid var(--border);padding:.9rem 0;margin-bottom:1.8rem}
.top .wrap{display:flex;align-items:center;gap:.8rem}
.brandmark{width:15px;height:15px;border-radius:4px;background:var(--accent)}
h1{font-size:1rem;margin:0;font-weight:680}
.screen{margin-bottom:3.5rem}
.screen-tag{font:600 .68rem ui-monospace,Menlo,monospace;letter-spacing:.08em;text-transform:uppercase;color:var(--accent);margin-bottom:.3rem}
.screen h2{font-size:1.4rem;margin:0 0 .25rem}
.path{font:.8rem ui-monospace,Menlo,monospace;color:var(--faint);margin:0 0 1.2rem}
.note{background:#2a1f16;border:1px solid #6b4a22;border-radius:8px;padding:.8rem .95rem;font-size:.85rem;margin-bottom:1.4rem;max-width:84ch;color:#e8d9c0}
.note.plain{background:var(--raised);border-color:var(--border);color:var(--muted)}
.layout{display:grid;grid-template-columns:186px minmax(0,1fr);gap:1.8rem;align-items:start}
.facets h3{font-size:.68rem;text-transform:uppercase;letter-spacing:.07em;color:var(--faint);margin:1.1rem 0 .4rem}
.facets h3:first-child{margin-top:0}
.facets label{display:flex;gap:.4rem;align-items:center;font-size:.83rem;color:var(--muted);padding:.08rem 0;cursor:pointer}
.search input{width:100%;background:var(--sunken);border:1px solid var(--border);border-radius:8px;padding:.55rem .75rem;color:var(--fg);font-size:.92rem;margin-bottom:.8rem}
.sortbar{display:flex;justify-content:space-between;align-items:center;color:var(--muted);font-size:.82rem;margin-bottom:.5rem}
.seg{display:inline-flex;border:1px solid var(--border);border-radius:6px;overflow:hidden}
.seg span{padding:.2rem .6rem;font-size:.79rem}
.seg span.on{background:var(--accent);color:#14161a;font-weight:600}
table.idx{width:100%;border-collapse:collapse;font-size:.88rem}
table.idx thead th{text-align:left;font:600 .68rem ui-monospace,Menlo,monospace;letter-spacing:.06em;
text-transform:uppercase;color:var(--faint);padding:.35rem .6rem;border-bottom:1px solid var(--border);
position:sticky;top:0;background:var(--bg)}
table.idx td{padding:.32rem .6rem;border-bottom:1px solid #21242b}
table.idx tr:hover td{background:var(--raised)}
.pname a{color:var(--fg);text-decoration:none;font-weight:560}
.pname a:hover{color:var(--accent)}
.pvendor{color:var(--muted);width:120px}
.pcount{width:54px;text-align:right;color:var(--muted)}
.pcount b{color:var(--fg)}
.psurf{width:140px}
.pcov{width:112px;color:var(--faint);white-space:nowrap}
.pcov b{color:var(--fg)}
.pworks{width:54px;text-align:right}
.covbar{display:inline-block;width:50px;height:4px;background:#2b2f37;border-radius:2px;overflow:hidden;margin-left:.35rem;vertical-align:middle}
.covbar i{display:block;height:100%;background:var(--accent)}
.badge{font:600 .66rem ui-monospace,Menlo,monospace;border:1px solid var(--border);border-radius:999px;padding:.05rem .42rem;color:var(--muted);margin-right:.2rem}
.ph{color:var(--faint)}
.combo{position:relative}
.chips{display:flex;flex-wrap:wrap;gap:.25rem;margin-bottom:.35rem}
.chip{display:inline-flex;gap:.35rem;align-items:center;font-size:.76rem;
background:rgba(224,166,60,.14);border:1px solid rgba(224,166,60,.5);color:var(--accent);
border-radius:999px;padding:.1rem .5rem;cursor:pointer}
.chip b{font-weight:600;opacity:.7}
.comboinput{width:100%;background:var(--sunken);border:1px solid var(--accent);
border-radius:7px;padding:.35rem .55rem;color:var(--fg);font-size:.83rem;font-family:inherit}
.combolist{list-style:none;margin:.3rem 0 0;padding:.25rem;background:var(--raised);
border:1px solid var(--border);border-radius:7px;max-height:190px;overflow-y:auto}
.combolist li{display:flex;justify-content:space-between;gap:.5rem;font-size:.83rem;
color:var(--fg);padding:.22rem .4rem;border-radius:4px;cursor:pointer}
.combolist li:hover{background:rgba(255,255,255,.05)}
.combolist li b{color:var(--faint);font-weight:400;font-size:.76rem}
.combolist li.muted{color:var(--faint);cursor:default;font-size:.75rem;
border-top:1px solid var(--border);margin-top:.2rem;padding-top:.35rem}
.combolist li.muted:hover{background:none}
.pager{display:flex;justify-content:space-between;align-items:center;margin-top:.8rem;color:var(--faint);font-size:.82rem}
.pager .pages span{border:1px solid var(--border);border-radius:5px;padding:.15rem .5rem;margin-left:.25rem}
.pager .pages span.on{background:var(--accent);color:#14161a;font-weight:600;border-color:var(--accent)}
.scaler{max-height:340px;overflow:auto;border:1px solid var(--border);border-radius:8px}
ul.mrows{list-style:none;margin:0 0 1.2rem;padding:0}
.mrow{display:grid;grid-template-columns:24px minmax(0,1fr) auto 58px minmax(0,1.1fr) 50px 60px;
gap:.8rem;align-items:center;padding:.45rem .7rem;border:1px solid var(--border);border-radius:8px;
background:var(--raised);margin-bottom:.35rem;font-size:.87rem}
.mid b{display:block;font-weight:560}
.msub{font:.74rem ui-monospace,Menlo,monospace;color:var(--faint);display:block;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.mcov{text-align:right;color:var(--faint)}
.mcov b{color:var(--fg)}
.mcaps{display:flex;flex-wrap:wrap;gap:.22rem}
.cap{font-size:.68rem;background:var(--sunken);border:1px solid var(--border);border-radius:999px;padding:.03rem .4rem;color:var(--muted)}
.cap.none{color:var(--faint)}
.mworks{text-align:right;color:var(--muted)}
.btn-sm{background:var(--accent);color:#14161a;border:0;border-radius:6px;padding:.25rem .6rem;font-size:.78rem;font-weight:640;text-decoration:none;text-align:center}
.strip .cell{fill:#242932}
.strip .cell.on{fill:var(--accent)}
.diffsum{color:var(--muted);font-size:.88rem;margin:0 0 .6rem}
.diffsum .hl{color:var(--accent)}
.cmptable{overflow-x:auto;border:1px solid var(--border);border-radius:9px;background:var(--raised)}
table.diff{width:100%;border-collapse:collapse;font-size:.86rem;margin:0}
table.diff th{text-align:left;vertical-align:top;padding:.6rem .7rem;border-bottom:1px solid var(--border)}
table.diff th .ct{display:block;font-size:.8rem}
table.diff th .cs{display:block;font-size:.74rem;color:var(--faint)}
table.diff th .cs b{color:var(--fg)}
table.diff td{padding:.32rem .7rem;border-bottom:1px solid #21242b}
table.diff .ctrlcell{width:140px;color:var(--muted)}
code{background:var(--sunken);border:1px solid var(--border);border-radius:4px;padding:.03rem .32rem;font-size:.78rem;font-family:ui-monospace,Menlo,monospace}
table.diff td.diff{color:var(--accent)}
table.diff tr.rdiff{background:rgba(224,166,60,.06)}
.none{color:var(--faint)}
.offface{color:var(--accent);font-size:.76rem}
.detail{display:grid;grid-template-columns:270px minmax(0,1fr);gap:1.8rem;align-items:start}
.dmeta h3{margin:0 0 .15rem;font:1rem ui-monospace,Menlo,monospace}
.dmeta .sub{color:var(--faint);font-size:.83rem;margin-bottom:.9rem}
.btn{background:var(--accent);color:#14161a;border:0;border-radius:7px;padding:.5rem 1rem;font-size:.88rem;font-weight:640;width:100%;cursor:pointer;font-family:inherit}
.dmeta dl{margin:1rem 0;font-size:.85rem}
.dmeta dt{color:var(--faint);font-size:.68rem;text-transform:uppercase;letter-spacing:.06em;margin-top:.65rem}
.dmeta dd{margin:0}
.face{width:100%;height:auto;background:#101418;border:1px solid var(--border);border-radius:8px}
.plate{fill:#161a20}
.ctrl{stroke:#4A5060;stroke-width:1}
.ctrl.lit{stroke:var(--accent);stroke-width:1.6}
.ctrl.dim{opacity:.45}
.ctrl.off{opacity:.1}
.silk{font:9px ui-monospace,Menlo,monospace;fill:#8d939d;text-anchor:middle}
.silk.dim{fill:#4d535c}
.silk.off{opacity:.1}
.leader{fill:none;stroke:var(--accent);stroke-width:.85;opacity:.55}
.leader-dot{fill:var(--accent)}
.lbl{font:11px ui-sans-serif,system-ui,sans-serif;fill:#d7dae0;dominant-baseline:middle}
.lbl-ctrl{font-family:ui-monospace,Menlo,monospace;font-size:9.5px;fill:var(--accent)}
.lbl-sep{fill:var(--faint)}
details{margin-top:.9rem}
summary{cursor:pointer;color:var(--muted);font-size:.86rem}
table.plain{width:100%;border-collapse:collapse;font-size:.84rem;margin-top:.5rem}
table.plain td{padding:.26rem .5rem;border-bottom:1px solid #21242b}
.dstrip{margin:.9rem 0 .2rem}
.secgrid{display:grid;grid-template-columns:repeat(auto-fill,minmax(230px,1fr));gap:.8rem}
.secblock{background:var(--raised);border:1px solid var(--border);border-radius:9px;padding:.6rem .7rem}
.secblock.full{border-color:rgba(224,166,60,.5)}
.secblock h4{margin:0 0 .35rem;font:600 .74rem ui-monospace,Menlo,monospace;
letter-spacing:.05em;text-transform:uppercase;color:var(--muted);display:flex;
justify-content:space-between;gap:.5rem}
.secblock.full h4{color:var(--accent)}
.seccount{color:var(--faint);font-weight:400}
.secblock.full .seccount{color:var(--accent)}
table.sectbl{width:100%;border-collapse:collapse;font-size:.83rem}
table.sectbl td{padding:.18rem .1rem;border:0}
table.sectbl .ctl{width:64px}
table.sectbl tr.off .par{color:var(--faint)}
table.sectbl tr.off code{opacity:.45}
table.sectbl .par{color:var(--fg)}
/* ---- mobile: everything stacks, nothing scrolls sideways ---- */
@media (max-width:760px){
  .wrap{padding:0 1rem}
  .layout,.detail{grid-template-columns:1fr;gap:1.1rem}
  .facets{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:0 1rem}
  .facets h3{grid-column:1/-1;margin-top:.7rem}
  table.idx thead{display:none}
  table.idx,table.idx tbody,table.idx tr,table.idx td{display:block;width:auto}
  table.idx tr{border:1px solid var(--border);border-radius:9px;background:var(--raised);
    padding:.5rem .7rem;margin-bottom:.4rem;position:relative}
  table.idx td{border:0;padding:.05rem 0}
  table.idx td.pname{font-size:.95rem;padding-right:5.5rem}
  table.idx td.pvendor:before{content:"Vendor ";color:var(--faint);font-size:.75rem}
  table.idx td.pcount{position:absolute;top:.5rem;right:.7rem;width:auto;text-align:right}
  table.idx td.pcount:after{content:" maps";color:var(--faint);font-size:.75rem}
  table.idx td.pcount.one:after{content:" map"}
  table.idx td.pcov{width:auto;white-space:normal}
  table.idx td.pcov:before{content:"Best ";color:var(--faint);font-size:.75rem}
  table.idx td.pworks{display:none}
  .mrow{grid-template-columns:22px minmax(0,1fr) auto;grid-auto-rows:auto;row-gap:.4rem}
  .mrow .mpick{grid-area:1/1}
  .mrow .mid{grid-area:1/2}
  .mrow .mcov{grid-area:1/3;align-self:start}
  .mrow .mstrip{grid-area:2/2/2/4}
  .mrow .mcaps{grid-area:3/2/3/4}
  .mrow .btn-sm{grid-area:4/2/4/4;justify-self:start;padding:.3rem 1.1rem}
  .mrow .mworks{display:none}
  .secgrid{grid-template-columns:1fr}
  .cmptable{font-size:.8rem}
}
"""

HEAD = ('<thead><tr><th>Plug-in</th><th>Vendor</th><th style="text-align:right">Maps</th>'
        '<th>Surfaces</th><th>Best coverage</th><th style="text-align:right">Works</th></tr></thead>')

OUT.write_text(f"""<!doctype html><meta charset=utf-8>
<title>Mapping Exchange &mdash; prototype</title>
<style>{CSS}</style>
<div class=top><div class=wrap><span class=brandmark></span><h1>Rea-Sixty &mdash; Mapping Exchange</h1>
<span style="margin-left:auto;color:var(--faint);font-size:.83rem">prototype</span></div></div>
<div class=wrap>

<div class=note><b>Real:</b> coverage, sections, surface scope and every binding come from the
29-map catalog on this machine. <b>Not real:</b> author, date, &ldquo;works for me&rdquo; and
vendor are not in <code>user_plugins.json</code> &mdash; vendor is recoverable from only 8 of
29 match strings and unreliably even then (<code>(SSL) (mono)</code> yields &ldquo;mono&rdquo;),
which is why the plan says it must be captured at upload. All render as <span class=ph>&mdash;</span>.</div>

<section class=screen>
  <p class=screen-tag>Screen 1 &mdash; browse</p>
  <h2>Plug-ins, not mappings</h2>
  <p class=path>/mappings</p>
  <div class="note plain">The index lists <b>plug-ins</b>. Listing maps instead means
  &ldquo;FabFilter Pro-Q 3&rdquo; appearing fourteen times down the page &mdash; at a few
  hundred maps that is not a list, it is noise. One row per plug-in, the map count as a
  column, the maps themselves one click away.</div>
  <div class=layout>
    <aside class=facets>
      <h3>Surface</h3>
      <label><input type=checkbox> UC1</label><label><input type=checkbox> UF8</label>
      <label><input type=checkbox> UC1+UF8</label>
      <h3>Vendor</h3>
      <div class=combo>
        <div class=chips><span class=chip>Plugin Alliance <b>&times;</b></span></div>
        <input class=comboinput placeholder="Search {len(VENDORS)} vendors&hellip;" value="sl">
        <ul class=combolist>
          <li><span>Slate Digital</span><b>112</b></li>
          <li><span>SSL</span><b>86</b></li>
          <li><span>Softube</span><b>18</b></li>
          <li><span>Sonnox</span><b>9</b></li>
          <li class=muted>4 of 82 match &ldquo;sl&rdquo;</li>
        </ul>
      </div>
      <h3>Uses</h3>{"".join(f'<label><input type=checkbox> {c}</label>' for c in ALL_CAPS)}
    </aside>
    <div>
      <div class=search><input placeholder="Search plug-ins &mdash; Pro-Q, 1176, console&hellip;"></div>
      <div class=sortbar><span>{len(GROUPS)} plug-ins &middot; {len(ANALYSED)} mappings</span>
        <span class=seg><span class=on>A&ndash;Z</span><span>Most mappings</span><span>Newest</span></span></div>
      <table class=idx>{HEAD}<tbody>{plugin_rows}</tbody></table>
      <div class=pager><span>1&ndash;18 of {len(GROUPS)}</span>
        <span class=pages><span class=on>1</span><span>2</span><span>&rsaquo;</span></span></div>
    </div>
  </div>
</section>

<section class=screen>
  <p class=screen-tag>Screen 1b &mdash; the same index at volume</p>
  <h2>Does it stay scannable?</h2>
  <p class=path>180 rows &mdash; scroll inside the box</p>
  <div class="note plain">Layout stress test only: the real plug-in names repeated to 180
  rows with synthetic map counts, to judge scanning density. Rows are ~30&nbsp;px &mdash; no
  thumbnails, no cards, one line each, sticky header.</div>
  <div class=scaler><table class=idx>{HEAD}<tbody>{scale_rows}</tbody></table></div>
</section>

<section class=screen id=s2>
  <p class=screen-tag>Screen 2 &mdash; one plug-in</p>
  <h2>FabFilter Pro-C 2 &mdash; {len(PAIR)} mappings</h2>
  <p class=path>/mappings/plugin/fabfilter-pro-c-2</p>
  <div class="note plain">Compact list, one row per map. <b>Comparison is a selection, not the
  default view</b> &mdash; a diff table stops being readable past about four columns, so when a
  popular plug-in carries fifteen maps you tick two or three and diff those.</div>
  <ul class=mrows>{"".join(map_row(a, True) for a in PAIR)}</ul>
  <p class=diffsum style="color:var(--faint)">&#9662; Comparing the 2 selected:</p>
  {diff_table(PAIR)}
</section>

<section class=screen id=s3>
  <p class=screen-tag>Screen 3 &mdash; one mapping</p>
  <h2>{html.escape(D['title'])}</h2>
  <p class=path>/mappings/a17f/bx-console-ssl-4000-g</p>
  <div class="note plain">No faceplate. Measured: the UC1 face is an 860&times;660
  drawing, so inside a phone&rsquo;s ~342&nbsp;px it scales to 0.40 and the 9&nbsp;px
  silkscreen becomes 3.6&nbsp;px &mdash; illegible, and already only 6.6&nbsp;px on a
  980&nbsp;px tablet. The grouped table below reflows to any width, is what a screen
  reader and a search engine read, and its section counts carry the shape of the
  mapping without a picture.</div>
  <div class=detail>
    <div class=dmeta>
      <h3>{html.escape(D['m']['match'][:28])}</h3>
      <p class=sub>{D['surface']} &middot; {D['dom']}</p>
      <button class=btn>Download .rea60map</button>
      <div class=dstrip>{strip(D, cell=6, gap=1.5, sgap=6)}</div>
      <dl><dt>Coverage</dt><dd><b>{D['cov_n']}</b> of {D['cov_d']} ({D['pct']}%)</dd>
        <dt>Author</dt><dd>{PH}</dd><dt>Published</dt><dd>{PH}</dd>
        <dt>Works for me</dt><dd>{PH}</dd><dt>Licence</dt><dd>CC0-1.0</dd>
        <dt>Uses</dt><dd>{", ".join(caps_of(D)) or "&mdash;"}</dd></dl>
    </div>
    <div>{section_table(D)}</div>
  </div>
</section>
</div>""")
print(f"plug-ins: {len(GROUPS)}  maps: {len(ANALYSED)}  wrote {OUT}")
