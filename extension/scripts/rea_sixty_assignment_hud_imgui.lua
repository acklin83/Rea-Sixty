-- @description Rea-Sixty — Learn-HUD (ReaImGui spike, place-anywhere + dockable)
-- @author Störsender
-- @version 0.1.0
-- @provides [main] .
-- @about
--   SPIKE / parallel variant of rea_sixty_assignment_hud.lua.
--
--   Same Learn-HUD (per-domain tabs, grouped param list, click-to-learn,
--   layer badge, auto-follow focus) but rendered with **ReaImGui** instead of
--   pure `gfx`. Wins: hi-res/DPI-aware text, freely movable + multi-monitor,
--   ReaImGui's built-in title-bar docking, hover tooltips for free.
--
--   Data layer is ported VERBATIM from the gfx HUD and reads the exact same
--   ExtState the extension publishes:
--     • hud_geom_uc1 / hud_state / hud_assign / hud_learn / hud_hint
--     • writes hud_cmd ("learn;<idx>" / "cancel")
--     • CS/BC colours via overlay_cs_col / overlay_bc_col (shared product-wide)
--   Text size / colour options under their own keys (hud_font / hud_text_white)
--   — SHARED with the gfx HUD on purpose. Window geometry under own keys
--   (hud_imgui_*) so this spike coexists with the shipping gfx HUD.
--
--   Unlike the gfx HUD this window has a TITLE BAR: the list rows are clickable
--   (learn), so dragging the body to move would fight the clicks — the title bar
--   gives clean move/dock instead. Requires ReaImGui (>= 0.9).

local SECT   = "rea_sixty"
local RUNKEY = "hud_imgui_running"

local _, _, sectionID, cmdID = reaper.get_action_context()

if reaper.GetExtState(SECT, RUNKEY) == "1" then
  reaper.SetExtState(SECT, RUNKEY, "0", false)
  return
end
reaper.SetExtState(SECT, RUNKEY, "1", false)
reaper.SetExtState(SECT, "hud_touch_learn", "0", false)   -- start with HW-learn off

local function setToggle(on)
  if sectionID and cmdID and sectionID >= 0 then
    reaper.SetToggleCommandState(sectionID, cmdID, on and 1 or 0)
    reaper.RefreshToolbar2(sectionID, cmdID)
  end
end
setToggle(true)

if not reaper.ImGui_CreateContext then
  reaper.MB("This spike needs ReaImGui (ReaScript ImGui API).\n\n"
    .. "Install via ReaPack: 'ReaImGui: ReaScript binding for Dear ImGui'.",
    "Rea-Sixty Learn-HUD (ImGui)", 0)
  reaper.SetExtState(SECT, RUNKEY, "0", false)
  setToggle(false)
  return
end

local floor = math.floor

------------------------------------------------------------------------
-- Tunables (live via ExtState — shared with the gfx HUD / inserts overlay).
------------------------------------------------------------------------
local function num(key, def)
  local v = tonumber(reaper.GetExtState(SECT, key))
  if v == nil then return def end
  return v
end
local function csRgb() return floor(num("overlay_cs_col", 0xFFFF00)) & 0xFFFFFF end
local function bcRgb() return floor(num("overlay_bc_col", 0xFF0000)) & 0xFFFFFF end

-- Fixed text scale. The old user-facing "Text size" option only visibly
-- affected the CS/BC tab titles (the face/list scale to the window), and even
-- its smallest step was too large, so it was removed (Frank 2026-06-16).
local function fontScale() return 1.0 end

