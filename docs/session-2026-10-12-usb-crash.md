# Session 12.10.2026: USB dropouts, REAPER crash, memory growth

macOS 27.0.1 (26A434), REAPER 7.82. Diagnosis only, no code change.

## USB dropouts (UF8 + UC1)
- Kernel: UF8 and UC1 "transaction error" in the same millisecond (15:22:07); later the
  UC1 re-enumerated as a whole device six times in ~75 s, each time taking the UF8 down.
- Topology: external USB2.0 hub, port 2 = UF8 (internal Microchip 0x0424:0x2422 hub) with
  the UC1 daisy-chained to it; port 3 = UF1 (unaffected).
- Rea-Sixty only reacted (reopen); it resets the UC1 only on a handshake PIPE, not logged.
- UC1 moved to another hub/controller at 17:09: no dropout since.
- macOS 27 does not show the kernel's IOUSBHost errors any more; device changes are in
  `icdd` / `usbpowerd` lines. `/usr/bin/log show` over minutes is expensive (load ~22).

## Crashes
- 21 crashes 06.-10.10. in `CSurf_SetSurfaceMute` (0x50 = SetSurfaceMute vtable slot of a
  deleted surface): all at quit, triggered by `reaper_playhead.dylib` loading a theme while
  unloading. Not Rea-Sixty, not CSI.
- 12.10. 17:31 and 18:51: SIGSEGV at 0x17d in Archetype Nolly X 1.0.2 (take FX), identical
  offsets both times. Reproducible: razor edit with the mouse during playback on items with
  Nolly as take FX crashes every time; without Nolly on the item it does not (Frank, 19:0x).
  A bug in Nolly, not memory exhaustion (18:51 at 7.9 of 36 GB; the `mach_vm_allocate_kernel`
  triage line also shows up in unrelated quit crashes). Frank renders the Nolly track.

## Solved: REAPER memory grew ~50-170 MB/min (not Rea-Sixty)
- Grew without the Rea-Sixty dylib loaded as well (18:01).
- `sample`: `ProcessFxMsgQ > DoUndoStateChange > SaveState` spends its time in SSL Meter Pro's
  getState. Meter Pro sits in the monitoring FX, saved to `reaper-hwoutfx.ini`: 57 MB, of it
  56 MB Meter Pro, rewritten every ~5 s during playback (34 GB written in 2 h).
- Meter Pro's loudness log file was on, so its whole log lives in the plug-in state. Log off:
  state 57.5 MB -> 1.28 MB, growth ~170 MB/min -> ~8 MB/min (19:14-19:18, playing). Meter Pro
  still reports a state change every ~5 s during playback, harmless at 1.28 MB.
- lldb on this project is not an option: Sonsig Rev-A's copy protection stops the debugger.
