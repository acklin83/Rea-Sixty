# SSL UF1 — Protocol Notes (reverse-engineering)

Started 2026-06-04. Subtractive decode: account for UF8-analogous controls first; the
colour screen is the leftover. Captures live in `captures/cap52+_uf1_*`. SSL 360 drives
the UF1 over SSLBUS while USBPcap (interface `\\.\USBPcap1`) records.

## USB topology
- UF1 chassis = internal Microchip hub (VID_0424 PID_2422) exposing TWO USB devices:
  - **PID_0025 "SSL Control I/F"** (serial UF1-009184) = the vendor protocol. **dev 11** on USBPcap1.
  - **PID_0022 HID composite** (mouse + keyboard + consumer control) = **dev 10** on USBPcap1.
    **Stayed SILENT through every labelled capture** (fader, all buttons, all encoders, jog,
    cap53–61). No UF1 control rides HID — everything is the PID_0025 vendor protocol. dev-10
    role still unknown (OS media keys? vestigial?); not needed for native driving.
- Both share parent hub `VID_0424&PID_2422\7&2ae38c01&0&1` → both inside the UF1.

## Endpoints (PID_0025 control IF) — all BULK
| EP   | Dir            | role                                   |
|------|----------------|----------------------------------------|
| 0x02 | OUT (host→UF1) | screen + LEDs + keepalive (FF 67 / FF 1B) |
| 0x81 | IN  (UF1→host) | control input (fader/buttons/encoder…)   |

Single OUT + single IN. **Colour screen is multiplexed onto 0x02 OUT**, not a separate
endpoint (same single-OUT design as UF8). Screen isolation = elimination-by-decoding +
audio-playback diff, NOT endpoint separation.

## Checksum (both directions, working model)
`ck = (sum(payload bytes from the FF opcode … last data byte) + 1) & 0xFF`
Verified on FF 21 fader-position frames (cap53). Assume same for other FF frames until disproven.

## IN endpoint 0x81 — frame format
```
32 60 | <FF-frame> | …
```
- `32 60` = constant 2-byte IN-report prefix. Bare `32 60` = idle poll (no event).
  `32 00` variant seen rarely — second byte may be a status/seq nibble (UNVERIFIED).

### Decoded IN frames
| Frame | Meaning | Notes |
|-------|---------|-------|
| `FF 20 02 00 …` | Fader **TOUCH** | analogous to UF8 FF 20 02 |
| `FF 21 03 00 <lo> <hi> <ck>` | Fader **POSITION** | 15-bit LE, 0x0000 bottom … 0x7FFF top; ~100 Hz while moving |

UF1 has ONE fader → the `00` after `FF 21 03` is presumably fader index 0.

### Button events (cap55, cap57)
`FF 22 03 <id> 00 <state>` — **same FF 22 03 opcode as UF8.** state 01=down, 00=up.
Button id map (growing):
| id | button |  | id | button |
|----|--------|--|----|--------|
| 0x08–0x0C | V-pot pushes (above-fader, 1..4) |  | 0x1D | Solo |
| 0x18 | Channel soft-key |  | 0x1E | Cut |
| 0x19 | Display soft-key 1 |  | 0x1F | Sel |
| 0x1A | Display soft-key 2 |  | 0x38 | Flip |
| 0x1B | Display soft-key 3 |  | 0x39 | Master |
| 0x1C | Display soft-key 4 |  | 0x20–0x27 | nav block (below) |

Nav-button block (cap59), ids 0x20–0x27:
| id | button |  | id | button |
|----|--------|--|----|--------|
| 0x20 | Mode |  | 0x21 | Bank Left |
| 0x22 | 5-8  |  | 0x23 | Bank Right |
| 0x24 | ← |  | 0x25 | 360 |
| 0x26 | → |  | 0x27 | Scrub |

NAV cross (cap60), ids 0x28–0x2C: Up=0x28, Left=0x29, Centre=0x2A, Right=0x2B, Down=0x2C.

Transport soft-keys (cap62), ids 0x30–0x36: Left=0x30, Right=0x31, Cycle=0x32, Click=0x33,
1=0x34, 2=0x35, Shift=0x36.

Flip/Master + transport (cap55/cap63), ids 0x38–0x3E: Flip=0x38, Master=0x39, RWD=0x3A,
FFW=0x3B, Stop=0x3C, Play=0x3D, Rec=0x3E.

### Full button id map (FF 22 03 <id>) — INPUT SIDE COMPLETE
```
0x08–0x0C  V-pot pushes (above-fader, 1..4)      0x0D  channel-encoder push
0x18       channel soft-key                      0x19–0x1C  display soft-keys 1–4
0x1D–0x1F  Solo / Cut / Sel
0x20–0x27  Mode,5-8,←,→ (even) / BankL,BankR,360,Scrub (odd)
0x28–0x2C  NAV cross: Up,Left,Centre,Right,Down
0x30–0x36  transport soft-keys: Left,Right,Cycle,Click,1,2,Shift
0x38–0x3E  Flip,Master,RWD,FFW,Stop,Play,Rec
```

