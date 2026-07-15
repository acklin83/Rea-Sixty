# HANDOFF — UF1 goniometer black + red Analogue numbers. Next: Windows USBPcap session 2026-07-17

**Status 2026-07-15, end of evening session (Fable 5).** The goniometer is
STILL black and the Analogue numbers STILL render red, after the OUT-side was
equalized against SSL byte-for-byte at every level we can measure from the Mac.
**Frank has scheduled a Wireshark/USBPcap session on the Windows box (StoerPC)
for 2026-07-17. The plan for that session is at the bottom — it is the next
step, and it is decisive.**

---

## The ground truth that anchors everything: cap101

`captures/cap101_ssl360_win_meterpro_overview_coldstart.pcap(.md)` — SSL 360
cold start on StoerPC, Frank's own MeterPro, **goniometer verifiably drawing
(Frank watched it during the capture)**. The ONLY capture in the repo that runs
from cold start to a working goniometer. Every equalization below is measured
against it. USBPcap3, UF1 = device 14. Claude-driven; the capture workflow
(announce → ok → capture) worked well.

Also new: `captures/cap102_our_stream_equalized_full_init.log.gz` — OUR
equalized stream (build `6f6a9d1`) incl. the complete traced init. The
reference for "what we send" going into the Windows session.

## Equalized TONIGHT — all verified on the wire, all still black

Everything in this table is MEASURED as now-identical to cap101 (or to SSL's
behaviour) and did NOT fix the face. Do not re-investigate these.

