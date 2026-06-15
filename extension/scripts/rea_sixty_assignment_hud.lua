-- @description Rea-Sixty — Learn-HUD (companion)
-- @author Störsender
-- @version 0.1.0
-- @provides [main] .
-- @about
--   Dockable on-screen HUD that draws the **focused plug-in's UC1 surface map**
--   as a vector mockup: every control in its physical place, mapped controls
--   ringed in their domain colour (CS / BC) with the bound parameter's name,
--   unmapped controls dimmed. Read-only in this milestone — it mirrors what the
--   hardware does so you can see a plug-in's assignments at a glance without
--   opening Settings.
--
--   Pure gfx (no ReaImGui, no JS_Composite). The extension publishes:
--     • "rea_sixty"/"hud_geom_uc1" — static control geometry (pixels in an
--       860×660 space; this script normalises by W/H so it scales to any size).
--     • "rea_sixty"/"hud_state"    — "UC1;<csPresent>;<bcPresent>;<csShort>;<bcShort>"
--     • "rea_sixty"/"hud_assign"   — one line per mapped control "<idx>;<name>;<inv>"
--   Run once to start (background defer); run again (or untick the Settings
--   checkbox) to stop. Dock state persists. Requires the Rea-Sixty extension
--   the Learn-HUD toggled on (learn_hud_toggle built-in / REASIXTY_LEARN_HUD_TOGGLE).

local _, _, sectionID, cmdID = reaper.get_action_context()

local SECT   = "rea_sixty"
local RUNKEY = "hud_running"

if reaper.GetExtState(SECT, RUNKEY) == "1" then
  reaper.SetExtState(SECT, RUNKEY, "0", false)
  return
end
reaper.SetExtState(SECT, RUNKEY, "1", false)

local function setToggle(on)
  if sectionID and cmdID and sectionID >= 0 then
    reaper.SetToggleCommandState(sectionID, cmdID, on and 1 or 0)
    reaper.RefreshToolbar2(sectionID, cmdID)
  end
end

------------------------------------------------------------------------
-- Tunables (live via ExtState — shared with the inserts overlay so CS/BC
-- colours stay consistent across the whole product).
------------------------------------------------------------------------
local function num(key, def)
  local v = tonumber(reaper.GetExtState(SECT, key))
  if v == nil then return def end
  return v
end
local function csRgb() return math.floor(num("overlay_cs_col", 0xFFFF00)) & 0xFFFFFF end
local function bcRgb() return math.floor(num("overlay_bc_col", 0xFF0000)) & 0xFFFFFF end

-- User-adjustable text size (right-click menu). Multiplies all font sizes +
-- the column geometry so the layout scales as one. Persisted in ExtState.
local function fontScale()
  local v = num("hud_font", 1.5)
  if v < 0.8 then v = 0.8 elseif v > 2.6 then v = 2.6 end
  return v
end

local function gset(rgb, a)
  gfx.set(((rgb >> 16) & 0xFF) / 255, ((rgb >> 8) & 0xFF) / 255,
          (rgb & 0xFF) / 255, a or 1)
end

------------------------------------------------------------------------
-- Geometry: parse "hud_geom_uc1" once (cache by raw string).
--   header: "UC1;<W>;<H>"
--   control: "<idx>;<shape>;<cx>;<cy>;<r>;<w>;<h>;<dom>;<label>;<section>"
--            shape 0=knob 1=toggle 2=dynbtn ; dom 'c'=CS 'b'=BC
-- NB the device token is "UC1" (letters + a digit) — match it with %w, not %a.
------------------------------------------------------------------------
local geomRaw = nil
local geom    = { w = 860, h = 660, ctrl = {} }

local function parseGeom(raw)
  local g = { w = 860, h = 660, ctrl = {} }
  local first = true
  for line in raw:gmatch("[^\n]+") do
    if first then
      local dev, w, h = line:match("^(%w+);(%d+);(%d+)")
      if w then g.w = tonumber(w); g.h = tonumber(h) end
      first = false
    else
      -- section is [^;]* (not greedy .*) so the appended ;<cap>;<legend> fields
      -- the mockup HUD uses are ignored here — this list HUD doesn't need them.
      local idx, shape, cx, cy, r, w, h, dom, label, sec =
        line:match("^(%d+);(%d+);([%d%.]+);([%d%.]+);([%d%.]+);([%d%.]+);([%d%.]+);(%a);([^;]*);([^;]*)")
      if idx then
        g.ctrl[tonumber(idx)] = {
          shape = tonumber(shape), cx = tonumber(cx), cy = tonumber(cy),
          r = tonumber(r), w = tonumber(w), h = tonumber(h),
          dom = dom, label = label or "", sec = sec or "",
        }
      end
    end
  end
  return g