**Parser note:** the IN payload after `32 60` is a byte-stream of concatenated `FF…` frames —
multiple frames can arrive in one URB (saw a fader FF21 coalesced with a button FF22 UP).
Split on the length byte, don't assume one-frame-per-read.

### V-pots / encoders (cap56) — 5 pots: above-fader + 1..4
| Function | frame | ids |
|----------|-------|-----|
| Rotation | `FF 24 02 <id> <delta> <ck>` | id 0x00–0x04 |
| Touch (capacitive) | `FF 23 02 <id> <state>` | id 0x00–0x04 (01/00) |
| Push | `FF 22 03 <id> 00 <state>` | id 0x08–0x0C |

id map: above-fader=0/push 0x08, vpot1=1/0x09, 2=2/0x0A, 3=3/0x0B, 4=4/0x0C,
**channel encoder=5/push 0x0D** (cap58), **jog wheel=6** (cap61, no push/touch).
**Delta** = 6-bit signed mod 0x40: **CW positive** (0x01=+1, 0x02=+2), **CCW negative**
(0x3F=-1, 0x3E=-2) — confirmed via jog CCW-then-CW (cap61). Relative, velocity-sensitive.
V-pots + channel encoder are touch-sensitive (FF23 brackets rotation); jog is not.
Channel encoder drives channel navigation (turning it moves the fader motor → FF21 read-echo).

### Decoded OUT frames (fader motor, cap54)
| Frame | Meaning | Notes |
|-------|---------|-------|
| `FF 1D 02 00 01` / `…00` | Motor **engage / release** | 01 each position tick, 00 on release |
| `FF 1E 03 00 <lo> <hi> <ck>` | Motor **POSITION** | 15-bit LE, same 0x0000…0x7FFF scale as FF21 |

**Fader fully decoded both directions.** Read = FF20/FF21 on 0x81; motor = FF1D/FF1E on 0x02.
Same 15-bit LE scale, same checksum. NOTE vs UF8: same concept, different opcodes + value scale
(UF8 = pitch-bend 14-bit, pb14_max=15583) → UF8 fader code not directly reusable.

### Button LEDs (OUT 0x02, cap55/cap64/cap65)
- `FF 39 04 <led_id> 00 <val>` = LED **brightness/colour** (the active control for toggling).
- `FF 3B 03 <led_id> 00 <state>` = on/off; role looser (initial enable?), FF39 carries the state.
- LED ids: **Solo=0x05, Cut=0x06, Sel=0x07** (cap64).
- **Brightness SOLVED by ground truth (cap65, Cut LED):** `val 0x00` = dim, `val 0x12` = bright red.
  Cut LED is never off — dim-red unmuted, bright-red muted.
- **Colour hypothesis (unconfirmed):** `val` low-nibble = colour index (Solo lit=0x11 green,
  Cut lit=0x12 red), high-nibble 0x1 = bright, 0x00 = dim. Confirm via a multi-colour LED sweep.
- Sel LED mirrors REAPER track selection (always lit on selected track).

## OUT endpoint 0x02 — idle steady-state loop (FF 67 family)
~25 Hz refresh loop, six frames repeating + FF 1B keepalive:
- `ff 67 06 00 09 ff ff 00 00 74`
- `ff 67 06 00 0a 00 00 00 00 77`
- `ff 67 03 00 15 ff 7e`
- `ff 67 03 00 16 ff 7f`
- `ff 67 ca 01 1c <202B ASCII payload> da`  ← **screen text row** (`2d`='-', `4f4646`="OFF")
- `ff 67 03 01 1d 11 99`
- `ff 1b 01 0x xx` keepalive (4 variants) — **same FF 1B 01 keepalive as UF8**

Frame structure (hypothesis): `FF 67 <len> <b3> <b4> <payload…> <ck>`, byte after 67 = length
(0x06, 0x03, 0xCA=202). Big FF 67 CA = one screen row carrying ASCII text + (likely) colour/
layout attributes → high-level draw commands, screen is decodable (not a raw framebuffer).

## Init handshake (cap66) — prerequisite for native output
SSL 360 cold-starts the UF1 with: **wake opcodes** (zero-payload `FF <op> 00 <ck>`: FF01, FF02,
FF05, FF4B, FF4E, FF47, FF4F, FF2D, FF1F, FF1E, FF1D) → **per-LED init sweep** (triplet
`FF 38 04 <id> 00 00 <v> <ck>` + `FF 39 04 <id> 00 00 <v> <ck>` + `FF 3B 03 <id> 00 <ck>` for
each LED id) → **FF 67 screen paint** → steady-state. Full stream in `cap66_uf1_init.pcapng`
(UF1 analog of UF8's 752-frame init replay).

**NEW LED opcode FF 38** pairs with FF39/FF3B in the init — LED writes may be FF38+FF39+FF3B
triplets. Canonical FF39 = `FF 39 04 <id> 00 00 <v> <ck>`; re-verify cap64/65 brightness offset.

**Operational:** restart SSL 360 only via SSL360Gui in Frank's desktop session (NOT SSL360Core
from SSH → session 0, leaves UF1 dark). SSL 360 restart **re-enumerates** the UF1 → USBPcap
device number changes (was dev 11, became dev 12). Re-check device number after any restart.

