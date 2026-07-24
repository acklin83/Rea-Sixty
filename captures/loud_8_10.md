# loud_8_10 — UF1 Loudness V-Pot pages 8-10 (StoerPC, 2026-07-24)

Captured via `analysis/stoerpc_capture.sh loud_8_10 50 3` (USBPcap3, SSL Meter Pro on
the StoerPC, Frank paging the last 3 Loudness pages). 4.7 MB, 12599 FF67 frames.

Completes the Loudness V-Pot table (cap109 held pages 0-6; its 115 s window closed at
page 7). Decoded with `analysis/uf1_loudness_vpot_decode.py` → 3 pages, every slot a
real TrackFX param from the Meter Pro dump (name+value form, 8-B name field):

| page | V2 | V3 | V4 |
|---|---|---|---|
| 7 | 40 Short-Term Max Alert (`Smax`) | 42 Loudness Range Min (`Rgmin`) | 43 Loudness Range Max (`Rgmax`) |
| 8 | 41 Momentary Max Alert (`Mmax`) | 44 Dialogue Range Min (`RDmin`) | 45 Dialogue Range Max (`RDmax`) |
| 9 | 52 Save Loudness History (`Save`) | blank (empty payload) | blank (empty payload) |

Built into `kUf1LoudnessVPots[7..9]` (main.cpp). pcap gitignored.
