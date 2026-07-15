# cap102_our_stream_equalized_full_init

**Date:** 2026-07-15 late evening. **Source:** our own frame trace
(`REASIXTY_UF1_TRACE=1`, gzipped), build `6f6a9d1` — the fully equalized stream,
WITH the complete init in the trace (the trace-flag race was fixed in the same
build, so this is the first trace containing EP0 'C' lines, the 256-byte zero
block, the cap101 init replay and FF54).

**State:** goniometer STILL BLACK, Analogue numbers STILL red, with every
measurable OUT-side difference vs cap101 eliminated (see
docs/HANDOFF-uf1-goniometer.md for the full list).

**Purpose:** reference stream for the 2026-07-17 Windows USBPcap session — this
is what the equalized build sends; diff it (and the Windows capture of the same
build) against cap101 at USB level.

Verified in this trace: EP0 vendor init all rc=0, modem status reads 3260 (same
as SSL receives), FF54 sent and **the device acked FF55 0055** — identical to
its answer to SSL in cap101.
