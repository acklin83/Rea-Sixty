// Upload route: device-token auth + the same ingest path as file import.

import test from 'node:test';
import assert from 'node:assert/strict';
import { rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

const DATA_DIR = join(tmpdir(), `rea60-upload-${process.pid}`);
rmSync(DATA_DIR, { recursive: true, force: true });
process.env.DATA_DIR = DATA_DIR;

const { buildApp } = await import('../src/app.js');
const { getDb } = await import('../src/db/index.js');
const { issueToken } = await import('../src/lib/auth.js');

function envelope({ match = 'Pro-C 2', originalName = 'FabFilter Pro-C 2 (FabFilter)', bundle = false } = {}) {
  const inner = {
    format_version: 10,
    plugins: [{
      match, domain: 'BusComp', uf8Mode: false,
      slots: [{ linkIdx: 0, vst3Param: 0 }, { linkIdx: 1, vst3Param: 1 }],
      paramSnapshot: [{ vst3Param: 0, name: 'Bypass' }, { vst3Param: 1, name: 'Threshold' }],
    }],
  };
  return JSON.stringify({
    format: bundle ? 'rea-sixty-setup' : 'rea-sixty-map',
    version: 2, plugin: match, original_name: originalName, vendor: '',
    surfaces: 'uc1', author: 'Frank', description: '', licence: 'CC0-1.0',
    created_at: 0, map: JSON.stringify(inner),
  });
}

const app = await buildApp();
const db = getDb();
const acctId = Number(db.prepare('INSERT INTO accounts (display_name, created_at) VALUES (?,?)')
  .run('Frank', 0).lastInsertRowid);
const { token } = issueToken(acctId, 'test');

test.after(() => app.close());

const post = (body, tok = token) => app.inject({
  method: 'POST', url: '/v1/maps', payload: body,
  headers: { 'content-type': 'application/octet-stream', ...(tok ? { authorization: `Bearer ${tok}` } : {}) },
});

test('no token -> 401', async () => {
  assert.equal((await post(envelope(), null)).statusCode, 401);
});

test('garbage token -> 401', async () => {
  assert.equal((await post(envelope(), 'rsx_notarealtoken')).statusCode, 401);
});

test('valid upload -> 201, ingested and browsable', async () => {
  const r = await post(envelope());
  assert.equal(r.statusCode, 201);
  const body = r.json();
  assert.ok(body.mapId > 0);
  assert.equal(body.pluginSlug, 'fabfilter-pro-c-2');
  assert.equal(body.coverage.d, 8);

  // It is immediately live on the browse surface.
  const list = (await app.inject({ url: '/v1/plugins' })).json();
  assert.ok(list.rows.find((p) => p.slug === 'fabfilter-pro-c-2' && p.vendor === 'FabFilter'));
});

test('a .rea60config bundle is rejected with the security code', async () => {
  const r = await post(envelope({ bundle: true }));
  assert.equal(r.statusCode, 422);
  assert.equal(r.json().error, 'bundle_rejected');
});

test('empty body -> 400', async () => {
  assert.equal((await post('')).statusCode, 400);
});

test('replaces someone else\'s map -> 400', async () => {
  // Another account's map.
  const otherAcct = Number(db.prepare('INSERT INTO accounts (display_name, created_at) VALUES (?,?)')
    .run('someone', 0).lastInsertRowid);
  const { ingestMap } = await import('../src/ingest/service.js');
  const foreign = ingestMap(envelope({ match: 'Other' }), { accountId: otherAcct });
  const r = await app.inject({
    method: 'POST', url: `/v1/maps?replaces=${foreign.mapId}`, payload: envelope(),
    headers: { 'content-type': 'application/octet-stream', authorization: `Bearer ${token}` },
  });
  assert.equal(r.statusCode, 400);
  assert.equal(r.json().error, 'replaces_not_yours');
});