end

local function refreshGeom()
  local raw = reaper.GetExtState(SECT, "hud_geom_uc1")
  if raw ~= "" and raw ~= geomRaw then
    geomRaw = raw
    geom = parseGeom(raw)
  end
end

------------------------------------------------------------------------
-- State + assignments.
------------------------------------------------------------------------
local function readState()
  local raw = reaper.GetExtState(SECT, "hud_state")
  -- "UC1;<csPresent>;<bcPresent>;<focusDom c/b/n>;<csShort>;<bcShort>"
  -- "UC1;<cs>;<bc>;<fdom>;<layer>;<csShort>;<bcShort>"
  -- device token is "UC1" (has a digit) → %w, not %a (the old %a broke the
  -- whole match, so csPresent/bcPresent/focusDom silently defaulted to off).
  -- layer = held-modifier FX-Learn layer (0=Normal 1=Option 2=Control).
  local dev, cs, bc, fdom, layer, csN, bcN =
    raw:match("^(%w+);(%d);(%d);(%a);(%d);([^;]*);(.*)$")
  return {
    dev = dev or "UC1",
    csPresent = (cs == "1"),
    bcPresent = (bc == "1"),
    focusDom = fdom or "n",
    layer = tonumber(layer) or 0,
    csShort = csN or "",
    bcShort = bcN or "",
  }
end

-- Active tab ("cs"/"bc") + the last focus domain we auto-switched on, so a
-- focus change follows but a manual tab click sticks until the next change.
local activeTab    = "cs"
local lastFocusDom = "n"
local tabRects     = {}   -- {dom,x,y,w,h} filled by drawTabs for hit-testing

-- Interactivity (Phase 1: click-a-control + wiggle-to-learn). The extension
-- owns the bind logic; this script only hit-tests + sends commands via
-- ExtState "hud_cmd" ("learn;<idx>" / "cancel") and reads back the armed
-- control idx from "hud_state"-sibling key "hud_learn".
local ctrlRects = {}      -- {idx,shape,x,y,w,h} row hit-rects (rebuilt each frame)
local learnIdx  = -1      -- armed control idx (authoritative, from extension)
local frame     = 0       -- frame counter for the learn pulse
local hintText  = ""      -- transient hint ("Factory map — not editable")
local hintFrames = 0
local scrollY    = 0      -- text-list vertical scroll (mouse wheel)
local maxScroll  = 0      -- clamp, recomputed each layout

local function sendCmd(s) reaper.SetExtState(SECT, "hud_cmd", s, false) end

local function readAssign()
  local raw = reaper.GetExtState(SECT, "hud_assign")
  local map = {}
  for line in raw:gmatch("[^\n]+") do
    local idx, name, inv = line:match("^(%d+);([^;]*);(%d)$")
    if idx then
      map[tonumber(idx)] = { name = name or "", inv = (inv == "1") }
    end
  end
  return map
end

------------------------------------------------------------------------
-- Window lifecycle (mirrors the inserts-overlay dock panel).
------------------------------------------------------------------------
local winOpen   = false
local rcPrev    = 0     -- previous right-button state (context-menu edge detect)
local lcPrev    = 0     -- previous left-button state (tab-click edge detect)
local DEF_W, DEF_H = 500, 500    -- default floating size
local savedW, savedH = DEF_W, DEF_H   -- last persisted floating size

local function winWanted() return reaper.GetExtState(SECT, "hud_on") == "1" end

