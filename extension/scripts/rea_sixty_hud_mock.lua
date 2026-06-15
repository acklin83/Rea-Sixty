-- @description Rea-Sixty — Learn-HUD MOCK publisher (dev/test, no hardware)
-- @author Störsender
-- @version 0.2.0
-- @provides [main] .
-- @about
--   DEV HELPER. Feeds the Learn-HUD (gfx OR ImGui spike) realistic CS/BC test
--   data so you can see the layout / sizing / tabs / columns / MOCKUP WITHOUT a
--   UC1/UF8 surface connected. Writes the exact ExtState the extension publishes:
--     • hud_geom_uc1  — control geometry (header "UC1;W;H" + one line per control)
--     • hud_state     — "UC1;<cs>;<bc>;<fdom>;<layer>;<csShort>;<bcShort>"
--     • hud_assign    — one line per MAPPED control "<idx>;<paramName>;<inv>"
--   Geometry coords + control order are transcribed VERBATIM from kUc1Controls
--   (SettingsScreen.cpp); the published label = canonical SSL slot name
--   (kSsl360LinkSlots[].name / kSsl360LinkBcSlots[].name), exactly like
--   hudGeometryUc1_ resolves it — NOT the internal silk abbreviation.
--   Re-asserts every defer tick so it survives the extension clearing the keys.
--   Run once to start, run again to stop. NOT for shipping — test only.

local SECT   = "rea_sixty"
local RUNKEY = "hud_mock_running"

if reaper.GetExtState(SECT, RUNKEY) == "1" then
  reaper.SetExtState(SECT, RUNKEY, "0", false)
  return
end
reaper.SetExtState(SECT, RUNKEY, "1", false)

-- SSL accent cap colours (RGBA) — verbatim from kUc1Controls' kCap* constants.
local R, G, B, K, GY, BC = 0xC03038FF, 0x408840FF, 0x4070C0FF, 0x101418FF, 0x6A707CFF, 0x2A4870FF
local NC = 0   -- no cap (toggles / dyn-buttons)

