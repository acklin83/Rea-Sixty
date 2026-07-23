# UF1 meter operator layer — build + fix session, 2026-07-23

Follows the 2026-07-22 StoerPC capture session (`session-2026-07-22-uf1-meter-
capture.md`). Built the meter operator layer from those captures, HW-verified each
step on the Mac (UF1 back on the Mac, SSL 360 quit, REASIXTY_UF1_TRACE for diffs),
merged `main`, and fixed the instance-routing fallout. Branch `uf1-native-build`,
tip `b45b4ee`.

## Feature A — V-Pot value labels repaint (the headline)
The 2026-07-22 "index 0 makes it repaint" theory (`0634f9e`) **FAILED on HW** — a
capture-grounded but causally-wrong guess. Diagnosed properly by tracing our OWN
0x010e frames (`REASIXTY_UF1_TRACE=1` → `/tmp/reaper_uf1_frames.log`) against SSL's
cap104:
- **name+value params:** our name field was 9, SSL's is 8. The UF1 repaints the
  value at a FIXED offset (1+8=9); our name-9 pushed the value to offset 10 → the
  name showed but the value never re-rendered. Fix: name field 9→8 (name8/value10 =
  19 B, byte-identical to SSL). Fixed Fade/TruePk/etc.
- **enum params shown as the value alone** (Digital Type, Analogue Mode, Analysis
  Source): SSL sends them NAME-ONLY (`idx + value`, like V-Pot1's instance label) —
  the only form the UF1 repaints for those slots. Added a `nameOnly` flag on
  `Uf1MeterVPot`; a name+value frame never repainted them.
Both HW-verified. Commit `739423b`.

## Feature B — paging + arrow LEDs
- Nav arrows (`kArrowLeft 0x24` / `kArrowRight 0x26`) page bidirectionally through
  the 3 V-Pot pages; freed Display soft-key 2 for its real Reset job. `739423b`.
- **Arrow LEDs need the FF38+FF39 PAIR** (`buildLedPrimary`+`buildLedLevel`, level
  `0x11` lit / `0x00` off) — `FF3B` (buildLed) ALONE is inert for them (it lights
  Solo but not the nav arrows). ids `0x0e` ► / `0x0c` ◄, correlated in cap106 by
  arrow-press-IN vs LED-OUT timing. `064a1cb` (FF3B, insufficient) → `66df86b`
  (full triple, lights). ► lit when a next page exists, ◄ when a previous one does.

## Table / scale / Ref
- `kUf1MeterVPots` + page counts rebuilt from the 57-param dump
  (`docs/ssl-native-params/VST3__SSL_Meter_Pro_(SSL).md`) + Frank's HW page walk-
  through: 3 pages/screen, correct params & step counts (Digital Type 3→7, RTA
  PkHold→8, Averaging→5). `1276273`. (The 07-22 "needs a param dump we don't have"
  claim was wrong — the dump was in the repo.)
- Scale selector `0x011a` after the label group: Digital Type NL=07/NL2x=08/Lin=03/
  Lin2x=04/K20=0e/K14=0c/K12=0a, Analogue VU=02/PPM=04. `2ff30b8`.
- Analogue Reference Level was steps=3 (only 0/-18/-36) → continuous (fine). `064a1cb`.

## Merge main → uf1-native-build
`8e1b29e`. 85 commits from main (incl. v0.4.1 + the exchange `server/` backend) into
104 uf1 commits — **merged, never rebased** (branch rule). Zero conflicts, compiles
clean, HW-verified nothing broke (incl. Frank's HUD/FX-Learn sensitivity, which he'd
flagged as a main-side regression that's fine on uf1 — it survived the merge).
Backup ref `backup-pre-merge-2026-07-23`.

## Instance routing (`66df86b`)
V-Pot1 selects a session-wide meter instance (impersonator pin); V-Pot2/3/4 used to
edit the FOCUSED track's meter — so every param hit one fixed instance. Added
`sslcore::currentMeterTrackIndex()` (1-based HostTrackIndex of the pinned instance)
+ `uf1PinnedMeterTrackFx_`; V-Pot2/3/4 edits, the displayed values, and the VU-needle
reference all resolve the pinned instance now. V-Pot1 turn re-emits the value labels
so they follow the selection. Removed dead `g_uf1MeterFxSel`.

## Silent-instance fixes (surfaced by the routing change)
- VU needle: the `cur[0] > -35.9f` gate treated a live-but-silent VuPpm (-36 floor)
  as "not live" → pegging BarPeak fallback. With `setView(1)` the -36 is real →
  use it. `a794254`.
- Overview bars: on the plug-in's silence floor the fallback read `Track_GetPeakInfo`
  on the FOCUSED track, painting its signal on a silent instance. Read the PINNED
  track instead; leave empty while running-but-unresolved. `a794254` + `b45b4ee`.

## Open / next (test 2026-07-24)
- **Auto-mode default state** (before the first V-Pot1 step): may show wrong/empty
  until you step; the auto-pick takes the first live instance, not the active one —
  a preference call. `b45b4ee` deployed, not yet HW-verified.
- Loudness is a 4th screen NOT in `kUf1MeterScreenCycle=3` (needs Meter-Pro
  detection — the impersonator doesn't surface PluginType).
- Bug D: K-mode (K-20/14/12) peak BAR law — `uf1BarByte_` uses one fixed table and
  doesn't read Digital Type; needs a per-K-mode level-sweep capture from the StoerPC.

## Traps paid this session
- **Deploy over a running REAPER corrupts the dylib code signature** → SIGKILL
  "Invalid Page" on next launch (a `cmp` of the deployed file returned rc=137). Fix:
  deploy with REAPER closed, or atomically (`cp tmp && mv tmp dest`). See
  `macos-codesign-after-install-name-tool` memory.
- A capture-grounded fix can still be causally wrong (the index-0 Feature-A attempt).
  When a deploy fails on HW, TRACE our own output and diff — don't re-theorize.
