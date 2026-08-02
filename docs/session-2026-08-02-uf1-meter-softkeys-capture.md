# UF1 meter soft-keys RESET / FINE / PRESETS — captured + decoded 2026-08-02

Capture: `captures/uf1_rp.pcapng` (StoerPC, SSL 360 driving the UF1 on sslbus, USBPcap3,
147678 pkts). SSL→UF1 = `FF 67 <len> <addrHi> <addrLo> <payload>` (uf1::buildScreen format).

## ★ CAPTURE GOTCHA (cost this session — write it down)
USBPcap only captures a device that **(re)enumerates while the capture is already running**.
The UF1 was claimed by SSL's **SSLBUS** driver before USBPcap hooked its stack → the meter
showed on the UF1 but the USB stream was invisible to USBPcap (0 FF67 on every interface).
**FIX: start the tshark capture, THEN physically re-plug the UF1** → USBPcap catches the
re-enum + the full stream. (Also: local tshark on the Mac reads the pcapng fine;
`/opt/homebrew/bin/tshark`.) Interface = USBPcap3 = the Intel-XHCI root hub `ROOT_HUB30\4&1dc5b97`
(shared with the Apple-BT + VID_0424 hubs the UF1 sits behind).

## Soft-key highlight element `0x0102` (1 byte) — CONFIRMED
- `01` normal · `03` **RESET flash** (~0.12 s, then back to 01) · `05` **FINE active** ·
  `09` **PRESETS active**. (05 matches the already-shipped FINE impl `b1b925b`.)

## Screen/mode element `0x0100` (2 bytes) — CONFIRMED
`0400` Overview · `0401` Analogue · `0402` RTA · `0403` **Presets menu** · `0405` Loudness.

## RESET (SK2 / 0x19+? display soft-key 2)
On press: `0x0102=03` for ~0.12 s → `01`. That is the ONLY SSL→UF1 effect. The actual meter
peak/clip RESET is a **Core→plug-in** command (NOT in the SSL→UF1 USB stream) — undecoded;
needs a Core↔plug-in loopback capture. UF1-side we can only paint the flash + (maybe) reset
our own fallback holds.

## PRESETS (SK4)
Enter: `0x0100=0403`, `0x0102=09`, and:
- `0x0104` soft-key relabels: idx0 OVERVIEW · idx1 RESET · **idx2 "Navigate Back"** (was FINE) ·
  idx3 PRESETS.
- `0x010e` V-Pot labels: idx0 "HARDWARE OUTPUT" · idx1/idx2 empty · **idx3 "Select"** (V-Pot4).
- `0x010d = 0a 06 06 0a` (per-V-Pot style: V1 active, V2/V3 dim, V4 active).
- **Scroll cursor = `0x011f`** (1 byte 0x00..0x64) — driven by turning **V-Pot4**; sweeps the list.
- `0x011e` = section/page byte (00 → 10 seen).
- **The preset LIST (names) is drawn in the `0x0122` IMAGE bitmap**, NOT as text elements —
  so replicating the list = rendering a name bitmap ourselves. Loading a preset = Core→plug-in.
Exit: `0x0100` back to the screen (0400/0401/…), `0x0102=01`.

## Loudness screen (0x0100=0405) — bonus (already built)
Soft-key idx2 = "PAUSE"; V-Pot labels "DAW Sync" / "30 secs" / "Scroll Timeline"; `0x011a=01`,
`0x0123=00` on entry.

## Implementability
- **FINE**: DONE (`b1b925b`), protocol-confirmed.
- **RESET**: the 0x0102=03 flash is trivial. The real peak reset = Core→plug-in (needs another
  capture / decode). Half-feature without it.
- **PRESETS**: chrome (mode/labels/cursor) is decodable, but the name LIST is a `0x0122` bitmap
  + load is Core→plug-in → a large lift (preset enumeration + bitmap render + load command).
