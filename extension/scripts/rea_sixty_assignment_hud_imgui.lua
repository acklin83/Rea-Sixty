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

-- Own text-size key (hud_imgui_font) with a smaller default than the gfx HUD:
-- ImGui renders crisper/larger at the same nominal px, so the gfx HUD's 1.5
-- came out way too big. 1.0 ≈ the focused panel's feel. Decoupled from the gfx
-- HUD's shared hud_font on purpose.
local function fontScale()
  local v = num("hud_imgui_font", 1.0)
  if v < 0.8 then v = 0.8 elseif v > 2.6 then v = 2.6 end
  return v
end

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
local ctrlRects    = {}
local learnIdx     = -1
local frame        = 0
local hintText     = ""
local hintFrames   = 0
local scrollY      = 0
local maxScroll    = 0

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
    local name  = (dom == "cs") and "CS" or "BC"
    local label = name .. ((present and short ~= "") and ("  " .. short) or "")
    local px    = floor(17 * fontScale() + 0.5)
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
  local px   = floor(13 * fontScale() + 0.5)
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
end

local function handleTabClick(mx, my)
  for _, t in ipairs(tabRects) do
    if mx >= t.x and mx <= t.x + t.w and my >= t.y and my <= t.y + t.h then
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
      if learnIdx == h.idx then sendCmd("cancel")
      else                      sendCmd("learn;" .. h.idx) end
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
  if WW - 2 * M < 60 or bottom - top < 40 then return end

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

-- Tiny SSL silk glyphs: filter slope (lpf/hpf) or EQ bell, drawn with line
-- segments beside a control. Uses the module draw-list + window origin.
local function eqGlyph(kind, gx, gy, sz, c)
  local function L(x1, y1, x2, y2)
    reaper.ImGui_DrawList_AddLine(dl, OX + x1, OY + y1, OX + x2, OY + y2, c, 1.3)
  end
  if kind == "lpf" then       -- flat, then slope down-right
    L(gx, gy, gx + sz * 0.55, gy); L(gx + sz * 0.55, gy, gx + sz, gy + sz * 0.6)
  elseif kind == "hpf" then   -- slope up, then flat
    L(gx, gy + sz * 0.6, gx + sz * 0.45, gy); L(gx + sz * 0.45, gy, gx + sz, gy)
  else                        -- bell hump
    L(gx, gy + sz * 0.5, gx + sz * 0.35, gy)
    L(gx + sz * 0.35, gy, gx + sz * 0.65, gy)
    L(gx + sz * 0.65, gy, gx + sz, gy + sz * 0.5)
  end
end

