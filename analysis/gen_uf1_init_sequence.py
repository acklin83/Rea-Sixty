#!/usr/bin/env python3
"""Generate extension/src/uf1_init_sequence.inc from cap66_uf1_init.pcapng.

UF1 analog of gen_init_sequence.py (UF8). Captures the cold-start init burst
SSL 360 sends over EP 0x02 OUT: wake/mode opcodes -> per-LED init sweep
(FF 38/39/3B triplets) -> initial FF 67 screen paint. Replayed in-order after
claiming the interface, before any of our own control/LED/screen frames.

Init window pinned from capture analysis (2026-06-04):
  - UF1 = usb.device_address 12 in cap66 (re-enumerated dev 11->12 on SSL restart).
  - Burst onset t=14.07s; clean init (wake + LED sweep + first paint) done by
    t=14.6s, after which it is pure ~100/s steady-state repaints (excluded).
  - Window [14.06, 14.60] = 360 blobs: 11 wake opcodes, 73x FF38/39/3B LED
    triplets, 128 FF67 paint frames, 1 FF1B keepalive (filtered), 1 all-zero
    descriptor artifact (filtered) -> ~358 init frames.

Usage:
  python3 analysis/gen_uf1_init_sequence.py captures/cap66_uf1_init.pcapng
"""
import subprocess
import sys
from pathlib import Path

CAP = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('captures/cap66_uf1_init.pcapng')
OUT_INC = Path(__file__).parent.parent / 'extension/src/uf1_init_sequence.inc'

# UF1 device address + init window in cap66 (see module docstring).
#
# The window now spans the cold-start burst AND the fader-dance calibration.
# In cap66 the dance runs t=14.467..22.570: FF1E (target) + FF1D engage drive
# the fader to each extreme (pos 0x0000 bottom and 0x7FFF top, multiple times,
# ~500 ms dwell each so the motor physically reaches the rail and the firmware
# learns the travel endpoints), then FF1D release. This is calibration, not
# cosmetic — Frank flagged the fader sitting dead at -12 without it.
#
# To keep the dance's load-bearing motor timing without replaying ~8 s of
# steady FF67 screen repaints that stream alongside it, we keep EVERYTHING in
# the initial burst [T_START, BURST_END] but ONLY motor frames (FF1D/FF1E)
# after BURST_END.
UF1_DEV_ADDR = 12
T_START   = 14.06
BURST_END = 14.62    # wake + LED sweep + first paint done by here
T_END     = 22.70    # through the last FF1D release of the dance (22.570)
GAP_PACING_MS_THRESHOLD = 50    # any gap >= this before a motor frame -> explicit delay
MAX_MOTOR_DELAY_MS      = 800   # cap dead gaps (the one ~2.5 s idle gap) while
                                # preserving every ~500 ms motor-travel dwell


def is_worker_heartbeat(data: bytes) -> bool:
    """Frames the UF1Device worker thread emits autonomously. Replaying these
    in the init creates double keepalives that desync the FF 1B counter. The
    UF1 worker only sends the PM keepalive FF 1B 01 <ctr> <ck> (no FF 5B / FF 66
    family — those are UF8/UC1-only)."""
    if len(data) < 3 or data[0] != 0xff:
        return False
    if data[1] == 0x1b and data[2] == 0x01:
        return True
    return False


def collect_out_frames():
    """Return list of (t_relative, bytes) for UF1 OUT frames in the init window.
    Skips the all-zero descriptor artifact and worker-managed heartbeats."""
    p = subprocess.run([
        'tshark', '-r', str(CAP),
        '-Y', f'usb.device_address == {UF1_DEV_ADDR} && usb.endpoint_address == 0x02 && usb.capdata',
        '-T', 'fields', '-e', 'frame.time_relative', '-e', 'usb.capdata',
    ], capture_output=True, text=True, check=True)
    frames = []
    skipped_hb = 0
    skipped_zero = 0
    skipped_post = 0
    for line in p.stdout.split('\n'):
        parts = line.strip().split('\t')
        if len(parts) < 2:
            continue
        t = float(parts[0])
        if t < T_START or t > T_END:
            continue
        data = bytes.fromhex(parts[1])
        if all(b == 0 for b in data):
            skipped_zero += 1
            continue
        if is_worker_heartbeat(data):
            skipped_hb += 1
            continue
        # After the initial burst, keep ONLY fader-dance motor frames
        # (FF 1D enable / FF 1E position) — drop the steady FF 67 repaints
        # that stream alongside the dance.
        if t > BURST_END:
            is_motor = len(data) >= 2 and data[0] == 0xFF and data[1] in (0x1D, 0x1E)
            if not is_motor:
                skipped_post += 1
                continue
        frames.append((t, data))
    print(f'Filtered {skipped_zero} all-zero + {skipped_hb} worker-heartbeat '
          f'+ {skipped_post} post-burst non-motor frames')
    return frames