| # | difference found | fix | commit |
|---|---|---|---|
| 1 | SSL sends a full **FTDI/D2XX vendor init on EP0** at connect (reset ×2 + modem-status ×2 @150 ms, 8N1, flow none, baud 0xc068/0x0200 ×3, latency 2 ms, purge RX ×6, TX ×1). We sent zero control transfers. EP0 is invisible to every bulk-level parser — that is why all older diffs read "identical". | replicated verbatim in `UF1Device::open()`; traced as `C` lines, all rc=0, modem status = 3260 like SSL's | `2bf2789` |
| 2 | FF1B keepalive landed inside ~11% of image bursts; SSL never interleaves | `sendBurst()` (atomic enqueue) + keepalive defers while queue non-empty; worker batch 64 | `2bf2789` |
| 3 | We painted timer-driven at ~33 Hz with stale-image repeats and 011c BEFORE the image; SSL paints data-driven at ~24.5 Hz, sends NOTHING between images, cycle order `img34..0 0009 000a 0015 0016 011c 0125 0126 0127 0128 011d` | slot seq counter in the impersonator; one atomic burst per fresh t10, exact cap101 order | `a455e29` |
| 4 | Cycle values: cap101 streams **0009=ffff0000, 0015=ff, 0016=ff** (1469/1469 cycles); we sent cap75's 00 (foreign session state). 0128 is LIVE on Overview (0a/0f with hot audio) | cap101 values + live overload mask (BarPeak→VuPpm f5/f6) | `bfae7a3` |
| 5 | Our entry burst ended with an image-less 011d commit; SSL's first 011d comes only after the first image cycle. Entry also wrote the 0009 group (SSL doesn't) | entry = cap101's group (…0129=ff, 011f=00, 0125/26/27=(0,0), 0128) with NO trailing 011d | `bfae7a3` |
| 6 | SSL's very first bulk frame after EP0 = **256 zero bytes** (parser flush; cap84's copy had been dismissed as "descriptor artifact" — the init generator's all-zero filter is WHY). We never sent it | sent first in `runInit_` | `ef516f6` |
| 7 | **0x000d=01** (contradicting its own source comment; cap84 AND cap101 = 03) and **0x011d seeded 0x19** (cap77's session value; SSL enters the meter path with 00) — in TWO writers: `runInit_` AND the channel branch of `uf1PaintChannel_`, which re-asserted the stale values on every `changed` | both writers fixed (000d=03, 011d=00) | `ef516f6`, `31f7f39` |
| 8 | Channel state: SSL holds the ff-state (0009=ffff0000, 0015/16=ff) + an EMPTY 0x011b; our cap66 replay left 0009=0000, 011b=0000 | channel branch writes the ff-state + empty 011b | `31f7f39` |
| 9 | Our whole boot was a **cap66 replay** — an init captured with the device in MCU mode (SSL 360's device-mode setting; nothing to do with Rea-Sixty's architecture) plus 10 overrides | init replay regenerated FROM CAP101 (306 frames incl. wake, LED sweep, connect groups, fader dance with preserved dwells); generator now takes dev/window CLI args | `d38afd2` |
| 10 | **FF54 00** — one-shot host→device write, acked by the device with FF55 00 in 2 ms, sent by SSL 1.7 s before the meter-view entry. In NO other capture — but cap66/84 never opened a meter view and cap75/76/89 are mid-session windows that cannot refute an earlier FF54. | sent after init; **device acks us with FF55 0055 exactly as it acked SSL** | `5340b86` |
| 11 | Trace-flag race: `setFrameTrace()` after `open()` randomly lost the whole init from traces ("did the init even run?" was unanswerable) | flag set before open | `6f6a9d1` |

**Result: still black. The Analogue number colours also did not change (red),
i.e. even the ff-writes to 0x0009/0x0015/0x0016 have no visible effect.**

## The two symptoms, and why they may be ONE

1. **Goniometer black** — image frames byte-perfect, device drains them
   (FTDI line-status busy pattern under our stream is identical to under
   SSL's: busy right after each burst, drained in ~4 ms), never renders them.
2. **Analogue numbers red** ("current" flashes red in sync with the overload
   LED, "peak" always red; SSL always white). Decoded far enough to know:
   our writes to the presumed colour elements (0009/0015/0016, unanimous
   ff-state in cap88/94/101) **have no visible effect** — while the overload
   LED (0128), needles (0125/0127) and text (011c) all render live.

Both smell like a **class of state the device accepts but does not apply for
us** — graphics/attribute state — while direct content (text, needle bytes,
LED bitmask) applies. If ONE gate controls "apply attribute/image state",
everything tonight would behave exactly as observed: every write equal,
nothing visible. What that gate is, is not visible in the Mac-side data —
hence the Windows session.

Note on cap80: its Analogue session had 0009 flickering 1e/1f and 0015/16=00
throughout — i.e. THAT session's numbers were red. Our red may simply be the
device's own default/current attribute state that we fail to overwrite (see
"inert writes" above), not something we ever set.

## Ruled out across today (do not redo)

- Bulk framing: byte-identical incl. checksums (reproduces SSL's on all 116663
  frames of cap89), chunk order 34→0, lengths 257/67, selector 04 00.
- Transport/flow control: device-internal drain identical under our stream vs
  SSL's (cap100/cap89 FTDI line-status analysis; busy median 2.7–4 ms after
  burst start, both sides).
- 0x011d as "commit we forgot": we send it 1:1 before every image, value 00,
  byte-identical (and its VALUE is view-state: 00 Overview / 11 channel idle).
- 0x0119: absent from cap101 entirely (MeterPro) — correctly omitted.
- 011c length: Overview cycle = 156 both sides (the 206 ones are channel view).
- Deployment doubts: build hash == installed hash, lsof shows REAPER mapping
  the UserPlugins dylib, trace proves the new code's frames on the wire.
- EP0, zero block, ff54 — see the table.

## OPEN anomalies (facts, no theory survives them all)

