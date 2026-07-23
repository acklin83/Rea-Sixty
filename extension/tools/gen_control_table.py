#!/usr/bin/env python3
"""Generate server/data/uc1-controls.json from the C++ source of truth.

WHY THIS EXISTS
---------------
The mapping exchange has to answer two questions about every uploaded map:

  1. Coverage — "23 of 33 Channel Strip controls" — which needs to know which
     controls physically exist on the UC1 face, per domain.
  2. The per-control diff — which needs a stable linkIdx -> human name.

Both answers live in C++. The throwaway prototype in analysis/mapping-exchange
regex-parsed SettingsScreen.cpp at render time, which its own README calls out
as the second-source-of-truth the plan warns against. This script is the fix:
the parse happens ONCE, here, and the committed JSON is what the server reads.
CI re-runs it and fails on drift (see check_control_table.py), so a control
rename cannot silently blank part of the exchange.

THE THREE SOURCES, AND WHY EACH ONE
-----------------------------------
* kSsl360LinkSlots[]   (PluginMap.cpp) — the CS linkIdx authority. The header
  says it outright: linkIdx 0..46 ARE SSL 360 Link's own VST3 param indices.
* kSsl360LinkBcSlots[] (PluginMap.cpp) — the same for Bus Comp.
* kUc1Controls[]       (SettingsScreen.cpp) — the hit-test table. Used ONLY to
  decide which slots are physically on the face (-> the coverage denominator).
  NOT used for names: it carries 4-char overlay labels ("HFGN"), not the
  readable names the exchange shows.

TRAPS THIS ENCODES
------------------
* linkIdx IS NAMESPACED PER DOMAIN. CS linkIdx 1 is FaderLevel; BC linkIdx 1 is
  Threshold. Keying the table by a bare linkIdx silently mislabels every BC map.
  This is the same class of error as "22 is DYN, not C RAT" — one level up.
* A slot that is NOT on the face is still legitimately bindable (Fader, Pan,
  Width, Out Trim, Bypass, QA1-6, SAT, GRP). Those get on_face=false and must
  render in an "also mapped" list rather than vanishing, or a thorough map
  looks sparse.
* The denominator is domain-scoped: 41 face controls = 33 CS + 8 BC. Scoring a
  complete BC map out of 41 shows good work as 20%.
"""

import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PLUGIN_MAP = REPO / "extension/src/PluginMap.cpp"
SETTINGS = REPO / "extension/src/SettingsScreen.cpp"
OUT = REPO / "server/data/uc1-controls.json"

# A LinkSlot row: { linkIdx, "id", "name", "legend", vst3Param, inverted[, deflt] }
# linkIdx may be a literal or an ext::Name constant; we only take literals here
# because every ext:: slot is off-face by construction (they are extension
# synthetics with no SSL 360 Link param, hence no UC1 control).
SLOT_RE = re.compile(
    r'\{\s*(\d+)\s*,\s*"([^"]+)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*'
    r'(-?\d+)\s*,\s*(true|false)'
)

# A Uc1Control row: { Kind, linkIdx, Domain, cx, cy, r, w, h, cap, "label" }
CONTROL_RE = re.compile(
    r"\{\s*Uc1Control::(\w+)\s*,\s*(\d+)\s*,\s*uf8::Domain::(\w+)\s*,"
    r"[^}]*?\"([^\"]*)\"\s*\}",
    re.S,
)


def table_body(text: str, decl: str) -> str:
    """Return the brace body of `constexpr LinkSlot <decl>[] = { ... };`."""
    m = re.search(r"constexpr \w+ " + re.escape(decl) + r"\[\] = \{(.*?)\n\};", text, re.S)
    if not m:
        sys.exit(f"FATAL: table {decl}[] not found — was it renamed?")
    return m.group(1)


