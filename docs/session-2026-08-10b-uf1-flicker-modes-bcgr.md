# Session 2026-08-10 (second) — UF1 display flicker, mode actions, BC-GR LEDs

Branch `uf1-native-build`, committed, **not pushed**. Nothing below is
hardware-verified — Frank tests on 2026-08-11.

## What shipped

| Commit | What |
|---|---|
| `1511dc0` | The display no longer flickers on MODE / SCRUB / MODE+encoder. |
| `68a4886` | Actions for the encoder modes and the four hardware modes. |
| `71c6dde` | Bus-Comp gain reduction on the four display soft-key LEDs. |
| `0fe92c8` | …with range and resolution split, plus the colour ramp and presets. |

## 1. The flicker was one flag doing two jobs

`uf1PaintChannel_` hangs everything off a single `changed`, and two of its
inputs are **text** events, not layout events: `menuEdge` (MODE held) and
`modeFieldChanged` (SCRUB held, jog mode, encoder mode). Both only rewrite the
header's `ENC …` cell — but `changed` also gated the large-LCD layout re-assert
and the FAKE-CS placeholder burst. So every MODE tap, every SCRUB tap and every
detent of MODE+encoder re-sent `0x0100` (the layout selector), slammed the
small-LCD meters to their idle `0xff`, and wrote the `4K E` / `PRE | SOLO SAFE |
PLUG-IN` placeholders that the real-value blocks overwrite a frame later.

Fix: a narrower `layoutChanged` (track / view / screen / FX-identity) gates both
bursts. The header one-shot keeps `changed` — it is a text rewrite of a cell the
pacer restates every cycle anyway.

**The second half is the one worth remembering:** a placeholder followed by the
real value *in the same tick* is a visible flash, because they are separate USB
frames and the device renders between them. The soft-key painter now owns the
non-SSL Plugin placeholder labels itself instead of leaning on the layout burst
to restore them.

## 2. Modes are reachable from three routes now

The four hardware modes had exactly one way in: hold MODE, hit a display
soft-key. The encoder modes had builtins but no Action List entries.

- builtins `uf1_view_plugin` / `_daw` / `_meter` / `_sends` / `_cycle`
- `REASIXTY_UF1_MODE_*` (four + cycle)
- `REASIXTY_UF1_ENCODER_*` (all 14 modes + next / previous)

The mode bodies moved into one `uf1SetViewMode_` that the soft-key handler calls
too — so the *hardware* path is new code as well, and worth a look on the
surface. The encoder actions mirror the builtins' toggle semantics (re-firing
the live mode returns to Channel Select) and the cycle steps the user's VISIBLE
ring, not the raw enum. A mode change announces on the banner: the two new
routes can fire with the UF1 out of sight.

## 3. BC gain reduction had nowhere to go on the UF1

The small LCD's comp LED (`0x0015`) is hardwired to the Channel Strip domain, so
a Bus Compressor only ever showed there by the "first FX exposing
`GainReduction_dB`" fallback, i.e. on a track with no CS. Frank: *"wir lassen die
vier LEDs oben tanzen"*.

Range and resolution are separate settings, because one knob for both means
every gain in resolution costs range:

- full scale 4 / 8 / 12 / 20 dB across the four LEDs
- 2 steps per LED (green·red) or 4 (green·yellow·orange·red)

8 dB / 4 steps = 0.5 dB a step, finer than the analogue needle. The LED frame
carries 4 bits per channel (`buildColour`: `xx = (g4<<4)|r4`), so the ramp costs
nothing on the wire. Presets write both; the two controls stay free below and
the preset reads "Custom" when they don't match. Deliberately **unsmoothed** —
Frank chose raw over peak-hold ballistics.

Source is the BC-**anchor** track, not the focused one: the BC sits on the
master or a bus, so reading the focused channel would leave the meter dark all
day. Anchor = the UC1's carousel anchor when a UC1 is present, so both surfaces
show the same compressor.

### The risk here is ownership, not display
Those four LEDs now have three owners, and the arbitration is the feature:
the p188 soft-key block stands down and **poisons its `sSkLed` cache** (which is
what repaints every LED the moment the meter releases them); the meter is
**suspended while the MODE menu is held**, because that overlay is
change-detected and would be overwritten on the next tick; and with no Bus
Compressor resolved the meter never claims the LEDs at all.

It runs in all four modes. In DAW that costs the bank-slot state LEDs and their
binding colours, in Sends the PRE/POST indication — deliberate, because CS/BC
V-Pot work already pins you to Plugin mode, so a Plugin-only meter would be lit
exactly when you don't need it.

## Open — one hardware pass, no capture run

Can the soft-key **activity** indication move off the LEDs onto a label
highlight? Then the GR meter would cost nothing in any mode. Three different
elements, easy to conflate:

- `0x0000` — backdrop behind the **channel SOFT key** label. One key, found and
  verified earlier today (`331611d`). Not the four.
- `0x0102` — highlight for the four display soft keys, but **one byte for the
  whole row**: `01` normal · `03` Reset · `05` Fine · `09` Presets, all Meter-view
  states. Never seen addressing SK1, never two at once. Today's channel-view
  sweep recorded `0x0100..0x011a` rendering nothing — weaker than it sounds (the
  probe wrote marker payloads, not a valid state code), but not confirmation
  either.
- `0x010d` — per-V-Pot styling, 4 bytes, `06` dimmed / `0a`·`0b` active. A real
  per-slot highlight, wrong row.

Answerable by writing `0x0102` = `00`…`0f` in Plugin view and seeing what lights.
Read p188 first — the manual has pre-empted this twice already today.
