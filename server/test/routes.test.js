// Route tests via fastify.inject() — no socket, real handler path. A fresh
// DATA_DIR is set BEFORE importing anything that opens the DB, so the singleton
// binds to the temp dir.

import test from 'node:test';
import assert from 'node:assert/strict';
import { rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

const DATA_DIR = join(tmpdir(), `rea60-routes-${process.pid}`);
rmSync(DATA_DIR, { recursive: true, force: true });
process.env.DATA_DIR = DATA_DIR;

const { buildApp } = await import('../src/app.js');
const { getDb } = await import('../src/db/index.js');
const { ingestMap } = await import('../src/ingest/service.js');

function v2({ match, originalName, domain = 'BusComp', slots }) {
  const inner = {
    format_version: 10,
    plugins: [{
      match, domain, uf8Mode: false,
      slots: slots ?? [
        { linkIdx: 0, vst3Param: 0 }, { linkIdx: 1, vst3Param: 1 }, { linkIdx: 7, vst3Param: 7 },
      ],
      paramSnapshot: [
        { vst3Param: 0, name: 'Bypass' }, { vst3Param: 1, name: 'Threshold' },
        { vst3Param: 7, name: 'Wet' }, { vst3Param: 8, name: 'Dry Gain' },
      ],
    }],
  };
  return JSON.stringify({
    format: 'rea-sixty-map', version: 2, plugin: match, original_name: originalName,
    vendor: '', surfaces: 'uc1', author: 'tester', description: '', licence: 'CC0-1.0',
    created_at: 0, map: JSON.stringify(inner),
  });
}

// Seed: one plug-in ("Pro-C 2") with two maps that differ on linkIdx 7.
const app = buildApp();
const db = getDb();
const acct = Number(db.prepare('INSERT INTO accounts (display_name, created_at) VALUES (?,?)')
  .run('tester', 0).lastInsertRowid);
const a = ingestMap(v2({ match: 'FabFilter Pro-C 2', originalName: 'FabFilter Pro-C 2 (FabFilter)' }), { accountId: acct });
const b = ingestMap(v2({
  match: 'Pro-C 2', originalName: 'FabFilter Pro-C 2 (FabFilter)',
  slots: [{ linkIdx: 0, vst3Param: 0 }, { linkIdx: 1, vst3Param: 1 }, { linkIdx: 7, vst3Param: 8 }],
}), { accountId: acct });

test.after(() => { app.close(); });

test('GET /health', async () => {
  const r = await app.inject({ url: '/health' });
  assert.equal(r.statusCode, 200);
  assert.equal(r.json().ok, true);
});

test('GET /v1/plugins lists one plug-in, two maps', async () => {
  const r = await app.inject({ url: '/v1/plugins' });
  assert.equal(r.statusCode, 200);
  const body = r.json();
  assert.equal(body.total, 1);
  assert.equal(body.rows[0].name, 'FabFilter Pro-C 2');
  assert.equal(body.rows[0].mapCount, 2);
  assert.equal(body.rows[0].vendor, 'FabFilter');
});

test('GET /v1/plugins?search filters', async () => {
  assert.equal((await app.inject({ url: '/v1/plugins?search=Pro-C' })).json().total, 1);
  assert.equal((await app.inject({ url: '/v1/plugins?search=nothing' })).json().total, 0);
});

test('GET /v1/plugins?surface=uf8 excludes a uc1-only plug-in', async () => {
  assert.equal((await app.inject({ url: '/v1/plugins?surface=uf8' })).json().total, 0);
  assert.equal((await app.inject({ url: '/v1/plugins?surface=uc1' })).json().total, 1);
});

test('GET /v1/plugins?vendor=999 -> 404 not empty page', async () => {
  assert.equal((await app.inject({ url: '/v1/plugins?vendor=999' })).statusCode, 404);
});

test('GET /v1/vendors returns the combobox data', async () => {
  const r = await app.inject({ url: '/v1/vendors' });
  const v = r.json().vendors;
  assert.ok(v.find((x) => x.name === 'FabFilter' && x.pluginCount === 1));
});

test('GET /v1/plugins/:slug returns maps with coverage sections', async () => {
  const list = (await app.inject({ url: '/v1/plugins' })).json();
  const slug = list.rows[0].slug;
  const page = (await app.inject({ url: `/v1/plugins/${slug}` })).json();
  assert.equal(page.maps.length, 2);
  assert.ok(page.maps[0].sections.length > 0);
  assert.equal(page.maps[0].coverage.d, 8);   // BusComp denominator
});

test('GET /v1/plugins/:slug/diff reproduces the per-control difference', async () => {
  const slug = (await app.inject({ url: '/v1/plugins' })).json().rows[0].slug;
  const r = await app.inject({ url: `/v1/plugins/${slug}/diff?maps=${a.mapId},${b.mapId}` });
  assert.equal(r.statusCode, 200);
  const d = r.json();
  const mix = d.rows.find((row) => row.name === 'Mix');
  assert.ok(mix.differs, 'the MIX row must differ (Wet vs Dry Gain)');
  assert.deepEqual(mix.values.map(String).sort(), ['Dry Gain', 'Wet']);
});

test('diff rejects a single map and foreign ids', async () => {
  const slug = (await app.inject({ url: '/v1/plugins' })).json().rows[0].slug;
  assert.equal((await app.inject({ url: `/v1/plugins/${slug}/diff?maps=${a.mapId}` })).statusCode, 400);
  assert.equal((await app.inject({ url: `/v1/plugins/${slug}/diff?maps=${a.mapId},99999` })).statusCode, 400);
});

test('GET /v1/maps/:id detail + download', async () => {
  const detail = await app.inject({ url: `/v1/maps/${a.mapId}` });
  assert.equal(detail.statusCode, 200);
  assert.equal(detail.json().plugin.name, 'FabFilter Pro-C 2');

  const dl = await app.inject({ url: `/v1/maps/${a.mapId}/download` });
  assert.equal(dl.statusCode, 200);
  assert.match(dl.headers['content-disposition'], /\.rea60map/);
  // The downloaded bytes must be a valid, re-importable envelope.
  const env = JSON.parse(dl.body);
  assert.equal(env.format, 'rea-sixty-map');
  assert.equal(env.version, 2);
});

test('unknown ids 404', async () => {
  assert.equal((await app.inject({ url: '/v1/plugins/nope' })).statusCode, 404);
  assert.equal((await app.inject({ url: '/v1/maps/99999' })).statusCode, 404);
});
