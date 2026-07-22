#!/usr/bin/env python3
"""Check OUR OWN goniometer output from the Mac frame trace — no pcap, no USBPcap.

The Windows path uses uf1_gonio_extract_replay.py on a USBPcap. On the Mac the
extension writes its OUT frames to a text log instead (REASIXTY_UF1_TRACE=1 ->
/tmp/reaper_uf1_frames.log, one bulk transfer per line):

    [<ms>] O rc=<rc> len=<n> <hexbytes...>

This reassembles the 35-chunk 0x0122 images we TRANSMIT and counts lit 4bpp
cells. It answers the first question of the live-render test: are our images
actually carrying signal (thousands of lit nibbles) while audio plays? If the
UF1 is black but this prints thousands of lit nibbles, the codec is right and
the problem is emission/timing, not content.

Frame layout mirrors out_frames() exactly (8ecaec5): within a transfer,
FF <b1> <len> <elemHi> <elemLo> <payload...> <seal>; element = bytes 3..4,
payload = bytes[5:-1]. 0x0122 payload[0] = chunk index 0..34 (34 leads, 0
latches), then 250 data bytes (chunk 34 = 60).

Usage:
  uf1_gonio_check_log.py [/tmp/reaper_uf1_frames.log] [--min-lit 2000]
"""
import argparse
import binascii
import sys

IMG_BYTES = 8560
N_CHUNKS = 35


def lit_nibbles(img):
    """Count non-zero 4bpp cells — two pixels per byte, high nibble first."""
    n = 0
    for b in img:
        if b >> 4:
            n += 1
        if b & 0x0F:
            n += 1
    return n


def out_frames_from_log(path):
    """Yield (ts_ms, elem, payload) for every OUT frame in the text trace."""
    with open(path, "rb") as fh:
        for raw in fh:
            line = raw.decode("latin-1", "replace").rstrip("\n")
            # [<ms>] O rc=<rc> len=<n> <hex>
            if "] O " not in line:
                continue
            try:
                ts = int(line[1:line.index("]")])
            except Exception:
                continue
            hx = line.rsplit(" ", 1)[-1].strip()
            try:
                b = binascii.unhexlify(hx)
            except Exception:
                continue
            i = 0
            while i + 4 <= len(b):
                if b[i] != 0xFF:
                    i += 1
                    continue
                ln = b[i + 2]
                end = i + 3 + ln + 1
                if end > len(b):
                    break
                f = b[i:end]
                if f[1] == 0x67 and len(f) >= 6:
                    yield ts, (f[3] << 8) | f[4], f[5:-1]
                i = end


def images(path):
    got = {}
    for ts, addr, p in out_frames_from_log(path):
        if addr != 0x0122 or not p:
            continue
        idx = p[0]
        if idx >= N_CHUNKS:
            continue
        want = 60 if idx == 34 else 250
        payload = bytes(p[1:1 + want])
        if len(payload) != want:
            continue
        if idx == 34:
            got = {}
        got[idx] = payload
        if idx == 0 and len(got) == N_CHUNKS:
            img = bytearray(IMG_BYTES)
            for c, pay in got.items():
                img[c * 250:c * 250 + len(pay)] = pay
            got = {}
            yield ts, bytes(img)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log", nargs="?", default="/tmp/reaper_uf1_frames.log")
    ap.add_argument("--min-lit", type=int, default=2000,
                    help="threshold for a 'live' (non-silent) image")
    a = ap.parse_args()

    imgs = list(images(a.log))
    if not imgs:
        sys.exit(f"no complete 0x0122 images in {a.log} — was the Meter/Overview "
                 f"view up and REASIXTY_UF1_TRACE=1 set at REAPER launch?")

    lits = [lit_nibbles(i) for _, i in imgs]
    live = sum(1 for x in lits if x >= a.min_lit)
    # longest contiguous live run — a real animation, not scattered flickers
    best = cur = 0
    for x in lits:
        cur = cur + 1 if x >= a.min_lit else 0
        best = max(best, cur)

    span = (imgs[-1][0] - imgs[0][0]) / 1000.0 if len(imgs) > 1 else 0.0
    print(f"images:        {len(imgs)}  over {span:.1f}s "
          f"({len(imgs)/span:.1f}/s)" if span else f"images: {len(imgs)}")
    print(f"lit nibbles:   min={min(lits)} max={max(lits)} "
          f"mean={sum(lits)//len(lits)}")
    print(f"live images:   {live}/{len(imgs)} (>= {a.min_lit} lit)  "
          f"longest run={best}")
    if max(lits) >= a.min_lit:
        print("VERDICT: our images CARRY SIGNAL. If the UF1 is black, the "
              "problem is emission/timing, not the codec.")
    else:
        print("VERDICT: our images are near-blank. Wrong plug-in instance "
              "(silent floor), or no audio, or view not on Overview.")


if __name__ == "__main__":
    main()