def main():
    frames = collect_out_frames()
    if not frames:
        print('ERROR: no frames collected — check capture path, device address, time window',
              file=sys.stderr)
        sys.exit(1)

    print(f'Collected {len(frames)} OUT frames from {CAP.name}')
    print(f'Window: t={frames[0][0]:.3f}s .. t={frames[-1][0]:.3f}s '
          f'({frames[-1][0] - frames[0][0]:.2f}s)')
    # Sanity: the documented init burst is ~358 frames. Warn loudly if far off —
    # a wrong device address or window silently yields the wrong count.
    if not (300 <= len(frames) <= 420):
        print(f'WARNING: {len(frames)} frames is outside the expected ~358 init '
              f'range — re-check UF1_DEV_ADDR / T_START / T_END.', file=sys.stderr)

    # Pace motor-control frames (FF 1D engage / FF 1E target) that follow a gap,
    # mirroring the UF8 generator. The UF1 init has a single FF 1D; pacing is a
    # no-op unless a load-bearing gap precedes it, but keep the logic for parity.
    def is_motor_ctl(d):
        return len(d) >= 2 and d[0] == 0xff and d[1] in (0x1d, 0x1e)
    entries = []
    last_motor_t = None
    for t, data in frames:
        delay_ms = 0
        if is_motor_ctl(data) and last_motor_t is not None:
            gap_ms = int(round((t - last_motor_t) * 1000))
            if gap_ms >= GAP_PACING_MS_THRESHOLD:
                delay_ms = min(gap_ms, MAX_MOTOR_DELAY_MS)
        if is_motor_ctl(data):
            last_motor_t = t
        entries.append((delay_ms, data))

    paced = sum(1 for d, _ in entries if d > 0)
    total_pace_ms = sum(d for d, _ in entries)
    print(f'Frames with explicit pacing: {paced}/{len(entries)} '
          f'(total pause: {total_pace_ms} ms)')

    out = []
    out.append('// Auto-generated by analysis/gen_uf1_init_sequence.py from')
    out.append(f'// {CAP.name} (2026-06-04).')
    out.append('//')
    out.append('// SSL 360 UF1 cold-start init: wake/mode opcodes -> per-LED init')
    out.append('// sweep (FF 38/39/3B triplets) -> initial FF 67 screen paint -> fader-')
    out.append('// dance calibration (FF 1E target + FF 1D engage/release driving the')
    out.append('// fader to both rails so the firmware learns the travel endpoints).')
    out.append('// Replays in-order on EP 0x02 OUT after claiming the interface, before')
    out.append('// any of our own control/LED/screen frames. UF1 analog of the UF8')
    out.append('// 752-frame init (init_sequence.inc).')
    out.append('//')
    out.append('// Frames where delay_ms_before > 0 sleep that long BEFORE sending.')
    out.append('')
    out.append('#pragma once')
    out.append('#include <array>')
    out.append('#include <cstddef>')
    out.append('#include <cstdint>')
    out.append('')
    out.append('namespace uf1 {')
    out.append('')
    out.append('struct InitFrame {')
    out.append('    const uint8_t* bytes;')
    out.append('    std::size_t   size;')
    out.append('    std::uint32_t delay_ms_before;')
    out.append('};')
    out.append('')
    for i, (_delay_ms, data) in enumerate(entries):
        hex_bytes = ', '.join(f'0x{b:02x}' for b in data)
        out.append(f'static constexpr std::uint8_t kUf1InitFrame{i}[] = {{ {hex_bytes} }};')
    out.append('')
    out.append(f'static constexpr std::array<InitFrame, {len(entries)}> kUf1InitSequence = {{{{')
    for i, (delay_ms, _data) in enumerate(entries):
        out.append(f'    {{ kUf1InitFrame{i}, sizeof(kUf1InitFrame{i}), {delay_ms} }},')
    out.append('}};')
    out.append('')
    out.append('} // namespace uf1')
    out.append('')

    OUT_INC.write_text('\n'.join(out))
    print(f'Wrote {OUT_INC} ({len(entries)} frames)')


if __name__ == '__main__':
    main()
