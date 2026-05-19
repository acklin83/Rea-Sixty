# Plan — Nav Mode Phase 2.8b + 2.8c

> Author: Claude Code, 2026-05-19
> Status: ready for review
> Builds on: Phase 2.8a (`b79b644..50197eb`), ROADMAP.md Phase 2.8b/c

## Goals

**2.8b** — UC1 Encoder 2 carousel: a `[prev | current | next]` browser
on UC1's central LCD that scrolls one marker/region per detent. Push
commits a transport jump. UF8's 8-window follows the cursor.

**2.8c** — Settings → Modes refactor into `BeginTabBar` with 6 sub-tabs;
new **NAV** sub-tab exposes the configurable options (default view,
region-press behaviour, auto-follow, UC1 take-over, etc).

Combined plan because 2.8c defines the configurable knobs whose
defaults 2.8b must match.

## Open design decisions (settled here)

### 1. Cursor model — encoder cursor and playhead cursor share state, gated by a latch

Today (2.8a): single `cursorIdx_` set only by auto-follow from
playhead. User cannot move it manually.

2.8b adds Encoder 2 rotation = user-driven cursor movement. Same conflict
the page-slide latch solved earlier:

- Auto-follow advances cursor as playhead moves.
- User rotates encoder; cursor moves.
- Next tick auto-follow snaps cursor back to playhead → user's scroll
  is overwritten every 33 ms.

**Solution:** mirror the `wasInFilter_` latch pattern with a new
`cursorPinned_` flag. Encoder rotation sets `cursorPinned_ = true`.
Auto-follow's cursor update is gated on `!cursorPinned_`. The pin
clears when:

- Playhead reaches the pinned cursor's item (auto-follow caught up
  organically).
- User presses Encoder 2 (commit — pin clears so post-jump auto-follow
  resumes).
- User exits Nav Mode / changes view / drills.

`cursorPinned_` is internal — not exposed in settings. Behaviour is
"manual scroll temporarily detaches cursor from playhead; commit or
catch-up re-attaches."

Auto-follow's **page-slide gate** (Phase 2.8a) keeps its current
behaviour: page only slides on cursor change. With the cursor-pin
gate above, cursor doesn't change from auto-follow while pinned → page
stays where the user's encoder browsing put it.

### 2. UC1 LCD takeover — reuse the existing 3-up carousel frame builder

`UC1Protocol.h` already exports `buildTrackNameTripleLarge(prev, curr,
next)` (47 B frame in zone 0x04, 3×14 char slots), used by
`bc_track_scroll` and `showInstanceCarousel`. Marker names fit
naturally in 14 chars.

The existing `showInstanceCarousel(prev, curr, next, header)` method
has the right shape but auto-fades after 3 s. Nav Mode wants a
persistent carousel while overlay-active.

