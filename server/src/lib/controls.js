// Access to the generated control table.
//
// The table is produced by extension/tools/gen_control_table.py from the C++
// source of truth and committed as data/uc1-controls.json. It is NOT parsed
// from C++ at runtime — the throwaway prototype did that, and its own README
// calls it the second-source-of-truth the plan warns against. CI runs the
// generator with --check so a control rename fails the build instead of
// silently blanking part of the exchange.

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const TABLE = JSON.parse(
  readFileSync(new URL('../../data/uc1-controls.json', import.meta.url), 'utf8'),
);

// UF8-only maps have a different denominator entirely: 2 fader banks x 8 V-Pot
// banks x 8 strips = 128 slots, plus 2 x 8 = 16 strip bindings.
// UserPluginCatalog.h:402/:470.
export const UF8_VPOT_SLOTS = 2 * 8 * 8;
export const UF8_STRIPS = 2 * 8;

/**
 * Resolve one slot. linkIdx IS NAMESPACED PER DOMAIN — CS 1 is FaderLevel,
 * BC 1 is Threshold. Passing the wrong domain silently mislabels every row,
 * so domain is required, never defaulted.
 *
 * Returns null for a linkIdx with no canonical slot (e.g. an ext:: synthetic
 * the table does not carry). Callers must still surface those as "also
 * mapped" rather than dropping them.
 */
export function lookupSlot(domain, linkIdx) {
  const d = TABLE.domains[domain];
  if (!d) return null;
  return d.slots[String(linkIdx)] ?? null;
}

/** Domain-scoped coverage denominator: ChannelStrip 33, BusComp 8. */
export function denominatorFor(domain) {
  const d = TABLE.domains[domain];
  if (!d) throw new Error(`no control table for domain ${domain}`);
  return d.denominator;
}

/** Every on-face slot for a domain, in table order — drives the coverage strip. */
export function faceSlots(domain) {
  const d = TABLE.domains[domain];
  if (!d) return [];
  return Object.entries(d.slots)
    .filter(([, s]) => s.on_face)
    .map(([linkIdx, s]) => ({ linkIdx: Number(linkIdx), ...s }));
}

export const SECTION_ORDER = [
  'Filters', 'HF', 'HMF', 'EQ', 'LMF', 'LF', 'Comp', 'Gate', 'Bus Comp', 'Channel',
];

export default TABLE;
