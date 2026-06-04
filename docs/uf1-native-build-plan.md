# UF1 — Decode Reference + Native Build Plan

One-page capstone after the 2026-06-04 decode session (cap52–83). Pairs with the byte-level
detail in `docs/protocol-notes-uf1.md` and the feature map in `docs/uf1-plugin-mode-gap-analysis.md`.

---

## 1. Protocol reference (everything decoded)

### USB / framing
- UF1 = VID_31E9 PID_0025 "SSL Control I/F". On Windows it re-enumerated to USBPcap dev 11→12
  (device number changes on SSL 360 restart). Internal hub also exposes PID_0022 HID composite =
  **unused by any control** (ignore it).
- Endpoints (bulk): **0x02 OUT** (screen + LEDs + keepalive), **0x81 IN** (controls).
- **Frame:** `FF <op> <len> <len-bytes…> <ck>`, total = `len + 4`. For screen frames the first two
  len-bytes are a 16-bit element address. **URBs concatenate multiple frames — split on len.**
- **Checksum:** `ck = (sum(bytes from FF opcode .. last data byte) + 1) & 0xFF`.
- **IN stream:** `32 60` prefix, then concatenated `FF…` frames. Bare `32 60` = idle poll.
- **Keepalive:** `FF 1B 01 …` (same as UF8 — must keep sending).

### INPUT (read, EP 0x81)
| Control | Frame |
|---------|-------|
| Fader touch | `FF 20 02 00 …` |
| Fader position | `FF 21 03 00 <lo> <hi>` — 15-bit LE, 0x0000 bottom … 0x7FFF top |
| Encoder rotate | `FF 24 02 <id> <delta>` — delta 6-bit signed, **CW +**, CCW − (0x3F=−1) |
| Encoder touch | `FF 23 02 <id> <state>` (v-pots + channel enc are touch-sensitive) |
| Button / push | `FF 22 03 <id> 00 <state>` (01 down / 00 up) |

Encoder ids (rotate/touch): v-pots 0x00–0x04 (above-fader + 1–4), channel enc 0x05, jog 0x06.
Push ids: v-pot pushes 0x08–0x0C, channel-enc push 0x0D.
Button ids: 0x18 ch-softkey, 0x19–0x1C display soft-keys, 0x1D/1E/1F Solo/Cut/Sel, 0x20–0x27 nav
block (Mode/5-8/←/→/BankL/BankR/360/Scrub), 0x28–0x2C NAV cross, 0x30–0x36 transport soft-keys,
0x38–0x3E Flip/Master/RWD/FFW/Stop/Play/Rec.

### OUTPUT (write, EP 0x02)
| Element | Frame |
|---------|-------|
| Fader motor engage/release | `FF 1D 02 00 01` / `…00` |
| Fader motor position | `FF 1E 03 00 <lo> <hi>` — same 15-bit LE as read |
| LED on/off | `FF 3B 03 <led_id> 00 <01\|00>` |
| **Colour (any element)** | `FF 38 04 <id> 00 <XX> <YY>` — `XX=(G4<<4)\|R4`, `YY=0xF0\|B4` (4-bit G·R·B; gamma-ish) |
| Screen element | `FF 67 <len> <addrHi> <addrLo> <payload> <ck>` (write `len-2` payload bytes to 16-bit addr) |

LED ids: Solo 0x05, Cut 0x06, Sel 0x07 (more in init sweep). Colour ids include SEL 0x07, EQ graph 0x03.

### SCREEN element addresses (FF 67)
- Channel-info zone (`0x00xx`): `0x000b` TrkNam · `0x000c` output dB · `0x000e` Pan label+value ·
  `0x000f` Pan bar · `0x0017` CS TYPE · `0x0014` ch# · **GR meter `0x0009` comp / `0x000a` gate**.
- Channel-strip large LCD (`0x01xx`): `0x0104` soft-key label · `0x010e` focused param text (name+value) ·
  `0x010f` 4 V-pot readout bars (bytes 0/2/4/6) · `0x0122` EQ graph (251-col height array, 0 dB=0x64) ·
  `0x011c` header row (8×25-byte fields: device/page N·8/state; or timecode placeholder).
