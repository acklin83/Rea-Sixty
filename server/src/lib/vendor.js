// Vendor and plug-in name handling.
//
// Vendor comes from REAPER, inside the name string — there is no vendor key in
// the API (TrackFX_GetNamedConfigParm exposes only fx_ident / fx_name /
// fx_type / original_name / renamed_name). `original_name` is that string, and
// the extension already reads it (PluginMap.cpp:426-447).
//
// THREE CONVENTIONS, so read the format FIRST:
//     VST / VST3   "Name (Vendor)"
//     CLAP         "Name (Vendor)"
//     AU           "Vendor: Name"            <- PREFIX, not parenthetical
//     JSFX         "JS: Name (Vendor) [path]"
//
// Measured over 1817 plug-ins in REAPER's own scan caches: VST 99%, AU 100%,
// CLAP 100%, JSFX 8% — 88% overall. JSFX is the weak case (community scripts
// mostly carry no vendor), which is why uploads need a vendor field.

// A trailing parenthetical is NOT automatically the vendor: channel specs like
// "(2->6ch)", "(32 out)", "(mono)" and the "!!!VSTi" suffix sit in exactly the
// same position. Parsing `match` produced "mono" from "… (SSL) (mono)".
// `ch$` alone is too greedy and it bites: it rejects the real JSFX vendor
// "DocShadrach" because the name happens to end in "ch". A channel count is
// always digit-led ("6ch", "2->6ch"), so require the digits.
import { readFileSync } from 'node:fs';

const CHANNEL_SPEC = /^\d+\s*(out|in)$|->|^\d+\s*ch$|^mono$|^stereo$/i;

const FX_TYPE_PREFIX = /^(VST3|VST|AU|CLAP|JS):\s*/i;

/** Format tag from a display name's prefix, or null. */
export function fxTypeOf(name) {
  const m = FX_TYPE_PREFIX.exec(name ?? '');
  return m ? m[1].toUpperCase() : null;
}

/** Last parenthetical that is not a channel spec. VST / CLAP / JSFX. */
function parentheticalVendor(name) {
  const base = String(name ?? '').split('!!!')[0].trim();
  const found = [...base.matchAll(/\(([^()]*)\)/g)]
    .map((m) => m[1].trim())
    .filter((p) => p && !CHANNEL_SPEC.test(p));
  return found.length ? found[found.length - 1] : null;
}

/**
 * Best-effort vendor from a full display name (an `original_name`).
 *
 * NOTE this is for seeding and for suggesting a value on the upload form.
 * The authoritative vendor is captured at EXPORT time by the extension and
 * carried in the envelope, because `match` is a user-editable substring:
 * only 8 of the 29 maps in the live catalog yield anything from it, and one
 * of those yields "mono".
 */
export function vendorFromName(name, fxType = null) {
  const raw = String(name ?? '').trim();
  if (!raw) return null;
  const type = fxType ?? fxTypeOf(raw);
  const body = raw.replace(FX_TYPE_PREFIX, '');

  if (type === 'AU') {
    // "Vendor: Name" — the prefix, not a parenthetical.
    const i = body.indexOf(': ');
    return i > 0 ? body.slice(0, i).trim() || null : null;
  }
  // JSFX carries a trailing [path] that must come off before the parenthetical
  // scan, or the path wins.
  return parentheticalVendor(body.replace(/\s*\[[^\]]*\]\s*$/, ''));
}

/**
 * Normalise for matching. TRIM FIRST, then fold — this is not pedantry:
 * two AU vendors ("Celemony ", "Synchro Arts ") carry a trailing space and
 * counted as separate vendors until stripped.
 *
 * Aliasing proper ("Universal Audio (UADx)" 72 plug-ins vs "UADx" 70) is a
 * table lookup, not a string rule — see vendor_aliases.
 */
export function normVendor(name) {
  return String(name ?? '')
    .trim()
    .replace(/\s+/g, ' ')
    .toLowerCase();
}

// Known vendor aliases (data/vendor-aliases.json): REAPER spells some vendors
// more than one way ("UADx" vs "Universal Audio (UADx)"), and some companies
// ship under a product-line name (Acustica's "Acqua"). Fold each to one
// canonical display name. Built once, keyed by normalised alias.
const VENDOR_ALIASES = (() => {
  try {
    const cfg = JSON.parse(
      readFileSync(new URL('../../data/vendor-aliases.json', import.meta.url), 'utf8'),
    );
    const map = new Map();
    for (const g of cfg.groups ?? []) {
      // The canonical name is its own alias, so it always resolves to itself.
      for (const a of [g.canonical, ...(g.aliases ?? [])]) {
        map.set(normVendor(a), g.canonical);
      }
    }
    return map;
  } catch {
    return new Map();
  }
})();

/**
 * Canonical display name for a vendor, applying the known-alias table. Returns
 * the trimmed input unchanged when it is not a known alias.
 */
export function canonicalVendor(name) {
  const trimmed = String(name ?? '').trim().replace(/\s+/g, ' ');
  return VENDOR_ALIASES.get(normVendor(trimmed)) ?? trimmed;
}

/**
 * Remove one trailing "(...)" group, tolerating nesting and a missing close.
 * Returns the input unchanged when there is no trailing group.
 */
function stripTrailingParenGroup(input) {
  const s = input.trimEnd();
  if (s.endsWith(')')) {
    let depth = 0;
    for (let i = s.length - 1; i >= 0; i--) {
      if (s[i] === ')') depth++;
      else if (s[i] === '(') {
        depth--;
        if (depth === 0) return s.slice(0, i);
      }
    }
    return s;                        // unbalanced close — leave it alone
  }
  // Unclosed group: "Name (Vendor" — cut at the last "(" that never closes.
  const open = s.lastIndexOf('(');
  if (open >= 0 && !s.slice(open).includes(')')) return s.slice(0, open);
  return s;
}

/**
 * Canonical plug-in name from a `match` string, for display and for the
 * auto-merge key.
 *
 * This CANNOT fully solve identity — `match` is user-editable and the live
 * catalog holds both "FabFilter Pro-C 2" and "Pro-C 2" for the same plug-in,
 * plus "UADx 1176 Rev A Compressor (Universal Audio" truncated mid-vendor
 * (the paren never closes). It gets the easy cases; plugin_aliases and the
 * uploader's confirmation handle the rest.
 */
export function pluginNameFromMatch(match) {
  let s = String(match ?? '').replace(FX_TYPE_PREFIX, '').trim();
  s = s.replace(/\s*\[[^\]]*\]\s*$/, '');       // JSFX [path]
  s = s.split('!!!')[0];
  // Drop trailing parenthetical groups REPEATEDLY. Three shapes occur in real
  // catalog data and a single regex handles none of them together:
  //   "SSL Delta Control 16 (SSL) (mono)"                    two groups
  //   "UADx API Vision Channel Strip (Universal Audio (UADx))"  NESTED
  //   "UADx 1176 Rev A Compressor (Universal Audio"          never closed
  // Nesting is why this is a scanner and not a regex: [^)]* cannot span the
  // inner ")", so the whole group silently fails to match and the vendor stays
  // glued to the plug-in name.
  let prev;
  do {
    prev = s;
    s = stripTrailingParenGroup(s).trim();
  } while (s !== prev && s !== '');
  return s.trim();
}

export function normPlugin(name) {
  return String(name ?? '')
    .trim()
    .replace(/\s+/g, ' ')
    .toLowerCase();
}

export function slugify(name) {
  return String(name ?? '')
    .normalize('NFKD')
    .replace(/[̀-ͯ]/g, '')
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '')
    .slice(0, 80) || 'plugin';
}
