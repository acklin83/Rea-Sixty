# UF1 control — HW-test bug-fix pass (2026-07-29)

Follow-up to the UF1 control build-out (V-Pots / soft-keys / timecode). Frank
HW-tested that build ("aber schonmal sauber") and reported 4 bugs. All four
worked here; built clean (`cmake --build extension/build --target reaper_uf8`),
deployed to UserPlugins 18:13, **UNCOMMITTED** on `uf1-native-build`, awaiting
Frank's next HW test. Nothing outside the UF1 code changed.

## What was done

1. **HQ MODE + A/B soft-keys now toggle** — reuse, not research. These are not
   VST3 params (confirmed absent in `docs/ssl-native-params/` for CS2/4K B/4K E/
   360 Link) but were already solved on the UF8 via chunk-replace
   (`uf8::togglePluginHQ` / `togglePluginAB`, `PluginChunkPatch.cpp`). Wired into
   the UF1 soft-key path:
   - `Uf1CsSoftKey` gained `enum Uf1CsSkAct{Param,HQ,AB}` + an `act` field.
   - The 3 page-1 HQ/AB entries (CS2/4K B/4K E) are tagged HQ/AB.
   - `applyUf1ChannelSoftKey_` routes HQ/AB to the toggles on the resolved
     strip's track (walks every SSL plug-in on it, exactly like UF8).

2. **Stuck rectangle under soft-key 1 → soft-key LEDs parked.** The painter drove
   speculative LED ids 0x01–0x04 (derived `led_id = btn_id − 0x18`, never captured
   for the display soft keys); guessing LED bytes is banned and one id left a
   rectangle stuck lit. Removed the LED sends (labels stay). If the rectangle
   persists on HW it is a display-element artifact (not our LED) and needs a
   capture. Re-enable soft-key LEDs (with on-state) once captured.

3. **FLIP wired per manual (p16: "assign the current V-Pot parameter to the
   fader").** New UF1-local `g_uf1Flip` (not the UF8/UC1 `g_flip`), session-only.
   Flip key 0x38 press toggles it. ON: fader rides Pan (`uf1PosToPan_` /
   `uf1PanToPos_`, linear), above-fader V-Pot rides Volume (dB nudge). OFF:
   normal (fader=Volume, V-Pot=Pan). No fader protocol/init/motor-frame change —
   only which value the follow/writeback targets. No Flip LED (bytes uncaptured).

4. **SEL — already manual-correct, no code change.** Dispatch fires (identical
   path to Solo/Cut). `SetOnlyTrackSelected` + `followSelectedInMixer` scrolls
   TCP+MCP into view. "Does nothing" is inherent on a single-strip UF1 (the
   focused track is usually already selected+visible). Per Frank, richer SEL
   options come later in Settings.

## Follow-up batch (same session): BC on UF1 + V-Pot push→default (deployed 18:58)

Frank asked for V-Pot push→default and "was ist mit BC auf dem UF1?".

**Manual check first (Frank: "wie im handbuch"):** the SSL UF1 User Guide Rev4.0 does NOT
describe a standalone Bus Compressor — its 360°-plugin list (p178-179) is only Meter / CS2 /
4K B / 4K E / 360 Link, and "Bus"/"Compressor" as a standalone appears nowhere in the 8815-line
manual. The channel strips' built-in compressor section IS covered (and already on the UF1 CS
pages). Frank chose to add the standalone SSL Bus Compressor 2 anyway, as a Rea-Sixty extra.

5. **Standalone SSL Bus Compressor 2 on the UF1 channel V-Pots (type 4).** `uf1CsPluginType_`
   now accepts `Domain::BusComp` → 4 for "BC 2" (L-BC not mapped). Auto-follows a **focused/active**
   BC (resolveActiveFx_ / GetFocusedFX2) — an unfocused BC on a bus is not grabbed. Extended
   `kUf1CsVPots`/`kUf1CsSoftKeys` to `[5][8]`; Rea-Sixty-designed 2-page layout (no manual ref):
   P1 Threshold/Ratio/Attack/Release, P2 Makeup/Dry-Wet Mix/S-C HPF; soft-keys EXT S/C, OVERSAMP,
   A/B. Param names verified vs the BC2 dump ("Dry/Wet" not "Mix"). Per-type page count
   `kUf1CsTypePageCount[5]={8,8,8,8,2}` + `g_uf1CsActiveType` atomic (painter writes, worker
   ◄►-wraps within it).

6. **V-Pot push→default (0x09-0x0C).** Channel-view intercept → `Uf1CsVpotPush` →
   `applyUf1ChannelVpotPush_`. Reuses the existing default (not reinvented): `uf1CsDefaultNorm_`
   bridges name-resolved vst3Param → PluginMap slot → `UserLinkSlot.defaultNorm` (self-mapped)
   else `LinkSlot.deflt` else 0.5. Above-fader (0x08) + channel (0x0D) pushes NOT claimed.
   **Caveat:** the `deflt` tables are mostly nullopt → push lands on 0.5 (0 dB for bipolar EQ
   gain/trim; midpoint elsewhere). Real defaults = populate `deflt` in the LinkSlot tables later.

## Open / deferred
- HW-verify all six changes.
- BC only reachable when the BC instance is focused/active (auto-follow). L-BC (360-Link BC) not mapped.
- BC 2-page V-Pot layout is a Rea-Sixty design — tune with Frank if the grouping isn't right.
- push→default lands on 0.5 where `deflt` isn't populated — populate the LinkSlot tables for real defaults (helps UF8/UC1 too).
- SEL options in Settings; Flip LED + persistence once bytes captured; re-enable soft-key LEDs after a capture.
- Not committed — UF1 stays on `uf1-native-build` until it fully works (Frank's rule).
