# cap134 — UF1 Plug-in Mixer layer, PLUG-IN mode, Harrison 32C v2 focused, SSL 360 2.1.12 (2026-09-11)

90 s USBPcap3 window, same rig as cap133, the Harrison 32Classic Channel Strip
v2 (2.0.20) track focused. Frank paged through every soft-key page, turned each
V-Pot on every page, toggled Bypass. Device address 18, OUT 0x02, 6.1 MB,
1548 frames of 0x010e (343 distinct V-Pot rows), 378 of 0x010f, 52 of 0x0104
(12 distinct soft-key labels: FILTERS, HQ MODE, A/B, LOW BELL MODE, EQ, HI BELL
MODE, DYN, EXPANDER, COMP EMPHASIS IN, GSFT, GATE SC LISTEN), 280 of 0x0122.
The CS TYPE zone 0x0017 = "32C" was sent on track change in cap132.

**Why:** the material for a 32C factory strip next to 4K B/E/G and CS2 —
page order, V-Pot labels, soft-key names, the EQ graph — straight from SSL 360.

**Decode:** `analysis/uf1_loudness_vpot_decode.py captures/cap134_uf1_pim_32c.pcap --raw`,
`analysis/uf1_screen_dump.py … 18`.
