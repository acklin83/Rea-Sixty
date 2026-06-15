-- @description Rea-Sixty — Learn-HUD MOCK publisher (dev/test, no hardware)
-- @author Störsender
-- @version 0.1.0
-- @provides [main] .
-- @about
--   DEV HELPER. Feeds the Learn-HUD (gfx OR ImGui spike) realistic CS/BC test
--   data so you can see the layout / sizing / tabs / columns WITHOUT a UC1/UF8
--   surface connected. Writes the exact ExtState the extension publishes:
--     • hud_geom_uc1  — control geometry (header "UC1;W;H" + one line per control)
--     • hud_state     — "UC1;<cs>;<bc>;<fdom>;<layer>;<csShort>;<bcShort>"
--     • hud_assign    — one line per MAPPED control "<idx>;<paramName>;<inv>"
--   Re-asserts every defer tick so it survives the extension clearing the keys.
--   Run once to start, run again to stop. NOT for shipping — test only.

local SECT   = "rea_sixty"
local RUNKEY = "hud_mock_running"

if reaper.GetExtState(SECT, RUNKEY) == "1" then
  reaper.SetExtState(SECT, RUNKEY, "0", false)
  return
end
reaper.SetExtState(SECT, RUNKEY, "1", false)

-- Control table: {idx, shape(0=knob 1=toggle), dom('c'/'b'), label, section}.
-- Sections match the HUD's SECTION_ORDER / COLUMN_SECTIONS so the columns fill.
local CTRLS = {
  -- CS — left column: Filter, EQ, I/O & Channel
  { 0,  0, "c", "HF Freq",        "Filter" },
  { 1,  0, "c", "LF Freq",        "Filter" },
  { 2,  0, "c", "HMF Gain",       "EQ" },
  { 3,  0, "c", "HMF Freq",       "EQ" },
  { 4,  0, "c", "LMF Gain",       "EQ" },
  { 5,  0, "c", "LMF Freq",       "EQ" },
  { 12, 0, "c", "Input Gain",     "I/O & Channel" },
  { 13, 1, "c", "Bypass",         "I/O & Channel" },
  -- CS — right column: Dynamics, Gate
  { 6,  0, "c", "Comp Threshold", "Dynamics" },
  { 7,  0, "c", "Comp Ratio",     "Dynamics" },
  { 8,  0, "c", "Comp Attack",    "Dynamics" },
  { 9,  0, "c", "Comp Release",   "Dynamics" },
  { 10, 0, "c", "Gate Threshold", "Gate" },
  { 11, 0, "c", "Gate Range",     "Gate" },
  -- BC — Bus Comp
  { 14, 0, "b", "Threshold",      "Bus Comp" },
  { 15, 0, "b", "Ratio",          "Bus Comp" },
  { 16, 0, "b", "Attack",         "Bus Comp" },
  { 17, 0, "b", "Release",        "Bus Comp" },
  { 18, 0, "b", "Make-Up Gain",   "Bus Comp" },
}

-- Which control indices are "mapped" → param name + invert flag.
local ASSIGN = {
  [0]  = { "High-Pass Freq", 0 },
  [2]  = { "HMF Gain",       0 },
  [3]  = { "HMF Frequency",  0 },
  [6]  = { "Comp Threshold", 0 },
  [7]  = { "Comp Ratio",     0 },
  [8]  = { "Comp Attack",    0 },
  [10] = { "Gate Threshold", 1 },   -- inverted, to show the "inv" tag
  [14] = { "Bus Threshold",  0 },
  [15] = { "Bus Ratio",      0 },
  [16] = { "Bus Attack",     0 },
}

-- Build the static payloads once.
local geom = "UC1;860;660\n"
for _, c in ipairs(CTRLS) do
  -- idx;shape;cx;cy;r;w;h;dom;label;section  (cx..h are dummies; list mode
  -- ignores them, it lays out by section/label).
  geom = geom .. string.format("%d;%d;%.1f;%.1f;%.1f;%.1f;%.1f;%s;%s;%s\n",
    c[1], c[2], 100.0, 100.0, 20.0, 40.0, 40.0, c[3], c[4], c[5])
end

-- "UC1;<csPresent>;<bcPresent>;<focusDom c/b/n>;<layer 0/1/2>;<csShort>;<bcShort>"
local state = "UC1;1;1;c;0;SSL CH;SSL Bus"

local assign = ""
for idx, a in pairs(ASSIGN) do
  assign = assign .. string.format("%d;%s;%d\n", idx, a[1], a[2])
end

local function loop()
  if reaper.GetExtState(SECT, RUNKEY) ~= "1" then
    -- Clear so the HUD goes back to "waiting" instead of showing stale mock data.
    reaper.SetExtState(SECT, "hud_geom_uc1", "", false)
    reaper.SetExtState(SECT, "hud_state",    "", false)
    reaper.SetExtState(SECT, "hud_assign",   "", false)
    return
  end
  reaper.SetExtState(SECT, "hud_geom_uc1", geom,   false)
  reaper.SetExtState(SECT, "hud_state",    state,  false)
  reaper.SetExtState(SECT, "hud_assign",   assign, false)
  reaper.defer(loop)
end

reaper.atexit(function() reaper.SetExtState(SECT, RUNKEY, "0", false) end)
reaper.defer(loop)
