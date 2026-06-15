-- @description Rea-Sixty — Focused-track panel (frameless, Gridbox-style)
-- @author Störsender
-- @version 0.1.0
-- @provides [main] .
-- @about
--   Frameless readout box that floats over the REAPER main window (no docker, no
--   window frame) showing the surface-focused track + its active CS / BC plug-in
--   names + last-touched param. Drag to move, drag the edges to resize,
--   right-click for layout / font / colour. Position + size persist.
--
--   Modelled on FeedTheCat's Gridbox: a LICE bitmap composited onto the main
--   window via JS_Composite, with mouse handled by JS_WindowMessage_Intercept
--   (only while hovering the box, so the rest of REAPER stays usable). Reads the
--   same ExtState the dockable panel does (overlay_focus / overlay /
--   overlay_param_cs|bc), published by the Rea-Sixty extension.

local SECT   = "rea_sixty"
local RUNKEY = "focused_panel_running"

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

-- Dependency check (js_ReaScriptAPI).
local NEED = { "JS_Composite", "JS_Composite_Delay", "JS_LICE_CreateBitmap",
  "JS_LICE_FillRect", "JS_LICE_Clear", "JS_LICE_DrawText", "JS_LICE_CreateFont",
  "JS_LICE_SetFontFromGDI", "JS_GDI_CreateFont", "JS_WindowMessage_Intercept",
  "JS_WindowMessage_Peek", "JS_Window_ScreenToClient", "JS_Window_GetClientSize",
  "JS_Mouse_LoadCursor", "JS_Mouse_SetCursor" }
for _, fn in ipairs(NEED) do
  if not reaper.APIExists(fn) then
    reaper.MB("js_ReaScriptAPI fehlt: " .. fn, "Rea-Sixty Focused Panel", 0)
    reaper.SetExtState(SECT, RUNKEY, "0", false)
    return
  end
end

local is_windows = reaper.GetOS():find("Win") ~= nil
local main_hwnd = reaper.GetMainHwnd()

------------------------------------------------------------------------
-- Settings (shared with the dockable panel).
------------------------------------------------------------------------
local function num(key, def)
  local v = tonumber(reaper.GetExtState(SECT, key))
  if v == nil then return def end
  return v
end
local function csRgb() return math.floor(num("overlay_cs_col", 0xFFFF00)) & 0xFFFFFF end
local function bcRgb() return math.floor(num("overlay_bc_col", 0xFF0000)) & 0xFFFFFF end
-- British/American spelling — mirrors the extension's reasixty_sp() / the
-- Settings → Appearance radio (ExtState "ui_spelling": "1" = American "color",
-- anything else = British "colour").
local function sp(uk, us)
  return (reaper.GetExtState(SECT, "ui_spelling") == "1") and us or uk
end
local function oneLine() return reaper.GetExtState(SECT, "focused_panel_oneline") == "1" end
local function hostPref()
  local p = reaper.GetExtState(SECT, "focused_panel_host")
  return (p == "") and "transport" or p
end
local function fontSize()
  local fs = math.floor(num("focused_panel_font", 18))
  if fs < 8 then fs = 8 end
  return fs
end

------------------------------------------------------------------------
-- Box state (client coords of the host main window). Persisted.
------------------------------------------------------------------------
local host           -- main window HWND
local win_w, win_h   -- host client size
local bm_x, bm_y, bm_w, bm_h
local bitmap
local lice_font, font_px
local is_resize, is_redraw = true, true
-- Auto-fit height to content ONCE. Only on a font/layout change (menu) or a
-- fresh install with no saved rect — NOT on every load, or the user's saved
-- height got clobbered by metrics() each restart. loadRect arms it when there
-- is no persisted size yet.
local fit_height = false
local drag_x, drag_y       -- drag anchor (client coords)
local resize_flags = 0     -- 1=L 2=T 4=R 8=B (combine for corners)
local last_sig             -- content signature → redraw only on change
local g_rehost = false     -- set by the "Attach to" menu → re-acquire host

local function saveRect()
  reaper.SetExtState(SECT, "focused_panel_rect",
    ("%d,%d,%d,%d"):format(bm_x, bm_y, bm_w, bm_h), true)
end

local function loadRect()
  local raw = reaper.GetExtState(SECT, "focused_panel_rect")
  local x, y, w, h = raw:match("^(%-?%d+),(%-?%d+),(%d+),(%d+)$")
  if x then
    bm_x, bm_y, bm_w, bm_h = tonumber(x), tonumber(y), tonumber(w), tonumber(h)
  else
    bm_x, bm_y, bm_w, bm_h = 60, 80, 360, 56
    fit_height = true   -- no saved size yet → auto-size the default height once
  end
end

