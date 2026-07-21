// Run the real ingest parser over the live 29-map catalog on this machine.
//
// The catalog is the same shape serialize_ writes, so wrapping each map in an
// envelope reproduces exactly what the extension's Export... produces. This is
// the closest thing to real corpus we have before anyone uploads, and it is
// also the seed set the plan wants to launch with.
//
//   node tools/verify-against-catalog.js
//
// Nothing here writes to the database; it parses and prints.

import { readFileSync } from 'node:fs';
import { homedir } from 'node:os';
import { join } from 'node:path';
import { parseRea60Map, IngestError } from '../src/lib/rea60map.js';

const CATALOG = process.env.CATALOG
  ?? join(homedir(), 'Library/Application Support/REAPER/rea_sixty/user_plugins.json');

// Mirror surfaceScope() (UserPluginCatalog.cpp:1357): domain None => "uf8",
// otherwise "uc1+uf8" when uf8Mode is set, else "uc1".
function surfaceScope(map) {
  if (map.domain === 'None') return map.uf8Mode ? 'uf8' : '';
  return map.uf8Mode ? 'uc1+uf8' : 'uc1';
}

function envelopeFor(map) {
  const one = { format_version: 10, plugins: [{ ...map, isDefault: false }] };
  return JSON.stringify({
    format: 'rea-sixty-map',
    version: 2,
    plugin: map.match,
    original_name: map.originalName ?? '',
    vendor: '',
    surfaces: surfaceScope(map),
    author: 'seed',
    description: '',
    licence: 'CC0-1.0',
    created_at: Math.floor(Date.now() / 1000),
    map: JSON.stringify(one),
  });
}

const catalog = JSON.parse(readFileSync(CATALOG, 'utf8'));
const maps = catalog.plugins ?? [];
console.log(`catalog: ${maps.length} maps (format_version ${catalog.format_version})\n`);

let ok = 0;
const failures = [];
const rows = [];

for (const map of maps) {
  const name = map.match.slice(0, 44);
  try {
    const r = parseRea60Map(envelopeFor(map));
    ok++;
    const { n, d } = r.coverage;
    const pct = d ? Math.round((100 * n) / d) : 0;
    const offFace = r.bindings.filter((b) => !b.onFace && b.modLayer === 'normal').length;
    const modOnly = r.bindings.filter((b) => b.modLayer !== 'normal').length;
    rows.push({ name, surfaces: r.envelope.surfaces, n, d, pct, offFace, modOnly });
  } catch (e) {
    failures.push({ name, code: e instanceof IngestError ? e.code : 'THREW', msg: e.message });
  }
}

console.log(`${'plug-in'.padEnd(44)} ${'surface'.padEnd(8)} ${'coverage'.padStart(9)} ${'%'.padStart(4)} ${'off'.padStart(4)} ${'mod'.padStart(4)}`);
console.log('-'.repeat(80));
for (const r of rows) {
  console.log(
    `${r.name.padEnd(44)} ${r.surfaces.padEnd(8)} ${`${r.n}/${r.d}`.padStart(9)} `
    + `${`${r.pct}%`.padStart(4)} ${String(r.offFace).padStart(4)} ${String(r.modOnly).padStart(4)}`,
  );
}

console.log(`\nparsed ${ok}/${maps.length}`);
if (failures.length) {
  console.log('\nFAILURES:');
  for (const f of failures) console.log(`  ${f.name.padEnd(44)} [${f.code}] ${f.msg}`);
  process.exitCode = 1;
}
