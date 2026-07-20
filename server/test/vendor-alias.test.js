// Vendor aliases: known variants fold to one canonical name at ingest, and the
// admin mergeVendors() folds dupes found afterwards.

import test from 'node:test';
import assert from 'node:assert/strict';
import { rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

const DATA_DIR = join(tmpdir(), `rea60-valias-${process.pid}`);
rmSync(DATA_DIR, { recursive: true, force: true });
process.env.DATA_DIR = DATA_DIR;

const { canonicalVendor } = await import('../src/lib/vendor.js');
const { migrate, getDb, closeDb } = await import('../src/db/index.js');
const { ingestMap } = await import('../src/ingest/service.js');
const { mergeVendors } = await import('../src/ingest/merge.js');

function env({ match, vendor }) {
  const inner = {
    format_version: 10,
    plugins: [{
      match, domain: 'BusComp', uf8Mode: false,
      slots: [{ linkIdx: 1, vst3Param: 1 }],
      paramSnapshot: [{ vst3Param: 1, name: 'Threshold' }],
    }],
  };
  return JSON.stringify({
    format: 'rea-sixty-map', version: 2, plugin: match, original_name: `${match} (x)`,
    vendor, surfaces: 'uc1', author: 'a', description: '', licence: 'CC0-1.0',
    created_at: 0, map: JSON.stringify(inner),
  });
}

test('canonicalVendor folds known aliases', () => {
  assert.equal(canonicalVendor('UADx'), 'Universal Audio');
  assert.equal(canonicalVendor('Universal Audio (UADx)'), 'Universal Audio');
  assert.equal(canonicalVendor('Acqua'), 'Acustica Audio');
  assert.equal(canonicalVendor('Acustica'), 'Acustica Audio');
  assert.equal(canonicalVendor('Valhalla DSP, LLC'), 'Valhalla DSP');
});

test('canonicalVendor is case/space-insensitive and passes unknowns through', () => {
  assert.equal(canonicalVendor('  uadx '), 'Universal Audio');
  assert.equal(canonicalVendor('FabFilter'), 'FabFilter');
});

test('aliased vendors ingest to ONE vendor row', () => {
  closeDb();
  rmSync(DATA_DIR, { recursive: true, force: true });
  migrate();
  const db = getDb();
  const acct = Number(db.prepare('INSERT INTO accounts (display_name, created_at) VALUES (?,?)')
    .run('a', 0).lastInsertRowid);

  ingestMap(env({ match: 'Comp A', vendor: 'UADx' }), { accountId: acct });
  ingestMap(env({ match: 'Comp B', vendor: 'Universal Audio (UADx)' }), { accountId: acct });
  ingestMap(env({ match: 'Comp C', vendor: 'Acqua' }), { accountId: acct });
  ingestMap(env({ match: 'Comp D', vendor: 'Acustica' }), { accountId: acct });

  const vendors = db.prepare('SELECT name FROM vendors ORDER BY name').all().map((v) => v.name);
  assert.deepEqual(vendors, ['Acustica Audio', 'Universal Audio']);
  assert.equal(db.prepare("SELECT COUNT(*) c FROM plugins WHERE vendor_id = (SELECT id FROM vendors WHERE name='Universal Audio')").get().c, 2);
});

test('mergeVendors folds a dupe found afterwards + leaves a dynamic alias', () => {
  closeDb();
  rmSync(DATA_DIR, { recursive: true, force: true });
  migrate();
  const db = getDb();
  const acct = Number(db.prepare('INSERT INTO accounts (display_name, created_at) VALUES (?,?)')
    .run('a', 0).lastInsertRowid);

  // Two vendors the static table doesn't know about.
  ingestMap(env({ match: 'X', vendor: 'Kiloheart' }), { accountId: acct });
  ingestMap(env({ match: 'Y', vendor: 'kHs' }), { accountId: acct });
  const kilo = db.prepare("SELECT id FROM vendors WHERE name='Kiloheart'").get().id;
  const khs = db.prepare("SELECT id FROM vendors WHERE name='kHs'").get().id;

  mergeVendors(kilo, khs);
  assert.equal(db.prepare('SELECT COUNT(*) c FROM vendors').get().c, 1);
  assert.equal(db.prepare('SELECT COUNT(*) c FROM plugins WHERE vendor_id = ?').get(kilo).c, 2);

  // A later upload of the folded name resolves to the target via the dynamic alias.
  ingestMap(env({ match: 'Z', vendor: 'kHs' }), { accountId: acct });
  assert.equal(db.prepare('SELECT COUNT(*) c FROM vendors').get().c, 1);
});
