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
- 12.10. 17:31: SIGSEGV in Archetype Nolly X (take FX), with `mach_vm_allocate_kernel
  failed`; REAPER had 11 GB malloc'd. Nolly X is the first victim of exhausted memory.

## Open: REAPER memory grows ~80-150 MB/min
- Continues with Nolly X bypassed and offline; Undo History shows nothing unusual.
- 16:22-16:39: 8.6 GB written via `ProcessFxMsgQ > FxChain::DoUndoStateChange > SaveState`.
- CPU mostly SSL 4K E DSP including its socket writes to the 360 core (our impersonator).
- Next: test REAPER without Rea-Sixty (same project, measure `ps rss`), then look at the
  SslCore impersonator's buffers and other growing containers.
