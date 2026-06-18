#pragma once
//
// Virtual notch / detent helper for relative-encoder parameter writes.
//
// SSL 360° makes pots "snap" to a parameter's neutral point (0 dB on
// EQ gains, 0 on pan) when the rotation crosses or lands near it.
// This is a magnet, not a sticky detent: once snapped you can leave
// on the very next click — the goal is to reliably hit 0, not to
// trap you there.
//
// Two ways the magnet fires:
//   - the rotation crosses through the centre in one event (fast spin)
//   - the rotation enters the zone from outside (slow approach)
//
// Once `cur` is at or inside the zone, subsequent rotations move
// normally so 0.1 dB nudges off-centre still work.
//

namespace uf8 {

double applyVirtualNotch(double cur, double delta, double center,
                         double zone, double lo, double hi);

// Stateful variant with a soft-detent "hold" at the centre. Once the value
// snaps to centre (cross or inward-into-zone), rotation is absorbed — the
// value stays parked at centre — until the accumulated rotation exceeds
// `hold` (in normalised units), at which point it releases outward carrying
// the overshoot. This is what stops an endless encoder from sailing past 0
// dB. `hold` == 0 → behaves exactly like applyVirtualNotch. State is keyed
// by `slot` (0..15); callers pass the strip index. Not thread-safe — call
// only from the single input-drain path.
double applyNotchHold(int slot, double cur, double delta, double center,
                      double zone, double hold, double lo, double hi);

} // namespace uf8