- ff-writes to 0009/0015/0016 change nothing visible tonight, but a session on
  2026-07-15 MORNING observed that writing ff DID recolour the readout (the
  "recolour incident", learnings #27). Same elements, opposite behaviour —
  unexplained. (Possibly the morning's change coincided with something else.)
- The "current" number flashes red in sync with the overload LED although we
  write 0009 only once (static) — so under OUR stream the flashing tint is
  firmware-driven (from 0128?); SSL streams the same 0128 bits and stays white.

## ▶▶ THE PLAN: Windows USBPcap session 2026-07-17

**Goal: capture OUR OWN equalized build driving the UF1 on StoerPC — the same
box, same USB stack, same USBPcap that produced cap101 — and diff at USB level
against cap101.** This is the first comparison with NO infrastructure variable:
any remaining difference (URB sizes/packetization, EP0 details, timing, IN-side
behaviour, something USBPcap sees that our trace cannot) becomes visible.

Prep (can be done before Frank is needed):
1. **Windows build of branch `uf1-native-build`** (commit ≥ `6f6a9d1`). CI
   builds Windows; see `.local-docs/release-process.md` and
   [[intel-mac-reapack-pipeline]]/[[release-runbook-pointer]] for the pipeline.
   No release needed — just the artifact.
2. Deploy to StoerPC: `sshpass -p claudepass ssh claude@192.168.177.197`, copy
   into **`C:\Users\sunny\AppData\Roaming\REAPER\UserPlugins\`** (sunny, NOT
   claude — see [[windows-debug-ssh]], a full session was burned on that once).
3. UF1 physically on StoerPC, SSL 360 QUIT there (our extension claims the
   device). REAPER (sunny) with a MeterPro track.

Session (Claude drives, capture workflow rules apply — announce → ok → act):
4. Probe which USBPcap interface has the UF1 (it moves between boots; it was
   USBPcap3/dev14 for cap101 — VERIFY, don't assume; learnings #7).
5. Capture window (single SSH invocation, Start-Process → Sleep → Stop): cold
   REAPER start → meter view → Overview → audio ~30 s. Frank confirms what the
   face shows.
6. Pull as cap103, then diff against cap101 (SSL) mechanically, in this order:
   a. EP0: full control-transfer sequence incl. stages and timing.
   b. Enumeration/URB level: `usb.data_len` distribution per endpoint —
      packetization differences between libusb-on-Windows(?) and D2XX. NOTE:
      our Windows build's USB path may differ from macOS libusb — check what
      the Windows build actually uses before assuming.
   c. Bulk OUT: ordered frame diff (the tooling from tonight: element
      sequences, cycle structure, per-element full payloads).
   d. **IN side**: everything beyond 3260/3200 status; and whether the status
      cadence/latency-timer behaviour differs.
   e. Timing: burst pacing, inter-frame gaps, keepalive placement.
7. If the Windows capture of OUR build shows the goniometer DRAWING on StoerPC:
   the remaining variable is the Mac USB path (libusb/macOS packetization,
   endpoint config, speed) — then capture/compare at that layer becomes the
   focus. If it stays black on StoerPC too: the diff in (6) contains the cause,
   because infrastructure is now identical.

Analysis tooling from tonight (all in scratchpad-style python via tshark, easy
to recreate): vocabulary+count diff, ordered cycle print, full-payload
last-state diff at first-image time (mind the earlier bug: filter windows by
TIME, not by an img34 marker that grep already removed), FTDI line-status
correlation. cap101 extraction commands are in the .md files.

## Tooling notes
- Frame trace: `REASIXTY_UF1_TRACE=1`, launch REAPER YOURSELF (env var), flag
  is now set before open() so the trace contains the full init incl. EP0 'C'
  lines. ~25 MB/session with meter use; delete after, park keepers in captures/.
- The init generator: `python3 analysis/gen_uf1_init_sequence.py <pcap> <dev>
  <t_start> <burst_end> <t_end>` — regenerates `uf1_init_sequence.inc`.
- StoerPC: see [[windows-debug-ssh]] (claude@192.168.177.197, USBPcapCMD
  pattern, single-invocation start/stop; REAPER deploys go to sunny).

## Method lessons tonight (also folded into learnings.md)
- A "we compared everything" claim must enumerate TRANSFER TYPES first: EP0 was
  invisible to every bulk parser for weeks (learnings #28).
- When correcting an element value, grep for ALL writers — the channel painter
  re-asserted what the init fix had just corrected (#7 in the table).
- The all-zero "descriptor artifact" was a real SSL frame; a generator's
  convenience filter became a protocol misbelief.
- Time-window state diffs, not marker-based, when the extraction already
  filtered the marker frames (the 0404-state red herring cost 20 minutes).
