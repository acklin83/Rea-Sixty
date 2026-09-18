# Capture runbook — SSL's own EXT FUNCS lists for the factory strips

**Why:** the UC1's hidden BACK-menu is a list SSL authored per channel strip.
We hold exactly one, the Harrison 32C's, read off cap137 in September. The
other factory strips — Channel Strip 2, 4K B, 4K E, 4K G, Bus Compressor 2 —
have no list at all. The obvious rule ("everything not on a knob") is
**disproved**: SSL's 32C list contains Comp Mix, Out Trim, Width, Comp Makeup,
Comp Emphasis Freq, Gate SC Filter Freq and Gate Hysteresis, and all seven also
sit on 32C V-Pots. So the list cannot be derived, only read.

Frank, 2026-09-18: *"GENAU WIE SSL! Schneids halt mit wenn du musst!"*

**Everything below is ready before the rig is touched** — the decoder is
written and already reproduces the 32C list from cap137, so nothing has to be
built while Frank is sitting there.

---

## Before the rig: the one thing that can block it

SSL 360 can only drive the UC1 if the UC1 is on **SSL's** driver. Ours (WinUSB,
provider "Rea-Sixty") blocks it, and the binding has drifted by itself before —
2026-08-14 found all three surfaces silently re-bound to ours.

⛔ **Measure, never assume.** On the StoerPC:

```powershell
pnputil /enum-devices /instanceid "USB\VID_31E9&PID_0023*"
```

Provider "Solid State Logic" = SSL's, good. Provider "Rea-Sixty" = ours, and
SSL 360 will not see the UC1. Switching back is `pnputil /delete-driver
<oemNN.inf> /uninstall` + `/scan-devices`; ⚠ **our OEM number drifts** (oem4,
then oem2), so read the current one out of the enum above rather than reusing a
number from any note.

## The capture

One window, all five strips, from the Mac:

```bash
analysis/stoerpc_capture.sh cap140_uc1_extfuncs_factory 300 3
```

300 s and USBPcap**3** — the interface cap137 used, with the UC1 daisy-chained
through the UF1's hub. The script resolves the IP through Tailscale (never
remember it, it is DHCP), captures, and pulls the pcap into `captures/`.

⚠ Preflight first, it costs four seconds: `analysis/stoerpc_capture.sh --test 3`.
A capture that opens on the wrong interface is 300 s of nothing.

## What Frank does, at the UC1

A REAPER project with one track per strip: **Channel Strip 2, 4K B, 4K E,
4K G, Bus Compressor 2**. SSL 360 running, Plug-in Mixer on.

For each strip, in that order:

1. Focus the track so the UC1 shows that strip.
2. Open the hidden BACK-menu.
3. Scroll through **the whole list, to the end**, one step at a time, slowly
   enough that each entry is on the LCD for about a second.
4. Leave the menu.
5. **Pause five seconds before the next strip** — that gap is what separates
   the five lists in the log.

Same order every time so the windows can be told apart without notes. If a list
is long, do not hurry it: a step that lands between two USB frames is a missing
entry, and a missing entry is exactly what we came to avoid.

## Reading it, afterwards

```bash
python3 analysis/uc1_extfuncs_decode.py captures/cap140_uc1_extfuncs_factory.pcap --dev N
```

`--dev` is the UC1's USB address; it is printed in the capture's own `.md` and
is needed because a capture that starts after enumeration carries no
descriptors. Add `--from`/`--to` to slice one strip's window.

**Proven on the known case before it was needed:** on cap137 (`--dev 20 --from
70 --to 86`) it prints `PLUG-IN, COMP MIX, PRE, MIC …`, which is
`kExtFuncs32c` in SSL's order.

## Then

The entries go into `UC1Surface.cpp` beside `kExtFuncs32c`, one table per
strip, with the `special` field for the two that are not host parameters
(A/B = 1, PLUG-IN = 2; HQ Mode is a real parameter on the 32C but not on the
SSL strips, so it needs a third). Param indices resolve against
`docs/ssl-native-params/`.
