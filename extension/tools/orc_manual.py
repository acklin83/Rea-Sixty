#!/usr/bin/env python3
"""Build the ORC manual, and refuse to when it no longer matches the code.

The manual is one HTML page. The prose is hand-written in
extension/orc/manual/template.html; every list the code knows better is filled
in from orc_manual_dump (the actions, the factory banks, the STRIP pages with
their ranges and push defaults, the reverb types) or read straight out of the
sources (version, minimum macOS, libusb version, the fine factor).

⇨ THE CHECKS ARE THE POINT (Frank 29.09.2026: "ein schlaues Handbuch"). The
build fails when
  · a UF1 key or encoder in UF1Protocol.h has no section (data-btn / data-enc),
  · a status text ORC can show in its menu bar is missing from the manual,
  · a {{token}} or <!--GEN:...--> marker is left unfilled.
So a new key, a new status or a new setting turns up here, not in a user's
e-mail.

Run:
  python3 extension/tools/orc_manual.py --dump extension/build/orc_manual_dump
  python3 extension/tools/orc_manual.py --dump ... --check     # checks only
Output: extension/build/orc-manual/index.html
"""
import argparse
import html
import json
import re
import subprocess
import sys
from pathlib import Path

EXT = Path(__file__).resolve().parent.parent
TEMPLATE = EXT / "orc" / "manual" / "template.html"
OUT = EXT / "build" / "orc-manual" / "index.html"

def src(rel):
    return (EXT / rel).read_text(encoding="utf-8")


def one(pattern, text, what):
    m = re.search(pattern, text)
    if not m:
        sys.exit(f"orc_manual: cannot find {what} in the sources ({pattern})")
    return m.group(1)


def e(s):
    return html.escape(str(s), quote=True)


# ── facts read from the sources ─────────────────────────────────────────────

def facts():
    cm = src("CMakeLists.txt")
    plist = src("orc/Info.plist.in")
    surface = src("orc/Surface.cpp")
    return {
        "version": one(r"MACOSX_BUNDLE_SHORT_VERSION_STRING\s+([0-9.]+)", cm, "ORC version"),
        "minMacos": one(r"<key>LSMinimumSystemVersion</key>\s*<string>([0-9.]+)</string>",
                        plist, "LSMinimumSystemVersion"),
        "libusbVersion": one(r"set\(ORC_LIBUSB_VERSION\s+([0-9.]+)\)", cm, "libusb version"),
        "fineFactor": one(r"constexpr double kFineFactor\s*=\s*([0-9.]+);", surface, "kFineFactor"),
    }


def statuses():
    """Every text ORC's menu bar can show, from the code that makes it."""
    out = []
    cfg = src("orc/OrcConfig.cpp")
    body = cfg[cfg.index("std::string linkSummary()"):]
    body = body[:body.index("\n}")]
    out += re.findall(r'return "([^"]+)";', body)
    mgr = src("src/RmeManager.cpp")
    for m in re.finditer(r'setStatus\(LinkState::(PortBusy|Silent),\s*"([^"]+)"', mgr):
        out.append(m.group(2).rstrip(" :"))
    # The snprintf'd one ("port %d is in use by another program"): its fixed part.
    if "is in use by another program" not in mgr:
        sys.exit("orc_manual: the port-busy text moved, update statuses()")
    out.append("is in use by another program")
    for m in re.finditer(r'st_\.error\s*=\s*"([^"]+)"', src("orc/Surface.cpp")):
        out.append(m.group(1))
    app = src("orc/OrcApp.mm")
    for m in re.finditer(r'@"UF1   ([a-z ]+)', app):
        out.append(m.group(1).split(",")[0].split(":")[0].strip())
    return sorted(set(s for s in out if s))


def surface_ids():
    """Every key and encoder the UF1 reports (UF1Protocol.h)."""
    p = src("src/UF1Protocol.h")
    btn = p[p.index("namespace btn {"):]
    btn = btn[:btn.index("\n}")]
    enc = p[p.index("namespace enc {"):]
    enc = enc[:enc.index("\n}")]
    return (sorted(set(re.findall(r"\b(k\w+)\s*=", btn))),
            sorted(set(re.findall(r"\b(k\w+)\s*=", enc))))


# ── generated blocks ─────────────────────────────────────────────────────────

def gen_actions(d):
    rows = []
    for a in sorted(d["actions"], key=lambda a: a["title"].lower()):
        kind = "switch" if a["switch"] else "press"
        rows.append(f"<tr><td>{e(a['title'])}</td><td><code>{e(a['label'])}</code></td>"
                    f"<td>{kind}</td><td>{e(a['description'])}</td></tr>")
    return ("<table class=\"data\"><thead><tr><th>Action</th><th>Key label</th>"
            "<th>Kind</th><th>What it does</th></tr></thead><tbody>"
            + "".join(rows) + "</tbody></table>")


