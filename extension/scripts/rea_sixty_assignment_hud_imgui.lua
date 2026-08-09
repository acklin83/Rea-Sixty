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

-- Text-size scale for the LIST + parameter-panel body (XS … XL, Medium = 1.0 =
-- the default). The chrome (tab titles, modifier badge, top buttons, TAB_H)
-- stays a FIXED size on purpose — scaling it made the titles balloon. The face
-- view scales to the window and ignores this entirely.
local function fontScale()
  local v = num("hud_imgui_font", 1.0)
  if v < 0.7 then v = 0.7 elseif v > 1.5 then v = 1.5 end
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
        -- Belt-and-suspenders: the section is the group key AND the column header,
        -- so it must never contain a ';'. If the cap/legend split ever fails
        -- (newer geometry format meeting an older line, or a non-numeric cap), the
        -- raw "section;cap;legend" triple would otherwise render as a wacky header
        -- row. Keep only the part before the first ';'.
        sec = sec:match("^[^;]*") or sec
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
local learnLayer   = 0     -- modifier overlay the armed learn will bind to
local ctxCtrlIdx   = -1   -- control under a right-click → control context menu
local ctxCtrlLayer = 0    -- active modifier layer captured AT right-click (the
                          -- modifier is still held then; renaming opens a native
                          -- text dialog that releases it, so we must latch here
                          -- rather than let the extension read the live layer).
-- UF8 device tab (Phase 2): interactive strip-grid.
local uf8Rects     = {}   -- per-cell hit-rects {kind, strip, x, y, w, h}
local uf8BankRects = {}   -- V-Pot bank selector hit-rects {b, x, y, w, h}
local uf8ColRects  = {}   -- per-strip colour-bar hit-rects {s, x, y, w, h} (mockup)
local uf8Learn     = -1   -- armed cell, encoded kind*8+strip (hud_uf8_learn), -1 none
local ctxUf8Kind   = -1   -- cell under a right-click → UF8 cell context menu
local ctxUf8Strip  = -1
local ctxUf8Bank   = -1   -- V-Pot bank under a right-click → bank rename menu
local ctxUf8ColStrip = -1 -- strip whose colour bar was clicked → palette popup
local nameRect     = nil  -- LCD / UF8-header click target → edit plug-in Kurzname
local csFavRect    = nil  -- CS-Favourite dropdown box (CS tab) hit-rect
local bcFavRect    = nil  -- BC-Favourite dropdown box (BC tab) hit-rect
local FAV_H        = 0     -- height reserved for the CS-Favourite row (0 on UF8)
local uf8Banks     = {}   -- [0..7] = V-Pot bank label ("" = none)
local uf8BankCols  = {}   -- [0..7] = V-Pot bank colour (0xRRGGBB or nil)
local uf8StripCols = {}   -- [0..7] = display colour-bar colour for the active bank
-- The 10 hardware-renderable SSL DAW-Colour swatches (must match
-- selPaletteRgb in Protocol.cpp): red, orange, yellow, green, cyan, blue,
-- purple, magenta, pink, white.
local UF8_BANK_PALETTE = {
  0xFF0000, 0xFF8000, 0xFFFF00, 0x00FF00, 0x00FFFF,
  0x0000FF, 0x8000FF, 0xFF00FF, 0xFF0080, 0xFFFFFF,
}
local lastUf8Tab   = nil  -- edge-write "hud_uf8_tab" so C++ auto-engages Plugin Mode
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
local escDownPrev      = false   -- global-Escape rising-edge tracker (js API)

local function sendCmd(s) reaper.SetExtState(SECT, "hud_cmd", s, false) end

local function clamp(v, lo, hi) return math.max(lo, math.min(v, hi)) end

-- Resolve the active domain's plug-in target from the extension's "hud_target"
-- publish ("csTrNum;csFx;bcTrNum;bcFx"; trNum 0 = master, 1-based track, -1 none).
-- On the UF8 tab the target comes from "hud_uf8_target" ("trNum;fx") instead — the
-- UF8-mapped FX, or the virgin cursor FX so its params can be picked + bound.
local function resolveTarget()
  local trN, fx
  if activeTab == "uf8" then
    local raw = reaper.GetExtState(SECT, "hud_uf8_target")
    local uN, uFx = raw:match("^(%-?%d+);(%-?%d+)$")
    if not uN then return nil end
    trN, fx = tonumber(uN), tonumber(uFx)
  else
    local raw = reaper.GetExtState(SECT, "hud_target")
    local csN, csFx, bcN, bcFx = raw:match("^(%-?%d+);(%-?%d+);(%-?%d+);(%-?%d+)$")
    if not csN then return nil end
    if activeTab == "cs" then trN, fx = tonumber(csN), tonumber(csFx)
    else                      trN, fx = tonumber(bcN), tonumber(bcFx) end
  end
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

