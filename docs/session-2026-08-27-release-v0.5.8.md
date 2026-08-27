# Session, 2026-08-27: two leaks found the hard way, then v0.5.8 shipped

A day that started with colour names and ended with a Mac Studio out of memory.
In between: the release that carries the whole Hue integration.

## The small repairs the day opened with

**The colour keys were lying.** A Track-Colours bank labelled its keys from names
stored in ExtState, and the palette had moved onto the ten colours the hardware
can actually render without those names following. Frank saw BROWN on a cyan
swatch. The word was nowhere in the code: it was in
`~/Library/Application Support/REAPER/reaper-extstate.ini`, typed against the old
Material palette. **When a label is nowhere in the source, look in ExtState before
guessing.**

First attempt invented replacements (TEAL, VIOLET). Frank, twice, in capitals:
look at what SSL calls them. They are in `LedColourType`, in the reconstructed
proto under `analysis/` and as plain strings inside SSL 360 itself: White, Red,
Green, Blue, Cyan, Magenta, Yellow, Orange, Purple, LightGreen, Pink, LightBlue,
Lamp, Black. Every word the surface says about the hardware belongs to the
hardware.

**The UF1 rested too bright.** `uf1KeyColourNibbles_` halved the nibbles for the
resting state, which reads as arithmetic and is not: the nibble drives the LED
roughly linearly, so a saturated channel rested at 7 of 15 while the UF8 rests at
1. SSL's own dim bytes settle it, every entry in `selPaletteRgb` rests on the
lowest step. SOLO and CUT were a step high as well.

## The manual, and TotalReaper's side of the bridge

The REC + RME chapter described our half and nothing of TotalMix's, so following
it exactly still left a dead mode. It now carries the prerequisites, each one
read out of the TotalReaper repo rather than remembered: TotalMix FX 2.1 with
Global OSC, the two settings that switch it on, the "Receive to hidden channels"
box that silently drops everything when it is off, ports 7001 and 7002 (confirmed
in TotalReaper's source), and TotalReaper 0.2.2 as the version that follows a
track when its input changes. The README's two RME download links are 404 today,
so the manual names RME's beta page instead, which is live.

Reading the NAV chapter against `SettingsScreen.cpp` turned up five more errors,
including a view picker that stopped existing on 2026-05-28 and a claim that
independent UC1 scopes were not implemented when the tab has offered three for
months. A manual can lie by calling a finished thing unfinished.

## TotalReaper: Only Set Main Submix

Frank's ask: the routing mirror must leave the other hardware outputs alone, so a
cue mix built by hand survives. Built as a settings toggle, gated in
`sendFader` / `sendBalpan` / `sendSolo`, the only route anything takes to
TotalMix, so one rule covers ordinary writes, the close-out of a routing that
disappeared and the mirror's own disable sweep.

Then the other half, which mattered more: a bus we do not write is a bus we do not
read. 2-Way still accepted what TotalMix reported on those buses and turned it
into REAPER send levels, so a client turning up their own headphones through
stoer_me would rewrite the session behind the engineer. Shipped as v0.2.3.

## The afternoon: 171 GB

phones.stoersender.ch hung on "Verbinde mit Bridge". The bridge was fine and
answering; its mix list was empty because nothing is listening on UDP 7003.
TotalMix has controller 2 "In Use" with 7003/7004 and still binds only 7001. That
is still open; the TotalMix restart that would settle it has not happened.

While measuring that, the machine ran out of memory. The kernel's jetsam report
named REAPER at **171.35 GB**.

**What worked, in order:**

1. `JetsamEvent-*.ips` names the largest process. No guessing about who.
2. `heap <pid>` twice, comparing the size histogram. Millions of identical block
   sizes means one source, not fragmentation.
3. **RSS lies.** Under pressure macOS compresses, and a leak made of zeros
   compresses almost completely: RSS fell from 5.7 GB to 187 MB while the block
   count went from 7.6 to 34 million. Read as "flat, so it stopped". It had not.
   Count blocks, not resident bytes.
4. `sample` was useless here, 4 KB zero-fills cost no CPU. It pointed at the SSL
   meter, and the meter kept streaming while the leak ran, which cleared it.
5. What decided it: relaunch with `MallocStackLogging=1`, then
   `malloc_history <pid> -allBySize`. Sixty seconds later the answer was on
   screen with function names.

**Leak one, ours.** `wdl_json_parser` frees only what `dispose_element` has
handed back; a tree still standing when the parser dies is leaked element by
element. All 23 parse sites in the project got that wrong, and it was invisible
while every parse happened once, at load. The Hue worker parses the bridge every
second. `JsonTreeGuard` now sits under every parse, RAII rather than "dispose at
the end", because most of these functions return early.

**Leak two, upstream's.** With the guard in place, 10'300 blocks a second
remained. `leaks` on a stack-logging REAPER: 4.6 million root leaks, all
`wdl_json_parser::new_element`. In `parse_internal` an object member parses its
key into an element, keeps only that element's string pointer, adds the value to
the tree and drops the key element where nothing can reach it. Proved in
isolation, thirty lines and `malloc_zone_statistics`: 480 bytes a parse before,
zero after. `vendor/` is gitignored and CMake fetches WDL fresh, so the patch
lives in `CMakeLists.txt`, idempotent, and fails the build if the anchor moves.

**The lesson worth keeping:** a bug that is harmless once becomes fatal on a
clock. When new code puts an existing function in a loop, its lifetime question
has to be asked again.

## v0.5.8 "Let There Be Light!"

Tagged on `dc70b4e`. CI green on four jobs, both Macs notarised, Linux packed
from the CI container, 15 assets plus the Stream Deck companion, ReaPack index
verified with 12 sources and all twelve URLs fetched, reasixty.com rebuilt and
all seven pages plus `/mappings` answering.

One more fix went in just before the tag, tested by Frank on the device: "touch
selects channel" no longer fires in UF8 Plugin Mode, where the eight strips are
eight parameters of one plug-in rather than eight tracks, and selecting a track
moved the focus out from under the plug-in mid-gesture.

## Open

- stoerme: TotalMix does not bind 7003. Restart pending.
- Three Hue details unverified on the device: the scene LED from `last_recall`,
  the leftovers warning in the sub-bank page, the names under Shift in the editor.
- Carried forward: a UF8 plug-in-mode fader on an FX parameter does not return to
  its envelope in Touch during playback.