def gen_banks(d):
    titles = {a["id"]: a["title"] for a in d["actions"]}
    out = []
    for b in d["banks"]:
        name = b["name"] or f"Bank {b['number']}"
        if b["kind"] == "snapshots":
            out.append(f"<li><strong>{e(name)}</strong>: TotalMix' eight snapshots, "
                       "keys 1 to 4 on the first half and 5 to 8 on the second.</li>")
            continue
        if b["kind"] == "layouts":
            out.append(f"<li><strong>{e(name)}</strong>: TotalMix' eight layouts, "
                       "keys 1 to 4 on the first half and 5 to 8 on the second.</li>")
            continue
        keys = [k for k in b["keys"] if k["action"] or k["label"]]
        if not keys:
            continue
        cells = ", ".join(
            f"{k['key']} <code>{e(k['label'])}</code> ({e(titles.get(k['action'], k['action']))})"
            for k in keys)
        out.append(f"<li><strong>{e(name)}</strong>: {cells}.</li>")
    return "<ul>" + "".join(out) + "</ul>"


def range_text(p):
    if p["kind"] == "toggle":
        return "on / off"
    names = p["names"]
    if p["kind"] == "list":
        txt = ", ".join(e(n) for n in names)
        if p["namesOut"]:
            txt += " (outputs: " + ", ".join(e(n) for n in p["namesOut"]) + ")"
        return txt
    return f"{e(p['loText'])} to {e(p['hiText'])}"


def gen_strip(d):
    params = {p["id"]: p for p in d["params"]}
    reverb_types = params.get("rev_type", {}).get("names", [])

    def shown_on(p):
        if not p["reverbTypes"]:
            return ""
        t = p["reverbTypes"]
        if len(t) == len(reverb_types):
            return ""
        names = [reverb_types[i] for i in t if i < len(reverb_types)]
        if len(names) > 4:
            others = [n for i, n in enumerate(reverb_types) if i not in t]
            return " <span class=\"note\">(not on " + ", ".join(e(n) for n in others) + ")</span>"
        return " <span class=\"note\">(only on " + ", ".join(e(n) for n in names) + ")</span>"

    rows_word = {"in,pb": "inputs and playbacks", "out": "outputs", "": "every channel that has it",
                 "reverb": "Reverb (FX row)", "echo": "Echo (FX row)"}

    def row(where, p, is_key):
        dflt = "" if is_key or not p["pushDefault"] else e(p["pushDefault"])
        return (f"<tr><td>{where}</td><td><code>{e(p['label'])}</code>{shown_on(p)}</td>"
                f"<td>{range_text(p)}</td><td>{dflt}</td></tr>")

    head = ("<table class=\"data\"><thead><tr><th>{w}</th><th>On the screen</th>"
            "<th>Range</th><th>Push sets</th></tr></thead><tbody>")
    # ⇨ A RUN IS PACKED (RmeStrip::views): adjacent pages with the same non-empty
    # rows share their pots, and what a channel has moves up to fill them. So a
    # run is one table in order, not fixed pot numbers the surface does not keep.
    pages = d["pages"]
    out, i = [], 0
    while i < len(pages):
        j = i + 1
        if pages[i]["rows"]:
            while j < len(pages) and pages[j]["rows"] == pages[i]["rows"]:
                j += 1
        run = pages[i:j]
        names = ", ".join(e(pg["name"]) for pg in run)
        graph = " Shows the EQ graph." if any(pg["graph"] for pg in run) else ""
        anchor = e(run[0]["name"].lower().replace(" ", "-"))
        if len(run) == 1:
            pg = run[0]
            body = [row(f"pot {k + 1}", params[pid], False)
                    for k, pid in enumerate(pg["pots"]) if pid in params]
            body += [row(f"key {k + 1}", params[pid], True)
                     for k, pid in enumerate(pg["keys"]) if pid in params]
            note = f"On {rows_word.get(pg['rows'], e(pg['rows']))}.{graph}"
            table = head.format(w="Where") + "".join(body) + "</tbody></table>"
        else:
            pots = [pid for pg in run for pid in pg["pots"] if pid in params]
            keys = [pid for pg in run for pid in pg["keys"] if pid in params]
            body = [row(str(n + 1), params[pid], False) for n, pid in enumerate(pots)]
            body += [row(f"key {n + 1}", params[pid], True) for n, pid in enumerate(keys)]
            note = (f"On {rows_word.get(run[0]['rows'], e(run[0]['rows']))}.{graph} "
                    "These pages are packed: the controls a channel has fill the four pots "
                    "in this order, and the keys likewise.")
            table = head.format(w="Order") + "".join(body) + "</tbody></table>"
        if body:
            out.append(f"<h4 id=\"page-{anchor}\">{names}</h4><p class=\"note\">{note}</p>{table}")
        i = j
    return "".join(out)


