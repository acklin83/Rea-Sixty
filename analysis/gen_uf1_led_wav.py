#!/usr/bin/env python3
"""ONE signal for the red overload LED and the second needle after Meter Pro 1.3.7.

Written 2026-09-11. gen_uf1_needle_wav.py tops out at -10 dBFS = Ref+8 against
the -18 dBFS reference, and the Meter Pro's "Analogue Meters LED Overload"
(param 15) defaults to 9 dB above Ref — so that file can never light the LED.
This one crosses the threshold, twice over, and leaves long silences after each
hit so the SECOND needle's fall (the plug-in's own, drawn by SSL 360 on 0x0127)
is on the wire too.

      4 s  -18 dBFS  = Ref, needle mid-dial, LED must stay dark
      3 s  silence
   5 x  (1 s @ -6 dBFS = Ref+12, LED must light)  + 4 s silence  (flash vs latch,
                                                       second needle's fall)
      3 s  -3 dBFS  = Ref+15, sustained             (latch behaviour under a hold)
      6 s  silence

Play it ONCE on the Analogue screen in VU, once in PPM, with SSL 360 driving the
UF1 and USBPcap open. Sections are announced on stdout for time alignment.
"""
import math, struct, sys, wave
SR = 48000; FREQ = 1000.0
def tone(level_dbfs, seconds):
    amp = 10.0 ** (level_dbfs / 20.0); n = int(SR * seconds); w = 2.0 * math.pi * FREQ / SR
    return [amp * math.sin(w * i) for i in range(n)]
def silence(seconds): return [0.0] * int(SR * seconds)
def main(path):
    plan = [("silence", None, 2.0), ("tone -18 dBFS = Ref (LED dark)", -18.0, 4.0), ("silence", None, 3.0)]
    for i in range(5):
        plan.append((f"hit {i+1}: 1 s @ -6 dBFS = Ref+12 (LED on)", -6.0, 1.0))
        plan.append((f"silence {i+1} -> flash/latch? second needle falls", None, 4.0))
    plan += [("tone -3 dBFS = Ref+15 sustained", -3.0, 3.0), ("silence -> latch under hold?", None, 6.0)]
    samples = []; t = 0.0
    print(f"{'start':>8}  {'len':>5}  section")
    for name, lvl, secs in plan:
        print(f"{t:8.2f}  {secs:5.2f}  {name}"); samples.extend(silence(secs) if lvl is None else tone(lvl, secs)); t += secs
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(b"".join(struct.pack("<h", max(-32768, min(32767, int(s * 32767.0)))) for s in samples))
    print(f"\n{t:.1f} s written to {path}")
if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "uf1_led.wav")
