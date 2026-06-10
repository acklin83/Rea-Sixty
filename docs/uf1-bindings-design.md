# UF1 — Settings → Bindings → UF1 Page: design

Status: DESIGN DRAFT 2026-06-10. Grounded in SSL UF1 User Guide Rev4.0 (hardware
layout p16-19, REAPER tutorial p134-137), our protocol decode (`docs/protocol-notes-uf1.md`
+ cap55–63), the existing Bindings architecture (`extension/src/Bindings.h`), and two
user forum posts (Frank, master-section use). NOT yet approved/implemented.

## Spine (the answer to "max flexibility + min complexity")

1. **Reuse the EXACT UF8/UC1 bindings model — do not invent a parallel system.**
   `ButtonId` is one global `enum class : uint16_t` (Bindings.h:33), the per-Layer
   bindings map (`std::unordered_map<ButtonId, Binding>`, Bindings.h:351) is
   surface-agnostic, and it was already widened for UC1 (`Uc1Encoder1/2/Push`). The
   enum's own comment: *"Phase B/C will widen the catalogue … without breaking older
   config files."* → UF1 controls become **new ButtonId enumerators** + a
   device-id→ButtonId map in the UF1 input handler. Modifier matrix (Plain/Shift/Cmd/Ctrl),
   ActionType (Reaper/Keyboard/Builtin/Midi), action chains, LED overrides, footswitches —
   all inherited for free. This is a **config feature, not a subsystem refactor.**

2. **Ship SSL-matching DEFAULTS so the user never has to open the editor unless they want to.**
   The REAPER-default profile (UG p137) is the baseline: V-Pots=Pan (push=centre),
   fader=track volume, Solo/Cut/Sel, Flip swaps V-Pot↔fader, Master=fader drives master.
   Everything rebindable underneath.

3. **The "UF1 Page" in Settings is just a UI filter** showing the UF1-range ButtonIds,
   same editor widgets as UF8. Users who know the UF8 page already know this one.

## Foundational decision: focus is PER-ELEMENT, not a global mode

Frank today runs **fader = Master AND V-pots = pinned-to-tracks-1-4 simultaneously**,
and explicitly wishes "if the vpots and the master could be integrated into banking
*separately*." SSL's FADER SEL RANGE (`1-16 = 2x UF8s + UF1`) literally banks the UF1
fader at the end of the UF8 chain — the native "13-channel mixer." So the fader and the
V-pot cluster must be **independent focus axes**, not one global selector:

- **Fader focus:** `{ Follow selected | Master | Pin to track N | Bank-extension slot }`
- **V-pot cluster focus:** `{ Follow selected | Pin to tracks N..N+3 | Bank-extension slots }`

This single split is simultaneously (a) Frank's current setup, (b) his stated wish, and
(c) the mechanism for the 13-ch mixer + "9th fader" + "pin to track". It IS the
flexibility spine — a global mode would *remove* flexibility he already has.

## Wish → mechanism, tiered by cost

### Tier 1 — cheap, mostly existing infra (lead with these; most daily wishes land here)
| Wish (forum) | Control | Mechanism |
|---|---|---|
| Soft keys → keystrokes (Shift/TAB/Space/ALT) | Lg-screen soft-keys 0x19-0x1C (+ 10 pages), sm-screen soft-key 0x18 | ⚠️ `ActionType::Keyboard` enum exists but executor is a STUB (Bindings.cpp:2430 "Phase D — needs platform key-event injection"). Needs CGEventPost (mac) / SendInput (Win). NOT free — bounded new work. |
| Secondary transport customised (scroll-to-top, ripple, loop pts) | sec-transport keys 0x30/31/34/35 | bindable buttons → Reaper actions |
| Little ←→ arrows (OFF/READ) for time selection | 0x24/0x26 | bindable buttons |
| Zoom-section reassignable | NAV cross 0x28-0x2C | bindable buttons (default = zoom, like SSL) |
| Jog: playhead/bar, modifier-finer, FLIP=auto-vert, OPT=auto-horiz | Jog 0x06 | bindable **encoder** w/ modifier matrix (mirror `dispatchEncoder` Plain/Shift/Cmd) |
| Jog more: nudge items, time-sel, automation-move increments | Jog 0x06 + Scrub 0x27 toggles a 2nd jog binding-set (SSL Factory/User scrub) | encoder modifier matrix + scrub-layer |
| Footswitches → action/keystroke | FS1/FS2 | bindable (already a ButtonId pattern) |
| Channel-encoder modes (incl. mouse-wheel/Focus) | 0x05 rot + 0x0D push | encoder-mode cycle: Bank / Nudge / Focus(mousewheel-emul) / … |

### Tier 2 — output/painting, partially built (verify against code, don't overstate)
| Wish | Status |
|---|---|
| Display timecode on large LCD | We paint header; timecode frame decoded (cap83). Wire to large-LCD. |
| FX graphics to UF1 on encoder FX-cycle | Channel-strip + EQ graph render (plugin-mode). V-pot bars + graphic area still **TBD/not integrated** per code. Hook into instance/FX-cycle. |
| Big LCD = meter plugin / other plugins on screen | SSL Meter mode painted; arbitrary-plugin surface = **new subsystem (big), later.** |