-- Control table — order + idx + shape + coords + cap match kUc1Controls; `name`
-- = canonical slot name; `legend` = LinkSlot.legend (short scribble); `section`
-- mirrors hudSectionFor_. All transcribed from verified source — no guessing.
-- Fields: idx, shape(0=knob 1=toggle 2=dynbtn), dom, name, section,
--         cx, cy, r, w, h, cap, legend.
local CTRLS = {
  -- ---- LEFT COLUMN — CS Filters + EQ ----
  {  0, 0, "c", "LPF",        "Filter",        82,  44,  20, 0,  0,  GY, "LPF"  },
  {  1, 0, "c", "HPF",        "Filter",        174, 70,  20, 0,  0,  GY, "HPF"  },
  {  2, 1, "c", "HF Type",    "EQ",            204, 146, 0,  34, 18, NC, "BELL" },
  {  3, 0, "c", "HF Gain",    "EQ",            82,  154, 20, 0,  0,  R,  "GAIN" },
  {  4, 0, "c", "HF Freq",    "EQ",            174, 182, 20, 0,  0,  R,  "FREQ" },
  {  5, 0, "c", "HMF Gain",   "EQ",            82,  232, 20, 0,  0,  G,  "GAIN" },
  {  6, 0, "c", "HMF Freq",   "EQ",            174, 260, 20, 0,  0,  G,  "FREQ" },
  {  7, 0, "c", "HMF Q",      "EQ",            82,  300, 20, 0,  0,  G,  "Q"    },
  {  8, 1, "c", "EQ Type",    "EQ",            204, 356, 0,  34, 18, NC, "TYPE" },
  {  9, 1, "c", "EQ In",      "EQ",            204, 380, 0,  34, 18, NC, "EQ"   },
  { 10, 0, "c", "LMF Gain",   "EQ",            82,  430, 20, 0,  0,  B,  "GAIN" },
  { 11, 0, "c", "LMF Freq",   "EQ",            174, 458, 20, 0,  0,  B,  "FREQ" },
  { 12, 0, "c", "LMF Q",      "EQ",            82,  498, 20, 0,  0,  B,  "Q"    },
  { 13, 0, "c", "LF Freq",    "EQ",            174, 558, 20, 0,  0,  K,  "FREQ" },
  { 14, 0, "c", "LF Gain",    "EQ",            82,  598, 20, 0,  0,  K,  "GAIN" },
  { 15, 1, "c", "LF Type",    "EQ",            204, 600, 0,  34, 18, NC, "BELL" },
  -- ---- CENTRE — CS In/Out Gain ----
  { 16, 0, "c", "Input Trim", "I/O & Channel", 310, 376, 20, 0,  0,  GY, "IN"   },
  { 17, 0, "c", "Fader Level","I/O & Channel", 550, 376, 20, 0,  0,  GY, "FDR"  },
  -- ---- CENTRE — BC ----
  { 18, 0, "b", "Threshold",  "Bus Comp",      370, 172, 20, 0,  0,  BC, "THR"  },
  { 19, 0, "b", "Makeup",     "Bus Comp",      490, 172, 20, 0,  0,  BC, "MAKE" },
  { 20, 0, "b", "Attack",     "Bus Comp",      370, 240, 20, 0,  0,  BC, "ATK"  },
  { 21, 0, "b", "Release",    "Bus Comp",      490, 240, 20, 0,  0,  BC, "REL"  },
  { 22, 0, "b", "Ratio",      "Bus Comp",      370, 308, 20, 0,  0,  BC, "RATIO"},
  { 23, 1, "b", "Bypass",     "Bus Comp",      476, 294, 0,  28, 28, NC, "BYP"  },
  { 24, 0, "b", "S/C HPF",    "Bus Comp",      370, 376, 20, 0,  0,  BC, "HPF"  },
  { 25, 0, "b", "Mix",        "Bus Comp",      490, 376, 20, 0,  0,  BC, "MIX"  },
  -- ---- RIGHT COLUMN — CS Comp + Gate + Channel ----
  { 26, 2, "c", "Comp F.Atk", "Dynamics",      772, 24,  0,  66, 22, NC, "F.ATK"},
  { 27, 2, "c", "Comp Peak",  "Dynamics",      772, 50,  0,  66, 22, NC, "PEAK" },
  { 28, 0, "c", "Comp Ratio", "Dynamics",      678, 96,  20, 0,  0,  GY, "RATIO"},
  { 29, 0, "c", "Comp Thr",   "Dynamics",      774, 124, 20, 0,  0,  GY, "THR"  },
  { 30, 0, "c", "Comp Rel",   "Dynamics",      678, 158, 20, 0,  0,  GY, "REL"  },
  { 31, 1, "c", "Dynamics In","Dynamics",      632, 221, 0,  22, 22, NC, "DYN"  },
  { 32, 0, "c", "Gate Range", "Gate",          678, 308, 20, 0,  0,  GY, "RNG"  },
  { 33, 0, "c", "Gate Thr",   "Gate",          774, 332, 20, 0,  0,  GY, "THR"  },
  { 34, 0, "c", "Gate Rel",   "Gate",          678, 374, 20, 0,  0,  GY, "REL"  },
  { 35, 0, "c", "Gate Hold",  "Gate",          774, 398, 20, 0,  0,  GY, "HOLD" },
  { 36, 2, "c", "Gate/Exp",   "Gate",          632, 430, 0,  66, 22, NC, "G/E"  },
  { 37, 2, "c", "Gate F.Atk", "Gate",          632, 456, 0,  66, 22, NC, "F.ATK"},
  { 38, 2, "c", "Polarity",   "I/O & Channel", 632, 510, 0,  66, 22, NC, "POL"  },
  { 39, 2, "c", "S/C Listen", "I/O & Channel", 632, 536, 0,  66, 22, NC, "LST"  },
  { 40, 1, "c", "Bypass",     "I/O & Channel", 793, 523, 0,  40, 40, NC, "BYP"  },
}

-- Mapped controls → param name + invert. A couple use param names that DIFFER
-- from the slot name (idx 29 "Comp Thr"→"Threshold", 19 "Makeup"→"Make-Up Gain")
-- to demonstrate the fixed-param-name vs slot-name-tooltip distinction.
local ASSIGN = {
  [0]  = { "LPF",            0 },
  [3]  = { "HF Gain",        0 },
  [4]  = { "HF Freq",        0 },
  [5]  = { "HMF Gain",       0 },
  [28] = { "Comp Ratio",     0 },
  [29] = { "Threshold",      0 },   -- slot "Comp Thr" → param "Threshold"
  [31] = { "Dynamics In",    0 },
  [32] = { "Gate Range",     1 },   -- inverted
  -- BC
  [18] = { "Threshold",      0 },
  [19] = { "Make-Up Gain",   0 },   -- slot "Makeup" → param "Make-Up Gain"
  [22] = { "Ratio",          0 },
  [23] = { "Comp In / Out",  0 },
}

-- Build the static payloads once.
local geom = "UC1;860;660\n"
for _, c in ipairs(CTRLS) do
  -- idx;shape;cx;cy;r;w;h;dom;label;section;cap;legend
  geom = geom .. string.format("%d;%d;%.1f;%.1f;%.1f;%.1f;%.1f;%s;%s;%s;%d;%s\n",
    c[1], c[2], c[6], c[7], c[8], c[9], c[10], c[3], c[4], c[5], c[11], c[12])
end

-- "UC1;<csPresent>;<bcPresent>;<focusDom c/b/n>;<layer 0/1/2>;<csShort>;<bcShort>"
local state = "UC1;1;1;c;0;LINK CS;LINK BUS COMP"

local assign = ""
for idx, a in pairs(ASSIGN) do
  assign = assign .. string.format("%d;%s;%d\n", idx, a[1], a[2])
end

local function loop()
  if reaper.GetExtState(SECT, RUNKEY) ~= "1" then
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
