# UF1 Meter view — handoff (branch `uf1-native-build`, 2026-07-14)

State of the UF1 Meter/Analyzer work when the branch was parked. Everything below
is backed by a capture, a commit, or the param dump — nothing is assumed. Read
this before touching the UF1 meter path again.

## Where it stands

The in-extension **SSL 360°Core impersonator works end-to-end**: the SSL Meter
(Pro) plug-in connects to us and streams its real meter values over UDP,
continuously and without reconnects (`2d8ad8d`, `d46c984`, `6315e6d`, `3f07c93`).
DataTypes t0–t7 and t10–t24 arrive and match the plug-in GUI. **t8/t9 (RTA) do
not** — see below.

The meter codecs are decoded and documented in `captures/cap88..cap95` + their
`.md` siblings: Analogue VU/PPM needles, Overview bargraphs, RTA byte law,
goniometer diamond geometry, Loudness structure.

## Shipped this session — `9b2f31d`

**Meter-view V-Pot operator layer.** In Meter view the 4 V-Pots operate the SSL
Meter (Pro) plug-in on the focused track:

- V-Pot 1 = Meter plug-in **instance select**
- V-Pot 2/3/4 = "Parameter Control 1/2/3" for the current screen + page
- **Display soft-key 2** cycles the V-Pot **page** (Overview 2, Analogue 2, RTA 3);
  Display soft-key 1 keeps cycling the screen and resets the page to 0.

Assignments come from the **UF1 User Guide Rev4.0, printed pages 189–191**,
cross-referenced against `docs/ssl-native-params/VST3__SSL_Meter_Pro_(SSL).md`.
The table is `kUf1MeterVPots` in `main.cpp`; every entry is a real param index.

Notable corrections the manual forced on the earlier plan:

- The UF1 has **3 meter screens only** (Overview / Analogue / RTA). There is **no
  Loudness screen** on the hardware in Rev4.0, even with Meter Pro loaded.
- Each screen has **multiple pages** of V-Pot assignments; the earlier plan only
  described page 1 and had the RTA order wrong (page 1 is Select Frequency /
  Scale Top / Scale Bottom).

**Analogue VU reference is now live.** The needle previously hardcoded a −18 dBFS
reference. It now reads the plug-in's **Analogue Reference Level (param 9)** every
paint — `refDbFS = -36 + norm*36` (dump: norm 0.0/0.5/1.0 = −36/−18/0 dBFS) — with
a −18 fallback when no Meter FX is on the track. So the needle follows the
reference whether it is set from the V-Pot or in the plug-in GUI. This was the
"ref/lineup nicht einstellbar" gap.

Detents accumulate against the encoder's ~4-counts-per-click granularity
(`kChannelEncoderScale`, grounded in cap58) so one physical click = exactly one
enum notch. Continuous params dial at `kUf1MeterContStep` (0.02/notch) — feel
only, HW-tunable, no correctness impact.

**Not verified on hardware.** It compiles, links and installs; the param indices
are ground truth; the behaviour is unverified.

## RTA — the earlier plan was wrong

The previous handoff said: *capture a cold-connect with 360° on the RTA screen to
grab RTA's subscribe object.* **That cannot work.** Established from
`/tmp/ssl_coldconnect.pcap` with the new tools
`analysis/ssl360-protobuf/enum_subscribe.py` and `dump_ctrl.py`:

- **The RTA data is already in the stream.** Port 16010 (the Meter Pro instance)
  streams `Rta31Band(8)` and `TextRta(9)` — 4680 messages each, 31/62 floats —
  alongside every type we already receive. 360° was on *Overview* at the time, so
  the screen does not gate it.
- **The plug-in explicitly declares RTA.** `type-17` control frames (plug-in→Core)
  carry `f2` = name, **`f5` = DataType**, `f6` = value count. The RTA ones are
  literally `"Rta Meter Data"` (f5=8, f6=31) and `"TextRta Meter Data"` (f5=9).
  Finding the RTA stream is a one-line match on `f5 == 8`.