**Approach:**
- New method `UC1Surface::showNavCarousel(prev, curr, next, contextLine, colorRgb)`.
- Sets a `navCarouselActive_` flag (separate from `bcScrollOverlayActive_`
  / `instanceCarouselActive_` so timeout logic doesn't expire it).
- Calls `buildCentralMode(CentralMode::Main, 0x02)` + `buildLcdHeader(...)`
  + the triple frame, same as showInstanceCarousel.
- Color bar via existing colour-bar zone (whatever
  `setFocusedTrack` writes today — reuse).

Cache: cache the last (prev, curr, next, header) string set so we
only send the frame when it changes. Pattern mirrors `g_lastSlotLabel`
in main.cpp's pushNavOverlayDecorations.

Exit: `hideNavCarousel()` clears the flag and restores the underlying
zones via `invalidateCache()` + a refresh.

### 3. UC1 mode arbitration — Nav wins, restored on exit

UC1 has 5 modes today (`Main` / `ExtFuncs` / `Routing` / `Presets` /
`Transport`). Plan: when Nav Mode activates, force `Uc1Mode::Main` and
remember the previous mode. On Nav exit, restore.

If the user navigates UC1 menus (e.g. presses the Routing button) while
Nav Mode is active, the Nav carousel **disengages** (Nav surface stays
on UF8 only; UC1 returns to its mode). Re-entering Nav Mode toggle
brings it back. Simpler than juggling layered LCDs.

Implementation:
- Add `g_uc1NavLcdActive` atomic bool. Set true on Nav activation,
  false on Nav exit OR when user enters a UC1 menu mode.
- `UC1Surface::poll()` pushes the Nav carousel each tick only while
  this flag is true.
- Menu-mode entry handlers (Routing/Presets/etc.) clear the flag.

### 4. Encoder 2 rotation intercept — same pattern as 2.8a's `PendingInput::EncoderRotation`

`UC1Surface::onKnobEvent` (around line 870) currently routes Encoder 2
through `bindings::dispatchEncoder(Uc1Encoder2, step)`. When overlay
is active, **short-circuit** before the bindings dispatch:

```cpp
if (uf8::nav::Overlay::instance().active()) {
    // Step the encoder cursor by `step` items (1 per detent).
    uf8::nav::Overlay::instance().moveCursor(step);
    return;
}
```

`moveCursor(delta)` is new on Overlay:
- Sets `cursorPinned_ = true`.
- `cursorIdx_ += delta`, clamped to `[0, items_.size()-1]`.
- Recomputes pageOffset to keep cursor visible (existing logic from
  tickAutoFollow, factored out).
- Sets `g_navOverlayDirty.store(true)` so UF8 + UC1 both repaint.

### 5. Encoder 2 push intercept — plain/shift/long-press

Push events arrive at `UC1Surface::onButtonEvent` (line ~1322). When
overlay active, intercept before the bindings dispatch:

| Gesture | View=Regions | View=MarkersInRegion | View=MarkersAll | RegionsOnly lock | MarkersOnly lock |
|---|---|---|---|---|---|
| Plain push | `GoToRegion` + drill | `SetEditCurPos` | `SetEditCurPos` | `GoToRegion` (no drill) | `SetEditCurPos` |
| Shift+push | `GoToRegion` (no drill) | `backToRegions` then jump | no-op | no-op | no-op |
| Long-press | `backToRegions` (no-op, already there) | `backToRegions` | `backToRegions` | no-op | no-op |

Long-press detection: timer started on press, fired on release if
held > 500 ms. Same threshold as the bindings layer uses for its
own long-press. Implementation:
- Press event: `g_uc1Enc2PressTimeMs = now()`. `g_uc1Enc2Pressed = true`.
- Release event: `held = now() - g_uc1Enc2PressTimeMs`. If
  `held > 500 ms` → long action. Else short action.
- Shift modifier (`Modifier::Shift` via bindings::currentModifierSnapshot)
  read at release time.

The "Plain push: SetEditCurPos in MarkersInRegion + MarkersAll" branches
match Phase 2.8a's marker handling (smooth-seek-aware via SetEditCurPos
with seekplay=true to avoid the GoToRegion queue conflict).

Defaults above are hard-coded for 2.8b; 2.8c adds Settings radio
buttons to let the user override.

### 6. UF8 follows UC1 — single shared cursorIdx

Phase 2.8a already has a `cursorIdx_` field driven by auto-follow.
The new Encoder 2 rotation writes to the same field. UF8's render path
(`pushNavOverlayDecorations`) already uses `cursorIdx_` to decide which
strip's top-soft-key LED is bright.

The "follow" happens naturally: cursor moves → page-slide gate
re-arms → UF8's pageOffset_ snaps to the new cursor's page. UF8 LED
brightens on the new cursor strip. No extra code.

UF8 → UC1 direction: a UF8 top-soft-key press today sets `cursorIdx_`
indirectly via `drillIntoRegion` (which resets cursor to 0). For 2.8b
we add: also explicitly set `cursorIdx_ = idx` on the pressed item so
the UC1 carousel snaps to that item. Already the right behaviour;
just needs the explicit write.

### 7. Settings → Modes BeginTabBar refactor

`SettingsScreen.cpp:7667` `drawModes` currently runs 5 inline sections
in one scrollable child. Refactor into:

```cpp
ImGui_BeginTabBar(ctx, "modes_tabbar", flags);
ImGui_BeginTabItem(ctx, "AUTO", ...);     // existing AUTO block
ImGui_EndTabItem(ctx);
ImGui_BeginTabItem(ctx, "FX / Cycle", ...);
ImGui_EndTabItem(ctx);
ImGui_BeginTabItem(ctx, "Plug-in GUI", ...);
ImGui_EndTabItem(ctx);
ImGui_BeginTabItem(ctx, "Faders", ...);
ImGui_EndTabItem(ctx);
ImGui_BeginTabItem(ctx, "REC", ...);
ImGui_EndTabItem(ctx);
ImGui_BeginTabItem(ctx, "NAV", ...);
ImGui_EndTabItem(ctx);
ImGui_EndTabBar(ctx);
```

