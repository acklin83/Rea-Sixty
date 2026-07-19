#!/usr/bin/env python3
"""
Extract the plug-in vendor list from REAPER's own scan caches.

Answers the question the plan spent a while marked UNVERIFIED: where does a
plug-in's manufacturer come from? Not from the API — TrackFX_GetNamedConfigParm
exposes only fx_ident / fx_name / fx_type / original_name / renamed_name, and
none of those is a vendor. It is inside the display name, and `original_name`
(which the extension already reads, PluginMap.cpp:426-447) is that string.

Measured on the dev machine, 1817 plug-ins: VST 99%, AU 100%, CLAP 100%,
JSFX 8% — 88% overall, 82 distinct vendors.

THREE CONVENTIONS. Read fx_type (or the name's prefix) first:
    VST / VST3   "Name (Vendor)"
    CLAP         "Name (Vendor)"
    AU           "Vendor: Name"          <- prefix, not parenthetical
    JSFX         "JS: Name (Vendor) [path]"

And a trailing parenthetical is NOT automatically the vendor: channel specs like
"(2->6ch)" and "(32 out)", plus the "!!!VSTi" suffix, sit in the same position.

Output (vendors.json) is deliberately gitignored — it lists every plug-in vendor
installed on whichever machine ran this, which does not belong in the repo. Run
it yourself to regenerate.

    python3 extract_vendors.py [--reaper-dir PATH] > /dev/null
"""
import argparse
import collections
import json
import os
import re
import sys

CHANNEL_SPEC = re.compile(r'^\d+\s*(out|in)$|->|ch$|^mono$|^stereo$', re.I)


def parenthetical_vendor(name):
    """Last parenthetical that is not a channel spec. Used for VST / CLAP / JSFX."""
    name = name.split('!!!')[0].strip()
    candidates = [p for p in re.findall(r'\(([^()]*)\)', name)
                  if not CHANNEL_SPEC.search(p.strip())]
    return candidates[-1].strip() if candidates else None


def scan(reaper_dir):
    counts = collections.Counter()
    stats = collections.Counter()

    def bump(fmt, vendor):
        stats[f'{fmt}.total'] += 1
        if vendor:
            stats[f'{fmt}.found'] += 1
            counts[vendor] += 1

    # VST / VST3 — "<file>=<ts>,<id>{<guid>,<Display Name> (<Vendor>)..."
    for fn in ('reaper-vstplugins_arm64.ini', 'reaper-vstplugins64.ini',
               'reaper-vstplugins.ini'):
        path = os.path.join(reaper_dir, fn)
        if not os.path.exists(path):
            continue
        for line in open(path, errors='replace'):
            if '=' not in line or line.startswith('['):
                continue
            parts = line.split('=', 1)[1].split(',')
            if len(parts) >= 3:
                bump('VST', parenthetical_vendor(','.join(parts[2:])))
        break                                   # first one that exists wins

    # AU — "<Vendor>: <Name>=<!inst>"
    for fn in ('reaper-auplugins_arm64.ini', 'reaper-auplugins64.ini'):
        path = os.path.join(reaper_dir, fn)
        if not os.path.exists(path):
            continue
        for line in open(path, errors='replace'):
            line = line.strip()
            if '=' not in line or line.startswith('['):
                continue
            key = line.split('=', 1)[0]
            bump('AU', key.split(': ', 1)[0].strip() if ': ' in key else None)
        break

    # CLAP — "<id>=<n>|<Name> (<Vendor>)"
    for fn in os.listdir(reaper_dir):
        if fn.startswith('reaper-clap-') and fn.endswith('.ini') and 'conflict' not in fn:
            for line in open(os.path.join(reaper_dir, fn), errors='replace'):
                if '|' in line:
                    bump('CLAP', parenthetical_vendor(line.split('|', 1)[1].strip()))

    # JSFX — 'NAME <path> "JS: <Name> (<Vendor>) [<path>]"'
    path = os.path.join(reaper_dir, 'reaper-jsfx.ini')
    if os.path.exists(path):
        for line in open(path, errors='replace'):
            m = re.match(r'NAME \S+ "(.*)"', line.strip())
            if m:
                bump('JS', parenthetical_vendor(
                    re.sub(r'\s*\[[^\]]*\]\s*$', '', m.group(1))))

    return counts, stats


def main():
    default = os.path.expanduser('~/Library/Application Support/REAPER')
    ap = argparse.ArgumentParser()
    ap.add_argument('--reaper-dir', default=default)
    ap.add_argument('--out', default=os.path.join(os.path.dirname(__file__), 'vendors.json'))
    args = ap.parse_args()

    if not os.path.isdir(args.reaper_dir):
        sys.exit(f'no REAPER resource directory at {args.reaper_dir}')

    counts, stats = scan(args.reaper_dir)
    total = found = 0
    print(f"{'format':6} {'entries':>8} {'vendor':>8} {'%':>5}", file=sys.stderr)
    for fmt in ('VST', 'AU', 'CLAP', 'JS'):
        t, f = stats[f'{fmt}.total'], stats[f'{fmt}.found']
        total += t
        found += f
        if t:
            print(f'{fmt:6} {t:>8} {f:>8} {100*f//t:>4}%', file=sys.stderr)
    print(f"{'TOTAL':6} {total:>8} {found:>8} "
          f"{100*found//max(total,1):>4}%", file=sys.stderr)
    print(f'{len(counts)} distinct vendors', file=sys.stderr)

    json.dump(sorted(([v, c] for v, c in counts.items()), key=lambda kv: -kv[1]),
              open(args.out, 'w'), indent=0)
    print(f'wrote {args.out}', file=sys.stderr)


if __name__ == '__main__':
    main()