def section_of(slot_id: str, domain: str) -> str:
    """Group for the coverage strip. Derived from the canonical id, not the
    4-char face label — the id is stable and unambiguous ('HighEqGain'),
    the label is display text that has already been re-laid-out twice."""
    if domain == "BusComp":
        return "Bus Comp"
    if slot_id in ("LowPassFreq", "HighPassFreq"):
        return "Filters"
    for prefix, name in (
        ("HighMidEq", "HMF"),
        ("HighEq", "HF"),
        ("LowMidEq", "LMF"),
        ("LowEq", "LF"),
    ):
        if slot_id.startswith(prefix):
            return name
    if slot_id in ("EqType", "EqIn"):
        return "EQ"
    if slot_id.startswith("Comp") or slot_id == "DynamicsIn":
        return "Comp"
    if slot_id.startswith("Gate"):
        return "Gate"
    return "Channel"


def main() -> None:
    pm = PLUGIN_MAP.read_text()
    ss = SETTINGS.read_text()

    # --- names, per domain ------------------------------------------------
    slots = {}
    for domain, decl in (("ChannelStrip", "kSsl360LinkSlots"),
                         ("BusComp", "kSsl360LinkBcSlots")):
        rows = SLOT_RE.findall(table_body(pm, decl))
        if not rows:
            sys.exit(f"FATAL: {decl}[] parsed to zero rows — the row shape changed.")
        slots[domain] = {
            int(link): {"id": sid, "name": name, "legend": legend}
            for link, sid, name, legend, _vst3, _inv in rows
        }

    # --- which of them are physically on the face -------------------------
    controls = CONTROL_RE.findall(table_body(ss, "kUc1Controls"))
    if len(controls) != 41:
        sys.exit(f"FATAL: kUc1Controls[] gave {len(controls)} rows, expected 41. "
                 "The face table changed — update the denominator assertions.")
    on_face = {}
    for _kind, link, domain, label in controls:
        on_face.setdefault(domain, {})[int(link)] = label

    # 41 = 33 ChannelStrip + 8 BusComp, counted 2026-07-19. Assert rather than
    # trust: the denominator is what makes a complete map read as complete.
    for domain, expected in (("ChannelStrip", 33), ("BusComp", 8)):
        got = len(on_face.get(domain, {}))
        if got != expected:
            sys.exit(f"FATAL: {domain} has {got} face controls, expected {expected}.")

    out = {
        "_generated_by": "extension/tools/gen_control_table.py — do not hand-edit",
        "_sources": ["extension/src/PluginMap.cpp", "extension/src/SettingsScreen.cpp"],
        "_note": ("linkIdx is namespaced PER DOMAIN: CS 1 is FaderLevel, "
                  "BC 1 is Threshold. Always key by (domain, linkIdx)."),
        "domains": {},
    }
    for domain in ("ChannelStrip", "BusComp"):
        face = on_face.get(domain, {})
        entries = {}
        for link, meta in sorted(slots[domain].items()):
            entries[str(link)] = {
                **meta,
                "section": section_of(meta["id"], domain),
                "on_face": link in face,
                "face_label": face.get(link),
            }
        # A face control with no canonical slot would render as an unnamed cell.
        missing = sorted(set(face) - set(slots[domain]))
        if missing:
            sys.exit(f"FATAL: {domain} face controls {missing} have no LinkSlot entry.")
        out["domains"][domain] = {"denominator": len(face), "slots": entries}

    rendered = json.dumps(out, indent=2, ensure_ascii=False) + "\n"

    # --check is the CI guard the plan asks for: a control rename that is not
    # regenerated would otherwise blank part of the exchange with a green build
    # — the same silent-failure class as `astro build` exiting 0 on a render
    # error. Fail loudly instead.
    if "--check" in sys.argv:
        if not OUT.exists():
            sys.exit(f"FAIL: {OUT.relative_to(REPO)} is missing. Run this script.")
        if OUT.read_text() != rendered:
            sys.exit(f"FAIL: {OUT.relative_to(REPO)} is stale — the C++ tables "
                     "changed. Re-run: python3 extension/tools/gen_control_table.py")
        print(f"ok: {OUT.relative_to(REPO)} matches the C++ tables")
        return

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(rendered)
    cs, bc = out["domains"]["ChannelStrip"], out["domains"]["BusComp"]
    print(f"wrote {OUT.relative_to(REPO)}")
    print(f"  ChannelStrip: {len(cs['slots'])} slots, {cs['denominator']} on face")
    print(f"  BusComp:      {len(bc['slots'])} slots, {bc['denominator']} on face")


if __name__ == "__main__":
    main()
