// Duplicate guard — no two identical mappings of the same plug-in.
// "Identical" = same bindings, regardless of author / description / timestamp.

import test from 'node:test';
import assert from 'node:assert/strict';
import { rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

const DATA_DIR = join(tmpdir(), `rea60-dedup-${process.pid}`);
rmSync(DATA_DIR, { recursive: true, force: true });
process.env.DATA_DIR = DATA_DIR;

const { migrate, getDb, closeDb } = await import('../src/db/index.js');
const { ingestMap } = await import('../src/ingest/service.js');
const { IngestError } = await import('../src/lib/rea60map.js');

function envelope({ match = 'Pro-C 2', author = 'a', desc = '', p7 = 7 } = {}) {
  const inner = {
    format_version: 10,
    plugins: [{
      match, domain: 'BusComp', uf8Mode: false,
      slots: [{ linkIdx: 0, vst3Param: 0 }, { linkIdx: 1, vst3Param: 1 }, { linkIdx: 7, vst3Param: p7 }],
      paramSnapshot: [{ vst3Param: 0, name: 'Bypass' }, { vst3Param: 1, name: 'Threshold' }, { vst3Param: p7, name: 'Mix' }],
    }],
  };
  return JSON.stringify({
    format: 'rea-sixty-map', version: 2, plugin: match,
    original_name: `${match} (FabFilter)`, vendor: '', surfaces: 'uc1',
    author, description: desc, licence: 'CC0-1.0', created_at: 0, map: JSON.stringify(inner),
  });
}

function fresh() {
  closeDb();
  rmSync(DATA_DIR, { recursive: true, force: true });
  migrate();
  const db = getDb();
  const acct = Number(db.prepare('INSERT INTO accounts (display_name, created_at) VALUES (?,?)')
    .run('a', 0).lastInsertRowid);
  return { db, acct };
}

test('a second identical upload is rejected', () => {
  const { acct } = fresh();
  ingestMap(envelope(), { accountId: acct });
  assert.throws(
    () => ingestMap(envelope(), { accountId: acct }),
    (e) => e instanceof IngestError && e.code === 'duplicate_mapping',
  );
});

test('identical bindings but different author/description still rejected', () => {
  const { acct } = fresh();
  ingestMap(envelope({ author: 'frank', desc: 'mine' }), { accountId: acct });
  assert.throws(
    () => ingestMap(envelope({ author: 'someone', desc: 'totally different text' }), { accountId: acct }),
    (e) => e.code === 'duplicate_mapping',
  );
});

test('different bindings are allowed (one param changed)', () => {
  const { acct, db } = fresh();
  ingestMap(envelope({ p7: 7 }), { accountId: acct });
  ingestMap(envelope({ p7: 8 }), { accountId: acct });   // MIX -> a different param
  assert.equal(db.prepare('SELECT COUNT(*) c FROM maps').get().c, 2);
});

test('same bindings on a DIFFERENT plug-in are allowed', () => {
  const { acct, db } = fresh();
  ingestMap(envelope({ match: 'Pro-C 2' }), { accountId: acct });
  ingestMap(envelope({ match: 'Some Other Comp' }), { accountId: acct });
  assert.equal(db.prepare('SELECT COUNT(*) c FROM maps').get().c, 2);
});

test('replacing your own map with an identical one is allowed', () => {
  const { acct, db } = fresh();
  const first = ingestMap(envelope(), { accountId: acct });
  // Re-upload identical, marked as replacing the first — a no-op refresh, not a dupe.
  ingestMap(envelope(), { accountId: acct, replacesId: first.mapId });
  const live = db.prepare('SELECT COUNT(*) c FROM maps WHERE published = 1').get().c;
  assert.equal(live, 1, 'the old one is unpublished, the new one published');
});