-- Persist the floating window size GLOBALLY (across projects) so a resize
-- sticks. Only while floating (docked size is the docker's business) and only
-- on change, to avoid spamming ExtState every frame.
-- CRITICAL: with gfx.ext_retina on, gfx.w/h report PHYSICAL pixels (2× on a
-- Retina display) but gfx.init expects LOGICAL points — so storing gfx.w raw
-- and feeding it back DOUBLES the window every session (runaway → huge). Divide
-- by the live retina scale and store logical points.
local function saveSizeIfChanged()
  if not winOpen then return end
  if (gfx.dock(-1) & 1) == 1 then return end          -- docked → don't capture
  local sc = gfx.ext_retina; if not sc or sc < 1 then sc = 1 end
  local w = math.floor(gfx.w / sc + 0.5)
  local h = math.floor(gfx.h / sc + 0.5)
  if w > 50 and h > 50 and (w ~= savedW or h ~= savedH) then
    savedW, savedH = w, h
    reaper.SetExtState(SECT, "hud_w", tostring(w), true)
    reaper.SetExtState(SECT, "hud_h", tostring(h), true)
  end
end

local function winClose(persistOff)
  if winOpen then
    saveSizeIfChanged()
    local dock = gfx.dock(-1)
    reaper.SetExtState(SECT, "hud_dock", tostring(dock or 0), true)
    gfx.quit()
    winOpen = false
  end
  if persistOff then reaper.SetExtState(SECT, "hud_on", "0", true) end
end

-- Text-size presets offered in the right-click menu.
local FONT_PRESETS = {
  { l = "Small",       v = 1.0 },
  { l = "Medium",      v = 1.25 },
  { l = "Large",       v = 1.5 },
  { l = "Extra Large", v = 1.9 },
  { l = "Huge",        v = 2.3 },
}

local function contextMenu()
  local docked = (gfx.dock(-1) & 1) == 1
  gfx.x, gfx.y = gfx.mouse_x, gfx.mouse_y
  local fs = fontScale()
  -- "Text size" submenu with a tick on the current preset.
  local sub = {}
  for _, p in ipairs(FONT_PRESETS) do
    sub[#sub + 1] = (math.abs(fs - p.v) < 0.01 and "!" or "") .. p.l
  end
  sub[#sub] = "<" .. sub[#sub]      -- mark the last submenu item
  -- "Text colour" submenu — list text in the CS/BC domain colour or plain white.
  local white = (reaper.GetExtState(SECT, "hud_text_white") == "1")
  local colSub = (white and "" or "!") .. "CS / BC colour|<" .. (white and "!" or "") .. "White"
  local menu = (docked and "!" or "") .. "Dock window in Docker|>Text size|"
             .. table.concat(sub, "|")
             .. "|>Text colour|" .. colSub
             .. "|Close HUD"
  local sel = gfx.showmenu(menu)
  -- Item order: 1=Dock, 2..(1+N)=font presets, (2+N)=CS/BC colour,
  -- (3+N)=White, (4+N)=Close.  N = #FONT_PRESETS.
  local N = #FONT_PRESETS
  if sel == 1 then
    if docked then gfx.dock(0) else gfx.dock(1) end
    reaper.SetExtState(SECT, "hud_dock", tostring(gfx.dock(-1) or 0), true)
  elseif sel >= 2 and sel <= 1 + N then
    reaper.SetExtState(SECT, "hud_font", tostring(FONT_PRESETS[sel - 1].v), true)
  elseif sel == 2 + N then
    reaper.SetExtState(SECT, "hud_text_white", "0", true)
  elseif sel == 3 + N then
    reaper.SetExtState(SECT, "hud_text_white", "1", true)
  elseif sel == 4 + N then
    winClose(true)
  end
end

------------------------------------------------------------------------
-- Render.
------------------------------------------------------------------------
local TAB_H = 36

-- Truncate a string to fit `maxw` px (current font), adding an ellipsis.
local function fit(s, maxw)
  if s == "" then return s end
  local w = gfx.measurestr(s)
  if w <= maxw then return s end
  while #s > 1 do
    s = s:sub(1, #s - 1)
    if gfx.measurestr(s .. "\xE2\x80\xA6") <= maxw then return s .. "\xE2\x80\xA6" end
  end
  return s
end

-- Tab strip: one tab per domain, active one highlighted in its colour. Stores
-- hit-rects in tabRects for click handling in the main loop.
local function drawTabs(st)
  gfx.set(0.09, 0.09, 0.10, 1); gfx.rect(0, 0, gfx.w, TAB_H, 1)
  tabRects = {}
  local function tab(dom, present, short, x)
    local name = (dom == "cs") and "CS" or "BC"
    local label = name .. ((present and short ~= "") and ("  " .. short) or "")
    gfx.setfont(2, "Arial", math.floor(17 * fontScale() + 0.5))
    local w = gfx.measurestr(label) + 28
    local active = (activeTab == dom)
    local rgb = (dom == "cs") and csRgb() or bcRgb()
    if active then gset(rgb, present and 0.30 or 0.16)
    else           gfx.set(0.14, 0.14, 0.16, 1) end
    gfx.rect(x, 4, w, TAB_H - 6, 1)
    if active then gset(rgb, present and 1 or 0.5); gfx.rect(x, 4, w, 3, 1) end
    gset(rgb, present and 1 or 0.40)
    gfx.x, gfx.y = x + 12, 4 + math.floor((TAB_H - 6 - gfx.texth) / 2)
    gfx.drawstr(label)
    tabRects[#tabRects + 1] = { dom = dom, x = x, y = 4, w = w, h = TAB_H - 6 }
    return x + w + 4
  end
  local x = 8
  x = tab("cs", st.csPresent, st.csShort, x)
  x = tab("bc", st.bcPresent, st.bcShort, x)

  -- Active modifier-layer badge (far right). NORM = dim (no overlay); OPT/CTRL
  -- light up so a changed param list reads as "this is that overlay layer".
  -- Layers apply to UC1 user maps only — built-ins ignore the held modifier.
  local name = (st.layer == 1 and "OPT") or (st.layer == 2 and "CTRL") or "NORM"
  gfx.setfont(2, "Arial", math.floor(13 * fontScale() + 0.5))
  local bw = gfx.measurestr(name) + 20
  local bh = TAB_H - 12
  local bx = gfx.w - bw - 8
  local by = 6
  if st.layer == 0 then
    gfx.set(0.16, 0.16, 0.18, 1); gfx.rect(bx, by, bw, bh, 1)
    gset(0x8890A0, 0.7)
    gfx.x, gfx.y = bx + 10, by + math.floor((bh - gfx.texth) / 2)
    gfx.drawstr(name)
  else
    local lc = (st.layer == 1) and 0x30C8A0 or 0xC878FF   -- OPT teal / CTRL purple
    gset(lc, 0.92); gfx.rect(bx, by, bw, bh, 1)
    gfx.set(0.07, 0.07, 0.08, 1)                          -- dark text on bright
    gfx.x, gfx.y = bx + 10, by + math.floor((bh - gfx.texth) / 2)
    gfx.drawstr(name)
  end
end

-- Click a tab (called from the loop on a left-button edge). Returns true if a
-- tab was hit (so the caller skips control hit-testing).
local function handleTabClick(mx, my)
  for _, t in ipairs(tabRects) do
    if mx >= t.x and mx <= t.x + t.w and my >= t.y and my <= t.y + t.h then
      activeTab = t.dom
      return true
    end
  end
  return false
end

-- Click a control: arm learn for it (or cancel if it's already armed). Uses
-- last frame's ctrlRects. Returns true if a control was hit.
local function handleControlClick(mx, my)
  for _, h in ipairs(ctrlRects) do
    local hit
    if h.shape == 0 then
      local dx, dy = mx - h.x, my - h.y
      hit = (dx * dx + dy * dy) <= (h.r + 3) * (h.r + 3)
    else
      hit = mx >= h.x and mx <= h.x + h.w and my >= h.y and my <= h.y + h.h
    end
    if hit then
      if learnIdx == h.idx then sendCmd("cancel")
      else                      sendCmd("learn;" .. h.idx) end
      return true
    end
  end
  return false
end

------------------------------------------------------------------------
-- Grouped text list (replaces the hardware-faithful scatter, which wasted
-- space + truncated names). Each row = silk label + the FULL bound param
-- name, grouped under section headers, flowed into columns.
------------------------------------------------------------------------
-- Section display order per domain. Controls bucket into these; any unlisted
-- section is appended in first-seen order. (Sections come from the geometry's
-- ;<section> field, sourced from kUc1Controls in the extension.)
local SECTION_ORDER = {
  cs = { "Filter", "EQ", "Dynamics", "Gate", "I/O & Channel" },
  bc = { "Bus Comp" },
}

-- Fixed column assignment per domain — mirrors the UC1 hardware: filters + EQ
-- (and I/O) on the LEFT, the Dynamics + Gate sections on the RIGHT. Sections
-- not listed here get appended to the last column.
local COLUMN_SECTIONS = {
  cs = { { "Filter", "EQ", "I/O & Channel" }, { "Dynamics", "Gate" } },
  bc = { { "Bus Comp" } },
}

-- Bucket the active domain's controls by section name (each list sorted by idx).
local function groupControls(domChar)
  local by = {}
  for idx, c in pairs(geom.ctrl) do
    if c.dom == domChar then
      c.idx = idx
      local s = (c.sec ~= "" and c.sec) or "Other"
      local list = by[s]; if not list then list = {}; by[s] = list end
      list[#list + 1] = c
    end
  end
  for _, list in pairs(by) do
    table.sort(list, function(a, b) return a.idx < b.idx end)
  end
  return by
end

-- Draw one control row at (x,y); records a hit-rect for learn.
local function drawRow(c, x, y, rowW, labelW, lineH, rowFont, present, rgb, asn)
  local a        = asn[c.idx]
  local mapped   = (a ~= nil)
  local learning = (c.idx == learnIdx)

  if learning then
    local pulse = 0.22 + 0.22 * math.abs((frame % 50) / 25 - 1)
    gset(0xFFFFFF, pulse); gfx.rect(x - 3, y - 1, rowW + 6, lineH, 1)
  end

  -- Silk label (left), state-coloured.
  local labCol, labA
  if mapped       then labCol, labA = rgb,      1.0
  elseif present  then labCol, labA = 0xC6CDD6, 0.95
  else                 labCol, labA = 0x808892, 0.5 end
  gfx.setfont(1, "Arial", rowFont)
  gset(labCol, labA)
  gfx.x, gfx.y = x, y + 1
  gfx.drawstr(c.label)

  -- Param name (right of the label), full — fit() is only a last-ditch guard.
  local txt, pCol, pA
  if mapped then
    txt = a.name; if a.inv then txt = txt .. "  inv" end
    pCol, pA = rgb, 1.0
  elseif present then txt, pCol, pA = "\xE2\x80\x94", 0x9097A0, 0.7   -- em dash
  else                txt, pCol, pA = "\xE2\x80\x94", 0x707880, 0.45 end
  gset(pCol, pA)
  gfx.x, gfx.y = x + labelW, y + 1
  gfx.drawstr(fit(txt, rowW - labelW))

  ctrlRects[#ctrlRects + 1] =
    { idx = c.idx, shape = 1, x = x - 3, y = y - 1, w = rowW + 6, h = lineH }
end

-- Lay out + draw the grouped text list for the active domain. Fixed columns
-- (hardware-ish), font scaled by the user's text-size preset. Updates ctrlRects
-- + maxScroll.
local function renderList(st, asn)
  local domChar = (activeTab == "cs") and "c" or "b"
  local present = (activeTab == "cs") and st.csPresent or st.bcPresent
  -- List text in the domain colour (CS/BC) or plain white (right-click → Text
  -- colour). Tabs keep their domain colour as the key. Headers + mapped rows +
  -- param names all read this `rgb`.
  local rgb     = (activeTab == "cs") and csRgb() or bcRgb()
  if reaper.GetExtState(SECT, "hud_text_white") == "1" then rgb = 0xFFFFFF end
  local by      = groupControls(domChar)

  local fs       = fontScale()
  local rowFont  = math.floor(16 * fs + 0.5)
  local headFont = math.floor(15 * fs + 0.5)

  local M      = 14
  local top    = TAB_H + 10
  local bottom = gfx.h - 8
  if gfx.w - 2 * M < 60 or bottom - top < 40 then return end

  -- Row metrics from the (scaled) row font. Column widths come from the ACTUAL
  -- content — left = widest control (SSL slot) label in this domain, right =
  -- widest param name — so the two name columns never overlap or truncate
  -- whatever the names turn out to be.
  gfx.setfont(1, "Arial", rowFont)
  local lineH  = gfx.texth + math.floor(5 * fs + 0.5)
  local labelW = 0
  for _, list in pairs(by) do
    for _, c in ipairs(list) do
      local w = gfx.measurestr(c.label)
      if w > labelW then labelW = w end
    end
  end
  labelW = labelW + math.floor(18 * fs + 0.5)
  local paramW = gfx.measurestr("Threshold")
  for _, a in pairs(asn) do
    local w = gfx.measurestr(a.name or "")
    if w > paramW then paramW = w end
  end
  local colW = labelW + paramW + math.floor(20 * fs + 0.5)
  gfx.setfont(1, "Arial", headFont)
  local headH  = gfx.texth + math.floor(11 * fs + 0.5)
  local gap    = math.floor(8 * fs + 0.5)
  local colGap = math.floor(16 * fs + 0.5)

  -- Column section assignment (fall back to one column = display order).
  local cols = COLUMN_SECTIONS[activeTab] or { SECTION_ORDER[activeTab] or {} }
  local assigned = {}
  for _, cset in ipairs(cols) do for _, s in ipairs(cset) do assigned[s] = true end end
  local leftover = {}
  for _, s in ipairs(SECTION_ORDER[activeTab] or {}) do
    if by[s] and not assigned[s] then leftover[#leftover + 1] = s; assigned[s] = true end
  end
  for s in pairs(by) do
    if not assigned[s] then leftover[#leftover + 1] = s; assigned[s] = true end
  end

  -- LAYOUT pass.
  local items, peakY = {}, top
  for ci, cset in ipairs(cols) do
    local x = M + (ci - 1) * (colW + colGap)
    local y = top
    local secList = cset
    if ci == #cols and #leftover > 0 then
      secList = {}
      for _, s in ipairs(cset)     do secList[#secList + 1] = s end
      for _, s in ipairs(leftover) do secList[#secList + 1] = s end
    end
    for _, s in ipairs(secList) do
      local ctrls = by[s]
      if ctrls and #ctrls > 0 then
        items[#items + 1] = { kind = "head", x = x, y = y, text = s }
        y = y + headH
        for _, c in ipairs(ctrls) do
          items[#items + 1] = { kind = "row", x = x, y = y, c = c }
          y = y + lineH
        end
        y = y + gap
      end
    end
    if y > peakY then peakY = y end
  end

  maxScroll = math.max(0, peakY - bottom)
  scrollY = math.max(0, math.min(scrollY, maxScroll))

  -- DRAW pass — apply scroll, cull off-screen, build hit-rects.
  ctrlRects = {}
  for _, it in ipairs(items) do
    local yy = it.y - scrollY
    if yy + lineH >= top and yy <= bottom then
      if it.kind == "head" then
        gfx.setfont(1, "Arial", headFont)
        gset(rgb, present and 0.9 or 0.5)
        gfx.x, gfx.y = it.x, yy
        gfx.drawstr(it.text)
        gset(rgb, present and 0.30 or 0.18)
        gfx.rect(it.x, yy + gfx.texth + 2, colW, 1, 1)
      else
        drawRow(it.c, it.x, yy, colW, labelW, lineH, rowFont, present, rgb, asn)
      end
    end
  end
end

local function render()
  -- Background.
  gfx.set(0.12, 0.12, 0.13, 1); gfx.rect(0, 0, gfx.w, gfx.h, 1)

  -- Tab-strip height scales with the text size so the tabs never clip.
  TAB_H = math.floor(22 * fontScale() + 0.5) + 16

  local st  = readState()
  local asn = readAssign()

  frame = frame + 1
  -- Armed control is owned by the extension (it refuses factory maps, times
  -- out, and clears on bind) — read it back as the source of truth.
  local hl = reaper.GetExtState(SECT, "hud_learn")
  learnIdx = (hl ~= "" and tonumber(hl)) or -1
  -- Transient hint from the extension (e.g. "Factory map — not editable").
  local hint = reaper.GetExtState(SECT, "hud_hint")
  if hint ~= "" then hintText = hint; hintFrames = 90
    reaper.SetExtState(SECT, "hud_hint", "", false) end

  -- Auto-follow focus: when the surface's focused domain changes, switch the
  -- active tab to it. A manual tab click sticks until the next focus change.
  if st.focusDom == "c" or st.focusDom == "b" then
    if st.focusDom ~= lastFocusDom then
      activeTab = (st.focusDom == "c") and "cs" or "bc"
      lastFocusDom = st.focusDom
    end
  else
    lastFocusDom = "n"
  end

  drawTabs(st)

  local nCtrl = 0; for _ in pairs(geom.ctrl) do nCtrl = nCtrl + 1 end

  if not geom or nCtrl == 0 then
    gfx.setfont(1, "Arial", math.floor(15 * fontScale() + 0.5))
    gset(0x808890, 0.9)
    gfx.x, gfx.y = 10, TAB_H + 12
    gfx.drawstr("Waiting for surface geometry\xE2\x80\xA6")
    return
  end

  local present = (activeTab == "cs") and st.csPresent or st.bcPresent

  if not present then
    -- Nothing focused in this domain → clear centred message (no list).
    ctrlRects = {}
    gfx.setfont(2, "Arial", math.floor(17 * fontScale() + 0.5))
    gset(0x9097A0, 0.9)
    local msg = (activeTab == "cs" and "No Channel-Strip" or "No Bus-Comp")
              .. " plug-in on the focused track"
    local tw = gfx.measurestr(msg)
    gfx.x, gfx.y = (gfx.w - tw) / 2, (gfx.h - gfx.texth) / 2
    gfx.drawstr(msg)
  else
    renderList(st, asn)
  end

  -- Centred banner just under the tab strip (bg box + coloured text).
  local function banner(msg, bgRgb, bgA, fgRgb)
    gfx.setfont(1, "Arial", math.floor(14 * fontScale() + 0.5))
    local tw = gfx.measurestr(msg)
    local bw, bh = tw + 20, gfx.texth + 8
    local bx, by = (gfx.w - bw) / 2, TAB_H + 4
    gset(bgRgb, bgA); gfx.rect(bx, by, bw, bh, 1)
    gset(fgRgb, 1);   gfx.x, gfx.y = bx + 10, by + 4; gfx.drawstr(msg)
  end

  -- Learn-mode banner (overrides the transient hint while armed).
  if learnIdx >= 0 then
    local c   = geom.ctrl[learnIdx]
    local lbl = (c and c.label ~= "" and c.label) or ("#" .. learnIdx)
    banner("Learning " .. lbl ..
           "  \xE2\x80\x94  wiggle a plug-in parameter to bind it"
           .. "   (click again or Esc to cancel)",
           0x101418, 0.88, 0xFFD060)
  elseif hintFrames > 0 then
    hintFrames = hintFrames - 1
    banner(hintText, 0x301014, 0.92, 0xFF8888)
  end
end

------------------------------------------------------------------------
-- Main defer loop.
------------------------------------------------------------------------
local shutdown

local function loop()
  if reaper.GetExtState(SECT, RUNKEY) ~= "1" then return shutdown() end

  local want = winWanted()
  if want and not winOpen then
    local dock = math.floor(num("hud_dock", 0))
    savedW = math.floor(num("hud_w", DEF_W))
    savedH = math.floor(num("hud_h", DEF_H))
    -- Heal sizes corrupted by the old retina-doubling bug (and any absurd value)
    -- back to the 500×500 default; clamp to a sane floating range.
    if savedW < 200 or savedW > 1600 then savedW = DEF_W end
    if savedH < 200 or savedH > 1600 then savedH = DEF_H end
    gfx.ext_retina = 1
    gfx.init("Rea-Sixty Learn-HUD", savedW, savedH, dock, 200, 200)
    -- Persist the (possibly healed) logical size so a corrupted old value is
    -- overwritten cleanly even before the user next resizes.
    reaper.SetExtState(SECT, "hud_w", tostring(savedW), true)
    reaper.SetExtState(SECT, "hud_h", tostring(savedH), true)
    winOpen = true
  elseif not want and winOpen then
    winClose(false)
  end

  if winOpen then
    local ch = gfx.getchar()
    if ch < 0 then return winClose(true) end               -- user closed window
    if ch == 27 and learnIdx >= 0 then sendCmd("cancel") end  -- Esc cancels learn
    local rc = gfx.mouse_cap & 2
    if rc == 2 and rcPrev ~= 2 then contextMenu() end
    rcPrev = rc
    if winOpen then
      -- Left-click edge → tab switch, else arm/cancel learn on a control
      -- (both use last frame's hit-rects).
      local lc = gfx.mouse_cap & 1
      if lc == 1 and lcPrev ~= 1 then
        if not handleTabClick(gfx.mouse_x, gfx.mouse_y) then
          handleControlClick(gfx.mouse_x, gfx.mouse_y)
        end
      end
      lcPrev = lc
      saveSizeIfChanged()   -- persist a floating resize (global, on change)
      -- Mouse wheel scrolls the list (only bites when content overflows; render
      -- clamps to [0, maxScroll]).
      if gfx.mouse_wheel ~= 0 then
        scrollY = scrollY - gfx.mouse_wheel / 4
        gfx.mouse_wheel = 0
      end
      refreshGeom()
      render()
      gfx.update()
    end
  end

  reaper.defer(loop)
end

shutdown = function()
  winClose(false)
  reaper.SetExtState(SECT, RUNKEY, "0", false)
  setToggle(false)
end

reaper.atexit(shutdown)
setToggle(true)
loop()