Each existing section's inner code moves verbatim into its tab item.
No semantic edits — pure layout. The NAV section we added in
commit `7eea2c0` is the seed for the NAV tab; rest of NAV's content
arrives below.

Persist last-active sub-tab in ExtState (`modes_subtab`) so the user
returns to where they left off.

### 8. NAV sub-tab content (Phase 2.8c)

Five sections, top to bottom:

**Activation** (read-only mirror)
- Three lines: `Drill toggle → <bound button or "(unbound)">`,
  `Markers-only toggle → ...`, `Regions-only toggle → ...`.
- Each line gets a "Bind…" button that jumps to Settings → Bindings
  pre-filtered to the toggle's builtin name.
- No setting state here — it's a navigation shortcut, the actual
  bindings live in bindings.json.

**View defaults**
- Radio: *Default view on toggle-enter* — `Regions` / `Markers in
  current region` / `Markers (all)` / `Last used`.
  - Stored in `ExtState "rea_sixty" / "nav_default_view"`.
- Radio: *Region-press behaviour* — `Jump + Drill (default)` /
  `Jump only` / `Drill only`.
  - Stored in `nav_region_press`.
  - Consumed in the NavJumpStrip drain (drillIntoRegion + GoToRegion
    fire conditionally).

**Auto-Follow** (existing checkbox)
- Already shipped at `7eea2c0`. Stays.
- Add: tooltip clarifying "follows playhead during transport, edit
  cursor when stopped".

**UF8 strip display**
- Radio: *Lower-row format* — `Off (V-Pot value)` / `Index (R03)` /
  `Timecode (MM:SS)`. Default = Off (current behaviour after the
  2.8a revision).
  - When non-Off, overlay's pushNavOverlayDecorations writes to lower
    row (currently skipped per Frank's "V-Pot field unangetastet").
  - Stored in `nav_lower_row`.
- Checkbox: *Pagination hints on strip 0 / 7 lower-row* — only meaningful
  when Lower-row != Off.
- Radio: *Color-bar source* — `REAPER marker colour` / `Force palette
  grey`. Default = REAPER.

**UC1 Encoder 2**
- Checkbox: *Take over while Nav Mode active* — default ON.
  - When off, Encoder 2 rotation falls through to `bc_track_scroll`
    (or whatever the user bound) even during Nav Mode. UC1 LCD shows
    its normal content; only UF8 reflects Nav Mode.
  - Stored in `nav_uc1_takeover`.
- Radio: *Carousel scope* — `Mirror UF8 view (default)` / `Always
  Regions` / `Always Markers (all)` / `Always Markers in current UF8
  region`.
  - When non-mirror, UC1's items_ list is computed independently from
    UF8's. Both surfaces show their own cursor.
  - Implementation note: defer this until the simpler "mirror" case
    works. Stub the radio in 2.8c with only "Mirror" enabled, the
    others greyed-out as "coming in 2.8d".
- Radio: *Push action* — `Jump + Drill (default)` / `Jump only` /
  `Drill only`.
- Radio: *Shift+Push action* — `Drill / Back / Toggle View`. Default
  `Drill`.
- Radio: *Long-press push action* — `Back (default)` / `Add marker at
  playhead` / `Disabled`.

## Architecture: shared cursor + view-lock interactions

The cursor latch already exists (`wasInFilter_`). The new pin works
in parallel:

```cpp
class Overlay {
    bool wasInFilter_  = false;  // 2.8a: gates auto-roll
    bool cursorPinned_ = false;  // 2.8b: gates auto-follow cursor write

    void moveCursor(int delta);  // sets pinned, moves, slides page
    void commitCursor();         // jump to cursorIdx_ item, clear pin
};
```

`tickAutoFollow` reads both:
- `wasInFilter_` gates the region auto-roll branch
- `cursorPinned_` gates the cursor scan branch
- Pin auto-clears when playhead reaches the pinned item's pos (or
  passes a threshold near it)