### Tier 3 — new subsystem (bigger, later)
- **Per-element banking extension** (the 13-ch mixer / UF1 = channel 9-12 + master).
  Needs the focus-axis model above + participation in the global bank offset.
- **Screen-as-arbitrary-plugin-surface.**

## Proposed Settings → Bindings → UF1 Page structure
- **Top:** two focus selectors — *Fader focus* and *V-pot cluster focus* (the foundational axes).
- **Below:** flat bindable-control list grouped like the hardware:
  Transport · Secondary transport (+Shift=automation) · Channel strip (Solo/Cut/Sel/Flip/Master)
  · V-pots (+push) · Soft-keys (sm + 4×10 lg pages) · NAV cross · Bank/Page/5-8/360-Layer
  · Channel encoder (mode list) · Jog (+Scrub layer) · Footswitches.
- Each row = the same binding editor as UF8 (modifier matrix, action picker, LED override, chains).
- **Defaults preloaded** to SSL's REAPER profile so an untouched UF1 behaves natively.

## DECISIONS (Frank, 2026-06-10)
1. ✅ **Per-element focus model CONFIRMED** — fader-focus and V-pot-cluster-focus are
   independent axes. This is the foundation everything hangs off.
2. ✅ **All four Tier-1 starters greenlit:** jog-encoder+modifiers, soft-keys/buttons
   bindable, pin-to-track focus, timecode on large LCD.
3. Bank-extension (13-ch mixer) stays Tier 3 / later.

## Phase 1 — validated integration points (code-confirmed 2026-06-10)
- `ButtonId` = one global `enum class : uint16_t` (Bindings.h:33); UC1 already appended →
  append UF1 entries at the end. UF8+UF1 SHARE the per-Layer bindings map, so UF1 MUST get
  DISTINCT ButtonIds (device ids 0x18/0x1D collide between surfaces but route through different
  device-id maps → different ButtonIds → no clash).
- `dispatch(ButtonId,pressed)` is designed to run ON THE WORKER THREAD (Bindings.cpp:294 comment).
  REAPER-API work is deferred by the builtins themselves (`__reaper_action__` → `queueInput
  ({MainAction,…})`, main.cpp:17801). So UF1 calls `dispatch` INLINE in `onUf1Event`, exactly like
  UF8 at main.cpp:8050. No bespoke queue path.
- Mirror points: `kNames[]` (Bindings.cpp:60-132, JSON names), `fromUf8DeviceId` (Bindings.cpp:149 →
  add `fromUf1DeviceId`), factory `seed` (Bindings.cpp:380+ `L1[ButtonId::X]=mkBuiltin(...)`).
- ⚠️ `ActionType::Keyboard` is a STUB (Bindings.cpp:2430) — soft-keys-as-keystrokes needs key injection.
- No focused-track solo/mute/select builtin exists → add `uf1_solo_focused`/`uf1_mute_focused`/
  `uf1_select_focused` wrapping the existing `PendingInput::Uf1SoloToggle/Uf1MuteToggle/Uf1SelectFocused`
  (and `uf1_transport` wrapping `Uf1Transport`) so factory defaults reproduce shipped behaviour exactly.

### Phase 1a — first commit scope ("UF1 buttons through Bindings")
1. Bindings.h: append UF1 button ButtonIds (~33: pushes, soft-keys, strip, NAV block, NAV cross,
   secondary transport, transport/Flip/Master). 2. Bindings.cpp: kNames + `fromUf1DeviceId`.
3. main.cpp: new builtins uf1_transport/solo/mute/select-focused (wrap existing queueInput kinds).
4. Bindings.cpp seed: factory-default the transport+Solo/Cut/Sel UF1 buttons to those builtins;
   ship NAV/arrows/display-soft/secondary-transport/pushes UNBOUND (like UF8 ships Nav/Nudge unbound).
5. main.cpp onUf1Event: replace the hardcoded Button switch with `fromUf1DeviceId(id)`→`dispatch`;
   keep MODE (0x20) as the firmware-local view toggle (return None from the map). Encoders = Phase 2.

## Build order (derived from decisions)
- **Phase 1 — Bindings foundation (PREREQUISITE for buttons + jog).** Add UF1 ButtonId
  enumerators + device-id→ButtonId map in the UF1 input handler; route press/encoder
  events through `Bindings::dispatch` / `dispatchEncoder` instead of the current hardcoded
  queueInput. Preload SSL-REAPER defaults. Unblocks soft-keys-as-keystrokes, secondary
  transport, NAV cross, arrows, footswitches.
- **Phase 2 — Jog encoder + modifier matrix** (rides on Phase 1's dispatchEncoder path)
  + Scrub-key second binding-set.
- **Phase 3 — Pin-to-track focus** (per-element data model: fader-focus + vpot-focus).
- **Phase 4 — Timecode on large LCD** (independent output; quick win, can interleave).
- Settings → Bindings → UF1 Page UI grows alongside Phases 1–3.
