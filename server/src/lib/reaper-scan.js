// Read the plug-in "Developer" (what the exchange calls vendor) out of REAPER's
// own scan caches. REAPER recognises every installed plug-in's developer; our
// seed maps predate v2 so they carry no original_name, which is why they show
// null. This recovers the developer by matching a map's `match` against the
// cache display names.
//
// Runs only where REAPER's resource dir exists (i.e. on the dev machine, during
// seeding) — the server can't reach a stranger's caches, so real uploads carry
// the developer in their v2 original_name instead. Returns null when there is
// no cache or no confident match; the caller falls back.

import { readFileSync, readdirSync, existsSync } from 'node:fs';
import { homedir } from 'node:os';
import { join } from 'node:path';
import { vendorFromName } from './vendor.js';

const DEFAULT_DIR = join(homedir(), 'Library/Application Support/REAPER');

const FX_PREFIX = /^(VST3|VST|AU|CLAP|JS):\s*/i;

// The plug-in NAME with the developer and trailing junk stripped, casefolded —
// the key both sides match on.
function cleanName(s) {
  let n = String(s ?? '').replace(FX_PREFIX, '').split('!!!')[0];
  n = n.replace(/\s*\[[^\]]*\]\s*$/, '');            // JSFX [path]
  let prev;
  do { prev = n; n = n.replace(/\s*\([^)]*\)?\s*$/, '').trim(); } while (n !== prev && n);
  return n.trim().toLowerCase();
}

let INDEX = null;   // [{ clean, developer, full }]

function buildIndex(dir) {
  const idx = [];
  const push = (full, developer) => { if (full) idx.push({ clean: cleanName(full), developer, full }); };
  const read = (fn) => {
    const p = join(dir, fn);
    return existsSync(p) ? readFileSync(p, 'utf8').split('\n') : [];
  };

  // VST / VST3 — "<file>=<ts>,<id>,<Display Name> (<Developer>)…"
  for (const fn of ['reaper-vstplugins_arm64.ini', 'reaper-vstplugins64.ini', 'reaper-vstplugins.ini']) {
    const lines = read(fn);
    if (!lines.length) continue;
    for (const line of lines) {
      if (!line.includes('=') || line.startsWith('[')) continue;
      const parts = line.split('=', 2)[1] === undefined ? [] : line.slice(line.indexOf('=') + 1).split(',');
      if (parts.length >= 3) {
        const disp = parts.slice(2).join(',').trim();
        push(disp, vendorFromName(disp, 'VST'));
      }
    }
    break;                                            // first cache that exists wins
  }

  // AU — "<Developer>: <Name>=<!inst>"
  for (const fn of ['reaper-auplugins_arm64.ini', 'reaper-auplugins64.ini']) {
    const lines = read(fn);
    if (!lines.length) continue;
    for (const line of lines) {
      const t = line.trim();
      if (!t.includes('=') || t.startsWith('[')) continue;
      const key = t.slice(0, t.indexOf('='));
      if (key.includes(': ')) {
        const dev = key.slice(0, key.indexOf(': ')).trim();
        const name = key.slice(key.indexOf(': ') + 2).trim();
        push(name, dev || null);
      }
    }
    break;
  }

  // CLAP — "<id>=<n>|<Name> (<Developer>)"
  try {
    for (const fn of readdirSync(dir)) {
      if (fn.startsWith('reaper-clap-') && fn.endsWith('.ini') && !fn.includes('conflict')) {
        for (const line of read(fn)) {
          if (line.includes('|')) {
            const disp = line.slice(line.indexOf('|') + 1).trim();
            push(disp, vendorFromName(disp, 'CLAP'));
          }
        }
      }
    }
  } catch { /* no dir */ }

  // JSFX — 'NAME <path> "JS: <Name> (<Developer>) [<author/script>]"'.
  // Most JSFX carry no "(Developer)"; the author is the "[author/...]" path
  // prefix instead, which REAPER shows as the developer.
  for (const line of read('reaper-jsfx.ini')) {
    const m = /^NAME \S+ "(.*)"/.exec(line.trim());
    if (!m) continue;
    let dev = vendorFromName(m[1], 'JS');
    if (!dev) {
      const author = /\[([^\]/]+)\//.exec(m[1]);   // "[loser/4BandEQ]" -> "loser"
      if (author) dev = author[1].trim();
    }
    push(m[1], dev);
  }

  return idx;
}

function loadIndex(dir) {
  if (INDEX) return INDEX;
  INDEX = existsSync(dir) ? buildIndex(dir) : [];
  return INDEX;
}

// A whole-word containment test, so "MicroShift" matches "Little MicroShift"
// but "TIGERT" does NOT match inside "FIRETHETIGERT".
function wordContains(haystack, needle) {
  if (!needle) return false;
  const i = haystack.indexOf(needle);
  if (i < 0) return false;
  const before = i === 0 ? ' ' : haystack[i - 1];
  const after = i + needle.length >= haystack.length ? ' ' : haystack[i + needle.length];
  return !/\w/.test(before) && !/\w/.test(after);
}

// A shortened match ("MicroShift" ⊂ "Little MicroShift") or a padded one
// ("FabFilter Pro-C 2" ⊃ "Pro-C 2") should match — but only when the shared
// part is substantial. A 5-char floor stops a cache "EQ" from matching inside
// "4-Band EQ" (which produced a bogus Airwindows hit).
const MIN_SHARED = 5;

/**
 * Developer for a map's `match`, from REAPER's caches, or null. Tiers, most
 * confident first: exact clean-name, then whole-word containment, then a plain
 * substring — the last two only when the shorter side is >= MIN_SHARED chars.
 * Only entries that actually carry a developer count.
 */
export function developerForMatch(match, { dir = process.env.REAPER_DIR ?? DEFAULT_DIR } = {}) {
  const idx = loadIndex(dir);
  if (!idx.length) return null;
  const m = cleanName(match);
  if (!m) return null;

  const withDev = idx.filter((e) => e.developer);
  const exact = withDev.find((e) => e.clean === m);
  if (exact) return exact.developer;

  const longEnough = (e) => Math.min(m.length, e.clean.length) >= MIN_SHARED;
  const contains = (e) => e.clean.includes(m) || m.includes(e.clean);

  const word = withDev.find((e) => longEnough(e)
    && (wordContains(e.clean, m) || wordContains(m, e.clean)));
  if (word) return word.developer;

  const loose = withDev.find((e) => longEnough(e) && contains(e));
  return loose ? loose.developer : null;
}

/** For tests / re-seeding after cache changes. */
export function _resetIndex() { INDEX = null; }
