# Session notes — 2026-07-19 · Mapping Exchange UI

Signed off by Frank. This records how the design got there, because most of it
arrived by being wrong first.

## What was decided

| Question | Answer |
| --- | --- |
| Index lists what? | **Plug-ins**, one row each, map count as a column |
| Comparing maps | **Per-control diff**, and it is a *selection* (2–3 ticked), not the default |
| Faceplate | **None, anywhere** |
| Vendor source | `original_name`, parsed per `fx_type` |
| Vendor facet | Searchable combobox |
| Auth | Passkey primary, magic-link equal fallback, optional recovery email |

## The four wrong turns, and what each one cost

**1. Designed the picture before the product.** The first pass produced a
detailed spec for a detail-page rendering and no exchange around it. The plan
even said "the unit of the exchange is the plug-in" and then laid out a list of
maps. Frank: *"DU MUSST EINE EXCHANGE DESIGNEN … UND KEINEN GANZSEITIGEN
MOCKUP!"*

**2. Designed without looking at prior art.** Researching reWASD and the Elgato
Marketplace took twenty minutes and immediately refuted the per-row render.
Doing it first would have skipped steps 1 and 3 entirely.

**3. Recorded the research finding, then ignored it.** The notes said "no render
on the index rows" two commits before the faceplate was still being polished.
Writing a finding down is not the same as acting on it.

**4. Drew from the wrong table.** Every early prototype rendered
`kUc1Controls[]` — the *hit-test* table, 41 bare shapes. The drawing code is
`drawUc1Face_`, 381 lines with chassis, panels, GR meter, LCD. The plan had said
to export that from the first draft; the prototypes took the easier table
because it was flat and parseable.

## The measurements that settled things

- Faceplate on mobile: 390 px viewport → face at ~342 px → 9 px silkscreen
  becomes **3.6 px**. Even a 980 px tablet gives 6.6 px. Not recoverable.
- Vendor from REAPER's scan caches, 1817 plug-ins: VST 99 %, AU 100 %,
  CLAP 100 %, JSFX 8 % — **88 % overall**, **82 distinct vendors on one machine**.
- Coverage denominators are domain-scoped: CS /33, BC /8, UF8 /128.
- Post-fix: all screens at 390 px, no horizontal overflow, min font 10.6 px.

## Bugs the prototyping exposed

- **UF8 coverage read 0/0** — the on-disk keys are `banksByFaderBank` /
  `stripsByFaderBank`, not the `banks` / `strips` of the C++ struct.
- **`SSL Delta Control 16` reported 130 of 128** — recursive `vst3Param`
  counting descends into nested slot structures. Correct is 64. An impossible
  number is what exposed it; a map under the cap would have shipped a plausible
  wrong figure. `assert n <= d`.
- **linkIdx guessed rather than derived** — 22 is `DYN` not `C RAT`, 5 is `POL`,
  36 is `S/C L`. Derive from `kUc1Controls[]`.
- **Claim written before checking the data** — a prototype caption asserted the
  two Pro-C 2 maps differed by "one covers the compressor, the other adds the
  sidechain". False; they differ on one MIX binding.

## Open

- **UF8 preview shape.** 128 V-Pot slots want an 8×8 grid per bank, not a face.
  Recommended in the plan, not prototyped.
- **JSFX vendor** at 8 % — those uploads need a user-supplied vendor.
- The HTML prototype lives in the session scratchpad and is **not** in the repo.
  Every finding it produced is in `docs/mapping-exchange-plan.md`; the code
  itself parses C++ with a regex and should not become a second source of truth.
