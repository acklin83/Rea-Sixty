// UF1 plugin-mode map (catalog v11) through the exchange.
//
// Three things had to be true before the extension could publish these, and all
// three failed silently before: the ingest dropped the UF1 half, the duplicate
// guard hashed without it (so two maps differing ONLY on the UF1 collided), and
// the domain gate rejected a UF1-only map outright.

import test from 'node:test';
import assert from 'node:assert/strict';
import { rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

const DATA_DIR = join(tmpdir(), `rea60-uf1-${process.pid}`);
rmSync(DATA_DIR, { recursive: true, force: true });
process.env.DATA_DIR = DATA_DIR;

const { migrate, getDb, closeDb } = await import('../src/db/index.js');
const { ingestMap } = await import('../src/ingest/service.js');
const { IngestError, extractUf1 } = await import('../src/lib/rea60map.js');
const { getMap } = await import('../src/query/detail.js');

// A UC1 (BusComp) map plus an optional UF1 layer. `uf1Param` lets two otherwise
// identical maps differ ONLY on the UF1 — the dedup case that matters.
function envelope({
  match = 'Pro-C 2', surfaces = 'uc1+uf1', domain = 'BusComp',
  uf8Mode = false, uf1Mode = true, uf1Param = 7, withUf1 = true, author = 'a',
} = {}) {
  const plugin = {
    match, domain, uf8Mode, uf1Mode,
    slots: [{ linkIdx: 0, vst3Param: 0 }, { linkIdx: 1, vst3Param: 1 }],
    paramSnapshot: [
      { vst3Param: 0, name: 'Bypass' }, { vst3Param: 1, name: 'Threshold' },
      { vst3Param: 7, name: 'Mix' }, { vst3Param: 8, name: 'Range' },
    ],
  };
  if (withUf1) {
    plugin.uf1 = {
      vpots: [
        { pos: 0, vst3Param: uf1Param, inverted: false },
        { pos: 5, vst3Param: 8, customLabel: 'Rng', inverted: true },  // sparse + page 1
      ],
      softKeys: [{ pos: 2, vst3Param: 0, inverted: false }],
    };
  }
  const inner = { format_version: 11, plugins: [plugin] };
  return JSON.stringify({
    format: 'rea-sixty-map', version: 2, plugin: match,
    original_name: `${match} (FabFilter)`, vendor: '', surfaces,
    author, description: '', licence: 'CC0-1.0', created_at: 0,
    map: JSON.stringify(inner),
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

test('extractUf1 reads both streams, keeps sparse positions and derives page/idx', () => {
  const map = JSON.parse(JSON.parse(envelope()).map).plugins[0];
  const out = extractUf1(map);
  assert.equal(out.length, 3);
  const v0 = out.find((u) => u.kind === 'vpot' && u.pos === 0);
  const v5 = out.find((u) => u.kind === 'vpot' && u.pos === 5);
  const s2 = out.find((u) => u.kind === 'softkey' && u.pos === 2);
  assert.equal(v0.vst3Param, 7);
  assert.equal(v0.paramName, 'Mix');           // resolved via paramSnapshot
  assert.equal(v5.page, 1);                    // pos 5 → page 1, idx 1
  assert.equal(v5.idx, 1);
  assert.equal(v5.label, 'Rng');
  assert.equal(v5.inverted, true);
  assert.equal(s2.vst3Param, 0);
});

test('a UC1+UF1 map keeps its UF1 half through ingest', () => {
  const { acct } = fresh();
  const { mapId } = ingestMap(envelope(), { accountId: acct });
  const detail = getMap(mapId);
  assert.equal(detail.uf1.length, 3);
  // Not gated on domain: this map is BusComp and still carries its UF1 half.
  assert.equal(detail.domain, 'BusComp');
  assert.ok(detail.uf1.some((u) => u.kind === 'vpot' && u.param === 'Mix'));
  assert.ok(detail.uf1.some((u) => u.kind === 'softkey'));
});

test('two maps identical on the UC1 but different on the UF1 are NOT duplicates', () => {
  const { acct } = fresh();
  ingestMap(envelope({ uf1Param: 7 }), { accountId: acct });
  // Same UC1 slots, different UF1 param. Before the fingerprint carried the UF1
  // this threw duplicate_mapping — i.e. the exact map this feature exists for
  // could not be published.
  const second = ingestMap(envelope({ uf1Param: 8, author: 'b' }), { accountId: acct });
  assert.ok(second.mapId > 0);
});

test('a genuinely identical UF1 map is still a duplicate', () => {
  const { acct } = fresh();
  ingestMap(envelope(), { accountId: acct });
  assert.throws(
    () => ingestMap(envelope({ author: 'b' }), { accountId: acct }),
    (e) => e instanceof IngestError && e.code === 'duplicate_mapping',
  );
});

test('a UF1-only map ingests (domain None, no UF8 layer)', () => {
  const { acct } = fresh();
  const { mapId } = ingestMap(
    envelope({ domain: 'None', uf8Mode: false, uf1Mode: true, surfaces: 'uf1' }),
    { accountId: acct },
  );
  const detail = getMap(mapId);
  assert.equal(detail.domain, 'None');
  assert.equal(detail.uf1.length, 3);
});

test('domain None with neither layer is still rejected', () => {
  const { acct } = fresh();
  assert.throws(
    () => ingestMap(
      envelope({ domain: 'None', uf8Mode: false, uf1Mode: false, withUf1: false, surfaces: 'uf1' }),
      { accountId: acct },
    ),
    (e) => e instanceof IngestError && e.code === 'bad_domain',
  );
});

test('UF1 params count toward parameter coverage', () => {
  const { acct } = fresh();
  // UC1 binds params 0 and 1; the UF1 adds 7 and 8 → 4 distinct params.
  const env = JSON.parse(envelope());
  const inner = JSON.parse(env.map);
  env.functional_params = 10;
  env.map = JSON.stringify(inner);
  const { mapId } = ingestMap(JSON.stringify(env), { accountId: acct });
  const detail = getMap(mapId);
  assert.equal(detail.paramCoverage.n, 4);
  assert.equal(detail.paramCoverage.d, 10);
});
