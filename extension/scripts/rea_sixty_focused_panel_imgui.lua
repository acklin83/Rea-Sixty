-- @description Rea-Sixty — Focused-track panel (ReaImGui spike, frameless, place-anywhere)
-- @author Störsender
-- @version 0.1.0
-- @provides [main] .
-- @about
--   SPIKE / parallel variant of rea_sixty_focused_panel.lua.
--
--   Same content (surface-focused track + active CS / BC plug-in + last-touched
--   param) and the SAME option ExtState keys (focused_panel_*), but rendered as a
--   real **ReaImGui** window instead of a JS_Composite LICE bitmap.
--
--   Why: the JS_Composite panel can only host on titled windows and turns the
--   arrange / TCP / ruler BLACK on macOS, so it can't be placed everywhere.
--   A ReaImGui frameless window is a real top-level OS window — drag it ANYWHERE,
--   over the arrange, over the TCP, onto a second monitor. No host-window
--   gymnastics, no black-out. Modelled on TK_TRANSPORT's window setup
--   (NoTitleBar | TopMost, no NoMove, no per-frame SetNextWindowPos).
--
--   Position + size persist under their OWN keys (focused_panel_imgui_*) so this
--   spike coexists with the shipping composite panel. Reads the same data the
--   extension publishes: overlay / overlay_focus / overlay_param_cs|bc.
--
--   Run to start; run again (or untick) to stop. Requires ReaImGui (>= 0.9).

local SECT   = "rea_sixty"
local RUNKEY = "focused_panel_imgui_running"

local _, _, sectionID, cmdID = reaper.get_action_context()

-- Single-instance toggle (same pattern as the composite panel).
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
    "Rea-Sixty Focused Panel (ImGui)", 0)
  reaper.SetExtState(SECT, RUNKEY, "0", false)
  setToggle(false)
  return
end

------------------------------------------------------------------------
-- Small helpers
------------------------------------------------------------------------
local function num(key, dflt)
  local v = tonumber(reaper.GetExtState(SECT, key))
  return v or dflt
end
local function oneLine() return reaper.GetExtState(SECT, "focused_panel_oneline") == "1" end

-- British/American spelling — mirrors the composite panel's sp() (ExtState
-- "ui_spelling": "1" = American "color", else British "colour").
local function sp(uk, us)
  return (reaper.GetExtState(SECT, "ui_spelling") == "1") and us or uk
end

-- Stored colours are 0xRRGGBB; ImGui wants 0xRRGGBBAA. SAME keys/defaults as the
-- composite panel so both stay colour-synced (overlay_cs_col / overlay_bc_col).
local function rgba(rgb, a)
  return ((rgb & 0xFFFFFF) << 8) | (a or 0xFF)
end
local function csRgb() return math.floor(num("overlay_cs_col", 0xFFFF00)) & 0xFFFFFF end
local function bcRgb() return math.floor(num("overlay_bc_col", 0xFF0000)) & 0xFFFFFF end

------------------------------------------------------------------------
-- Focused-track data — ported verbatim from rea_sixty_focused_panel.lua
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

local function trackDisplay(name)
  if not name then return nil end
  if reaper.GetExtState(SECT, "focused_panel_trackname") == "0" then return nil end
  if reaper.GetExtState(SECT, "focused_panel_track_abbr") == "1" then
    return smartAbbrev(name, math.max(1, math.floor(num("focused_panel_track_len", 8))))
  end
  return name
end

local function trackNameRgb(tr)
  if not tr then return nil end
  if reaper.GetExtState(SECT, "focused_panel_track_color") ~= "1" then return nil end
  local nat = reaper.GetTrackColor(tr)
  if not nat or nat == 0 then return nil end
  local rr, gg, bb = reaper.ColorFromNative(nat)
  return ((rr & 0xFF) << 16) | ((gg & 0xFF) << 8) | (bb & 0xFF)
end

