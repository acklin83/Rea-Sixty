-- @description Rea-Sixty — Inserts active CS/BC overlay (companion)
-- @author Störsender
-- @version 0.5.0
-- @provides [main] .
-- @about
--   Non-destructive highlight of the **active CS / BC instance** in REAPER's
--   Mixer (MCP) Inserts list, PLUS an optional dockable readout panel showing
--   the surface-focused track and its active CS / BC instance names. Reads the
--   active instance per track from the Rea-Sixty extension (ExtState
--   "rea_sixty"/"overlay" + "overlay_focus") and draws a JS_Composite overlay
--   directly on the native fxlist windows — no FX rename, no dirty project.
--   Run once to start (background defer action); run again to stop. Requires
--   js_ReaScriptAPI + the Rea-Sixty extension with "Mark active CS/BC in
--   Inserts list" enabled. The dock panel is toggled in Settings.
--
--   Two surfaces, two geometries:
--     • MCP — each track strip's FX list is its OWN child window (mcp.fxlist).
--       Composite at the child's (0,0) origin; row = fxIdx * rowH from the top.
--     • TCP — the FX names (tcp.fx) live in ONE shared track-panel window for
--       all tracks. Composite via JS_Window_ScreenToClient at the row's screen
--       y, refined per track.
--
--   Compositing recipe (macOS-correct, mirrors FeedTheCat's Adaptive Grid):
--     • draw onto the fxlist child / panel window (not the top-level main window)
--     • colours MUST carry an 0xFF alpha byte (color | 0xFF000000), else LICE
--       writes transparent pixels and nothing shows
--     • macOS: clear the bitmap to the colour (color & 0x00FFFFFF), not to 0
--     • JS_LICE_FillRect mode 0; JS_Composite_Delay before JS_Composite

local _, _, sectionID, cmdID = reaper.get_action_context()

local RUNKEY = "overlay_running"
if reaper.GetExtState("rea_sixty", RUNKEY) == "1" then
  reaper.SetExtState("rea_sixty", RUNKEY, "0", false)
  return
end
reaper.SetExtState("rea_sixty", RUNKEY, "1", false)

local function setToggle(on)
  if sectionID and cmdID and sectionID >= 0 then
    reaper.SetToggleCommandState(sectionID, cmdID, on and 1 or 0)
    reaper.RefreshToolbar2(sectionID, cmdID)
  end
end

local NEED = { "GetThingFromPoint", "GetTrackFromPoint", "JS_Composite",
  "JS_Composite_Delay", "JS_Window_FromPoint", "JS_Window_GetClientSize",
  "JS_Window_GetRect", "JS_Window_ScreenToClient", "JS_LICE_CreateBitmap",
  "JS_LICE_FillRect", "JS_LICE_Clear", "JS_LICE_RoundRect" }
for _, fn in ipairs(NEED) do
  if not reaper.APIExists(fn) then
    reaper.MB("js_ReaScriptAPI fehlt: " .. fn, "Rea-Sixty Inserts Overlay", 0)
    reaper.SetExtState("rea_sixty", RUNKEY, "0", false)
    return
  end
end

local is_windows = reaper.GetOS():find("Win") ~= nil

------------------------------------------------------------------------
-- Tunables (live via ExtState — calibrate without editing the file).
------------------------------------------------------------------------
local SECT  = "rea_sixty"
local ALPHA = 0xFF000000

local function num(key, def)
  local v = tonumber(reaper.GetExtState(SECT, key))
  if v == nil then return def end
  return v
end

-- Design tunables, read live from ExtState (set in Settings → Inserts). Colours
-- are stored as 0xRRGGBB ints; drive BOTH the MCP highlight and the dock panel.
local function csRgb()  return math.floor(num("overlay_cs_col", 0x33C0FF)) & 0xFFFFFF end
local function bcRgb()  return math.floor(num("overlay_bc_col", 0xFFB000)) & 0xFFFFFF end
local function csCol()  return csRgb() | ALPHA end
local function bcCol()  return bcRgb() | ALPHA end
local function fillA()  return num("overlay_fill_a", 0.32) end
local function lineA()  return num("overlay_line_a", 0.90) end

local STEP = 16

------------------------------------------------------------------------
-- ExtState: active CS/BC per track GUID
------------------------------------------------------------------------
local function readActive()
  local raw = reaper.GetExtState(SECT, "overlay")
  if raw == "" then return false, 0, {} end
  local on  = (raw:match("^(%d);") == "1")
  local rev = tonumber(raw:match("^%d;(%d+);")) or 0
  local byGuid = {}
  for guid, cs, bc in raw:gmatch("({[%x%-]+}),(%-?%d+),(%-?%d+)") do
    byGuid[guid] = { cs = tonumber(cs), bc = tonumber(bc) }
  end
  return on, rev, byGuid
end

------------------------------------------------------------------------
-- TCP block refine: the tcp.fx (FX-names) region for one track, in screen
-- coords. GetThingFromPoint returns "tcp.fx" for the whole names column
-- (no per-row id), so we find the column rect and divide by row height.
------------------------------------------------------------------------
local function refineTcp(px, py, track)
  local function ok(x, y)
    local _, info = reaper.GetThingFromPoint(x, y)
    return info == "tcp.fxparm" and reaper.GetTrackFromPoint(x, y) == track
  end
  local t = py; while ok(px, t - 1) do t = t - 1 end
  local b = py; while ok(px, b + 1) do b = b + 1 end
  local mid = (t + b) // 2
  local l = px; while ok(l - 1, mid) do l = l - 1 end
  local r = px; while ok(r + 1, mid) do r = r + 1 end
  return l, t, r + 1, b + 1
end

-- The mcp.fxlist on macOS (this theme) is ONE window spanning ALL strips, not a
-- per-strip child — so compositing at the window's full client width painted the
-- highlight across every channel (Frank 2026-06-15, 5-track project). Walk left /
-- right from the hit point along the same row while still over THIS track's
-- mcp.fxlist to find the strip's own x-column [l, r) in screen coords; the draw
-- then confines the highlight to that column only.
local function refineMcpColumn(px, py, track)
  local function ok(x)
    local _, info = reaper.GetThingFromPoint(x, py)
    return info == "mcp.fxlist" and reaper.GetTrackFromPoint(x, py) == track
  end
  local l = px; while ok(l - 1) do l = l - 1 end
  local r = px; while ok(r + 1) do r = r + 1 end
  return l, r + 1
end

------------------------------------------------------------------------
-- Locate FX-list targets per track: MCP per-strip child windows and the
-- TCP names column (shared window). byGuid[guid] = list of blocks.
------------------------------------------------------------------------
-- `want` = set of active track GUIDs (the only ones the overlay ever draws).
-- We skip every track that's not in it and STOP the whole-window grid scan as
-- soon as all wanted tracks' fxlist windows are located — so locating the
-- overlay targets no longer walks the entire mixer for strips we never draw.
-- Frank 2026-06-14 (MCP scroll lag).
local function scanFxBlocks(want)
  local byGuid, seen = {}, {}
  local wantN = 0
  if want then for _ in pairs(want) do wantN = wantN + 1 end end
  local foundGuids, foundN = {}, 0
  -- TCP overlay is experimental (shared track-panel window + Retina coordinate
  -- mismatch causes glitches) — opt-in via ExtState overlay_tcp=1. MCP always on.
  local tcpOn = num("overlay_tcp", 0) ~= 0
  local main = reaper.GetMainHwnd()
  local _, ml, mt, mr, mb = reaper.JS_Window_GetRect(main)
  local x0, x1 = math.min(ml, mr), math.max(ml, mr)
  local y0, y1 = math.min(mt, mb), math.max(mt, mb)
  local done = false
  local y = y0
  while y <= y1 and not done do
    local x = x0
    while x <= x1 do
      local _, info = reaper.GetThingFromPoint(x, y)
      local kind
      if info == "mcp.fxlist" then kind = "mcp"
      elseif tcpOn and info == "tcp.fxparm" then kind = "tcp" end
      if kind then
        local tr = reaper.GetTrackFromPoint(x, y)
        local h  = reaper.JS_Window_FromPoint(x, y)
        if tr and h then
          local g = reaper.GetTrackGUID(tr)
          -- Only the active CS/BC tracks are ever drawn — ignore the rest.
          if g and g ~= "" and (not want or want[g]) then
            local key = g .. "|" .. tostring(h) .. "|" .. kind
            if not seen[key] then
              seen[key] = true
              byGuid[g] = byGuid[g] or {}
              if kind == "mcp" then
                local _, cw, ch = reaper.JS_Window_GetClientSize(h)
                if cw and ch and cw > 0 and ch > 0 then
                  -- Confine to THIS strip's x-column (the fxlist window spans all
                  -- strips on macOS — see refineMcpColumn). sl/sw in screen x;
                  -- converted to client x at draw time. sy = a screen y inside
                  -- the window for the ScreenToClient call.
                  local sl, sr = refineMcpColumn(x, y, tr)
                  local cnt = reaper.TrackFX_GetCount(tr)
                  byGuid[g][#byGuid[g] + 1] =
                    { kind = "mcp", hwnd = h, ch = ch, count = cnt,
                      sl = sl, sw = sr - sl, sy = y }
                end
              else
                local l, t, r = refineTcp(x, y, tr)
                byGuid[g][#byGuid[g] + 1] = { kind = "tcp", hwnd = h, sl = l, st = t, sw = r - l }
              end
              -- Track how many wanted tracks we've located so we can bail early.
              if not foundGuids[g] then foundGuids[g] = true; foundN = foundN + 1 end
            end
          end
        end
      end
      -- Early-out once every active target is found (MCP-only path; the
      -- experimental TCP refine needs the full sweep, so skip the shortcut).
      if wantN > 0 and foundN >= wantN and not tcpOn then done = true; break end
      x = x + STEP
    end
    y = y + STEP
  end
  return byGuid
end

------------------------------------------------------------------------
-- Compositing
------------------------------------------------------------------------
local g_drawn = {}

local function clearDrawn()
  for _, d in ipairs(g_drawn) do
    reaper.JS_Composite(d.hwnd, 0, 0, 0, 0, d.bmp, 0, 0, 0, 0)
    reaper.JS_Window_InvalidateRect(d.hwnd, d.x, d.y, d.x + d.w, d.y + d.h, false)
    reaper.JS_LICE_DestroyBitmap(d.bmp)
  end
  g_drawn = {}
end

-- Supersample factor for crisp compositing on Retina. dst stays in logical
-- points (w,h) but the bitmap is rendered SS× bigger and composited
-- src(w*SS,h*SS)→dst(w,h): on a 2× display that maps 1:1 to physical pixels
-- (crisp), on 1× it's harmlessly supersampled-then-downscaled. Live-tunable
-- via ExtState overlay_ss (default 2 on macOS, 1 on Windows). Frank 2026-06-15.
local function ssFactor()
  return math.max(1, math.floor(num("overlay_ss", is_windows and 1 or 2)))
end

local function composite(hwnd, dx, dy, w, h, col)
  if w <= 0 or h <= 0 then return end
  local ss = ssFactor()
  local bw, bh = w * ss, h * ss
  local bmp = reaper.JS_LICE_CreateBitmap(true, bw, bh)
  if is_windows then reaper.JS_LICE_Clear(bmp, 0x00000000)
  else                reaper.JS_LICE_Clear(bmp, col & 0x00FFFFFF) end
  reaper.JS_LICE_FillRect(bmp, 0, 0, bw, bh, col, fillA(), 0)
  -- Border ~1 logical px thick = SS nested source-pixel outlines.
  for i = 0, ss - 1 do
    reaper.JS_LICE_RoundRect(bmp, i, i, bw - 1 - 2 * i, bh - 1 - 2 * i, 0, col, lineA(), 0, true)
  end
  reaper.JS_Composite_Delay(hwnd, 0.03, 0.05, 8)
  reaper.JS_Composite(hwnd, dx, dy, w, h, bmp, 0, 0, bw, bh)
  reaper.JS_Window_InvalidateRect(hwnd, dx, dy, dx + w, dy + h, false)
  g_drawn[#g_drawn + 1] = { hwnd = hwnd, bmp = bmp, x = dx, y = dy, w = w, h = h }
end

local function drawBlockRow(block, fxIdx, col)
  if block.kind == "mcp" then
    -- Row height is a THEME/UI-scale font constant (mcp.fxlist.font = 16/24/32 px
    -- @ scale 1/1.5/2), NOT derivable from the box (the inserts area is a fixed
    -- height that does NOT grow to fit — ch/count fails on short chains). So a
    -- tuned constant + a Settings slider. topPad nudges the first row.
    local rowH   = num("overlay_rowh", 17)
    local topPad = num("overlay_toppad", 1)
    local y = topPad + fxIdx * rowH
    if y < -1 or y + rowH > block.ch + 1 then return end
    -- Confine to this strip's column: screen-left → client x (x isn't flipped on
    -- macOS; only y is, handled in the tcp path). Width = the strip's own width.
    local cx = reaper.JS_Window_ScreenToClient(block.hwnd, block.sl, block.sy)
    composite(block.hwnd, math.floor(cx + 0.5), math.floor(y + 0.5),
              math.floor(block.sw + 0.5), math.floor(rowH + 0.5), col)
  else  -- tcp: shared track-panel window
    local rowH   = num("overlay_rowh_tcp", 14)
    local topPad = num("overlay_toppad_tcp", 0)
    local screenY = block.st + topPad + fxIdx * rowH
    local cx, cy = reaper.JS_Window_ScreenToClient(block.hwnd, block.sl, screenY)
    -- macOS: ScreenToClient returns y-up (from the client bottom), but
    -- JS_Composite dst is y-down (from the top) — flip it. (Windows: no flip.)
    local yd = cy
    if not is_windows then
      local _, _, ch = reaper.JS_Window_GetClientSize(block.hwnd)
      yd = (ch or 0) - cy
    end
    composite(block.hwnd, cx, yd, block.sw, rowH, col)
  end
end

local function rebuildDraw(byGuid, blocks)
  clearDrawn()
  for guid, a in pairs(byGuid) do
    local list = blocks[guid]
    if list then
      for _, block in ipairs(list) do
        if a.cs and a.cs >= 0 then drawBlockRow(block, a.cs, csCol()) end
        if a.bc and a.bc >= 0 then drawBlockRow(block, a.bc, bcCol()) end
      end
    end
  end
end

------------------------------------------------------------------------
-- Optional dockable readout panel (WT-style standalone gfx window).
-- Shows the surface-focused track + its active CS / BC instance NAMES —
-- no track-Y alignment, dockable into any toolbar/docker. Independent of
-- the JS_Composite list highlight. Toggled from Settings via ExtState
-- "overlay_panel"; focused track GUID published by the extension as
-- "overlay_focus". Active CS/BC chain indices come from the same "overlay"
-- payload the list highlight reads (byGuid[guid] = {cs, bc}).
------------------------------------------------------------------------
local panelOpen   = false
local panelDock   = nil
local panelFs     = nil   -- last applied font size (detect change -> resize float)
local panelRcPrev = 0     -- previous right-button state (context-menu edge detect)

local function panelWanted() return reaper.GetExtState(SECT, "overlay_panel") == "1" end

local function panelOneLine()
  return reaper.GetExtState(SECT, "overlay_panel_oneline") == "1"
end

-- Font size is user-set (right-click menu / Settings, ExtState
-- "overlay_panel_font"). Everything else (padding, line height, window size)
-- scales off it. Layout is one or two rows ("overlay_panel_oneline").
local function panelMetrics()
  local fs = math.floor(num("overlay_panel_font", 18))
  if fs < 8 then fs = 8 end
  local oneLine = panelOneLine()
  local pad  = math.max(6, math.floor(fs * 0.5))
  local lh   = fs + math.floor(fs * 0.5)
  local rows = oneLine and 1 or 2
  -- One line packs CS + BC side by side → needs more width.
  local w    = math.max(300, fs * (oneLine and 30 or 18))
  local h    = pad * 2 + lh * rows + 6
  return fs, pad, lh, w, h, oneLine
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

-- Smart track-name abbreviation — a Lua port of the extension's
-- abbreviateTrackName_ (TrackName.cpp), so the panel reads like the UF8 / UC1
-- scribble. Passes: 1) strip separators; 2) per-token drop later vowels
-- (all-caps 2-4-char acronyms like DI/EQ/FX survive); 3) collapse repeated
-- consonants; 4) hard-truncate the result. `maxLen` is user-set (different
-- lengths for different tastes). Returns src unchanged if it already fits.
local function isVowel(c)
  c = c:lower()
  return c == "a" or c == "e" or c == "i" or c == "o" or c == "u"
