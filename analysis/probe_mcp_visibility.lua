-- probe_mcp_visibility.lua
-- Frank scrollt den REAPER-Mixer so, dass einige Tracks links UND rechts
-- aus dem sichtbaren Ausschnitt fallen, dann diese Action ausführen.
-- Ziel: herausfinden, was I_MCPX / I_MCPW für off-screen-Tracks melden,
-- damit followSelectedInMixer() den exakten Sichtbarkeits-Vergleich kriegt.

local n = reaper.CountTracks(0)
local lm = reaper.GetMixerScroll()
local lmNum = lm and math.floor(reaper.GetMediaTrackInfo_Value(lm, "IP_TRACKNUMBER")) or -1

local lines = {}
lines[#lines+1] = string.format("=== MCP visibility probe ===")
lines[#lines+1] = string.format("GetMixerScroll() leftmost = track #%d", lmNum)
lines[#lines+1] = string.format("%-6s %-8s %-8s %-8s %-6s", "trk#", "MCPX", "MCPW", "MCPH", "sel")
for i = 0, n - 1 do
  local tr  = reaper.GetTrack(0, i)
  local x   = reaper.GetMediaTrackInfo_Value(tr, "I_MCPX")
  local w   = reaper.GetMediaTrackInfo_Value(tr, "I_MCPW")
  local h   = reaper.GetMediaTrackInfo_Value(tr, "I_MCPH")
  local sel = reaper.GetMediaTrackInfo_Value(tr, "I_SELECTED")
  local inmix = reaper.GetMediaTrackInfo_Value(tr, "B_SHOWINMIXER")
  lines[#lines+1] = string.format("%-6d %-8d %-8d %-8d %-6d showinmix=%d",
    i + 1, math.floor(x), math.floor(w), math.floor(h), math.floor(sel), math.floor(inmix))
end
reaper.ShowConsoleMsg(table.concat(lines, "\n") .. "\n\n")
