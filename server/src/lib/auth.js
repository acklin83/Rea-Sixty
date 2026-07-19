// Device-token auth — the extension's credential for uploads.
//
// Browse and download are anonymous. Only writes (upload, rate, report) need a
// token, which the user pastes once into the extension. WebAuthn is a browser
// API and cannot live in an ImGui window, so the in-app credential is a bearer
// token issued after a real (passkey / magic-link) login on the website — or,
// for the admin, by the mktoken CLI.
//
// The token is shown to the user exactly once, at creation. We persist only its
// SHA-256, so the DB never holds anything replayable.

import { createHash, randomBytes, timingSafeEqual } from 'node:crypto';
import { getDb, now } from '../db/index.js';

const PREFIX = 'rsx_';

/** SHA-256 hex of a token — the only form that touches the DB. */
export function hashToken(token) {
  return createHash('sha256').update(token, 'utf8').digest('hex');
}

/**
 * Mint a token for an account. Returns the PLAINTEXT token (shown once) plus
 * its row id. The caller must surface the plaintext to the user immediately;
 * it is unrecoverable afterwards.
 */
export function issueToken(accountId, label = null) {
  const db = getDb();
  const token = PREFIX + randomBytes(24).toString('base64url');  // ~32 chars of entropy
  const info = db.prepare(
    'INSERT INTO account_tokens (account_id, token_sha256, label, created_at) VALUES (?,?,?,?)',
  ).run(accountId, hashToken(token), label, now());
  return { token, id: Number(info.lastInsertRowid) };
}

/**
 * Resolve a bearer token to an account, or null. Constant-time compare on the
 * hash (the UNIQUE index already makes this a direct lookup, but the compare
 * guards against a timing oracle on the hash itself). Touches last_used_at.
 */
export function accountForToken(token) {
  if (typeof token !== 'string' || !token.startsWith(PREFIX)) return null;
  const db = getDb();
  const sha = hashToken(token);
  const row = db.prepare(
    `SELECT t.id, t.account_id, t.token_sha256, a.suspended_at
       FROM account_tokens t JOIN accounts a ON a.id = t.account_id
      WHERE t.token_sha256 = ? AND t.revoked_at IS NULL`,
  ).get(sha);
  if (!row) return null;
  // Defensive constant-time recheck.
  const a = Buffer.from(row.token_sha256, 'hex');
  const b = Buffer.from(sha, 'hex');
  if (a.length !== b.length || !timingSafeEqual(a, b)) return null;
  if (row.suspended_at) return null;

  db.prepare('UPDATE account_tokens SET last_used_at = ? WHERE id = ?').run(now(), row.id);
  return { accountId: row.account_id, tokenId: row.id };
}

/**
 * Fastify preHandler that requires a valid bearer token and attaches
 * req.account = { accountId, tokenId }. Rejects with 401 otherwise.
 */
export function requireToken(req, reply, done) {
  const header = req.headers.authorization ?? '';
  const m = /^Bearer\s+(.+)$/i.exec(header);
  const account = m ? accountForToken(m[1].trim()) : null;
  if (!account) {
    reply.code(401).send({ error: 'unauthorized', detail: 'valid Bearer token required' });
    return;
  }
  req.account = account;
  done();
}