end

local function smartAbbrev(src, maxLen)
  if not src or maxLen <= 0 or #src <= maxLen then return src end
  local tokens = {}
  for tok in src:gmatch("[^%s%-_/]+") do tokens[#tokens + 1] = tok end
  if #tokens == 0 then return src:sub(1, maxLen) end
  -- Pass 1: just strip separators.
  local joined = table.concat(tokens)
  if #joined <= maxLen then return joined end
  -- Pass 2: per-token vowel drop (keep first char + consonants).
  local abbr = {}
  for _, t in ipairs(tokens) do
    if #t >= 2 and #t <= 4 and t:match("^%u+$") then
      abbr[#abbr + 1] = t                      -- acronym token survives
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
  -- Pass 3: collapse repeated consonants (letters only).
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
  -- Pass 4: hard truncate.
  return joined:sub(1, maxLen)
end

-- Format a track name for the panel per the user's options:
--   overlay_panel_trackname  "0" hide / else show (default show)
--   overlay_panel_track_abbr "1" smart / else full  (default full)
--   overlay_panel_track_len  smart-abbrev length     (default 8)
local function panelTrackDisplay(name)
  if not name then return nil end
  if reaper.GetExtState(SECT, "overlay_panel_trackname") == "0" then return nil end
  if reaper.GetExtState(SECT, "overlay_panel_track_abbr") == "1" then
    return smartAbbrev(name, math.max(1, math.floor(num("overlay_panel_track_len", 8))))
  end
  return name
end

-- "VST3: SSL Native Channel Strip 2 (Solid State Logic)" -> "SSL Native Channel Strip 2"
local function fxLabel(tr, fxIdx)
  if not tr or not fxIdx or fxIdx < 0 then return nil end
  local _, nm = reaper.TrackFX_GetFXName(tr, fxIdx, "")
  if not nm or nm == "" then return nil end
  nm = nm:gsub("^%u%u+%d*i?:%s*", "")    -- strip "VST3: " / "AU: " / "JS: " / "CLAP: "
  nm = nm:gsub("%s*%([^()]-%)%s*$", "")   -- strip trailing " (vendor)"
  return nm
end

local function gset(rgb, a)
  gfx.set(((rgb >> 16) & 0xFF) / 255, ((rgb >> 8) & 0xFF) / 255, (rgb & 0xFF) / 255, a or 1)
end

-- The surface BC lives on whatever track it's anchored to — find the (single)
-- published entry carrying a bc index, regardless of the focused track.
local function findActiveBc(byGuid)
  if not byGuid then return nil end
  for guid, a in pairs(byGuid) do
    if a.bc and a.bc >= 0 then return guid, a.bc end
  end
  return nil
end

-- "name<tab>value" ExtState -> name, value (nil if unset).
local function paramStr(key)
  local raw = reaper.GetExtState(SECT, key)
  if not raw or raw == "" then return nil, nil end
  local n, v = raw:match("^(.-)\t(.*)$")
  if not n then n = raw end
  return n, v
end

local function drawPanel(byGuid)
  local fs, pad, lh, _, _, oneLine = panelMetrics()
  gfx.set(0.11, 0.11, 0.12, 1); gfx.rect(0, 0, gfx.w, gfx.h, 1)
  gfx.setfont(1, "Arial", fs)

  -- CS = focused track's active instance. BC = the SURFACE BC, on whatever
  -- track it's anchored to (not necessarily the focused one). Each row shows
  -- its OWN track name + the last param changed on THAT plugin.
  local fGuid = reaper.GetExtState(SECT, "overlay_focus")
  local ftr, fMaster = findTrackByGuid(fGuid)
  local fa = byGuid and byGuid[fGuid] or nil
  local csTrk  = ftr and panelTrackDisplay(trackLabel(ftr, fMaster)) or nil
  local csName = (ftr and fa) and fxLabel(ftr, fa.cs) or nil

  local bcGuid, bcIdx = findActiveBc(byGuid)
  local btr, bMaster = nil, false
  if bcGuid then btr, bMaster = findTrackByGuid(bcGuid) end
  local bcTrk  = btr and panelTrackDisplay(trackLabel(btr, bMaster)) or nil
  local bcName = btr and fxLabel(btr, bcIdx) or nil

  local csPN, csPV = paramStr("overlay_param_cs")
  local bcPN, bcPV = paramStr("overlay_param_bc")

  -- One segment, drawn at the current gfx.x/gfx.y (drawstr advances x):
  -- "[<track>  ]<TAG> <fx>   <param> <val>" — track dim, tag in domain colour,
  -- fx bright, param blue + value dim. Track name only in two-line mode (one
  -- line is tight; CS + BC share the row there).
  local function seg(tag, col, name, pn, pv, trk)
    local lit = name ~= nil
    if trk ~= nil then
      gfx.set(0.62, 0.62, 0.70, lit and 1 or 0.35)
      gfx.drawstr((trk or "") .. "  ")
    end
    gset(col, lit and 1 or 0.35); gfx.drawstr(tag .. " ")
    gfx.set(0.93, 0.93, 0.96, lit and 1 or 0.35)
    gfx.drawstr(name or "\xE2\x80\x94")
    if pn and pn ~= "" then
      gfx.set(0.66, 0.78, 0.96, 1); gfx.drawstr("   " .. pn)
      if pv and pv ~= "" then
        gfx.set(0.6, 0.6, 0.66, 1); gfx.drawstr(" " .. pv)
      end
    end
  end

  if oneLine then
    gfx.x, gfx.y = pad, pad
    seg("CS", csRgb(), csName, csPN, csPV, csTrk)
    gfx.drawstr("     ")
    seg("BC", bcRgb(), bcName, bcPN, bcPV, bcTrk)
  else
    gfx.x, gfx.y = pad, pad;      seg("CS", csRgb(), csName, csPN, csPV, csTrk)
    gfx.x, gfx.y = pad, pad + lh; seg("BC", bcRgb(), bcName, bcPN, bcPV, bcTrk)
  end
end

local function panelClose(persistOff)
  if panelOpen then
    panelDock = gfx.dock(-1)
    reaper.SetExtState(SECT, "overlay_panel_dock", tostring(panelDock or 1), true)
    gfx.quit()
    panelOpen = false
  end
  if persistOff then reaper.SetExtState(SECT, "overlay_panel", "0", true) end
end

-- Declarative menu builder (compact port of FeedTheCat's Gridbox menu system).
-- A menu is an array of entries; an entry is one of:
--   { title=..., OnReturn=fn, checked=bool, grayed=bool }   -- leaf
--   { separator = true }                                    -- divider
--   { title=..., <nested entries...> }                      -- submenu
-- buildMenu → gfx.showmenu string; runMenu dispatches the clicked entry's
-- OnReturn (separators/submenu headers don't count as selectable indices, so
-- the index walk matches gfx.showmenu's numbering).
local function buildMenu(menu)
  local str = menu.title and (">" .. menu.title .. "|") or ""
  for _, e in ipairs(menu) do
    if e.separator then
      str = str .. "|"
    elseif #e > 0 then
      str = str .. buildMenu(e) .. "|"
    elseif e.title then
      if e.checked then str = str .. "!" end
      if e.grayed  then str = str .. "#" end
      str = str .. e.title .. "|"
    end
  end
  if menu.title then str = str .. "<" end
  return str
end

local function runMenu(menu, idx, i)
  i = i or 1
  for _, e in ipairs(menu) do
    if #e > 0 then
      i = runMenu(e, idx, i)
      if i < 0 then return i end
    elseif e.title and not e.separator then
      if i == idx then
        if e.OnReturn then e.OnReturn() end
        return -1
      end
      i = i + 1
    end
  end
  return i
end

local function panelSetFontSize()
  local cur = math.floor(num("overlay_panel_font", 18))
  local ret, input = reaper.GetUserInputs("Panel font", 1,
    "Font size: (e.g. 18),extrawidth=40", tostring(cur))
  if not ret then return end
  local fs = tonumber(input)
  if fs then
    reaper.SetExtState(SECT, "overlay_panel_font",
      tostring(math.max(8, math.floor(fs))), true)
  end
end

local function panelSetTrackLen()
  local cur = math.floor(num("overlay_panel_track_len", 8))
  local ret, input = reaper.GetUserInputs("Track name length", 1,
    "Smart-abbrev length: (e.g. 8),extrawidth=40", tostring(cur))
  if not ret then return end
  local v = tonumber(input)
  if v then
    reaper.SetExtState(SECT, "overlay_panel_track_len",
      tostring(math.max(1, math.floor(v))), true)
  end
end

-- Native colour picker → store as a 0xRRGGBB int string (same key the Settings
-- ColorEdit + the csRgb/bcRgb readers use). ColorFromNative handles the
-- platform byte order.
local function panelChooseColor(key)
  local ret, color = reaper.GR_SelectColor(reaper.GetMainHwnd())
  if ret ~= 0 then
    local r, g, b = reaper.ColorFromNative(color)
    reaper.SetExtState(SECT, key, tostring(r * 65536 + g * 256 + b), true)
  end
end

-- Right-click context menu: gfx windows don't reliably expose a native "Dock
-- window" entry on macOS, so we provide our own + layout / font / colour. Dock
-- state is persisted (overlay_panel_dock) so the panel reopens where it was.
local function panelContextMenu()
  local docked  = (gfx.dock(-1) & 1) == 1
  local oneLine = panelOneLine()
  local menu = {
    { title = "Layout",
      { title = "Two lines (CS / BC)", checked = not oneLine,
        OnReturn = function()
          reaper.SetExtState(SECT, "overlay_panel_oneline", "0", true)
        end },
      { title = "One line", checked = oneLine,
        OnReturn = function()
          reaper.SetExtState(SECT, "overlay_panel_oneline", "1", true)
        end },
    },
    { title = "Track name",
      { title = "Show track name",
        checked = (reaper.GetExtState(SECT, "overlay_panel_trackname") ~= "0"),
        OnReturn = function()
          local on = reaper.GetExtState(SECT, "overlay_panel_trackname") ~= "0"
          reaper.SetExtState(SECT, "overlay_panel_trackname", on and "0" or "1", true)
        end },
      { separator = true },
      { title = "Full name",
        checked = (reaper.GetExtState(SECT, "overlay_panel_track_abbr") ~= "1"),
        OnReturn = function() reaper.SetExtState(SECT, "overlay_panel_track_abbr", "0", true) end },
      { title = "Smart abbreviate",
        checked = (reaper.GetExtState(SECT, "overlay_panel_track_abbr") == "1"),
        OnReturn = function() reaper.SetExtState(SECT, "overlay_panel_track_abbr", "1", true) end },
      { title = "Abbreviation length\xE2\x80\xA6", OnReturn = panelSetTrackLen },
    },
    { title = "Font size\xE2\x80\xA6", OnReturn = panelSetFontSize },
    { title = "CS colour\xE2\x80\xA6", OnReturn = function() panelChooseColor("overlay_cs_col") end },
    { title = "BC colour\xE2\x80\xA6", OnReturn = function() panelChooseColor("overlay_bc_col") end },
    { separator = true },
    { title = "Dock in Docker", checked = docked,
      OnReturn = function()
        if docked then gfx.dock(0) else gfx.dock(1) end
        panelDock = gfx.dock(-1)
        reaper.SetExtState(SECT, "overlay_panel_dock", tostring(panelDock or 0), true)
      end },
    { separator = true },
    { title = "Close panel", OnReturn = function() panelClose(true) end },
  }
  gfx.x, gfx.y = gfx.mouse_x, gfx.mouse_y
  local sel = gfx.showmenu(buildMenu(menu))
  if sel and sel > 0 then runMenu(menu, sel) end
end

local function panelTick(byGuid)
  local want = panelWanted()
  if want and not panelOpen then
    local dock = math.floor(num("overlay_panel_dock", 0))
    local fs, _, _, w, h, oneLine = panelMetrics()
    gfx.ext_retina = 1
    gfx.init("Rea-Sixty Inserts", w, h, dock, 200, 200)
    panelOpen = true; panelFs = fs .. "|" .. (oneLine and "1" or "0")
  elseif not want and panelOpen then
    panelClose(false)
    return
  end
  if not panelOpen then return end
  if gfx.getchar() < 0 then return panelClose(true) end   -- user closed window
  -- Right-click anywhere in the panel → dock / undock / close menu.
  local rc = gfx.mouse_cap & 2
  if rc == 2 and panelRcPrev ~= 2 then panelContextMenu() end
  panelRcPrev = rc
  if not panelOpen then return end   -- menu may have closed it
  -- Live-resize a FLOATING window when the font size OR layout changed (a
  -- docked panel is sized by its docker — there it just fills the space).
  local fs, _, _, w, h, oneLine = panelMetrics()
  local sig = fs .. "|" .. (oneLine and "1" or "0")
  if sig ~= panelFs and gfx.dock(-1) == 0 then
    gfx.init("Rea-Sixty Inserts", w, h, 0, 200, 200)
    panelFs = sig
  end
  drawPanel(byGuid)
  gfx.update()
end

------------------------------------------------------------------------
-- Main defer loop
------------------------------------------------------------------------
local g_lastSig, g_blocks, g_lastRev = nil, {}, -1
local g_lastScroll, g_lastCount = nil, -1   -- cheap rescan triggers
local g_emptyTick = 0                       -- slow retry while no target found
local g_scrollPending = false               -- rescan owed once scrolling settles
local g_stableTicks = 0                     -- ticks since the mixer scroll last moved

local function blockSig(b)
  if b.kind == "mcp" then return string.format("m%s,%d,%d,%d,%d", tostring(b.hwnd), b.sl, b.sw, b.ch, b.count or 0)
  else return string.format("t%s,%d,%d,%d", tostring(b.hwnd), b.sl, b.st, b.sw) end
end

local function drawSig(byGuid, blocks)
  local parts = {}
  for guid, a in pairs(byGuid) do
    local list = blocks[guid]
    local bs = "x"
    if list then
      local t = {}
      for _, b in ipairs(list) do t[#t + 1] = blockSig(b) end
      table.sort(t); bs = table.concat(t, ";")
    end
    parts[#parts + 1] = string.format("%s:%s:%s:%s", guid, tostring(a.cs), tostring(a.bc), bs)
  end
  table.sort(parts)
  return table.concat(parts, "|") .. "|" .. num("overlay_rowh", 17) .. "," .. num("overlay_toppad", 1)
    .. "," .. num("overlay_rowh_tcp", 14) .. "," .. num("overlay_toppad_tcp", 0)
    .. "|" .. csRgb() .. "," .. bcRgb() .. "," .. fillA() .. "," .. lineA()
    .. "," .. num("overlay_ss", is_windows and 1 or 2)
end

-- Motion fingerprint for "is the mixer mid-scroll". GetMixerScroll alone is
-- quantised to whole tracks, so it stays constant for many ticks during a
-- smooth/pixel scroll → the settle counter fired while the user was still
-- scrolling. I_MCPX is the strip's live pixel-x in the mixer and moves
-- continuously, so folding it in makes the fingerprint change every tick the
-- strips actually move. Cheap (a getter per active track). Frank 2026-06-15.
local function scrollFp(byGuid)
  local parts = { tostring(reaper.GetMixerScroll()) }
  for guid in pairs(byGuid) do
    local tr = findTrackByGuid(guid)
    if tr then
      parts[#parts + 1] =
        string.format("%d", math.floor((reaper.GetMediaTrackInfo_Value(tr, "I_MCPX") or 0) + 0.5))
    end
  end
  table.sort(parts)
  return table.concat(parts, ",")
end

local shutdown

local function loop()
  if reaper.GetExtState("rea_sixty", RUNKEY) ~= "1" then return shutdown() end
  local on, rev, byGuid = readActive()
  if not on or next(byGuid) == nil then
    if g_lastSig ~= "off" then clearDrawn(); g_lastSig = "off" end
  else
    -- Event-driven rescan only — the full-window GetThingFromPoint scan is
    -- expensive (main thread), so NEVER run it on a timer. Re-acquire the
    -- fxlist windows only when the active set changed (rev), the mixer
    -- scrolled/banked (GetMixerScroll), or tracks were added/removed. Steady
    -- typing triggers none of these, so the overlay is idle and cheap.
    local scroll = scrollFp(byGuid)
    local ntrk   = reaper.CountTracks(0)
    if scroll ~= g_lastScroll then
      -- Mixer is scrolling/banking: the active strips' positions are in flux.
      -- Running the window scan AND keeping a live composite on a continuously
      -- repainting mixer is what makes scrolling feel sluggish, so HIDE the
      -- overlay and owe a rescan for once scrolling has settled. Reset the
      -- stable-tick counter each time the scroll moves. Frank 2026-06-14/15.
      g_lastScroll = scroll
      g_scrollPending = true
      g_stableTicks = 0
      if g_lastSig ~= "scrolling" then clearDrawn(); g_lastSig = "scrolling" end
    else
      g_stableTicks = g_stableTicks + 1
      -- Settle delay before re-acquiring after a scroll: stay hidden until the
      -- scroll has been still for `overlay_settle` defer ticks (~30 Hz; default
      -- 8 ≈ 0.25 s, set 30 for ~1 s). Keeps the scan off the mixer while the
      -- user is still flinging it.
      local settle = math.max(1, math.floor(num("overlay_settle", 8)))
      local need
      if g_scrollPending then
        need = (g_stableTicks >= settle)      -- wait out the settle, stay hidden
      else
        need = rev ~= g_lastRev or ntrk ~= g_lastCount
        -- No target yet (mixer hidden / strip scrolled off) → slow ~1 Hz retry,
        -- never per-tick, so a closed mixer can't re-introduce lag.
        if next(g_blocks) == nil then
          g_emptyTick = g_emptyTick + 1
          if g_emptyTick >= 30 then need = true; g_emptyTick = 0 end
        else
          g_emptyTick = 0
        end
      end
      if need then
        -- Only the active CS/BC tracks are ever drawn — scan for just those and
        -- bail as soon as they're located (see scanFxBlocks).
        local want = {}
        for guid in pairs(byGuid) do want[guid] = true end
        g_blocks = scanFxBlocks(want)
        g_lastRev, g_lastCount = rev, ntrk
        g_scrollPending = false
      end
      -- Stay hidden while a post-scroll rescan is still owed (settling).
      if not g_scrollPending then
        local sig = drawSig(byGuid, g_blocks)
        if sig ~= g_lastSig then rebuildDraw(byGuid, g_blocks); g_lastSig = sig end
      end
    end
  end
  panelTick(byGuid)   -- dockable readout panel (independent of the list highlight)
  reaper.defer(loop)
end

shutdown = function()
  clearDrawn()
  panelClose(false)
  reaper.SetExtState("rea_sixty", RUNKEY, "0", false)
  setToggle(false)
end

reaper.atexit(shutdown)
setToggle(true)
loop()
