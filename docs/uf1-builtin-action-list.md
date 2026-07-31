# UF1 built-in action list — curated proposal

Status: **5-feature batch BUILT + DEPLOYED 2026-07-30 21:36, UNCOMMITTED** on `uf1-native-build`,
awaiting Frank's HW test. Built: (1) Encoder mode LOCAL (`g_uf1EncoderMode`, hold-MODE+turn picker,
desktop banner feedback); (2) SENDS mode = 4th MODE slot (4 V-Pots = send levels, "5-8" banks, FLIP =
send-on-fader, unified UIVol index); (3) Strip Mode LOCAL (`g_uf1StripMode`); (4) NAV cross → Zoom
(seed+backfill); (5) UF1 dynamic soft-key banks (FxBank/ParamGroups/TrackColours, DAW mode, LED
state-only — display-soft-key colour protocol undecoded). Open items tracked in
`HANDOFF-uf1-control-buildout-2026-07-29` memory. Original proposal preserved below.
Source of truth for the existing built-ins: `Bindings.cpp builtinCategory()` (20 categories),
`main.cpp` (EncoderMode enum, send/receive routing), verified in source this session.
UF1 hardware IDs: `uf1-decode-plan` + `HANDOFF-uf1-control-buildout-2026-07-29`.

---

## Why UF1 needs its own list

UF8/UC1 are **8-strip** surfaces: 8 faders, 8 V-Pots, 8 sets of Solo/Cut/Sel. Almost every
"…across the bank" and "…strip 0..7" built-in assumes 8 strips.

**UF1 is a ONE-strip surface** with a very different control set:

| UF1 control | ID | today's default |
|---|---|---|
| CHANNEL encoder (notched) + push | 0x05 / 0x0D | rotate = track-select · Shift = instance-cycle · push = toggle GUI |
| Jog wheel | 0x06 | playhead nudge |
| 4 display V-Pots + pushes | 0x01–0x04 / 0x09–0x0C | plugin params (Plugin) · 4 track vols (DAW) |
| Above-fader V-Pot + push | 0x00 / 0x08 | pan · (push unclaimed) |
| Motor fader | — | track volume (or CS Out-Gain in Strip Mode) |
| FLIP / MASTER | 0x38 / 0x39 | fader↔V-Pot (local) · strip follows master |
| 4 display soft-keys + channel soft-key | 0x19–0x1C / 0x18 | CS section toggles (Plugin) · user bank slots (DAW) |
| "5-8" | 0x22 | DAW: jump to next 4-track group |
| MODE | 0x20 | hold → Plugin / DAW / Meter menu (SK4 free) |
| ◄ ► arrows | 0x24 / 0x26 | soft-key / CS pages |
| BANK ◄ ► | 0x21 / 0x23 | ±8 track select |
| 360 / SCRUB | 0x25 / 0x27 | 360 = time-format cycle · **SCRUB = INERT** |
| NAV cross (U/L/C/R/D) | 0x28–0x2C | **INERT** |
| SHIFT | 0x36 | shared modifier |
| Secondary transport (L/R/Cycle/Click/1/2) | 0x30–0x35 | 1/2 = quick keys · +Shift = automation |
| Primary transport (RWD/FFW/Stop/Play/Rec) | 0x3A–0x3E | locked native REAPER |

So the mapping rule for every existing action:

- **Global / focused-track** built-in → **keep as-is**, just make it bindable to a UF1 control.
- **"×8 across the bank"** built-in → **drop** (UF1 has 1 strip) or **re-window to 4** (sends).
- **"strip 0..7 param"** built-in → **drop** (no per-strip row).
- Transport / Solo / Cut / Sel → stay **locked native**, never a UF1 built-in
  (see `feedback-dont-reinvent-reaper-builtins`).

---

## Local vs Global — the state-ownership rule (decided 2026-07-30)

The whole "should this be shared with UF8" question splits cleanly on ONE line — and the
built-in dispatch forces our hand: `invokeBuiltin(name, param)` carries **no surface context**,
so a built-in cannot know which surface fired it.

1. **REAPER / project state → inherently GLOBAL. No decision.** One project; both surfaces
   mirror it: transport / **Rec** / Cycle / Click, Solo / Cut / Sel / track-select, automation
   mode, Markers, **Zoom**, Selection Sets, Favourites (they change the real FX), Plug-in
   show/bypass/move, Param Groups, Tracks-Arm.