def gen_reverbtypes(d):
    params = {p["id"]: p for p in d["params"]}
    types = params["rev_type"]["names"]
    special = {}
    for pid in ("rev_roomscale", "rev_highcut", "rev_attack", "rev_hold", "rev_release",
                "rev_time", "rev_highdamp"):
        p = params.get(pid)
        if not p:
            continue
        for t in p["reverbTypes"]:
            special.setdefault(t, []).append(p["label"])
    rows = []
    for i, n in enumerate(types):
        rows.append(f"<tr><td>{i + 1}</td><td><code>{e(n)}</code></td>"
                    f"<td>{', '.join(e(x) for x in special.get(i, []))}</td></tr>")
    return ("<table class=\"data\"><thead><tr><th>#</th><th>On the display</th>"
            "<th>Controls of its own</th></tr></thead><tbody>" + "".join(rows)
            + "</tbody></table>")


def gen_pots(d):
    role = {"main": "Main", "mainB": "Main B", "phones1": "Phones 1", "phones2": "Phones 2",
            "phones3": "Phones 3", "phones4": "Phones 4", "talk": "Talkback", "": "nothing"}
    cells = []
    for i, p in enumerate(d["defaults"]["pots"]):
        bank, pot = i // 4 + 1, i % 4 + 1
        cells.append(f"<tr><td>{bank}</td><td>{pot}</td><td>{e(role.get(p['target'], p['target']))}</td></tr>")
    return ("<table class=\"data\"><thead><tr><th>Bank</th><th>Pot</th><th>Out of the box</th>"
            "</tr></thead><tbody>" + "".join(cells) + "</tbody></table>")


# ── build ────────────────────────────────────────────────────────────────────

def build(dump_json, check_only):
    d = json.loads(dump_json)
    f = facts()
    t = TEMPLATE.read_text(encoding="utf-8")

    tokens = {
        "version": f["version"], "minMacos": f["minMacos"], "libusbVersion": f["libusbVersion"],
        "fineFactorText": {"0.25": "a quarter"}.get(f["fineFactor"], f"{f['fineFactor']} times"),
        "host": d["defaults"]["host"], "sendPort": d["defaults"]["sendPort"],
        "recvPort": d["defaults"]["recvPort"], "jogStepDb": d["defaults"]["jogStepDb"],
        "potStepDb": d["defaults"]["potStepDb"], "pushQuietMs": d["pushQuietMs"],
        "rowCount": len(d["rows"]), "actionCount": len(d["actions"]),
    }
    gens = {"actions": gen_actions(d), "banks": gen_banks(d), "strip": gen_strip(d),
            "reverbtypes": gen_reverbtypes(d), "pots": gen_pots(d)}

    out = t
    for k, v in tokens.items():
        out = out.replace("{{" + k + "}}", e(v))
    for k, v in gens.items():
        out = out.replace(f"<!--GEN:{k}-->", v)

    problems = []
    for m in re.findall(r"\{\{(\w+)\}\}", out):
        problems.append(f"unfilled token {{{{{m}}}}}")
    for m in re.findall(r"<!--GEN:(\w+)-->", out):
        problems.append(f"unfilled block GEN:{m}")
    btns, encs = surface_ids()
    for b in btns:
        if f'data-btn="{b}"' not in t and f'data-btn="{b} ' not in t and not re.search(
                rf'data-btn="[^"]*\b{b}\b', t):
            problems.append(f"UF1 key uf1::btn::{b} has no section (data-btn)")
    for c in encs:
        if not re.search(rf'data-enc="[^"]*\b{c}\b', t):
            problems.append(f"UF1 encoder uf1::enc::{c} has no section (data-enc)")
    text = html.unescape(re.sub(r"<[^>]+>", " ", out))
    for s in statuses():
        if s not in text:
            problems.append(f"menu-bar text not in the manual: \"{s}\"")
    if problems:
        print("orc_manual: the manual no longer matches the code:")
        for p in problems:
            print("  -", p)
        return 1
    if check_only:
        print("orc_manual: every key, encoder and menu-bar text has its place")
        return 0
    # Every data table in a scroll wrapper, so a narrow screen scrolls the
    # table and never the page.
    out = re.sub(r'(<table class="data">.*?</table>)', r'<div class="tw">\1</div>', out, flags=re.S)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(out, encoding="utf-8")
    print(f"orc_manual: wrote {OUT}")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", required=True, help="path to the orc_manual_dump binary")
    ap.add_argument("--check", action="store_true", help="check only, write nothing")
    a = ap.parse_args()
    r = subprocess.run([a.dump], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"orc_manual: {a.dump} failed: {r.stderr}")
    sys.exit(build(r.stdout, a.check))


if __name__ == "__main__":
    main()
