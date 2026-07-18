#!/usr/bin/env python3
"""Extract SSL's own goniometer images from a capture -> gonio_replay.bin.

The extension streams this file's images through the LIVE Overview cycle
(uf1GonioReplayNext_ in main.cpp) instead of rendering its own. That makes the
decisive bisection possible with one binary:

    SSL's pixels + our emission path  ->  draws?    content is the problem
                                      ->  black?    emission is the problem

The generator snippets from the 2026-07-17 StoerPC session were lost with the
session; this is the same job as a committed tool.

Wire format (8ecaec5, cap89/cap101): 0x0122 is chunked, byte[0] = chunk index
0..34, then 250 payload bytes (chunk 34 carries 60). Chunks DESCEND 34 -> 0 and
chunk 0 latches, so an image is complete when 0 arrives with all others present.

Usage:
  uf1_gonio_extract_replay.py <pcap> [-o gonio_replay.bin] [-n 120] [--min-lit 2000]
"""
import argparse
import binascii
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from uf1_vu_needle_fit import out_frames   # noqa: E402

IMG_BYTES = 8560
N_CHUNKS = 35


def lit_nibbles(img):
    """Count non-zero 4bpp cells — THE codec (2026-07-17): two pixels per byte."""
    n = 0
    for b in img:
        if b >> 4:
            n += 1
        if b & 0x0F:
            n += 1
    return n


def images(pcap, dev=None):
    """Yield (ts, bytes) for every complete 35-chunk 0x0122 image."""
    got = {}
    for ts, addr, p in out_frames(pcap, dev):
        if addr != 0x0122 or not p:
            continue
        idx = p[0]
        if idx >= N_CHUNKS:
            continue
        want = 60 if idx == 34 else 250
        payload = bytes(p[1:1 + want])
        if len(payload) != want:
            continue
        if idx == 34:                      # 34 leads: a new image starts here
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
    ap.add_argument("pcap")
    ap.add_argument("-o", "--out", default="gonio_replay.bin")
    ap.add_argument("-n", "--count", type=int, default=120,
                    help="images to keep (~24.5/s -> 120 = 5 s loop)")
    ap.add_argument("--min-lit", type=int, default=2000,
                    help="skip near-blank images (silence draws ~1 cell)")
    ap.add_argument("--dev", type=int, default=None)
    a = ap.parse_args()

    # Take the LONGEST CONTIGUOUS run of lit images, not the first N lit ones: a
    # capture is mostly silence (cap101: 2179 images, one 904-image run at
    # t=41..78 s), and a loop spliced across a silence is not the animation SSL
    # drew. Whole-file scan first, then slice — 2000 images is ~19 MB, cheap.
    all_imgs = list(images(a.pcap, a.dev))
    best, cur = [], []
    for ts, img in all_imgs:
        if lit_nibbles(img) >= a.min_lit:
            cur.append(img)
            if len(cur) > len(best):
                best = cur
        else:
            cur = []
    seen, blank = len(all_imgs), sum(1 for _, i in all_imgs
                                     if lit_nibbles(i) < a.min_lit)
    if not best:
        sys.exit(f"no image with >= {a.min_lit} lit nibbles "
                 f"({seen} images seen, {blank} near-blank)")
    # Middle of the run: its edges are the fade in/out around the audio.
    if len(best) > a.count:
        off = (len(best) - a.count) // 2
        best = best[off:off + a.count]
    kept = best

    with open(a.out, "wb") as f:
        for img in kept:
            f.write(img)
    lits = [lit_nibbles(i) for i in kept]
    print(f"{len(kept)} images -> {a.out} ({len(kept) * IMG_BYTES} B)")
    print(f"lit nibbles: min={min(lits)} max={max(lits)} "
          f"mean={sum(lits) // len(lits)}  ({seen} seen, {blank} near-blank)")


if __name__ == "__main__":
    main()
