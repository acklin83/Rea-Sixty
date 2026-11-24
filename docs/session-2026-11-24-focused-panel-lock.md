# Session 2026-11-24: Focused-track panel, Lock position / Lock size

## Request
Frank: right-click options "lock position" and "lock size" on the focused-track
panel, so a floating panel cannot be moved or resized by accident.

## Built
- `extension/scripts/rea_sixty_focused_panel_imgui.lua`: two menu items after
  *Dock window*, ExtState `focused_panel_lock_pos` / `focused_panel_lock_size`
  (persisted, off by default). Floating only: they add `WindowFlags_NoMove` /
  `WindowFlags_NoResize`. Docked, the items are greyed out and the flags are not
  applied. The first-frame restore still places the window (SetNextWindowPos/Size
  is not blocked by those flags). The "Drag the box anywhere" hint hides while
  the position is locked or the panel is docked.
- Manual, Focused-track panel: menu list brought in line with the real menu
  (Layout = centre + arrange, Elements carries CS/BC parameters and Track name,
  Dock window, the two locks; the old *Align* bullet was gone from the menu).
- Built, embedded (`Embedding rea_sixty_focused_panel_imgui.lua`), installed on the
  Mac Studio via /tmp + mv, hash checked. REAPER needs a restart to pick it up.
