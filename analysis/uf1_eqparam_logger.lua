-- uf1_eqparam_logger.lua
-- Logs the focused FX's parameters (names once, then formatted values on every
-- change) with timestamps, so an offline tool can correlate plug-in param values
-- with SSL 360's captured 0x0122 EQ curves (USBPcap) by ORDER (+ time tiebreak).
--
-- Use (Windows REAPER hosting the SSL plug-in + SSL 360 driving the UF1):
--   1. Run this action. It writes to  <resource path>\uf1_eqparams.log
--      (REAPER: Options > Show REAPER resource path in explorer).
--   2. Focus the SSL plug-in on a track, then turn EVERY EQ param through its
--      range — one band at a time is cleanest. Switch flavours (EQ Type) too.
--      Pause briefly on distinct values (dwell) so each state is captured stable.
--   3. Run the action again to STOP (it toggles). Send me the .log file.
--
-- A NEW "FX:" header line is written whenever the focused FX changes (so I see
-- the plug-in name + param-name list). A "T:" line is written whenever ANY
-- formatted value changes.

local LOGPATH = reaper.GetResourcePath() .. "/uf1_eqparams.log"

-- toggle stop via ExtState
if reaper.GetExtState("uf1eqlog", "running") == "1" then
  reaper.SetExtState("uf1eqlog", "running", "0", false)
  reaper.ShowConsoleMsg("uf1_eqparam_logger: STOPPED\n")
  return
end
reaper.SetExtState("uf1eqlog", "running", "1", false)

local f = io.open(LOGPATH, "w")
if not f then
  reaper.ShowConsoleMsg("uf1_eqparam_logger: cannot open " .. LOGPATH .. "\n")
  return
end
f:write(string.format("# uf1_eqparam_logger start t0=%.6f\n", reaper.time_precise()))
f:flush()
reaper.ShowConsoleMsg("uf1_eqparam_logger: LOGGING -> " .. LOGPATH .. "\n(run the action again to stop)\n")

local lastFxKey = nil
local lastSnap  = nil

local function focusedTrackFx()
  local retval, trnum, itnum, fxnum = reaper.GetFocusedFX2()
  if not retval or (retval & 1) == 0 then return nil end          -- not a track FX
  if trnum <= 0 then return nil end
  local tr = reaper.GetTrack(0, trnum - 1)
  if not tr then return nil end
  local fx = fxnum & 0xFFFFFF
  if fx < 0 or fx >= reaper.TrackFX_GetCount(tr) then return nil end
  return tr, fx
end

local function loop()
  if reaper.GetExtState("uf1eqlog", "running") ~= "1" then
    f:write("# stopped\n"); f:close()
    return
  end

  local tr, fx = focusedTrackFx()
  if tr then
    local _, fxname = reaper.TrackFX_GetFXName(tr, fx, "")
    local n = reaper.TrackFX_GetNumParams(tr, fx)
    local fxKey = tostring(tr) .. ":" .. fx .. ":" .. fxname
    if fxKey ~= lastFxKey then
      lastFxKey = fxKey
      lastSnap = nil
      local names = {}
      for p = 0, n - 1 do
        local _, pn = reaper.TrackFX_GetParamName(tr, fx, p, "")
        names[#names + 1] = string.format("[%d]%s", p, pn)
      end
      f:write(string.format("FX: \"%s\" nparams=%d | %s\n", fxname, n, table.concat(names, " ; ")))
      f:flush()
    end
    -- snapshot formatted values
    local vals = {}
    for p = 0, n - 1 do
      local _, pv = reaper.TrackFX_GetFormattedParamValue(tr, fx, p, "")
      vals[#vals + 1] = string.format("[%d]%s", p, pv)
    end
    local snap = table.concat(vals, " ; ")
    if snap ~= lastSnap then
      lastSnap = snap
      f:write(string.format("T:%.6f | %s\n", reaper.time_precise(), snap))
      f:flush()
    end
  end

  reaper.defer(loop)
end

reaper.defer(loop)