------------------------------------------------------------------------
-- Geometry: parse "hud_geom_uc1" once (cache by raw string). [verbatim]
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
      local idx, shape, cx, cy, r, w, h, dom, label, rest =
        line:match("^(%d+);(%d+);([%d%.]+);([%d%.]+);([%d%.]+);([%d%.]+);([%d%.]+);(%a);([^;]*);(.*)$")
      if idx then
        -- rest = "section"  OR (new format) "section;<cap RGBA>;<legend>".
        -- Backward compatible: old geometry without cap/legend still parses.
        local sec, cap, legend = rest, 0, ""
        local s2, c2, l2 = rest:match("^([^;]*);(%d+);(.*)$")
        if s2 then sec, cap, legend = s2, tonumber(c2) or 0, l2 end
        local cid = tonumber(idx)
        g.ctrl[cid] = {
          idx = cid,   -- stored as a field too (mockup hit-rects + learn need it)
          shape = tonumber(shape), cx = tonumber(cx), cy = tonumber(cy),
          r = tonumber(r), w = tonumber(w), h = tonumber(h),
          dom = dom, label = label or "", sec = sec or "",
          cap = cap, legend = (legend ~= "" and legend) or (label or ""),
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
-- State + assignments. [verbatim]
------------------------------------------------------------------------
local function readState()
  local raw = reaper.GetExtState(SECT, "hud_state")
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

local activeTab    = "cs"
local lastFocusDom = "n"
local tabRects     = {}
local learnBtnRect = nil
local ctrlRects    = {}
local learnIdx     = -1
local frame        = 0
local hintText     = ""
local hintFrames   = 0
local scrollY      = 0
local maxScroll    = 0

-- Parameter-list drawer (right-hand panel): pick a param → click a control to
-- assign it (no wiggle). The window GROWS outward by PARAM_PW when the drawer
-- opens (see loop()) so the mockup keeps its size; RW reserves that strip on the
-- right. PARAM_PW is fixed so the grow delta and the reserve match exactly.
local PARAM_PW         = 240
local paramPanelOpen   = false
local RW               = 0
local paramBtnRect     = nil
local paramRects       = {}
local selectedParam    = -1
local selectedParamNm  = ""
local paramFilter      = ""
local paramScroll      = 0
local paramMaxScroll   = 0
local paramCacheKey    = nil
local paramList        = {}

local function sendCmd(s) reaper.SetExtState(SECT, "hud_cmd", s, false) end

local function clamp(v, lo, hi) return math.max(lo, math.min(v, hi)) end

-- Resolve the active domain's plug-in target from the extension's "hud_target"
-- publish ("csTrNum;csFx;bcTrNum;bcFx"; trNum 0 = master, 1-based track, -1 none).
local function resolveTarget()
  local raw = reaper.GetExtState(SECT, "hud_target")
  local csN, csFx, bcN, bcFx = raw:match("^(%-?%d+);(%-?%d+);(%-?%d+);(%-?%d+)$")
  if not csN then return nil end
  local trN, fx
  if activeTab == "cs" then trN, fx = tonumber(csN), tonumber(csFx)
  else                      trN, fx = tonumber(bcN), tonumber(bcFx) end
  if trN < 0 or fx < 0 then return nil end
  local tr = (trN == 0) and reaper.GetMasterTrack(0) or reaper.GetTrack(0, trN - 1)
  if not tr then return nil end
  return tr, fx
end

-- Enumerate the target plug-in's params (cached; names are static per plug-in).
local function getParams(tr, fx)
  local key = tostring(tr) .. ";" .. fx
  if key ~= paramCacheKey then
    paramCacheKey = key
    paramList = {}
    local n = reaper.TrackFX_GetNumParams(tr, fx)
    for p = 0, n - 1 do
      local _, nm = reaper.TrackFX_GetParamName(tr, fx, p, "")
      if nm == nil or nm == "" then nm = "Param " .. p end
      paramList[#paramList + 1] = { p = p, name = nm }
    end
  end
  return paramList
end

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

local SECTION_ORDER = {
  cs = { "Filter", "EQ", "Dynamics", "Gate", "I/O & Channel" },
  bc = { "Bus Comp" },
}
local COLUMN_SECTIONS = {
  cs = { { "Filter", "EQ", "I/O & Channel" }, { "Dynamics", "Gate" } },
  bc = { { "Bus Comp" } },
}

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

------------------------------------------------------------------------
-- ImGui context + draw abstraction. gfx primitives → DrawList.
--   gfx.set/gset    → col(rgb,a)        (0xRRGGBB + 0..1 alpha → 0xRRGGBBAA)
--   gfx.rect        → rect(x,y,w,h,col) (window-local coords)
--   gfx.drawstr     → dtext(x,y,col,s,px)
--   gfx.measurestr  → measure(s,px) → w,h
-- All coords are window-local; OX/OY/WW/WH (set each frame) map to screen.
------------------------------------------------------------------------
local ctx  = reaper.ImGui_CreateContext('Rea-Sixty Learn-HUD')
local font = reaper.ImGui_CreateFont('sans-serif', 0)   -- v0.10: size at push time
reaper.ImGui_Attach(ctx, font)

local dl, OX, OY, WW, WH

local function col(rgb, a)
  local al = floor((a or 1) * 255 + 0.5)
  if al < 0 then al = 0 elseif al > 255 then al = 255 end
  return ((rgb & 0xFFFFFF) << 8) | al
end

local function rect(x, y, w, h, c)
  reaper.ImGui_DrawList_AddRectFilled(dl, OX + x, OY + y, OX + x + w, OY + y + h, c, 0)
end

local function dtext(x, y, c, s, px)
  reaper.ImGui_PushFont(ctx, font, px)
  reaper.ImGui_DrawList_AddText(dl, OX + x, OY + y, c, s)
  reaper.ImGui_PopFont(ctx)
end

local function measure(s, px)
  reaper.ImGui_PushFont(ctx, font, px)
  local w, h = reaper.ImGui_CalcTextSize(ctx, s)
  reaper.ImGui_PopFont(ctx)
  return w, h
end

------------------------------------------------------------------------
-- Render. [ported from gfx, using the draw abstraction]
------------------------------------------------------------------------
local TAB_H = 36

local function fit(s, maxw, px)
  if s == "" then return s end
  local w = measure(s, px)
  if w <= maxw then return s end
  while #s > 1 do
    s = s:sub(1, #s - 1)
    if measure(s .. "\xE2\x80\xA6", px) <= maxw then return s .. "\xE2\x80\xA6" end
  end
  return s
end

local function drawTabs(st)
  rect(0, 0, WW, TAB_H, col(0x171719, 1))
  tabRects = {}
  local function tab(dom, present, short, x)
    local name  = (dom == "cs") and "Channel Strip" or "Bus Compressor"
    local label = name .. ((present and short ~= "") and ("  " .. short) or "")
    local px    = floor(9 * fontScale() + 0.5)   -- ~half the old title size
    local w, h  = measure(label, px); w = w + 28
    local active = (activeTab == dom)
    local rgb    = (dom == "cs") and csRgb() or bcRgb()
    if active then rect(x, 4, w, TAB_H - 6, col(rgb, present and 0.30 or 0.16))
    else           rect(x, 4, w, TAB_H - 6, col(0x242429, 1)) end
    if active then rect(x, 4, w, 3, col(rgb, present and 1 or 0.5)) end
    dtext(x + 12, 4 + floor((TAB_H - 6 - h) / 2), col(rgb, present and 1 or 0.40), label, px)
    tabRects[#tabRects + 1] = { dom = dom, x = x, y = 4, w = w, h = TAB_H - 6 }
    return x + w + 4
  end
  local x = 8
  x = tab("cs", st.csPresent, st.csShort, x)
  x = tab("bc", st.bcPresent, st.bcShort, x)

  -- Active modifier-layer badge (far right).
  local name = (st.layer == 1 and "OPT") or (st.layer == 2 and "CTRL") or "NORM"
  local px   = floor(10 * fontScale() + 0.5)
  local bwm, bhm = measure(name, px)
  local bw = bwm + 20
  local bh = TAB_H - 12
  local bx = WW - bw - 8
  local by = 6
  if st.layer == 0 then
    rect(bx, by, bw, bh, col(0x29292E, 1))
    dtext(bx + 10, by + floor((bh - bhm) / 2), col(0x8890A0, 0.7), name, px)
  else
    local lc = (st.layer == 1) and 0x30C8A0 or 0xC878FF
    rect(bx, by, bw, bh, col(lc, 0.92))
    dtext(bx + 10, by + floor((bh - bhm) / 2), col(0x121214, 1), name, px)
  end

  -- Toggle buttons walking LEFT from the modifier badge: Touch-to-Learn, then
  -- Parameter List. Both also have a right-click-menu entry.
  local rx = bx
  local function rightToggle(lbl, on, onRgb)
    local lw, lh = measure(lbl, px)
    local w = lw + 20
    local tx = rx - w - 6
    if on then
      rect(tx, by, w, bh, col(onRgb, 0.95))
      dtext(tx + 10, by + floor((bh - lh) / 2), col(0x161208, 1), lbl, px)
    else
      rect(tx, by, w, bh, col(0x29292E, 1))
      reaper.ImGui_DrawList_AddRect(dl, OX + tx, OY + by, OX + tx + w, OY + by + bh,
        col(0x4A5060, 1), 0, 0, 1)
      dtext(tx + 10, by + floor((bh - lh) / 2), col(0x9098A4, 0.85), lbl, px)
    end
    rx = tx
    return { x = tx, y = by, w = w, h = bh }
  end

  learnBtnRect = rightToggle("Touch to Learn",
    reaper.GetExtState(SECT, "hud_touch_learn") == "1", 0xE0A838)   -- amber
  paramBtnRect = rightToggle("Parameter List",
    reaper.GetExtState(SECT, "hud_imgui_params") == "1", 0x4A90D8)  -- blue
end

local function hitRect(r, mx, my)
  return r and mx >= r.x and mx <= r.x + r.w and my >= r.y and my <= r.y + r.h
end

local function handleLearnBtnClick(mx, my)
  if hitRect(learnBtnRect, mx, my) then
    local on = (reaper.GetExtState(SECT, "hud_touch_learn") == "1")
    reaper.SetExtState(SECT, "hud_touch_learn", on and "0" or "1", false)
    return true
  end
  return false
end

local function handleParamBtnClick(mx, my)
  if hitRect(paramBtnRect, mx, my) then
    local on = (reaper.GetExtState(SECT, "hud_imgui_params") == "1")
    reaper.SetExtState(SECT, "hud_imgui_params", on and "0" or "1", true)
    return true
  end
  return false
end

local function handleTabClick(mx, my)
  for _, t in ipairs(tabRects) do
    if mx >= t.x and mx <= t.x + t.w and my >= t.y and my <= t.y + t.h then
      if t.dom ~= activeTab then selectedParam = -1 end   -- param idx is per-FX
      activeTab = t.dom
      return true
    end
  end
  return false
end

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
      if selectedParam >= 0 then
        -- Param picked in the list → assign it straight to this control.
        sendCmd("bind;" .. h.idx .. ";" .. selectedParam)
        selectedParam = -1
      elseif learnIdx == h.idx then sendCmd("cancel")
      else                          sendCmd("learn;" .. h.idx) end
      return true
    end
  end
  return false
end

local function handleParamClick(mx, my)
  for _, h in ipairs(paramRects) do
    if mx >= h.x and mx <= h.x + h.w and my >= h.y and my <= h.y + h.h then
      if selectedParam == h.p then
        selectedParam = -1
      else
        selectedParam, selectedParamNm = h.p, h.name
      end
      return true
    end
  end
  return false
end

local function drawRow(c, x, y, rowW, labelW, lineH, rowFont, present, rgb, asn)
  local a        = asn[c.idx]
  local mapped   = (a ~= nil)
  local learning = (c.idx == learnIdx)

  if learning then
    local pulse = 0.22 + 0.22 * math.abs((frame % 50) / 25 - 1)
    rect(x - 3, y - 1, rowW + 6, lineH, col(0xFFFFFF, pulse))
  end

  local labCol, labA
  if mapped       then labCol, labA = rgb,      1.0
  elseif present  then labCol, labA = 0xC6CDD6, 0.95
  else                 labCol, labA = 0x808892, 0.5 end
  dtext(x, y + 1, col(labCol, labA), c.label, rowFont)

  local txt, pCol, pA
  if mapped then
    txt = a.name; if a.inv then txt = txt .. "  inv" end
    pCol, pA = rgb, 1.0
  elseif present then txt, pCol, pA = "\xE2\x80\x94", 0x9097A0, 0.7
  else                txt, pCol, pA = "\xE2\x80\x94", 0x707880, 0.45 end
  dtext(x + labelW, y + 1, col(pCol, pA), fit(txt, rowW - labelW, rowFont), rowFont)

  ctrlRects[#ctrlRects + 1] =
    { idx = c.idx, shape = 1, x = x - 3, y = y - 1, w = rowW + 6, h = lineH }
end

local function renderList(st, asn)
  local domChar = (activeTab == "cs") and "c" or "b"
  local present = (activeTab == "cs") and st.csPresent or st.bcPresent
  local rgb     = (activeTab == "cs") and csRgb() or bcRgb()
  if reaper.GetExtState(SECT, "hud_text_white") == "1" then rgb = 0xFFFFFF end
  local by      = groupControls(domChar)

  local fs       = fontScale()
  local rowFont  = floor(16 * fs + 0.5)
  local headFont = floor(15 * fs + 0.5)

  local M      = 14
  local top    = TAB_H + 10
  local bottom = WH - 8
  if (WW - RW) - 2 * M < 60 or bottom - top < 40 then return end

  local _, rowTH = measure("Ag", rowFont)
  local lineH  = rowTH + floor(5 * fs + 0.5)
  local labelW = 0
  for _, list in pairs(by) do
    for _, c in ipairs(list) do
      local w = measure(c.label, rowFont)
      if w > labelW then labelW = w end
    end
  end
  labelW = labelW + floor(18 * fs + 0.5)
  local paramW = measure("Threshold", rowFont)
  for _, a in pairs(asn) do
    local w = measure(a.name or "", rowFont)
    if w > paramW then paramW = w end
  end
  local colW = labelW + paramW + floor(20 * fs + 0.5)
  local _, headTH = measure("Ag", headFont)
  local headH  = headTH + floor(11 * fs + 0.5)
  local gap    = floor(8 * fs + 0.5)
  local colGap = floor(16 * fs + 0.5)

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

  ctrlRects = {}
  for _, it in ipairs(items) do
    local yy = it.y - scrollY
    if yy + lineH >= top and yy <= bottom then
      if it.kind == "head" then
        dtext(it.x, yy, col(rgb, present and 0.9 or 0.5), it.text, headFont)
        local _, th = measure(it.text, headFont)
        rect(it.x, yy + th + 2, colW, 1, col(rgb, present and 0.30 or 0.18))
      else
        drawRow(it.c, it.x, yy, colW, labelW, lineH, rowFont, present, rgb, asn)
      end
    end
  end
end

-- ===================================================================
-- Settings-schematic face (unified UC1 mockup) — mirrors drawUc1Face_
-- (SettingsScreen.cpp) 1:1: landscape 860×660, BOTH domains at once with the
-- inactive one dimmed, full silk labels + chassis / GR-meter / LCD decoration.
-- Bindable controls come from the published geometry (cap + mapped state via
-- the live assignments); click-to-learn + tooltip ride on top. Non-bindable
-- hardware keys (SOLO/CUT/FINE/SOLO CLR) are intentionally omitted so the
-- bottom breathes. Layout numbers track analysis/settings_face_reference.py.
-- ===================================================================

-- idx → on-face silk label (the settings face uses full words, not the short
-- HUD legends). idx = kUc1Controls array order = the published geometry key.
local FACE_LABEL = {
  [0]="LO-PASS",[1]="HI-PASS",[2]="BELL",[3]="GAIN",[4]="FREQ",[5]="GAIN",
  [6]="FREQ",[7]="Q",[8]="TYPE",[9]="IN",[10]="GAIN",[11]="FREQ",[12]="Q",
  [13]="FREQ",[14]="GAIN",[15]="BELL",[16]="INPUT",[17]="OUTPUT",
  [18]="THR",[19]="MAKE-UP",[20]="ATTACK",[21]="RELEASE",[22]="RATIO",
  [23]="IN",[24]="S/C HPF",[25]="MIX",[26]="FAST ATK",[27]="PEAK",
  [28]="RATIO",[29]="THRESHOLD",[30]="RELEASE",[31]="IN",[32]="RANGE",
  [33]="THRESHOLD",[34]="RELEASE",[35]="HOLD",[36]="EXPAND",[37]="FAST ATK",
  [38]="\xC3\x98",[39]="S/C LISTEN",[40]="IN",
}
-- Per-control position overrides. As of 2026-06-16 the SSL-style layout is
-- baked into the extension's kUc1Controls (single source for the published
-- geometry + the settings schematic), so the HUD draws straight from the
-- published positions — no overrides needed. Kept (empty) as the hook for any
-- future HUD-only nudge.
local FACE_OV = {}
-- Light (white, dark-text) toggle keys like the settings face's IN buttons.
local FACE_LIGHT = { [23]=true, [40]=true }

-- LCD content: real focused-track data. Prefer the extension's "hud_lcd"
-- publish ("seg;line1;line2;line3"); fall back to the last-touched track so it
-- shows real data even before the C++ side ships.
local function lcdLines()
  local raw = reaper.GetExtState(SECT, "hud_lcd")
  if raw ~= "" then
    local s, a, b, c = raw:match("^([^;]*);([^;]*);([^;]*);(.*)$")
    if s then return s, a, b, c end
  end
  local tr = reaper.GetLastTouchedTrack()
  if not tr then return "--", "TRACK", "(no track)", "" end
  local num = reaper.GetMediaTrackInfo_Value(tr, "IP_TRACKNUMBER")
  local _, name = reaper.GetSetMediaTrackInfo_String(tr, "P_NAME", "", false)
  local nch = reaper.GetMediaTrackInfo_Value(tr, "I_NCHAN")
  local seg, top
  if num == -1 then
    seg, top = "M", "MASTER"; if name == "" then name = "Master" end
  else
    seg, top = string.format("%03d", floor(num)), "TRACK"
    if name == "" then name = "Track " .. floor(num) end
  end
  return seg, top, name, (nch and nch >= 2) and "Stereo" or "Mono"
end

local function renderFace(st, asn)
  local DW, DH = geom.w or 860, geom.h or 660
  local M, top = 8, TAB_H + 6
  local availW, availH = (WW - RW) - 2 * M, WH - top - M
  if availW < 60 or availH < 60 then return end
  local scale = math.min(availW / DW, availH / DH)
  local ox = M + (availW - DW * scale) / 2
  local oy = top + (availH - DH * scale) / 2
  local function X(v) return ox + v * scale end
  local function Y(v) return oy + v * scale end

  local whiteTxt = (reaper.GetExtState(SECT, "hud_text_white") == "1")
  local capCS, capBC = csRgb(), bcRgb()
  local brightDom = (activeTab == "cs") and "c" or "b"
  local function da(dom)              -- dim alpha: inactive domain fades back
    if dom == nil then return 1.0 end
    return (dom == brightDom) and 1.0 or 0.30
  end
  local function presentOf(dom)
    if dom == "c" then return st.csPresent elseif dom == "b" then return st.bcPresent end
    return true
  end

  local lf = math.max(7, floor(11 * scale + 0.5))   -- control / label font
  local sf = math.max(8, floor(12 * scale + 0.5))   -- section-header font
  local mx, my = reaper.ImGui_GetMousePos(ctx)
  local lmx, lmy = mx - OX, my - OY
  local tipSlot, tipParam
  ctrlRects = {}

  -- design-space draw helpers ------------------------------------------
  local function dRect(x, y, w, h, fill, outl, rounding)
    local rd = (rounding or 3) * scale
    reaper.ImGui_DrawList_AddRectFilled(dl, OX + X(x), OY + Y(y), OX + X(x + w), OY + Y(y + h), fill, rd)
    if outl then reaper.ImGui_DrawList_AddRect(dl, OX + X(x), OY + Y(y), OX + X(x + w), OY + Y(y + h), outl, rd, 0, math.max(1, scale)) end
  end
  local function dLine(x1, y1, x2, y2, c, w)
    reaper.ImGui_DrawList_AddLine(dl, OX + X(x1), OY + Y(y1), OX + X(x2), OY + Y(y2), c, math.max(1, (w or 1) * scale))
  end
  local function dTextC(cx, cy, c, s, px)
    local tw, th = measure(s, px); dtext(X(cx) - tw / 2, Y(cy) - th / 2, c, s, px)
  end
  local function dTextL(x, y, c, s, px) dtext(X(x), Y(y), c, s, px) end
  local function dTextLV(x, cy, c, s, px)   -- left-aligned, vertically centred on cy
    local _, th = measure(s, px); dtext(X(x), Y(cy) - th / 2, c, s, px)
  end
  local function dTextRB(xr, yb, c, s, px)  -- right edge xr, bottom edge yb
    local tw, th = measure(s, px); dtext(X(xr) - tw, Y(yb) - th, c, s, px)
  end

  -- bindable knob ------------------------------------------------------
  local function knob(idx, cx, cy, rD, cap, dom)
    local f = da(dom); local present = presentOf(dom)
    local mapped = idx and asn[idx] ~= nil
    local learning = idx and idx == learnIdx
    local capCol = (cap and cap > 0) and ((cap >> 8) & 0xFFFFFF) or ((dom == "b") and capBC or capCS)
    local r = rD * scale; local sx, sy = X(cx), Y(cy)
    reaper.ImGui_DrawList_AddCircleFilled(dl, OX + sx, OY + sy, r, col(0x14181E, f))
    reaper.ImGui_DrawList_AddCircle(dl, OX + sx, OY + sy, r, col(0x4A5060, f), 0, math.max(1, r * 0.10))
    local capA = mapped and 1.0 or (present and 0.7 or 0.4)
    reaper.ImGui_DrawList_AddCircleFilled(dl, OX + sx, OY + sy, r * 0.78, col(capCol, capA * f))
    if mapped then reaper.ImGui_DrawList_AddCircle(dl, OX + sx, OY + sy, r + 2 * scale, col(capCol, 0.9 * f), 0, math.max(1, r * 0.12)) end
    if learning then
      local p = 0.4 + 0.4 * math.abs((frame % 50) / 25 - 1)
      reaper.ImGui_DrawList_AddCircle(dl, OX + sx, OY + sy, r + 3 * scale, col(0xFFFFFF, p), 0, math.max(1, r * 0.14))
    end
    reaper.ImGui_DrawList_AddLine(dl, OX + sx, OY + sy - r * 0.85, OX + sx, OY + sy - r * 0.45, col(0xE8E8E8, (present and 0.9 or 0.5) * f), math.max(1, r * 0.13))
    local lbl = idx and FACE_LABEL[idx]
    if lbl then dTextC(cx, cy + rD + 12, col(0xB8BCC4, (present and 0.95 or 0.6) * f), lbl, lf) end
    if idx then
      ctrlRects[#ctrlRects + 1] = { idx = idx, shape = 0, x = sx, y = sy, r = r }
      local dx, dy = lmx - sx, lmy - sy
      if dx * dx + dy * dy <= (r + 3) * (r + 3) then
        local g = geom.ctrl[idx]; tipSlot = (g and g.label) or lbl
        tipParam = mapped and (asn[idx].name .. (asn[idx].inv and "  (inverted)" or "")) or nil
      end
    end
  end

  -- bindable / labelled button ----------------------------------------
  local function btn(idx, x, y, wD, hD, label, dom, light)
    local f = da(dom); local present = presentOf(dom)
    local mapped = idx and asn[idx] ~= nil
    local learning = idx and idx == learnIdx
    local sx, sy = X(x), Y(y); local sw, sh = wD * scale, hD * scale
    local capCol = (dom == "b") and capBC or capCS
    local fill, txtC, outl
    if light then
      fill = col(0xE0E0E0, (present and 1 or 0.6) * f)
      txtC = col(0x303338, f)
      outl = col(mapped and capCol or 0x808088, f)
    else
      fill = mapped and col(capCol, 0.30 * f) or col(0x252A33, f)
      local tc = mapped and (whiteTxt and 0xFFFFFF or capCol) or (present and 0xC0C4CC or 0x707880)
      txtC = col(tc, (present and 1 or 0.6) * f)
      outl = col(mapped and capCol or 0x4A5060, f)
    end
    reaper.ImGui_DrawList_AddRectFilled(dl, OX + sx, OY + sy, OX + sx + sw, OY + sy + sh, fill, 3 * scale)
    reaper.ImGui_DrawList_AddRect(dl, OX + sx, OY + sy, OX + sx + sw, OY + sy + sh, outl, 3 * scale, 0, math.max(1, scale))
    if learning then
      local p = 0.4 + 0.4 * math.abs((frame % 50) / 25 - 1)
      reaper.ImGui_DrawList_AddRect(dl, OX + sx - 2, OY + sy - 2, OX + sx + sw + 2, OY + sy + sh + 2, col(0xFFFFFF, p), 3 * scale, 0, math.max(1, 2 * scale))
    end
    local shown = fit(label, sw - 4, lf)
    local tw, th = measure(shown, lf)
    dtext(sx + (sw - tw) / 2, sy + (sh - th) / 2, txtC, shown, lf)
    if idx then
      ctrlRects[#ctrlRects + 1] = { idx = idx, shape = 1, x = sx, y = sy, w = sw, h = sh }
      if lmx >= sx and lmx <= sx + sw and lmy >= sy and lmy <= sy + sh then
        local g = geom.ctrl[idx]; tipSlot = (g and g.label) or label
        tipParam = mapped and (asn[idx].name .. (asn[idx].inv and "  (inverted)" or "")) or nil
      end
    end
  end

  local function section(x, y, text, dom) dTextL(x, y, col(0x9CA0AA, da(dom)), text, sf) end
  local function decoKnob(cx, cy, rD, label)
    local r = rD * scale; local sx, sy = X(cx), Y(cy)
    reaper.ImGui_DrawList_AddCircleFilled(dl, OX + sx, OY + sy, r, col(0x14181E, 1))
    reaper.ImGui_DrawList_AddCircle(dl, OX + sx, OY + sy, r, col(0x4A5060, 1), 0, math.max(1, r * 0.10))
    reaper.ImGui_DrawList_AddCircleFilled(dl, OX + sx, OY + sy, r * 0.78, col(0x6A707C, 0.85))
    reaper.ImGui_DrawList_AddLine(dl, OX + sx, OY + sy - r * 0.85, OX + sx, OY + sy - r * 0.45, col(0xE8E8E8, 0.8), math.max(1, r * 0.13))
    dTextC(cx, cy + rD + 12, col(0xB8BCC4, 0.85), label, lf)
  end
  local function decoBtn(x, y, wD, hD, label)
    dRect(x, y, wD, hD, col(0x252A33, 1), col(0x4A5060, 1), 3)
    local tw, th = measure(label, lf)
    dtext(X(x) + (wD * scale - tw) / 2, Y(y) + (hD * scale - th) / 2, col(0xC0C4CC, 1), label, lf)
  end

  -- ===== Chassis + column frames =====
  dRect(4, 4, DW - 8, DH - 8, col(0x14181E, 0.0), col(0x2A3038, 1), 8)
  dRect(12, 12, 230, DH - 24, col(0x1A1E24, da("c")), col(0x2A3038, da("c")), 6)         -- left (EQ)
  dRect(250, 12, 360, 420, col(0x1A1E24, da("b")), col(0x2A4870, da("b")), 6)            -- centre BC
  dRect(618, 12, 230, DH - 24, col(0x1A1E24, da("c")), col(0x2A3038, da("c")), 6)        -- right (Dyn/Chan)
  dRect(250, 440, 360, 208, col(0x1A1E24, 1), col(0x903030, 1), 6)                       -- CCP (neutral)

  -- ===== Section + side labels =====
  local fC = da("c")
  section(26, 104, "FILTERS", "c")
  dLine(70, 122, 234, 122, col(0x9DA2AC, 0.6 * fC), 1.0)   -- match band divider lines
  -- Band labels aligned to each band's FREQ knob height.
  dTextLV(26, 188, col(0xB8BCC4, fC), "HF", lf)
  dTextLV(26, 288, col(0xB8BCC4, fC), "HMF", lf)
  dTextLV(26, 379, col(0xB8BCC4, fC), "EQ", lf)   -- left of the HMF|LMF (Type/In) divider
  dTextLV(26, 458, col(0xB8BCC4, fC), "LMF", lf)
  dTextLV(26, 558, col(0xB8BCC4, fC), "LF", lf)
  section(632, 26, "COMPRESSOR", "c")
  -- GATE / EXPANDER — right-aligned, bottom flush with the FAST ATK button (510).
  dTextRB(834, 492, col(0x9CA0AA, fC), "GATE /", sf)
  dTextRB(834, 510, col(0x9CA0AA, fC), "EXPANDER", sf)
  section(632, 518, "CHANNEL", "c")
  dTextC(430, 22, col(0x9CA0AA, da("b")), "BUS COMPRESSOR", sf)

  -- ===== BC GR meter (BC domain) =====
  local fB = da("b")
  local mw, mh = 196, 80
  local mxg, myg = 250 + (360 - mw) / 2, 44
  dRect(mxg, myg, mw, mh, col(0x141416, fB), col(0x282A2E, fB), 4)
  dRect(mxg + 4, myg + 4, mw - 8, mh - 8, col(0x080C12, fB), col(0x444A55, fB), 2)
  local mcx, mcy, ra = mxg + mw / 2, myg + mh - 3, 70
  local a0, a1 = math.rad(-130), math.rad(-50)
  local function dBtoA(dbv) return a0 + (dbv / 20) * (a1 - a0) end
  for _, t in ipairs({ { 0, "0" }, { 5, "5" }, { 10, "10" }, { 15, "15" }, { 20, "20" } }) do
    local a = dBtoA(t[1])
    dLine(mcx + math.cos(a) * ra, mcy + math.sin(a) * ra, mcx + math.cos(a) * (ra - 8), mcy + math.sin(a) * (ra - 8), col(0x4499DD, fB), 1.6)
    dTextC(mcx + math.cos(a) * (ra - 16), mcy + math.sin(a) * (ra - 16), col(0x4499DD, fB), t[2], lf)
  end
  local aN = dBtoA(7)
  dLine(mcx, mcy, mcx + math.cos(aN) * (ra - 4), mcy + math.sin(aN) * (ra - 4), col(0x4499DD, fB), 2.0)
  dTextC(mcx, myg + mh - 12, col(0x4499DD, fB), "GR", lf)

  -- ===== Comp GR LED meter — 2 dot columns + dB number, in col2 (cx=774) =====
  for i, s in ipairs({ "20", "14", "10", "6", "3" }) do
    local ly = 220 + (i - 1) * 13
    dTextC(760, ly, col(0x808088, fC), s, lf)
    reaper.ImGui_DrawList_AddCircleFilled(dl, OX + X(780), OY + Y(ly), math.max(1, 3 * scale), col(0x9A5A2A, fC))
    reaper.ImGui_DrawList_AddCircleFilled(dl, OX + X(794), OY + Y(ly), math.max(1, 3 * scale), col(0x2E8040, fC))
  end

  -- ===== Central Control Panel: 7-seg + LCD (real data) + buttons =====
  local seg, l1, l2, l3 = lcdLines()
  local kCcpY = 440
  dRect(264, kCcpY + 14, 56, 30, col(0x1A0408, 1), col(0x401818, 1), 3)
  dTextC(292, kCcpY + 28, col(0xFF3030, 1), seg, math.max(9, floor(18 * scale + 0.5)))
  local lcdX, lcdW = 328, 222; local lcdCx = lcdX + lcdW / 2
  dRect(lcdX, kCcpY + 12, lcdW, 76, col(0x05080C, 1), col(0x444A55, 1), 3)
  dTextC(lcdCx, kCcpY + 26, col(0x808088, 1), l1, lf)
  dTextC(lcdCx, kCcpY + 46, col(0xE0E0E0, 1), fit(l2, lcdW * scale - 8, math.max(9, floor(13 * scale + 0.5))), math.max(9, floor(13 * scale + 0.5)))
  dTextC(lcdCx, kCcpY + 66, col(0x4488DD, 1), l3, lf)
  do
    local bw, bh, gap = 80, 22, 20; local total = 2 * bw + gap
    local x0, y0 = 250 + (360 - total) / 2, kCcpY + 100
    decoBtn(x0, y0, bw, bh, "360"); decoBtn(x0 + bw + gap, y0, bw, bh, "MAGNIFY")
  end
  decoKnob(390, kCcpY + 145, 18, "CS Encoder")
  decoKnob(470, kCcpY + 145, 18, "BC Encoder")
  dTextC(430, kCcpY + 208 - 14, col(0x707880, 1), "Rea-Sixty", lf)

  -- ===== EQ band divider lines — stepped (horizontal · 45° · horizontal) to
  -- follow the staggered 2-column knob layout, like the SSL 4000E silk. =====
  do
    local dc = col(0x9DA2AC, 0.6 * da("c"))
    local th = math.max(1, scale)
    local function poly(pts)
      for i = 1, #pts - 1 do
        reaper.ImGui_DrawList_AddLine(dl, OX + X(pts[i][1]), OY + Y(pts[i][2]),
          OX + X(pts[i + 1][1]), OY + Y(pts[i + 1][2]), dc, th)
      end
    end
    local gxR, fxR = 102, 182          -- GAIN col right edge / FREQ col right edge (c2=162)
    poly({ { 62, 210 }, { gxR, 210 }, { gxR + 28, 238 }, { fxR, 238 } })  -- HF | HMF (down)
    poly({ { 62, 379 }, { 143, 379 } })                                   -- HMF | LMF → IN/TYPE
    poly({ { 62, 548 }, { gxR, 548 }, { gxR + 40, 508 }, { fxR, 508 } })  -- LMF | LF (up)
  end

  -- ===== Bindable controls from the published geometry =====
  for idx, g in pairs(geom.ctrl) do
    local ov = FACE_OV[idx]
    if g.shape == 0 then
      knob(idx, (ov and ov.cx) or g.cx, (ov and ov.cy) or g.cy, g.r, g.cap, g.dom)
    else
      local x = (ov and ov.x) or g.cx
      local y = (ov and ov.y) or g.cy
      local w = (ov and ov.w) or g.w
      local h = (ov and ov.h) or g.h
      btn(idx, x, y, w, h, FACE_LABEL[idx] or g.legend, g.dom, FACE_LIGHT[idx])
    end
  end

  if tipSlot then
    local t = tipSlot
    if tipParam then t = t .. "  \xE2\x80\x94  " .. tipParam end
    reaper.ImGui_SetTooltip(ctx, t)
  end
end

-- ===================================================================
-- Parameter-list drawer. Right-hand panel listing every param of the active
-- domain's plug-in; click a row to arm it, then click a control to bind it.
-- Type-to-filter (no InputText widget — fits the DrawList draw model + dodges
-- the standalone-focus InputText trap). Mapped params get a green dot.
-- ===================================================================
local function renderParamPanel(st, asn)
  local PW = RW
  local x0  = WW - PW
  local top = TAB_H
  local fs  = fontScale()
  local hf  = floor(13 * fs + 0.5)
  local rf  = floor(14 * fs + 0.5)
  local pad = floor(8 * fs + 0.5)

  rect(x0, top, PW, WH - top, col(0x16171A, 0.97))
  rect(x0, top, 2, WH - top, col(0x303440, 1))    -- left edge seam

  paramRects = {}
  local tr, fx = resolveTarget()
  if not tr then
    dtext(x0 + pad, top + pad, col(0x808890, 0.9),
      fit("No editable plug-in on the focused track", PW - 2 * pad, hf), hf)
    return
  end

  local rgb = (activeTab == "cs") and csRgb() or bcRgb()
  local _, fxname = reaper.TrackFX_GetFXName(tr, fx, "")
  dtext(x0 + pad, top + pad, col(0xC8CCD4, 1), fit(fxname or "", PW - 2 * pad, hf), hf)

  -- Filter line (type to filter, Backspace to delete — handled in loop()).
  local fy = top + pad + floor(19 * fs + 0.5)
  local ftxt = (paramFilter ~= "") and ("Filter: " .. paramFilter)
                                    or  "Type to filter\xE2\x80\xA6"
  dtext(x0 + pad, fy, col(0x8890A0, paramFilter ~= "" and 1 or 0.6),
    fit(ftxt, PW - 2 * pad, hf), hf)

  -- Active-domain mapped param NAMES (for the green-dot tint). hud_assign only
  -- carries names, so we match on name — exact enough for a visual hint.
  local mappedNames = {}
  for idx, a in pairs(asn) do
    local g = geom.ctrl[idx]
    if g and ((activeTab == "cs" and g.dom == "c")
           or (activeTab == "bc" and g.dom == "b")) then
      mappedNames[a.name] = true
    end
  end

  local params = getParams(tr, fx)
  local flo    = paramFilter:lower()
  local rows   = {}
  for _, pr in ipairs(params) do
    if flo == "" or pr.name:lower():find(flo, 1, true) then rows[#rows + 1] = pr end
  end

  local _, rowTH  = measure("Ag", rf)
  local lineH     = rowTH + floor(5 * fs + 0.5)
  local listTop   = fy + floor(20 * fs + 0.5)
  local listBot   = WH - pad
  paramMaxScroll  = math.max(0, #rows * lineH - (listBot - listTop))
  paramScroll     = clamp(paramScroll, 0, paramMaxScroll)

  local y = listTop - paramScroll
  for _, pr in ipairs(rows) do
    if y + lineH >= listTop and y <= listBot then
      local sel    = (pr.p == selectedParam)
      local mapped = mappedNames[pr.name]
      if sel then rect(x0 + 2, y - 1, PW - 4, lineH, col(rgb, 0.34)) end
      local tc = sel and 0xFFFFFF or (mapped and 0x78C898 or 0xC0C4CC)
      dtext(x0 + pad, y + 1, col(tc, 1), fit(pr.name, PW - 2 * pad - 12, rf), rf)
      if mapped then
        dtext(x0 + PW - pad - 8, y + 1, col(0x78C898, 1), "\xE2\x97\x8F", rf)
      end
      paramRects[#paramRects + 1] =
        { p = pr.p, name = pr.name, x = x0, y = y, w = PW, h = lineH }
    end
    y = y + lineH
  end
end

local function render()
  TAB_H = floor(22 * fontScale() + 0.5) + 16

  local st  = readState()
  local asn = readAssign()

  -- Param-list drawer: reserve a fixed right strip. The window itself grows by
  -- PARAM_PW when the drawer opens (loop()), so the mockup keeps its size.
  paramPanelOpen = (reaper.GetExtState(SECT, "hud_imgui_params") == "1")
  if paramPanelOpen then
    RW = PARAM_PW
  else
    RW, paramRects, selectedParam = 0, {}, -1
  end

  frame = frame + 1
  local hl = reaper.GetExtState(SECT, "hud_learn")
  learnIdx = (hl ~= "" and tonumber(hl)) or -1
  local hint = reaper.GetExtState(SECT, "hud_hint")
  if hint ~= "" then hintText = hint; hintFrames = 90
    reaper.SetExtState(SECT, "hud_hint", "", false) end

  if st.focusDom == "c" or st.focusDom == "b" then
    if st.focusDom ~= lastFocusDom then
      local newTab = (st.focusDom == "c") and "cs" or "bc"
      if newTab ~= activeTab then selectedParam = -1 end   -- param idx is per-FX
      activeTab = newTab
      lastFocusDom = st.focusDom
    end
  else
    lastFocusDom = "n"
  end

  drawTabs(st)

  local nCtrl = 0; for _ in pairs(geom.ctrl) do nCtrl = nCtrl + 1 end

  if not geom or nCtrl == 0 then
    dtext(10, TAB_H + 12, col(0x808890, 0.9),
      "Waiting for surface geometry\xE2\x80\xA6", floor(15 * fontScale() + 0.5))
    return
  end

  local present = (activeTab == "cs") and st.csPresent or st.bcPresent

  if reaper.GetExtState(SECT, "hud_imgui_view") == "mockup" then
    -- Unified face draws the whole surface (both domains, inactive dimmed) —
    -- always shown; absent plug-ins just render greyed, no "no plug-in" gate.
    renderFace(st, asn)
  elseif not present then
    ctrlRects = {}
    local px = floor(17 * fontScale() + 0.5)
    local msg = (activeTab == "cs" and "No Channel-Strip" or "No Bus-Comp")
              .. " plug-in on the focused track"
    local tw, th = measure(msg, px)
    dtext((WW - tw) / 2, (WH - th) / 2, col(0x9097A0, 0.9), msg, px)
  else
    renderList(st, asn)
  end

  if paramPanelOpen then renderParamPanel(st, asn) end

  local function banner(msg, bgRgb, bgA, fgRgb)
    local px = floor(14 * fontScale() + 0.5)
    local tw, th = measure(msg, px)
    local bw, bh = tw + 20, th + 8
    local bx, by = (WW - RW - bw) / 2, TAB_H + 4
    rect(bx, by, bw, bh, col(bgRgb, bgA))
    dtext(bx + 10, by + 4, col(fgRgb, 1), msg, px)
  end

  if selectedParam >= 0 then
    banner("Assigning " .. selectedParamNm ..
           "  \xE2\x80\x94  click a control to bind it   (Esc to cancel)",
           0x10202C, 0.9, 0x70C0FF)
  elseif learnIdx < 0 and reaper.GetExtState(SECT, "hud_touch_learn") == "1" then
    banner("Touch-to-Learn  \xE2\x80\x94  move a UC1 control to arm it, "
           .. "then wiggle a plug-in parameter", 0x101A14, 0.88, 0x70D0A0)
  elseif learnIdx >= 0 then
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
-- Right-click menu (ImGui popup). Dock is handled by ReaImGui's built-in
-- title-bar context menu, so it's no longer in here.
------------------------------------------------------------------------
local POPUP = "##hud_ctx"

local function drawContextMenu()
  -- Roomier popup — WindowPadding read at BeginPopup, so push first; pop always.
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_WindowPadding(), 10, 8)
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_ItemSpacing(), 10, 7)
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_FramePadding(), 7, 4)
  if not reaper.ImGui_BeginPopup(ctx, POPUP) then
    reaper.ImGui_PopStyleVar(ctx, 3)
    return
  end

  local mockup = (reaper.GetExtState(SECT, "hud_imgui_view") == "mockup")
  if reaper.ImGui_BeginMenu(ctx, "View") then
    if reaper.ImGui_MenuItem(ctx, "List", nil, not mockup) then
      reaper.SetExtState(SECT, "hud_imgui_view", "list", true)
    end
    if reaper.ImGui_MenuItem(ctx, "Mockup", nil, mockup) then
      reaper.SetExtState(SECT, "hud_imgui_view", "mockup", true)
    end
    reaper.ImGui_EndMenu(ctx)
  end

  local pPanel = (reaper.GetExtState(SECT, "hud_imgui_params") == "1")
  if reaper.ImGui_MenuItem(ctx, "Parameter list", nil, pPanel) then
    reaper.SetExtState(SECT, "hud_imgui_params", pPanel and "0" or "1", true)
  end

  -- Touch-to-Learn: move a UC1 control to arm it (instead of clicking the
  -- mockup). Session-only (persist=false) so it never leaves the surface inert
  -- across restarts; also cleared on HUD shutdown.
  local touchLearn = (reaper.GetExtState(SECT, "hud_touch_learn") == "1")
  if reaper.ImGui_MenuItem(ctx, "Touch to Learn (UC1)", nil, touchLearn) then
    reaper.SetExtState(SECT, "hud_touch_learn", touchLearn and "0" or "1", false)
  end

  local white = (reaper.GetExtState(SECT, "hud_text_white") == "1")
  if reaper.ImGui_BeginMenu(ctx, "Text colour") then
    if reaper.ImGui_MenuItem(ctx, "CS / BC colour", nil, not white) then
      reaper.SetExtState(SECT, "hud_text_white", "0", true)
    end
    if reaper.ImGui_MenuItem(ctx, "White", nil, white) then
      reaper.SetExtState(SECT, "hud_text_white", "1", true)
    end
    reaper.ImGui_EndMenu(ctx)
  end

  reaper.ImGui_Separator(ctx)
  if reaper.ImGui_MenuItem(ctx, "Close HUD") then
    reaper.SetExtState(SECT, RUNKEY, "0", false)
  end
  reaper.ImGui_EndPopup(ctx)
  reaper.ImGui_PopStyleVar(ctx, 3)   -- matches the 3 pushes before BeginPopup
end

------------------------------------------------------------------------
-- Geometry persistence (own keys → coexists with gfx HUD).
------------------------------------------------------------------------
local DEF_W, DEF_H = 920, 720
local function loadRect()
  local raw = reaper.GetExtState(SECT, "hud_imgui_rect")
  local x, y, w, h = raw:match("^(%-?%d+),(%-?%d+),(%d+),(%d+)$")
  if x then return tonumber(x), tonumber(y), tonumber(w), tonumber(h) end
  return nil
end
local restore_x, restore_y, restore_w, restore_h = loadRect()
local first_frame = true
local last_x, last_y, last_w, last_h
local lastParamPanelOpen = nil   -- edge-detect drawer toggle → grow/shrink window

------------------------------------------------------------------------
-- Main loop.
------------------------------------------------------------------------
local WFLAGS = reaper.ImGui_WindowFlags_NoScrollbar()
  | reaper.ImGui_WindowFlags_NoScrollWithMouse()

local shutdown
local function loop()
  if reaper.GetExtState(SECT, RUNKEY) ~= "1" then return shutdown() end

  if first_frame then
    if restore_x then
      reaper.ImGui_SetNextWindowPos(ctx, restore_x, restore_y)
      reaper.ImGui_SetNextWindowSize(ctx, restore_w, restore_h)
    else
      reaper.ImGui_SetNextWindowSize(ctx, DEF_W, DEF_H)
    end
    first_frame = false
  end

  -- Grow the window outward when the param drawer opens (and shrink back when it
  -- closes) so the mockup keeps its size instead of being squeezed. Edge-detect
  -- the toggle from any source (button or menu). On startup adopt the current
  -- state without resizing — the saved rect already matches it.
  local paramOpenNow = (reaper.GetExtState(SECT, "hud_imgui_params") == "1")
  if lastParamPanelOpen == nil then
    lastParamPanelOpen = paramOpenNow
  elseif paramOpenNow ~= lastParamPanelOpen then
    if last_w and last_h then
      local nw = paramOpenNow and (last_w + PARAM_PW)
                              or  math.max(200, last_w - PARAM_PW)
      reaper.ImGui_SetNextWindowSize(ctx, nw, last_h)
    end
    lastParamPanelOpen = paramOpenNow
  end

  -- Edge-to-edge content (we draw our own margins, like the gfx HUD).
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_WindowPadding(), 0, 0)
  reaper.ImGui_PushStyleColor(ctx, reaper.ImGui_Col_WindowBg(), col(0x1F1F21, 1))

  local visible, open = reaper.ImGui_Begin(ctx, 'Rea-Sixty Learn-HUD###hud', true, WFLAGS)
  if visible then
    dl     = reaper.ImGui_GetWindowDrawList(ctx)
    OX, OY = reaper.ImGui_GetCursorScreenPos(ctx)
    WW, WH = reaper.ImGui_GetContentRegionAvail(ctx)

    -- Input — only when this window is hovered (popups capture otherwise).
    if reaper.ImGui_IsWindowHovered(ctx) then
      local mx, my = reaper.ImGui_GetMousePos(ctx)
      local lx, ly = mx - OX, my - OY
      if reaper.ImGui_IsMouseClicked(ctx, 1) and ly >= 0 then
        -- Content-area right-click only; the title bar keeps ReaImGui's own
        -- dock menu (negative ly = title bar, since OY is the content origin).
        reaper.ImGui_OpenPopup(ctx, POPUP)
      elseif reaper.ImGui_IsMouseClicked(ctx, 0) then
        -- Top toggle buttons first, then param-panel row, then tab, then control.
        if handleLearnBtnClick(lx, ly) or handleParamBtnClick(lx, ly) then
        elseif paramPanelOpen and lx >= WW - RW and ly >= TAB_H then
          handleParamClick(lx, ly)
        elseif not handleTabClick(lx, ly) then
          handleControlClick(lx, ly)
        end
      end
      local wheel = reaper.ImGui_GetMouseWheel(ctx)
      if wheel ~= 0 then
        if paramPanelOpen and lx >= WW - RW then
          paramScroll = clamp(paramScroll - wheel * 40, 0, paramMaxScroll)
        else
          scrollY = scrollY - wheel * 40
        end
      end
    end

    -- Type-to-filter the param list (no InputText widget). Drains the ImGui
    -- char queue + Backspace while the drawer is open and the window focused.
    if paramPanelOpen and reaper.ImGui_IsWindowFocused(ctx)
       and reaper.ImGui_GetInputQueueCharacter then
      local i = 0
      while true do
        local ok, ch = reaper.ImGui_GetInputQueueCharacter(ctx, i)
        if not ok then break end
        if ch >= 32 and ch < 127 then paramFilter = paramFilter .. string.char(ch) end
        i = i + 1
      end
      if reaper.ImGui_IsKeyPressed(ctx, reaper.ImGui_Key_Backspace())
         and #paramFilter > 0 then
        paramFilter = paramFilter:sub(1, #paramFilter - 1)
      end
    end

    if reaper.ImGui_IsKeyPressed(ctx, reaper.ImGui_Key_Escape()) then
      if selectedParam >= 0 then selectedParam = -1
      elseif paramFilter ~= "" then paramFilter = ""
      elseif learnIdx >= 0 then sendCmd("cancel") end
    end

    refreshGeom()
    render()
    drawContextMenu()

    -- Persist geometry on change.
    local px, py = reaper.ImGui_GetWindowPos(ctx)
    local pw, ph = reaper.ImGui_GetWindowSize(ctx)
    px, py, pw, ph = floor(px), floor(py), floor(pw), floor(ph)
    if px ~= last_x or py ~= last_y or pw ~= last_w or ph ~= last_h then
      last_x, last_y, last_w, last_h = px, py, pw, ph
      reaper.SetExtState(SECT, "hud_imgui_rect",
        string.format("%d,%d,%d,%d", px, py, pw, ph), true)
    end
  end
  reaper.ImGui_End(ctx)            -- UNCONDITIONAL (see reaimgui_v010_pairing_rules)
  reaper.ImGui_PopStyleColor(ctx, 1)
  reaper.ImGui_PopStyleVar(ctx, 1)

  if open then reaper.defer(loop) else shutdown() end
end

shutdown = function()
  reaper.SetExtState(SECT, RUNKEY, "0", false)
  reaper.SetExtState(SECT, "hud_touch_learn", "0", false)   -- don't leave UC1 inert
  setToggle(false)
end

reaper.atexit(shutdown)
reaper.defer(loop)