- **Subscribe object ids are per-session random.** The real Core registered **59
  distinct objects** (`type-2`, 8-byte hashes) and sent **178 subscribes**
  (`type-18`). We hardcode **4** ids replayed from an older session — and **none of
  those 4 ids appear in this capture at all**. Replaying captured ids is
  architecturally dead: a captured RTA id is stale the next time the plug-in runs.
- `type-2` register = `[28-byte header][8-byte objId]` with an **empty** protobuf.
  `type-19` (`PluginMeterDataReference`, 4 of them) is Core→plug-in, scope string
  `PluginControls.PerSslMeterProPlugin`, and carries a **live UDP data port** in
  field 1 — Core assigns data ports dynamically.
- Our parser (`SslMeterProtocol.cpp`) and the store loop are **generic**: they
  would decode and store t8/t9 if a datagram arrived. So the frames simply never
  reach our socket — this is a subscribe problem, not a decode problem.

**The remaining blocker.** The type-17 prepare's own header id (off 20–27) does
*not* equal any type-18 subscribe object hash. The subscribe targets are
content-hashed Lgx property-VM node ids, and we have not found a frame that maps
"the RTA meter" to its subscribe hash. Since our stale (non-matching) ids still
yield t0–t7/t10–t24, those types evidently stream by default once *any* subscribe
arrives, while RTA appears to be opt-in per object.

**Next step — needs the live plug-in, not a new capture.** Use the existing
`ssl_core_probe` harness (`extension/tools/ssl_core_probe.cpp`, 360° **off**, a
Meter plug-in loaded) and iterate: subscribe to more/all advertised objects, or
vary the subscribe, and watch which one turns t8 on. Then implement the dynamic
subscribe in `SslCoreImpersonator.cpp` — parse incoming type-17 frames, match
`f5 == 8`, subscribe by that session's live id, and honour the type-19 port
assignment. Alternative (harder): reverse the property-path→hash scheme.

## Goniometer — blocked, do not guess

The diamond **geometry is fully decoded** (`analysis/uf1_gonio_decode.py` renders
SSL's own 0x0122 buffer: 35 chunks → 8560 bytes → 187 diamond rows, one intensity
byte per pixel).

What is missing is the **forward map**: plug-in `Lissajous(10)` data → that 8560-byte
diamond. It cannot be derived from what we have: in the cold-connect capture the
Lissajous payload is **chunked** (observed lengths 1500 / 613 / 5000 / 2113) and
**entirely zero** — no audio was playing. Any mapping invented against silence is
unverifiable.

To unblock, one capture is genuinely required: **plug-in protobuf Lissajous and the
UF1 USB 0x0122 frames recorded simultaneously**, with a known rotating-phase signal,
so the raster and the rendered diamond can be paired. Until then this stays unwired
— per the standing rule, do not guess it.

## Open / unverified on hardware

- "VU funktioniert nicht" — the live-reference fix may resolve it; needs a test with
  a build that is actually restarted into REAPER.
- "Peak-Lines fallen nicht" — with a **constant** tone hold == peak by construction,
  so the line never falls. Re-test with a varying signal before treating it as a bug.
- The whole V-Pot layer (`9b2f31d`) is unverified on hardware.

## Test setup

UF1 on the Mac; REAPER with SSL Meter (or Meter Pro) on a track; **SSL 360° must be
quit**; impersonator enabled via ExtState `rea_sixty/ssl_core=1`. Frame trace:
launch REAPER with `REASIXTY_UF1_TRACE=1` → `/tmp/reaper_uf1_frames.log` (OUT frames
are `FF 67 <len> <addrHi> <addrLo> <payload> <ck>`). Impersonator trace →
`/tmp/reaper_sslcore.log`.

## Pointers

- Memory: `HANDOFF-uf1-meter-live.md`, `uf1-vpot-operator-spec.md`,
  `uf1-meter-codec-decoded.md`, `ssl360-plugin-protobuf-protocol.md`
- Tools: `analysis/ssl360-protobuf/{decode_capture,enum_subscribe,dump_ctrl}.py`,
  `analysis/uf1_gonio_decode.py`
- Captures: `captures/cap88..cap95` + `.md` siblings; `/tmp/ssl_coldconnect.pcap`
