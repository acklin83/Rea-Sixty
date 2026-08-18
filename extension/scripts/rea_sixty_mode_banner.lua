-- @description Rea-Sixty — Mode-change banner (transient on-screen overlay)
-- @author Störsender
-- @version 0.1.0
-- @provides [main] .
-- @about
--   Transient on-screen banner that flashes the new mode whenever the
--   Selection-Mode or Encoder-Mode flips (Stream B3, Frank 2026-06-22).
--
--   The extension publishes the change to ExtState "rea_sixty"/"mode_banner"
--   as "<text>\t<seq>". This companion watches the seq; on each fresh change
--   it shows the text for a couple of seconds, then fades back to invisible.
--   NOT a permanent HUD — between changes the window is fully transparent and
--   click-through.
--
--   A real ReaImGui frameless top-most window (same skeleton as the focused
--   panel) so it can sit anywhere — over the arrange, TCP, ruler, 2nd monitor.
--   Drag it (while a banner is visible) to reposition; position persists.
--
--   Run to start; run again (or untick) to stop. Requires ReaImGui (>= 0.9).

local SECT   = "rea_sixty"
local RUNKEY = "mode_banner_running"

local _, _, sectionID, cmdID = reaper.get_action_context()

-- Single-instance toggle.
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
  reaper.MB("This needs ReaImGui (ReaScript ImGui API).\n\n"
    .. "Install via ReaPack: 'ReaImGui: ReaScript binding for Dear ImGui'.",
    "Rea-Sixty Mode Banner", 0)
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
local function rgba(rgb, a)
  return ((rgb & 0xFFFFFF) << 8) | (a or 0xFF)
end
-- How long (seconds) the banner stays visible after a change.
local function holdSecs() return math.max(0.3, num("mode_banner_secs", 2.0)) end
local function fontPx()   return math.max(10, math.floor(num("mode_banner_font", 30))) end

------------------------------------------------------------------------
-- ImGui context + font
------------------------------------------------------------------------
local ctx  = reaper.ImGui_CreateContext('Rea-Sixty Mode Banner')
local font = reaper.ImGui_CreateFont('sans-serif', 0)
reaper.ImGui_Attach(ctx, font)
local font_px = fontPx()

-- Position persists under its own key. Default: roughly screen-top-centre on
-- the first run (no JS_ dependency — a sensible fixed offset the user nudges).
local function loadRect()
  local raw = reaper.GetExtState(SECT, "mode_banner_rect")
  local x, y, w, h = raw:match("^(%-?%d+),(%-?%d+),(%d+),(%d+)$")
  if x then return tonumber(x), tonumber(y), tonumber(w), tonumber(h) end
  return nil
end
local function saveRect(x, y, w, h)
  reaper.SetExtState(SECT, "mode_banner_rect",
    string.format("%d,%d,%d,%d", x, y, w, h), true)
end

local restore_x, restore_y, restore_w, restore_h = loadRect()
local first_frame = true
local last_save_x, last_save_y, last_save_w, last_save_h

------------------------------------------------------------------------
-- Banner state — watch the published seq, restart the hide timer on change.
------------------------------------------------------------------------
local cur_text   = ""
local last_seq   = nil
local show_until = 0.0

local function pollBanner()
  local raw = reaper.GetExtState(SECT, "mode_banner")
  if raw == "" then return end
  local text, seq = raw:match("^(.-)\t(%d+)$")
  if not seq then return end
  -- First poll of this run: adopt whatever is already published WITHOUT showing
  -- it. The ExtState survives the companion, so starting up (or restarting it)
  -- with last_seq = nil re-flashed the last banner of the previous run, which
  -- read as a mode changing on its own at startup (Frank 2026-08-18).
  if last_seq == nil then
    last_seq = seq
    return
  end
  if seq ~= last_seq then
    last_seq   = seq
    cur_text   = text or ""
    show_until = reaper.time_precise() + holdSecs()
  end
end

------------------------------------------------------------------------
-- Right-click menu
------------------------------------------------------------------------
local POPUP_ID = "##mb_ctx"

local function setFontSize()
  local ret, input = reaper.GetUserInputs("Banner font", 1,
    "Font size: (e.g. 30),extrawidth=40", tostring(fontPx()))
  if ret then
    local fs = tonumber(input)
    if fs then reaper.SetExtState(SECT, "mode_banner_font",
      tostring(math.max(10, math.floor(fs))), true) end
  end
end

local function setHold()
  local ret, input = reaper.GetUserInputs("Banner duration", 1,
    "Seconds visible: (e.g. 2),extrawidth=40", tostring(holdSecs()))
  if ret then
    local v = tonumber(input)
    if v then reaper.SetExtState(SECT, "mode_banner_secs",
      string.format("%.2f", math.max(0.3, v)), true) end
  end
end

local function chooseColor(key)
  local ret, color = reaper.GR_SelectColor(reaper.GetMainHwnd())
  if ret ~= 0 then
    local cr, cg, cb = reaper.ColorFromNative(color)
    reaper.SetExtState(SECT, key, tostring(cr * 65536 + cg * 256 + cb), true)
  end
end

------------------------------------------------------------------------
-- "Load on startup" — append a guarded line to __startup.lua (same scheme
-- as the focused panel).
------------------------------------------------------------------------
local STARTUP_BEGIN = "-- >>> rea_sixty_mode_banner (auto)"
local STARTUP_END   = "-- <<< rea_sixty_mode_banner (auto)"
local function startupPath() return reaper.GetResourcePath() .. "/Scripts/__startup.lua" end
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
      "Rea-Sixty Mode Banner", 0)
    return
  end
  local content = stripStartupBlock(readFileAll(startupPath()) or "")
  if on then
    if content ~= "" and not content:match("\n$") then content = content .. "\n" end
    content = content
      .. STARTUP_BEGIN .. "\n"
      .. "if reaper.GetExtState('rea_sixty','mode_banner_running') ~= '1' then "
      .. "reaper.Main_OnCommand(reaper.NamedCommandLookup('" .. cmd .. "'), 0) end\n"
      .. STARTUP_END .. "\n"
  end
  local f = io.open(startupPath(), "wb")
  if not f then reaper.MB("Could not write:\n" .. startupPath(), "Rea-Sixty Mode Banner", 0); return end
  f:write(content); f:close()
