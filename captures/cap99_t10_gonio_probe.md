# cap99 — Lissajous (t10) geometry, SOLVED (2026-07-15)

Our impersonator dump (`REASIXTY_T10_DUMP=1`), Overview screen,
`uf1_meter_probe.wav` played once. First dump taken AFTER the chunk reassembler
learned to work without f7 — so every T10 line here is a COMPLETE image
(all 1167 frames n=17113), not a chunk. Every earlier attempt was fitted to
chunk boundaries and was wrong because of that.

## The answer
**The plug-in's Lissajous raster is a DIAMOND of ODD row widths:
1, 3, 5, …, 185, …, 5, 3, 1 — 185 rows, sum 93² + 92² = 17113.**

Derive rows from n, never hardcode: 2n-1 = (2k-1)² -> R = 2k-1.
17113: 2n-1 = 34225 = 185² -> R = 185, k = 93. Exact.

## The proof — physics on the complete array, and it is exact
| section     | nz  | rows touched | frac across row      | meaning |
|-------------|-----|--------------|----------------------|---------|
| corr +1 (mono, L=R) | 185 | **185 = ALL** | **0.500, min = max** | one cell per row, dead centre = a perfect VERTICAL line |
| corr -1 (L=-R)      | 185 | **1**        | 0.000 … 1.000        | the widest row, fully lit = a HORIZONTAL line |
| L only / R only     | 93  | 93           | 0.0 … 1.0            | one diamond EDGE (93 = (185+1)/2) |
| corr 0              | ~6000 | 136        | spread               | the scatter cloud |

No lit index falls outside the diamond. nz=185 and nz=93 are the giveaway:
the diagonals are 185 cells and a half-diagonal is 93 = (185+1)/2.

## Resampling onto the UF1 (185-row source -> 187-row / 8560-byte UF1 diamond)
**FORWARD map every source pixel and keep the MAXIMUM.** Backward nearest-
neighbour sampling — what the code did — silently drops the picture: the source
is 185 wide where the UF1 is 92, so it is a 2:1 shrink and point-sampling steps
straight over a one-pixel trace. Measured on the real corr=+1 frame here:
  backward NN : 93 of 187 rows lit  (half the mono line missing)
  forward+max : 185 of 187 rows lit, meanFrac 0.513
The two unlit rows are the tips (185 source rows cannot fill 187 dest rows).