2. **Surface view / interaction state → LOCAL per surface** (so UF8 and UF1 can differ at the
   same time): encoder mode, FLIP (already `g_uf1Flip`), **SSL Strip Mode** (now local — see
   below), sub-mode (Plugin/DAW/Meter/Sends), page, bank window, send-routing, Fine, Master.

Decisions (Frank 2026-07-30): **Encoder mode = LOCAL**; **SSL Strip Mode = LOCAL** (like FLIP,
was shared).

## The two big reuses you asked about

### A) Encoder Modes → the single CHANNEL encoder — **LOCAL `g_uf1EncoderMode`**

UF8 has ONE conceptual channel-encoder with **16 modes** switched via the `encoder_*`
built-ins (they all set the atomic `g_encoderMode` — read only by UF8's nav encoder; UC1 never
touches it). UF1 already does 2 of them, but **hardcoded** (select + Shift=instance).

**Decision:** UF1 gets its **own** `g_uf1EncoderMode` atomic (NOT the shared `g_encoderMode`),
so UF8 can sit in FX-Cycle while UF1 is in Instance — simultaneously. The UF1 CHANNEL encoder
(0x05) reads `g_uf1EncoderMode`; default `ChSelect` (unchanged feel).

**Switched via a UF1-native picker — NO new built-ins** (Frank's choice, keeps the action
picker clean): the mode list lives on the existing **MODE-hold menu** (add an "Encoder"
sub-page) or a dedicated soft-key page, which writes `g_uf1EncoderMode` directly. The shared
`encoder_*` built-ins stay UF8-only. Shift+encoder = Instance stays an always-on quick layer
regardless of mode.

| Encoder mode | built-in | UF1 use |
|---|---|---|
| Channel Select | `encoder_nav` | default — nav tracks |
| Instance Cycle | `encoder_instance` | already the Shift layer |
| FX Cycle / FX Scroll (all) | `encoder_fx_cycle` / `encoder_fx_scroll_all` | walk FX on focused / across tracks |
| Instance Scroll (all) | `encoder_instance_scroll_all` | cross-track instance walk |
| FX Move | `encoder_fx_move` | reorder active FX in the chain |
| CS / BC / Favourite Cycle | `encoder_cs_cycle` / `encoder_bc_cycle` / `encoder_fav_cycle` | swap the strip's Channel-Strip / Bus-Comp favourite |
| Selset Cycle | `encoder_selset_cycle` | step selection sets |
| Markers | `encoder_markers` | prev / next marker |
| Bank by 1 | `encoder_bank_by_1` | nudge selection by 1 |
| Last Param | `encoder_last_param` | ride the last-touched param |
| Nudge / Mousewheel | `encoder_nudge` / `encoder_focus` | edit-cursor / mousewheel emulation |

**Verdict: KEEP ALL 16 modes — reuse the logic, LOCAL state.** New work = a `g_uf1EncoderMode`
atomic + the UF1 encoder dispatch reads it + the MODE-menu/soft-key picker writes it. No new
built-ins. Keep Shift+encoder = Instance as the always-on quick layer regardless of mode.

### B) Sends / Receives → 4 V-Pots + "5-8" + FLIP  ← your idea, formalised

UF8 sends model (all in `main.cpp`, HW-verified, actually rendered — not just state):
- `send_this` / `recv_this` — the focused track's sends/receives on the 8 strips,
  paged by `g_sendBankOffset` (Bank ◄► = ±8, bank-by-1 = ±1).
- `send_all_N` / `recv_all_N` — send/receive N across all 8 tracks in the bank.
- The binding's `param` = **Flip flag**: 0 → faders, 1 → V-Pots (that's the send-on-fader vs
  send-on-vpot split, already in the engine).

Map onto UF1:

