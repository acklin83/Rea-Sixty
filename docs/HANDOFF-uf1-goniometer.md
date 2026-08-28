# HANDOFF — UF1 goniometer. Codec DECODED, live render still unverified.

**Status 2026-07-17, end of the StoerPC session.** The evening produced the two
breakthroughs that had blocked everything — the image CODEC and the cycle
ANATOMY — plus two real live-path bugs (wrong plug-in instance, entry re-fire).
The goniometer has NOT yet been seen drawing from the live path: the final
build (all fixes combined) never got a test run with confirmed audio + the
meter view up. **That single test is the first action of the next session.**

---

## ★ PROVEN FACTS (do not re-derive, do not doubt)

### 1. THE IMAGE CODEC — 4 bits per pixel, no resampling
The 8560-byte 0x0122 image is **the plug-in's t10 diamond at 4bpp**: 17113
cells → 17120 nibbles → 8560 bytes (7 zero pad nibbles at the tail), high
nibble first, packed continuously across row boundaries. Proof: cap101's own
transmitted images decode into a flawless symmetric cloud over the 185-row
odd-diamond source geometry (ASCII render in the session log). The old model —
8-bit pixels resampled onto an invented 187-row table (`2,1,1,2..91,92,92,
91..2,1,1`) — was CIRCULAR (verified only against our own renderer) and its
output was nibble-space garbage. Bisection proof on hardware: the full-session
blast (verbatim cap101) **draws animated**; blast-C (same session, ONLY the
image payloads replaced with our old 8-bit images, checksums valid) **stays
black**. Implemented in `uf1PaintGoniometer_` (commit `history: fix(uf1): THE
goniometer codec`).

### 2. THE CYCLE ANATOMY — three sub-slots with render windows
cap101's 40.78 ms Overview cycle, measured offsets: **image chunks 0..4.7 ms →
SILENCE → meters group (0009 000a 0015 0016) floating at ~18 ms → silence →
end group (011c, 0125-0127, 0128, 011d) at ~40.3 ms → next image 0.06 ms
later.** The silences are the device's render windows. Implemented: dedicated
pacer thread (`uf1CyclePacerLoop_`) at 40.8 ms with sub-slot sleeps; REAPER's
~33 ms timer cannot hold this cadence (measured 62-97 ms gaps), hence the
thread. The channel view streams the idle cycle (0009..0016, 011c, 011d) so
the cycle chain never breaks — SSL never stops it (cap84, cap101 pre-entry).

### 3. Live-path bugs found ON THE WIRE and fixed
- **Wrong plug-in instance**: two MeterPro instances stream with equal
  diligence — one music, one floor values — and getMeter took the first in
  PORT ORDER: the silent one, all evening (sslcore trace: src 65118 silence,
  src 65119 t10 with 9130 lit cells). **Frank called the instance question
  hours before the trace proved it.** Fixed: instances track `lastLiveMs`
  (any level meter > −120 dBFS); getMeter/getOverload prefer live-within-2s.
  The UF1-side instance SELECTOR (SSL's top V-Pot on the meter view) is still
  an open feature.
- **Entry re-fire**: the meter entry burst fired on `tr != sTr` (last-touched-
  track flips during playback) — a mid-stream layout reset SSL never sends
  (cap106: 2× in 30 s sitting still). Now gated on view/screen change only.
- Entry cleanup: the lone zeroed chunk-34 0x0122 write + duplicated extras
  are gone (cap101's entry sends no 0x0122 before the first real burst).

### 4. Windows infrastructure (StoerPC)
- **WinUSB bound to the UF1** (PID_0025) via `install_winusb_v3.ps1` (extends
  the May installer; INF now lists PID_0025; svc=WinUSB verified). SSL 360 on
  the box cannot see the UF1 until its driver is reinstalled. **The in-repo
  INF (`extension/resources/rea_sixty_winusb.inf`) + the Settings-button
  installer still lack PID_0025 — TODO.**
- Windows builds: `ssh claude@$STOERPC` (the IP is DHCP and moves — resolve it, see `.local-docs/release-process.md`), repo `C:\Users\claude\
  reaper-uf8`, `build_rea.bat`; deploy to **sunny**'s UserPlugins. REAPER must
  be closed (DLL lock — a silently-failed copy cost a round; ALWAYS verify
  deployed timestamp == build timestamp).
- `ssl_core=1` + `ssl_core_trace=1` set in sunny's reaper-extstate.ini. All
  diagnostic logs now go through `uf8::logPath` (%TEMP% on Windows) — three
  separate literal-`/tmp` bugs ate logs this session.
- MSVC min/max macro trap hit again (gonio geometry std::min) — fixed.

## Diagnostic tooling built tonight (all still in the tree / on the box)
- **Full-session blast**: `uf1_blast.bin` in the log dir → `runInit_` replays
  it verbatim ([u32 dt_us][u16 len][bytes]) with exclusive wire (worker drops
  user frames + keepalives). THE decisive instrument: byte- and timing-
  faithful SSL from our process. Generator snippets in the session; blast
  variants A/B/C in the scratchpad method: splice cap101 windows/payloads.
- **Image replay**: `<resource>/rea_sixty/gonio_replay.bin` (N×8560 raw
  images) → Overview cycle streams them instead of live rendering.
- capX pcaps: cap101 (SSL ground truth), cap103/104 (our old codec),
  cap105-109 (live-path iterations). On the box under C:\Users\claude\.

## ▶▶ NEXT SESSION — first actions
1. **The one test that never ran**: current build (codec + anatomy + instance
   preference + entry gate), REAPER on StoerPC or Mac, MeterPro with SIGNAL
   (open the plug-in GUI and see it draw first!), Meter → Overview, audio
   running — look at the UF1. Verify on the wire in parallel (15 s capture:
   images must show thousands of lit NIBBLES; cap109's check script).
2. If STILL black with confirmed live nibbles on the wire: diff the live
   stream against the blast at the remaining margins (keepalive placement
   inside the cycle, 011c content, intra-trailer micro-gaps) — or extend the
   blast bisection: splice OUR pacer's exact output into the blast timing.
3. Then: port the fixes' remaining TODOs — Analogue/RTA cycle chains (only
   channel + Overview stream continuously so far), UF1 instance selector,
   in-repo INF + installer PID_0025, LissajousFadeTime (SSL's smooth value
   continuum suggests a fade accumulator before quantising to nibbles —
   cosmetic, do after first light).

## Session lessons (fed into learnings.md)
- The instance question came from Frank at 17:00 and was proven right at
  21:00 — a user's structural question outranks another byte-level diff.
- Wrongly told Frank his instances had no signal when OUR code read the
  silent one — the accusation rule (#13) applies to framing, not just intent.
- Three separate literal-`/tmp` log paths ate every diagnostic on Windows.
- Circular verification (decode with the same table you encoded with) proves
  nothing — the codec survived four days because of it.
