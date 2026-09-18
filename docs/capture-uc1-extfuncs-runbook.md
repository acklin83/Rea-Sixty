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

⛔ **Measure, never assume.** From the Mac, over SSH:

```bash
ssh claude@$IP 'pnputil /enum-devices /drivers' | grep -A 12 "UC-000604"
```

⚠ **Do not filter with `/instanceid "USB\VID_31E9&PID_0023*"`.** The remote
shell is CMD, CMD eats the `&` inside the instance id, and the command answers
**"No devices were found on the system"** with the UC1 sitting right there,
connected and working. 2026-09-18 read that as "the UC1 is gone". A filter that
fails open is worse than no filter: grep the full enum by serial instead.

Provider "Solid State Logic" = SSL's, good. Provider "Rea-Sixty" = ours, and
SSL 360 will not see the UC1. Switching back is `pnputil /delete-driver
<oemNN.inf> /uninstall` + `/scan-devices`; ⚠ **our OEM number drifts** (oem4,
then oem2), so read the current one out of the enum above rather than reusing a
number from any note.

Measured 2026-09-18: UC1 `UC-000604` and UF1 `UF1-009184` both on `oem16.inf`
(original `sslbus.inf`, provider Solid State Logic, 2.12.36.4), Status Started.

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
python3 analysis/uc1_extfuncs_decode.py captures/cap140_uc1_extfuncs_factory.pcap --dev 41
```

`--dev` is the UC1's USB address, and it is needed because a capture that
starts after enumeration carries no descriptors — the VID/PID filter finds
nothing to match. ⚠ **The address is per enumeration, not per device:** it was
20 on cap137 and 41 on 2026-09-18. Re-read it whenever the rig has been
replugged or rebooted. Add `--from`/`--to` to slice one strip's window.

To re-read it, list the addresses and find the one that is not the UF1:

```bash
tshark -r captures/NAME.pcap -T fields -e usb.device_address -e usb.endpoint_address \
  -e usb.transfer_type | sort | uniq -c | sort -rn | head
```

Two devices carry heavy bulk traffic (`transfer_type 0x03`): the UF1 and the
UC1. The UF1 is the one whose OUT endpoint (`0x02`) carries `ff67…` frames.
⛔ That is an exclusion, not a proof — confirm the other one positively by
decoding it and seeing LCD text come out. A silent decode proves nothing: an
idle UC1 redraws no text, so "no output" and "wrong address" look identical.

**Proven twice.** On cap137 (`--dev 20 --from 70 --to 86`) it prints
`PLUG-IN, COMP MIX, PRE, MIC …`, which is `kExtFuncs32c` in SSL's order. And on
cap139 (`--dev 41`), a 40 s probe taken on 2026-09-18 with Frank turning one
knob, it prints `PLUG-IN, COMP MIX, FILT IN, PAN …` live off the current rig.

## Then

The entries go into `UC1Surface.cpp` beside `kExtFuncs32c`, one table per
strip, with the `special` field for the two that are not host parameters
(A/B = 1, PLUG-IN = 2; HQ Mode is a real parameter on the 32C but not on the
SSL strips, so it needs a third). Param indices resolve against
`docs/ssl-native-params/`.
