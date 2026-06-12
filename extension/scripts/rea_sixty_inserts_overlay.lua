-- @description Rea-Sixty — Inserts active CS/BC overlay (companion)
-- @author Störsender
-- @version 0.1.0
-- @provides [main] .
-- @about
--   Non-destructive highlight of the **active CS / BC instance** in REAPER's
--   Mixer Inserts list. Reads the active instance per track from the Rea-Sixty
--   extension (ExtState "rea_sixty"/"overlay") and draws a JS_Composite overlay
--   on the native mixer — no FX rename, no dirty project.
--
--   Run it once; it lives as a background (defer) action. Run again to toggle
--   off. Requires the js_ReaScriptAPI extension and the Rea-Sixty extension
--   with "Mark active CS/BC in Inserts list" enabled.

------------------------------------------------------------------------
-- Toggle-on-relaunch + toolbar state
------------------------------------------------------------------------
local _, _, sectionID, cmdID = reaper.get_action_context()

local function setToggle(on)
  if sectionID and cmdID and sectionID >= 0 then
    reaper.SetToggleCommandState(sectionID, cmdID, on and 1 or 0)
    reaper.RefreshToolbar2(sectionID, cmdID)
  end
end

------------------------------------------------------------------------
-- Capability check
------------------------------------------------------------------------
local function has(fn) return reaper.APIExists(fn) end
local NEED = { "GetThingFromPoint", "GetTrackFromPoint", "JS_Composite",
  "JS_Window_FromPoint", "JS_Window_GetClientRect", "JS_LICE_CreateBitmap",
  "JS_LICE_FillRect", "JS_LICE_Line", "JS_LICE_Clear" }
for _, fn in ipairs(NEED) do
  if not has(fn) then
    reaper.MB("Diese Funktion fehlt: " .. fn ..
      "\n\nBitte js_ReaScriptAPI installieren (ReaPack).",
      "Rea-Sixty Inserts Overlay", 0)
    return
  end
end