View-lock interactions:
- MarkersOnly: encoder browses MarkersAll. Push = SetEditCurPos. No
  drill. Shift+push, long-press = no-op.
- RegionsOnly: encoder browses Regions. Push = GoToRegion (no drill).
  Shift+push, long-press = no-op.
- None (drill mode): full behaviour per the table in section 5.

## REAPER API surface — additions over 2.8a

Same set as 2.8a. No new APIs:
- `EnumProjectMarkers3`, `CountProjectMarkers` (data)
- `GoToRegion(proj, idx, false)` (region jumps, smooth-seek)
- `SetEditCurPos(pos, true, true)` (marker jumps, seek-queue-safe)
- `GetPlayPosition()` / `GetCursorPosition()` / `GetPlayState()`

UC1 protocol: only existing builders (`buildTrackNameTripleLarge`,
`buildLcdHeader`, `buildCentralMode`, `buildDisplayInvalidate`,
`buildLcdColourBar`).

## Build order (atomic commits, mirroring 2.8a)

**Phase 2.8b — Carousel** (estimated 6 commits)

1. `Overlay::moveCursor(delta)` + `cursorPinned_` flag + page-slide
   factored out. tickAutoFollow gated on pin. Diagnostic: `REASIXTY_NAV_DUMP`
   prints pin state.
2. `UC1Surface::showNavCarousel` + `hideNavCarousel` + `navCarouselActive_`
   flag. Cache strings; only resend on change.
3. UC1 main-tick wiring: when `Overlay::active()` AND
   `g_uc1NavLcdActive`, push the carousel each frame with strings
   computed from cursor-centred 3-window.
4. Encoder 2 rotation intercept in `UC1Surface::onKnobEvent` → routes
   to `Overlay::moveCursor(step)`.
5. Encoder 2 push intercept in `UC1Surface::onButtonEvent` →
   detects short / shift / long via timer; fires Jump+Drill (Regions)
   or SetEditCurPos (Markers).
6. Mode arbitration: Nav Mode entry forces `Uc1Mode::Main` + remembers
   prior. Menu-mode entry clears `g_uc1NavLcdActive`. Nav exit
   restores prior UC1 mode if no menu was entered.

**Phase 2.8c — Settings UI** (estimated 5 commits)

7. `drawModes` → BeginTabBar with 6 sub-tabs. Pure layout change,
   every existing section moves verbatim. Persist last-active tab in
   ExtState.
8. NAV sub-tab Activation section (read-only mirrors of 3 toggle bindings).
9. NAV sub-tab View defaults (default-view radio, region-press radio).
10. NAV sub-tab UC1 Encoder 2 section (take-over + push radios). Carousel
    scope radio with only "Mirror" enabled.
11. NAV sub-tab UF8 strip display section (lower-row format, pagination,
    colour-bar source) + accessor wiring in main.cpp.

Each commit ends with `cp` to UserPlugins + `git push` (per the
existing CLAUDE.md workflow). Frank tests on hardware after the
batch — no per-commit verification needed.

## Risks / open spots

- **Long-press detection**: independent timer on UC1 input thread.
  If REAPER's editor blocks the input thread (unlikely), long-press
  fires late. Acceptable.
- **UC1 LCD frame collision** with the BC carousel: both want the
  3-up triple zone. Nav has higher priority (overrides) but
  re-entry coordination needs testing. Plan: Nav explicitly clears
  `bcScrollOverlayActive_` and `instanceCarouselActive_` on entry,
  same pattern as `showInstanceCarousel`.
- **Mirror scope correctness**: when UF8 is in MarkersInRegion(A)
  and UC1 carousel mirrors, UC1 also shows A's markers. Cursor is
  shared so it works. When UF8 switches via auto-roll, UC1's carousel
  follows on the next tick. Verify there's no visible flicker.
- **Settings → Modes tab persistence**: ImGui tabs have their own
  persistence via the imgui.ini file; we may need explicit
  SetTabItemFlags to override. Test once 2.8c step 7 lands.

## Out of scope (Phase 2.8d)

- V-Pot rotate = fine-move selected marker
- V-Pot push (hold) = delete marker
- SEL on strip = reposition marker to playhead
- Empty slot press = AddProjectMarker at playhead
- Long-press top-soft-key on region = set loop + play
- Carousel scope: Always-Regions / Always-Markers (the non-Mirror
  options stubbed in 2.8c)