------------------------------------------------------------------------
-- LICE drawing helpers (mirror Gridbox's macOS-correct primitives).
------------------------------------------------------------------------
local ALPHA = 0xFF000000

local function clearBitmap(bm, color)
  if is_windows then reaper.JS_LICE_Clear(bm, 0x00000000)
  else                reaper.JS_LICE_Clear(bm, color & 0x00FFFFFF) end
end

local function fillRect(bm, x, y, w, h, color, a)
  reaper.JS_LICE_FillRect(bm, x, y, w, h, color | ALPHA, a or 1, 0)
end

local function rectOutline(bm, x, y, w, h, color, a)
  reaper.JS_LICE_RoundRect(bm, x, y, w - 1, h - 1, 0, color | ALPHA, a or 1, 0, true)
end

-- Rounded filled rect (corners drawn with circles, like Gridbox's DrawLICERect)
-- so the cleared-transparent corners show through → real rounded corners.
local function roundedFill(bm, x, y, w, h, color, r)
  color = color | ALPHA
  if h <= 2 * r then r = math.floor(h / 2 - 1) end
  if w <= 2 * r then r = math.floor(w / 2 - 1) end
  if r < 1 then reaper.JS_LICE_FillRect(bm, x, y, w, h, color, 1, 0); return end
  local FC = reaper.JS_LICE_FillCircle
  FC(bm, x + r,         y + r,         r, color, 1, 0, 1)
  FC(bm, x + w - r - 1, y + r,         r, color, 1, 0, 1)
  FC(bm, x + w - r - 1, y + h - r - 1, r, color, 1, 0, 1)
  FC(bm, x + r,         y + h - r - 1, r, color, 1, 0, 1)
  reaper.JS_LICE_FillRect(bm, x,         y + r, r,         h - r * 2, color, 1, 0)
  reaper.JS_LICE_FillRect(bm, x + w - r, y + r, r,         h - r * 2, color, 1, 0)
  reaper.JS_LICE_FillRect(bm, x + r,     y,     w - r * 2, h,         color, 1, 0)
end

local function roundedOutline(bm, x, y, w, h, color, r)
  reaper.JS_LICE_RoundRect(bm, x, y, w - 1, h - 1, r or 0, color | ALPHA, 1, 0, true)
end

-- gfx font is used only to MEASURE (mirrors Gridbox); LICE/GDI font draws.
local function measure(s) return (gfx.measurestr(s)) end

local function liceText(s, x, y, color)
  reaper.JS_LICE_SetFontColor(lice_font, color | ALPHA)
  reaper.JS_LICE_DrawText(bitmap, lice_font, s, #s, x, y, bm_w, bm_h)
  return x + measure(s)
end

local function rebuildFont()
  local fs = fontSize()
  font_px = fs
  gfx.setfont(1, "Arial", fs)   -- for measurement
  if lice_font then reaper.JS_LICE_DestroyFont(lice_font) end
  lice_font = reaper.JS_LICE_CreateFont()
  local gdi = reaper.JS_GDI_CreateFont(fs, 0, 0, 0, 0, 0, "Arial")
  reaper.JS_LICE_SetFontFromGDI(lice_font, gdi, "")
  reaper.JS_GDI_DeleteObject(gdi)
end

------------------------------------------------------------------------
-- Focused-track data (same sources as the dockable panel).
------------------------------------------------------------------------
local function readActive()
  local raw = reaper.GetExtState(SECT, "overlay")
  if raw == "" then return {} end
  local byGuid = {}
  for guid, cs, bc in raw:gmatch("({[%x%-]+}),(%-?%d+),(%-?%d+)") do
    byGuid[guid] = { cs = tonumber(cs), bc = tonumber(bc) }
  end
  return byGuid
end

local function findTrackByGuid(guid)
  if not guid or guid == "" then return nil end
  local m = reaper.GetMasterTrack(0)
  if m and reaper.GetTrackGUID(m) == guid then return m, true end
  for i = 0, reaper.CountTracks(0) - 1 do
    local tr = reaper.GetTrack(0, i)
    if tr and reaper.GetTrackGUID(tr) == guid then return tr, false end
  end
  return nil
end

local function trackLabel(tr, isMaster)
  if isMaster then return "Master" end
  local _, nm = reaper.GetTrackName(tr)
  if not nm or nm == "" then return "Track" end
  return nm
end

-- Smart track-name abbreviation — Lua port of the extension's
-- abbreviateTrackName_ (TrackName.cpp), so the box reads like the UF8 / UC1
-- scribble. Passes: 1) strip separators; 2) per-token drop later vowels
-- (all-caps 2-4-char acronyms like DI/EQ/FX survive); 3) collapse repeated
-- consonants; 4) hard-truncate. `maxLen` is user-set. Returns src unchanged
-- if it already fits.
local function isVowel(c)
  c = c:lower()
  return c == "a" or c == "e" or c == "i" or c == "o" or c == "u"
end

