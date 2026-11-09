# Session 09.11.2026: UF1 Meter view strip, v0.9.2 "Left Side Story"

## Built
- **UF1 Meter view:** the fader and its small display run on as in the channel view (`d2652a89`). The gate from `fcfcfed8` (11.09., plane held still to match SSL 360's wire) is gone; Frank: the Meter view is the right half, the fader the left. Two intermediate steps the same morning (`29caca2a`, `2109e25e`) are superseded.
- The red readout on the Analogue screen is the firmware's own tint at high level (settled 12.09., cap138) and unrelated to this plane.

## Measured and dropped
- Probe `e13c899d` (removed `87c922de`): REAPER start loads the empty project before the surface exists, handover after ~1 s. The 6 s hang Frank saw came from a project opened by hand 25 s later; REAPER's timer stood 4.8 s during that load. Waiting with the handover does not help there, so the planned v0.9.2 change (hand over after project load) is dropped. Frank keeps it as it is.
- Crash while opening a .rpp by double-click: Sonsig Rev-A 1.1.2 (VST3), null pointer in `CDSPResamplerDual::Reset` during `SetNch` in `FxChain::LoadState`. No Rea-Sixty frame in the crashing thread.

## Release v0.9.2
Tag on `9cb52060`, CI tag run 42599950689 green, Mac arm64 + Intel notarised, GitHub release with 16 assets, ReaPack 0.9.2 (12 download URLs 200), website merged (`bcbcce52`), deployed by Frank, 9 routes 200, manual v0.9.2.

## Open
- MoreMe changes after ORC 1.6.3 go out with the next ORC version.
- The case "REAPER opens the .rpp itself at start" was never measured.