end

local function drawContextMenu()
  if not reaper.ImGui_BeginPopup(ctx, POPUP_ID) then return end
  if reaper.ImGui_MenuItem(ctx, "Font size\xE2\x80\xA6")  then setFontSize() end
  if reaper.ImGui_MenuItem(ctx, "Duration\xE2\x80\xA6")   then setHold() end
  reaper.ImGui_Separator(ctx)
  if reaper.ImGui_MenuItem(ctx, "Background colour\xE2\x80\xA6") then chooseColor("mode_banner_bg") end
  if reaper.ImGui_MenuItem(ctx, "Text colour\xE2\x80\xA6")       then chooseColor("mode_banner_fg") end
  reaper.ImGui_Separator(ctx)
  reaper.ImGui_BeginDisabled(ctx)
  reaper.ImGui_MenuItem(ctx, "Drag the box (while visible) to move it")
  reaper.ImGui_EndDisabled(ctx)
  reaper.ImGui_Separator(ctx)
  local startOn = startupEnabled()
  if reaper.ImGui_MenuItem(ctx, "Load on startup", nil, startOn) then
    setStartup(not startOn)
  end
  if reaper.ImGui_MenuItem(ctx, "Close banner") then
    reaper.SetExtState(SECT, RUNKEY, "0", false)
  end
  reaper.ImGui_EndPopup(ctx)
end

------------------------------------------------------------------------
-- Main loop. ImGui context must be touched every defer cycle (Begin/End)
-- or ReaImGui v0.10 GCs it — so we ALWAYS Begin/End; visibility is driven by
-- the window-bg alpha + whether we draw text + the NoInputs flag.
------------------------------------------------------------------------
local BASE_FLAGS = reaper.ImGui_WindowFlags_NoTitleBar()
  | reaper.ImGui_WindowFlags_NoScrollbar()
  | reaper.ImGui_WindowFlags_NoScrollWithMouse()
  | reaper.ImGui_WindowFlags_NoCollapse()
  | reaper.ImGui_WindowFlags_AlwaysAutoResize()
  | (reaper.ImGui_WindowFlags_TopMost and reaper.ImGui_WindowFlags_TopMost() or 0)

local function loop()
  if reaper.GetExtState(SECT, RUNKEY) ~= "1" then
    setToggle(false)
    return
  end

  pollBanner()
  font_px = fontPx()
  local showing = reaper.time_precise() < show_until and cur_text ~= ""

  if first_frame then
    if restore_x then
      reaper.ImGui_SetNextWindowPos(ctx, restore_x, restore_y)
    else
      reaper.ImGui_SetNextWindowPos(ctx, 600, 120)
    end
    first_frame = false
  end

  local bg = math.floor(num("mode_banner_bg", 0x101216)) & 0xFFFFFF
  local fg = math.floor(num("mode_banner_fg", 0x9CD1FF)) & 0xFFFFFF
  local bgA = showing and 0xF0 or 0x00
  local bdA = showing and 0xFF or 0x00

  -- When hidden, make the window click-through so it never steals input.
  local flags = BASE_FLAGS
  if not showing then
    flags = flags
      | (reaper.ImGui_WindowFlags_NoInputs and reaper.ImGui_WindowFlags_NoInputs() or 0)
      | (reaper.ImGui_WindowFlags_NoNav and reaper.ImGui_WindowFlags_NoNav() or 0)
      | (reaper.ImGui_WindowFlags_NoFocusOnAppearing and reaper.ImGui_WindowFlags_NoFocusOnAppearing() or 0)
  end

  reaper.ImGui_PushStyleColor(ctx, reaper.ImGui_Col_WindowBg(), rgba(bg, bgA))
  reaper.ImGui_PushStyleColor(ctx, reaper.ImGui_Col_Border(),   rgba(0x3A4660, bdA))
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_WindowRounding(), 8)
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_WindowBorderSize(), 1)
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_WindowPadding(), 16, 10)

  reaper.ImGui_PushFont(ctx, font, font_px)
  local visible, open = reaper.ImGui_Begin(ctx, 'Rea-Sixty Mode##mb', true, flags)
  if visible then
    if showing then
      reaper.ImGui_TextColored(ctx, rgba(fg), cur_text)
      if reaper.ImGui_IsWindowHovered(ctx)
         and reaper.ImGui_IsMouseClicked(ctx, 1) then
        reaper.ImGui_OpenPopup(ctx, POPUP_ID)
      end
      drawContextMenu()
      local px, py = reaper.ImGui_GetWindowPos(ctx)
      local pw, ph = reaper.ImGui_GetWindowSize(ctx)
      px, py, pw, ph = math.floor(px), math.floor(py), math.floor(pw), math.floor(ph)
      if px ~= last_save_x or py ~= last_save_y then
        last_save_x, last_save_y, last_save_w, last_save_h = px, py, pw, ph
        saveRect(px, py, pw, ph)
      end
    else
      -- Keep the context alive with a zero-footprint dummy.
      reaper.ImGui_Dummy(ctx, 1, 1)
    end
    reaper.ImGui_End(ctx)
  end
  reaper.ImGui_PopFont(ctx)
  reaper.ImGui_PopStyleVar(ctx, 3)
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
