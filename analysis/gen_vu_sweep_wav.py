#!/usr/bin/env python3
"""Generate the VU level-sweep test signal for the UF1 Analogue-screen decode.

Why a stepped signal and not music: the VU needle is ballistic (~300ms) while
the 0x011c text readout has a hold, so during music the two never agree and the
pairs are useless (proven on cap80). Holding each level for seconds lets BOTH
settle, which turns 0x0125/0x0127 (needle) vs 0x011c (dB text) into exact pairs
straight out of the USB stream — no protobuf capture needed to correlate.

L and R sweep in OPPOSITE directions: one capture yields two independent sweeps
and simultaneously proves which address is which needle.

Reference: the Analogue screen's own settings say "Ref -18.0dB", i.e.
0 VU = -18 dBFS (cap76).

Usage: gen_vu_sweep_wav.py [out.wav]
"""
import math
import struct
import sys
import wave

SR = 48000
FREQ = 1000.0          # 1 kHz — the classic VU alignment tone
STEP_SECS = 2.5        # >> 300ms VU integration, so the needle fully settles
VU_REF_DBFS = -18.0    # 0 VU = -18 dBFS (Analogue screen "Ref -18.0dB")
VU_LO, VU_HI = -30, 5  # sweep past both ends of the -20..+3 VU scale to catch
                       # where the needle pins

FADE = 0.005           # 5ms edges — avoid clicks that would spike the peak hold


def dbfs_to_amp(db):
    return 10.0 ** (db / 20.0)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "vu_sweep.wav"
    steps_l = list(range(VU_HI, VU_LO - 1, -1))   # L: loud -> quiet
    steps_r = list(range(VU_LO, VU_HI + 1))       # R: quiet -> loud (opposite)
    n = len(steps_l)
    frames_per = int(SR * STEP_SECS)

    data = bytearray()
    phase = 0.0
    dphase = 2 * math.pi * FREQ / SR
    for i in range(n):
        ampL = dbfs_to_amp(VU_REF_DBFS + steps_l[i])
        ampR = dbfs_to_amp(VU_REF_DBFS + steps_r[i])
        for k in range(frames_per):
            env = 1.0
            if k < SR * FADE:
                env = k / (SR * FADE)
            elif k > frames_per - SR * FADE:
                env = max(0.0, (frames_per - k) / (SR * FADE))
            s = math.sin(phase) * env
            phase += dphase
            if phase > 2 * math.pi:
                phase -= 2 * math.pi
            data += struct.pack("<hh", int(s * ampL * 32767), int(s * ampR * 32767))

    with wave.open(out, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(bytes(data))

    total = n * STEP_SECS
    print(f"wrote {out}: {total:.1f}s, {n} steps x {STEP_SECS}s")
    print(f"  0 VU = {VU_REF_DBFS} dBFS, {FREQ:.0f} Hz")
    print(f"  L: {steps_l[0]:+d} VU -> {steps_l[-1]:+d} VU  "
          f"({VU_REF_DBFS+steps_l[0]:+.0f} .. {VU_REF_DBFS+steps_l[-1]:+.0f} dBFS)")
    print(f"  R: {steps_r[0]:+d} VU -> {steps_r[-1]:+d} VU  (opposite)")
    print("\nstep schedule (t = seconds from playback start):")
    for i in range(n):
        print(f"  t={i*STEP_SECS:6.1f}  L={steps_l[i]:+3d} VU  R={steps_r[i]:+3d} VU")


if __name__ == "__main__":
    main()
