// Remove every mapping, leaving the accounts alone.
//
//   node src/tools/empty-maps.js --yes
//
// WHY NOT `seed.js --reset`. That deletes the whole var/ directory: the
// database, the uploaded blobs, AND every account, credential and device token
// with it. You would be signed out and the token pasted into REAPER would stop
// working — which is not what "empty the mappings" means. This clears the
// corpus and keeps the people.
//
// WHAT GOES: maps, and by cascade their bindings, UF8 slots, works-for-me
// confirmations and reports. Then plug-ins with no maps left, then vendors with
// no plug-ins left — otherwise the index keeps a list of empty plug-ins that
// look like a corpus but are not.
//
// WHAT STAYS: accounts, credentials, sessions, device tokens, and the dynamic
// vendor aliases (they are a curation decision, not data derived from uploads).

import { rmSync, existsSync } from 'node:fs';
import { join } from 'node:path';
import { migrate, getDb, MAPS_DIR } from '../db/index.js';

if (!process.argv.includes('--yes')) {
  console.error([
    'This deletes every mapping. It cannot be undone.',
    '',
    'Accounts, device tokens and sign-in methods are kept.',
    '',
    'Re-run with --yes if that is what you want.',
  ].join('\n'));
  process.exit(2);
}

migrate();
const db = getDb();

const before = {
  maps: db.prepare('SELECT COUNT(*) c FROM maps').get().c,
  plugins: db.prepare('SELECT COUNT(*) c FROM plugins').get().c,
  vendors: db.prepare('SELECT COUNT(*) c FROM vendors').get().c,
};

// Collect the blob paths BEFORE the rows go, or they become unfindable orphans
// taking up space nobody can account for.
const files = db.prepare('SELECT file_path FROM maps').all().map((r) => r.file_path);

// Cascades are declared in the schema, but only if they are switched on — the
// pragma is set per connection, and a tool that forgets it silently leaves
// every binding behind.
db.pragma('foreign_keys = ON');

const wipe = db.transaction(() => {
  db.prepare('DELETE FROM maps').run();
  db.prepare('DELETE FROM plugins WHERE id NOT IN (SELECT plugin_id FROM maps)').run();
  db.prepare('DELETE FROM vendors WHERE id NOT IN (SELECT vendor_id FROM plugins WHERE vendor_id IS NOT NULL)').run();
});
wipe();

// file_path is relative to MAPS_DIR, not DATA_DIR. The schema comment claimed
// DATA_DIR and this loop believed it: every path missed, and the run reported
// "0 file(s)" — which reads as "there were none" rather than "I could not find
// them". Count the misses separately so a wrong base can never look like a
// clean sweep again.
let removedFiles = 0;
let missing = 0;
for (const rel of files) {
  const abs = join(MAPS_DIR, rel);
  if (existsSync(abs)) {
    rmSync(abs, { force: true });
    removedFiles += 1;
  } else {
    missing += 1;
  }
}
if (missing) {
  console.warn(`WARNING: ${missing} of ${files.length} blob file(s) were not where the database said`);
}

const after = {
  maps: db.prepare('SELECT COUNT(*) c FROM maps').get().c,
  bindings: db.prepare('SELECT COUNT(*) c FROM map_bindings').get().c,
  uf8: db.prepare('SELECT COUNT(*) c FROM uf8_slots').get().c,
  plugins: db.prepare('SELECT COUNT(*) c FROM plugins').get().c,
  vendors: db.prepare('SELECT COUNT(*) c FROM vendors').get().c,
  accounts: db.prepare('SELECT COUNT(*) c FROM accounts').get().c,
  tokens: db.prepare('SELECT COUNT(*) c FROM account_tokens WHERE revoked_at IS NULL').get().c,
};

console.log(
  `removed ${before.maps} mapping(s), ${before.plugins - after.plugins} plug-in(s), `
  + `${before.vendors - after.vendors} vendor(s), ${removedFiles} file(s)`,
);
console.log(
  `remaining — maps ${after.maps}, bindings ${after.bindings}, uf8 slots ${after.uf8}, `
  + `plug-ins ${after.plugins}, vendors ${after.vendors}`,
);
console.log(`kept — accounts ${after.accounts}, active device tokens ${after.tokens}`);
