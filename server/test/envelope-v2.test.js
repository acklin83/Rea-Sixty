// Envelope v2 (original_name) is the fix for plug-in identity: two maps whose
// user-typed `match` differs ("FabFilter Pro-C 2" vs "Pro-C 2") but whose live
// `original_name` is identical must land on ONE plug-in, with the vendor
// resolved from that name — no manual merge, no upload-form confirmation.
//
// The envelopes here are built exactly as exportMapToFile now writes them
// (version 2, top-level original_name), verified against the C++ at
// UserPluginCatalog.cpp:1395.

import test from 'node:test';
import assert from 'node:assert/strict';
import { rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

// Isolate the DB per test run — DATA_DIR is read at first getDb().
const DATA_DIR = join(tmpdir(), `rea60-v2-test-${process.pid}`);
process.env.DATA_DIR = DATA_DIR;

const { migrate, getDb, closeDb } = await import('../src/db/index.js');
const { ingestMap } = await import('../src/ingest/service.js');
const { parseRea60Map } = await import('../src/lib/rea60map.js');

/** Fresh, empty DB for a test case: drop the cached handle, wipe DATA_DIR. */
function freshDb() {
  closeDb();
  rmSync(DATA_DIR, { recursive: true, force: true });
  migrate();
  return getDb();
}

function v2Envelope({ match, originalName, vendor = '', domain = 'BusComp', p7 = 7 }) {
  const inner = {
    format_version: 10,
    plugins: [{
      match,
      domain,
      uf8Mode: false,
      slots: [
        { linkIdx: 0, vst3Param: 0 },
        { linkIdx: 1, vst3Param: 1 },
        { linkIdx: 7, vst3Param: p7 },
      ],
      paramSnapshot: [
        { vst3Param: 0, name: 'Bypass' },
        { vst3Param: 1, name: 'Threshold' },
        { vst3Param: 7, name: 'Wet' },
      ],
    }],
  };
  return JSON.stringify({
    format: 'rea-sixty-map',
    version: 2,
    plugin: match,
    original_name: originalName,
    vendor,
    surfaces: 'uc1',
    author: 'test',
    description: '',
    licence: 'CC0-1.0',
    created_at: 0,
    map: JSON.stringify(inner),
  });
}

test('v2 envelope parses and surfaces original_name', () => {
  const parsed = parseRea60Map(v2Envelope({
    match: 'Pro-C 2', originalName: 'FabFilter Pro-C 2 (FabFilter)',
  }));
  assert.equal(parsed.envelope.originalName, 'FabFilter Pro-C 2 (FabFilter)');
});

test('v1 envelope (no original_name) still accepted', () => {
  const v1 = JSON.parse(v2Envelope({ match: 'Pro-C 2', originalName: 'x' }));
  v1.version = 1;
  delete v1.original_name;
  const parsed = parseRea60Map(JSON.stringify(v1));
  assert.equal(parsed.envelope.originalName, '');
  assert.equal(parsed.envelope.plugin, 'Pro-C 2');
});

test('a version above MAX_VERSION is rejected', () => {
  const bad = JSON.parse(v2Envelope({ match: 'x', originalName: 'y' }));
  bad.version = 3;
  assert.throws(() => parseRea60Map(JSON.stringify(bad)), /unsupported envelope version/);
});

test('same original_name, different match -> ONE plug-in, vendor resolved', () => {
  const db = freshDb();
  const acct = db.prepare('INSERT INTO accounts (display_name, created_at) VALUES (?,?)')
    .run('tester', 0).lastInsertRowid;

  // The real split: two different user-typed matches, one true factory name.
  // Distinct bindings (p7) so they merge by identity without tripping the
  // duplicate-mapping guard.
  ingestMap(v2Envelope({ match: 'FabFilter Pro-C 2', originalName: 'FabFilter Pro-C 2 (FabFilter)', p7: 7 }),
    { accountId: Number(acct) });
  ingestMap(v2Envelope({ match: 'Pro-C 2', originalName: 'FabFilter Pro-C 2 (FabFilter)', p7: 8 }),
    { accountId: Number(acct) });

  const plugins = db.prepare('SELECT id, name, vendor_id FROM plugins').all();
  assert.equal(plugins.length, 1, 'both maps should resolve to one plug-in');
  assert.equal(plugins[0].name, 'FabFilter Pro-C 2');

  const vendor = db.prepare('SELECT name FROM vendors WHERE id = ?').get(plugins[0].vendor_id);
  assert.equal(vendor.name, 'FabFilter');

  const mapCount = db.prepare('SELECT COUNT(*) c FROM maps WHERE plugin_id = ?')
    .get(plugins[0].id).c;
  assert.equal(mapCount, 2, 'both maps hang off the one plug-in');
});

test('v1 fallback: no original_name still splits (documents the limitation)', () => {
  const db = freshDb();
  const acct = db.prepare('INSERT INTO accounts (display_name, created_at) VALUES (?,?)')
    .run('tester', 0).lastInsertRowid;

  const v1 = (match) => {
    const e = JSON.parse(v2Envelope({ match, originalName: 'ignored' }));
    e.version = 1; delete e.original_name;
    return JSON.stringify(e);
  };
  ingestMap(v1('FabFilter Pro-C 2'), { accountId: Number(acct) });
  ingestMap(v1('Pro-C 2'), { accountId: Number(acct) });

  // Without original_name the two matches normalise differently and split —
  // this is exactly why v2 exists, and why v1 maps need suggestMerges().
  assert.equal(db.prepare('SELECT COUNT(*) c FROM plugins').get().c, 2);
});
