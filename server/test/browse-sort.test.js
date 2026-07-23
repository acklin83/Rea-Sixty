// Browse sort: by vendor, and direction (asc/desc), plus additive search.

import test from 'node:test';
import assert from 'node:assert/strict';
import { rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

const DATA_DIR = join(tmpdir(), `rea60-sort-${process.pid}`);
rmSync(DATA_DIR, { recursive: true, force: true });
process.env.DATA_DIR = DATA_DIR;

const { migrate, getDb } = await import('../src/db/index.js');
const { ingestMap } = await import('../src/ingest/service.js');
const { listPlugins } = await import('../src/query/browse.js');

function env({ match, originalName, vendor }) {
  const inner = {
    format_version: 10,
    plugins: [{
      match, domain: 'BusComp', uf8Mode: false,
      slots: [{ linkIdx: 1, vst3Param: 1 }],
      paramSnapshot: [{ vst3Param: 1, name: 'Threshold' }],
    }],
  };
  return JSON.stringify({
    format: 'rea-sixty-map', version: 3, plugin: match, original_name: originalName,
    functional_params: 10, vendor, surfaces: 'uc1', author: 'a', description: '',
    licence: 'CC0-1.0', created_at: 0, map: JSON.stringify(inner),
  });
}

migrate();
const db = getDb();
const acct = Number(db.prepare('INSERT INTO accounts (display_name, created_at) VALUES (?,?)')
  .run('a', 0).lastInsertRowid);
// Names and vendors deliberately out of order relative to each other.
ingestMap(env({ match: 'Zebra', originalName: 'Zebra (Acme)', vendor: 'Acme' }), { accountId: acct });
ingestMap(env({ match: 'Alpha', originalName: 'Alpha (Zzz Audio)', vendor: 'Zzz Audio' }), { accountId: acct });
ingestMap(env({ match: 'Mango', originalName: 'Mango (Mid Co)', vendor: 'Mid Co' }), { accountId: acct });

const names = (opts) => listPlugins(opts).rows.map((r) => r.name);

test('sort by name asc / desc', () => {
  assert.deepEqual(names({ sort: 'name', dir: 'asc' }), ['Alpha', 'Mango', 'Zebra']);
  assert.deepEqual(names({ sort: 'name', dir: 'desc' }), ['Zebra', 'Mango', 'Alpha']);
});

test('sort by vendor orders by developer, not name', () => {
  // Vendors: Acme < Mid Co < Zzz Audio -> Zebra, Mango, Alpha.
  assert.deepEqual(names({ sort: 'vendor', dir: 'asc' }), ['Zebra', 'Mango', 'Alpha']);
  assert.deepEqual(names({ sort: 'vendor', dir: 'desc' }), ['Alpha', 'Mango', 'Zebra']);
});

test('additive search matches across name and vendor', () => {
  assert.deepEqual(names({ search: 'zzz' }), ['Alpha']);        // vendor only
  assert.deepEqual(names({ search: 'zebra' }), ['Zebra']);      // name only
  assert.deepEqual(names({ search: 'acme zebra' }), ['Zebra']); // both tokens
  assert.deepEqual(names({ search: 'acme alpha' }), []);        // tokens on different rows
});