-- Per-knob tuning detail (full-parity menu). One line per mapped UC1 knob on
-- the active layer: "idx;pol;rmin;rmax;sens;dn;stepped;nsteps;t0:v0,t1:v1,…".
-- Presence of a row means the control is an editable knob; absence => button /
-- unmapped / factory map (the menu falls back to the basic Learn/Rename/Unbind).
local function readDetail()
  local raw = reaper.GetExtState(SECT, "hud_detail")
  local map = {}
  for line in raw:gmatch("[^\n]+") do
    local idx, pol, rmin, rmax, sens, dn, st, ns, curve =
      line:match("^(%d+);(%d+);([%-%d%.]+);([%-%d%.]+);([%-%d%.]+);([%-%d%.]+);(%d+);(%d+);(.*)$")
    if idx then
      local pts = {}
      for t, v in curve:gmatch("([%-%d%.]+):([%-%d%.]+)") do
        pts[#pts + 1] = { tonumber(t), tonumber(v) }
      end
      map[tonumber(idx)] = {
        pol = tonumber(pol) or 0,
        rmin = tonumber(rmin) or 0, rmax = tonumber(rmax) or 1,
        sens = tonumber(sens) or 1, dn = tonumber(dn) or 0.5,
        stepped = (st == "1"), nsteps = tonumber(ns) or 0,
        curve = pts,
      }
    end
  end
  return map
end

-- Push-cycle button index set (which controls offer the "Push cycle" submenu).
local pushBtnSet  = {}
local pushBtnsRaw = nil
local function refreshPushBtns()
  local raw = reaper.GetExtState(SECT, "hud_pushbtns")
  if raw == pushBtnsRaw then return end
  pushBtnsRaw = raw
  pushBtnSet = {}
  for n in raw:gmatch("(%d+)") do pushBtnSet[tonumber(n)] = true end
end

-- Push-cycle editor data for the requested button (request/response via
-- "hud_push_req" → "hud_push"). Lines: "<idx>", then "S;param;norm;en;foreign;
-- label" per step, then "P;param;name;norm0~label0|norm1~label1|…" per discrete
-- param (the "+ Add step" catalog).
local function readPush()
  local raw = reaper.GetExtState(SECT, "hud_push")
  if raw == "" then return nil end
  local first = true
  local pd = { idx = -1, steps = {}, params = {} }
  for ln in raw:gmatch("[^\n]+") do
    if first then pd.idx = tonumber(ln) or -1; first = false
    elseif ln:sub(1, 2) == "S;" then
      local param, norm, en, foreign, label =
        ln:match("^S;(%-?%d+);([%-%d%.]+);(%d);(%d);(.*)$")
      if param then
        pd.steps[#pd.steps + 1] = {
          param = tonumber(param), norm = tonumber(norm),
          en = (en == "1"), foreign = (foreign == "1"), label = label or "" }
      end
    elseif ln:sub(1, 2) == "P;" then
      local param, name, opts = ln:match("^P;(%-?%d+);([^;]*);(.*)$")
      if param then
        local olist = {}
        for o in (opts .. "|"):gmatch("([^|]*)|") do
          local n, l = o:match("^([%-%d%.]+)~(.*)$")
          if n then olist[#olist + 1] = { norm = tonumber(n), label = l or "" } end
        end
        pd.params[#pd.params + 1] =
          { param = tonumber(param), name = name or "", opts = olist }
      end
    end
  end
  if pd.idx < 0 then return nil end
  return pd
end

-- UF1 plugin-mode map (v12). Payload: "P;<pages>;<curPage>" then one
--   "<pos>;<sk>;<param>;<inv>;<ledRgb>;<label>"
-- per mapped position (label LAST so a ';' inside a param name can't shift the
-- numeric fields). ledRgb = 0xRRGGBB as a decimal number, 0 = no LED override.
-- Empty channel = the shown plug-in has no UF1 layer.
local function readUf1()
  local raw = reaper.GetExtState(SECT, "hud_uf1_assign")
  if raw == "" then return nil end
  local u = { pages = 1, page = 0, mapped = true, name = "",
              cells = { [0] = {}, [1] = {} } }
  for ln in raw:gmatch("[^\n]+") do
    if ln:sub(1, 2) == "P;" then
      -- "P;<pages>;<page>;<hasMap>;<plug-in name>" — the name rides last
      -- because it may contain ';'. The two-field form is the pre-v14 header.
      local pg, cur, mapped, nm = ln:match("^P;(%d+);(%d+);(%d);(.*)$")
      if not pg then pg, cur = ln:match("^P;(%d+);(%d+)$") end
      u.pages  = tonumber(pg) or 1
      u.page   = tonumber(cur) or 0
      u.mapped = (mapped ~= "0")
      u.name   = nm or ""
    else
      -- "<pos>;<sk>;<param>;<inv>;<ledRgb>;<inherited>;<label>". `inherited`
      -- marks a position the UF1 drives from the plug-in's own map rather than
      -- from an explicit UF1 binding. Pre-v14 rows have no such field.
      local pos, sk, param, inv, rgb, inh, label =
        ln:match("^(%d+);(%d);(%-?%d+);(%d);(%d+);(%d);(.*)$")
      if not pos then
        pos, sk, param, inv, rgb, label =
          ln:match("^(%d+);(%d);(%-?%d+);(%d);(%d+);(.*)$")
        inh = "0"
      end
      if pos then
        u.cells[tonumber(sk)][tonumber(pos)] =
          { param = tonumber(param), inv = (inv == "1"),
            ledRgb = tonumber(rgb) or 0, inherited = (inh == "1"),
            label = label or "" }
      end
    end
  end
  return u
end

-- Per-V-Pot tuning detail for the UF1 tab's full-parity menu. One line per
-- mapped UF1 V-Pot:
--   pos;sk;pol;rmin;rmax;sens;dn;stepped;nsteps;t0:v0,…
-- V-Pots only (sk is always 0 — a soft-key is a press, so travel/curve have
-- nothing to act on), keyed by flat position.
local function readUf1Detail()
  local raw = reaper.GetExtState(SECT, "hud_uf1_detail")
  local map = {}
  for line in raw:gmatch("[^\n]+") do
    local pos, sk, pol, rmin, rmax, sens, dn, st, ns, curve =
      line:match("^(%d+);(%d+);(%d+);([%-%d%.]+);([%-%d%.]+);([%-%d%.]+);([%-%d%.]+);(%d+);(%d+);(.*)$")
    if pos then
      local pts = {}
      for t, v in curve:gmatch("([%-%d%.]+):([%-%d%.]+)") do
        pts[#pts + 1] = { tonumber(t), tonumber(v) }
      end
      map[tonumber(pos)] = {
        sk = tonumber(sk) or 0,
        pol = tonumber(pol) or 0,
        rmin = tonumber(rmin) or 0, rmax = tonumber(rmax) or 1,
        sens = tonumber(sens) or 1, dn = tonumber(dn) or 0.5,
        stepped = (st == "1"), nsteps = tonumber(ns) or 0,
        curve = pts,
      }
    end
  end
  return map
end

-- UF8 V-Pot step-cycle data (same wire format as readPush, but the first line
-- is the STRIP and the channel is "hud_uf8_push"). Request via "hud_uf8_push_req".
local function readUf8Push()
  local raw = reaper.GetExtState(SECT, "hud_uf8_push")
  if raw == "" then return nil end
  local first = true
  local pd = { strip = -1, steps = {}, params = {} }
  for ln in raw:gmatch("[^\n]+") do
    if first then pd.strip = tonumber(ln) or -1; first = false
    elseif ln:sub(1, 2) == "S;" then
      local param, norm, en, foreign, label =
        ln:match("^S;(%-?%d+);([%-%d%.]+);(%d);(%d);(.*)$")
      if param then
        pd.steps[#pd.steps + 1] = {
          param = tonumber(param), norm = tonumber(norm),
          en = (en == "1"), foreign = (foreign == "1"), label = label or "" }
      end
    elseif ln:sub(1, 2) == "P;" then
      local param, name, opts = ln:match("^P;(%-?%d+);([^;]*);(.*)$")
      if param then
        local olist = {}
        for o in (opts .. "|"):gmatch("([^|]*)|") do
          local n, l = o:match("^([%-%d%.]+)~(.*)$")
          if n then olist[#olist + 1] = { norm = tonumber(n), label = l or "" } end
        end
        pd.params[#pd.params + 1] =
          { param = tonumber(param), name = name or "", opts = olist }
      end
    end
  end
  if pd.strip < 0 then return nil end
  return pd
end

-- UF1 soft-key push-cycle data (same wire format again; the first line is the
-- flat POSITION and the channel is "hud_uf1_push"). Request via
-- "hud_uf1_push_req". Soft-keys only — a V-Pot turns, it doesn't step.
local function readUf1Push()
  local raw = reaper.GetExtState(SECT, "hud_uf1_push")
  if raw == "" then return nil end
  local first = true
  local pd = { pos = -1, steps = {}, params = {} }
  for ln in raw:gmatch("[^\n]+") do
    if first then pd.pos = tonumber(ln) or -1; first = false
    elseif ln:sub(1, 2) == "S;" then
      local param, norm, en, foreign, label =
        ln:match("^S;(%-?%d+);([%-%d%.]+);(%d);(%d);(.*)$")
      if param then
        pd.steps[#pd.steps + 1] = {
          param = tonumber(param), norm = tonumber(norm),
          en = (en == "1"), foreign = (foreign == "1"), label = label or "" }
      end
    elseif ln:sub(1, 2) == "P;" then
      local param, name, opts = ln:match("^P;(%-?%d+);([^;]*);(.*)$")
      if param then
        local olist = {}
        for o in (opts .. "|"):gmatch("([^|]*)|") do
          local n, l = o:match("^([%-%d%.]+)~(.*)$")
          if n then olist[#olist + 1] = { norm = tonumber(n), label = l or "" } end
        end
        pd.params[#pd.params + 1] =
          { param = tonumber(param), name = name or "", opts = olist }
      end
    end
  end
  if pd.pos < 0 then return nil end
  return pd
end

-- "Send to UC1" target list for the UF1 menu. The UC1's slots are NAMED and
-- finite, so the extension publishes them rather than the HUD guessing:
--   first line "N" → this plug-in's map has no UC1 domain (a UF1-only map)
--   else "<linkIdx>;<boundParam>;<slotName>;<boundName>" per slot.
-- Requested via "hud_uf1_uc1_req" = the layer to show, while the submenu is open.
local function readUf1Uc1Slots()
  local raw = reaper.GetExtState(SECT, "hud_uf1_uc1slots")
  if raw == "" then return nil end
  if raw:sub(1, 1) == "N" then return { noDomain = true, slots = {} } end
  local out = { noDomain = false, slots = {} }
  for line in raw:gmatch("[^\n]+") do
    local li, bp, sname, bname = line:match("^(%-?%d+);(%-?%d+);([^;]*);(.*)$")
    if li then
      out.slots[#out.slots + 1] = {
        linkIdx = tonumber(li), param = tonumber(bp),
        name = sname or "", bound = bname or "",
      }
    end
  end
  return out
end

-- Global feel-preset list: "slot;used;name" per line (10 slots).
local function readFeel()
  local raw = reaper.GetExtState(SECT, "hud_feel")
  local arr = {}
  for line in raw:gmatch("[^\n]+") do
    local slot, used, name = line:match("^(%d+);(%d);(.*)$")
    if slot then
      arr[tonumber(slot)] = { used = (used == "1"), name = name or "" }
    end
  end
  return arr
end

-- CS-Switch favourites for the CS-Favourite submenu. Line 0 = "<cur>;<hasCs>"
-- (cur = the slot the focused track's active CS occupies, -1 none; hasCs = 1
-- when there is an active CS to assign). Lines 1..8 = "<i>;<used>;<label>".
-- Line 0 carries an optional 3rd field = the active favourite SOURCE (the
-- assigned set name, or "Base"). 4th return is that source string.
local function readCsFav()
  local raw = reaper.GetExtState(SECT, "hud_cs_fav")
  local cur, hasCs, slots, src, first = -1, false, {}, "", true
  for line in raw:gmatch("[^\n]+") do
    if first then
      local c, h, s = line:match("^(%-?%d+);(%d);(.*)$")
      if not c then c, h = line:match("^(%-?%d+);(%d)$") end
      if c then cur, hasCs, src = tonumber(c), (h == "1"), (s or "") end
      first = false
    else
      local i, used, label = line:match("^(%d+);(%d);(.*)$")
      if i then slots[tonumber(i)] = { used = (used == "1"), label = label or "" } end
    end
  end
  return cur, hasCs, slots, src
end

-- BC-Switch favourites, identical format to hud_cs_fav (Phase 4b).
local function readBcFav()
  local raw = reaper.GetExtState(SECT, "hud_bc_fav")
  local cur, hasBc, slots, src, first = -1, false, {}, "", true
  for line in raw:gmatch("[^\n]+") do
    if first then
      local c, h, s = line:match("^(%-?%d+);(%d);(.*)$")
      if not c then c, h = line:match("^(%-?%d+);(%d)$") end
      if c then cur, hasBc, src = tonumber(c), (h == "1"), (s or "") end
      first = false
    else
      local i, used, label = line:match("^(%d+);(%d);(.*)$")
      if i then slots[tonumber(i)] = { used = (used == "1"), label = label or "" } end
    end
  end
  return cur, hasBc, slots, src
end

-- Plug-in Kurzname (displayShort) seeds, "<cs>;<bc>;<uf8>" — the USER map's
-- short label per domain, empty when that domain has no editable user map.
-- Feeds the inline Kurzname editor (click the LCD / UF8 header).
local function readShort()
  local raw = reaper.GetExtState(SECT, "hud_short")
  local cs, bc, uf8 = raw:match("^([^;]*);([^;]*);(.*)$")
  return cs or "", bc or "", uf8 or ""
end

-- Open the native rename dialog for the active plug-in's Kurzname and push it
-- to the extension. `dom` = "c" CS / "b" BC / "u" UF8. Seeds with the current
-- value so an edit is non-destructive. No-op when the FX has no user map (the
-- extension silently drops it). Frank 2026-06-24.
local function editPluginShort(dom)
  local cs, bc, uf8 = readShort()
  local cur = (dom == "b" and bc) or (dom == "u" and uf8) or cs
  local ok, val = reaper.GetUserInputs("Plug-in name", 1,
    "Short name (max 7),extrawidth=80", cur)
  if not ok then return end
  val = val:gsub("[;\n]", " "):sub(1, 7)
  sendCmd("setshort;" .. dom .. ";" .. val)
end

-- Per-V-Pot tuning detail for the UF8 tab's full-parity menu. One line per
-- mapped V-Pot on the live banks:
--   strip;pol;rmin;rmax;sens;dn;stepped;nsteps;vpotMode;t0:v0,…
local function readUf8Detail()
  local raw = reaper.GetExtState(SECT, "hud_uf8_detail")
  local map = {}
  for line in raw:gmatch("[^\n]+") do
    local strip, pol, rmin, rmax, sens, dn, st, ns, mode, curve =
      line:match("^(%d+);(%d+);([%-%d%.]+);([%-%d%.]+);([%-%d%.]+);([%-%d%.]+);(%d+);(%d+);(%d+);(.*)$")
    if strip then
      local pts = {}
      for t, v in curve:gmatch("([%-%d%.]+):([%-%d%.]+)") do
        pts[#pts + 1] = { tonumber(t), tonumber(v) }
      end
      map[tonumber(strip)] = {
        pol = tonumber(pol) or 0,
        rmin = tonumber(rmin) or 0, rmax = tonumber(rmax) or 1,
        sens = tonumber(sens) or 1, dn = tonumber(dn) or 0.5,
        stepped = (st == "1"), nsteps = tonumber(ns) or 0,
        mode = tonumber(mode) or 0, curve = pts,
      }
    end
  end
  return map
end

-- UF8 device tab (read-only strip-grid). State + per-control assignments come on
-- their own ExtState keys (hud_uf8_state / hud_uf8_assign) so they coexist with
-- the UC1 CS/BC payloads. UF8 has no modifier layers.
--   state : "UF8;<present>;<faderBank>;<vpotBank>;<boot>;<focus>;<short>"
--   assign: "<strip>;<kind>;<paramName>;<inv>"  kind 0=V-Pot 1=Fader 2=Solo 3=Cut 4=Sel
-- boot=1 (present=0): no UF8 map but a virgin FX is under the cursor → the grid
-- renders armable em-dash cells so a click + wiggle bootstraps a UF8-only map.
-- focus=1: the cursor IS the shown UF8 plug-in → auto-switch to the UF8 tab.
local function readUf8State()
  local raw = reaper.GetExtState(SECT, "hud_uf8_state")
  local dev, present, fb, vb, boot, focus, short =
    raw:match("^(%w+);(%d);(%d);(%d);(%d);(%d);(.*)$")
  return {
    present   = (present == "1"),
    boot      = (boot == "1"),
    focus     = (focus == "1"),
    faderBank = tonumber(fb) or 0,
    vpotBank  = tonumber(vb) or 0,
    short     = short or "",
  }
end

local function readUf8Assign()
  local raw = reaper.GetExtState(SECT, "hud_uf8_assign")
  local m = {}   -- m[strip][kind] = { name, inv, mode }  (mode: V-Pot 0/1)
  for line in raw:gmatch("[^\n]+") do
    local strip, kind, name, inv, mode = line:match("^(%d+);(%d+);([^;]*);(%d);(%d)$")
    if not strip then   -- tolerate the old 4-field form (no mode)
      strip, kind, name, inv = line:match("^(%d+);(%d+);([^;]*);(%d)$"); mode = "0"
    end
    if strip then
      local s, k = tonumber(strip), tonumber(kind)
      m[s] = m[s] or {}
      m[s][k] = { name = name or "", inv = (inv == "1"), mode = tonumber(mode) or 0 }
    end
  end
  return m
end

-- V-Pot bank labels + colours (per Top-Soft-Key bank). Interleaved
-- "label0;RRGGBB0;label1;RRGGBB1;…" (16 fields). Returns label table + colour
-- table; labels stay plain strings so existing consumers are unchanged.
local function readUf8Banks()
  local raw = reaper.GetExtState(SECT, "hud_uf8_banks")
  local fields = {}
  for field in (raw .. ";"):gmatch("([^;]*);") do fields[#fields + 1] = field end
  local labels, cols = {}, {}
  for b = 0, 7 do
    labels[b] = fields[b * 2 + 1] or ""
    cols[b]   = tonumber(fields[b * 2 + 2] or "", 16)
  end
  return labels, cols
end

-- Display colour-bar colours for the ACTIVE V-Pot bank's 8 strips.
-- "RRGGBB;RRGGBB;…" (8 fields; "0" / empty = unset).
local function readUf8StripCols()
  local raw = reaper.GetExtState(SECT, "hud_uf8_stripcols")
  local t = {}
  local i = 0
  for field in (raw .. ";"):gmatch("([^;]*);") do
    t[i] = tonumber(field, 16); i = i + 1
    if i >= 8 then break end
  end
  return t
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

-- Content origin below the chrome: the tab row (TAB_H) plus the CS-Favourite
-- row when shown (FAV_H, 0 on the UF8 tab). All body renderers anchor here so
-- the dropdown row reserves its own space without overlapping the list/mockup.
local function bodyTop() return TAB_H + FAV_H end

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

local function drawTabs(st, ust)
  rect(0, 0, WW, TAB_H, col(0x171719, 1))
  tabRects = {}
  local function tab(dom, present, x)
    local label = (dom == "cs") and "Channel Strip"
              or  (dom == "bc") and "Bus Comp"
              or  (dom == "uf1") and "UF1"
              or  "UF8"
    local px    = 10   -- fixed chrome, matches the top-right toggle buttons
    local w, h  = measure(label, px); w = w + 28
    local active = (activeTab == dom)
    local rgb    = (dom == "cs") and csRgb()
               or  (dom == "bc") and bcRgb()
               or  (dom == "uf1") and 0x50C8A0       -- UF1 accent (teal)
               or  0x4A90D8                          -- UF8 accent (blue)
    if active then rect(x, 4, w, TAB_H - 6, col(rgb, present and 0.30 or 0.16))
    else           rect(x, 4, w, TAB_H - 6, col(0x242429, 1)) end
    if active then rect(x, 4, w, 3, col(rgb, present and 1 or 0.5)) end
    dtext(x + 12, 4 + floor((TAB_H - 6 - h) / 2), col(rgb, present and 1 or 0.40), label, px)
    tabRects[#tabRects + 1] = { dom = dom, x = x, y = 4, w = w, h = TAB_H - 6 }
    return x + w + 4
  end
  local x = 8
  x = tab("cs",  st.csPresent,            x)
  x = tab("bc",  st.bcPresent,            x)
  x = tab("uf8", ust and ust.present,     x)
  -- UF1 tab appears whenever the UF1 is showing a plug-in at all — mapped or
  -- not. It used to require a non-empty hud_uf1_assign, i.e. an existing UF1
  -- layer, which meant an UNMAPPED plug-in had no tab and therefore no way to
  -- create one from the HUD, while FX-Learn (and Touch-to-Learn) could
  -- (Frank 2026-08-09). The extension now publishes a bare header for the
  -- unmapped case, so the tab draws its empty grid and a click learns.
  if reaper.GetExtState(SECT, "hud_uf1_assign") ~= "" then
    x = tab("uf1", true, x)
  elseif activeTab == "uf1" then
    activeTab = "cs"                        -- the UF1 shows no plug-in at all
  end

  -- UF8 has no modifier layers, so skip the badge. Both toggles ARE supported on
  -- UF8: Touch-to-Learn (move a UF8 control to arm it, then wiggle a param) and
  -- the Parameter List drawer (software pick-a-param → click-a-cell bind). They
  -- walk LEFT from the right edge, mirroring the UC1 layout.
  local px = 10   -- fixed chrome (badge + the top toggle buttons)
  if activeTab == "uf8" then
    local by, bh = 6, TAB_H - 12
    local rx = WW - 8
    local function uf8Toggle(lbl, on, onRgb)
      local lw, lh = measure(lbl, px)
      local w = lw + 20
      local tx = rx - w
      if on then
        rect(tx, by, w, bh, col(onRgb, 0.95))
        dtext(tx + 10, by + floor((bh - lh) / 2), col(0x161208, 1), lbl, px)
      else
        rect(tx, by, w, bh, col(0x29292E, 1))
        reaper.ImGui_DrawList_AddRect(dl, OX + tx, OY + by, OX + tx + w, OY + by + bh,
          col(0x4A5060, 1), 0, 0, 1)
        dtext(tx + 10, by + floor((bh - lh) / 2), col(0x9098A4, 0.85), lbl, px)
      end
      rx = tx - 6
      return { x = tx, y = by, w = w, h = bh }
    end
    learnBtnRect = uf8Toggle("Touch to Learn",
      reaper.GetExtState(SECT, "hud_touch_learn") == "1", 0xE0A838)   -- amber
    paramBtnRect = uf8Toggle("Parameter List",
      reaper.GetExtState(SECT, "hud_imgui_params") == "1", 0x4A90D8)  -- blue
    return
  end

  -- Active modifier-layer badge (far right).
  local name = (st.layer == 1 and "OPT") or (st.layer == 2 and "CTRL")
            or (st.layer == 3 and "C+O") or "NORM"
  local bwm, bhm = measure(name, px)
  local bw = bwm + 20
  local bh = TAB_H - 12
  local bx = WW - bw - 8
  local by = 6
  if st.layer == 0 then
    rect(bx, by, bw, bh, col(0x29292E, 1))
    dtext(bx + 10, by + floor((bh - bhm) / 2), col(0x8890A0, 0.7), name, px)
  else
    local lc = (st.layer == 1 and 0x30C8A0)
            or (st.layer == 2 and 0xC878FF)
            or 0xE0A040   -- C+O = amber
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

-- Control idx under (mx,my) using the last frame's hit-rects (knob = circle,
-- toggle/btn/list-row = box). Returns nil when the point is over no control.
local function controlAt(mx, my)
  for _, h in ipairs(ctrlRects) do
    local hit
    if h.shape == 0 then
      local dx, dy = mx - h.x, my - h.y
      hit = (dx * dx + dy * dy) <= (h.r + 3) * (h.r + 3)
    else
      hit = mx >= h.x and mx <= h.x + h.w and my >= h.y and my <= h.y + h.h
    end
    if hit then return h.idx end
  end
  return nil
end

-- UF8 grid cell under (mx,my) → {kind, strip}, or nil. Uses last frame's rects.
local function uf8CellAt(mx, my)
  for _, h in ipairs(uf8Rects) do
    if mx >= h.x and mx <= h.x + h.w and my >= h.y and my <= h.y + h.h then
      return h.kind, h.strip
    end
  end
  return nil
end

-- V-Pot bank cell under (mx,my) → bank index 0..7, or nil.
local function uf8BankAt(mx, my)
  for _, h in ipairs(uf8BankRects) do
    if mx >= h.x and mx <= h.x + h.w and my >= h.y and my <= h.y + h.h then
      return h.b
    end
  end
  return nil
end

-- Display colour-bar under (mx,my) → strip index 0..7, or nil (mockup only).
local function uf8ColAt(mx, my)
  for _, h in ipairs(uf8ColRects) do
    if mx >= h.x and mx <= h.x + h.w and my >= h.y and my <= h.y + h.h then
      return h.s
    end
  end
  return nil
end

local function handleControlClick(mx, my)
  local idx = controlAt(mx, my)
  if not idx then
    -- Click on empty content area → cancel any armed learn (easy mouse escape).
    if learnIdx >= 0 and selectedParam < 0 then sendCmd("cancel") end
    return false
  end
  if selectedParam >= 0 then
    -- Param picked in the list → assign it straight to this control.
    sendCmd("bind;" .. idx .. ";" .. selectedParam)
    selectedParam = -1
  elseif learnIdx == idx then sendCmd("cancel")
  else                        sendCmd("learn;" .. idx) end
  return true
end

-- UF8 grid: left-click a cell → arm a learn (click the armed cell again to
-- cancel). The wiggle then binds via the extension. Banks are resolved C++-side.
local function handleUf8CellClick(mx, my)
  local kind, strip = uf8CellAt(mx, my)
  if not kind then return false end
  -- A param picked in the drawer → software-bind it onto this cell (no wiggle).
  if selectedParam >= 0 then
    sendCmd("uf8bind;" .. kind .. ";" .. strip .. ";" .. selectedParam)
    selectedParam = -1
    return true
  end
  if uf8Learn == (kind * 8 + strip) then sendCmd("uf8cancel")
  else                                   sendCmd("uf8learn;" .. kind .. ";" .. strip) end
  return true
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
  local top    = bodyTop() + 10
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
  -- Fallback (extension hasn't published yet): no active-FX info here, so
  -- line 3 stays blank rather than showing the old Stereo/Mono.
  local tr = reaper.GetLastTouchedTrack()
  if not tr then return "--", "TRACK", "(no track)", "" end
  local num = reaper.GetMediaTrackInfo_Value(tr, "IP_TRACKNUMBER")
  local _, name = reaper.GetSetMediaTrackInfo_String(tr, "P_NAME", "", false)
  local seg, top
  if num == -1 then
    seg, top = "M", "MASTER"; if name == "" then name = "Master" end
  else
    seg, top = string.format("%03d", floor(num)), "TRACK"
    if name == "" then name = "Track " .. floor(num) end
  end
  return seg, top, name, ""
end

local function renderFace(st, asn)
  local DW, DH = geom.w or 860, geom.h or 660
  local M, top = 8, bodyTop() + 6
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
  local det = readDetail()   -- per-knob invert/range/curve for the indicators

  -- Hover-tooltip suffix: show the sensitivity factor when it isn't the
  -- default 1.0 (knobs/V-Pots only; buttons have no det entry → ""). Frank 2026-06-25.
  local function sensTip(idx)
    local d = det[idx]
    if d and d.sens and math.abs(d.sens - 1.0) > 0.001 then
      return string.format("  \xC2\xB7  sens %.2g\xC3\x97", d.sens)
    end
    return ""
  end

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
    -- Invert "i" badge + range-limit ticks on the ring — parity with the
    -- FX-Learn schematic. Min tick green, Max tick red; centre dot = curve set.
    if idx and mapped then
      if asn[idx].inv then
        dtext(sx + r * 0.5, sy - r * 1.45, col(0xFFC04C, f), "i", lf)
      end
      local d = det[idx]
      if d and (d.rmin > 0.001 or d.rmax < 0.999 or #d.curve > 0) then
        local function tickAt(v, c)
          local a  = 2.0944 + v * 5.236   -- 7 o'clock start, 300° CW sweep
          local ca, sa = math.cos(a), math.sin(a)
          reaper.ImGui_DrawList_AddLine(dl,
            OX + sx + (r + 2) * ca, OY + sy + (r + 2) * sa,
            OX + sx + (r + 7) * ca, OY + sy + (r + 7) * sa,
            c, math.max(1, r * 0.12))
        end
        tickAt(d.rmin, col(0x8FE3A8, f))
        tickAt(d.rmax, col(0xE38F8F, f))
        if #d.curve > 0 then
          reaper.ImGui_DrawList_AddCircleFilled(dl, OX + sx, OY + sy,
            math.max(1.5, r * 0.12), col(0xFFC080, f))
        end
      end
    end
    if idx then
      ctrlRects[#ctrlRects + 1] = { idx = idx, shape = 0, x = sx, y = sy, r = r }
      local dx, dy = lmx - sx, lmy - sy
      if dx * dx + dy * dy <= (r + 3) * (r + 3) then
        local g = geom.ctrl[idx]; tipSlot = (g and g.label) or lbl
        tipParam = mapped and (asn[idx].name .. (asn[idx].inv and "  (inverted)" or "") .. sensTip(idx)) or nil
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
        tipParam = mapped and (asn[idx].name .. (asn[idx].inv and "  (inverted)" or "") .. sensTip(idx)) or nil
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
  dTextC(430, 22, col(0x9CA0AA, da("b")), "BUS COMP", sf)

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
  dTextC(lcdCx, kCcpY + 66, col(0x4488DD, 1), fit(l3, lcdW * scale - 8, lf), lf)
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
  local top = bodyTop()
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

  local rgb = (activeTab == "cs") and csRgb()
           or (activeTab == "bc") and bcRgb()
           or 0x4A90D8                                   -- UF8 accent (blue)
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
  if activeTab == "uf8" then
    -- UF8 assign is uasn[strip][kind] = { name, ... } — dot every bound param.
    for _, kinds in pairs(asn) do
      for _, a in pairs(kinds) do
        if a.name and a.name ~= "" then mappedNames[a.name] = true end
      end
    end
  else
    for idx, a in pairs(asn) do
      local g = geom.ctrl[idx]
      if g and ((activeTab == "cs" and g.dom == "c")
             or (activeTab == "bc" and g.dom == "b")) then
        mappedNames[a.name] = true
      end
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

-- UF8 device tab — interactive strip-grid. 8 strip columns × 5 control rows
-- (V-Pot / Fader / Solo / Cut / Sel), left gutter carries the row labels, top
-- row the strip numbers. Cells show the bound plug-in param name (em-dash when
-- unmapped, "i" when inverted). Follows the live hardware banks via the state
-- header. Phase 2 (interactive): left-click a cell to learn (wiggle a param);
-- right-click for Invert / Fill sequential / Unbind; virgin plug-ins bootstrap
-- a UF8-only map. See handleUf8CellClick / drawUf8ControlContextMenu.
local UF8_KINDS = {
  { k = 0, l = "V-Pot" }, { k = 1, l = "Fader" }, { k = 2, l = "Solo" },
  { k = 3, l = "Cut"   }, { k = 4, l = "Sel"   },
}
local UF8_ACCENT = 0x4A90D8

-- UF1 plugin-mode tab (v11). Deliberately the same picture as the Settings
-- editor's UF1 mockup: a page selector over two rows of four (V-Pots then
-- soft-keys), because that is exactly what the UF1 surfaces at once. Click a
-- cell to arm a learn on the hardware position; right-click for unbind/invert.
local uf1Rects     = {}
local uf1PageRects = {}
local uf1EditPage  = 0
local function renderUf1Tab(u1)
  uf1Rects, uf1PageRects = {}, {}
  local px = 12
  -- BELOW the edit row (Name / favourites), which drawHudEditRow paints for
  -- every tab at TAB_H. Starting at TAB_H + 10 drew the page buttons straight
  -- over the Name field (Frank 2026-08-09). bodyTop() is what a tab body may use.
  local y  = bodyTop() + 6
  if not u1 then
    dtext(14, y, col(0x9A9AA2, 1),
          "This plug-in has no UF1 layer — turn it on in Settings \xE2\x86\x92 FX Learn.", px)
    return
  end
  if uf1EditPage >= u1.pages then uf1EditPage = u1.pages - 1 end
  if uf1EditPage < 0 then uf1EditPage = 0 end

  -- WHICH plug-in this grid is. The Name field above belongs to the shared edit
  -- row (the CS/BC plug-in), and the UF1 can be focused on a different track —
  -- so without this an unmapped UF1 target looked like the named plug-in's map
  -- had vanished (Frank 2026-08-09).
  if u1.name ~= "" then
    dtext(14, y, col(u1.mapped and 0x50C8A0 or 0xD8A050, 1),
          u1.mapped and ("UF1 map: " .. u1.name)
                     or ("No UF1 map yet: " .. u1.name
                         .. " \xE2\x80\x94 click a control to create one"), px - 1)
    y = y + 20
  end

  -- Page selector.
  local x = 14
  for p = 0, u1.pages - 1 do
    local lbl  = "Page " .. (p + 1)
    local w, h = measure(lbl, px); w = w + 20
    local on   = (p == uf1EditPage)
    rect(x, y, w, h + 8, col(on and 0x4A90D8 or 0x242429, on and 0.35 or 1))
    if on then rect(x, y, w, 3, col(0x4A90D8, 1)) end
    dtext(x + 10, y + 4, col(on and 0xE8E8EE or 0x9A9AA2, 1), lbl, px)
    uf1PageRects[#uf1PageRects + 1] = { p = p, x = x, y = y, w = w, h = h + 8 }
    x = x + w + 6
  end
  y = y + 34

  local armed = tonumber(reaper.GetExtState(SECT, "hud_uf1_learn")) or -1
  local CW, CH, GAP = 150, 46, 8
  -- Soft-keys ABOVE the V-Pots, matching the hardware layout and the FX-Learn
  -- page (Frank 2026-08-09).
  for _, row in ipairs({ { sk = 1, name = "Soft-keys" }, { sk = 0, name = "V-Pots" } }) do
    dtext(14, y, col(0x9A9AA2, 1), row.name, px - 1)
    y = y + 18
    for i = 0, 3 do
      local pos  = uf1EditPage * 4 + i
      local cell = u1.cells[row.sk][pos]
      local cx   = 14 + i * (CW + GAP)
      local isArmed = (armed >= 0) and (armed & 0xFF) == pos
                      and (((armed & 0x100) ~= 0) == (row.sk == 1))
      -- Muted green for an INHERITED position: live on the hardware, but taken
      -- from the plug-in's own map rather than bound here.
      local bg = isArmed and 0x8A6A20
              or (cell and (cell.inherited and 0x24402E or 0x2E5C3A) or 0x242429)
      rect(cx, y, CW, CH, col(bg, 1))
      local label = isArmed and "\xE2\x97\x8F listening\xE2\x80\xA6"
                 or (cell and (cell.label ~= "" and cell.label
                               or ("param " .. tostring(cell.param))) or "\xE2\x80\x94")
      -- A soft-key's label carries its LED colour so the tab reads like the
      -- hardware — you find the key by colour, not by counting (Frank
      -- 2026-08-09). Listening keeps the amber wording.
      local labCol = (cell or isArmed) and 0xE8E8EE or 0x6A6A72
      if cell and row.sk == 1 and (cell.ledRgb or 0) ~= 0 and not isArmed then
        labCol = cell.ledRgb
      end
      dtext(cx + 8, y + 8, col(labCol, 1), label, px)
      if cell and cell.inv then
        dtext(cx + 8, y + 26, col(0xD8A050, 1), "inverted", px - 2)
      end
      -- Per-key LED colour (soft-keys, v12): a stripe down the right edge so
      -- the tab shows what the hardware key will light up as.
      if cell and row.sk == 1 and (cell.ledRgb or 0) ~= 0 then
        rect(cx + CW - 6, y + 4, 3, CH - 8, col(cell.ledRgb, 1))
      end
      uf1Rects[#uf1Rects + 1] = { sk = row.sk, pos = pos, x = cx, y = y, w = CW, h = CH }
    end
    y = y + CH + 12
  end
  dtext(14, y + 2, col(0x6A6A72, 1),
        "Click a control to learn it \xC2\xB7 click it again (or empty space) to cancel"
        .. " \xC2\xB7 right-click to unbind or invert", px - 2)
end
local function uf1CellAt(mx, my)
  for _, h in ipairs(uf1Rects) do
    if mx >= h.x and mx <= h.x + h.w and my >= h.y and my <= h.y + h.h then
      return h.sk, h.pos
    end
  end
  return nil
end
local function uf1PageAt(mx, my)
  for _, h in ipairs(uf1PageRects) do
    if mx >= h.x and mx <= h.x + h.w and my >= h.y and my <= h.y + h.h then
      return h.p
    end
  end
  return nil
end

local function renderUf8Grid(ust, uasn)
  ctrlRects = {}                                  -- no UC1 hit-rects on this tab
  uf8Rects  = {}
  uf8ColRects = {}                                -- colour bars are mockup-only
  local availW = WW - RW
  local top    = bodyTop()
  local hpx    = floor(13 * fontScale() + 0.5)

  local sub
  if ust.present then
    sub = (ust.short ~= "" and ust.short or "UF8 plug-in")
        .. "      Fader Bank " .. (ust.faderBank + 1) .. " / 2"
  elseif ust.boot then
    sub = "Map " .. (ust.short ~= "" and ust.short or "plug-in")
        .. "  \xE2\x80\x94  click a cell, then wiggle a parameter to create a UF8 map"
  else
    sub = "No UF8 plug-in on the focused track"
  end
  local _, subH = measure(sub, hpx)
  dtext(10, top + 6, col(0x9098A4, 0.95), sub, hpx)
  -- Nothing to map (no UF8 map AND no virgin FX) → no grid.
  if not ust.present and not ust.boot then return end

  -- V-Pot bank selector row: the 8 Top-Soft-Key banks, current one bright.
  -- Clickable — click a bank to switch it (sets the live Top-Soft-Key bank via
  -- the extension), so you can reach + map empty banks from the HUD without
  -- needing the hardware soft-keys. The hardware Top-Soft-Keys drive it too.
  uf8BankRects = {}
  uf8Banks, uf8BankCols = readUf8Banks()
  local bankPx = floor(11 * fontScale() + 0.5)
  local bankY  = top + 6 + subH + 6
  local bankH  = floor(bankPx + 8 * fontScale() + 0.5)
  do
    local _, glh = measure("V-Pot Bank", bankPx)
    dtext(10, bankY + (bankH - glh) / 2, col(0x8890A0, 0.85), "V-Pot Bank", bankPx)
    local minW = floor(22 * fontScale() + 0.5)
    local maxW = floor(96 * fontScale() + 0.5)
    local bx   = 10 + measure("V-Pot Bank", bankPx) + 10
    -- Named banks show their label (right-click to rename); else the number.
    -- Cells auto-size to the label so names stay legible.
    for b = 0, 7 do
      local lbl  = uf8Banks[b]
      local txt  = (lbl and lbl ~= "") and lbl or tostring(b + 1)
      local tw   = measure(txt, bankPx)
      local cw   = math.max(minW, math.min(maxW, tw + 12))
      local disp = fit(txt, cw - 8, bankPx)
      local on   = (b == ust.vpotBank)
      rect(bx, bankY, cw, bankH, col(on and UF8_ACCENT or 0x2A2A30, on and 0.9 or 1))
      local dw, dh = measure(disp, bankPx)
      dtext(bx + (cw - dw) / 2, bankY + (bankH - dh) / 2,
            col(on and 0x121214 or 0x9098A4, on and 1 or 0.8), disp, bankPx)
      uf8BankRects[#uf8BankRects + 1] = { b = b, x = bx, y = bankY, w = cw, h = bankH }
      bx = bx + cw + 4
    end
  end

  local gridTop = bankY + bankH + 10
  local px      = floor(12 * fontScale() + 0.5)
  local lblPx   = floor(11 * fontScale() + 0.5)
  local rowH    = floor(px + 15 * fontScale() + 0.5)
  local Lw      = floor(58 * fontScale() + 0.5)
  local colW    = (availW - Lw - 6) / 8

  -- header row: strip numbers 1..8
  for s = 0, 7 do
    local cx = Lw + s * colW
    local hs = tostring(s + 1)
    local tw, th = measure(hs, lblPx)
    dtext(cx + (colW - tw) / 2, gridTop + (rowH - th) / 2,
          col(UF8_ACCENT, 0.9), hs, lblPx)
  end
  rect(0, gridTop + rowH - 1, availW, 1, col(0x303036, 1))

  for r, kd in ipairs(UF8_KINDS) do
    local ry = gridTop + r * rowH
    if r % 2 == 0 then rect(0, ry, availW, rowH, col(0xFFFFFF, 0.025)) end
    local _, lh = measure(kd.l, lblPx)
    dtext(6, ry + (rowH - lh) / 2, col(0xC0C6D0, 0.85), kd.l, lblPx)
    for s = 0, 7 do
      local cx = Lw + s * colW
      if s > 0 then rect(cx, ry, 1, rowH, col(0x2A2A30, 0.8)) end

      -- Armed-learn pulse on the cell.
      if uf8Learn == (kd.k * 8 + s) then
        local pulse = 0.22 + 0.22 * math.abs((frame % 50) / 25 - 1)
        rect(cx + 1, ry + 1, colW - 2, rowH - 2, col(0xFFFFFF, pulse))
      end

      local a = uasn[s] and uasn[s][kd.k]
      if a and a.name ~= "" then
        local nm    = fit(a.name, colW - (a.inv and 18 or 10), px)
        local _, nh = measure(nm, px)
        dtext(cx + 6, ry + (rowH - nh) / 2, col(0xE8ECF2, 0.96), nm, px)
        if a.inv then
          local _, ih = measure("i", lblPx)
          dtext(cx + colW - 11, ry + (rowH - ih) / 2, col(UF8_ACCENT, 0.9), "i", lblPx)
        end
      else
        local em = "\xE2\x80\x94"
        local _, eh = measure(em, px)
        dtext(cx + 6, ry + (rowH - eh) / 2, col(0x60656E, 0.7), em, px)
      end

      uf8Rects[#uf8Rects + 1] =
        { kind = kd.k, strip = s, x = cx, y = ry, w = colW, h = rowH }
    end
  end
  -- (The learn / touch-learn / assigning banner is drawn once in render(), after
  -- the view dispatch, so it covers both the grid and mockup views.)
end

-- UF8 device tab — hardware MOCKUP view, faithful to the FX-Learn UF8 face
-- (drawUf8Face_): design-space 860x520 scaled to fit. The 8 Top-Soft-Keys are
-- the V-Pot bank selectors (label + colour); per strip a scribble LCD, a small
-- V-Pot ring, stacked SOLO/CUT/SEL and a tall fader. Rebuilds the same
-- uf8Rects/uf8BankRects the grid does so learn / colour / bank / context menus
-- work unchanged. The strip-grid (renderUf8Grid) is the list view.
local function renderUf8Face(ust, uasn)
  ctrlRects    = {}
  uf8Rects     = {}
  uf8BankRects = {}
  uf8ColRects  = {}
  uf8Banks, uf8BankCols = readUf8Banks()
  uf8StripCols = readUf8StripCols()

  -- Same sizing as the CS/BC mockup (renderFace): design face centred in the
  -- full content area, scaled to fit, accounting for the param-list strip (RW).
  local FW, FH = 860, 520
  local M, top = 8, bodyTop() + 6
  local availW, availH = (WW - RW) - 2 * M, WH - top - M
  if availW < 60 or availH < 60 then return end

  if not ust.present and not ust.boot then
    local msg = "No UF8 plug-in on the focused track"
    local mpx = floor(14 * fontScale() + 0.5)
    local tw, th = measure(msg, mpx)
    dtext(M + (availW - tw) / 2, top + (availH - th) / 2, col(0x808890, 0.9), msg, mpx)
    return
  end

  local sc      = math.min(availW / FW, availH / FH)
  local faceX   = M + (availW - FW * sc) / 2
  local faceTop = top + (availH - FH * sc) / 2

  -- Compact context line in the top centring margin (plugin + fader bank).
  do
    local ipx  = floor(12 * fontScale() + 0.5)
    local info = ust.boot
      and ("Map " .. (ust.short ~= "" and ust.short or "plug-in")
           .. "  \xE2\x80\x94  click a control, then wiggle a parameter")
      or  ((ust.short ~= "" and ust.short or "UF8")
           .. "      Fader Bank " .. (ust.faderBank + 1) .. " / 2")
    dtext(M + 2, top, col(0x9098A4, 0.9), info, ipx)
  end

  local function px(dx) return faceX + dx * sc end
  local function py(dy) return faceTop + dy * sc end
  local function sr(dx, dy, dw, dh, fill, outl, rnd)
    local x1, y1 = OX + px(dx), OY + py(dy)
    local x2, y2 = OX + px(dx + dw), OY + py(dy + dh)
    reaper.ImGui_DrawList_AddRectFilled(dl, x1, y1, x2, y2, fill, (rnd or 0) * sc)
    if outl then
      reaper.ImGui_DrawList_AddRect(dl, x1, y1, x2, y2, outl, (rnd or 0) * sc, 0, math.max(1, sc))
    end
  end
  local function sctext(dxc, dyc, c, s, pxsz)
    local tw, th = measure(s, pxsz)
    dtext(px(dxc) - tw / 2, py(dyc) - th / 2, c, s, pxsz)
  end

  -- design constants (verbatim from SettingsScreen.cpp drawUf8Face_)
  local STRIPW, GAP, OXs = 80, 7, 86
  local function cxOf(s) return OXs + s * (STRIPW + GAP) + STRIPW / 2 end
  local TSK_Y, TSK_H = 12, 22
  local SCR_Y, SCR_H = 40, 58
  local BAR_H        = 16
  local VP_CY, VP_R  = 124, 18
  local SOLO_Y, CUT_Y, SEL_Y, SCS_H = 152, 172, 192, 16
  -- Fader name band sits between SEL (ends 208) and the rail so a long fader
  -- param name ("Volume A") has room and never overlaps SEL (Frank 2026-06-17).
  local NAME_Y = 214
  local RAIL_Y, RAIL_H, RAIL_W = 234, 222, 22

  local fLbl = math.max(7, floor(12 * sc + 0.5))
  local fSm  = math.max(7, floor(11 * sc + 0.5))

  local cChassis = col(0x14181E, 1)
  local cEdge    = col(0x2A3038, 1)
  local cBtnFill = col(0x252A33, 1)
  local cBtnEdge = col(0x4A5060, 1)
  local cScrib   = col(0x080C12, 1)
  local cScrEdge = col(0x444A55, 1)
  local cRingIn  = col(0x555A66, 1)
  local cRail    = col(0x303338, 1)
  local cSilk    = col(0x9CA0AA, 1)
  local cAcc     = col(UF8_ACCENT, 1)
  local cAccDim  = col(UF8_ACCENT, 0.28)
  local cWhite12 = col(0x121214, 1)

  sr(4, 4, FW - 8, FH - 8, cChassis, cEdge, 8)

  local vpotBank = ust.vpotBank or 0
  for s = 0, 7 do
    local cx   = cxOf(s)
    local colX = cx - STRIPW / 2

    -- Top-Soft-Key = bank-s selector (label + colour). Active = current bank.
    do
      local on   = (s == vpotBank)
      local bcol = uf8BankCols[s]
      sr(colX + 6, TSK_Y, STRIPW - 12, TSK_H, on and cAcc or cBtnFill, cBtnEdge, 3.5)
      if bcol then
        sr(colX + 6, TSK_Y + TSK_H - 3, STRIPW - 12, 3, (bcol << 8) | 0xFF, nil, 0)
      end
      local lbl = uf8Banks[s]
      lbl = (lbl and lbl ~= "") and lbl or tostring(s + 1)
      sctext(cx, TSK_Y + TSK_H / 2 - 1, on and cWhite12 or cSilk,
             fit(lbl, (STRIPW - 16) * sc, fLbl), fLbl)
      uf8BankRects[#uf8BankRects + 1] =
        { b = s, x = px(colX + 6), y = py(TSK_Y), w = (STRIPW - 12) * sc, h = TSK_H * sc }
    end

    -- Scribble LCD — V-Pot param name (mapped) like a track name.
    sr(colX + 4, SCR_Y, STRIPW - 8, SCR_H, cScrib, cScrEdge, 2)
    do
      local a = uasn[s] and uasn[s][0]
      if a and a.name ~= "" then
        sctext(cx, SCR_Y + 14, col(0xE8ECF2, 0.96), fit(a.name, (STRIPW - 12) * sc, fSm), fSm)
      end
    end
    -- Display colour bar — per-strip stripColour (RGBA 0xRRGGBBAA), dim grey
    -- when unset. Clickable like the FX-Learn mockup: left-click picks THIS
    -- strip's colour, right-click fills all 8 strips of the active bank.
    do
      local bar = uf8StripCols and uf8StripCols[s]
      local bfill = (bar and bar ~= 0) and ((bar << 8) | 0xFF) or col(0x404040, 0.5)
      local bx, by = colX + 6, SCR_Y + SCR_H - BAR_H - 2
      sr(bx, by, STRIPW - 12, BAR_H, bfill, nil, 0)
      uf8ColRects[#uf8ColRects + 1] =
        { s = s, x = px(bx), y = py(by), w = (STRIPW - 12) * sc, h = BAR_H * sc }
    end

    -- V-Pot ring (kind 0)
    do
      local a        = uasn[s] and uasn[s][0]
      local mapped   = a and a.name ~= ""
      local learning = uf8Learn == s
      local scx, scy = OX + px(cx), OY + py(VP_CY)
      local r = VP_R * sc
      reaper.ImGui_DrawList_AddCircleFilled(dl, scx, scy, r, cChassis)
      reaper.ImGui_DrawList_AddCircle(dl, scx, scy, r, cBtnEdge, 0, math.max(1, sc))
      reaper.ImGui_DrawList_AddCircleFilled(dl, scx, scy, r - 4 * sc, mapped and cAcc or cBtnFill)
      reaper.ImGui_DrawList_AddCircle(dl, scx, scy, r - 4 * sc, cRingIn, 0, math.max(1, sc))
      if learning then
        local p = 0.4 + 0.4 * math.abs((frame % 50) / 25 - 1)
        reaper.ImGui_DrawList_AddCircle(dl, scx, scy, r + 2 * sc, col(0xFFFFFF, p), 0, math.max(1, 2 * sc))
      end
      reaper.ImGui_DrawList_AddLine(dl, scx, scy - (VP_R - 2) * sc, scx, scy - (VP_R - 10) * sc,
        col(0xCCCCCC, 1), math.max(1, 2 * sc))
      uf8Rects[#uf8Rects + 1] =
        { kind = 0, strip = s, x = px(cx - VP_R), y = py(VP_CY - VP_R), w = 2 * VP_R * sc, h = 2 * VP_R * sc }
    end

    -- SOLO / CUT / SEL stacked (kinds 2/3/4)
    local function scsBtn(kind, dy, deflbl)
      local a        = uasn[s] and uasn[s][kind]
      local mapped   = a and a.name ~= ""
      local learning = uf8Learn == (kind * 8 + s)
      sr(colX + 8, dy, STRIPW - 16, SCS_H, mapped and cAccDim or cBtnFill,
         mapped and cAcc or cBtnEdge, 3)
      if learning then
        local p = 0.3 + 0.3 * math.abs((frame % 50) / 25 - 1)
        sr(colX + 7, dy - 1, STRIPW - 14, SCS_H + 2, 0, col(0xFFFFFF, p), 3)
      end
      sctext(cx, dy + SCS_H / 2, mapped and col(0xFFFFFF, 1) or cSilk,
             fit(mapped and a.name or deflbl, (STRIPW - 18) * sc, fSm), fSm)
      uf8Rects[#uf8Rects + 1] =
        { kind = kind, strip = s, x = px(colX + 8), y = py(dy), w = (STRIPW - 16) * sc, h = SCS_H * sc }
    end
    scsBtn(2, SOLO_Y, "SOLO")
    scsBtn(3, CUT_Y,  "CUT")
    scsBtn(4, SEL_Y,  "SEL")

    -- Fader rail + thumb (kind 1)
    do
      local a        = uasn[s] and uasn[s][1]
      local mapped   = a and a.name ~= ""
      local learning = uf8Learn == (8 + s)
      sr(cx - RAIL_W / 2, RAIL_Y, RAIL_W, RAIL_H, cRail, cEdge, 4)
      local thumbY = RAIL_Y + RAIL_H / 2 - 5
      sr(cx - RAIL_W / 2 - 1, thumbY, RAIL_W + 2, 10, mapped and cAcc or col(0x60C060, 0.85),
         col(0xE8F0E8, 1), 2)
      if learning then
        local p = 0.3 + 0.3 * math.abs((frame % 50) / 25 - 1)
        sr(cx - RAIL_W / 2 - 2, thumbY - 1, RAIL_W + 4, 12, 0, col(0xFFFFFF, p), 2)
      end
      if mapped then
        sctext(cx, NAME_Y + 6, col(0xC0C6D0, 0.95), fit(a.name, STRIPW * sc, fSm), fSm)
      end
      uf8Rects[#uf8Rects + 1] =
        { kind = 1, strip = s, x = px(cx - RAIL_W / 2 - 4), y = py(RAIL_Y),
          w = (RAIL_W + 8) * sc, h = RAIL_H * sc }
    end

    sctext(cx, RAIL_Y + RAIL_H + 14, col(0x707680, 1), tostring(s + 1), fSm)
  end

  -- Bank L / R (decorative; hardware Bank buttons drive the live fader bank).
  do
    local bcx = (cxOf(3) + cxOf(4)) / 2
    local BW, BY, BH = 60, 482, 18
    sr(bcx - 4 - BW, BY, BW, BH, cBtnFill, cBtnEdge, 3)
    sctext(bcx - 4 - BW / 2, BY + BH / 2, cSilk, "BANK \xE2\x97\x82", fSm)
    sr(bcx + 4, BY, BW, BH, cBtnFill, cBtnEdge, 3)
    sctext(bcx + 4 + BW / 2, BY + BH / 2, cSilk, "BANK \xE2\x96\xB8", fSm)
  end

  if uf8Learn >= 0 then
    local kd  = UF8_KINDS[floor(uf8Learn / 8) + 1]
    local stp = (uf8Learn % 8) + 1
    local msg = "Learning " .. ((kd and kd.l) or "?") .. " " .. stp
             .. "  \xE2\x80\x94  wiggle a plug-in parameter to bind it"
             .. "   (click again or Esc to cancel)"
    local mpx = floor(13 * fontScale() + 0.5)
    local tw, th = measure(msg, mpx)
    rect(8, top + 4, tw + 20, th + 8, col(0x101418, 0.9))
    dtext(18, top + 8, col(0xFFD060, 1), msg, mpx)
  end
end

-- Edit row above the body (all tabs): an always-visible "Name" field for the
-- plug-in Kurzname (click → native dialog) plus, on the CS tab ONLY, the
-- CS-Favourite slot dropdown. Both are DrawList "fields" styled with a border
-- so the edit affordance is visible (Frank: the click-the-LCD edit wasn't).
-- Sets FAV_H so the body shifts down; registers nameRect + csFavRect.
local CS_FAV_ACCENT = 0x4A90D8
local function field(x, y, w, h, fillRgb)
  rect(x, y, w, h, col(fillRgb, 1))
  rect(x,         y,         w, 1, col(0x3A3A42, 1))   -- top edge
  rect(x,         y + h - 1, w, 1, col(0x3A3A42, 1))   -- bottom edge
  rect(x,         y,         1, h, col(0x3A3A42, 1))   -- left edge
  rect(x + w - 1, y,         1, h, col(0x3A3A42, 1))   -- right edge
end
local function drawHudEditRow()
  local fpx  = floor(12 * fontScale() + 0.5)
  local rowH = floor(fpx + 11 * fontScale() + 0.5)
  FAV_H = rowH + 8
  local y = TAB_H + 4
  rect(0, TAB_H, WW - RW, FAV_H, col(0x131316, 1))

  local dom = (activeTab == "bc") and "b" or (activeTab == "uf8") and "u" or "c"
  local cs, bc, uf8 = readShort()
  local curShort = (dom == "b" and bc) or (dom == "u" and uf8) or cs

  -- Name field (Kurzname) — all tabs.
  local nlbl = "Name:"
  local nlw, nlh = measure(nlbl, fpx)
  dtext(10, y + (rowH - nlh) / 2, col(0x8890A0, 0.9), nlbl, fpx)
  local nbx  = 10 + nlw + 8
  local nbW  = floor(120 * fontScale() + 0.5)
  field(nbx, y, nbW, rowH, 0x202024)
  local ntxt = (curShort ~= "" and curShort) or "click to set\xE2\x80\xA6"
  local ncol = (curShort ~= "") and 0xD0D6E0 or 0x60656E
  local nd   = fit(ntxt, nbW - 12, fpx)
  local _, ndh = measure(nd, fpx)
  dtext(nbx + 6, y + (rowH - ndh) / 2, col(ncol, 1), nd, fpx)
  nameRect = { x = nbx, y = y, w = nbW, h = rowH, dom = dom }

  -- Favourite dropdown — CS tab shows the CS-Fav list, BC tab the BC-Fav list
  -- (Phase 4b; BC mirrors the CS row Frank knew from 2026-06-24).
  local rightX = nbx + nbW + 22
  if activeTab == "cs" or activeTab == "bc" then
    local isCs = (activeTab == "cs")
    -- NB: `isCs and readCsFav() or readBcFav()` would collapse the returns to
    -- the first — call explicitly so has/slots/src survive.
    local cur, has, slots, src
    if isCs then cur, has, slots, src = readCsFav()
    else         cur, has, slots, src = readBcFav() end
    -- Annotate the source set ("Base" when no named set is assigned).
    local srcTag = (src ~= "" and src) or "Base"
    if #srcTag > 12 then srcTag = srcTag:sub(1, 12) end
    local flbl = (isCs and "CS Fav" or "BC Fav") .. " \xC2\xB7 " .. srcTag .. ":"
    local flw  = measure(flbl, fpx)
    local fbx  = rightX
    dtext(fbx, y + (rowH - nlh) / 2, col(0x8890A0, 0.9), flbl, fpx)
    local bx   = fbx + flw + 8
    local boxW = floor(160 * fontScale() + 0.5)
    local cval
    if not has then
      cval = isCs and "(no CS)" or "(no BC)"
    elseif cur >= 0 then
      local s = slots[cur] or { label = "" }
      cval = string.format("%d \xE2\x80\x94 %s", cur + 1,
                           s.label ~= "" and s.label or "?")
    else
      cval = "None"
    end
    field(bx, y, boxW, rowH, has and 0x2A2A30 or 0x202024)
    local disp  = fit(cval, boxW - 24, fpx)
    local _, dh = measure(disp, fpx)
    dtext(bx + 8, y + (rowH - dh) / 2,
          col(has and 0xC6CCD6 or 0x6A6F78, 1), disp, fpx)
    dtext(bx + boxW - 15, y + (rowH - dh) / 2,
          col(has and CS_FAV_ACCENT or 0x6A6F78, 0.95), "\xE2\x96\xBE", fpx)
    if has then
      if isCs then csFavRect = { x = bx, y = y, w = boxW, h = rowH }
      else        bcFavRect = { x = bx, y = y, w = boxW, h = rowH } end
    end
    rightX = bx + boxW + 18
  end
end

local function render()
  TAB_H = 38   -- fixed chrome height (text-size option only affects list/param body)

  local st   = readState()
  local asn  = readAssign()
  local ust  = readUf8State()
  local uasn = readUf8Assign()

  -- Param-list drawer: reserve a fixed right strip. The window itself grows by
  -- PARAM_PW when the drawer opens (loop()), so the mockup keeps its size.
  paramPanelOpen = (reaper.GetExtState(SECT, "hud_imgui_params") == "1")
  if paramPanelOpen then
    RW = PARAM_PW
  else
    RW, paramRects, selectedParam = 0, {}, -1
  end

  frame = frame + 1
  -- "idx" (legacy) or "idx;layer" — the layer is the modifier overlay the bind
  -- will land on (latched at arm time). Feeds the "Learning Ctrl+HPF" banner.
  local hl = reaper.GetExtState(SECT, "hud_learn")
  local hlIdx, hlLayer = hl:match("^(%-?%d+);(%d+)$")
  learnIdx     = tonumber(hlIdx) or tonumber(hl) or -1
  learnLayer   = tonumber(hlLayer) or 0
  local ul = reaper.GetExtState(SECT, "hud_uf8_learn")
  uf8Learn = (ul ~= "" and tonumber(ul)) or -1
  local hint = reaper.GetExtState(SECT, "hud_hint")
  if hint ~= "" then hintText = hint; hintFrames = 90
    reaper.SetExtState(SECT, "hud_hint", "", false) end

  -- Bootstrap: surface is pointed at an unlearned FX ("1;<name>") → the tabs are
  -- published empty + the LCD shows <name>; surface a "Map <name>" hint.
  local bootRaw    = reaper.GetExtState(SECT, "hud_boot")
  local bootActive = (bootRaw:sub(1, 2) == "1;")
  local bootName   = bootActive and bootRaw:sub(3) or ""

  -- Auto-follow focus to the matching tab. CS/BC win (focusDom c/b); otherwise a
  -- UF8-mapped plug-in under the cursor (ust.focus) switches to the UF8 tab.
  -- Manual tab clicks stick until the focus context next CHANGES (lastFocusDom
  -- doubles as the focus-key tracker: "c"/"b"/"u"/"n").
  local focusKey = st.focusDom
  if focusKey ~= "c" and focusKey ~= "b" and ust and ust.focus then focusKey = "u" end
  if focusKey ~= lastFocusDom then
    local newTab = (focusKey == "c") and "cs"
               or  (focusKey == "b") and "bc"
               or  (focusKey == "u") and "uf8" or nil
    if newTab and newTab ~= activeTab then
      selectedParam = -1   -- param idx is per-FX
      activeTab = newTab
    end
    lastFocusDom = focusKey
  end

  drawTabs(st, ust)

  -- Edit row above the body (all tabs): Name field + CS-Favourite dropdown
  -- (CS only). Sets FAV_H (body shifts down) + nameRect/csFavRect.
  csFavRect   = nil
  bcFavRect   = nil
  nameRect    = nil
  drawHudEditRow()

  -- Tell the extension when the UF8 tab is showing so it auto-engages UF8
  -- Plugin Mode (hardware Top-Soft-Keys drive V-Pot banks). Edge-write only.
  local onUf8 = (activeTab == "uf8")
  if lastUf8Tab ~= onUf8 then
    lastUf8Tab = onUf8
    reaper.SetExtState(SECT, "hud_uf8_tab", onUf8 and "1" or "0", false)
  end

  -- UF8 device tab: list view = strip-grid, mockup view = hardware face. Mirrors
  -- the CS/BC list/mockup toggle (shared hud_imgui_view). Independent of the UC1
  -- geometry.
  -- UF1 device tab (v11): one view only — the page grid mirrors the hardware,
  -- so there is no list/mockup split to make.
  if activeTab == "uf1" then
    renderUf1Tab(readUf1())
    return
  end

  if onUf8 then
    if reaper.GetExtState(SECT, "hud_imgui_view") == "mockup" then
      renderUf8Face(ust, uasn)
    else
      renderUf8Grid(ust, uasn)
    end
    if paramPanelOpen then renderParamPanel(st, uasn) end
    -- Status banner (mutually exclusive): assigning a picked param → touch-learn
    -- armed → touch-learn mode idle. Pinned to the BOTTOM of the window so it
    -- never covers the plug-in name / bank row at the top (Frank 2026-06-17).
    local function uf8Banner(msg, bgRgb, bgA, fgRgb)
      local px = floor(14 * fontScale() + 0.5)
      local tw, th = measure(msg, px)
      local bw, bh = tw + 20, th + 8
      local bx, by = (WW - RW - bw) / 2, WH - bh - 8
      rect(bx, by, bw, bh, col(bgRgb, bgA))
      dtext(bx + 10, by + 4, col(fgRgb, 1), msg, px)
    end
    if selectedParam >= 0 then
      uf8Banner("Assigning " .. selectedParamNm
        .. "  \xE2\x80\x94  click a UF8 control to bind it   (Esc to cancel)",
        0x10202C, 0.9, 0x70C0FF)
    elseif uf8Learn < 0 and reaper.GetExtState(SECT, "hud_touch_learn") == "1" then
      uf8Banner("Touch to Learn  \xE2\x80\x94  move a UF8 control to arm it, "
        .. "then wiggle a plug-in parameter", 0x101A14, 0.88, 0x70D0A0)
    elseif uf8Learn >= 0 then
      uf8Banner("Learning  \xE2\x80\x94  wiggle a plug-in parameter to bind it"
        .. "   (click the cell again or Esc to cancel)", 0x101418, 0.88, 0xFFD060)
    end
    return
  end

  local nCtrl = 0; for _ in pairs(geom.ctrl) do nCtrl = nCtrl + 1 end

  if not geom or nCtrl == 0 then
    dtext(10, bodyTop() + 12, col(0x808890, 0.9),
      "Waiting for surface geometry\xE2\x80\xA6", floor(15 * fontScale() + 0.5))
    return
  end

  local present = (activeTab == "cs") and st.csPresent or st.bcPresent

  if reaper.GetExtState(SECT, "hud_imgui_view") == "mockup" then
    -- Unified face draws the whole surface (both domains, inactive dimmed) —
    -- always shown; absent plug-ins just render greyed, no "no plug-in" gate.
    renderFace(st, asn)
  else
    -- Always render the list — even with no plug-in the rows draw as em-dash
    -- and stay click-armable, so an EMPTY tab can bootstrap a virgin plug-in:
    -- click a control → wiggle its param → the extension creates a new map.
    renderList(st, asn)
  end

  if paramPanelOpen then renderParamPanel(st, asn) end

  local function banner(msg, bgRgb, bgA, fgRgb)
    local px = floor(14 * fontScale() + 0.5)
    local tw, th = measure(msg, px)
    local bw, bh = tw + 20, th + 8
    local bx, by = (WW - RW - bw) / 2, bodyTop() + 4
    rect(bx, by, bw, bh, col(bgRgb, bgA))
    dtext(bx + 10, by + 4, col(fgRgb, 1), msg, px)
  end

  if selectedParam >= 0 then
    banner("Assigning " .. selectedParamNm ..
           "  \xE2\x80\x94  click a control to bind it   (Esc to cancel)",
           0x10202C, 0.9, 0x70C0FF)
  elseif learnIdx < 0 and reaper.GetExtState(SECT, "hud_touch_learn") == "1" then
    banner("Touch to Learn  \xE2\x80\x94  move a UC1 control to arm it, "
           .. "then wiggle a plug-in parameter", 0x101A14, 0.88, 0x70D0A0)
  elseif learnIdx >= 0 then
    local c   = geom.ctrl[learnIdx]
    local lbl = (c and c.label ~= "" and c.label) or ("#" .. learnIdx)
    -- Prefix the held modifier overlay so the bind target is unambiguous, e.g.
    -- "Learning Ctrl+HPF". learnLayer is latched at arm time (see hud_learn).
    local mod = (learnLayer == 1 and "Opt+") or (learnLayer == 2 and "Ctrl+")
             or (learnLayer == 3 and "Ctrl+Opt+") or ""
    banner("Learning " .. mod .. lbl ..
           "  \xE2\x80\x94  wiggle a plug-in parameter to bind it"
           .. "   (click again or Esc to cancel)",
           0x101418, 0.88, 0xFFD060)
  elseif hintFrames > 0 then
    hintFrames = hintFrames - 1
    banner(hintText, 0x301014, 0.92, 0xFF8888)
  elseif bootActive then
    banner("Map " .. bootName .. "  \xE2\x80\x94  click a control, then wiggle "
           .. "its parameter (binds to " .. (activeTab == "cs" and "Channel Strip"
           or "Bus Comp") .. ")", 0x10201A, 0.90, 0x84E0A8)
  elseif not present then
    banner((activeTab == "cs" and "No Channel Strip" or "No Bus Comp")
           .. " plug-in  \xE2\x80\x94  click a control, then wiggle a parameter "
           .. "to create a map", 0x101A20, 0.88, 0x80B8E0)
  end
end

------------------------------------------------------------------------
-- Right-click menu (ImGui popup). Dock is handled by ReaImGui's built-in
-- title-bar context menu, so it's no longer in here.
------------------------------------------------------------------------
local POPUP          = "##hud_ctx"
local CTRL_POPUP     = "##hud_ctrl_ctx"
local UF8_CTRL_POPUP = "##hud_uf8_ctrl_ctx"
local UF1_CTRL_POPUP = "##hud_uf1_ctrl_ctx"
local ctxUf1Sk, ctxUf1Pos = -1, -1
local ctxUf1Layer = 0     -- modifier layer captured AT right-click; only the
                          -- "Send to UC1" target uses it (the UF1 map itself is
                          -- layer-free)
local uf1NameBuf, uf1NameFor = nil, nil   -- inline rename field, seeded per cell
local UF8_BANK_POPUP = "##hud_uf8_bank_ctx"
local UF8_STRIPCOL_POPUP      = "##hud_uf8_stripcol"       -- left-click: this strip
local UF8_STRIPCOL_FILL_POPUP = "##hud_uf8_stripcol_fill"  -- right-click: fill all
local CS_FAV_POPUP   = "##hud_cs_fav_ctx"
local BC_FAV_POPUP   = "##hud_bc_fav_ctx"
local CURVE_POPUP    = "Curve editor###hud_curve_editor"

-- Full-parity control-menu working state (Frank 2026-06-20). `cedit` holds the
-- live slider values for the right-click menu — reloaded from hud_detail each
-- frame UNLESS a widget is active (so a drag holds its value). The curve editor
-- is a nested popup with its own draggable-point working copy.
local cedit       = { rmin = 0, rmax = 1, sens = 1, dn = 0.5 }
local curveOpen   = false
local curveNeedsOpen = false  -- defer ImGui_OpenPopup to top-level (a popup
                              -- opened from inside the closing menu is dropped)
local curveUf8    = false   -- false = UC1 (idx,layer); true = UF8 V-Pot (strip)
local curveUf1    = false   -- true = UF1 V-Pot (pos,sk); wins over curveUf8
local curveIdx    = -1
local curveLayer  = 0
local curveStrip  = -1
local curveUf1Pos = -1      -- UF1 flat position (page*4 + idx)
local curveUf1Sk  = 0       -- UF1 stream: 0 V-Pot, 1 soft-key (tuning = V-Pot only)
local curvePts    = {}      -- working copy {{t,v},…} (absolute param-space v)
local curveDrag   = -1
local curveSens   = 1.0
-- Feel-preset rename prompt: which slot is being saved (-1 = none). The name is
-- gathered via a native GetUserInputs (works from a defer frame).
local feelTarget  = -1

-- List / parameter-panel body text size (Medium = current default). Chrome
-- (titles, badge, buttons) is unaffected.
local FONT_PRESETS = {
  { l = "XS",     v = 0.75 },
  { l = "S",      v = 0.88 },
  { l = "Medium", v = 1.0  },
  { l = "L",      v = 1.2  },
  { l = "XL",     v = 1.45 },
}

-- Standalone CS-Favourite slot picker, opened by clicking the CS-Favourite
-- dropdown row. Same data + command as the right-click "CS Favourite" submenu
-- (readCsFav / csfav;<slot>) — just surfaced as an always-visible dropdown so
-- the active slot is ersichtlich. Frank 2026-06-24.
local function drawCsFavPopup()
  -- Drop the popup just under the dropdown field (else it anchors at the window
  -- origin and covers the mockup — Frank 2026-06-24).
  if csFavRect then
    reaper.ImGui_SetNextWindowPos(ctx, OX + csFavRect.x,
                                  OY + csFavRect.y + csFavRect.h + 2)
  end
  if not reaper.ImGui_BeginPopup(ctx, CS_FAV_POPUP) then return end
  local cur, _, slots = readCsFav()
  if reaper.ImGui_MenuItem(ctx, "None", nil, cur < 0) then
    sendCmd("csfav;-1")
  end
  reaper.ImGui_Separator(ctx)
  for i = 0, 7 do
    local s = slots[i] or { used = false, label = "" }
    local lbl = s.used
      and string.format("%d  \xE2\x80\x94  %s", i + 1, s.label)
      or  string.format("%d  \xE2\x80\x94  (empty)", i + 1)
    if reaper.ImGui_MenuItem(ctx, lbl .. "##csfavpop" .. i, nil, cur == i) then
      sendCmd("csfav;" .. i)
    end
  end
  reaper.ImGui_EndPopup(ctx)
end

-- BC-Favourite slot picker — mirror of drawCsFavPopup (readBcFav / bcfav;<slot>).
local function drawBcFavPopup()
  if bcFavRect then
    reaper.ImGui_SetNextWindowPos(ctx, OX + bcFavRect.x,
                                  OY + bcFavRect.y + bcFavRect.h + 2)
  end
  if not reaper.ImGui_BeginPopup(ctx, BC_FAV_POPUP) then return end
  local cur, _, slots = readBcFav()
  if reaper.ImGui_MenuItem(ctx, "None", nil, cur < 0) then
    sendCmd("bcfav;-1")
  end
  reaper.ImGui_Separator(ctx)
  for i = 0, 7 do
    local s = slots[i] or { used = false, label = "" }
    local lbl = s.used
      and string.format("%d  \xE2\x80\x94  %s", i + 1, s.label)
      or  string.format("%d  \xE2\x80\x94  (empty)", i + 1)
    if reaper.ImGui_MenuItem(ctx, lbl .. "##bcfavpop" .. i, nil, cur == i) then
      sendCmd("bcfav;" .. i)
    end
  end
  reaper.ImGui_EndPopup(ctx)
end

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
  if reaper.ImGui_MenuItem(ctx, "Parameter List", nil, pPanel) then
    reaper.SetExtState(SECT, "hud_imgui_params", pPanel and "0" or "1", true)
  end

  -- Touch-to-Learn: move a UC1 control to arm it (instead of clicking the
  -- mockup). Session-only (persist=false) so it never leaves the surface inert
  -- across restarts; also cleared on HUD shutdown.
  local touchLearn = (reaper.GetExtState(SECT, "hud_touch_learn") == "1")
  if reaper.ImGui_MenuItem(ctx, "Touch to Learn", nil, touchLearn) then
    reaper.SetExtState(SECT, "hud_touch_learn", touchLearn and "0" or "1", false)
  end

  if reaper.ImGui_BeginMenu(ctx, "Text size (list)") then
    local fs = fontScale()
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

  -- CS-Switch favourites — assign the focused track's active Channel Strip to
  -- one of the 8 favourite slots the "Switch to CS N" / "CS Cycle" actions step
  -- through. Greyed out when no CS is focused; one plug-in occupies one slot, so
  -- picking a used slot rewrites it to the active CS. The plug-in name shows
  -- next to each taken number. Frank 2026-06-20.
  -- Hidden on the UF8 tab — it's a Channel-Strip concept, out of place in the
  -- UF8 V-Pot context (Frank 2026-06-23).
  if activeTab ~= "uf8" then
    local cur, hasCs, slots = readCsFav()
    if reaper.ImGui_BeginMenu(ctx, "CS Favourite", hasCs) then
      if reaper.ImGui_MenuItem(ctx, "None", nil, cur < 0) then
        sendCmd("csfav;-1")
      end
      reaper.ImGui_Separator(ctx)
      for i = 0, 7 do
        local s = slots[i] or { used = false, label = "" }
        local lbl = s.used
          and string.format("%d  \xE2\x80\x94  %s", i + 1, s.label)
          or  string.format("%d  \xE2\x80\x94  (empty)", i + 1)
        if reaper.ImGui_MenuItem(ctx, lbl .. "##csfav" .. i, nil, cur == i) then
          sendCmd("csfav;" .. i)
        end
      end
      reaper.ImGui_EndMenu(ctx)
    end
  end

  reaper.ImGui_Separator(ctx)
  if reaper.ImGui_MenuItem(ctx, "Close HUD") then
    reaper.SetExtState(SECT, RUNKEY, "0", false)
  end
  reaper.ImGui_EndPopup(ctx)
  reaper.ImGui_PopStyleVar(ctx, 3)   -- matches the 3 pushes before BeginPopup
end

-- Per-control context menu (right-click a control): Learn / Invert / Unbind.
-- Acts on ctxCtrlIdx (captured at right-click) via the hud_cmd channel; the
-- extension resolves the active modifier layer + domain target. Invert/Unbind
-- are disabled on an unmapped control (nothing to act on). State (mapped/inv) is
-- read fresh from hud_assign so the tick + name reflect the current binding.
-- Send one "field;idx;layer;field;value" tuning command for the right-clicked
-- control (full-parity menu — fields match hudSetField_ in SettingsScreen.cpp).
local function sendField(field, value)
  sendCmd(string.format("field;%d;%d;%d;%.6f",
                        ctxCtrlIdx, ctxCtrlLayer, field, value))
end

-- Push-cycle editor (button right-click → "Push cycle"). Reorder via ▲▼ (the
-- FX-Learn page uses drag-drop, impractical in a Lua menu), untick to exclude,
-- x removes a macro step, "+ Add step" picks a discrete param + value. Data
-- comes via the request/response readPush() channel.
-- Selectable flag "don't auto-close the popup on click" — renamed across ImGui
-- versions (DontClosePopups → NoAutoClosePopups in ImGui 1.90+). Resolve once
-- with a fallback so we never call a nil ReaImGui function (Frank's runtime
-- error 2026-06-23). 0 if neither exists (degrades to the old close-on-pick).
local SEL_NOCLOSE =
     (reaper.ImGui_SelectableFlags_NoAutoClosePopups
      and reaper.ImGui_SelectableFlags_NoAutoClosePopups())
  or (reaper.ImGui_SelectableFlags_DontClosePopups
      and reaper.ImGui_SelectableFlags_DontClosePopups())
  or 0

local function drawPushCycle(idx)
  local pd = readPush()
  if not pd or pd.idx ~= idx then
    reaper.ImGui_TextDisabled(ctx, "Loading\xE2\x80\xA6")
    return
  end
  if #pd.steps == 0 and #pd.params == 0 then
    reaper.ImGui_TextDisabled(ctx, "No discrete options on this plug-in")
    return
  end
  if #pd.steps > 0 then
    reaper.ImGui_TextDisabled(ctx, "Steps \xC2\xB7 untick to exclude:")
    for i, st in ipairs(pd.steps) do
      if reaper.ImGui_SmallButton(ctx, "\xE2\x96\xB2##pu_up" .. i) then
        sendCmd(string.format("pushmove;%d;%d;%d;-1", idx, ctxCtrlLayer, i - 1))
      end
      reaper.ImGui_SameLine(ctx)
      if reaper.ImGui_SmallButton(ctx, "\xE2\x96\xBC##pu_dn" .. i) then
        sendCmd(string.format("pushmove;%d;%d;%d;1", idx, ctxCtrlLayer, i - 1))
      end
      reaper.ImGui_SameLine(ctx)
      local rv = reaper.ImGui_Checkbox(ctx, st.label .. "##pu_cb" .. i, st.en)
      if rv then
        sendCmd(string.format("pushtoggle;%d;%d;%d", idx, ctxCtrlLayer, i - 1))
      end
      if st.foreign then
        reaper.ImGui_SameLine(ctx)
        if reaper.ImGui_SmallButton(ctx, "x##pu_rm" .. i) then
          sendCmd(string.format("pushremove;%d;%d;%d", idx, ctxCtrlLayer, i - 1))
        end
      end
    end
    if reaper.ImGui_SmallButton(ctx, "Reset (cycle all)##pu_reset") then
      sendCmd(string.format("pushreset;%d;%d", idx, ctxCtrlLayer))
    end
  end
  if #pd.params > 0 then
    reaper.ImGui_Separator(ctx)
    if reaper.ImGui_BeginMenu(ctx, "+ Add step") then
      for _, p in ipairs(pd.params) do
        if reaper.ImGui_BeginMenu(ctx, p.name .. "##pu_p" .. p.param) then
          for oi, o in ipairs(p.opts) do
            -- DontClosePopups so adding several steps in a row keeps the
            -- submenu open instead of collapsing the whole chain each pick
            -- (Frank 2026-06-23).
            if reaper.ImGui_Selectable(ctx,
                o.label .. "##pu_o" .. p.param .. "_" .. oi, false, SEL_NOCLOSE) then
              sendCmd(string.format("pushadd;%d;%d;%d;%.6f",
                idx, ctxCtrlLayer, p.param, o.norm))
            end
          end
          reaper.ImGui_EndMenu(ctx)
        end
      end
      reaper.ImGui_EndMenu(ctx)
    end
  end
end

-- UF8 V-Pot rotary step-cycle editor (V-Pot right-click → "Step cycle"). Same
-- shape as drawPushCycle but strip-keyed and over the uf8push* commands /
-- readUf8Push() channel. Turning the V-Pot scrubs the list (clamps at the ends).
local function drawPushCycleUf8(strip)
  local pd = readUf8Push()
  if not pd or pd.strip ~= strip then
    reaper.ImGui_TextDisabled(ctx, "Loading\xE2\x80\xA6")
    return
  end
  if #pd.steps == 0 and #pd.params == 0 then
    reaper.ImGui_TextDisabled(ctx, "No discrete options on this plug-in")
    return
  end
  if #pd.steps > 0 then
    reaper.ImGui_TextDisabled(ctx, "Steps \xC2\xB7 untick to exclude:")
    for i, st in ipairs(pd.steps) do
      if reaper.ImGui_SmallButton(ctx, "\xE2\x96\xB2##puu_up" .. i) then
        sendCmd(string.format("uf8pushmove;%d;%d;-1", strip, i - 1))
      end
      reaper.ImGui_SameLine(ctx)
      if reaper.ImGui_SmallButton(ctx, "\xE2\x96\xBC##puu_dn" .. i) then
        sendCmd(string.format("uf8pushmove;%d;%d;1", strip, i - 1))
      end
      reaper.ImGui_SameLine(ctx)
      local rv = reaper.ImGui_Checkbox(ctx, st.label .. "##puu_cb" .. i, st.en)
      if rv then
        sendCmd(string.format("uf8pushtoggle;%d;%d", strip, i - 1))
      end
      if st.foreign then
        reaper.ImGui_SameLine(ctx)
        if reaper.ImGui_SmallButton(ctx, "x##puu_rm" .. i) then
          sendCmd(string.format("uf8pushremove;%d;%d", strip, i - 1))
        end
      end
    end
    if reaper.ImGui_SmallButton(ctx, "Clear steps##puu_reset") then
      sendCmd(string.format("uf8pushreset;%d", strip))
    end
  end
  if #pd.params > 0 then
    reaper.ImGui_Separator(ctx)
    if reaper.ImGui_BeginMenu(ctx, "+ Add step") then
      for _, p in ipairs(pd.params) do
        if reaper.ImGui_BeginMenu(ctx, p.name .. "##puu_p" .. p.param) then
          for oi, o in ipairs(p.opts) do
            -- DontClosePopups — keep the submenu open across multiple adds.
            if reaper.ImGui_Selectable(ctx,
                o.label .. "##puu_o" .. p.param .. "_" .. oi, false, SEL_NOCLOSE) then
              sendCmd(string.format("uf8pushadd;%d;%d;%.6f",
                strip, p.param, o.norm))
            end
          end
          reaper.ImGui_EndMenu(ctx)
        end
      end
      reaper.ImGui_EndMenu(ctx)
    end
  end
end

-- UF1 soft-key push cycle — same editor again, position-keyed over the
-- uf1push* commands / readUf1Push() channel. Pressing the key steps to the next
-- enabled entry; an empty list is the plain on/off toggle, so "Clear steps"
-- means "back to a normal toggle" here (there is no cycle-all default).
local function drawPushCycleUf1(pos)
  local pd = readUf1Push()
  if not pd or pd.pos ~= pos then
    reaper.ImGui_TextDisabled(ctx, "Loading\xE2\x80\xA6")
    return
  end
  if #pd.steps == 0 and #pd.params == 0 then
    reaper.ImGui_TextDisabled(ctx, "No discrete options on this plug-in")
    return
  end
  if #pd.steps > 0 then
    reaper.ImGui_TextDisabled(ctx, "Steps \xC2\xB7 untick to exclude:")
    for i, st in ipairs(pd.steps) do
      if reaper.ImGui_SmallButton(ctx, "\xE2\x96\xB2##pu1_up" .. i) then
        sendCmd(string.format("uf1pushmove;%d;%d;-1", pos, i - 1))
      end
      reaper.ImGui_SameLine(ctx)
      if reaper.ImGui_SmallButton(ctx, "\xE2\x96\xBC##pu1_dn" .. i) then
        sendCmd(string.format("uf1pushmove;%d;%d;1", pos, i - 1))
      end
      reaper.ImGui_SameLine(ctx)
      local rv = reaper.ImGui_Checkbox(ctx, st.label .. "##pu1_cb" .. i, st.en)
      if rv then
        sendCmd(string.format("uf1pushtoggle;%d;%d", pos, i - 1))
      end
      if st.foreign then
        reaper.ImGui_SameLine(ctx)
        if reaper.ImGui_SmallButton(ctx, "x##pu1_rm" .. i) then
          sendCmd(string.format("uf1pushremove;%d;%d", pos, i - 1))
        end
      end
    end
    if reaper.ImGui_SmallButton(ctx, "Clear steps (plain toggle)##pu1_reset") then
      sendCmd(string.format("uf1pushreset;%d", pos))
    end
  end
  if #pd.params > 0 then
    reaper.ImGui_Separator(ctx)
    if reaper.ImGui_BeginMenu(ctx, "+ Add step") then
      for _, p in ipairs(pd.params) do
        if reaper.ImGui_BeginMenu(ctx, p.name .. "##pu1_p" .. p.param) then
          for oi, o in ipairs(p.opts) do
            if reaper.ImGui_Selectable(ctx,
                o.label .. "##pu1_o" .. p.param .. "_" .. oi, false, SEL_NOCLOSE) then
              sendCmd(string.format("uf1pushadd;%d;%d;%.6f", pos, p.param, o.norm))
            end
          end
          reaper.ImGui_EndMenu(ctx)
        end
      end
      reaper.ImGui_EndMenu(ctx)
    end
  end
end

local function drawControlContextMenu()
  if ctxCtrlIdx < 0 then
    reaper.SetExtState(SECT, "hud_push_req", "", false)
    reaper.SetExtState(SECT, "hud_ctx_layer", "-1", false)
    return
  end
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_WindowPadding(), 10, 8)
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_ItemSpacing(), 10, 7)
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_FramePadding(), 7, 4)
  if not reaper.ImGui_BeginPopup(ctx, CTRL_POPUP) then
    reaper.SetExtState(SECT, "hud_push_req", "", false)
    reaper.SetExtState(SECT, "hud_ctx_layer", "-1", false)
    reaper.ImGui_PopStyleVar(ctx, 3)
    return
  end
  -- Freeze the modifier layer while the menu is open: publish the layer that
  -- was active at right-click (ctxCtrlLayer) so the extension renders + edits
  -- THIS layer's data even after the user releases the modifier to navigate the
  -- menu (Frank 2026-06-23: nobody wants to hold the modifier while adjusting).
  reaper.SetExtState(SECT, "hud_ctx_layer", tostring(ctxCtrlLayer), false)

  local idx, layer = ctxCtrlIdx, ctxCtrlLayer
  local asn    = readAssign()
  local a      = asn[idx]
  local mapped = a ~= nil
  local det    = readDetail()[idx]   -- present => editable knob, mapped on layer
  local g      = geom.ctrl[idx]

  -- Header: SSL slot label + bound param name (disabled, informational).
  reaper.ImGui_BeginDisabled(ctx)
  local hdr = (g and g.label) or ("Control " .. idx)
  if mapped then hdr = hdr .. "  \xE2\x86\x92  " .. a.name end
  reaper.ImGui_MenuItem(ctx, hdr)
  reaper.ImGui_EndDisabled(ctx)
  reaper.ImGui_Separator(ctx)

  if reaper.ImGui_MenuItem(ctx, "Learn (wiggle a parameter)") then
    sendCmd("learn;" .. idx)
  end

  if not mapped then reaper.ImGui_BeginDisabled(ctx) end
  if reaper.ImGui_MenuItem(ctx, "Invert", nil, mapped and a.inv or false) then
    sendCmd("invert;" .. idx .. ";" .. layer)
  end

  -- Knob-only tuning block — only when the control is an editable knob mapped
  -- on this layer (det present). Mirrors the FX-Learn page right-click menu.
  if det then
    reaper.ImGui_Separator(ctx)
    if reaper.ImGui_MenuItem(ctx, "Polarity: Unipolar (0 \xE2\x86\x92 1)",
                             nil, det.pol == 0) then
      sendField(0, 0)
    end
    if reaper.ImGui_MenuItem(ctx, "Polarity: Bipolar (\xE2\x88\x92 0.5 +)",
                             nil, det.pol == 1) then
      sendField(0, 1)
    end

    -- Knob travel (Min / Max). Held value while a slider is active so a drag
    -- isn't yanked back by the every-frame hud_detail readback.
    reaper.ImGui_Separator(ctx)
    reaper.ImGui_Text(ctx, "Knob travel:")
    if not reaper.ImGui_IsAnyItemActive(ctx) then
      cedit.rmin, cedit.rmax, cedit.dn = det.rmin, det.rmax, det.dn
    end
    reaper.ImGui_SetNextItemWidth(ctx, 150)
    local ch, v = reaper.ImGui_SliderDouble(ctx, "Min##fxlmin",
                                            cedit.rmin, 0, 1, "%.3f")
    if ch then cedit.rmin = v; sendField(1, v) end
    reaper.ImGui_SameLine(ctx)
    if reaper.ImGui_SmallButton(ctx, "Set##capmin") then sendField(5, 0) end
    reaper.ImGui_SetNextItemWidth(ctx, 150)
    ch, v = reaper.ImGui_SliderDouble(ctx, "Max##fxlmax",
                                      cedit.rmax, 0, 1, "%.3f")
    if ch then cedit.rmax = v; sendField(2, v) end
    reaper.ImGui_SameLine(ctx)
    if reaper.ImGui_SmallButton(ctx, "Set##capmax") then sendField(6, 0) end
    if reaper.ImGui_SmallButton(ctx, "Reset range##rr") then sendField(8, 0) end
    if det.stepped then
      reaper.ImGui_SameLine(ctx)
      reaper.ImGui_TextDisabled(ctx,
        string.format("stepped \xE2\x80\xA2 %d values", det.nsteps))
    end

    -- Push reset value (UF8 V-Pot mirror).
    reaper.ImGui_Separator(ctx)
    reaper.ImGui_Text(ctx, "Push reset value (UF8 mirror):")
    reaper.ImGui_SetNextItemWidth(ctx, 150)
    ch, v = reaper.ImGui_SliderDouble(ctx, "##fxldn", cedit.dn, 0, 1, "%.3f")
    if ch then cedit.dn = v; sendField(4, v) end
    reaper.ImGui_SameLine(ctx)
    if reaper.ImGui_SmallButton(ctx, "Capture##capdn") then sendField(7, 0) end
    if det.pol == 1 then
      reaper.ImGui_SameLine(ctx)
      if reaper.ImGui_SmallButton(ctx, "0.5##dncentre") then sendField(4, 0.5) end
    end

    -- Advanced — curve editor + sensitivity (nested popup, full canvas).
    reaper.ImGui_Separator(ctx)
    if reaper.ImGui_MenuItem(ctx, "Advanced\xE2\x80\xA6 (curve + sensitivity)") then
      curveOpen, curveNeedsOpen, curveUf8, curveUf1 = true, true, false, false
      curveIdx, curveLayer = idx, layer
      curveSens, curveDrag = det.sens, -1
      curvePts = {}
      for _, p in ipairs(det.curve) do curvePts[#curvePts + 1] = { p[1], p[2] } end
    end
  end

  -- Display label — native rename (any mapped control).
  reaper.ImGui_Separator(ctx)
  if reaper.ImGui_MenuItem(ctx, "Rename\xE2\x80\xA6 (display label)") then
    local cur = ((mapped and a.name) or ""):gsub(",", " ")
    local ok, val = reaper.GetUserInputs("Rename control", 1,
      "Display name (empty = default):,extrawidth=180", cur)
    if ok then
      sendCmd("rename;" .. idx .. ";" .. layer .. ";"
              .. (val:gsub("[;\n]", " ")))
    end
  end

  -- Feel presets — save / apply / clear the tuning bundle (knobs only).
  if det then
    reaper.ImGui_Separator(ctx)
    if reaper.ImGui_BeginMenu(ctx, "Feel presets") then
      local feel = readFeel()
      if reaper.ImGui_BeginMenu(ctx, "Save feel to") then
        for s = 0, 9 do
          local fp  = feel[s]
          local lbl = string.format("Slot %d%s", s + 1,
            (fp and fp.used) and ("  \xE2\x80\xA2  " .. fp.name) or "  (empty)")
          if reaper.ImGui_MenuItem(ctx, lbl .. "##fsave" .. s) then
            local pre = ((fp and fp.used and fp.name) or ""):gsub(",", " ")
            local ok, val = reaper.GetUserInputs("Save feel preset", 1,
              "Name:,extrawidth=160", pre)
            if ok then
              sendCmd(string.format("feelsave;%d;%d;%d;%s",
                idx, layer, s, (val:gsub("[;\n]", " "))))
            end
          end
        end
        reaper.ImGui_EndMenu(ctx)
      end
      if reaper.ImGui_BeginMenu(ctx, "Apply feel from") then
        local any = false
        for s = 0, 9 do
          local fp = feel[s]
          if fp and fp.used then
            any = true
            if reaper.ImGui_MenuItem(ctx,
                string.format("Slot %d: %s##fapp%d", s + 1, fp.name, s)) then
              sendCmd(string.format("feelapply;%d;%d;%d", idx, layer, s))
            end
          end
        end
        if not any then
          reaper.ImGui_BeginDisabled(ctx)
          reaper.ImGui_MenuItem(ctx, "(no saved presets)")
          reaper.ImGui_EndDisabled(ctx)
        end
        reaper.ImGui_EndMenu(ctx)
      end
      if reaper.ImGui_BeginMenu(ctx, "Clear preset") then
        local any = false
        for s = 0, 9 do
          local fp = feel[s]
          if fp and fp.used then
            any = true
            if reaper.ImGui_MenuItem(ctx,
                string.format("Slot %d: %s##fclr%d", s + 1, fp.name, s)) then
              sendCmd("feelclear;" .. s)
            end
          end
        end
        if not any then
          reaper.ImGui_BeginDisabled(ctx)
          reaper.ImGui_MenuItem(ctx, "(none)")
          reaper.ImGui_EndDisabled(ctx)
        end
        reaper.ImGui_EndMenu(ctx)
      end
      if reaper.ImGui_MenuItem(ctx, "Reset feel to default") then
        sendField(9, 0)
      end
      if reaper.ImGui_MenuItem(ctx, "Apply this feel to all mappings") then
        sendField(10, 0)
      end
      reaper.ImGui_EndMenu(ctx)
    end
  end

  reaper.ImGui_Separator(ctx)
  if reaper.ImGui_MenuItem(ctx, "Unbind") then
    sendCmd("unbind;" .. idx .. ";" .. layer)
  end
  if not mapped then reaper.ImGui_EndDisabled(ctx) end

  -- Push-cycle editor (discrete buttons) — outside the unmapped-disabled guard
  -- so a macro can be built on an as-yet-unmapped button. Writes hud_push_req
  -- only while the submenu is open so the extension publishes just this button.
  local pushReq = ""
  if pushBtnSet[idx] then
    reaper.ImGui_Separator(ctx)
    if reaper.ImGui_BeginMenu(ctx, "Push cycle") then
      pushReq = tostring(idx)
      drawPushCycle(idx)
      reaper.ImGui_EndMenu(ctx)
    end
  end
  reaper.SetExtState(SECT, "hud_push_req", pushReq, false)

  reaper.ImGui_EndPopup(ctx)
  reaper.ImGui_PopStyleVar(ctx, 3)
end

-- Curve editor — nested popup opened from the control menu's "Advanced…".
-- Replicates the FX-Learn page canvas: sensitivity box, draggable breakpoints
-- (left-click empty = add, drag = move, right-click = remove), Linear/Log/Exp/
-- Reset presets. Points are absolute param-space v, displayed normalised within
-- [rangeMin..rangeMax]. curvePts is the authoritative working copy (loaded on
-- open); each mutation pushes a "curve;…" command. Frank 2026-06-20.
local function curveCsv()
  local t = {}
  for _, p in ipairs(curvePts) do
    t[#t + 1] = string.format("%.4f:%.4f", p[1], p[2])
  end
  return table.concat(t, ",")
end
-- Source-agnostic command emitters: route to the UC1 ("curve"/"field"), the UF8
-- V-Pot ("uf8curve"/"uf8field") or the UF1 V-Pot ("uf1curve"/"uf1field")
-- channel depending on what opened the editor. The UF1 carries no layer — its
-- map is layer-free — so its verbs take (pos, sk) instead.
local function sendCurve()
  if curveUf1 then
    sendCmd(string.format("uf1curve;%d;%d;%s", curveUf1Pos, curveUf1Sk, curveCsv()))
  elseif curveUf8 then
    sendCmd(string.format("uf8curve;%d;%s", curveStrip, curveCsv()))
  else
    sendCmd(string.format("curve;%d;%d;%s", curveIdx, curveLayer, curveCsv()))
  end
end
local function sendCurveSens(v)
  if curveUf1 then
    sendCmd(string.format("uf1field;%d;%d;3;%.6f", curveUf1Pos, curveUf1Sk, v))
  elseif curveUf8 then
    sendCmd(string.format("uf8field;%d;3;%.6f", curveStrip, v))
  else
    sendCmd(string.format("field;%d;%d;3;%.6f", curveIdx, curveLayer, v))
  end
end

local function drawCurveEditor()
  if not curveOpen then return end
  -- Open here (top-level), not from the menu item — a popup opened while the
  -- triggering menu popup is closing gets dropped by ImGui.
  if curveNeedsOpen then
    reaper.ImGui_OpenPopup(ctx, CURVE_POPUP)
    curveNeedsOpen = false
  end
  -- Centre on the Learn-HUD window (OX/OY = content origin, WW/WH = content
  -- size, set each frame). Pivot 0.5,0.5 so the popup centre lands there.
  reaper.ImGui_SetNextWindowPos(ctx,
    (OX or 0) + (WW or 420) / 2, (OY or 0) + (WH or 380) / 2,
    reaper.ImGui_Cond_Appearing(), 0.5, 0.5)
  reaper.ImGui_SetNextWindowSize(ctx, 440, 400, reaper.ImGui_Cond_Appearing())
  -- Roomier padding so the content isn't flush against the frame. The parent
  -- HUD window pushes WindowPadding 0,0 (it draws its own margins), which this
  -- nested popup would otherwise inherit → everything glued to the edge. Push
  -- the same 10,8 the right-click context menus use. Read at BeginPopup, so
  -- pop right after regardless of the result. Frank 2026-07-09.
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_WindowPadding(), 10, 8)
  local curveShown = reaper.ImGui_BeginPopup(ctx, CURVE_POPUP)
  reaper.ImGui_PopStyleVar(ctx, 1)
  if not curveShown then
    -- Still waiting for the deferred open to take effect → keep curveOpen.
    if not curveNeedsOpen then curveOpen = false end
    return
  end
  local det
  if curveUf1     then det = readUf1Detail()[curveUf1Pos]
  elseif curveUf8 then det = readUf8Detail()[curveStrip]
  else                 det = readDetail()[curveIdx] end
  if not det then
    curveOpen = false
    reaper.ImGui_CloseCurrentPopup(ctx)
    reaper.ImGui_EndPopup(ctx)
    return
  end

  -- Sensitivity (held while the input is active, like the menu sliders).
  if not reaper.ImGui_IsAnyItemActive(ctx) then curveSens = det.sens end
  reaper.ImGui_Text(ctx, det.stepped and "Detent speed:" or "Sensitivity:")
  reaper.ImGui_SameLine(ctx)
  reaper.ImGui_SetNextItemWidth(ctx, 90)
  local ch, v = reaper.ImGui_InputDouble(ctx, "##csens", curveSens, 0, 0, "%.2fx")
  if ch then
    v = clamp(v, 0.01, 4.0)
    curveSens = v
    sendCurveSens(v)
  end
  reaper.ImGui_SameLine(ctx)
  if reaper.ImGui_SmallButton(ctx, "1x##csensr") then
    curveSens = 1.0
    sendCurveSens(1.0)
  end
  reaper.ImGui_Separator(ctx)

  if det.stepped then
    reaper.ImGui_TextWrapped(ctx, string.format(
      "Stepped parameter \xE2\x80\x94 %d values. Curve disabled; "
      .. "Min/Max snap to the step grid.", det.nsteps))
    reaper.ImGui_Separator(ctx)
    if reaper.ImGui_Button(ctx, "Close##ccls") then
      curveOpen = false
      reaper.ImGui_CloseCurrentPopup(ctx)
    end
    reaper.ImGui_EndPopup(ctx)
    return
  end

  local rmin, rmax = det.rmin, det.rmax
  local span   = rmax - rmin
  local spanOk = span > 1e-6
  local function normToV(n) if not spanOk then return rmin end return rmin + n * span end
  local function vToNorm(vv) if not spanOk then return 0.5 end return (vv - rmin) / span end
  reaper.ImGui_Text(ctx, "Knob travel curve:")

  local W, H = 340, 180
  local cx, cy = reaper.ImGui_GetCursorScreenPos(ctx)
  reaper.ImGui_InvisibleButton(ctx, "##ccanvas", W, H)
  local hovered = reaper.ImGui_IsItemHovered(ctx)
  local dl = reaper.ImGui_GetWindowDrawList(ctx)
  local function toC(t, vv) return cx + t * W, cy + (1 - vToNorm(vv)) * H end

  -- Backing + linear reference diagonal.
  reaper.ImGui_DrawList_AddRectFilled(dl, cx, cy, cx + W, cy + H, 0x1A1A1AFF, 4)
  reaper.ImGui_DrawList_AddLine(dl, cx, cy + H, cx + W, cy, 0xFFFFFF44, 1)

  -- Curve polyline (implicit endpoints at rangeMin / rangeMax).
  local poly = { { 0, rmin } }
  for _, p in ipairs(curvePts) do poly[#poly + 1] = p end
  poly[#poly + 1] = { 1, rmax }
  for i = 2, #poly do
    local ax, ay = toC(poly[i - 1][1], poly[i - 1][2])
    local bx, by = toC(poly[i][1], poly[i][2])
    reaper.ImGui_DrawList_AddLine(dl, ax, ay, bx, by, 0xFFC080FF, 2)
  end

  -- Points + hit-test.
  local mx, my = reaper.ImGui_GetMousePos(ctx)
  local R, nearest, best = 6, -1, 8
  for i, p in ipairs(curvePts) do
    local px, py = toC(p[1], p[2])
    reaper.ImGui_DrawList_AddCircleFilled(dl, px, py, R, 0xFFC080FF)
    reaper.ImGui_DrawList_AddCircle(dl, px, py, R, 0x202020FF, 0, 1)
    local d = math.sqrt((mx - px) ^ 2 + (my - py) ^ 2)
    if d < best then best = d; nearest = i end
  end
  local eax, eay = toC(0, rmin)
  local ebx, eby = toC(1, rmax)
  reaper.ImGui_DrawList_AddCircleFilled(dl, eax, eay, R, 0x6090FFFF)
  reaper.ImGui_DrawList_AddCircleFilled(dl, ebx, eby, R, 0x6090FFFF)

  -- Interaction.
  if hovered and reaper.ImGui_IsMouseClicked(ctx, 0) then
    if nearest > 0 then
      curveDrag = nearest
    else
      local t = clamp((mx - cx) / W, 0.01, 0.99)
      local n = clamp(1 - (my - cy) / H, 0, 1)
      curvePts[#curvePts + 1] = { t, normToV(n) }
      table.sort(curvePts, function(a, b) return a[1] < b[1] end)
      sendCurve()
    end
  end
  if hovered and reaper.ImGui_IsMouseClicked(ctx, 1) and nearest > 0 then
    table.remove(curvePts, nearest)
    sendCurve()
  end
  if reaper.ImGui_IsMouseReleased(ctx, 0) then curveDrag = -1 end
  if curveDrag > 0 and curveDrag <= #curvePts then
    local t = clamp((mx - cx) / W, 0.01, 0.99)
    local n = clamp(1 - (my - cy) / H, 0, 1)
    local vv = normToV(n)
    curvePts[curveDrag] = { t, vv }
    table.sort(curvePts, function(a, b) return a[1] < b[1] end)
    for i, p in ipairs(curvePts) do
      if p[1] == t and p[2] == vv then curveDrag = i; break end
    end
    sendCurve()
  end

  -- Presets.
  reaper.ImGui_Separator(ctx)
  local a, b = rmin, rmax
  local c = a + (b - a) * 0.5
  local bipolar = (det.pol == 1)
  if reaper.ImGui_SmallButton(ctx, "Linear##cpl") then
    curvePts = {}; sendCurve()
  end
  reaper.ImGui_SameLine(ctx)
  if reaper.ImGui_SmallButton(ctx, "Log##cplog") then
    if bipolar then
      curvePts = { {0.25, a + (b-a)*0.30}, {0.50, c}, {0.75, a + (b-a)*0.70} }
    else
      curvePts = { {0.25, a + (b-a)*0.55}, {0.50, a + (b-a)*0.80}, {0.75, a + (b-a)*0.93} }
    end
    sendCurve()
  end
  reaper.ImGui_SameLine(ctx)
  if reaper.ImGui_SmallButton(ctx, "Exp##cpexp") then
    if bipolar then
      curvePts = { {0.25, a + (b-a)*0.10}, {0.50, c}, {0.75, a + (b-a)*0.90} }
    else
      curvePts = { {0.25, a + (b-a)*0.07}, {0.50, a + (b-a)*0.20}, {0.75, a + (b-a)*0.45} }
    end
    sendCurve()
  end
  reaper.ImGui_SameLine(ctx)
  if reaper.ImGui_SmallButton(ctx, "Reset all##cprst") then
    curvePts = {}; sendCurve()
    sendCurveSens(1.0)
  end
  reaper.ImGui_TextDisabled(ctx,
    "Click empty: add point. Drag: move. Right-click: remove.")

  reaper.ImGui_Separator(ctx)
  if reaper.ImGui_Button(ctx, "Close##ccls2") then
    curveDrag = -1
    curveOpen = false
    reaper.ImGui_CloseCurrentPopup(ctx)
  end
  reaper.ImGui_EndPopup(ctx)
end

-- UF8 grid cell right-click menu: Learn / Invert / Fill sequential / Unbind.
-- Acts on (ctxUf8Kind, ctxUf8Strip) at the live banks (resolved C++-side) via
-- the hud_cmd channel. UF8 has no modifier layers and no rename (the grid shows
-- the plug-in's own param name, not a per-control label).
-- UF1 cell right-click menu: Learn / Invert / Rename / Unbind. Acts on
-- (ctxUf1Sk, ctxUf1Pos); the target plug-in is resolved C++-side from whatever
-- the UF1 is actually showing, so nothing about it rides in the command.
-- The UF1 map is layer-free, hence no layer argument anywhere here.
local function drawUf1ControlContextMenu()
  if ctxUf1Pos < 0 then return end
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_WindowPadding(), 10, 8)
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_ItemSpacing(), 10, 7)
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_FramePadding(), 7, 4)
  if not reaper.ImGui_BeginPopup(ctx, UF1_CTRL_POPUP) then
    reaper.ImGui_PopStyleVar(ctx, 3)
    return
  end
  local u1   = readUf1()
  local cell = u1 and u1.cells[ctxUf1Sk] and u1.cells[ctxUf1Sk][ctxUf1Pos]
  local arg  = ctxUf1Pos .. ";" .. ctxUf1Sk

  reaper.ImGui_BeginDisabled(ctx)
  local hdr = ((ctxUf1Sk == 1) and "Soft-key " or "V-Pot ") .. ((ctxUf1Pos % 4) + 1)
           .. "  (page " .. (math.floor(ctxUf1Pos / 4) + 1) .. ")"
  if cell then hdr = hdr .. "  \xE2\x86\x92  " .. (cell.label ~= "" and cell.label
                                                   or ("param " .. cell.param)) end
  reaper.ImGui_MenuItem(ctx, hdr)
  reaper.ImGui_EndDisabled(ctx)
  reaper.ImGui_Separator(ctx)

  if reaper.ImGui_MenuItem(ctx, "Learn\xE2\x80\xA6") then
    sendCmd("uf1learn;" .. arg)
  end
  if cell then
    if reaper.ImGui_MenuItem(ctx, "Invert", nil, cell.inv) then
      sendCmd("uf1invert;" .. arg)
    end

    -- Send to UC1 — the reverse of the UC1 menu's "Send to UF1". The UC1's
    -- slots are named and finite, so the user picks one; the list (and what
    -- each slot currently holds on this layer) is published by the extension
    -- while this submenu is open. The param is COPIED — it keeps its UF1 spot.
    local uc1Req = ""
    if reaper.ImGui_BeginMenu(ctx, "Send to UC1") then
      uc1Req = tostring(ctxUf1Layer)
      local sl = readUf1Uc1Slots()
      if not sl then
        reaper.ImGui_TextDisabled(ctx, "Loading\xE2\x80\xA6")
      elseif sl.noDomain then
        reaper.ImGui_TextDisabled(ctx, "This map has no UC1 domain")
        reaper.ImGui_TextDisabled(ctx, "(set one on the FX-Learn page)")
      else
        for _, s in ipairs(sl.slots) do
          local lbl = s.name
          if s.param >= 0 then
            lbl = lbl .. "  \xE2\x86\x92 " ..
                  ((s.bound ~= "") and s.bound or ("param " .. s.param))
          end
          if reaper.ImGui_MenuItem(ctx, lbl .. "##u1tuc" .. s.linkIdx) then
            sendCmd(string.format("uf1touc1;%d;%d;%d;%d",
              ctxUf1Pos, ctxUf1Sk, s.linkIdx, ctxUf1Layer))
          end
        end
      end
      reaper.ImGui_EndMenu(ctx)
    end
    reaper.SetExtState(SECT, "hud_uf1_uc1_req", uc1Req, false)

    -- Full tuning — V-Pot only (a soft-key is a press; same rule that keeps
    -- UF8 faders out of this menu). Parity with the FX-Learn UF1 cell menu:
    -- polarity / knob travel / push reset / curve + sensitivity / feel presets.
    -- [[feedback-fx-learn-changes-mirror-to-hud]]. Commands carry (pos, sk)
    -- only — no layer, and the plug-in is resolved C++-side.
    local det = (ctxUf1Sk == 0) and readUf1Detail()[ctxUf1Pos] or nil
    if det then
      local function uf1Field(f, val)
        sendCmd(string.format("uf1field;%d;%d;%d;%.6f",
          ctxUf1Pos, ctxUf1Sk, f, val))
      end
      reaper.ImGui_Separator(ctx)
      if reaper.ImGui_MenuItem(ctx, "Polarity: Unipolar (0 \xE2\x86\x92 1)",
                               nil, det.pol == 0) then uf1Field(0, 0) end
      if reaper.ImGui_MenuItem(ctx, "Polarity: Bipolar (\xE2\x88\x92 0.5 +)",
                               nil, det.pol == 1) then uf1Field(0, 1) end

      reaper.ImGui_Separator(ctx)
      reaper.ImGui_Text(ctx, "Knob travel:")
      if not reaper.ImGui_IsAnyItemActive(ctx) then
        cedit.rmin, cedit.rmax, cedit.dn = det.rmin, det.rmax, det.dn
      end
      reaper.ImGui_SetNextItemWidth(ctx, 150)
      local ch, v = reaper.ImGui_SliderDouble(ctx, "Min##uf1min", cedit.rmin, 0, 1, "%.3f")
      if ch then cedit.rmin = v; uf1Field(1, v) end
      reaper.ImGui_SameLine(ctx)
      if reaper.ImGui_SmallButton(ctx, "Set##u1capmin") then uf1Field(5, 0) end
      reaper.ImGui_SetNextItemWidth(ctx, 150)
      ch, v = reaper.ImGui_SliderDouble(ctx, "Max##uf1max", cedit.rmax, 0, 1, "%.3f")
      if ch then cedit.rmax = v; uf1Field(2, v) end
      reaper.ImGui_SameLine(ctx)
      if reaper.ImGui_SmallButton(ctx, "Set##u1capmax") then uf1Field(6, 0) end
      if reaper.ImGui_SmallButton(ctx, "Reset range##u1rr") then uf1Field(8, 0) end
      if det.stepped then
        reaper.ImGui_SameLine(ctx)
        reaper.ImGui_TextDisabled(ctx,
          string.format("stepped \xE2\x80\xA2 %d values", det.nsteps))
      end

      -- The UF1 V-Pots DO push (0x09-0x0C) and the press writes this value, so
      -- unlike the UC1's dormant mirror this acts on the hardware.
      reaper.ImGui_Separator(ctx)
      reaper.ImGui_Text(ctx, "Push reset value:")
      reaper.ImGui_SetNextItemWidth(ctx, 150)
      ch, v = reaper.ImGui_SliderDouble(ctx, "##uf1dn", cedit.dn, 0, 1, "%.3f")
      if ch then cedit.dn = v; uf1Field(4, v) end
      reaper.ImGui_SameLine(ctx)
      if reaper.ImGui_SmallButton(ctx, "Capture##u1capdn") then uf1Field(7, 0) end
      if det.pol == 1 then
        reaper.ImGui_SameLine(ctx)
        if reaper.ImGui_SmallButton(ctx, "0.5##u1dncentre") then uf1Field(4, 0.5) end
      end

      reaper.ImGui_Separator(ctx)
      if reaper.ImGui_MenuItem(ctx, "Advanced\xE2\x80\xA6 (curve + sensitivity)") then
        curveOpen, curveNeedsOpen, curveUf8, curveUf1 = true, true, false, true
        curveUf1Pos, curveUf1Sk = ctxUf1Pos, ctxUf1Sk
        curveSens, curveDrag = det.sens, -1
        curvePts = {}
        for _, p in ipairs(det.curve) do curvePts[#curvePts + 1] = { p[1], p[2] } end
      end

      reaper.ImGui_Separator(ctx)
      if reaper.ImGui_BeginMenu(ctx, "Feel presets") then
        local feel = readFeel()
        if reaper.ImGui_BeginMenu(ctx, "Save feel to") then
          for sslot = 0, 9 do
            local fp  = feel[sslot]
            local lbl = string.format("Slot %d%s", sslot + 1,
              (fp and fp.used) and ("  \xE2\x80\xA2  " .. fp.name) or "  (empty)")
            if reaper.ImGui_MenuItem(ctx, lbl .. "##u1fsave" .. sslot) then
              local pre = ((fp and fp.used and fp.name) or ""):gsub(",", " ")
              local ok, val = reaper.GetUserInputs("Save feel preset", 1,
                "Name:,extrawidth=160", pre)
              if ok then
                sendCmd(string.format("uf1feelsave;%d;%d;%d;%s",
                  ctxUf1Pos, ctxUf1Sk, sslot, (val:gsub("[;\n]", " "))))
              end
            end
          end
          reaper.ImGui_EndMenu(ctx)
        end
        if reaper.ImGui_BeginMenu(ctx, "Apply feel from") then
          local any = false
          for sslot = 0, 9 do
            local fp = feel[sslot]
            if fp and fp.used then
              any = true
              if reaper.ImGui_MenuItem(ctx,
                  string.format("Slot %d: %s##u1fapp%d", sslot + 1, fp.name, sslot)) then
                sendCmd(string.format("uf1feelapply;%d;%d;%d",
                  ctxUf1Pos, ctxUf1Sk, sslot))
              end
            end
          end
          if not any then
            reaper.ImGui_BeginDisabled(ctx)
            reaper.ImGui_MenuItem(ctx, "(no saved presets)")
            reaper.ImGui_EndDisabled(ctx)
          end
          reaper.ImGui_EndMenu(ctx)
        end
        -- Clearing a preset is global, so it reuses the shared verb.
        if reaper.ImGui_BeginMenu(ctx, "Clear preset") then
          local any = false
          for sslot = 0, 9 do
            local fp = feel[sslot]
            if fp and fp.used then
              any = true
              if reaper.ImGui_MenuItem(ctx,
                  string.format("Slot %d: %s##u1fclr%d", sslot + 1, fp.name, sslot)) then
                sendCmd("feelclear;" .. sslot)
              end
            end
          end
          if not any then
            reaper.ImGui_BeginDisabled(ctx)
            reaper.ImGui_MenuItem(ctx, "(none)")
            reaper.ImGui_EndDisabled(ctx)
          end
          reaper.ImGui_EndMenu(ctx)
        end
        reaper.ImGui_EndMenu(ctx)
      end
      if reaper.ImGui_MenuItem(ctx, "Reset feel to default##u1fdef") then
        uf1Field(9, 0)
      end
    end

    -- Per-key LED colour (v12) — soft-keys only: a UF1 V-Pot has no LED, it is
    -- drawn on the screen. Same palette as the UF8 bank/strip swatches; the key
    -- carries its on-state in the brightness, so a coloured key still reads.
    if ctxUf1Sk == 1 then
      reaper.ImGui_Separator(ctx)
      if reaper.ImGui_BeginMenu(ctx, "LED colour") then
        for i, rgb in ipairs(UF8_BANK_PALETTE) do
          local swatch = (rgb << 8) | 0xFF          -- 0xRRGGBBAA for ReaImGui
          if i % 5 ~= 1 then reaper.ImGui_SameLine(ctx) end
          if reaper.ImGui_ColorButton(ctx, "##u1led" .. i, swatch, 0, 24, 24) then
            sendCmd(string.format("uf1ledcol;%d;%06X", ctxUf1Pos, rgb))
          end
        end
        reaper.ImGui_Separator(ctx)
        if reaper.ImGui_MenuItem(ctx, "No colour (state only)") then
          sendCmd(string.format("uf1ledcol;%d;000000", ctxUf1Pos))
        end
        reaper.ImGui_EndMenu(ctx)
      end
    end

    -- Fixed action (v14) — soft-keys only. SSL's own plug-in pages weld
    -- PLUG-IN to soft-key 4 of page 1 and HQ / A/B to page 2; this moves them
    -- to any key. Overrides the parameter binding on that key.
    if ctxUf1Sk == 1 then
      reaper.ImGui_Separator(ctx)
      if reaper.ImGui_BeginMenu(ctx, "Action") then
        local UF1_SPECIALS = {
          { 0, "Plug-in parameter"    },
          { 3, "SSL Strip Mode"       },
          { 4, "SSL Strip Mode + GUI" },
          { 1, "HQ Mode"              },
          { 2, "A/B compare"          },
        }
        for _, sp in ipairs(UF1_SPECIALS) do
          if reaper.ImGui_MenuItem(ctx, sp[2]) then
            sendCmd(string.format("uf1special;%d;%d", ctxUf1Pos, sp[1]))
          end
        end
        reaper.ImGui_EndMenu(ctx)
      end
    end

    -- Push cycle — soft-keys only (a V-Pot turns, it doesn't step). Writes
    -- hud_uf1_push_req only while the submenu is open, so the extension builds
    -- just this key's steps + param catalog.
    local u1PushReq = ""
    if ctxUf1Sk == 1 then
      reaper.ImGui_Separator(ctx)
      if reaper.ImGui_BeginMenu(ctx, "Push cycle") then
        u1PushReq = tostring(ctxUf1Pos)
        drawPushCycleUf1(ctxUf1Pos)
        reaper.ImGui_EndMenu(ctx)
      end
    end
    reaper.SetExtState(SECT, "hud_uf1_push_req", u1PushReq, false)

    -- Rename writes an inline field rather than a native dialog, matching the
    -- Settings side (native dialogs are unreliable on macOS 15).
    reaper.ImGui_Separator(ctx)
    -- 11 characters before the UF1's yellow value zone starts (V-Pots) —
    -- measured on the hardware 2026-08-09. Mirrors kUf1LabelChars (main.cpp)
    -- and the Settings FX-Learn cell; keep all three in step.
    reaper.ImGui_Text(ctx, (ctxUf1Sk == 0) and "Display name (11 chars)"
                                            or "Display name")
    if uf1NameBuf == nil or uf1NameFor ~= arg then
      uf1NameFor = arg
      uf1NameBuf = cell.label or ""
    end
    reaper.ImGui_SetNextItemWidth(ctx, 160)
    local ch, v = reaper.ImGui_InputText(ctx, "##uf1name", uf1NameBuf)
    if ch then uf1NameBuf = v; sendCmd("uf1rename;" .. arg .. ";" .. v) end
    reaper.ImGui_Separator(ctx)
    if reaper.ImGui_MenuItem(ctx, "Unbind") then
      sendCmd("uf1unbind;" .. arg)
    end
  end
  reaper.ImGui_EndPopup(ctx)
  reaper.ImGui_PopStyleVar(ctx, 3)
end

local function drawUf8ControlContextMenu()
  if ctxUf8Kind < 0 then return end
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_WindowPadding(), 10, 8)
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_ItemSpacing(), 10, 7)
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_FramePadding(), 7, 4)
  if not reaper.ImGui_BeginPopup(ctx, UF8_CTRL_POPUP) then
    reaper.ImGui_PopStyleVar(ctx, 3)
    return
  end

  local uasn   = readUf8Assign()
  local a      = uasn[ctxUf8Strip] and uasn[ctxUf8Strip][ctxUf8Kind]
  local mapped = (a ~= nil and a.name ~= "")
  local kd     = UF8_KINDS[ctxUf8Kind + 1]

  -- Header (disabled, informational): "<kind> <strip>  →  <param>".
  reaper.ImGui_BeginDisabled(ctx)
  local hdr = ((kd and kd.l) or "Cell") .. " " .. (ctxUf8Strip + 1)
  if mapped then hdr = hdr .. "  \xE2\x86\x92  " .. a.name end
  reaper.ImGui_MenuItem(ctx, hdr)
  reaper.ImGui_EndDisabled(ctx)
  reaper.ImGui_Separator(ctx)

  if reaper.ImGui_MenuItem(ctx, "Learn (wiggle a parameter)") then
    sendCmd("uf8learn;" .. ctxUf8Kind .. ";" .. ctxUf8Strip)
  end
  if not mapped then reaper.ImGui_BeginDisabled(ctx) end
  if reaper.ImGui_MenuItem(ctx, "Invert", nil, mapped and a.inv or false) then
    sendCmd("uf8invert;" .. ctxUf8Kind .. ";" .. ctxUf8Strip)
  end
  -- V-Pot only: Value (continuous) vs Toggle (push = on/off).
  if ctxUf8Kind == 0 then
    if reaper.ImGui_BeginMenu(ctx, "V-Pot mode") then
      if reaper.ImGui_MenuItem(ctx, "Value (continuous)", nil, a and a.mode == 0) then
        sendCmd("uf8vmode;" .. ctxUf8Strip .. ";0")
      end
      if reaper.ImGui_MenuItem(ctx, "Toggle (push on/off)", nil, a and a.mode == 1) then
        sendCmd("uf8vmode;" .. ctxUf8Strip .. ";1")
      end
      if reaper.ImGui_MenuItem(ctx, "Step cycle (turn)", nil, a and a.mode == 2) then
        sendCmd("uf8vmode;" .. ctxUf8Strip .. ";2")
      end
      reaper.ImGui_EndMenu(ctx)
    end
  end

  -- V-Pot full tuning (mapped V-Pot only) — parity with the FX-Learn UF8 V-Pot
  -- menu: polarity / travel / push-reset (Value mode) + curve + feel presets.
  -- Faders are deliberately excluded (same as FX-Learn — motor faders fight the
  -- inverse-curve echo). Commands send strip only; C++ resolves banks live.
  local det = (ctxUf8Kind == 0) and readUf8Detail()[ctxUf8Strip] or nil
  if det then
    local function uf8Field(f, val)
      sendCmd(string.format("uf8field;%d;%d;%.6f", ctxUf8Strip, f, val))
    end
    if det.mode == 0 then
      reaper.ImGui_Separator(ctx)
      if reaper.ImGui_MenuItem(ctx, "Polarity: Unipolar (0 \xE2\x86\x92 1)",
                               nil, det.pol == 0) then uf8Field(0, 0) end
      if reaper.ImGui_MenuItem(ctx, "Polarity: Bipolar (\xE2\x88\x92 0.5 +)",
                               nil, det.pol == 1) then uf8Field(0, 1) end
    end

    reaper.ImGui_Separator(ctx)
    reaper.ImGui_Text(ctx, "Knob travel:")
    if not reaper.ImGui_IsAnyItemActive(ctx) then
      cedit.rmin, cedit.rmax, cedit.dn = det.rmin, det.rmax, det.dn
    end
    reaper.ImGui_SetNextItemWidth(ctx, 150)
    local ch, v = reaper.ImGui_SliderDouble(ctx, "Min##uf8min", cedit.rmin, 0, 1, "%.3f")
    if ch then cedit.rmin = v; uf8Field(1, v) end
    reaper.ImGui_SameLine(ctx)
    if reaper.ImGui_SmallButton(ctx, "Set##ucapmin") then uf8Field(5, 0) end
    reaper.ImGui_SetNextItemWidth(ctx, 150)
    ch, v = reaper.ImGui_SliderDouble(ctx, "Max##uf8max", cedit.rmax, 0, 1, "%.3f")
    if ch then cedit.rmax = v; uf8Field(2, v) end
    reaper.ImGui_SameLine(ctx)
    if reaper.ImGui_SmallButton(ctx, "Set##ucapmax") then uf8Field(6, 0) end
    if reaper.ImGui_SmallButton(ctx, "Reset range##urr") then uf8Field(8, 0) end
    if det.stepped then
      reaper.ImGui_SameLine(ctx)
      reaper.ImGui_TextDisabled(ctx,
        string.format("stepped \xE2\x80\xA2 %d values", det.nsteps))
    end

    if det.mode == 0 then
      reaper.ImGui_Separator(ctx)
      reaper.ImGui_Text(ctx, "Push reset value:")
      reaper.ImGui_SetNextItemWidth(ctx, 150)
      ch, v = reaper.ImGui_SliderDouble(ctx, "##uf8dn", cedit.dn, 0, 1, "%.3f")
      if ch then cedit.dn = v; uf8Field(4, v) end
      reaper.ImGui_SameLine(ctx)
      if reaper.ImGui_SmallButton(ctx, "Capture##ucapdn") then uf8Field(7, 0) end
      if det.pol == 1 then
        reaper.ImGui_SameLine(ctx)
        if reaper.ImGui_SmallButton(ctx, "0.5##udncentre") then uf8Field(4, 0.5) end
      end
    end

    reaper.ImGui_Separator(ctx)
    if reaper.ImGui_MenuItem(ctx, "Advanced\xE2\x80\xA6 (curve + sensitivity)") then
      curveOpen, curveNeedsOpen, curveUf8, curveUf1 = true, true, true, false
      curveStrip = ctxUf8Strip
      curveSens, curveDrag = det.sens, -1
      curvePts = {}
      for _, p in ipairs(det.curve) do curvePts[#curvePts + 1] = { p[1], p[2] } end
    end

    reaper.ImGui_Separator(ctx)
    if reaper.ImGui_BeginMenu(ctx, "Feel presets") then
      local feel = readFeel()
      if reaper.ImGui_BeginMenu(ctx, "Save feel to") then
        for sslot = 0, 9 do
          local fp  = feel[sslot]
          local lbl = string.format("Slot %d%s", sslot + 1,
            (fp and fp.used) and ("  \xE2\x80\xA2  " .. fp.name) or "  (empty)")
          if reaper.ImGui_MenuItem(ctx, lbl .. "##ufsave" .. sslot) then
            local pre = ((fp and fp.used and fp.name) or ""):gsub(",", " ")
            local ok, val = reaper.GetUserInputs("Save feel preset", 1,
              "Name:,extrawidth=160", pre)
            if ok then
              sendCmd(string.format("uf8feelsave;%d;%d;%s",
                ctxUf8Strip, sslot, (val:gsub("[;\n]", " "))))
            end
          end
        end
        reaper.ImGui_EndMenu(ctx)
      end
      if reaper.ImGui_BeginMenu(ctx, "Apply feel from") then
        local any = false
        for sslot = 0, 9 do
          local fp = feel[sslot]
          if fp and fp.used then
            any = true
            if reaper.ImGui_MenuItem(ctx,
                string.format("Slot %d: %s##ufapp%d", sslot + 1, fp.name, sslot)) then
              sendCmd(string.format("uf8feelapply;%d;%d", ctxUf8Strip, sslot))
            end
          end
        end
        if not any then
          reaper.ImGui_BeginDisabled(ctx)
          reaper.ImGui_MenuItem(ctx, "(no saved presets)")
          reaper.ImGui_EndDisabled(ctx)
        end
        reaper.ImGui_EndMenu(ctx)
      end
      if reaper.ImGui_BeginMenu(ctx, "Clear preset") then
        local any = false
        for sslot = 0, 9 do
          local fp = feel[sslot]
          if fp and fp.used then
            any = true
            if reaper.ImGui_MenuItem(ctx,
                string.format("Slot %d: %s##ufclr%d", sslot + 1, fp.name, sslot)) then
              sendCmd("feelclear;" .. sslot)
            end
          end
        end
        if not any then
          reaper.ImGui_BeginDisabled(ctx)
          reaper.ImGui_MenuItem(ctx, "(none)")
          reaper.ImGui_EndDisabled(ctx)
        end
        reaper.ImGui_EndMenu(ctx)
      end
      if reaper.ImGui_MenuItem(ctx, "Reset feel to default") then uf8Field(9, 0) end
      reaper.ImGui_EndMenu(ctx)
    end
  end

  -- Fill sequential: only for strips 1..7 (binds strips to the right).
  if ctxUf8Strip >= 7 then reaper.ImGui_BeginDisabled(ctx) end
  if reaper.ImGui_MenuItem(ctx, "Fill sequential \xE2\x86\x92") then
    sendCmd("uf8fill;" .. ctxUf8Kind .. ";" .. ctxUf8Strip)
  end
  if ctxUf8Strip >= 7 then reaper.ImGui_EndDisabled(ctx) end
  if reaper.ImGui_MenuItem(ctx, "Unbind") then
    sendCmd("uf8unbind;" .. ctxUf8Kind .. ";" .. ctxUf8Strip)
  end
  if not mapped then reaper.ImGui_EndDisabled(ctx) end

  -- Step-cycle editor (V-Pot only) — outside the unmapped-disabled guard so a
  -- macro can be built on an as-yet-unmapped V-Pot. Writes hud_uf8_push_req
  -- only while the submenu is open so the extension publishes just this strip.
  local uPushReq = ""
  if ctxUf8Kind == 0 then
    reaper.ImGui_Separator(ctx)
    if reaper.ImGui_BeginMenu(ctx, "Step cycle") then
      uPushReq = tostring(ctxUf8Strip)
      drawPushCycleUf8(ctxUf8Strip)
      reaper.ImGui_EndMenu(ctx)
    end
  end
  reaper.SetExtState(SECT, "hud_uf8_push_req", uPushReq, false)

  reaper.ImGui_EndPopup(ctx)
  reaper.ImGui_PopStyleVar(ctx, 3)
end

-- V-Pot bank right-click menu: rename / clear the bank's display name (the
-- hardware Top-Soft-Key label). Acts on ctxUf8Bank via the hud_cmd channel.
local function drawUf8BankContextMenu()
  if ctxUf8Bank < 0 then return end
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_WindowPadding(), 10, 8)
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_ItemSpacing(), 10, 7)
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_FramePadding(), 7, 4)
  if not reaper.ImGui_BeginPopup(ctx, UF8_BANK_POPUP) then
    reaper.ImGui_PopStyleVar(ctx, 3)
    return
  end

  local cur = uf8Banks[ctxUf8Bank] or ""

  reaper.ImGui_BeginDisabled(ctx)
  local hdr = "V-Pot Bank " .. (ctxUf8Bank + 1)
  if cur ~= "" then hdr = hdr .. "  \xE2\x86\x92  " .. cur end
  reaper.ImGui_MenuItem(ctx, hdr)
  reaper.ImGui_EndDisabled(ctx)
  reaper.ImGui_Separator(ctx)

  if reaper.ImGui_MenuItem(ctx, "Rename\xE2\x80\xA6") then
    local prefill = cur:gsub(",", " ")
    local ok, val = reaper.GetUserInputs("Rename V-Pot bank", 1,
      "Bank name (empty = number):,extrawidth=160", prefill)
    if ok then
      sendCmd("uf8bankname;" .. ctxUf8Bank .. ";" .. (val:gsub("[;\n]", " ")))
    end
  end
  if cur == "" then reaper.ImGui_BeginDisabled(ctx) end
  if reaper.ImGui_MenuItem(ctx, "Clear name") then
    sendCmd("uf8bankname;" .. ctxUf8Bank .. ";")
  end
  if cur == "" then reaper.ImGui_EndDisabled(ctx) end

  -- Colour picker — identical packing + flags to the FX-Learn palette
  -- (ColorButton takes 0xRRGGBBAA, flags 0; the earlier NoAlpha/NoBorder flags
  -- were the only difference and made the swatches read swapped). A separate
  -- swatch shows the current colour; the 10 SSL DAW-Colour swatches set it.
  reaper.ImGui_Separator(ctx)
  local curCol = uf8BankCols[ctxUf8Bank]
  reaper.ImGui_Text(ctx, "Colour")
  reaper.ImGui_SameLine(ctx)
  reaper.ImGui_ColorButton(ctx, "##bankcolcur",
    curCol and ((curCol << 8) | 0xFF) or 0x40404080, 0, 44, 18)
  for i, rgb in ipairs(UF8_BANK_PALETTE) do
    if reaper.ImGui_ColorButton(ctx, "##bankcol" .. i, (rgb << 8) | 0xFF, 0, 20, 20) then
      sendCmd(string.format("uf8bankcolour;%d;%06X", ctxUf8Bank, rgb))
      reaper.ImGui_CloseCurrentPopup(ctx)
    end
    if i % 5 ~= 0 then reaper.ImGui_SameLine(ctx) end
  end

  -- Display colour-bar colour — ONE colour for all 8 strips of the active bank
  -- (like FX-Learn "Fill all"). Sets the per-strip stripColour, not the LED.
  reaper.ImGui_Separator(ctx)
  reaper.ImGui_Text(ctx, "Bar colour (all)")
  reaper.ImGui_SameLine(ctx)
  if reaper.ImGui_SmallButton(ctx, "Default##barcoldef") then
    sendCmd("uf8stripcolfill;000000")
    reaper.ImGui_CloseCurrentPopup(ctx)
  end
  for i, rgb in ipairs(UF8_BANK_PALETTE) do
    if reaper.ImGui_ColorButton(ctx, "##barcol" .. i, (rgb << 8) | 0xFF, 0, 20, 20) then
      sendCmd(string.format("uf8stripcolfill;%06X", rgb))
      reaper.ImGui_CloseCurrentPopup(ctx)
    end
    if i % 5 ~= 0 then reaper.ImGui_SameLine(ctx) end
  end

  reaper.ImGui_EndPopup(ctx)
  reaper.ImGui_PopStyleVar(ctx, 3)
end

-- Display colour-bar popups (mockup view) — mirrors FX-Learn's
-- drawFxLearnUf8StripBars_: left-click a strip's bar opens a per-strip palette
-- (writes uf8stripcol;<strip>;<rgb>), right-click opens a fill-all palette
-- (uf8stripcolfill;<rgb>). Both use the 10 SSL DAW-Colour swatches; "Default"/
-- "Clear all" send 000000 = no override (back to the track-colour fallback).
local function drawUf8StripColPopups()
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_WindowPadding(), 10, 8)
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_ItemSpacing(), 8, 6)
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_FramePadding(), 6, 4)

  -- Left-click → this strip only.
  if reaper.ImGui_BeginPopup(ctx, UF8_STRIPCOL_POPUP) then
    reaper.ImGui_BeginDisabled(ctx)
    reaper.ImGui_MenuItem(ctx, "Strip " .. (ctxUf8ColStrip + 1) .. " bar colour")
    reaper.ImGui_EndDisabled(ctx)
    reaper.ImGui_Separator(ctx)
    if reaper.ImGui_SmallButton(ctx, "Default##stripcoldef") then
      sendCmd("uf8stripcol;" .. ctxUf8ColStrip .. ";000000")
      reaper.ImGui_CloseCurrentPopup(ctx)
    end
    for i, rgb in ipairs(UF8_BANK_PALETTE) do
      if reaper.ImGui_ColorButton(ctx, "##scp" .. i, (rgb << 8) | 0xFF, 0, 20, 20) then
        sendCmd(string.format("uf8stripcol;%d;%06X", ctxUf8ColStrip, rgb))
        reaper.ImGui_CloseCurrentPopup(ctx)
      end
      if i % 5 ~= 0 then reaper.ImGui_SameLine(ctx) end
    end
    reaper.ImGui_EndPopup(ctx)
  end

  -- Right-click → fill all 8 strips of the active bank.
  if reaper.ImGui_BeginPopup(ctx, UF8_STRIPCOL_FILL_POPUP) then
    reaper.ImGui_BeginDisabled(ctx)
    reaper.ImGui_MenuItem(ctx, "Fill all strips (this bank)")
    reaper.ImGui_EndDisabled(ctx)
    reaper.ImGui_Separator(ctx)
    if reaper.ImGui_SmallButton(ctx, "Clear all##stripcolclr") then
      sendCmd("uf8stripcolfill;000000")
      reaper.ImGui_CloseCurrentPopup(ctx)
    end
    for i, rgb in ipairs(UF8_BANK_PALETTE) do
      if reaper.ImGui_ColorButton(ctx, "##scfp" .. i, (rgb << 8) | 0xFF, 0, 20, 20) then
        sendCmd(string.format("uf8stripcolfill;%06X", rgb))
        reaper.ImGui_CloseCurrentPopup(ctx)
      end
      if i % 5 ~= 0 then reaper.ImGui_SameLine(ctx) end
    end
    reaper.ImGui_EndPopup(ctx)
  end

  reaper.ImGui_PopStyleVar(ctx, 3)
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
        if activeTab == "uf1" then
          local sk, pos = uf1CellAt(lx, ly)
          if sk then
            ctxUf1Sk, ctxUf1Pos = sk, pos
            -- Freeze the modifier layer at right-click, like the UC1 menu does:
            -- "Send to UC1" lands on THIS layer even after the modifier is
            -- released to navigate the menu. (The UF1 map itself is layer-free;
            -- the layer only picks where the param arrives on the UC1.)
            ctxUf1Layer = readState().layer
            reaper.ImGui_OpenPopup(ctx, UF1_CTRL_POPUP)
          else
            reaper.ImGui_OpenPopup(ctx, POPUP)
          end
        elseif activeTab == "uf8" then
          -- Over a colour bar → fill-all palette; over a V-Pot bank → rename
          -- menu; over a grid cell → per-cell menu; else the main HUD menu.
          local cs = uf8ColAt(lx, ly)
          local bk = uf8BankAt(lx, ly)
          local k, s = uf8CellAt(lx, ly)
          if cs ~= nil then
            ctxUf8ColStrip = cs
            reaper.ImGui_OpenPopup(ctx, UF8_STRIPCOL_FILL_POPUP)
          elseif bk then
            ctxUf8Bank = bk
            reaper.ImGui_OpenPopup(ctx, UF8_BANK_POPUP)
          elseif k then
            ctxUf8Kind, ctxUf8Strip = k, s
            reaper.ImGui_OpenPopup(ctx, UF8_CTRL_POPUP)
          else
            reaper.ImGui_OpenPopup(ctx, POPUP)
          end
        else
          -- Over a control → per-control menu (Learn/Invert/Unbind); else the
          -- main HUD menu.
          local cidx = controlAt(lx, ly)
          if cidx then
            ctxCtrlIdx = cidx
            -- Latch the held-modifier layer NOW (right-click, modifier still
            -- down). readState() re-parses the live hud_state payload — `st`
            -- from render() isn't in scope in loop(). The native rename dialog
            -- later releases the modifier, so we must capture it here.
            ctxCtrlLayer = readState().layer
            reaper.ImGui_OpenPopup(ctx, CTRL_POPUP)
          else
            reaper.ImGui_OpenPopup(ctx, POPUP)
          end
        end
      elseif reaper.ImGui_IsMouseClicked(ctx, 0) then
        if csFavRect and hitRect(csFavRect, lx, ly) then
          -- CS-Favourite dropdown box → open the slot picker popup.
          reaper.ImGui_OpenPopup(ctx, CS_FAV_POPUP)
        elseif bcFavRect and hitRect(bcFavRect, lx, ly) then
          reaper.ImGui_OpenPopup(ctx, BC_FAV_POPUP)
        elseif nameRect and hitRect(nameRect, lx, ly) then
          -- LCD / UF8 header plug-in name → edit the Kurzname inline.
          editPluginShort(nameRect.dom)
        elseif activeTab == "uf1" then
          -- Tab row first, then page selector, then a cell (arms a learn on the
          -- hardware position — the same arm Touch-to-Learn uses).
          if handleTabClick(lx, ly) then
          else
            local p = uf1PageAt(lx, ly)
            if p then uf1EditPage = p
            else
              local sk, pos = uf1CellAt(lx, ly)
              local armed = tonumber(reaper.GetExtState(SECT, "hud_uf1_learn")) or -1
              local armedPos = (armed >= 0) and (armed & 0xFF) or -1
              local armedSk  = (armed >= 0) and (((armed & 0x100) ~= 0) and 1 or 0) or -1
              if sk then
                -- Clicking the ARMED cell disarms, exactly like the FX-Learn
                -- cell. Anything else arms that cell.
                if pos == armedPos and sk == armedSk then
                  sendCmd("uf1cancel")
                else
                  sendCmd("uf1learn;" .. pos .. ";" .. sk)
                end
              elseif armed >= 0 then
                sendCmd("uf1cancel")   -- click into empty space = escape
              end
            end
          end
        elseif activeTab == "uf8" then
          -- Tab → toggles (Touch-to-Learn / Parameter List) → param row → bank
          -- row → grid cell. A click that misses everything cancels any armed
          -- learn (an easy mouse "escape").
          if handleTabClick(lx, ly) then
          elseif handleLearnBtnClick(lx, ly) or handleParamBtnClick(lx, ly) then
          elseif paramPanelOpen and lx >= WW - RW and ly >= bodyTop() then
            handleParamClick(lx, ly)
          else
            local cs = uf8ColAt(lx, ly)
            local b  = uf8BankAt(lx, ly)
            if cs ~= nil then
              -- Colour bar → per-strip palette (left-click picks this strip).
              ctxUf8ColStrip = cs
              reaper.ImGui_OpenPopup(ctx, UF8_STRIPCOL_POPUP)
            elseif b then sendCmd("uf8bank;" .. b)
            elseif not handleUf8CellClick(lx, ly) then
              if uf8Learn >= 0 then sendCmd("uf8cancel") end
            end
          end
        -- Top toggle buttons first, then param-panel row, then tab, then control.
        elseif handleLearnBtnClick(lx, ly) or handleParamBtnClick(lx, ly) then
        elseif paramPanelOpen and lx >= WW - RW and ly >= bodyTop() then
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

    -- Escape cancels the current assign / learn. ImGui only sees the key while
    -- the HUD window is focused, but the usual flow arms a control then clicks
    -- into the plug-in GUI to wiggle — which defocuses the HUD — so also read
    -- Escape globally (rising edge) via js_ReaScriptAPI when it's installed.
    local escPressed = reaper.ImGui_IsKeyPressed(ctx, reaper.ImGui_Key_Escape())
    if reaper.JS_VKeys_GetState then
      local down = (reaper.JS_VKeys_GetState(0):byte(27) == 1)   -- VK_ESCAPE = 27
      if down and not escDownPrev then escPressed = true end
      escDownPrev = down
    end
    if escPressed then
      if selectedParam >= 0 then selectedParam = -1
      elseif paramFilter ~= "" then paramFilter = ""
      elseif uf8Learn >= 0 then sendCmd("uf8cancel")
      elseif learnIdx >= 0 then sendCmd("cancel") end
    end

    refreshGeom()
    refreshPushBtns()
    render()
    drawContextMenu()
    drawCsFavPopup()
    drawBcFavPopup()
    drawControlContextMenu()
    drawCurveEditor()
    drawUf8ControlContextMenu()
    drawUf1ControlContextMenu()
    drawUf8BankContextMenu()
    drawUf8StripColPopups()

    -- Persist geometry on change.
    local px, py = reaper.ImGui_GetWindowPos(ctx)
    local pw, ph = reaper.ImGui_GetWindowSize(ctx)
    px, py, pw, ph = floor(px), floor(py), floor(pw), floor(ph)
    if px ~= last_x or py ~= last_y or pw ~= last_w or ph ~= last_h then
      last_x, last_y, last_w, last_h = px, py, pw, ph
      reaper.SetExtState(SECT, "hud_imgui_rect",
        string.format("%d,%d,%d,%d", px, py, pw, ph), true)
    end
    -- ReaImGui 0.10: call End ONLY when Begin returned true (visible). Calling it
    -- unconditionally raises "ImGui_End: Calling End() too many times!" every
    -- frame the window is collapsed / clipped / fully off-screen (visible=false)
    -- — which then spams the console and can wedge the instance. See the memory
    -- note reaimgui-v010-begin-end-pairing. Pop* below stay UNCONDITIONAL: they
    -- balance the pushes made BEFORE Begin (window stack only is conditional).
    reaper.ImGui_End(ctx)
  end
  reaper.ImGui_PopStyleColor(ctx, 1)
  reaper.ImGui_PopStyleVar(ctx, 1)

  if open then reaper.defer(loop) else shutdown() end
end

shutdown = function()
  reaper.SetExtState(SECT, RUNKEY, "0", false)
  reaper.SetExtState(SECT, "hud_touch_learn", "0", false)   -- don't leave UC1 inert
  reaper.SetExtState(SECT, "hud_uf8_tab", "0", false)       -- let C++ revert Plugin Mode
  setToggle(false)
end

reaper.atexit(shutdown)
reaper.defer(loop)