-- Physical UC1 mockup for the active domain. SHARED scale for CS + BC (from the
-- full 860×660 face) so a knob is the same size in either view — BC just uses
-- less of the window. Param name fixed under each control (asn name = bound
-- param, custom-label aware, like the list); canonical slot name (c.label) in
-- the hover tooltip. Unmapped → slot name shown fixed (dimmed).
local function renderMockup(st, asn)
  local domChar = (activeTab == "cs") and "c" or "b"
  local present = (activeTab == "cs") and st.csPresent or st.bcPresent
  local rgb     = (activeTab == "cs") and csRgb() or bcRgb()
  if reaper.GetExtState(SECT, "hud_text_white") == "1" then rgb = 0xFFFFFF end

  -- This domain's controls (shallow copies). CS: compress the dead centre band
  -- (where the BC + meters sit on the hardware) so the strip fills a portrait
  -- window — left cluster stays, the gap incl. IN/FDR is squeezed, the right
  -- cluster shifts left. cxmap is identity for BC.
  local function cxmap(cx)
    if domChar ~= "c" then return cx end
    local L, Rs, NG = 250, 600, 150
    if cx <= L then return cx end
    if cx >= Rs then return cx - (Rs - L - NG) end
    return L + (cx - L) / (Rs - L) * NG
  end
  local function ecx(c)   -- nudge IN/FDR toward the centre
    local x = cxmap(c.cx)
    if c.label == "Input Trim" then x = x + 28
    elseif c.label == "Fader Level" then x = x - 28 end
    return x
  end
  local items = {}
  for _, c in pairs(geom.ctrl) do
    if c.dom == domChar then
      items[#items + 1] = { idx = c.idx, shape = c.shape, cx = ecx(c), cy = c.cy,
        r = c.r, w = c.w, h = c.h, dom = c.dom, label = c.label, sec = c.sec,
        cap = c.cap, legend = c.legend }
    end
  end
  if #items == 0 then return end
  local minX, minY, maxX, maxY = 1e9, 1e9, -1e9, -1e9
  for _, c in ipairs(items) do
    local x0, y0, x1, y1
    if c.shape == 0 then x0, y0, x1, y1 = c.cx - c.r, c.cy - c.r, c.cx + c.r, c.cy + c.r
    else                 x0, y0, x1, y1 = c.cx, c.cy, c.cx + c.w, c.cy + c.h end
    if x0 < minX then minX = x0 end
    if y0 < minY then minY = y0 end
    if x1 > maxX then maxX = x1 end
    if y1 > maxY then maxY = y1 end
  end

  local M    = 10
  local bpf  = floor(11 * fontScale() + 0.5)        -- band/section label font
  -- Gutter (CS only) sized to the widest gutter label via REAL ImGui metrics →
  -- never clips regardless of the on-screen font size (DPI/retina). BC: none.
  local GUT  = (domChar == "c") and math.max(56, math.min(120, (measure("FILTERS", bpf)) + 14)) or 8
  local top  = TAB_H + 10
  local availW = WW - 2 * M - GUT
  local availH = WH - top - 8
  if availW < 40 or availH < 40 then return end

  -- Per-domain scale: fill the window with THIS domain's bbox (BC fills big, CS
  -- uses its own width). Capped so BC knobs don't get absurd.
  local domW, domH = (maxX - minX), (maxY - minY)
  local scale = math.min(math.min(availW / domW, availH / domH), 2.6)
  if scale <= 0 then return end
  local originX = M + GUT + (availW - domW * scale) / 2 - minX * scale
  local originY = top + (availH - domH * scale) / 2 - minY * scale

  local pf = floor(9 * fontScale() + 0.5)
  local whiteText = (reaper.GetExtState(SECT, "hud_text_white") == "1")
  ctrlRects = {}
  local mx, my = reaper.ImGui_GetMousePos(ctx)
  local lmx, lmy = mx - OX, my - OY
  local tipSlot, tipParam

  -- Band connectors (drawn FIRST, behind the controls): a line through each EQ
  -- band's knob centres — data-driven from the control NAMES (prefix), no
  -- authored pixel art. The knobs paint over the connector's mid-sections.
  local function bandOf(nm)
    local p = nm:match("^(%u+) ")
    if p == "HF" or p == "HMF" or p == "LMF" or p == "LF" then return p end
    return nil
  end
  local bands = {}
  for _, c in ipairs(items) do
    if c.shape == 0 then
      local b = bandOf(c.label)
      if b then
        local t = bands[b]; if not t then t = {}; bands[b] = t end
        t[#t + 1] = { sx = originX + c.cx * scale, sy = originY + c.cy * scale,
                      rr = math.max(3, c.r * scale) }
      end
    end
  end
  for _, list in pairs(bands) do
    table.sort(list, function(a, b)
      if math.abs(a.sy - b.sy) > 2 then return a.sy < b.sy end
      return a.sx < b.sx
    end)
    for i = 1, #list - 1 do
      reaper.ImGui_DrawList_AddLine(dl, OX + list[i].sx, OY + list[i].sy,
        OX + list[i + 1].sx, OY + list[i + 1].sy,
        col(0x6A6E78, present and 0.5 or 0.25), math.max(1, list[i].rr * 0.10))
    end
  end

  -- LEFT gutter labels (CS): FILTERS / HF / HMF / EQ / LMF / LF, right-aligned,
  -- each at its band/section vertical centre (distinct y's → no overlap), like
  -- the silk-screen column on the hardware. (bpf computed up top.)
  if domChar == "c" then
    local function avgCY(pred)
      local s, n = 0, 0
      for _, c in ipairs(items) do if pred(c) then s = s + c.cy; n = n + 1 end end
      if n > 0 then return s / n end
      return nil
    end
    local rows = {}
    local fy = avgCY(function(c) return c.label == "LPF" or c.label == "HPF" end)
    if fy then rows[#rows + 1] = { "FILTERS", fy } end
    for _, bn in ipairs({ "HF", "HMF", "LMF", "LF" }) do
      local cy = avgCY(function(c) return c.label:match("^" .. bn .. " ") ~= nil end)
      if cy then rows[#rows + 1] = { bn, cy } end
    end
    for _, c in ipairs(items) do if c.label == "EQ In" then rows[#rows + 1] = { "EQ", c.cy } end end
    local xR = M + GUT - 8
    for _, r in ipairs(rows) do
      local w, h = measure(r[1], bpf)
      dtext(xR - w, originY + r[2] * scale - h / 2,
        col(present and 0xB8BCC4 or 0x70747C, present and 0.9 or 0.5), r[1], bpf)
    end
  end

  -- Section headers: COMPRESSOR / GATE-EXP above their right clusters (with a
  -- rule); CHANNEL to the LEFT of its cluster (no room above); BUS COMP for BC.
  local spf = floor(11 * fontScale() + 0.5)
  local function secHeader(pred, label, side)
    local cs = {}
    for _, c in ipairs(items) do if pred(c) then cs[#cs + 1] = c end end
    if #cs == 0 then return end
    local x0, x1, y0 = 1e9, -1e9, 1e9
    for _, c in ipairs(cs) do
      local cxs = originX + c.cx * scale
      local a = (c.shape == 0) and (cxs - c.r * scale) or cxs
      local b = (c.shape == 0) and (cxs + c.r * scale) or (cxs + c.w * scale)
      local yy = (c.shape == 0) and (originY + c.cy * scale - c.r * 1.3 * scale) or (originY + c.cy * scale)
      if a < x0 then x0 = a end; if b > x1 then x1 = b end; if yy < y0 then y0 = yy end
    end
    local w, h = measure(label, spf)
    local hcol = col(present and 0xCED2DA or 0x808691, present and 0.95 or 0.5)
    if side == "left" then
      local yc = 0; for _, c in ipairs(cs) do yc = yc + originY + c.cy * scale end; yc = yc / #cs
      dtext(math.max(2, x0 - w - 10), yc - h / 2, hcol, label, spf)
      return
    end
    local hy = math.max(TAB_H + 2, y0 - h - 6)
    local hx = math.min(math.max(2, x0), WW - 2 - w)
    dtext(hx, hy, hcol, label, spf)
    local ry, rx0, rx1 = hy + h / 2, hx + w + 6, math.min(WW - 2, x1)
    if rx1 > rx0 then
      reaper.ImGui_DrawList_AddLine(dl, OX + rx0, OY + ry, OX + rx1, OY + ry,
        col(present and 0x4A4E58 or 0x33363D, 1), 1.0)
    end
  end
  if domChar == "c" then
    secHeader(function(c) return c.sec == "Dynamics" end, "COMPRESSOR")
    secHeader(function(c) return c.sec == "Gate" end, "GATE/EXP")
    secHeader(function(c) return c.sec == "I/O & Channel" and c.shape ~= 0 end, "CHANNEL", "left")
  else
    secHeader(function() return true end, "BUS COMP")
  end

  for _, c in ipairs(items) do
    local a        = asn[c.idx]
    local mapped   = (a ~= nil)
    local learning = (c.idx == learnIdx)
    -- Ring = the SSL accent cap colour (per EQ band: red/green/blue/black, BC
    -- blue). Toggles/btns publish cap 0 → fall back to the domain colour.
    local capCol = (c.cap and c.cap > 0) and ((c.cap >> 8) & 0xFFFFFF) or rgb
    -- State by brightness (caps are physically always coloured on the hardware).
    local ringA = mapped and 1.0 or (present and 0.55 or 0.30)
    local ringCol = present and capCol or 0x707880
    local fillCol = mapped and col(capCol, 0.20) or col(0x202227, 1)

    local cell
    if c.shape == 0 then
      local sx, sy = originX + c.cx * scale, originY + c.cy * scale
      local rr = math.max(3, c.r * scale)
      reaper.ImGui_DrawList_AddCircleFilled(dl, OX + sx, OY + sy, rr, fillCol)
      if learning then
        local p = 0.4 + 0.4 * math.abs((frame % 50) / 25 - 1)
        reaper.ImGui_DrawList_AddCircle(dl, OX + sx, OY + sy, rr + 2, col(0xFFFFFF, p), 0, 2)
      end
      reaper.ImGui_DrawList_AddCircle(dl, OX + sx, OY + sy, rr, col(ringCol, ringA), 0, math.max(1, rr * 0.12))
      -- SSL smart-LED ring: ~270° arc of dots around the knob (gap at the
      -- bottom), like the hardware surface. Static (no live value) — dim, in the
      -- cap colour when the plug-in is present, grey otherwise.
      local ringR = rr * 1.28
      local dotR  = math.max(0.7, rr * 0.10)
      local dotCol = col(present and capCol or 0x60646E, present and 0.55 or 0.3)
      local A0, A1, N = 2.356, 7.069, 11   -- 135°..405° → 270° arc, gap at 6 o'clock
      for di = 0, N - 1 do
        local ang = A0 + (A1 - A0) * (di / (N - 1))
        reaper.ImGui_DrawList_AddCircleFilled(dl,
          OX + sx + math.cos(ang) * ringR, OY + sy + math.sin(ang) * ringR, dotR, dotCol)
      end
      -- Pointer notch at 12 o'clock.
      reaper.ImGui_DrawList_AddLine(dl, OX + sx, OY + sy - rr * 0.82,
        OX + sx, OY + sy - rr * 0.34, col(0xE8E8E8, present and 0.9 or 0.4), math.max(1, rr * 0.12))
      local gk = (c.label == "LPF" and "lpf") or (c.label == "HPF" and "hpf") or nil
      if gk then eqGlyph(gk, sx + rr + 3, sy - rr * 0.3, rr * 0.7,
        col(0x9DA2AC, present and 0.8 or 0.4)) end
      ctrlRects[#ctrlRects + 1] = { idx = c.idx, shape = 0, x = sx, y = sy, r = rr }
      cell = { knob = true, cx = sx, top = sy + ringR + dotR + 2, w = math.max(60, rr * 4) }
    else
      local sx, sy = originX + c.cx * scale, originY + c.cy * scale
      local sw, sh = c.w * scale, c.h * scale
      reaper.ImGui_DrawList_AddRectFilled(dl, OX + sx, OY + sy, OX + sx + sw, OY + sy + sh, fillCol, 3)
      if learning then
        local p = 0.4 + 0.4 * math.abs((frame % 50) / 25 - 1)
        reaper.ImGui_DrawList_AddRect(dl, OX + sx - 2, OY + sy - 2, OX + sx + sw + 2, OY + sy + sh + 2, col(0xFFFFFF, p), 3, 0, 2)
      end
      reaper.ImGui_DrawList_AddRect(dl, OX + sx, OY + sy, OX + sx + sw, OY + sy + sh, col(ringCol, ringA), 3, 0, 1)
      if c.label:match("Type$") then eqGlyph("bell", sx + sw + 3, sy + sh * 0.2,
        sh * 0.7, col(0x9DA2AC, present and 0.8 or 0.4)) end
      ctrlRects[#ctrlRects + 1] = { idx = c.idx, shape = c.shape, x = sx, y = sy, w = sw, h = sh }
      cell = { knob = false, rx = sx + sw, cy = sy + sh / 2,
               isType = (c.label:match("Type$") ~= nil), w = math.max(60, sw + 16) }
    end

    -- Compact fixed label = the short scribble legend (like the hardware face).
    -- The full slot name + bound param name go in the hover tooltip below.
    local tcol, ta
    if mapped      then tcol, ta = (whiteText and 0xFFFFFF or capCol), 1.0
    elseif present then tcol, ta = 0xC6CDD6, 0.85
    else                tcol, ta = 0x707880, 0.5 end
    local shown = fit(c.legend, cell.w, pf)
    local tw, th = measure(shown, pf)
    local tx, ty
    if cell.knob then          -- knob: label centred below
      tx = cell.cx - tw / 2; ty = cell.top
      if tx < 2 then tx = 2 elseif tx + tw > WW - 2 then tx = WW - 2 - tw end
    else                       -- button/toggle: label to the RIGHT (stacked buttons
      local extra = cell.isType and 16 or 0   -- would collide if labelled below; +glyph room
      tx = cell.rx + 5 + extra; ty = cell.cy - th / 2
      if tx + tw > WW - 2 then tx = cell.rx - tw - 5 end   -- flip left if no room
    end
    dtext(tx, ty, col(tcol, ta), shown, pf)

    local h = ctrlRects[#ctrlRects]
    local over
    if h.shape == 0 then
      local dx, dy = lmx - h.x, lmy - h.y
      over = (dx * dx + dy * dy) <= (h.r + 3) * (h.r + 3)
    else
      over = lmx >= h.x and lmx <= h.x + h.w and lmy >= h.y and lmy <= h.y + h.h
    end
    if over then
      tipSlot  = c.label
      tipParam = mapped and (a.name .. (a.inv and "  (inverted)" or "")) or nil
    end
  end

  if tipSlot then
    local t = tipSlot
    if tipParam then t = t .. "  \xE2\x80\x94  " .. tipParam end
    reaper.ImGui_SetTooltip(ctx, t)
  end
end

local function render()
  TAB_H = floor(22 * fontScale() + 0.5) + 16

  local st  = readState()
  local asn = readAssign()

  frame = frame + 1
  local hl = reaper.GetExtState(SECT, "hud_learn")
  learnIdx = (hl ~= "" and tonumber(hl)) or -1
  local hint = reaper.GetExtState(SECT, "hud_hint")
  if hint ~= "" then hintText = hint; hintFrames = 90
    reaper.SetExtState(SECT, "hud_hint", "", false) end

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
    dtext(10, TAB_H + 12, col(0x808890, 0.9),
      "Waiting for surface geometry\xE2\x80\xA6", floor(15 * fontScale() + 0.5))
    return
  end

  local present = (activeTab == "cs") and st.csPresent or st.bcPresent

  if not present then
    ctrlRects = {}
    local px = floor(17 * fontScale() + 0.5)
    local msg = (activeTab == "cs" and "No Channel-Strip" or "No Bus-Comp")
              .. " plug-in on the focused track"
    local tw, th = measure(msg, px)
    dtext((WW - tw) / 2, (WH - th) / 2, col(0x9097A0, 0.9), msg, px)
  elseif reaper.GetExtState(SECT, "hud_imgui_view") == "mockup" then
    renderMockup(st, asn)
  else
    renderList(st, asn)
  end

  local function banner(msg, bgRgb, bgA, fgRgb)
    local px = floor(14 * fontScale() + 0.5)
    local tw, th = measure(msg, px)
    local bw, bh = tw + 20, th + 8
    local bx, by = (WW - bw) / 2, TAB_H + 4
    rect(bx, by, bw, bh, col(bgRgb, bgA))
    dtext(bx + 10, by + 4, col(fgRgb, 1), msg, px)
  end

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
-- Right-click menu (ImGui popup). Dock is handled by ReaImGui's built-in
-- title-bar context menu, so it's no longer in here.
------------------------------------------------------------------------
local FONT_PRESETS = {
  { l = "Small",       v = 1.0 },
  { l = "Medium",      v = 1.25 },
  { l = "Large",       v = 1.5 },
  { l = "Extra Large", v = 1.9 },
  { l = "Huge",        v = 2.3 },
}
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
  local fs = fontScale()

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

  if reaper.ImGui_BeginMenu(ctx, "Text size") then
    for _, p in ipairs(FONT_PRESETS) do
      if reaper.ImGui_MenuItem(ctx, p.l, nil, math.abs(fs - p.v) < 0.01) then
        reaper.SetExtState(SECT, "hud_imgui_font", tostring(p.v), true)
      end
    end
    reaper.ImGui_EndMenu(ctx)
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
local DEF_W, DEF_H = 500, 500
local function loadRect()
  local raw = reaper.GetExtState(SECT, "hud_imgui_rect")
  local x, y, w, h = raw:match("^(%-?%d+),(%-?%d+),(%d+),(%d+)$")
  if x then return tonumber(x), tonumber(y), tonumber(w), tonumber(h) end
  return nil
end
local restore_x, restore_y, restore_w, restore_h = loadRect()
local first_frame = true
local last_x, last_y, last_w, last_h

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
        -- Tab switch first, else arm/cancel learn (last frame's hit-rects).
        if not handleTabClick(lx, ly) then handleControlClick(lx, ly) end
      end
      local wheel = reaper.ImGui_GetMouseWheel(ctx)
      if wheel ~= 0 then scrollY = scrollY - wheel * 40 end
    end
    if reaper.ImGui_IsKeyPressed(ctx, reaper.ImGui_Key_Escape()) and learnIdx >= 0 then
      sendCmd("cancel")
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
  setToggle(false)
end

reaper.atexit(shutdown)
reaper.defer(loop)