local function smartAbbrev(src, maxLen)
  if not src or maxLen <= 0 or #src <= maxLen then return src end
  local tokens = {}
  for tok in src:gmatch("[^%s%-_/]+") do tokens[#tokens + 1] = tok end
  if #tokens == 0 then return src:sub(1, maxLen) end
  local joined = table.concat(tokens)
  if #joined <= maxLen then return joined end
  local abbr = {}
  for _, t in ipairs(tokens) do
    if #t >= 2 and #t <= 4 and t:match("^%u+$") then
      abbr[#abbr + 1] = t
    else
      local a = {}
      for i = 1, #t do
        local c = t:sub(i, i)
        if i == 1 or not isVowel(c) then a[#a + 1] = c end
      end
      if #a == 0 then a[1] = t:sub(1, 1) end
      abbr[#abbr + 1] = table.concat(a)
    end
  end
  joined = table.concat(abbr)
  if #joined <= maxLen then return joined end
  for k, a in ipairs(abbr) do
    local c = {}
    for i = 1, #a do
      local ch = a:sub(i, i)
      local letter = ch:match("%a") ~= nil
      if not (#c > 0 and c[#c] == ch and letter and not isVowel(ch)) then
        c[#c + 1] = ch
      end
    end
    abbr[k] = table.concat(c)
  end
  joined = table.concat(abbr)
  if #joined <= maxLen then return joined end
  return joined:sub(1, maxLen)
end

-- Format a track name per the user's options (own keys, independent of layout):
--   focused_panel_trackname  "0" hide / else show (default show)
--   focused_panel_track_abbr "1" smart / else full (default full)
--   focused_panel_track_len  smart-abbrev length    (default 8)
local function trackDisplay(name)
  if not name then return nil end
  if reaper.GetExtState(SECT, "focused_panel_trackname") == "0" then return nil end
  if reaper.GetExtState(SECT, "focused_panel_track_abbr") == "1" then
    return smartAbbrev(name, math.max(1, math.floor(num("focused_panel_track_len", 8))))
  end
  return name
end

-- Track-colour for the track name, when "Use track colour" is on and the track
-- carries a custom colour. Returns 0xRRGGBB or nil (→ default dim grey).
local function trackNameRgb(tr)
  if not tr then return nil end
  if reaper.GetExtState(SECT, "focused_panel_track_color") ~= "1" then return nil end
  local nat = reaper.GetTrackColor(tr)
  if not nat or nat == 0 then return nil end     -- no custom colour assigned
  local rr, gg, bb = reaper.ColorFromNative(nat)
  return ((rr & 0xFF) << 16) | ((gg & 0xFF) << 8) | (bb & 0xFF)
end

local function fxLabel(tr, fxIdx)
  if not tr or not fxIdx or fxIdx < 0 then return nil end
  local _, nm = reaper.TrackFX_GetFXName(tr, fxIdx, "")
  if not nm or nm == "" then return nil end
  nm = nm:gsub("^%u%u+%d*i?:%s*", "")
  nm = nm:gsub("%s*%([^()]-%)%s*$", "")
  return nm
end

local function findActiveBc(byGuid)
  for guid, a in pairs(byGuid) do
    if a.bc and a.bc >= 0 then return guid, a.bc end
  end
  return nil
end

local function paramStr(key)
  local raw = reaper.GetExtState(SECT, key)
  if not raw or raw == "" then return nil, nil end
  local n, v = raw:match("^(.-)\t(.*)$")
  if not n then n = raw end
  return n, v
end

------------------------------------------------------------------------
-- Draw the box content into the bitmap.
------------------------------------------------------------------------
local function drawContent()
  local r  = math.floor(num("focused_panel_corner", 6))
  local bg = math.floor(num("focused_panel_bg",     0x1C1C1F)) & 0xFFFFFF
  local bd = math.floor(num("focused_panel_border", 0x3A3D44)) & 0xFFFFFF
  -- Clear to the bg colour with 0 alpha so the rounded corners stay transparent.
  clearBitmap(bitmap, bg)
  roundedFill(bitmap, 0, 0, bm_w, bm_h, bg, r)
  roundedOutline(bitmap, 0, 0, bm_w, bm_h, resize_flags > 0 and 0x6A86C0 or bd, r)

  local byGuid = readActive()
  local fGuid  = reaper.GetExtState(SECT, "overlay_focus")
  local ftr, fMaster = findTrackByGuid(fGuid)
  local fa = byGuid[fGuid]
  local csTrk  = ftr and trackDisplay(trackLabel(ftr, fMaster)) or nil
  local csName = (ftr and fa) and fxLabel(ftr, fa.cs) or nil
  local csTrkCol = trackNameRgb(ftr)

  local bcGuid, bcIdx = findActiveBc(byGuid)
  local btr, bMaster = nil, false
  if bcGuid then btr, bMaster = findTrackByGuid(bcGuid) end
  local bcTrk  = btr and trackDisplay(trackLabel(btr, bMaster)) or nil
  local bcName = btr and fxLabel(btr, bcIdx) or nil
  local bcTrkCol = trackNameRgb(btr)

  local csPN, csPV = paramStr("overlay_param_cs")
  local bcPN, bcPV = paramStr("overlay_param_bc")

  local fs  = font_px
  local pad = math.max(6, math.floor(fs * 0.5))
  local lh  = fs + math.floor(fs * 0.5)

  -- One segment at (x,y); returns advanced x. Track name shown per trackDisplay
  -- (independent of layout — passed in both one- and two-line modes).
  local function seg(x, y, tag, col, name, pn, pv, trk, trkCol)
    local lit = name ~= nil
    local dim = lit and 0xB0B0BC or 0x6A6A74
    if trk ~= nil then x = liceText((trk or "") .. "  ", x, y, trkCol or dim) end
    x = liceText(tag .. " ", x, y, col)
    x = liceText(name or "\xE2\x80\x94", x, y, lit and 0xEDEDF2 or 0x6A6A74)
    if pn and pn ~= "" then
      x = liceText("   " .. pn, x, y, 0xA8C6F0)
      if pv and pv ~= "" then x = liceText(" " .. pv, x, y, 0x9A9AA2) end
    end
    return x
  end

  -- Vertically centre the text block in the box (works at any box height).
  local rows   = oneLine() and 1 or 2
  local blockH = rows * lh
  local top    = math.max(2, (bm_h - blockH) // 2)
  local ty0    = top + (lh - fs) // 2     -- centre each line within its slot
  if oneLine() then
    local x = seg(pad, ty0, "CS", csRgb(), csName, csPN, csPV, csTrk, csTrkCol)
    seg(x + measure("     "), ty0, "BC", bcRgb(), bcName, bcPN, bcPV, bcTrk, bcTrkCol)
  else
    seg(pad, ty0,      "CS", csRgb(), csName, csPN, csPV, csTrk, csTrkCol)
    seg(pad, ty0 + lh, "BC", bcRgb(), bcName, bcPN, bcPV, bcTrk, bcTrkCol)
  end

  reaper.JS_Window_InvalidateRect(host, bm_x, bm_y, bm_x + bm_w, bm_y + bm_h, false)
end

local function metrics()
  local fs = fontSize()
  local pad = math.max(6, math.floor(fs * 0.5))
  local lh  = fs + math.floor(fs * 0.5)
  local rows = oneLine() and 1 or 2
  return pad * 2 + lh * rows + 4
end

-- Re-create the bitmap (size change) + (re)place the composite.
local function applyComposite()
  if bitmap then reaper.JS_LICE_DestroyBitmap(bitmap) end
  bitmap = reaper.JS_LICE_CreateBitmap(true, bm_w, bm_h)
  rebuildFont()
  reaper.JS_Composite_Delay(host, 0.03, 0.05, 8)
  reaper.JS_Composite(host, bm_x, bm_y, bm_w, bm_h, bitmap, 0, 0, bm_w, bm_h)
end

local function moveComposite()
  reaper.JS_Composite_Delay(host, 0.03, 0.05, 8)
  reaper.JS_Composite(host, bm_x, bm_y, bm_w, bm_h, bitmap, 0, 0, bm_w, bm_h)
end

-- Move the box to (nx,ny): invalidate the OLD rect (so the host repaints over
-- the previous frame → no smear trail) then the NEW rect, then re-composite.
-- Mirrors Gridbox's SetBitmapCoords.
local function placeBox(nx, ny)
  if host then
    reaper.JS_Window_InvalidateRect(host, bm_x, bm_y, bm_x + bm_w, bm_y + bm_h, false)
  end
  bm_x, bm_y = nx, ny
  reaper.JS_Window_InvalidateRect(host, bm_x, bm_y, bm_x + bm_w, bm_y + bm_h, false)
  reaper.JS_Composite_Delay(host, 0.03, 0.05, 8)
  reaper.JS_Composite(host, bm_x, bm_y, bm_w, bm_h, bitmap, 0, 0, bm_w, bm_h)
end

local function clampToHost()
  if bm_w < 80  then bm_w = 80  end
  if bm_h < 24  then bm_h = 24  end
  if win_w and bm_w > win_w then bm_w = win_w end
  if win_h and bm_h > win_h then bm_h = win_h end
  if win_w then bm_x = math.max(0, math.min(win_w - bm_w, bm_x)) end
  if win_h then bm_y = math.max(0, math.min(win_h - bm_h, bm_y)) end
end

------------------------------------------------------------------------
-- Declarative right-click menu (same pattern as the dockable panel).
------------------------------------------------------------------------
local function buildMenu(menu)
  local str = ""
  for _, e in ipairs(menu) do
    if e.separator then str = str .. "|"
    elseif #e > 0 then
      str = str .. ">" .. e.title .. "|" .. buildMenu(e) .. "<|"
    elseif e.title then
      if e.grayed  then str = str .. "#" end
      if e.checked then str = str .. "!" end
      str = str .. e.title .. "|"
    end
  end
  return str
end

local function runMenu(menu, idx, i)
  i = i or 1
  for _, e in ipairs(menu) do
    if #e > 0 then
      i = runMenu(e, idx, i); if i < 0 then return i end
    elseif e.title and not e.separator then
      if i == idx then if e.OnReturn then e.OnReturn() end; return -1 end
      i = i + 1
    end
  end
  return i
end

local function setFontSize()
  local ret, input = reaper.GetUserInputs("Panel font", 1,
    "Font size: (e.g. 18),extrawidth=40", tostring(fontSize()))
  if not ret then return end
  local fs = tonumber(input)
  if fs then
    reaper.SetExtState(SECT, "focused_panel_font",
      tostring(math.max(8, math.floor(fs))), true)
    is_resize = true; fit_height = true
  end
end

local function chooseColor(key)
  local ret, color = reaper.GR_SelectColor(reaper.GetMainHwnd())
  if ret ~= 0 then
    local r, g, b = reaper.ColorFromNative(color)
    reaper.SetExtState(SECT, key, tostring(r * 65536 + g * 256 + b), true)
    is_redraw = true
  end
end

local function setCorner()
  local cur = math.floor(num("focused_panel_corner", 6))
  local ret, input = reaper.GetUserInputs("Corners", 1,
    "Corner radius: (e.g. 6),extrawidth=40", tostring(cur))
  if not ret then return end
  local v = tonumber(input)
  if v then
    reaper.SetExtState(SECT, "focused_panel_corner", tostring(math.max(0, math.floor(v))), true)
    is_redraw = true
  end
end

local function setTrackLen()
  local cur = math.floor(num("focused_panel_track_len", 8))
  local ret, input = reaper.GetUserInputs("Track name length", 1,
    "Smart-abbrev length: (e.g. 8),extrawidth=40", tostring(cur))
  if not ret then return end
  local v = tonumber(input)
  if v then
    reaper.SetExtState(SECT, "focused_panel_track_len", tostring(math.max(1, math.floor(v))), true)
    is_redraw = true
  end
end

local function setHost(p)
  reaper.SetExtState(SECT, "focused_panel_host", p, true)
  g_rehost = true
end

local menu_time
local function showContextMenu(mx, my)
  -- gfx has no window here, so open a tiny hidden one at the mouse for showmenu.
  local one = oneLine()
  local menu = {
    { title = "Layout",
      { title = "Two lines (CS / BC)", checked = not one,
        OnReturn = function() reaper.SetExtState(SECT, "focused_panel_oneline", "0", true); is_resize = true; fit_height = true end },
      { title = "One line", checked = one,
        OnReturn = function() reaper.SetExtState(SECT, "focused_panel_oneline", "1", true); is_resize = true; fit_height = true end },
    },
    { title = "Track name",
      { title = "Show track name",
        checked = (reaper.GetExtState(SECT, "focused_panel_trackname") ~= "0"),
        OnReturn = function()
          local on = reaper.GetExtState(SECT, "focused_panel_trackname") ~= "0"
          reaper.SetExtState(SECT, "focused_panel_trackname", on and "0" or "1", true)
          is_redraw = true
        end },
      { title = sp("Use track colour", "Use track color"),
        checked = (reaper.GetExtState(SECT, "focused_panel_track_color") == "1"),
        OnReturn = function()
          local on = reaper.GetExtState(SECT, "focused_panel_track_color") == "1"
          reaper.SetExtState(SECT, "focused_panel_track_color", on and "0" or "1", true)
          is_redraw = true
        end },
      { separator = true },
      { title = "Full name",
        checked = (reaper.GetExtState(SECT, "focused_panel_track_abbr") ~= "1"),
        OnReturn = function() reaper.SetExtState(SECT, "focused_panel_track_abbr", "0", true); is_redraw = true end },
      { title = "Smart abbreviate",
        checked = (reaper.GetExtState(SECT, "focused_panel_track_abbr") == "1"),
        OnReturn = function() reaper.SetExtState(SECT, "focused_panel_track_abbr", "1", true); is_redraw = true end },
      { title = "Abbreviation length\xE2\x80\xA6", OnReturn = setTrackLen },
    },
    { title = "Customize",
      { title = "Font size\xE2\x80\xA6",       OnReturn = setFontSize },
      { title = "Corner radius\xE2\x80\xA6",   OnReturn = setCorner },
      { separator = true },
      { title = sp("Background colour\xE2\x80\xA6", "Background color\xE2\x80\xA6"), OnReturn = function() chooseColor("focused_panel_bg") end },
      { title = sp("Border colour\xE2\x80\xA6", "Border color\xE2\x80\xA6"),     OnReturn = function() chooseColor("focused_panel_border") end },
      { title = sp("CS colour\xE2\x80\xA6", "CS color\xE2\x80\xA6"),         OnReturn = function() chooseColor("overlay_cs_col") end },
      { title = sp("BC colour\xE2\x80\xA6", "BC color\xE2\x80\xA6"),         OnReturn = function() chooseColor("overlay_bc_col") end },
    },
    { separator = true },
    { title = "Drag the box onto any window to move it there", grayed = true },
    { title = "Move to Transport", OnReturn = function()
        reaper.SetExtState(SECT, "fp_attach_title", "", true)
        reaper.SetExtState(SECT, "fp_attach_child", "", true)
        g_rehost = true
      end },
    { separator = true },
    { title = "Close panel", OnReturn = function()
        reaper.SetExtState(SECT, "focused_panel_on", "0", true)
      end },
  }
  local focus = reaper.JS_Window_GetFocus()
  gfx.init("rea_sixty_fp_menu", 10, 10, 0, mx, my)
  local gh = reaper.JS_Window_Find("rea_sixty_fp_menu", true)
  if gh then
    reaper.JS_Window_SetOpacity(gh, "ALPHA", 0)
    reaper.JS_Window_Show(gh, "HIDE")
  end
  if focus then reaper.JS_Window_SetFocus(focus) end
  gfx.x, gfx.y = 0, 0
  local sel = gfx.showmenu(buildMenu(menu))
  gfx.quit()
  if focus then reaper.JS_Window_SetFocus(focus) end
  if sel and sel > 0 then runMenu(menu, sel) end
  menu_time = reaper.time_precise()
end

------------------------------------------------------------------------
-- Mouse intercepts (only active while hovering the box).
------------------------------------------------------------------------
local Intercept = reaper.JS_WindowMessage_Intercept
local Release   = reaper.JS_WindowMessage_Release
local Peek      = reaper.JS_WindowMessage_Peek
local is_intercept = false
local msgs = { "WM_LBUTTONDOWN", "WM_LBUTTONUP", "WM_RBUTTONUP", "WM_SETCURSOR" }
local stamps = {}

local LoadCursor = reaper.JS_Mouse_LoadCursor
local cur_normal = LoadCursor(is_windows and 32512 or 0)
local cur_move   = LoadCursor(32646)
local cur_horz   = LoadCursor(32644)
local cur_vert   = LoadCursor(32645)
local cur_d1     = LoadCursor(32642)
local cur_d2     = LoadCursor(32643)

local function startIntercepts()
  if is_intercept then return end
  is_intercept = true
  for _, m in ipairs(msgs) do
    Intercept(host, m, false)
    -- Baseline the timestamp to the message that already exists pre-intercept,
    -- so a stale click from BEFORE we started hovering doesn't fire (that was
    -- the "menu opens on mouse-over" bug). Only genuinely newer messages fire.
    local _, _, time = Peek(host, m)
    stamps[m] = time
  end
end
local function endIntercepts()
  if not is_intercept then return end
  is_intercept = false
  for _, m in ipairs(msgs) do Release(host, m); stamps[m] = nil end
  reaper.JS_Mouse_SetCursor(cur_normal)
end

local function cursorFor(flags)
  if flags == 3 or flags == 12 then return cur_d2 end
  if flags == 6 or flags == 9  then return cur_d1 end
  if flags & 1 == 1 or flags & 4 == 4 then return cur_horz end
  if flags & 2 == 2 or flags & 8 == 8 then return cur_vert end
  return cur_move
end

-- Returns true if a fresh message of `msg` arrived.
local function peeked(msg)
  local ret, _, time = Peek(host, msg)
  if ret and time ~= stamps[msg] then stamps[msg] = time; return true end
  return false
end

------------------------------------------------------------------------
-- Main loop.
------------------------------------------------------------------------
local shutdown

-- Drag-to-attach (Gridbox model): the box can be dragged onto ANY window and
-- hosts there; the host's identity (title, or parent-title + child-id when the
-- window itself has no title) is persisted so it comes back on the same window.
local transport_title = reaper.JS_Localize and
  reaper.JS_Localize("Transport", "common") or "Transport"

local function transportHost()
  local t = reaper.JS_Window_Find(transport_title, true)
  if t and reaper.ValidatePtr(t, "HWND*") then return t end
  return nil
end

-- Can the box host (composite) on this window? Mirrors Gridbox's attach
-- validation: a window must have a title, OR be a no-title child of a *titled,
-- non-main* parent that has its own control ID (docked toolbars). On macOS the
-- arrange / TCP / ruler are real **no-title child windows of the MAIN window** —
-- JS_Window_FromPoint returns them (NOT GetMainHwnd), GetThingFromPoint returns
-- "" over them, and compositing there turns them BLACK. Requiring a title (and
-- rejecting no-title children of main) is what reliably keeps the box off them.
local function attachable(hwnd)
  if not hwnd or not reaper.ValidatePtr(hwnd, "HWND*") then return false end
  if hwnd == main_hwnd then return false end
  local title = (reaper.JS_Window_GetTitle and reaper.JS_Window_GetTitle(hwnd)) or ""
  if title ~= "" then return true end
  -- No title → allow only docked toolbars: child of a titled, non-main parent
  -- with its own control ID. The TCP/arrange/ruler are no-title children of MAIN
  -- → rejected here.
  local parent = reaper.JS_Window_GetParent and reaper.JS_Window_GetParent(hwnd)
  if parent and parent ~= main_hwnd and reaper.ValidatePtr(parent, "HWND*") then
    local pt = (reaper.JS_Window_GetTitle and reaper.JS_Window_GetTitle(parent)) or ""
    local id = reaper.JS_Window_GetLong and tonumber(reaper.JS_Window_GetLong(hwnd, "ID"))
    if pt ~= "" and id and id ~= 0 then return true end
  end
  return false
end

-- Save the window we're hosted on so we can re-find it next session (Gridbox's
-- SaveAttachedWindow: title, falling back to parent-title + child-id).
local function saveAttach(hwnd)
  if not hwnd then return end
  local title = (reaper.JS_Window_GetTitle and reaper.JS_Window_GetTitle(hwnd)) or ""
  local childId = ""
  if title == "" then
    local parent = reaper.JS_Window_GetParent and reaper.JS_Window_GetParent(hwnd)
    if parent and reaper.ValidatePtr(parent, "HWND*") and reaper.JS_Window_GetLong then
      local id = tonumber(reaper.JS_Window_GetLong(hwnd, "ID"))
      if id and id ~= 0 then
        childId = tostring(math.floor(id))
        title = (reaper.JS_Window_GetTitle and reaper.JS_Window_GetTitle(parent)) or ""
      end
    end
  end
  if hwnd == reaper.GetMainHwnd() then title = "REAPER Main Window" end
  reaper.SetExtState(SECT, "fp_attach_title", title, true)
  reaper.SetExtState(SECT, "fp_attach_child", childId, true)
end

local function findAttach()
  local title = reaper.GetExtState(SECT, "fp_attach_title")
  if not title or title == "" then return nil end
  local childId = tonumber(reaper.GetExtState(SECT, "fp_attach_child"))
  local hwnd
  if title == "REAPER Main Window" then
    hwnd = reaper.GetMainHwnd()
  elseif reaper.JS_Window_ListFind then
    local cnt, list = reaper.JS_Window_ListFind(title, true)
    if cnt and cnt >= 1 and list and list ~= "" then
      local first = (list .. ","):match("(.-),")
      if first then hwnd = reaper.JS_Window_HandleFromAddress(first) end
    end
  end
  if hwnd and childId and reaper.JS_Window_FindChildByID then
    hwnd = reaper.JS_Window_FindChildByID(hwnd, childId)
  end
  if hwnd and reaper.ValidatePtr(hwnd, "HWND*") then return hwnd end
  return nil
end

-- Saved attach first (rejected if it's gone stale onto the TCP/arrange), else
-- the transport (proven default), else main (never leaves it without a host).
local function acquireHost()
  local h = findAttach()
  if h and attachable(h) then return h end
  return transportHost() or reaper.GetMainHwnd()
end

-- End a drag: snap inside the (possibly new) host, persist rect + attachment.
local function endDrag()
  if not drag_x then return end
  drag_x, drag_y = nil, nil
  clampToHost()
  is_resize = true
  saveRect()
  saveAttach(host)
end

local function loop()
  if reaper.GetExtState(SECT, RUNKEY) ~= "1" then return shutdown() end

  -- "Attach to" changed → drop the box off the old host, re-acquire next.
  if g_rehost then
    g_rehost = false
    endIntercepts()
    if host and bitmap then
      reaper.JS_Composite(host, 0, 0, 0, 0, bitmap, 0, 0, 0, 0)
      if bm_x then
        reaper.JS_Window_InvalidateRect(host, bm_x, bm_y, bm_x + bm_w, bm_y + bm_h, false)
      end
    end
    if bitmap then reaper.JS_LICE_DestroyBitmap(bitmap); bitmap = nil end
    host = nil
    is_resize = true
  end

  if not host or not reaper.ValidatePtr(host, "HWND*") then
    local prev = host
    host = acquireHost()
    if not host then reaper.defer(loop); return end
    if host ~= prev then is_resize = true end   -- recomposite onto the new host
  end
  local _, w, h = reaper.JS_Window_GetClientSize(host)
  win_w, win_h = w, h

  if not bm_x then loadRect() end
  if not drag_x then clampToHost() end   -- never clamp mid-drag (would fight it)

  if is_resize then
    if fit_height then bm_h = metrics(); fit_height = false end  -- only on font/layout change
    clampToHost()
    applyComposite()
    is_resize = false
    is_redraw = true
  end

  local mx, my = reaper.GetMousePosition()
  local hov = reaper.JS_Window_FromPoint(mx, my)
  local cx, cy = reaper.JS_Window_ScreenToClient(host, mx, my)
  local m = 8   -- edge grab / hover slop (wider = easier to catch the L/R edges)
  local over = (hov == host or drag_x) and
               cx > bm_x - m and cx < bm_x + bm_w + m and
               cy > bm_y - m and cy < bm_y + bm_h + m

  if over then
    startIntercepts()

    -- Edge detection for resize cursor (only when not dragging).
    if not drag_x then
      local nf = 0
      if math.abs(cx - bm_x)          < m then nf = nf | 1 end
      if math.abs(cy - bm_y)          < m then nf = nf | 2 end
      if math.abs(cx - (bm_x + bm_w)) < m then nf = nf | 4 end
      if math.abs(cy - (bm_y + bm_h)) < m then nf = nf | 8 end
      if nf ~= resize_flags then resize_flags = nf; is_redraw = true end
      reaper.JS_Mouse_SetCursor(over and (nf > 0 and cursorFor(nf) or cur_move) or cur_normal)
    end

    -- Left down → begin drag (move or resize depending on resize_flags).
    if peeked("WM_LBUTTONDOWN") then
      if not (menu_time and reaper.time_precise() < menu_time + 0.1) then
        drag_x, drag_y = cx, cy
      end
    end
    -- Left up → end drag, persist rect + attachment.
    if peeked("WM_LBUTTONUP") then endDrag() end
    -- Right up → context menu.
    if peeked("WM_RBUTTONUP") then
      if not (menu_time and reaper.time_precise() < menu_time + 0.1) then
        showContextMenu(mx, my)
      end
    end
  else
    endIntercepts()
    if resize_flags ~= 0 then resize_flags = 0; is_redraw = true end
  end

  -- Dragging: resize, drag-to-attach (onto another window), or move within host.
  if drag_x and reaper.JS_Mouse_GetState(1) == 1 then
    if resize_flags > 0 then
      local dx, dy = cx - drag_x, cy - drag_y
      if dx ~= 0 or dy ~= 0 then
        -- Invalidate the OLD (possibly larger) rect first so shrinking doesn't smear.
        reaper.JS_Window_InvalidateRect(host, bm_x, bm_y, bm_x + bm_w, bm_y + bm_h, false)
        if resize_flags & 1 == 1 then local r = bm_x + bm_w; bm_x = math.min(cx, r - 80); bm_w = r - bm_x end
        if resize_flags & 2 == 2 then local b = bm_y + bm_h; bm_y = math.min(cy, b - 24); bm_h = b - bm_y end
        if resize_flags & 4 == 4 then bm_w = math.max(80, cx - bm_x) end
        if resize_flags & 8 == 8 then bm_h = math.max(24, cy - bm_y) end
        -- No clamp mid-resize: per-frame clampToHost re-pinned bm_x and made the
        -- opposite (right) edge drift while dragging the left edge → felt broken.
        -- endDrag() clamps into the host on release.
        applyComposite(); is_redraw = true
        drag_x, drag_y = cx, cy
      end
    elseif hov and hov ~= host and attachable(hov) then
      -- The mouse left the current host onto another window we CAN composite on
      -- (has a title / is a real docked toolbar) → re-host there.
      local offx, offy = drag_x - bm_x, drag_y - bm_y   -- anchor offset in box
      endIntercepts()
      reaper.JS_Composite(host, 0, 0, 0, 0, bitmap, 0, 0, 0, 0)
      reaper.JS_Composite_Delay(host, 0, 0, 0)   -- stop compositing on the old host
      reaper.JS_Window_InvalidateRect(host, bm_x, bm_y, bm_x + bm_w, bm_y + bm_h, false)
      host = hov
      reaper.JS_Window_SetFocus(host)
      local ncx, ncy = reaper.JS_Window_ScreenToClient(host, mx, my)
      bm_x, bm_y = ncx - offx, ncy - offy
      drag_x, drag_y = ncx, ncy
      local _, nw, nh = reaper.JS_Window_GetClientSize(host)
      win_w, win_h = nw, nh
      startIntercepts()
      is_resize = true     -- rebuild bitmap + composite on the new host next tick
    elseif hov == host then
      -- Move the box within the current host.
      local dx, dy = cx - drag_x, cy - drag_y
      if dx ~= 0 or dy ~= 0 then
        -- No clamp mid-drag so the box can reach an edge; endDrag() clamps it in.
        -- placeBox invalidates the old rect → no smear trail.
        placeBox(bm_x + dx, bm_y + dy); is_redraw = true
        drag_x, drag_y = cx, cy
      end
    else
      -- Mouse is over a window we must NOT composite on (arrange / TCP / ruler /
      -- main / any no-title window). Freeze the box on its current host instead
      -- of following the mouse there — compositing on those blacks them out.
      -- Keep the drag anchor synced so the box doesn't jump when the mouse comes
      -- back over a valid host.
      drag_x, drag_y = cx, cy
    end
  elseif drag_x and reaper.JS_Mouse_GetState(1) == 0 then
    endDrag()
  end

  -- Redraw only when something visible changed (data / colours / font / layout /
  -- rect / hover-edge) — never per-frame, so no flicker and near-zero idle cost.
  local sig = table.concat({
    reaper.GetExtState(SECT, "overlay_focus"),
    reaper.GetExtState(SECT, "overlay"),
    reaper.GetExtState(SECT, "overlay_param_cs"),
    reaper.GetExtState(SECT, "overlay_param_bc"),
    csRgb(), bcRgb(), font_px or 0, oneLine() and 1 or 0,
    num("focused_panel_corner", 6), num("focused_panel_bg", 0),
    num("focused_panel_border", 0),
    reaper.GetExtState(SECT, "focused_panel_trackname"),
    reaper.GetExtState(SECT, "focused_panel_track_abbr"),
    num("focused_panel_track_len", 8),
    bm_x, bm_y, bm_w, bm_h, resize_flags,
  }, "|")
  if is_redraw or sig ~= last_sig then
    drawContent()
    last_sig = sig
    is_redraw = false
  end

  reaper.defer(loop)
end

shutdown = function()
  endIntercepts()
  if host and bitmap then
    reaper.JS_Composite(host, 0, 0, 0, 0, bitmap, 0, 0, 0, 0)
    if bm_x then
      reaper.JS_Window_InvalidateRect(host, bm_x, bm_y, bm_x + bm_w, bm_y + bm_h, false)
    end
    reaper.JS_LICE_DestroyBitmap(bitmap)
    bitmap = nil
  end
  if lice_font then reaper.JS_LICE_DestroyFont(lice_font); lice_font = nil end
  reaper.SetExtState(SECT, RUNKEY, "0", false)
  setToggle(false)
end

reaper.atexit(shutdown)
setToggle(true)
loop()