- `send_all_N` / `recv_all_N` (×8 across tracks) → **DROP.** One strip, no bank to spread across.
- `send_this` / `recv_this` → **ADAPT into a UF1 "SENDS" mode** using the exact machinery you
  named:
  - **4 V-Pots = sends 1–4** of the focused track (level). Reuses `g_sendVpotThisTrack`,
    windowed to 4 instead of 8.
  - **"5-8" (0x22) banks the send window** — sends 5–8, 9–12… via `g_sendBankOffset`
    (same paging as UF8's Bank ◄►, just the UF1 "5-8" button drives it; identical to how
    "5-8" already banks the DAW 4-track group).
  - **FLIP = send-on-fader** — puts the focused send on the motor fader (the engine's Flip
    flag: fader ⇄ V-Pot). Exactly "flip über fader für send auf fader".
  - **V-Pot push = reset send to unity** (or mute-send — your call).
  - **Receives** = the same mode under **Shift**, or on soft-key 4.
  - **◄ ► arrows** page the send window too (mirror "5-8"), or leave to "5-8" only.

**Home for SENDS = the 4th MODE-menu slot** (Plugin / DAW / Meter / **SENDS**). You already
left SK4 free and said "dann hätten wir sogar 4". That's the natural place — no new button
needed. The soft-key labels in Sends mode show the 4 send destination names.

---

### C) Dynamic soft-key banks → UF1 DAW-mode banks

UF8/UC1 have a **dynamic soft-key bank** mechanic: a bank flagged with `DynamicBankKind`
(Bindings.h:366) ignores its static slots and computes its keys LIVE from the focused track via
`dynamicBankSlot_` / `applyDynBankReq_` — surface-agnostic REAPER logic.

**Current kinds (verified in source 2026-07-30): `FxBank`, `ParamGroups`, `TrackColours`.**
The **`Sends` kind was DROPPED** (commit `d411c28`) — send LEVELS belong on the V-Pots (the
SENDS mode in section B), not on soft-key toggles. Do not revive it. (The stale
`dynamic-softkey-banks` memory still lists Sends — it's gone.)

- **FxBank** — focused track's FX chain, colour-coded LEDs (CS yellow / BC red / UF8-mapped
  blue / plain white), bright=enabled / dim=bypassed / off=empty; paged; gestures configurable
  (`g_fxBankOp`: Focus/Float/Bypass/FxSolo/Offline/Move…).
- **ParamGroups** — 8 parameter groups; Push = toggle membership, Long = toggle group active.
- **TrackColours** — 8 configurable RGB; Push = apply, Long = clear.

**Port to UF1 (clean — the engine is already shared):**
1. Add a `DynamicBankKind dynamic` field to the UF1 soft-bank config (`uf1SoftBanks[10][4]`,
   Bindings.h) — mirrors `UserQuickSubBank.dynamic`. Each of the 10 DAW-mode banks becomes
   selectable: static slots **or** FX chain / Param Groups / Track Colours.
2. **4-wide window** instead of 8: `dynamicBankSlot_(kind, focusTrack, pageBase + s)` for
   s=0..3. **◄ ► pages** the FX bank (they already page DAW soft-key banks). Needs a page-by-4
   granularity next to the existing `page*8`.
3. Gestures **Push / +Shift / Long** port (SHIFT is wired, long-press works). Colour LEDs
   already flow via the DAW-mode FF38-GRB path, so FX colour-coding comes through 1:1.
4. Threading via UF1's own `queueInput`→drain instead of `g_dynBankReq` — same pattern.

