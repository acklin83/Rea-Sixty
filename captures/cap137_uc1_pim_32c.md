# cap137 — UC1 (and UF1) with the Harrison 32C focused, SSL 360 2.1.12 (2026-09-11)

90 s USBPcap3 window, StoerPC, SSL 360 2.1.12.72214, the UC1 (`UC-000604`,
device address 20, OUT 0x02 / IN 0x81) daisy-chained through the UF1's hub,
Plug-in Mixer with the Harrison 32Classic v2 (2.0.20) focused. Frank turned
every UC1 pot and pressed every button once. The UF1 (address 18) streamed
alongside (EQ graph, page header). 10.9 MB. UC1 opcodes: FF66 ×2340 (LCD text
rows, label + value), FF5B ×4502 (LEDs/rings), FF13 ×2845, FF1B ×600.

**Why:** the UC1 side of a 32C factory strip — which LCD label SSL prints for
each pot, the value formatting, and which button LEDs are live for this strip.
Companion to cap134 (the UF1 side). The UC1 node already had USBPcap in its
stack; the device-level UpperFilters entry was added anyway.

**Decode:** `analysis/parse_usbpcap_uc1.py`, `analysis/uc1_decode/`.
