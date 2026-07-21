# Mapping Exchange — UI prototype

Throwaway prototype that produced the design in
[`docs/mapping-exchange-plan.md`](../../docs/mapping-exchange-plan.md).
Committed because the next step is building the real thing and this is the
executable version of the argument.

**It is not the shipping path.** It parses `SettingsScreen.cpp` with a regex to
get control geometry, which is exactly the second-source-of-truth the plan warns
against. Read it for the layout decisions and the measurements; do not grow it.

## Run

```bash
python3 extract_vendors.py     # reads REAPER's scan caches -> vendors.json
python3 gen_exchange.py        # -> exchange.html
python3 -m http.server 4340    # then open exchange.html / phone.html
```

Both generated files are gitignored. `vendors.json` especially: it lists every
plug-in vendor installed on whichever machine ran it.

## Files

| File | What it is |
| --- | --- |
| `gen_exchange.py` | The three screens: plug-in index, per-plug-in diff, one mapping. Reads the live `user_plugins.json`, so the content is real. |
| `extract_vendors.py` | Where the vendor comes from in REAPER. Three conventions, one per plug-in format — see its docstring. |
| `uc1_face.py` | Faithful transcription of `drawUc1Face_` to SVG. **The exchange no longer uses it** (see below); kept as the reference if an app-side or desktop-only face view is ever built. |
| `phone.html` | Three screens side by side in 390 px iframes. The harness's own viewport control proved unreliable; this is how mobile was actually verified. |

## The dead ends, so nobody repeats them

**A faceplate render does not survive mobile.** The UC1 face is an 860 × 660
drawing: at a 390 px phone it scales to 0.40 and the 9 px silkscreen becomes
3.6 px. Even a 980 px tablet gives 6.6 px. Four prototypes went into this — from
the hit-test table and from `drawUc1Face_`, with stacked labels, then leader
lines, then labels on the control — and the last one still died on a phone.
What replaced it: the coverage strip (no text, so it scales), the
section-grouped control table, and a domain-scoped coverage number.

**`kUc1Controls[]` is the hit-test table, not the drawing code.** 41 bare
circles and rectangles: no chassis, no panels, no GR meter, no LCD. Rendering it
produces a wiring diagram. The drawing is `drawUc1Face_`
(`SettingsScreen.cpp:2271-2652`), 381 lines, and `uc1_face.py` is its
transcription — only seven distinct primitives, which is why an SVG sink on
`VCanvas` would be a small job if it is ever wanted.

**Do not guess a linkIdx.** 22 is `DYN`, not `C RAT`; 5 is `POL`; 36 is
`S/C L`. Derive the mapping from `kUc1Controls[]` for the map's own domain.

**Do not count UF8 slots by recursing for `vst3Param`.** Nested slot structures
carry their own, which reported 130 bound out of a possible 128. Walk the fixed
three-level shape and assert `n <= d`. The on-disk keys are
`banksByFaderBank` / `stripsByFaderBank`, *not* the `banks` / `strips` of the
C++ struct — the struct names silently read zero and every UF8 row renders
`0/0`.
