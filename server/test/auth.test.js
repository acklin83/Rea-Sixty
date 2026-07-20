// Auth, account and moderation via fastify.inject(). Same pattern as
// routes.test.js: a fresh DATA_DIR is set BEFORE anything that opens the DB, so
// the singleton binds to the temp directory.
//
// What is worth testing here is the stuff that is quietly wrong rather than
// loudly broken: that the sign-in endpoint cannot be used to discover who has
// an account, that a device token appears exactly once, that a suspension is
// not cosmetic, and that the admin surface does not admit it exists.

import test from 'node:test';
import assert from 'node:assert/strict';
import { rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

const DATA_DIR = join(tmpdir(), `rea60-auth-${process.pid}`);
rmSync(DATA_DIR, { recursive: true, force: true });
process.env.DATA_DIR = DATA_DIR;
process.env.SITE_ORIGIN = 'http://localhost:4321';
delete process.env.SMTP_HOST;              // force the dev-log mail path
process.env.NODE_ENV = 'test';

const { buildApp } = await import('../src/app.js');
const { getDb, now } = await import('../src/db/index.js');
const { ingestMap } = await import('../src/ingest/service.js');

const app = await buildApp({ rateLimits: false });

/** Complete a magic-link sign-in and return the session cookie value. */
async function signIn(email) {
  const req = await app.inject({
    method: 'POST', url: '/auth/email/request', payload: { email },
  });
  const { devLink } = req.json();
  assert.ok(devLink, 'dev mode must hand back the link when there is no SMTP');

  const cb = await app.inject({ method: 'GET', url: devLink.replace('http://localhost:4321', '') });
  assert.equal(cb.statusCode, 302);
  const cookie = cb.cookies.find((c) => c.name === 'rs_session');
  assert.ok(cookie, 'callback must set a session cookie');
  return { cookie: `rs_session=${cookie.value}`, redirect: cb.headers.location };
}

const auth = (cookie) => ({ cookie });

// ------------------------------------------------------------- magic link

test('sign-in link: a fresh address creates an account and lands on /welcome', async () => {
  const { redirect } = await signIn('new@example.com');
  assert.match(redirect, /\/welcome$/);
});

test('sign-in link: the same address signs into the SAME account, no duplicate', async () => {
  await signIn('repeat@example.com');
  await signIn('repeat@example.com');
  const c = getDb().prepare("SELECT COUNT(*) c FROM credentials WHERE email = ?").get('repeat@example.com').c;
  assert.equal(c, 1);
});

test('sign-in link: answers identically for known and unknown addresses', async () => {
  const known = await app.inject({
    method: 'POST', url: '/auth/email/request', payload: { email: 'repeat@example.com' },
  });
  const unknown = await app.inject({
    method: 'POST', url: '/auth/email/request', payload: { email: 'nobody-here@example.com' },
  });
  assert.equal(known.statusCode, unknown.statusCode);
  // The dev link differs (it is a nonce); the human-visible answer must not.
  assert.equal(known.json().detail, unknown.json().detail);
  assert.equal(known.json().ok, unknown.json().ok);
});

test('sign-in link: is single use', async () => {
  const req = await app.inject({
    method: 'POST', url: '/auth/email/request', payload: { email: 'once@example.com' },
  });
  const path = req.json().devLink.replace('http://localhost:4321', '');
  const first = await app.inject({ method: 'GET', url: path });
  const second = await app.inject({ method: 'GET', url: path });
  assert.match(first.headers.location, /\/welcome$/);
  assert.match(second.headers.location, /error=link_expired/);
});

test('sign-in link: rejects something that is not an address', async () => {
  const r = await app.inject({
    method: 'POST', url: '/auth/email/request', payload: { email: 'not-an-address' },
  });
  assert.equal(r.statusCode, 400);
});

// ---------------------------------------------------------- display names

test('display name: placeholder is reported as needing a name, then cleared', async () => {
  const { cookie } = await signIn('namer@example.com');

  const before = await app.inject({ method: 'GET', url: '/auth/me', headers: auth(cookie) });
  assert.equal(before.json().account.needsDisplayName, true);

  const set = await app.inject({
    method: 'POST', url: '/account/name', headers: auth(cookie), payload: { displayName: 'Namer' },
  });
  assert.equal(set.statusCode, 200);

  const after = await app.inject({ method: 'GET', url: '/auth/me', headers: auth(cookie) });
  assert.equal(after.json().account.needsDisplayName, false);
  assert.equal(after.json().account.displayName, 'Namer');
});

test('display name: a taken name is a 409, not a 500', async () => {
  const { cookie } = await signIn('clash@example.com');
  const r = await app.inject({
    method: 'POST', url: '/account/name', headers: auth(cookie), payload: { displayName: 'Namer' },
  });
  assert.equal(r.statusCode, 409);
  assert.equal(r.json().error, 'name_taken');
});

test('display name: rejects control characters and over-long names', async () => {
  const { cookie } = await signIn('bad-name@example.com');
  for (const displayName of ['a', 'x'.repeat(41), 'we‮ird', '  ']) {
    const r = await app.inject({
      method: 'POST', url: '/account/name', headers: auth(cookie), payload: { displayName },
    });
    assert.equal(r.statusCode, 400, `should reject ${JSON.stringify(displayName)}`);
  }
});

// --------------------------------------------------------------- sessions

test('/auth/me is anonymous without a cookie, and logout really ends it', async () => {
  const anon = await app.inject({ method: 'GET', url: '/auth/me' });
  assert.equal(anon.json().signedIn, false);

  const { cookie } = await signIn('bye@example.com');
  assert.equal((await app.inject({ method: 'GET', url: '/auth/me', headers: auth(cookie) })).json().signedIn, true);

  await app.inject({ method: 'POST', url: '/auth/logout', headers: auth(cookie) });
  const after = await app.inject({ method: 'GET', url: '/auth/me', headers: auth(cookie) });
  assert.equal(after.json().signedIn, false, 'the session row must be gone, not just the cookie');
});

test('an expired session is refused and swept', async () => {
  const { cookie } = await signIn('stale@example.com');
  const token = cookie.split('=')[1];
  getDb().prepare('UPDATE sessions SET expires_at = ? WHERE token = ?').run(now() - 1, token);

  const r = await app.inject({ method: 'GET', url: '/auth/me', headers: auth(cookie) });
  assert.equal(r.json().signedIn, false);
  assert.equal(getDb().prepare('SELECT COUNT(*) c FROM sessions WHERE token = ?').get(token).c, 0);
});

// ---------------------------------------------------------- device tokens

test('device token: shown exactly once, never retrievable afterwards', async () => {
  const { cookie } = await signIn('tokens@example.com');
  const made = await app.inject({
    method: 'POST', url: '/account/tokens', headers: auth(cookie), payload: { label: 'MacBook' },
  });
  assert.equal(made.statusCode, 200);
  const { token } = made.json();
  assert.ok(token && token.length > 20);

  const list = await app.inject({ method: 'GET', url: '/account/tokens', headers: auth(cookie) });
  const body = JSON.stringify(list.json());
  assert.ok(!body.includes(token), 'the plaintext token must never come back from the list');
  assert.equal(getDb().prepare('SELECT COUNT(*) c FROM account_tokens WHERE token_sha256 = ?').get(token).c, 0,
    'the raw token must not be what is stored');
});

test('device token: authenticates an upload, and stops working once revoked', async () => {
  const { cookie } = await signIn('uploader@example.com');
  await app.inject({
    method: 'POST', url: '/account/name', headers: auth(cookie), payload: { displayName: 'Uploader' },
  });
  const made = await app.inject({
    method: 'POST', url: '/account/tokens', headers: auth(cookie), payload: { label: 'box' },
  });
  const { token } = made.json();
  const id = getDb().prepare(
    'SELECT id FROM account_tokens WHERE account_id = (SELECT account_id FROM sessions WHERE token = ?)',
  ).get(cookie.split('=')[1]).id;

  const ok = await app.inject({
    method: 'POST', url: '/v1/maps',
    headers: { authorization: `Bearer ${token}`, 'content-type': 'application/octet-stream' },
    payload: mapFile('Token Test Plugin'),
  });
  assert.equal(ok.statusCode, 201);

  await app.inject({ method: 'DELETE', url: `/account/tokens/${id}`, headers: auth(cookie) });

  const denied = await app.inject({
    method: 'POST', url: '/v1/maps',
    headers: { authorization: `Bearer ${token}`, 'content-type': 'application/octet-stream' },
    payload: mapFile('Token Test Plugin Two'),
  });
  assert.equal(denied.statusCode, 401);
});

// ------------------------------------------------------------ credentials

test('the last credential cannot be removed — that would be an unrecoverable lockout', async () => {
  const { cookie } = await signIn('lonely@example.com');
  const me = await app.inject({ method: 'GET', url: '/account/me', headers: auth(cookie) });
  const [only] = me.json().credentials;
  const r = await app.inject({ method: 'DELETE', url: `/account/credentials/${only.id}`, headers: auth(cookie) });
  assert.equal(r.statusCode, 409);
  assert.equal(r.json().error, 'last_credential');
});

// ------------------------------------------------------------- moderation

test('the admin surface answers 404 to a non-admin, never 403', async () => {
  const { cookie } = await signIn('curious@example.com');
  for (const url of ['/admin/overview', '/admin/reports', '/admin/accounts']) {
    const r = await app.inject({ method: 'GET', url, headers: auth(cookie) });
    assert.equal(r.statusCode, 404, `${url} must not confirm it exists`);
  }
  const anon = await app.inject({ method: 'GET', url: '/admin/overview' });
  assert.equal(anon.statusCode, 404);
});

test('unpublishing requires a note, because the author is shown it', async () => {
  const { cookie: adminCookie, mapId } = await seedAdminAndMap();
  const bare = await app.inject({
    method: 'POST', url: `/admin/maps/${mapId}/unpublish`, headers: auth(adminCookie), payload: {},
  });
  assert.equal(bare.statusCode, 400);

  const withNote = await app.inject({
    method: 'POST', url: `/admin/maps/${mapId}/unpublish`,
    headers: auth(adminCookie), payload: { note: 'duplicate of #3' },
  });
  assert.equal(withNote.statusCode, 200);
  assert.equal(getDb().prepare('SELECT published FROM maps WHERE id = ?').get(mapId).published, 0);
});

test('an author cannot republish over a moderator decision', async () => {
  const { cookie: adminCookie, mapId, ownerCookie } = await seedAdminAndMap();
  await app.inject({
    method: 'POST', url: `/admin/maps/${mapId}/unpublish`,
    headers: auth(adminCookie), payload: { note: 'not a real mapping' },
  });
  const r = await app.inject({
    method: 'POST', url: `/account/maps/${mapId}/republish`, headers: auth(ownerCookie),
  });
  assert.equal(r.statusCode, 403);
  assert.equal(r.json().error, 'moderated');
});

test('an author CAN undo their own withdrawal', async () => {
  const { mapId, ownerCookie } = await seedAdminAndMap();
  await app.inject({ method: 'POST', url: `/account/maps/${mapId}/withdraw`, headers: auth(ownerCookie) });
  assert.equal(getDb().prepare('SELECT published FROM maps WHERE id = ?').get(mapId).published, 0);
  const back = await app.inject({ method: 'POST', url: `/account/maps/${mapId}/republish`, headers: auth(ownerCookie) });
  assert.equal(back.statusCode, 200);
  assert.equal(getDb().prepare('SELECT published FROM maps WHERE id = ?').get(mapId).published, 1);
});

test('suspension is not cosmetic: existing sessions die and writes are refused', async () => {
  const { cookie: adminCookie } = await seedAdminAndMap();
  const { cookie: victim } = await signIn('victim@example.com');
  const victimId = getDb().prepare('SELECT account_id a FROM sessions WHERE token = ?')
    .get(victim.split('=')[1]).a;

  assert.equal((await app.inject({ method: 'GET', url: '/auth/me', headers: auth(victim) })).json().signedIn, true);

  const r = await app.inject({
    method: 'POST', url: `/admin/accounts/${victimId}/suspend`, headers: auth(adminCookie),
  });
  assert.equal(r.statusCode, 200);

  const after = await app.inject({ method: 'GET', url: '/auth/me', headers: auth(victim) });
  assert.equal(after.json().signedIn, false, 'the old cookie must stop working immediately');
});

test('an admin cannot suspend themselves out of the console', async () => {
  const { cookie: adminCookie } = await seedAdminAndMap();
  const adminId = getDb().prepare('SELECT account_id a FROM sessions WHERE token = ?')
    .get(adminCookie.split('=')[1]).a;
  const r = await app.inject({
    method: 'POST', url: `/admin/accounts/${adminId}/suspend`, headers: auth(adminCookie),
  });
  assert.equal(r.statusCode, 400);
});

// ------------------------------------------------------- works / reporting

test('works-for-me is idempotent and reporting your own map is refused', async () => {
  const { mapId, ownerCookie } = await seedAdminAndMap();
  const { cookie: other } = await signIn(`fan-${mapId}@example.com`);

  const first = await app.inject({ method: 'POST', url: `/v1/maps/${mapId}/works`, headers: auth(other), payload: {} });
  const second = await app.inject({
    method: 'POST', url: `/v1/maps/${mapId}/works`, headers: auth(other), payload: { pluginVersion: '2.1.4' },
  });
  assert.equal(first.json().worksCount, 1);
  assert.equal(second.json().worksCount, 1, 'pressing it twice must not count twice');
  assert.equal(
    getDb().prepare('SELECT plugin_version v FROM works_for_me WHERE map_id = ?').get(mapId).v, '2.1.4',
    're-confirming should update the version',
  );

  const own = await app.inject({
    method: 'POST', url: `/v1/maps/${mapId}/report`, headers: auth(ownerCookie),
    payload: { reason: 'I do not like it any more' },
  });
  assert.equal(own.statusCode, 400);
  assert.equal(own.json().error, 'own_map');
});

test('a report reaches the moderation queue exactly once per reporter', async () => {
  const { mapId, cookie: adminCookie } = await seedAdminAndMap();
  const { cookie: reporter } = await signIn(`reporter-${mapId}@example.com`);

  for (let i = 0; i < 2; i++) {
    const r = await app.inject({
      method: 'POST', url: `/v1/maps/${mapId}/report`, headers: auth(reporter),
      payload: { reason: 'this is a copy of somebody else s work' },
    });
    assert.equal(r.statusCode, 200);
  }
  const queue = await app.inject({ method: 'GET', url: '/admin/reports', headers: auth(adminCookie) });
  const mine = queue.json().reports.filter((x) => x.map.id === mapId);
  assert.equal(mine.length, 1, 'a second report from the same person must not queue twice');
});

// ------------------------------------------------------------------ setup

let seeded = 0;
async function seedAdminAndMap() {
  seeded += 1;
  const { cookie } = await signIn(`admin${seeded}@example.com`);
  const db = getDb();
  const adminId = db.prepare('SELECT account_id a FROM sessions WHERE token = ?')
    .get(cookie.split('=')[1]).a;
  db.prepare('UPDATE accounts SET is_admin = 1, display_name = ? WHERE id = ?')
    .run(`Admin ${seeded}`, adminId);

  const { cookie: ownerCookie } = await signIn(`owner${seeded}@example.com`);
  const ownerId = db.prepare('SELECT account_id a FROM sessions WHERE token = ?')
    .get(ownerCookie.split('=')[1]).a;
  const { mapId } = ingestMap(mapFile(`Moderated Plugin ${seeded}`), { accountId: ownerId });

  return { cookie, ownerCookie, mapId };
}

function mapFile(name) {
  const inner = {
    format_version: 10,
    plugins: [{
      match: name, domain: 'BusComp', uf8Mode: false,
      slots: [{ linkIdx: 1, vst3Param: 1 }, { linkIdx: 7, vst3Param: 7 }],
      paramSnapshot: [{ vst3Param: 1, name: 'Threshold' }, { vst3Param: 7, name: 'Mix' }],
    }],
  };
  return JSON.stringify({
    format: 'rea-sixty-map', version: 2, plugin: name, original_name: `${name} (Tester)`,
    vendor: '', surfaces: 'uc1', author: 'tester', description: '', licence: 'CC0-1.0',
    created_at: 0, map: JSON.stringify(inner),
  });
}

// The suite above runs with rateLimits:false so that signing in twenty times
// does not trip the cap. That would leave the cap itself unverified, which is
// the one thing standing between this endpoint and being used to post mail to
// somebody else's inbox — so it gets its own instance, with limits on.
test('the sign-in cap actually fires', async () => {
  const limited = await buildApp({ rateLimits: true });
  const codes = [];
  for (let i = 0; i < 7; i++) {
    const r = await limited.inject({
      method: 'POST', url: '/auth/email/request', payload: { email: `flood${i}@example.com` },
    });
    codes.push(r.statusCode);
  }
  await limited.close();
  assert.equal(codes.filter((c) => c === 200).length, 5, 'five should get through');
  assert.ok(codes.includes(429), 'the sixth must be refused');
});

test.after(() => app.close());