------------------------------------------------------------------------
-- Tunables (read live from ExtState so they can be calibrated without an
-- edit; defaults match the stock REAPER theme on Frank's setup). Row in
-- the Inserts list == FX chain index, packed from the top of the block.
------------------------------------------------------------------------
local SECT = "rea_sixty"
local function num(key, def)
  local v = tonumber(reaper.GetExtState(SECT, key))
  if v == nil then return def end
  return v
end

-- Colours (0xRRGGBB) + alphas for the highlight fill / border per domain.
local CS_COL, BC_COL = 0x33C0FF, 0xFFB000   -- cyan-ish / amber
local FILL_A, LINE_A = 0.20, 0.85

-- Re-scan the mixer for fxlist blocks at most every RESCAN_FRAMES ticks
-- (mixer scroll / resize / track add) — between scans we reuse cached rects.
local RESCAN_FRAMES = 18

------------------------------------------------------------------------
-- ExtState read: active CS/BC per track GUID
--   value = "<on>;<rev>;guid,csFx,bcFx;guid,csFx,bcFx;..."
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
-- Locate the mcp.fxlist block (screen rect) for every track that has one.
-- Scans the main REAPER window region with GetThingFromPoint (works at any
-- screen coord, not just the mouse), groups hits by track via
-- GetTrackFromPoint, then refines each block's edges to the pixel.
------------------------------------------------------------------------
local STEP = 16

local function isList(x, y)
  local _, info = reaper.GetThingFromPoint(x, y)
  return info == "mcp.fxlist"
end

local function refineBlock(cx, cy)
  local t = cy; while isList(cx, t - 1) do t = t - 1 end
  local b = cy; while isList(cx, b + 1) do b = b + 1 end
  local mid = (t + b) // 2
  local l = cx; while isList(l - 1, mid) do l = l - 1 end
  local r = cx; while isList(r + 1, mid) do r = r + 1 end
  return l, t, r + 1, b + 1
end

local function scanFxListBlocks()
  local blocks = {}           -- guid -> {l,t,r,b}
  local seedT, seedB
  local main = reaper.GetMainHwnd()
  local regions = {}
  if main then
    local ok, l, t, r, b = reaper.JS_Window_GetRect(main)
    if ok then regions[#regions + 1] = { l = l, t = t, r = r, b = b } end
  end
  for _, rg in ipairs(regions) do
    local y = rg.t
    while y <= rg.b do
      local x = rg.l
      while x <= rg.r do
        if isList(x, y) then
          local tr = reaper.GetTrackFromPoint(x, y)
          if tr then
            local guid = reaper.GetTrackGUID(tr)
            if guid and guid ~= "" and not blocks[guid] then
              local bl, bt, br, bb = refineBlock(x, y)
              blocks[guid] = { l = bl, t = bt, r = br, b = bb }
            end
          end
        end
        x = x + STEP
      end
      y = y + STEP
    end
  end
  return blocks
end

------------------------------------------------------------------------
-- Compositing — one LICE bitmap per highlighted row, tracked for cleanup.
------------------------------------------------------------------------
local g_drawn = {}            -- list of {hwnd, bmp}

local function clearDrawn()
  for _, d in ipairs(g_drawn) do
    reaper.JS_Composite(d.hwnd, 0, 0, 0, 0, d.bmp, 0, 0, 0, 0, false)
    reaper.JS_Window_InvalidateRect(d.hwnd, 0, 0, 99999, 99999, false)
    reaper.JS_LICE_DestroyBitmap(d.bmp)
  end
  g_drawn = {}
end

-- screen rect -> composite a filled+bordered highlight on the hwnd under it
local function drawRowHighlight(sl, st, sr, sb, col)
  local hwnd = reaper.JS_Window_FromPoint(sl + 2, st + 2)
  if not hwnd then return end
  local ok, cl, ct = reaper.JS_Window_GetClientRect(hwnd)
  if not ok then return end
  local w = math.max(1, sr - sl)
  local h = math.max(1, sb - st)
  local bmp = reaper.JS_LICE_CreateBitmap(true, w, h)
  if not bmp then return end
  reaper.JS_LICE_Clear(bmp, 0)
  reaper.JS_LICE_FillRect(bmp, 0, 0, w, h, col, FILL_A, "COPY")
  reaper.JS_LICE_Line(bmp, 0,     0,     w - 1, 0,     col, LINE_A, "COPY", false)
  reaper.JS_LICE_Line(bmp, 0,     h - 1, w - 1, h - 1, col, LINE_A, "COPY", false)
  reaper.JS_LICE_Line(bmp, 0,     0,     0,     h - 1, col, LINE_A, "COPY", false)
  reaper.JS_LICE_Line(bmp, w - 1, 0,     w - 1, h - 1, col, LINE_A, "COPY", false)
  local dstx, dsty = sl - cl, st - ct
  reaper.JS_Composite(hwnd, dstx, dsty, w, h, bmp, 0, 0, w, h, false)
  reaper.JS_Window_InvalidateRect(hwnd, dstx, dsty, dstx + w, dsty + h, false)
  g_drawn[#g_drawn + 1] = { hwnd = hwnd, bmp = bmp }
end

------------------------------------------------------------------------
-- Build the set of row highlights from active state + cached blocks, draw.
------------------------------------------------------------------------
local function rowRect(block, fxIdx, rowH, topPad)
  local t = block.t + topPad + fxIdx * rowH
  return block.l, t, block.r, t + rowH
end

local function rebuildDraw(byGuid, blocks)
  clearDrawn()
  local rowH   = num("overlay_rowh", 14)
  local topPad = num("overlay_toppad", 1)
  for guid, a in pairs(byGuid) do
    local block = blocks[guid]
    if block then
      if a.cs and a.cs >= 0 then
        local l, t, r, b = rowRect(block, a.cs, rowH, topPad)
        if t >= block.t and b <= block.b + 1 then drawRowHighlight(l, t, r, b, CS_COL) end
      end
      if a.bc and a.bc >= 0 then
        local l, t, r, b = rowRect(block, a.bc, rowH, topPad)
        if t >= block.t and b <= block.b + 1 then drawRowHighlight(l, t, r, b, BC_COL) end
      end
    end
  end
end

------------------------------------------------------------------------
-- Main defer loop. Repaints only when something changed (rev, geometry,
-- or a periodic re-scan picks up scroll/resize) — steady state is idle.
------------------------------------------------------------------------
local g_lastSig   = nil
local g_blocks    = {}
local g_frame     = 0
local g_lastRev   = -1

local function drawSig(byGuid, blocks)
  local parts = {}
  for guid, a in pairs(byGuid) do
    local b = blocks[guid]
    parts[#parts + 1] = string.format("%s:%s:%s:%s",
      guid, tostring(a.cs), tostring(a.bc),
      b and string.format("%d,%d,%d,%d", b.l, b.t, b.r, b.b) or "x")
  end
  table.sort(parts)
  return table.concat(parts, "|") .. "|rh=" .. num("overlay_rowh", 14)
    .. ",tp=" .. num("overlay_toppad", 1)
end

local function loop()
  local on, rev, byGuid = readActive()

  if not on or next(byGuid) == nil then
    if g_lastSig ~= "off" then clearDrawn(); g_lastSig = "off" end
  else
    g_frame = g_frame + 1
    -- Re-scan blocks on rev change or periodically (catches scroll/resize).
    if rev ~= g_lastRev or g_frame >= RESCAN_FRAMES then
      g_blocks = scanFxListBlocks()
      g_lastRev = rev
      g_frame = 0
    end
    local sig = drawSig(byGuid, g_blocks)
    if sig ~= g_lastSig then
      rebuildDraw(byGuid, g_blocks)
      g_lastSig = sig
    end
  end

  reaper.defer(loop)
end

local function shutdown()
  clearDrawn()
  setToggle(false)
end

reaper.atexit(shutdown)
setToggle(true)
loop()
