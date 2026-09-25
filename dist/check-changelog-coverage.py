#!/usr/bin/env python3
"""Refuse a release whose ReaPack @changelog does not cover its release notes.

v0.6.0 shipped with 28 bullets for a release the notes described in 73 sections.
The block had been extended forward from an early draft with each cherry-pick
and never once held against the notes, so the Nav Mode rebuild, Sleep, the
hover boxes, the captured GR calibration and the analogue needle were simply
absent from what users read in ReaPack. Nobody noticed until Frank asked.

This is the same shape as dist/check-linux-abi.py: a guard that runs at release
time and says no, rather than a rule someone has to remember.

  python3 dist/check-changelog-coverage.py \
      dist/RELEASE-NOTES-v0.6.0.md \
      ~/Documents/dev/reaper-scripts/Rea-Sixty/Rea-Sixty.ext

Exit 0 = every bolded section of the notes has a keyword footprint in the
changelog. Exit 1 = it names what is missing.
"""
import re, sys

STOP = {'that','this','with','from','what','they','them','when','which','their',
        'have','does','been','into','more','than','only','also','said','says',
        'each','both','where','while','your','you','the','and','for','its'}

def words(s):
    return {w for w in re.findall(r"[a-z0-9']+", s.lower()) if len(w) > 3 and w not in STOP}

def main(notes_path, ext_path, threshold=0.34):
    notes = open(notes_path, encoding='utf-8').read()
    # Überschrift PLUS der Satz dahinter. Eine Überschrift wie "Stability."
    # trägt keine Inhaltswörter; erst der Absatz sagt, wovon sie handelt, und
    # ohne ihn schlägt der Wächter grundlos an.
    heads = []
    for m in re.finditer(r"^\*\*([^*]+)\*\*(.*)$", notes, re.M):
        tail = m.group(2).strip()
        heads.append((m.group(1).strip(), (m.group(1) + ' ' + tail[:220]).strip()))

    ext = open(ext_path, encoding='utf-8').read().split('\n')
    start = next(i for i, l in enumerate(ext) if l.startswith('@changelog'))
    end = next(i for i in range(start + 1, len(ext)) if ext[i].startswith('@'))
    block = '\n'.join(ext[start + 1:end])
    bullets = [l for l in block.split('\n') if l.strip().startswith('-')]
    hay = block.lower()

    # Eine Leerzeile im Block beendet den Metadatenblock: @provides wird nie
    # gelesen und das Paket fliegt mit "no files provided" aus dem Index.
    blanks = [i for i, l in enumerate(ext[start + 1:end], start + 2) if not l.strip()]

    missing = []
    for title, probe in heads:
        w = words(probe)
        if not w:
            continue
        if sum(1 for x in w if x in hay) / len(w) < threshold:
            missing.append(title)

    print("Notes-Abschnitte: %d" % len(heads))
    print("Changelog-Punkte: %d" % len(bullets))
    bad = False
    if blanks:
        print("\n⛔ LEERZEILE im @changelog, Zeile(n): %s" % blanks)
        bad = True
    if missing:
        print("\n⛔ %d Abschnitt(e) ohne Entsprechung im Changelog:" % len(missing))
        for m in missing:
            print("   -", m)
        bad = True
    if bad:
        return 1
    print("\nJeder Abschnitt der Notes kommt im Changelog vor.")
    return 0

if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1], sys.argv[2]))
