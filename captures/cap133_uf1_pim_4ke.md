# cap133 — UF1 Plug-in Mixer layer, PLUG-IN mode, 4K E focused, SSL 360 2.1.12 (2026-09-11)

90 s USBPcap3 window, same rig as cap132, PLUG-IN soft key ON, the 4K E track
focused. Frank paged through every soft-key page with the page keys under
V-Pots 3 and 4, turned each of the four V-Pots briefly on every page, and
toggled Bypass on the small display. Device address 18, OUT 0x02, 6.0 MB,
1252 frames of 0x010e (V-Pot label+value rows, 288 distinct), 314 of 0x010f
(bars), 68 of 0x0104 (17 distinct soft-key labels: FILTERS, HQ MODE, A/B, LF
BELL, EQ COLOUR, EQ, HF BELL, FAST ATTACK, DYN, EXPANDER, S/C LISTEN, AUTO
MAKEUP, PRE, SOLO SAFE, PLUG-IN), 334 of 0x0122 (EQ graph), 44 of 0x0119.

**Why:** the reference for what 2.1.12 draws in Plug-in mode for a 4K E, to
compare page by page with our UF1 Plug-in view (labels, pages, V-Pot rows,
the EQ graph) before anything is changed.

**Ten pages, not nine:** one ► past AUTO MAKEUP (page 9) SSL shows a tenth page
whose only key is `EXPANDER` (► down t=37.06, label 37.07, LED byte 0x0102 = 00;
◄ at 38.40 returns to AUTO MAKEUP). Same on cap135/136, where Frank also pressed
that key and its LED toggled.

**Decode:** `analysis/uf1_loudness_vpot_decode.py captures/cap133_uf1_pim_4ke.pcap --raw`
(0x010e pages), `analysis/uf1_screen_dump.py … 18`.