Home: **DAW mode** (where UF1's user banks live). Plugin-mode soft-keys stay CS sections.

## Full pass — every category

Legend: **KEEP** = reuse the global built-in, bind to a UF1 control · **ADAPT** = re-window /
re-home for one strip · **DROP** = 8-strip-only or superseded · **NATIVE** = stays locked,
not a built-in.

### Favourites — `switch_fav_N`, `copy_fav_N`, `fav_cycle`, `switch_cs_*`, `cs_cycle`, `switch_bc_*`, `bc_cycle`, `*_copy_own_toggle`
**KEEP (all global / focused-domain).** `fav_cycle` / `cs_cycle` / `bc_cycle` → encoder modes
(above). `switch_fav_N` / `switch_cs_N` → bind to soft-keys or NAV cross. Copy/own toggles →
soft-key or Settings. Fully surface-agnostic; no changes.

### Cycle Actions — `instance_cycle`, `fx_cycle`, `fx_scroll_all`, `instance_scroll_all`, `fx_move`, `instance_next/prev`, `bc_track_scroll(_select)`, `select_relative`, `track_scroll`, `track_select_range`, `temp_selset_scroll`, `playhead_nudge`, `mouse_scroll`
**KEEP.** These are exactly the encoder/jog targets. `select_relative` = encoder default,
`playhead_nudge` = jog default, `instance_cycle` = Shift+encoder default. The rest = encoder
modes (A) or bind to NAV cross / soft-keys. `track_select_range` (Shift+encoder range) already
a UF8 pattern — offer on UF1 too.

### Selection Modes — `selection_mode_norm`
**DROP / N-A.** This is the UF8 8-V-Pot "what does a V-Pot push select" layer. UF1's 4 V-Pots
are driven by MODE (Plugin params / sends / DAW faders), so there's no per-V-Pot sel-mode row.
The MODE menu covers the intent.

### Encoder Modes — `encoder_*` (16)
**KEEP ALL — the headline reuse (section A).**

### Hardware Modes — `flip`, `pan_force`, `mixer_toggle`, `home`, `folder_mode`, `show_only_selected`, `ssl_strip_mode_*`, `uf8_plugin_mode_*`, `uc1_outgain_fader_toggle`, `learn_hud_toggle`, `touch_to_learn_toggle`, `focused_panel_toggle`, `mode_banner_toggle`, `tcp_follows_selection_toggle`, `surface_mirror_tcp/mcp`, `marker_overlay_*`, `uf1_time_display_step`, `restart`
- **KEEP (global):** `mixer_toggle`, `home`, `folder_mode`, `show_only_selected`,
  `learn_hud_toggle`, `touch_to_learn_toggle`, `focused_panel_toggle`, `mode_banner_toggle`,
  `tcp_follows_selection_toggle`, `surface_mirror_tcp/mcp`, `marker_overlay_*`, `pan_force`,
  `restart` — bind to soft-keys / channel soft-key / NAV cross.
- **Already wired natively:** `ssl_strip_mode_toggle` = UF1 PLUG-IN key; `uf1_time_display_step`
  = UF1 360 key; `flip` — UF1 uses its **own local flip** (`g_uf1Flip`, fader↔pan / sends).
  Decision: keep the UF1-local FLIP (it needs the Sends-mode meaning), don't wire the UF8 `flip`
  built-in to the FLIP key.
- **Strip Mode → make LOCAL (Frank 2026-07-30).** The UF1 PLUG-IN key currently toggles the
  **shared** `g_pluginFaderMode` (UF8+UF1 move together). Change: give UF1 its own
  `g_uf1StripMode`, so UF8 can be in Strip Mode while UF1 shows normal volume faders —
  consistent with FLIP. `csFaderForTrack` stays the fader source; only the gate becomes
  per-surface.
- **DROP:** `uf8_plugin_mode_*` (UF8-only), `uc1_outgain_fader_toggle` (UC1-only).

### Plug-in — `show_focused_plugin_gui`, `show_fx_chain`, `close_all_fx_guis`, `quick_learn`, `quick_learn_track`, `plugin_bypass`, `plugin_offline`, `plugin_move_up/down`, `plugin_preset_cycle/next/prev`
**KEEP (all focused-FX, global).** `show_focused_plugin_gui` already = channel-encoder push.
Rest → soft-keys / NAV cross / secondary-transport keys. `plugin_preset_*` and
`plugin_move_*` pair nicely with the free NAV cross.

### Layer — `layer_select_1/2/3`
**KEEP (low priority).** UF1 shares the per-layer bindings map, so these switch the UF1 binding
layer. Distinct from the FX-learn Ctrl/Opt modifier layers (those are host-keyboard, already
wired). Bind only if you want hardware layer-switching.

### Soft-Key Bank — `softkey_bank_select`
**DROP / N-A.** UF8 SSL page-bank selector. UF1 has its **own** soft-key banking: CS section
pages in Plugin mode (◄►) + the 10×4 user soft-key banks in DAW mode. UF1's system supersedes it.

### SSL — `domain_cs`, `domain_bc`, `ssl_softkey`, `ssl_bank_*`
- **KEEP:** `domain_cs` / `domain_bc` (latch the focused SSL domain — global, useful for the
  encoder favourite/CS/BC cycles).
- **DROP:** `ssl_softkey`, `ssl_bank_vpot`, `ssl_bank_1..5` — these take `param 0..7` = *which
  of 8 strips*; meaningless on one strip. UF1's display soft-keys already drive the CS section
  toggles directly.

### Bank / Page — `bank_left/right`, `page_left/right`, `bank_by_1_left/right`
**KEEP (bindable) — but note UF1 already has hardcoded equivalents:** BANK ◄► = ±8 select,
◄► arrows = pages. Re-expose these built-ins so the user can re-bind, and so `bank_*` also
drives **send-window paging** (the engine's `applySendBankStep_` already piggybacks on
`bank_left/right`) — which is what makes "5-8" / Bank page the Sends window in (B).

### Automation — `auto_trim/read/touch/write/latch/off` (+ `_global`, `_prv`) + `automation_zero_all`
**KEEP ALL (global).** The mockup already reserves **secondary transport + SHIFT = automation**.
Perfect home: RWD/FFW/Stop/Play/Rec-row under Shift → the six automation modes.

### Zoom — `zoom_center`, `zoom_up/down/left/right`
**KEEP — and this is the obvious default for the INERT NAV cross.** NAV cross Up/Left/Centre/
Right/Down (0x28–0x2C) maps 1:1 onto `zoom_up/left/center/right/down`. Strong candidate for the
NAV-cross factory default (alternative: track/time navigation — offer both, zoom as default).

### Sends / Receives — `send_this`, `recv_this`, `send_all_N`, `recv_all_N`
**ADAPT `send_this`/`recv_this` → UF1 SENDS mode (section B); DROP `send_all_N`/`recv_all_N`.**

### Selection Sets — `selset_cycle/recall/save`, `temp_selset_*`
**KEEP (global).** `selset_cycle` = encoder mode; recall/save/temp_* → soft-keys. All work as-is.

### Parameter Groups — `param_group_remove_all`, `multi_select_as_temp_group_toggle`
**KEEP (global).** Bind to a soft-key if wanted; otherwise Settings-only.

### Tracks — `selection_clear_all`, `tracks_arm_all`, `automation_zero_all`
**KEEP (global).** Bind to soft-keys / NAV cross.

### Master — `master_pin_strip1`, `master_pin_strip8`
**DROP.** These pin the master track to UF8 strip 1/8. UF1 has one strip and a dedicated
**MASTER button** (already shows the master track) — that fully covers the intent.

### Brightness — `brightness_leds/lcds/both_up/down`
**DROP for now — BLOCKED.** UF1 has no decoded global brightness frame (only per-LED
2-level bright/dim; screen intensity undecoded). Needs a capture before it can drive UF1.
Partial option: scale the 3 button LEDs with the LED step. Flag: capture required.

### Modifiers — `mod_shift`, `mod_cmd`, `mod_ctrl`
**KEEP.** UF1 SHIFT (0x36) already feeds the shared modifier; binding SHIFT → `mod_shift`
gives full parity (incl. the double-click latch). `mod_cmd` / `mod_ctrl` bindable to spare keys
if you want hardware Cmd/Ctrl.

### FX Param — `fx_param_*`
**KEEP (global, dynamic).** Bindable like anywhere else.

---

## Suggested UF1 factory default map (the free controls)

| Control | Proposed default |
|---|---|
| NAV cross (U/L/C/R/D) | **Zoom** (`zoom_up/left/center/right/down`) |
| SCRUB | Toggle scrub / jog-scrub (native) — or free for binding |
| Channel soft-key (0x18) | `home` (one-press exit of all modes) |
| Secondary transport +Shift | Automation modes (`auto_*`) |
| "5-8" in SENDS mode | Bank the send window ±4 |
| FLIP in SENDS mode | Send-on-fader |
| MODE menu SK4 | **SENDS mode** |
| SHIFT (0x36) | `mod_shift` (shared, with latch) |

## Net result

- **Reused wholesale (global built-ins, just bind them):** ~5 encoder-mode + Favourites +
  Cycle + Plug-in + Automation + Zoom + Selection-Sets + Tracks + Modifiers + most Hardware
  Modes + FX Param.
- **Adapted for UF1:** the sends model → 4-V-Pot / "5-8" / FLIP SENDS mode.
- **Dropped (8-strip / per-strip only):** `send_all_N`, `recv_all_N`, `ssl_softkey`,
  `ssl_bank_*`, `softkey_bank_select`, `master_pin_*`, `uf8_plugin_mode_*`,
  `uc1_outgain_fader_toggle`, `selection_mode_*`.
- **Blocked:** Brightness (needs a UF1 capture).
- **Stays native (never a UF1 built-in):** transport, Solo, Cut, Sel.
