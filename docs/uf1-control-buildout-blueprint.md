# UF1 Control-Surface Build-Out Blueprint

Branch `uf1-native-build`. Refs are `extension/src/…`. Status: **WIRED** / **INERT** (routed, no-op) / **UNBOUND** (has ButtonId, no action). Mirror the WORKING UF8/UC1 code; follow the p188 manual tables. Do not re-derive from memory.

## 0. The gap in one sentence
Channel view: the 4 main V-Pots `uf1::enc::kVpot1..kVpot4` (0x01–0x04) are **INERT** at `main.cpp:11616-11622` (`if (g_uf1MeterView.load()) applyUf1MeterVpot_(...)` — no `else`). Fix = twin the Meter-view V-Pot operator (`applyUf1MeterVpot_`, `main.cpp:11399`) for the channel strip, driven by the p188 tables.

## 1. Input inventory (key anchors)
- Enc ids `uf1::enc` (`UF1Protocol.h:53-58`): `kVpotAboveFader=0x00`, `kVpot1..4=0x01-0x04`, `kChannel=0x05`, `kJog=0x06`.
- `onUf1Event` worker (`main.cpp:15794`): EncoderRotate → `queueInput({Uf1Encoder,id,delta})` (`:15820`); Button else → `bindings::dispatch(fromUf1DeviceId(id))` (`:15881`); MODE 0x20 toggles `g_uf1MeterView` (`:15834`); arrows 0x24/0x26 page in meter view (`:15863`).
- `drainInputQueue` Uf1Encoder (`main.cpp:11595-11626`): kChannel→`applySelectRelative_`; kJog→`applyPlayheadNudge_`; kVpotAboveFader→`applyUf1AboveFaderVpot_`(pan); **kVpot1..4→meter only, channel INERT (`:11620`)**.
- Transport/Solo/Cut/Sel fire in drain `:11562-11593`; LEDs paint `uf1PaintChannel_:18153-18193`.

## 2. p188 tables — `docs/uf1-vpot-softkey-tables.md` (authoritative)
4 plugin types (CS2/4K B/4K E/360 Link) × 8 pages; each page = 4 soft-key + 4 V-Pot descriptors. ←/→ (nav 0x24/0x26) page 1–8 wrap; Quick-Key-2 (`Uf1SecKey2` 0x35) = Normal↔Fine; page shared by soft-keys AND V-Pots. Params matched **by name** (doc: "mirrors `uf1ParamByName_`"). Common V-Pot shape: P1 Width/OutTrim/CompMix, P2 InTrim/HPF/LPF, P3 LF, P4 LMF, P5 HMF, P6 HF, P7 Comp, P8 Gate. Open points: V-Pot 0x00 role unspecified (keep Pan); exact param-name strings need HW confirm; non-SSL FX = out of scope for 3b.

## 3. Drive the SAME params the screen shows
- `uf1PaintChannel_` (`main.cpp:17832`): V-Pot slots `0x010e` painted with placeholders `:18202-18220` (idx2/idx3 "blank until V-pot mapping lands"); `0x010f` bars placeholder `:18266`.
- FX resolution to REUSE: `resolveActiveFx_()` (`main.cpp:6771`) → `uf1FindEqFx_()` (`:17571`) → params by name `uf1ParamByName_()` (`:17596`); focused track `uf1FocusedTrack_()` (`:11089`). Same as `uf1PaintEqGraph_:17706-17716`.
- Plugin-type from FX name: `lookupPluginMapByName` (`PluginMap.cpp:393`, registry `:378` CS2/4KG/4KE/4KB/360Link).

## 4. Mirror targets
- **V-Pot write (UF8):** `main.cpp:12882-12954` — `kVpotBoost=1.25` (`:12889`), fine `reasixty_uf8KnobScale(vpotFineActive_())` (`:12903`), notch-hold `uf8::applyNotchHold` (`:12934`), `TrackFX_SetParamNormalized(tr,fx,vst3Param,next)` (`:12949`).
- **Twin to copy (UF1 meter):** `applyUf1MeterVpot_` (`main.cpp:11399-11471`) — per-slot accum `/kChannelEncoderScale`, enum-vs-cont step (`:11463`), write `:11468`, re-emit labels. Tables `Uf1MeterVPot/Uf1MeterPage` (`:11130-11148`), lookup `uf1MeterVPotPage_` (`:11218`).
- **Soft-key banks/bindings:** `fromUf1DeviceId` (`Bindings.cpp:264-316`), factory seed (`:765-799`), `ssl_softkey` builtin (`main.cpp:32422`), `softkey_bank_select` (`:32358`).
- **Settings surface:** `SettingsScreen::drawBindings` (`:5615`), device tab bar UF8/UC1 only (`:5672-5700`, NO UF1 tab/`drawUf1Vector`); UF1 already in device list (`:605`).
- **Verify fires:** fader→vol `CSurf_OnVolumeChange` (`main.cpp:18302`), above-fader pan `CSurf_OnPanChange` (`:11118`), transport (`:11562`), solo/cut/sel (`:11572`). Trace env `REASIXTY_UF1_TRACE`.

## 5. Build order
1. **D — verify baseline fires** (transport/solo/cut/sel/fader-vol/pan). Trace `REASIXTY_UF1_TRACE`.
2. **A — 3b V-Pots → channel-strip (HEADLINE):** un-inert `:11620`; add `applyUf1ChannelVpot_` (twin of `applyUf1MeterVpot_`); encode p188 tables keyed by plugin type; resolve on-screen FX via `resolveActiveFx_`/`uf1FindEqFx_`, params via `uf1ParamByName_`; ←/→ page (`g_uf1CsPage`) + QK2 Fine in `onUf1Event:15858`; replace placeholder screen writes `:18202-18269`; V-Pot pushes 0x09-0x0C → reset param default.
3. **B — soft-keys:** UF1 tab + `drawUf1Vector` in `SettingsScreen:5672`; factory-seed soft-keys `Bindings.cpp:765`; a `uf1_softkey` builtin.
4. **C — settings sub-page** for UF1 (above-fader mode, focus model).
