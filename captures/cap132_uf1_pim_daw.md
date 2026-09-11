# cap132 — UF1 Plug-in Mixer layer, DAW mode (PLUG-IN key unlit), SSL 360 2.1.12 (2026-09-11)

60 s USBPcap3 window, StoerPC, SSL 360 2.1.12.72214, REAPER 7.66 as host with
360-enabled strips on three tracks (4K E, 4K B, Harrison 32C 2.0.20). UF1 on
Layer 3 = Plug-in Mixer with the PLUG-IN soft key OFF, i.e. the DAW-control
side of the layer. Frank pressed the CHANNEL encoder (mode list), turned
through the modes, turned a pan, paged the soft keys and toggled PLUG-IN/DAW.
Device address 18, OUT 0x02, 4.1 MB, 10237 FF67 frames.

On the wire: 0x0104 soft keys `PRE / SOLO SAFE / PLUG-IN` and the host name
`REAPER`; 0x000e pan readouts (`Pan  C`, `Pan +47` …); 0x000c fader dB; 0x000b
track names; 0x0017 CS types `32C / 4K B / 4K E`; 0x0119 (7-segment) and
0x010e/0x010f (V-Pot rows and bars) for the pan/fader display; 0x0122 ×60.

**Why:** the new 2.1.12 DAW-control display of the Plug-in Mixer layer (pan on
the 7-segment, mode list, host tab), to be compared with our Plug-in/DAW views.
Nothing is adopted from it without Frank's say.

**Decode:** `analysis/uf1_screen_dump.py captures/cap132_uf1_pim_daw.pcap 18`,
`analysis/uf1_meter_capture_analyze.py … --dev 18 --timeline`.