------------------------------------------------------------------------
-- FX short-name resolution. Order of preference:
--   (1) user_plugins.json  — the FX-Learn short-name source of truth:
--       match-substring → displayShort. Cached; re-read on size change
--       (throttled to ~1 s) so edits in the FX-Learn editor show up live.
--   (2) SSL factory map    — SSL plug-ins are constexpr in the (un-readable)
--       extension and are NOT in user_plugins.json, so a small built-in
--       substring map gives "Bus Compressor 2" → "BC2", "Channel Strip 2"
--       → "CS2", etc.  (hud_state ExtState ALSO carries shorts but is only
--       published while the Learn-HUD runs — g_hudEnabled gate, main.cpp —
--       so we deliberately don't depend on it here: this stays always-on.)
--   (3) generic cleanup    — strip "VST3:"/"AU:" prefix + "(vendor)" suffix.
------------------------------------------------------------------------
-- Longest substring first so the most specific name wins ("…2" before "…").
local SSL_SHORTS = {
  { "Bus Compressor 2", "BC2" },
  { "Channel Strip 2",  "CS2" },
  { "BusCompressor 2",  "BC2" },
  { "ChannelStrip 2",   "CS2" },
  { "Bus Compressor",   "BC"  },
  { "Channel Strip",    "CS"  },
}

local userShorts      = nil   -- { {match=, short=}, ... }, longest match first
local userShorts_size = nil   -- byte size of the JSON when last parsed
local userShorts_next = 0     -- next time_precise() at which we re-check size

local function userPluginsPath()
  return reaper.GetResourcePath() .. "/rea_sixty/user_plugins.json"
end

local function loadUserShorts()
  local now = reaper.time_precise()
  if userShorts and now < userShorts_next then return end   -- throttle re-check
  userShorts_next = now + 1.0

  local path = userPluginsPath()
  local f = io.open(path, "rb")
  if not f then userShorts = userShorts or {}; return end
  local size = f:seek("end")
  if userShorts and size == userShorts_size then f:close(); return end
  f:seek("set", 0)
  local data = f:read("*a"); f:close()
  userShorts_size = size

  local list = {}
  -- `match` always precedes `displayShort` in each plugin object, and both keys
  -- are top-level-only (verified — never nested in slots/paramSnapshot), so a
  -- non-greedy paired gmatch reliably zips each match→short.
  for m, s in data:gmatch('"match"%s*:%s*"(.-)".-"displayShort"%s*:%s*"(.-)"') do
    if m ~= "" and s ~= "" then list[#list + 1] = { match = m, short = s } end
  end
  table.sort(list, function(a, b) return #a.match > #b.match end)
  userShorts = list
end

local function shortName(raw)
  if not raw or raw == "" then return raw end
  loadUserShorts()
  for _, e in ipairs(userShorts) do
    if raw:find(e.match, 1, true) then return e.short end
  end
  for _, e in ipairs(SSL_SHORTS) do
    if raw:find(e[1], 1, true) then return e[2] end
  end
  local nm = raw:gsub("^%u%u+%d*i?:%s*", "")
  nm = nm:gsub("%s*%([^()]-%)%s*$", "")
  return nm
end

local function fxLabel(tr, fxIdx)
  if not tr or not fxIdx or fxIdx < 0 then return nil end
  local _, nm = reaper.TrackFX_GetFXName(tr, fxIdx, "")
  if not nm or nm == "" then return nil end
  return shortName(nm)
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
-- ImGui context + font (recreate-on-change, like TK's font_needs_update)
------------------------------------------------------------------------
local ctx  = reaper.ImGui_CreateContext('Rea-Sixty Focused Panel')
-- ReaImGui v0.10: fonts are size-less objects; the size is supplied at PushFont
-- time, so we create+attach ONCE (2nd arg = style flags, not px) and just push a
-- different px each frame. (Pre-0.10 baked the size into CreateFont — if this
-- spike ever runs on an older ReaImGui, switch to recreate-on-change.)
local font = reaper.ImGui_CreateFont('sans-serif', 0)
reaper.ImGui_Attach(ctx, font)
local function fontPx()
  return math.max(8, math.floor(num("focused_panel_font", 18)))
end
local font_px = fontPx()

-- Restore saved position/size (own keys → coexists with composite panel).
local function loadRect()
  local raw = reaper.GetExtState(SECT, "focused_panel_imgui_rect")
  local x, y, w, h = raw:match("^(%-?%d+),(%-?%d+),(%d+),(%d+)$")
  if x then return tonumber(x), tonumber(y), tonumber(w), tonumber(h) end
  return nil
end
local function saveRect(x, y, w, h)
  reaper.SetExtState(SECT, "focused_panel_imgui_rect",
    string.format("%d,%d,%d,%d", x, y, w, h), true)
end

local restore_x, restore_y, restore_w, restore_h = loadRect()
local first_frame = true
local last_save_x, last_save_y, last_save_w, last_save_h

------------------------------------------------------------------------
-- One CS/BC segment. Uses SameLine to lay out coloured runs on a row.
------------------------------------------------------------------------
local function segment(tag, tagRgb, name, pn, pv, trk, trkRgb)
  local lit = name ~= nil
  local dim = lit and 0xB0B0BC or 0x6A6A74
  -- Track name position: before (default) or after the CS/BC tag+name+param.
  local after = reaper.GetExtState(SECT, "focused_panel_track_after") == "1"

  if trk ~= nil and not after then
    reaper.ImGui_TextColored(ctx, rgba(trkRgb or dim), (trk or "") .. "  ")
    reaper.ImGui_SameLine(ctx, 0, 0)
  end
  reaper.ImGui_TextColored(ctx, rgba(tagRgb), tag .. " ")
  reaper.ImGui_SameLine(ctx, 0, 0)
  reaper.ImGui_TextColored(ctx, rgba(lit and 0xEDEDF2 or 0x6A6A74),
    name or "\xE2\x80\x94")
  if pn and pn ~= "" then
    reaper.ImGui_SameLine(ctx, 0, 0)
    reaper.ImGui_TextColored(ctx, rgba(0xA8C6F0), "   " .. pn)
    if pv and pv ~= "" then
      reaper.ImGui_SameLine(ctx, 0, 0)
      reaper.ImGui_TextColored(ctx, rgba(0x9A9AA2), " " .. pv)
    end
  end
  if trk ~= nil and after then
    reaper.ImGui_SameLine(ctx, 0, 0)
    reaper.ImGui_TextColored(ctx, rgba(trkRgb or dim), "  " .. (trk or ""))
  end
end

local function drawContent()
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

  if oneLine() then
    segment("CS", csRgb(), csName, csPN, csPV, csTrk, csTrkCol)
    reaper.ImGui_SameLine(ctx, 0, font_px)   -- gap between CS and BC
    segment("BC", bcRgb(), bcName, bcPN, bcPV, bcTrk, bcTrkCol)
  else
    segment("CS", csRgb(), csName, csPN, csPV, csTrk, csTrkCol)
    segment("BC", bcRgb(), bcName, bcPN, bcPV, bcTrk, bcTrkCol)
  end
end

------------------------------------------------------------------------
-- Right-click menu — ported from the composite panel. Native modal dialogs
-- (GetUserInputs / GR_SelectColor) work fine from an ImGui defer loop. No
-- is_resize/is_redraw flags here: immediate mode re-reads ExtState every frame,
-- so every change applies on the next frame automatically.
------------------------------------------------------------------------
local function setFontSize()
  local ret, input = reaper.GetUserInputs("Panel font", 1,
    "Font size: (e.g. 18),extrawidth=40", tostring(fontPx()))
  if not ret then return end
  local fs = tonumber(input)
  if fs then
    reaper.SetExtState(SECT, "focused_panel_font", tostring(math.max(8, math.floor(fs))), true)
  end
end

local function setCorner()
  local ret, input = reaper.GetUserInputs("Corners", 1,
    "Corner radius: (e.g. 6),extrawidth=40", tostring(math.floor(num("focused_panel_corner", 6))))
  if not ret then return end
  local v = tonumber(input)
  if v then reaper.SetExtState(SECT, "focused_panel_corner", tostring(math.max(0, math.floor(v))), true) end
end

local function setTrackLen()
  local ret, input = reaper.GetUserInputs("Track name length", 1,
    "Smart-abbrev length: (e.g. 8),extrawidth=40", tostring(math.floor(num("focused_panel_track_len", 8))))
  if not ret then return end
  local v = tonumber(input)
  if v then reaper.SetExtState(SECT, "focused_panel_track_len", tostring(math.max(1, math.floor(v))), true) end
end

local function chooseColor(key)
  local ret, color = reaper.GR_SelectColor(reaper.GetMainHwnd())
  if ret ~= 0 then
    local cr, cg, cb = reaper.ColorFromNative(color)
    reaper.SetExtState(SECT, key, tostring(cr * 65536 + cg * 256 + cb), true)
  end
end

-- Toggle a 0/1 ExtState key (treats `def` as the "unset" default).
local function toggleKey(key, def_on)
  local cur = reaper.GetExtState(SECT, key)
  local on = (cur == "") and def_on or (cur == "1")
  reaper.SetExtState(SECT, key, on and "0" or "1", true)
end

------------------------------------------------------------------------
-- "Load on startup" — this is a standalone ReaScript, so autostart = adding a
-- one-liner to REAPER's Scripts/__startup.lua shim. Our line is bracketed with
-- markers so any other __startup.lua content is preserved. The file itself is
-- the source of truth (no ExtState mirror to desync).
------------------------------------------------------------------------
local STARTUP_BEGIN = "-- >>> rea_sixty_focused_panel_imgui (auto)"
local STARTUP_END   = "-- <<< rea_sixty_focused_panel_imgui (auto)"

local function startupPath() return reaper.GetResourcePath() .. "/Scripts/__startup.lua" end

-- This script's command id as a "_RS..." string usable by NamedCommandLookup.
local function selfCommandName()
  if not cmdID then return nil end
  local nm = reaper.ReverseNamedCommandLookup(cmdID)
  if not nm or nm == "" then return nil end
  return "_" .. nm
end

local function readFileAll(path)
  local f = io.open(path, "rb"); if not f then return nil end
  local s = f:read("*a"); f:close(); return s
end

local function litPat(s) return (s:gsub("[%(%)%.%%%+%-%*%?%[%]%^%$]", "%%%1")) end

-- Remove our marker block (BEGIN…END inclusive + trailing newline) from content.
local function stripStartupBlock(content)
  if not content then return "" end
  local pat = litPat(STARTUP_BEGIN) .. ".-" .. litPat(STARTUP_END) .. "%s*\n?"
  return (content:gsub(pat, ""))
end

local function startupEnabled()
  local content = readFileAll(startupPath())
  return content ~= nil and content:find(STARTUP_BEGIN, 1, true) ~= nil
end

local function setStartup(on)
  local cmd = selfCommandName()
  if not cmd then
    reaper.MB("Could not resolve this script's command id for autostart.\n"
      .. "(Run it once from the Action List so REAPER assigns one.)",
      "Rea-Sixty Focused Panel", 0)
    return
  end
  local content = stripStartupBlock(readFileAll(startupPath()) or "")
  if on then
    if content ~= "" and not content:match("\n$") then content = content .. "\n" end
    content = content
      .. STARTUP_BEGIN .. "\n"
      .. "reaper.Main_OnCommand(reaper.NamedCommandLookup('" .. cmd .. "'), 0)\n"
      .. STARTUP_END .. "\n"
  end
  local f = io.open(startupPath(), "wb")
  if not f then reaper.MB("Could not write:\n" .. startupPath(), "Rea-Sixty Focused Panel", 0); return end
  f:write(content); f:close()
end

local POPUP_ID = "##fp_ctx"

-- Build the popup body. Mirrors the composite panel's menu tree.
local function drawContextMenu()
  if not reaper.ImGui_BeginPopup(ctx, POPUP_ID) then return end

  local one = oneLine()
  if reaper.ImGui_BeginMenu(ctx, "Layout") then
    if reaper.ImGui_MenuItem(ctx, "Two lines (CS / BC)", nil, not one) then
      reaper.SetExtState(SECT, "focused_panel_oneline", "0", true)
    end
    if reaper.ImGui_MenuItem(ctx, "One line", nil, one) then
      reaper.SetExtState(SECT, "focused_panel_oneline", "1", true)
    end
    reaper.ImGui_EndMenu(ctx)
  end

  if reaper.ImGui_BeginMenu(ctx, "Track name") then
    local showName = reaper.GetExtState(SECT, "focused_panel_trackname") ~= "0"
    if reaper.ImGui_MenuItem(ctx, "Show track name", nil, showName) then
      toggleKey("focused_panel_trackname", true)
    end
    local useCol = reaper.GetExtState(SECT, "focused_panel_track_color") == "1"
    if reaper.ImGui_MenuItem(ctx, sp("Use track colour", "Use track color"), nil, useCol) then
      toggleKey("focused_panel_track_color", false)
    end
    reaper.ImGui_Separator(ctx)
    local smart = reaper.GetExtState(SECT, "focused_panel_track_abbr") == "1"
    if reaper.ImGui_MenuItem(ctx, "Full name", nil, not smart) then
      reaper.SetExtState(SECT, "focused_panel_track_abbr", "0", true)
    end
    if reaper.ImGui_MenuItem(ctx, "Smart abbreviate", nil, smart) then
      reaper.SetExtState(SECT, "focused_panel_track_abbr", "1", true)
    end
    if reaper.ImGui_MenuItem(ctx, "Abbreviation length\xE2\x80\xA6") then setTrackLen() end
    reaper.ImGui_Separator(ctx)
    local after = reaper.GetExtState(SECT, "focused_panel_track_after") == "1"
    if reaper.ImGui_MenuItem(ctx, "Before CS/BC", nil, not after) then
      reaper.SetExtState(SECT, "focused_panel_track_after", "0", true)
    end
    if reaper.ImGui_MenuItem(ctx, "After CS/BC", nil, after) then
      reaper.SetExtState(SECT, "focused_panel_track_after", "1", true)
    end
    reaper.ImGui_EndMenu(ctx)
  end

  if reaper.ImGui_BeginMenu(ctx, "Customize") then
    if reaper.ImGui_MenuItem(ctx, "Font size\xE2\x80\xA6")     then setFontSize() end
    if reaper.ImGui_MenuItem(ctx, "Corner radius\xE2\x80\xA6") then setCorner() end
    reaper.ImGui_Separator(ctx)
    if reaper.ImGui_MenuItem(ctx, sp("Background colour\xE2\x80\xA6", "Background color\xE2\x80\xA6")) then chooseColor("focused_panel_bg") end
    if reaper.ImGui_MenuItem(ctx, sp("Border colour\xE2\x80\xA6", "Border color\xE2\x80\xA6"))         then chooseColor("focused_panel_border") end
    if reaper.ImGui_MenuItem(ctx, sp("CS colour\xE2\x80\xA6", "CS color\xE2\x80\xA6"))                 then chooseColor("overlay_cs_col") end
    if reaper.ImGui_MenuItem(ctx, sp("BC colour\xE2\x80\xA6", "BC color\xE2\x80\xA6"))                 then chooseColor("overlay_bc_col") end
    reaper.ImGui_EndMenu(ctx)
  end

  reaper.ImGui_Separator(ctx)
  reaper.ImGui_BeginDisabled(ctx)
  reaper.ImGui_MenuItem(ctx, "Drag the box anywhere to move it")
  reaper.ImGui_EndDisabled(ctx)

  reaper.ImGui_Separator(ctx)
  local startOn = startupEnabled()
  if reaper.ImGui_MenuItem(ctx, "Load on startup", nil, startOn) then
    setStartup(not startOn)
  end
  if reaper.ImGui_MenuItem(ctx, "Close panel") then
    reaper.SetExtState(SECT, RUNKEY, "0", false)
  end

  reaper.ImGui_EndPopup(ctx)
end

------------------------------------------------------------------------
-- Main loop
------------------------------------------------------------------------
local WFLAGS = reaper.ImGui_WindowFlags_NoTitleBar()
  | reaper.ImGui_WindowFlags_NoScrollbar()
  | reaper.ImGui_WindowFlags_NoScrollWithMouse()
  | reaper.ImGui_WindowFlags_NoCollapse()
  | (reaper.ImGui_WindowFlags_TopMost and reaper.ImGui_WindowFlags_TopMost() or 0)

local function loop()
  if reaper.GetExtState(SECT, RUNKEY) ~= "1" then
    setToggle(false)
    return  -- stop deferring → context GC'd
  end

  font_px = fontPx()

  -- Restore saved geometry on first frame only; afterwards the OS/ImGui owns the
  -- window position (this is exactly why it's freely draggable — we do NOT call
  -- SetNextWindowPos every frame).
  if first_frame then
    if restore_x then
      reaper.ImGui_SetNextWindowPos(ctx, restore_x, restore_y)
      reaper.ImGui_SetNextWindowSize(ctx, restore_w, restore_h)
    else
      reaper.ImGui_SetNextWindowSize(ctx, 320, oneLine() and 40 or 64)
    end
    first_frame = false
  end

  local bg = math.floor(num("focused_panel_bg",     0x1C1C1F)) & 0xFFFFFF
  local bd = math.floor(num("focused_panel_border", 0x3A3D44)) & 0xFFFFFF
  local corner = math.floor(num("focused_panel_corner", 6))

  reaper.ImGui_PushStyleColor(ctx, reaper.ImGui_Col_WindowBg(), rgba(bg, 0xF0))
  reaper.ImGui_PushStyleColor(ctx, reaper.ImGui_Col_Border(),   rgba(bd))
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_WindowRounding(), corner)
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_WindowBorderSize(), 1)

  reaper.ImGui_PushFont(ctx, font, font_px)   -- v0.10: size at push time
  -- ReaImGui 0.10 / Dear ImGui 1.92 rule: End() is PAIRED with the `visible`
  -- branch — call it ONLY when Begin returns true. Calling End unconditionally
  -- (the old <=0.9 convention) raises "Calling End() too many times!" whenever the
  -- window is collapsed / clipped / fully off-screen (visible == false). The
  -- official ReaImGui_Demo does exactly this: `if not rv then return` / End inside.
  local visible, open = reaper.ImGui_Begin(ctx, 'Rea-Sixty Focused##fp', true, WFLAGS)
  if visible then
    drawContent()

    -- Right-click anywhere in the window → context menu (whole-window hit area).
    if reaper.ImGui_IsWindowHovered(ctx)
       and reaper.ImGui_IsMouseClicked(ctx, 1) then
      reaper.ImGui_OpenPopup(ctx, POPUP_ID)
    end
    drawContextMenu()

    -- Persist geometry when it changes (throttled to actual deltas).
    local px, py = reaper.ImGui_GetWindowPos(ctx)
    local pw, ph = reaper.ImGui_GetWindowSize(ctx)
    px, py, pw, ph = math.floor(px), math.floor(py), math.floor(pw), math.floor(ph)
    if px ~= last_save_x or py ~= last_save_y or pw ~= last_save_w or ph ~= last_save_h then
      last_save_x, last_save_y, last_save_w, last_save_h = px, py, pw, ph
      saveRect(px, py, pw, ph)
    end
    reaper.ImGui_End(ctx)   -- only when visible (see note at Begin)
  end
  -- Pop* are UNCONDITIONAL: they balance the pushes made BEFORE Begin (font +
  -- style stacks are independent of the window stack).
  reaper.ImGui_PopFont(ctx)
  reaper.ImGui_PopStyleVar(ctx, 2)
  reaper.ImGui_PopStyleColor(ctx, 2)

  if open then
    reaper.defer(loop)
  else
    reaper.SetExtState(SECT, RUNKEY, "0", false)
    setToggle(false)
  end
end

reaper.atexit(function()
  reaper.SetExtState(SECT, RUNKEY, "0", false)
  setToggle(false)
end)

reaper.defer(loop)
