-- @description Rea-Sixty — Inserts active CS/BC overlay (companion)
-- @author Störsender
-- @version 0.4.0
-- @provides [main] .
-- @about
--   Non-destructive highlight of the **active CS / BC instance** in REAPER's
--   Mixer (MCP) AND Track-panel (TCP) Inserts lists. Reads the active instance
--   per track from the Rea-Sixty extension (ExtState "rea_sixty"/"overlay") and
--   draws a JS_Composite overlay directly on the native fxlist windows — no FX
--   rename, no dirty project. Run once to start (background defer action); run
--   again to stop. Requires js_ReaScriptAPI + the Rea-Sixty extension with
--   "Mark active CS/BC in Inserts list" enabled.
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
local CS_COL = 0x33C0FF | ALPHA   -- cyan
local BC_COL = 0xFFB000 | ALPHA   -- amber
local FILL_A, LINE_A = 0.32, 0.9

local function num(key, def)
  local v = tonumber(reaper.GetExtState(SECT, key))
  if v == nil then return def end
  return v
end

local STEP = 16
local RESCAN_FRAMES = 18

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

------------------------------------------------------------------------
-- Locate FX-list targets per track: MCP per-strip child windows and the
-- TCP names column (shared window). byGuid[guid] = list of blocks.
------------------------------------------------------------------------
local function scanFxBlocks()
  local byGuid, seen = {}, {}
  -- TCP overlay is experimental (shared track-panel window + Retina coordinate
  -- mismatch causes glitches) — opt-in via ExtState overlay_tcp=1. MCP always on.
  local tcpOn = num("overlay_tcp", 0) ~= 0
  local main = reaper.GetMainHwnd()
  local _, ml, mt, mr, mb = reaper.JS_Window_GetRect(main)
  local x0, x1 = math.min(ml, mr), math.max(ml, mr)
  local y0, y1 = math.min(mt, mb), math.max(mt, mb)
  local y = y0
  while y <= y1 do
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
          local key = g .. "|" .. tostring(h) .. "|" .. kind
          if g and g ~= "" and not seen[key] then
            seen[key] = true
            byGuid[g] = byGuid[g] or {}
            if kind == "mcp" then
              local _, cw, ch = reaper.JS_Window_GetClientSize(h)
              if cw and ch and cw > 0 and ch > 0 then
                byGuid[g][#byGuid[g] + 1] = { kind = "mcp", hwnd = h, cw = cw, ch = ch }
              end
            else
              local l, t, r = refineTcp(x, y, tr)
              byGuid[g][#byGuid[g] + 1] = { kind = "tcp", hwnd = h, sl = l, st = t, sw = r - l }
            end
          end
        end
      end
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

local function composite(hwnd, dx, dy, w, h, col)
  if w <= 0 or h <= 0 then return end
  local bmp = reaper.JS_LICE_CreateBitmap(true, w, h)
  if is_windows then reaper.JS_LICE_Clear(bmp, 0x00000000)
  else                reaper.JS_LICE_Clear(bmp, col & 0x00FFFFFF) end
  reaper.JS_LICE_FillRect(bmp, 0, 0, w, h, col, FILL_A, 0)
  reaper.JS_LICE_RoundRect(bmp, 0, 0, w - 1, h - 1, 0, col, LINE_A, 0, true)
  reaper.JS_Composite_Delay(hwnd, 0.03, 0.05, 8)
  reaper.JS_Composite(hwnd, dx, dy, w, h, bmp, 0, 0, w, h)
  reaper.JS_Window_InvalidateRect(hwnd, dx, dy, dx + w, dy + h, false)
  g_drawn[#g_drawn + 1] = { hwnd = hwnd, bmp = bmp, x = dx, y = dy, w = w, h = h }
end

local function drawBlockRow(block, fxIdx, col)
  if block.kind == "mcp" then
    local rowH   = num("overlay_rowh", 14)
    local topPad = num("overlay_toppad", 2)
    local y = topPad + fxIdx * rowH
    if y < 0 or y + rowH > block.ch then return end
    composite(block.hwnd, 0, y, block.cw, rowH, col)
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
        if a.cs and a.cs >= 0 then drawBlockRow(block, a.cs, CS_COL) end
        if a.bc and a.bc >= 0 then drawBlockRow(block, a.bc, BC_COL) end
      end
    end
  end
end

------------------------------------------------------------------------
-- Main defer loop
------------------------------------------------------------------------
local g_lastSig, g_blocks, g_frame, g_lastRev = nil, {}, 0, -1

local function blockSig(b)
  if b.kind == "mcp" then return string.format("m%s,%d,%d", tostring(b.hwnd), b.cw, b.ch)
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
  return table.concat(parts, "|") .. "|" .. num("overlay_rowh", 14) .. "," .. num("overlay_toppad", 2)
    .. "," .. num("overlay_rowh_tcp", 14) .. "," .. num("overlay_toppad_tcp", 0)
end

local shutdown

local function loop()
  if reaper.GetExtState("rea_sixty", RUNKEY) ~= "1" then return shutdown() end
  local on, rev, byGuid = readActive()
  if not on or next(byGuid) == nil then
    if g_lastSig ~= "off" then clearDrawn(); g_lastSig = "off" end
  else
    g_frame = g_frame + 1
    if rev ~= g_lastRev or g_frame >= RESCAN_FRAMES then
      g_blocks = scanFxBlocks(); g_lastRev = rev; g_frame = 0
    end
    local sig = drawSig(byGuid, g_blocks)
    if sig ~= g_lastSig then rebuildDraw(byGuid, g_blocks); g_lastSig = sig end
  end
  reaper.defer(loop)
end

shutdown = function()
  clearDrawn()
  reaper.SetExtState("rea_sixty", RUNKEY, "0", false)
  setToggle(false)
end

reaper.atexit(shutdown)
setToggle(true)
loop()
