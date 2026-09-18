# cap139 — UC1 address probe before the EXT FUNCS run (2026-09-18)

40 s USBPcap3 window, StoerPC, SSL 360 + REAPER running, Frank turning one UC1
knob so the LCD would redraw. 5.0 MB. Not a content capture — its only job was
to answer **which USB device address the UC1 has in a fresh capture**, before
spending 300 s of Frank's time on cap140.

**Answer: 41.** (It was 20 on cap137. The address is per enumeration.) The UF1
is 40 — that is the one whose OUT `0x02` carries `ff67…`.

UC1 opcodes on OUT `0x02`: FF13 ×2212, FF5B ×2002, FF66 ×772, FF1B ×267.

⛔ **Why this capture exists at all.** The 4 s preflight decoded to nothing on
*both* candidate addresses, because an idle UC1 redraws no LCD text — so "no
output" and "wrong address" were indistinguishable. Exclusion said 41; only a
knob turn made it a measurement. `uc1_extfuncs_decode.py --dev 41` prints
`PLUG-IN, COMP MIX, FILT IN, PAN …` from 6.5 s on.

**Decode:** `analysis/uc1_extfuncs_decode.py captures/cap139_uc1_addr_probe.pcap --dev 41`
