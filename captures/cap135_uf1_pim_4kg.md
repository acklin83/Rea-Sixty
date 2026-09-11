# cap135 — UF1 Plug-in Mixer layer, PLUG-IN mode, 4K G focused, SSL 360 2.1.12 (2026-09-11)

90 s USBPcap3 window, same rig as cap133, the SSL 4K G (1.3.1) track focused;
every soft-key page, each V-Pot per page, Bypass. Device address 18, OUT 0x02,
10.9 MB.

Pages as sent (analysis/…/pages.py): Home `Width/Mic/Out Trim/Mix`; p2 `In
Trim / Impedance / High Pass / Low Pass` with `FILTERS / IMP IN / HQ MODE / A/B`;
p3 LF, p4 LMF, p5 HMF, p6 HF with `EQ COLOUR / EQ` on keys 3-4; p7 comp
`FAST ATTACK / – / – / DYN`; p8 gate `FAST ATTACK / EXPANDER / S/C LISTEN / DYN`;
p9 `AUTO MAKEUP`, no V-Pots — the 4K E layout (cap133) plus Impedance.

**Soft key 1 on the LF and HF pages depends on the EQ colour:** with the EQ
COLOUR LED lit (0x0102 = 0x0c) it reads `LF BELL` / `HF BELL`; with it unlit
(0x08) it reads `LMF DIV3` / `HMF X3` — the G-series EQ's ÷3 / ×3 switches.
Observed three times each at the moment the colour changed (t=20.6/24.7/25.0
and 36.6/38.2/38.5). On page 9 the single key read `AUTO MAKEUP` on entry and
`EXPANDER` once mid-page (also seen on the 4K E, cap133 t=37.07) — what
triggered that swap is not established.

**Decode:** as cap133.
