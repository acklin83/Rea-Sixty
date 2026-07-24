#!/usr/bin/env python3
"""Decode the UF1 main-LCD time display — SSL→UF1 element 0x0119.

DECODED 2026-07-24 (captures cap123-128, all 6 REAPER transport formats). The
0x0119 payload is an 11-character 7-segment display, one byte per character
position, sent by SSL 360 (= what the UF1 shows on the NORMAL/Channel view, NOT
the meter/analyzer). Our native build must drive it the same way.

  byte = (SEG7[digit] << 1) | dp          dp (bit0) = the separator dot after
  0x00 = blank                            this position ("." between H.M.S.F etc.)
  SEG7 = standard 7-seg (a=0x01 … g=0x40): 0=0x3f 1=0x06 2=0x5b 3=0x4f 4=0x66
         5=0x6d 6=0x7d 7=0x07 8=0x7f 9=0x67 (9 has NO bottom segment → 0x67)

There are NO colon/special glyphs — every format (Measures/Beats, Min:Sec, Sec,
Samples, H:M:S:F, Absolute-frames) uses only digits + dp dots + blanks. Formats
differ ONLY in layout (which of the 11 positions hold digits + where the dots go).
Idle/reset = "1.1.00" (`00 00 00 0d 0d 7e 7e 00 …`), also in the meter entry burst.

Frame model: FF 67 <len> 01 19 <11 payload bytes> <cksum>.
Usage: uf1_0119_timecode_decode.py <pcap> [--dev N]
"""
import subprocess, binascii, sys, argparse
from collections import Counter

TSHARK = "/opt/homebrew/bin/tshark"
SEG7 = {0x3f:'0',0x06:'1',0x5b:'2',0x4f:'3',0x66:'4',
        0x6d:'5',0x7d:'6',0x07:'7',0x7f:'8',0x67:'9'}
# reverse: digit char -> payload byte (dp NOT set); OR in 1 for the dot.
DIGIT_TO_BYTE = {v: (k << 1) for k, v in SEG7.items()}

def encode_char(ch, dot=False):
    """Char ('0'..'9' or ' ') -> 0x0119 byte. dot=True sets the separator dp."""
    if ch == ' ':
        return 0x00
    b = DIGIT_TO_BYTE[ch]
    return b | (1 if dot else 0)

def decode_byte(b):
    dp = b & 1
    if b == 0:
        return ' '
    ch = SEG7.get(b >> 1, '?')
    return ch + ('.' if dp else '')

def busiest(pcap):
    out = subprocess.run([TSHARK, "-r", pcap, "-Y",
        "usb.endpoint_address==0x02 && usb.capdata", "-T", "fields",
        "-e", "usb.device_address"], capture_output=True, text=True).stdout
    c = Counter(x for x in out.split() if x)
    return int(c.most_common(1)[0][0]) if c else 0

def frames(pcap, dev):
    df = f"usb.device_address=={dev} && usb.endpoint_address==0x02 && usb.capdata"
    out = subprocess.run([TSHARK, "-r", pcap, "-Y", df, "-T", "fields",
        "-e", "frame.time_relative", "-e", "usb.capdata"],
        capture_output=True, text=True).stdout
    for line in out.splitlines():
        p = line.split("\t")
        if len(p) < 2: continue
        try: ts = float(p[0]); b = binascii.unhexlify(p[1].strip().lower())
        except Exception: continue
        i = 0
        while i + 4 <= len(b):
            if b[i] != 0xFF: i += 1; continue
            ln = b[i+2]; end = i+3+ln+1
            if end > len(b): break
            f = b[i:end]
            if f[1] == 0x67 and len(f) >= 6:
                yield ts, (f[3] << 8) | f[4], f[5:-1]
            i = end

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pcap"); ap.add_argument("--dev", type=int, default=None)
    a = ap.parse_args()
    dev = a.dev if a.dev is not None else busiest(a.pcap)
    print(f"# {a.pcap} dev={dev}  (0x0119 time display)")
    prev = None
    for ts, e, pl in frames(a.pcap, dev):
        if e != 0x0119 or len(pl) != 11 or pl == prev: continue
        prev = pl
        print(f"{ts:8.3f}  {pl.hex()}  '{''.join(decode_byte(x) for x in pl)}'")

if __name__ == "__main__":
    main()
