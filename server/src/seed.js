// Seed the exchange from the live catalog on this machine.
//
// "An empty platform reads as a dead one; this one launches with content."
// The 29 maps on the dev machine are that content. Each is wrapped in the same
// envelope the extension's Export... writes, so this exercises the real ingest
// path rather than a shortcut around it.
//
//   node src/seed.js [--reset]

import { readFileSync, rmSync } from 'node:fs';
import { homedir } from 'node:os';
import { join } from 'node:path';
import { getDb, migrate, now, DATA_DIR } from './db/index.js';
import { ingestMap } from './ingest/service.js';
import { IngestError } from './lib/rea60map.js';
import { developerForMatch } from './lib/reaper-scan.js';

const CATALOG = process.env.CATALOG
  ?? join(homedir(), 'Library/Application Support/REAPER/rea_sixty/user_plugins.json');

// surfaceScope(), UserPluginCatalog.cpp:1357.
function surfaceScope(map) {
  if (map.domain === 'None') return map.uf8Mode ? 'uf8' : '';
  return map.uf8Mode ? 'uc1+uf8' : 'uc1';
}

function envelopeFor(map, author) {
  return JSON.stringify({
    format: 'rea-sixty-map',
    version: 2,
    plugin: map.match,
    // Passthrough: empty on a catalog written before the extension captured
    // original_name; populated once the user has focused each plug-in live.
    original_name: map.originalName ?? '',
    // Seed maps predate v2, so recover the developer from REAPER's scan caches
    // (what REAPER shows as "Developer") — otherwise these all read null.
    // Empty when the cache can't resolve it (e.g. author-less JSFX).
    vendor: developerForMatch(map.match) ?? '',
    surfaces: surfaceScope(map),
    author,
    description: '',
    licence: 'CC0-1.0',
    created_at: now(),
    map: JSON.stringify({ format_version: 10, plugins: [{ ...map, isDefault: false }] }),
  });
}

if (process.argv.includes('--reset')) {
  rmSync(DATA_DIR, { recursive: true, force: true });
  console.log('reset: removed', DATA_DIR);
}

migrate();
const db = getDb();

const AUTHOR = process.env.SEED_AUTHOR ?? 'Frank';
let account = db.prepare('SELECT id FROM accounts WHERE display_name = ?').get(AUTHOR);
if (!account) {
  const id = db.prepare('INSERT INTO accounts (display_name, created_at, is_admin) VALUES (?,?,1)')
    .run(AUTHOR, now()).lastInsertRowid;
  account = { id: Number(id) };
  console.log(`created admin account "${AUTHOR}" (id ${account.id})`);
}

const catalog = JSON.parse(readFileSync(CATALOG, 'utf8'));
const maps = catalog.plugins ?? [];
let ok = 0;
const failed = [];

for (const map of maps) {
  const scope = surfaceScope(map);
  if (!scope) {
    // (domain=None, uf8Mode=false) — the invalid pair the extension filters.
    failed.push([map.match, 'invalid (domain, uf8Mode) pair']);
    continue;
  }
  try {
    ingestMap(envelopeFor(map, AUTHOR), { accountId: account.id });
    ok++;
  } catch (e) {
    failed.push([map.match, e instanceof IngestError ? `[${e.code}] ${e.message}` : e.message]);
  }
}

console.log(`\nseeded ${ok}/${maps.length} maps`);
for (const [name, why] of failed) console.log(`  SKIPPED ${name} — ${why}`);

const counts = db.prepare(`
  SELECT (SELECT COUNT(*) FROM plugins) AS plugins,
         (SELECT COUNT(*) FROM vendors) AS vendors,
         (SELECT COUNT(*) FROM maps)    AS maps,
         (SELECT COUNT(*) FROM map_bindings) AS bindings
`).get();
console.log(`\n${counts.plugins} plug-ins · ${counts.vendors} vendors · ${counts.maps} maps · ${counts.bindings} bindings`);
