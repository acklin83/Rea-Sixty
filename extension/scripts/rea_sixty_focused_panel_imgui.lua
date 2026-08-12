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

-- Interactive favourite back-channel (Phase 4c). The panel writes commands to
-- "focused_panel_cmd"; the extension drains them (favmode / favset_cs_track / favset_bc_track).
local function sendPanelCmd(s) reaper.SetExtState(SECT, "focused_panel_cmd", s, false) end

-- "overlay_fav" = "<csOwn><bcOwn>\t<CS set>\t<BC set>" (own per domain; set empty
-- = base favourites). Returns csOwn, bcOwn, csSet, bcSet.
local function readFav()
  local raw = reaper.GetExtState(SECT, "overlay_fav")
  local own, cs, bc = raw:match("^(%d%d)\t([^\t]*)\t(.*)$")
  own = own or "00"
  return own:sub(1, 1) == "1", own:sub(2, 2) == "1", cs or "", bc or ""
end

-- "overlay_cs_sets" / "overlay_bc_sets" = newline-separated set names.
local function readSets(key)
  local out = {}
  for line in reaper.GetExtState(SECT, key):gmatch("[^\n]+") do
    out[#out + 1] = line
  end
  return out
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

-- Insert-list active marker, stripped so a renamed FX doesn't show the
-- decoration. Must stay in step with kInsertMarkers_ in main.cpp.
local INSERT_MARKERS = { "\226\150\182 ", "\226\151\137CS ", "\226\151\137BC ", "[*] " }

local function stripInsertMarker(s)
  for _, m in ipairs(INSERT_MARKERS) do
    if s:sub(1, #m) == m then return s:sub(#m + 1) end
  end
  return s
end

local function fxLabel(tr, fxIdx)
  if not tr or not fxIdx or fxIdx < 0 then return nil end
  -- A USER RENAME WINS over the factory name — same precedence the surfaces
  -- use (fxUserRename_ → instanceLabel_ on UF8/UC1/UF1). TrackFX_GetFXName
  -- returns the factory name regardless of the rename, so asking only that
  -- left this panel showing the original while every surface showed the
  -- rename (Frank 2026-08-12). Not run through shortName(): the rename is
  -- the user's own wording, with no "VST3:" prefix or vendor tail to trim.
  local okRn, rn = reaper.TrackFX_GetNamedConfigParm(tr, fxIdx, "renamed_name")
  if okRn and rn and rn ~= "" then return stripInsertMarker(rn) end
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
-- Docking MUST be enabled at context creation, else the window can't dock and
-- ImGui_GetWindowDockID always returns 0 — so dock state can never persist (the
-- bug Frank hit with the top toolbar docker). Proven pattern: rodilab Color
-- palette. Frank 2026-06-27.
local DOCK_CFG = reaper.ImGui_ConfigFlags_DockingEnable
  and reaper.ImGui_ConfigFlags_DockingEnable() or 0
local ctx  = reaper.ImGui_CreateContext('Rea-Sixty Focused Panel', DOCK_CFG)
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
local g_contentW, g_contentH               -- last measured content size (centering)
local mb_text, mb_seq, mb_until = "", nil, 0   -- in-panel mode-change flash state

-- Dock state. ReaImGui dock id: 0 = floating, <0 = a REAPER docker cell. We
-- persist it under its own key and re-attach on first frame, so a docked panel
-- survives restart (the saved rect alone can't — it only restores a FLOATING
-- position). Guarded: older ReaImGui without the dock API stays floating-only.
local has_dock = DOCK_CFG ~= 0
  and reaper.APIExists("ImGui_SetNextWindowDockID")
  and reaper.APIExists("ImGui_GetWindowDockID")
local restore_dock  = tonumber(reaper.GetExtState(SECT, "focused_panel_imgui_dock")) or 0
-- Preferred docker to re-dock into when toggling float→dock (last one used; a
-- REAPER docker is a negative id). Kept separate so undocking to 0 doesn't lose it.
local dock_pref     = tonumber(reaper.GetExtState(SECT, "focused_panel_imgui_dockpref")) or -1
if dock_pref >= 0 then dock_pref = -1 end
local cur_dock      = restore_dock
local last_save_dock = restore_dock
local pending_dock  = nil   -- menu-requested dock id, applied on the next frame

------------------------------------------------------------------------
-- One CS/BC segment. Uses SameLine to lay out coloured runs on a row.
------------------------------------------------------------------------
-- Float/unfloat the FX window for a clicked plug-in name. idx = the REAL chain
-- index the extension publishes (overlay_idx_cs|bc), not the visual MCP slot.
local function toggleFxOpen(tr, idx)
  if not (tr and idx and idx >= 0) then return end
  if not reaper.ValidatePtr2(0, tr, "MediaTrack*") then return end
  if idx >= reaper.TrackFX_GetCount(tr) then return end
  reaper.TrackFX_Show(tr, idx, reaper.TrackFX_GetOpen(tr, idx) and 2 or 3)
end

local function segment(tag, tagRgb, name, pn, pv, trk, trkRgb, openTr, openIdx)
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
  -- Click the plug-in name → float/unfloat its FX window (hand cursor = affordance).
  if lit and openTr and openIdx and openIdx >= 0
     and reaper.GetExtState(SECT, "focused_panel_open_click") ~= "0" then
    if reaper.ImGui_IsItemHovered(ctx) then
      reaper.ImGui_SetMouseCursor(ctx, reaper.ImGui_MouseCursor_Hand())
    end
    if reaper.ImGui_IsItemClicked(ctx) then toggleFxOpen(openTr, openIdx) end
  end
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

------------------------------------------------------------------------
-- Optional extra elements (toggled via the context menu → Elements).
------------------------------------------------------------------------
local function runAction(cmdName)
  local id = reaper.NamedCommandLookup(cmdName)
  if id ~= 0 then reaper.Main_OnCommand(id, 0) end
end

-- Watch the extension's mode-change seq; flash the new label for a couple of
-- seconds. Polled every frame so enabling the flash later never replays an old
-- change, and a panel started mid-session syncs the baseline silently.
local function pollModeBanner()
  local raw = reaper.GetExtState(SECT, "mode_banner")
  local text, seq = raw:match("^(.-)\t(%d+)$")
  if not seq then return end
  if mb_seq == nil then mb_seq = seq; return end   -- baseline only, no flash
  if seq ~= mb_seq then
    mb_seq, mb_text = seq, text or ""
    mb_until = reaper.time_precise() + math.max(0.3, num("mode_banner_secs", 2.0))
  end
end

-- Persistent current-mode line ("mode_state" = "<sel>\t<enc>").
local function drawModeIndicator()
  local sel, enc = reaper.GetExtState(SECT, "mode_state"):match("^(.-)\t(.*)$")
  if not sel then return end
  reaper.ImGui_TextColored(ctx, rgba(0x8890A0), "Sel ")
  reaper.ImGui_SameLine(ctx, 0, 0)
  reaper.ImGui_TextColored(ctx, rgba(0xC8D0E0), sel)
  reaper.ImGui_SameLine(ctx, 0, font_px)
  reaper.ImGui_TextColored(ctx, rgba(0x8890A0), "Enc ")
  reaper.ImGui_SameLine(ctx, 0, 0)
  reaper.ImGui_TextColored(ctx, rgba(0xC8D0E0), enc)
end

local function drawButtons()
  if reaper.ImGui_Button(ctx, "Settings") then runAction("_REASIXTY_TOGGLE_SETTINGS") end
  reaper.ImGui_SameLine(ctx, 0, math.max(4, font_px * 0.5))
  if reaper.ImGui_Button(ctx, "HUD") then runAction("_REASIXTY_LEARN_HUD_TOGGLE") end
end

local function drawCycle()
  local function pair(label, prevCmd, nextCmd)
    reaper.ImGui_TextColored(ctx, rgba(0x9A9AA2), label .. " ")
    reaper.ImGui_SameLine(ctx, 0, 0)
    if reaper.ImGui_Button(ctx, "\xE2\x97\x80##p" .. label) then runAction(prevCmd) end
    reaper.ImGui_SameLine(ctx, 0, 2)
    if reaper.ImGui_Button(ctx, "\xE2\x96\xB6##n" .. label) then runAction(nextCmd) end
  end
  pair("CS", "_REASIXTY_CS_CYCLE_PREV", "_REASIXTY_CS_CYCLE_NEXT")
  reaper.ImGui_SameLine(ctx, 0, font_px)
  pair("BC", "_REASIXTY_BC_CYCLE_PREV", "_REASIXTY_BC_CYCLE_NEXT")
end

------------------------------------------------------------------------
-- Element ordering. Blocks render in a user-defined order, inline (one row) by
-- default; a block flagged "new line" starts a fresh row. Arrange mode shows a
-- drag grip per block so the order can be rearranged by dragging. Frank 2026-06-27.
------------------------------------------------------------------------
local BLOCK_IDS = { "params", "fav", "mode", "cycle", "buttons" }
local has_dnd = reaper.APIExists("ImGui_BeginDragDropSource")
  and reaper.APIExists("ImGui_AcceptDragDropPayload")

-- Saved order, sanitised: known ids only, no dups, with any newly-added block
-- appended so it can never go missing.
local function parseOrder()
  local seen, out = {}, {}
  for id in reaper.GetExtState(SECT, "focused_panel_order"):gmatch("[^,]+") do
    for _, k in ipairs(BLOCK_IDS) do
      if k == id and not seen[id] then out[#out + 1] = id; seen[id] = true end
    end
  end
  for _, k in ipairs(BLOCK_IDS) do
    if not seen[k] then out[#out + 1] = k end
  end
  return out
end

local function saveOrder(order)
  reaper.SetExtState(SECT, "focused_panel_order", table.concat(order, ","), true)
end

local function breakSet()
  local s = {}
  for id in reaper.GetExtState(SECT, "focused_panel_breaks"):gmatch("[^,]+") do s[id] = true end
  return s
end

local function toggleBreak(id)
  local s = breakSet(); s[id] = (not s[id]) or nil
  local out = {}
  for _, k in ipairs(BLOCK_IDS) do if s[k] then out[#out + 1] = k end end
  reaper.SetExtState(SECT, "focused_panel_breaks", table.concat(out, ","), true)
end

-- Move `src` to just before `dst`, persist.
local function moveBefore(order, src, dst)
  if src == dst then return end
  local out = {}
  for _, id in ipairs(order) do if id ~= src then out[#out + 1] = id end end
  local res = {}
  for _, id in ipairs(out) do
    if id == dst then res[#res + 1] = src end
    res[#res + 1] = id
  end
  saveOrder(res)
end

local function drawContent()
  local byGuid = readActive()
  local fGuid  = reaper.GetExtState(SECT, "overlay_focus")
  local ftr, fMaster = findTrackByGuid(fGuid)
  local fa = byGuid[fGuid]
  local csTrk  = ftr and trackDisplay(trackLabel(ftr, fMaster)) or nil
  -- Plug-in name comes from the extension (correct chain index + rename-safe),
  -- NOT from fxLabel(slot) — the overlay body carries the visual slot, which
  -- mis-indexed GetFXName when the chain had gaps. Frank 2026-06-26.
  local csIdent = reaper.GetExtState(SECT, "overlay_name_cs")
  local csName = (csIdent ~= "") and shortName(csIdent) or nil
  local csTrkCol = trackNameRgb(ftr)

  local bcGuid, bcIdx = findActiveBc(byGuid)
  local btr, bMaster = nil, false
  if bcGuid then btr, bMaster = findTrackByGuid(bcGuid) end
  local bcTrk  = btr and trackDisplay(trackLabel(btr, bMaster)) or nil
  local bcIdent = reaper.GetExtState(SECT, "overlay_name_bc")
  local bcName = (bcIdent ~= "") and shortName(bcIdent) or nil
  local bcTrkCol = trackNameRgb(btr)

  local csPN, csPV = paramStr("overlay_param_cs")
  local bcPN, bcPV = paramStr("overlay_param_bc")

  -- Real chain index (click-to-open), published by the extension.
  local csOpenIdx = tonumber(reaper.GetExtState(SECT, "overlay_idx_cs"))
  local bcOpenIdx = tonumber(reaper.GetExtState(SECT, "overlay_idx_bc"))

  -- Favourite controls (Phase 4c) — copy/own toggle + per-track set picker,
  -- each independently shown (Layout → Favourites), positioned on their own
  -- line or inline before/after the CS/BC parameter line. Frank 2026-06-26.
  local showMode = reaper.GetExtState(SECT, "focused_panel_fav_mode") == "1"
  local showSet  = reaper.GetExtState(SECT, "focused_panel_fav_set")  == "1"
  local anyFav = showMode or showSet

  local function drawFav()
    local csOwn, bcOwn, curCs, curBc = readFav()
    local drew = false
    if showMode then
      -- Per-domain copy/own (Frank 2026-06-26). Full ImGui_Button so the height
      -- matches the set combo.
      if reaper.ImGui_Button(ctx, csOwn and "CS: Own" or "CS: Copy") then
        sendPanelCmd("favmode;cs;" .. (csOwn and "0" or "1"))
      end
      reaper.ImGui_SameLine(ctx, 0, font_px)
      if reaper.ImGui_Button(ctx, bcOwn and "BC: Own" or "BC: Copy") then
        sendPanelCmd("favmode;bc;" .. (bcOwn and "0" or "1"))
      end
      drew = true
    end
    if showSet then
      -- CS and BC sets are assigned independently (Frank 2026-06-26).
      local function setCombo(label, key, curVal, cmd)
        if drew then reaper.ImGui_SameLine(ctx, 0, font_px) end
        reaper.ImGui_SetNextItemWidth(ctx, 150)
        local preview = label .. ": " .. ((curVal ~= "" and curVal) or "(base)")
        if reaper.ImGui_BeginCombo(ctx, "##fav_" .. label, preview) then
          if reaper.ImGui_Selectable(ctx, "(Base Favourites)", curVal == "") then
            sendPanelCmd(cmd .. ";")
          end
          for _, nm in ipairs(readSets(key)) do
            if reaper.ImGui_Selectable(ctx, nm, curVal == nm) then
              sendPanelCmd(cmd .. ";" .. nm)
            end
          end
          reaper.ImGui_EndCombo(ctx)
        end
        drew = true
      end
      setCombo("CS", "overlay_cs_sets", curCs, "favset_cs_track")
      setCombo("BC", "overlay_bc_sets", curBc, "favset_bc_track")
    end
  end

  -- Params block: the CS/BC segments (oneLine controls their internal stacking;
  -- the segments can be hidden entirely so someone may want only fav controls).
  local function drawParams()
    segment("CS", csRgb(), csName, csPN, csPV, csTrk, csTrkCol, ftr, csOpenIdx)
    if oneLine() then reaper.ImGui_SameLine(ctx, 0, font_px) end
    segment("BC", bcRgb(), bcName, bcPN, bcPV, bcTrk, bcTrkCol, btr, bcOpenIdx)
  end

  -- Block registry: id → render fn + whether it shows this frame.
  local blocks = {
    params  = { draw = drawParams,        show = reaper.GetExtState(SECT, "focused_panel_params") ~= "0" },
    fav     = { draw = drawFav,           show = anyFav },
    mode    = { draw = drawModeIndicator, show = reaper.GetExtState(SECT, "focused_panel_mode")    == "1" },
    cycle   = { draw = drawCycle,         show = reaper.GetExtState(SECT, "focused_panel_cycle")   == "1" },
    buttons = { draw = drawButtons,       show = reaper.GetExtState(SECT, "focused_panel_buttons") == "1" },
  }

  -- Transient mode-change flash sits above the flow (not reorderable).
  pollModeBanner()
  if reaper.GetExtState(SECT, "focused_panel_banner") == "1"
     and reaper.time_precise() < mb_until and mb_text ~= "" then
    reaper.ImGui_TextColored(ctx, rgba(0x70E0A0), mb_text)
  end

  -- Flow blocks in the saved order: inline (one row) unless flagged new-line.
  -- Arrange mode adds a per-block drag grip + a new-line toggle, and disables the
  -- block content so dragging never trips a button/click.
  local arrange = has_dnd and reaper.GetExtState(SECT, "focused_panel_arrange") == "1"
  local order   = parseOrder()
  local brk     = breakSet()
  local first   = true
  for _, id in ipairs(order) do
    local b = blocks[id]
    if b and b.show then
      if not first and not brk[id] then reaper.ImGui_SameLine(ctx, 0, font_px) end
      reaper.ImGui_PushID(ctx, id)
      if arrange then
        reaper.ImGui_Button(ctx, "\xE2\xA0\xBF")        -- ⠿ drag grip
        if reaper.ImGui_BeginDragDropSource(ctx) then
          reaper.ImGui_SetDragDropPayload(ctx, "FP_BLOCK", id)
          reaper.ImGui_Text(ctx, id)
          reaper.ImGui_EndDragDropSource(ctx)
        end
        if reaper.ImGui_BeginDragDropTarget(ctx) then
          local ok, payload = reaper.ImGui_AcceptDragDropPayload(ctx, "FP_BLOCK")
          if ok and payload and payload ~= "" then moveBefore(order, payload, id) end
          reaper.ImGui_EndDragDropTarget(ctx)
        end
        reaper.ImGui_SameLine(ctx, 0, 2)
        -- ↵ = breaks to a new line before this block; → = stays inline.
        if reaper.ImGui_Button(ctx, brk[id] and "\xE2\x86\xB5" or "\xE2\x86\x92") then
          toggleBreak(id)
        end
        reaper.ImGui_SameLine(ctx, 0, 6)
        reaper.ImGui_BeginDisabled(ctx)
        b.draw()
        reaper.ImGui_EndDisabled(ctx)
      else
        b.draw()
      end
      reaper.ImGui_PopID(ctx)
      first = false
    end
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
    -- Guard the launch: the extension also auto-restores the panel at boot
    -- (from the persisted focused_panel_on flag). Without this guard, whichever
    -- runs second fires Main_OnCommand on the already-running script and REAPER
    -- pops its "script is running — terminate / new instance?" dialog every
    -- start (Frank 2026-06-17). The RUNKEY check makes both launchers cooperate.
    content = content
      .. STARTUP_BEGIN .. "\n"
      .. "if reaper.GetExtState('rea_sixty','focused_panel_imgui_running') ~= '1' then "
      .. "reaper.Main_OnCommand(reaper.NamedCommandLookup('" .. cmd .. "'), 0) end\n"
      .. STARTUP_END .. "\n"
  end
  local f = io.open(startupPath(), "wb")
  if not f then reaper.MB("Could not write:\n" .. startupPath(), "Rea-Sixty Focused Panel", 0); return end
  f:write(content); f:close()
end

-- One-time upgrade: builds before 2026-06-17 wrote an UNguarded startup line
-- that popped REAPER's re-entry dialog every boot. If our block exists but
-- lacks the running guard, rewrite it in the new guarded form (self-heals on
-- the next start; runs only on the real first instance, past the RUNKEY guard).
if startupEnabled() then
  local c = readFileAll(startupPath()) or ""
  if not c:find("focused_panel_imgui_running", 1, true) then setStartup(true) end
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
    reaper.ImGui_Separator(ctx)
    local showParams = reaper.GetExtState(SECT, "focused_panel_params") ~= "0"
    if reaper.ImGui_MenuItem(ctx, "Show CS / BC parameters", nil, showParams) then
      toggleKey("focused_panel_params", true)
    end
    local showMode = reaper.GetExtState(SECT, "focused_panel_fav_mode") == "1"
    if reaper.ImGui_MenuItem(ctx, "Favourite: copy/own toggle", nil, showMode) then
      toggleKey("focused_panel_fav_mode", false)
    end
    local showSet = reaper.GetExtState(SECT, "focused_panel_fav_set") == "1"
    if reaper.ImGui_MenuItem(ctx, "Favourite: set picker", nil, showSet) then
      toggleKey("focused_panel_fav_set", false)
    end
    if has_dnd then
      reaper.ImGui_Separator(ctx)
      local arr = reaper.GetExtState(SECT, "focused_panel_arrange") == "1"
      if reaper.ImGui_MenuItem(ctx, "Arrange elements (drag to reorder)", nil, arr) then
        toggleKey("focused_panel_arrange", false)
      end
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

  if reaper.ImGui_BeginMenu(ctx, "Elements") then
    local function item(label, key, def)
      local cur = reaper.GetExtState(SECT, key)
      local on = (cur == "") and def or (cur == "1")
      if reaper.ImGui_MenuItem(ctx, label, nil, on) then toggleKey(key, def) end
    end
    item("Mode indicator (Sel / Encoder)",        "focused_panel_mode",       false)
    item("Flash mode changes",                    "focused_panel_banner",     false)
    item("Settings + HUD buttons",                "focused_panel_buttons",    false)
    item("CS / BC cycle buttons",                 "focused_panel_cycle",      false)
    item("Click plug-in name to open",            "focused_panel_open_click", true)
    reaper.ImGui_EndMenu(ctx)
  end

  if reaper.ImGui_BeginMenu(ctx, "Align") then
    local ch = reaper.GetExtState(SECT, "focused_panel_center_h") == "1"
    if reaper.ImGui_MenuItem(ctx, "Centre horizontally", nil, ch) then
      toggleKey("focused_panel_center_h", false)
    end
    local cv = reaper.GetExtState(SECT, "focused_panel_center_v") == "1"
    if reaper.ImGui_MenuItem(ctx, "Centre vertically", nil, cv) then
      toggleKey("focused_panel_center_v", false)
    end
    reaper.ImGui_EndMenu(ctx)
  end

  if has_dock then
    reaper.ImGui_Separator(ctx)
    if reaper.ImGui_MenuItem(ctx, "Dock window", nil, cur_dock < 0) then
      -- Toggle: float (0) ↔ the preferred REAPER docker (negative id, remembered
      -- from the last time it was docked). Applied next frame.
      pending_dock = (cur_dock < 0) and 0 or dock_pref
    end
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
-- Flags split so docking can drop the frameless/topmost character: a docked
-- window needs a title-bar tab and must not be top-most. Floating keeps the
-- frameless place-anywhere overlay look.
local BASE_FLAGS = reaper.ImGui_WindowFlags_NoScrollbar()
  | reaper.ImGui_WindowFlags_NoScrollWithMouse()
  | reaper.ImGui_WindowFlags_NoCollapse()
local FLOAT_FLAGS = reaper.ImGui_WindowFlags_NoTitleBar()
  | (reaper.ImGui_WindowFlags_TopMost and reaper.ImGui_WindowFlags_TopMost() or 0)

local function loop()
  if reaper.GetExtState(SECT, RUNKEY) ~= "1" then
    setToggle(false)
    return  -- stop deferring → context GC'd
  end

  font_px = fontPx()

  -- Apply a menu-requested dock change (dock into / out of a REAPER docker).
  if has_dock and pending_dock ~= nil then
    reaper.ImGui_SetNextWindowDockID(ctx, pending_dock)
    cur_dock = pending_dock
    pending_dock = nil
  end

  -- Restore saved geometry on first frame only; afterwards the OS/ImGui owns the
  -- window position (this is exactly why it's freely draggable — we do NOT call
  -- SetNextWindowPos every frame). Re-attach the saved dock here too.
  if first_frame then
    if has_dock and restore_dock < 0 then
      -- Re-dock to the saved docker; do NOT also set pos/size (that would force
      -- it to float at the saved rect, overriding the dock).
      reaper.ImGui_SetNextWindowDockID(ctx, restore_dock)
    elseif restore_x then
      reaper.ImGui_SetNextWindowPos(ctx, restore_x, restore_y)
      reaper.ImGui_SetNextWindowSize(ctx, restore_w, restore_h)
    else
      reaper.ImGui_SetNextWindowSize(ctx, 320, oneLine() and 40 or 64)
    end
    first_frame = false
  end

  local docked = has_dock and cur_dock < 0
  local wflags = docked and BASE_FLAGS or (BASE_FLAGS | FLOAT_FLAGS)

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
  local visible, open = reaper.ImGui_Begin(ctx, 'Rea-Sixty Focused##fp', true, wflags)
  if visible then
    -- Optional centering: offset the cursor by (avail − content)/2 using LAST
    -- frame's measured content size (1-frame lag, invisible). Content is wrapped
    -- in a group so GetItemRectSize yields its bounding box. Frank 2026-06-27.
    local availW, availH = reaper.ImGui_GetContentRegionAvail(ctx)
    local sx, sy = reaper.ImGui_GetCursorPos(ctx)
    if reaper.GetExtState(SECT, "focused_panel_center_h") == "1"
       and g_contentW and g_contentW < availW then
      reaper.ImGui_SetCursorPosX(ctx, sx + (availW - g_contentW) * 0.5)
    end
    if reaper.GetExtState(SECT, "focused_panel_center_v") == "1"
       and g_contentH and g_contentH < availH then
      reaper.ImGui_SetCursorPosY(ctx, sy + (availH - g_contentH) * 0.5)
    end
    reaper.ImGui_BeginGroup(ctx)
    drawContent()
    reaper.ImGui_EndGroup(ctx)
    g_contentW, g_contentH = reaper.ImGui_GetItemRectSize(ctx)

    -- Right-click anywhere in the window → context menu (whole-window hit area).
    if reaper.ImGui_IsWindowHovered(ctx)
       and reaper.ImGui_IsMouseClicked(ctx, 1) then
      reaper.ImGui_OpenPopup(ctx, POPUP_ID)
    end
    drawContextMenu()

    -- Track + persist the live dock id so a docked panel re-attaches on restart.
    if has_dock then
      cur_dock = reaper.ImGui_GetWindowDockID(ctx)
      if cur_dock ~= last_save_dock then
        last_save_dock = cur_dock
        reaper.SetExtState(SECT, "focused_panel_imgui_dock", tostring(cur_dock), true)
        -- Remember the docker we were in so float→dock can return there.
        if cur_dock < 0 and cur_dock ~= dock_pref then
          dock_pref = cur_dock
          reaper.SetExtState(SECT, "focused_panel_imgui_dockpref", tostring(dock_pref), true)
        end
      end
    end

    -- Persist FLOATING geometry when it changes (throttled to actual deltas). Skip
    -- while docked — the docker owns the size then, and saving it would clobber the
    -- floating rect we want to return to on undock.
    if not (has_dock and cur_dock < 0) then
      local px, py = reaper.ImGui_GetWindowPos(ctx)
      local pw, ph = reaper.ImGui_GetWindowSize(ctx)
      px, py, pw, ph = math.floor(px), math.floor(py), math.floor(pw), math.floor(ph)
      if px ~= last_save_x or py ~= last_save_y or pw ~= last_save_w or ph ~= last_save_h then
        last_save_x, last_save_y, last_save_w, last_save_h = px, py, pw, ph
        saveRect(px, py, pw, ph)
      end
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
