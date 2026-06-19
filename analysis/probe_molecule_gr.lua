-- probe_molecule_gr.lua
--
-- ReaScript. Polls EVERY FX on the selected track for ~2 seconds and
-- reports the MAX gain-reduction value each FX exposes through the
-- PreSonus-style named-config-parm readback (and a few likely aliases).
--
-- Purpose: determine whether REAPER exposes a JSFX's ext_gr_meter gain
-- reduction via the host API at all. The TCP meter showing GR is NOT
-- proof the API can read it — REAPER may draw the meter internally
-- without publishing a readable named-config-parm.
--
-- Use:
--   1. Select the track hosting The Analog Molecule Deluxe.
--   2. Make sure its compressor is ACTIVE (Ch/Bus Comp Active = On,
--      not bypassed) and the topology/mode actually drives GR.
--   3. Start playback of a loud, sustained signal so the comp is
--      really reducing gain.
--   4. Actions -> run this script -> watch the REAPER console.
--
-- A nonzero "max" for any parm = that FX's GR IS readable via the API
-- (Rea-Sixty's walk can use it). All zero / no answer = the GR is
-- display-only and we need a different source (gr_probe JSFX).

local CANDIDATES = {
  "GainReduction_dB",   -- PreSonus standard (VST/VST3, ReaComp)
  "GainReduction",
  "Gain_Reduction_dB",
  "GR_dB",
  "GR",
  "gain_reduction",
}

local tr = reaper.GetSelectedTrack(0, 0)
if not tr then
  reaper.ShowMessageBox("Select the track hosting the plugin under test.",
                        "probe_molecule_gr", 0)
  return
end

local nFx = reaper.TrackFX_GetCount(tr)
-- maxVal[fx][parm] = largest |value| seen
local maxVal = {}
for fx = 0, nFx - 1 do maxVal[fx] = {} end

local DURATION = 2.0          -- seconds to poll
local t0 = reaper.time_precise()

local function poll()
  for fx = 0, nFx - 1 do
    for _, name in ipairs(CANDIDATES) do
      local ok, val = reaper.TrackFX_GetNamedConfigParm(tr, fx, name)
      if ok and val and val ~= "" then
        local num = tonumber(val) or 0.0
        if num < 0 then num = -num end
        local prev = maxVal[fx][name]
        if prev == nil or num > prev then maxVal[fx][name] = num end
      end
    end
  end
  if reaper.time_precise() - t0 < DURATION then
    reaper.defer(poll)
  else
    report()
  end
end

function report()
  reaper.ShowConsoleMsg("\n=== probe_molecule_gr (max over " ..
                        DURATION .. "s) ===\n")
  for fx = 0, nFx - 1 do
    local _, fxName = reaper.TrackFX_GetFXName(tr, fx, "")
    reaper.ShowConsoleMsg(string.format("\n[%d] %s\n", fx, fxName))
    local any = false
    for _, name in ipairs(CANDIDATES) do
      local v = maxVal[fx][name]
      if v ~= nil then
        reaper.ShowConsoleMsg(string.format(
          "  %-20s  max=%.3f  %s\n", name, v,
          v > 0.01 and "<-- READABLE" or "(answered, but stayed ~0)"))
        any = true
      end
    end
    if not any then
      reaper.ShowConsoleMsg("  (no GR parm answered)\n")
    end
  end
  reaper.ShowConsoleMsg("=== done ===\n")
end

reaper.ShowConsoleMsg("\nPolling for " .. DURATION ..
                      "s -- keep audio playing with the comp reducing...\n")
poll()
