// Browser sessions — the website's credential, distinct from the extension's.
//
// TWO CREDENTIALS, ON PURPOSE. The extension holds a long-lived device token
// (lib/auth.js): it is pasted once into an ImGui field and must survive
// restarts, because WebAuthn is a browser API that cannot run in that window.
// The website holds a short session cookie, because a browser can re-run a
// passkey any time. Conflating them would mean either a bearer token living in
// a browser or a cookie living in the extension — both worse than having two.
//
// The cookie is httpOnly (script cannot read it), SameSite=Lax (survives the
// magic-link click, which is a top-level GET from a mail client, but is not
// sent on cross-site POSTs) and Secure whenever we are not on plain localhost.

import { randomBytes } from 'node:crypto';
import { getDb, now } from '../db/index.js';

export const SESSION_COOKIE = 'rs_session';

// 30 days. Long enough that a hobbyist uploading a map every few weeks is not
// re-authenticating every visit, short enough that an abandoned session on a
// shared machine expires by itself.
const SESSION_TTL = 30 * 24 * 60 * 60;

/** Cookie options shared by set and clear, so the two cannot drift apart —
 *  a clear whose attributes differ from the set silently fails to delete. */
export function cookieOptions() {
  const secure = !/^http:\/\/(localhost|127\.0\.0\.1)(:|$)/.test(siteOrigin());
  return { httpOnly: true, sameSite: 'lax', path: '/', secure };
}

export function siteOrigin() {
  return process.env.SITE_ORIGIN ?? 'http://localhost:4321';
}

export function createSession(accountId) {
  const token = randomBytes(32).toString('base64url');
  const t = now();
  getDb()
    .prepare('INSERT INTO sessions (token, account_id, created_at, expires_at) VALUES (?,?,?,?)')
    .run(token, accountId, t, t + SESSION_TTL);
  return { token, maxAge: SESSION_TTL };
}

/** The account behind a session token, or null. Expired rows are deleted on
 *  sight rather than left to a sweeper — the check is already a write path's
 *  worth of work and this keeps the table from growing without a cron. */
export function accountForSession(token) {
  if (!token) return null;
  const db = getDb();
  const row = db
    .prepare(`SELECT s.expires_at, a.id, a.display_name, a.is_admin, a.suspended_at
                FROM sessions s JOIN accounts a ON a.id = s.account_id
               WHERE s.token = ?`)
    .get(token);
  if (!row) return null;
  if (row.expires_at <= now()) {
    db.prepare('DELETE FROM sessions WHERE token = ?').run(token);
    return null;
  }
  return {
    id: row.id,
    // Deliberate duplicate of `id`. Token auth (lib/auth.js) also attaches its
    // result to req.account, but shaped { accountId, tokenId } — two different
    // shapes under one property name is exactly the sort of thing that reads
    // fine and returns undefined at runtime. Carrying both keys means a route
    // that accepts EITHER credential can say req.account.accountId and be right
    // no matter which one signed the request.
    accountId: row.id,
    displayName: row.display_name,
    isAdmin: !!row.is_admin,
    suspended: !!row.suspended_at,
  };
}

export function destroySession(token) {
  if (token) getDb().prepare('DELETE FROM sessions WHERE token = ?').run(token);
}

/** Drop every session for an account — used when suspending someone, where
 *  leaving their existing cookies working would make the suspension cosmetic. */
export function destroyAccountSessions(accountId) {
  getDb().prepare('DELETE FROM sessions WHERE account_id = ?').run(accountId);
}

// ------------------------------------------------------------- preHandlers

/** Attaches req.account (or null). Never rejects — for routes that render
 *  differently when signed in but are public. */
export function loadSession(req, _reply, done) {
  req.account = accountForSession(req.cookies?.[SESSION_COOKIE]);
  done();
}

export function requireSession(req, reply, done) {
  const account = accountForSession(req.cookies?.[SESSION_COOKIE]);
  if (!account) {
    reply.code(401).send({ error: 'unauthorized', detail: 'sign in first' });
    return;
  }
  // A suspended account keeps its maps and can still read; it must not write.
  // Checked here rather than per route so a new write route cannot forget it.
  if (account.suspended) {
    reply.code(403).send({ error: 'suspended', detail: 'this account cannot post' });
    return;
  }
  req.account = account;
  done();
}

export function requireAdmin(req, reply, done) {
  const account = accountForSession(req.cookies?.[SESSION_COOKIE]);
  if (!account || !account.isAdmin) {
    // 404, not 403: an admin surface should not confirm its own existence to
    // someone who cannot use it.
    reply.code(404).send({ error: 'not_found' });
    return;
  }
  req.account = account;
  done();
}
