# Device-scope audit (forum item 4.5, step 4) — 2026-08-19

Question asked: **which actions silently do nothing when the device they need
isn't there?** That is the fault class behind forum item 2.5, where a UF1-only
user pressed FX Cycle and nothing happened, ever, because six entry points
opened with a bare `if (!g_uc1_surface) return;` (fixed in `30689ab`).

## Method

Mechanical census over `extension/src/main.cpp` and `Bindings.cpp`, then every
candidate read by hand. Nothing below is inferred from a name alone.

1. Every early return on a device pointer (`g_dev`, `g_uc1_dev`,
   `g_uc1_surface`, `g_uf1_dev`), with its enclosing function.
2. Every `registerBuiltin` site, bucketed: writes state / calls REAPER directly
   (device-independent by construction) vs. delegates to helpers.
3. For the delegating ones, a depth-3 path search to any device-gated helper.
4. For every global a builtin writes, the full reader set, classified by which
   surface's code reads it. Catches the subtler case: the atomic is stored, but
   only one device's painter ever reads it.
5. Dead-state check: any builtin-written global with no reader at all.

## Numbers

| | |
|---|---|
| `registerBuiltin` sites | 189 |
| helpers with a hard device gate | 24 |
| builtins writing state / calling REAPER directly | 113 |
| builtins that delegate | 74 |
| ...of those, reaching a gated helper within 3 hops | 16 |
| distinct globals written by builtins | 61 |
| ...with no reader | 0 |

## Result: the action layer is clean

All 24 gated helpers are emit / paint / serial functions. No device, nothing to
draw — the gate is correct.

Of the 16 delegating paths, **12 run through `activeFocusTrack_`**
(`main.cpp:8073`), which is not a gate but a preference: the UC1's focus wins
when there is a UC1, otherwise the UF1's own focus, otherwise selection. That is
exactly what `30689ab` installed. The other four are UF8 LED feedback attached to
UF8 features (`plugin_move_up/down`, `page_left/right`). None is a fault.

The single-consumer check flagged 8 globals whose readers are all UF1 code — all
8 belong to builtins named `uf1_*`, so they are correctly scoped and correctly
labelled. No UF8-only or UC1-only orphan exists.

## Two findings, both fixed in `51883f9`

**1. `pan_force` was a no-op on a UF1-only rig and still sat in the UF1 picker.**
It writes `g_forcePan`, read only by the 8-strip path (`computeStripCurrentPb_`,
`slotForStrip`, `stickyGloballyEnabled_`, `commitDebouncedTouchReleases`,
`pushUf8GlobalLeds`). The code already says so, in the note above
`stickyUf1AboveEnabled_`: *"all UF8/UC1 state that does not apply to the UF1."*

**2. The device mask could not be right by construction.**
`builtinDeviceMask()` (`Bindings.cpp`) decides from the id's prefix plus four
hand-maintained exception blocks. Anything device-specific without a
`uf8_`/`uc1_`/`uf1_` prefix falls through to "universal" in silence. `flip` and
`encoder_*` were caught by hand; `pan_force` was missed, and the next unprefixed
builtin would be missed the same way.

Fix: `BuiltinDescriptor::deviceMask`, set at the registration site and preferred
by `builtinShownForId`. `0` means "not stated" and keeps the old table as the
fallback for every id already shipped. `pan_force` declares `0b011` and is the
first user. The table now carries a "do not grow the exception list" note.

## One gap, not a bug

FX Cycle, Instance Cycle and Favourites cycling are device-neutral and work
everywhere, but their carousel readout is UC1-only: `showCycleCarousel_`
(`main.cpp:7793`) and `showFavCarousel_` (`main.cpp:7829`) both open with
`if (!g_uc1_surface || !tr) return;`, and no UF1 equivalent exists anywhere.

It is not silent on the UF1: `resolveActiveFx_` is un-gated and feeds
`uf1ActiveFxShortName_` (`main.cpp:25568`) into the CS-TYPE cell, so the landed
name does appear. What is missing is the prev/next context the UC1 shows on
three fields.

**Not verified:** whether Favourites cycling shows the UF1 anything at all. That
needs the device.

## What this means for 4.5

Step 4 produced two fixes and no fault list. The weight of item 4.5 therefore
sits in steps 1 to 3 — filtering the Settings panes by which devices the user
actually owns, gated on "ever seen on this machine" rather than "connected right
now", so unplugging a UF8 for one session doesn't make its settings vanish.