- Meter mode (`0x0104` = mode label + Reset/Fine/Presets; Loudness SK3 = Play/Pause):
  - Overview: `0x011c` T-PEAK/RMS · `0x0122` Lissajous scope · `0x0126` balance · `0x0127` correlation
  - Analogue: `0x0125` VU needle L · `0x0127` VU needle R · `0x011c` current/max
  - RTA: `0x0122` 31-band spectrum (64 B)
  - Loudness: `0x011c` Integrated LKFS + LRA · `0x000c` momentary · `0x0122` loudness-history graph
- **Timecode:** `0x0119` (11 B, per-digit 7-segment; 0x0C='1', 0x7F/7E≈'0'). **MCU layer only**
  (empty in Plugin Mode — comes via HUI/MCU, not the 360° protocol).

### Init handshake (cap66 — must replay to drive natively)
Wake opcodes (`FF 01/02/05/4B/4E/47/4F/2D/1F/1E/1D`, zero-payload) → per-LED init sweep
(`FF 38`+`FF 39`+`FF 3B` per led id) → `FF 67` screen paint → steady-state (keepalive loop).
Full byte stream preserved in `captures/cap66_uf1_init.pcapng`.

### Polish remaining (not blocking)
Exact byte→value scaling for: EQ dB→height curve, meter graphics (scope/VU/RTA/loudness pixels),
TC 7-seg→digit map, FF38 colour gamma. All derivable via controlled level/value sweeps if/when needed.

---

## 2. Native build plan

### Platform order — **Mac first** (our standard starting point, and safest for UF1)
On macOS, libusb claims the UF1 directly — **no driver swap, no WinUSB, no sslbus conflict**
(the Windows WinUSB rebind is what broke the UF1 on 2026-06-03; avoid it for initial bring-up).
Connect the UF1 to the Mac, claim PID_0025, replay the init, drive it. Port to Windows after
(WinUSB per [[winusb-installer-deletes-sslbus]]: force-bind ONLY 0021/0023/0025, never delete sslbus).
Claiming the UF1 takes it from SSL 360 (expected, same as UF8/UC1).

### Architecture — mirror UF8Surface
- New `UF1Surface` in the rea-sixty extension, modeled on the existing UF8/UC1 surface classes.
  Reuse the libusb plumbing, the input-queue → main-thread dispatch, and the bindings system.
- **Threading (hard rule, see [[feedback-reaper-api-input-thread]]):** the libusb read callback may
  only do atomic stores / cached reads. ANY REAPER API or ImGui call goes through `queueInput()` and
  is dispatched on the timer (main thread). Same for screen writes triggered by REAPER state.
- Open device, run the **init replay** (cap66 sequence) before any control/LED/screen frame, keep
  the `FF 1B 01` keepalive loop alive.

### Bring-up order (each a small, testable step)
1. **Read first:** parse the IN stream (split frames, verify checksum), log fader/encoder/button
   events. Confirms claim + parsing on real hardware. Low risk.
2. **Fader:** map 15-bit position ↔ REAPER track volume; drive motor (`FF 1D`/`FF 1E`) on track/
   selection change; handle touch (FF 20) to suspend motor while gripped (UF8 motor-echo lessons apply).
3. **Buttons/encoders → REAPER** via the bindings layer (Solo/Cut/Sel, transport, nav, v-pots).
4. **LEDs + colour:** `FF 3B` on/off + `FF 38` GRB colour (Solo/Cut/Sel; SEL = track colour).
5. **Screen:** header (`0x011c`), channel-info zone (TrkNam/dB/Pan/CS-type), focused param (`0x010e`),
   V-pot bars (`0x010f`). Build a small frame-emitter keyed by element address.
6. **EQ graph / meters:** self-render (compute heights/bars from REAPER + plugin data → emit to
   `0x0122`), since SSL's exact pixel codec is type-specific. Lowest priority.

### First milestone to prove the path
Mac: claim UF1 → replay init → **send one colour to SEL** (`FF 38 04 07 00 <GRB>`) and **read a
fader move**. That single round-trip proves claim + init + send + read end-to-end — the thing we
couldn't do mid-decode because SSL 360 owned the device.