## Screen grammar (FF 67) — structure decoded from cap52/cap66 (free, no new capture)
**Frame model:** `FF 67 <len> <addrHi> <addrLo> <payload…> <ck>` — write `<len>` payload bytes
to a **16-bit element address** `(addrHi addrLo)`. NOT a pixel framebuffer — high-level text/graphic
elements at fixed addresses. Screen fully repainted ~24 Hz (every element resent each cycle →
diff static snapshots, timing irrelevant). Analyse with `analysis/uf1_screen_dump.py <pcap> <dev>`.

**Address planes (cap66):**
- `0x00xx` = parameter/V-pot zone: 0x0004="PAN" (label), 0x000c="0.0 dB" (value), 0x0014="1".
- `0x01xx` = main display: 0x0104=soft-key label (F1/F2/F3/"SMPTE/BEATS"), 0x010e=value ("0.0dB"),
  0x010f="O O O O", **0x011c = main text row (len 0xCA=202)**, 0x0122=graphic/bar area (FD frame,
  253 B filled with 0x64).
- `0x011c` text row = **8 fields × 25 bytes**, ASCII, null-padded. cap66: "FADER SEL" @field0,
  "1/10" @field1. cap52 ("OFF" state): dashes @fields 0/1/2, "OFF" @field4 (off 100).

**Paging — SOLVED (cap68, Plugin Mode):** main text row `0x011c` = `REAPER | N/8 | OFF`.
Pages cycled by **Left/Right nav buttons** (← 0x24 / → 0x26). 8 pages in Plugin Mode (DAW mode
shows a different "1/10"). Per-page content is in 0x00xx param-zone + 0x0104, not 0x011c.

**Colour — SOLVED (cap70, ground-truth RGB sweep).** Element/track colour rides **FF38** frames
(id-addressed; SEL=id 0x07), sent **on-change only**. The 2nd FF38 frame per change is the colour
(FF39's 2nd value is constant `0000f0`):
```
FF 38 04 <id> | 00 | XX | YY | <ck>     XX = (G4<<4)|R4 ,  YY = 0xF0 | B4
```
**4-bit-per-channel, order G·R·B**, constant 0xF nibble in YY high. NOT RGB/BGR.
Verified fwd+back: Red `000ff0`, Green `00f0f0`, Blue `0000ff`, Yellow `00eff0`, Cyan `00f0ff`,
White `00ffff`, Orange `003ff0`. Pure primaries exact; 8→4-bit quantization is non-linear
(gamma-ish: G255→E in yellow, G128→3 in orange) — structure solid, exact curve TBD if needed.
To set colour natively: `FF 38 04 <id> 00 (G4<<4|R4) (F0|B4) <ck>`. Revisit cap64/65 LED bytes
under this model (the 1st FF38 frame per change is a transient — ignore for colour).

**Plugin-data screen (SSL Channel Strip, cap71/72) — FOCUS-BASED:**
- `0x010e` (22 ch) = focused parameter readout: name(left)+value(right). All 4 V-pots correlate
  with this one address → it follows focus, NOT 4 separate named slots.
- `0x0104` (13 ch) = section/soft-key label (S/C LISTEN, HQ MODE, A/B, EQ, DYN…).
- `0x010f` (10 B) + `0x0122` (FD, 253 B) = value-bar graphics (the "up to 4" V-pot indicators).
- Full param set extracted (In Trim, High/Low Pass, LF/LMF/HMF/HF Gain/Freq/Q, Ratio, Threshold,
  Release, Range, Width, Mic, Out Trim, Mix). Same for all SSL Channel Strip plugins incl. Link 360.

**Still unknown:**
- Per-V-pot value-bar bytes inside 0x010f / 0x0122 (turn ONE v-pot, watch which bytes move).
- Meter plugin (SSL Meter / Meter Pro) — animated, needs audio. NEXT.
- Field→screen-region mapping; FF39 vs FF38 roles; brightness field (cap65 dim/bright).
- Keep transport STOPPED for layout diffs (meters animate = noise).

## TODO (subtractive order)
1. ~~Fader read~~ ✓ (cap53)
2. ~~Fader motor-drive~~ ✓ (cap54)
3. ~~Buttons~~ ✓ — full id map done (cap55,57,59,60,62,63). Remaining: exact led_id↔button
   LED mapping (FF 3B / FF 39).
4. ~~V-pot/encoder rotation + touch + push~~ ✓ (cap56/58/61). Remaining: ring-LED OUT + readout bar.
5. ~~Jog wheel~~ ✓ (cap61) — encoder id 0x06; dev-10 HID ruled out.
6. Screen (the leftover) — FF 67 frame grammar: addressing, colour, text layout.

**INPUT SIDE COMPLETE.** Remaining work is all OUTPUT: button/ring LEDs, V-pot readout bars,
and the FF 67 screen grammar.
