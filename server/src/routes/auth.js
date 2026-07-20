// Sign-in: passkey primary, magic link an EQUAL fallback.
//
// Not a hierarchy — Linux generally has no OS passkey store and Linux is a
// shipped platform, so a passkey-only door would gate a supported platform
// behind a second device. Either credential alone is a full account.
//
// ACCOUNT ENUMERATION. /auth/email/request answers the same way whether or not
// the address is known. The reply never says "no such account", and a new
// address creates the account only once the link is actually opened — so the
// endpoint cannot be used to test who has signed up, and cannot be used to
// create accounts for addresses nobody controls.
//
// DISPLAY NAMES. accounts.display_name is NOT NULL UNIQUE and is the public
// author name, but we only learn it after the address is verified. A first
// sign-in therefore mints a placeholder and the site sends the user to
// /welcome to choose one. There is deliberately no "needs_name" column: the
// migrate() step is a plain schema.sql exec with no ALTER support, so adding a
// column would silently not apply to databases that already exist.

import { randomBytes } from 'node:crypto';
import {
  generateRegistrationOptions,
  verifyRegistrationResponse,
  generateAuthenticationOptions,
  verifyAuthenticationResponse,
} from '@simplewebauthn/server';

import { getDb, now } from '../db/index.js';
import {
  SESSION_COOKIE, cookieOptions, createSession, destroySession,
  accountForSession, requireSession, siteOrigin,
} from '../lib/session.js';
import { rpID, rpName, expectedOrigins } from '../lib/webauthn.js';
import { sendMagicLink } from '../lib/mail.js';
import { issueToken } from '../lib/auth.js';

const MAGIC_TTL = 15 * 60;          // 15 minutes
const WEBAUTHN_TTL = 5 * 60;        // the browser prompt is on screen; short
// Ten minutes is long enough to walk to another machine and short enough that
// an abandoned code is not sitting there to be guessed.
const DEVICE_TTL = 10 * 60;

// No 0/O, no 1/I/L: the code is read off one screen and typed on another, and
// those are the pairs people get wrong. 8 characters from a 31-symbol alphabet
// is ~39 bits, which against a ten-minute window and a rate limit is plenty.
const USER_CODE_ALPHABET = '23456789ABCDEFGHJKMNPQRSTUVWXYZ';

function newUserCode() {
  const b = randomBytes(8);
  let out = '';
  for (let i = 0; i < 8; i++) {
    if (i === 4) out += '-';
    out += USER_CODE_ALPHABET[b[i] % USER_CODE_ALPHABET.length];
  }
  return out;
}

/** A post-sign-in destination, or ''. SAME-SITE PATHS ONLY: this value ends up
 *  in a redirect, and an unchecked one is an open redirect — in a link we mail
 *  to people, which is the worst place to have one. A single leading slash and
 *  no second one; "//evil.example" is a protocol-relative URL, not a path. */
function safeNext(raw) {
  const v = String(raw ?? '');
  return /^\/(?!\/)/.test(v) && v.length <= 300 ? v : '';
}

function sweepDeviceGrants() {
  getDb().prepare('DELETE FROM device_grants WHERE expires_at <= ?').run(now());
}

const PLACEHOLDER_RE = /^new-user-[0-9a-f]{8}$/;

/** True when the account has not chosen a public name yet. Recognised by shape
 *  rather than a flag — see the note at the top about migrate(). */
export function needsDisplayName(displayName) {
  return PLACEHOLDER_RE.test(displayName);
}

function normaliseEmail(raw) {
  return String(raw ?? '').trim().toLowerCase();
}

// Deliberately permissive. The address is proved by the link arriving, so a
// strict pattern only rejects valid-but-unusual addresses; this catches typos
// and obvious junk, nothing more.
function looksLikeEmail(email) {
  return /^[^\s@]+@[^\s@.]+\.[^\s@]+$/.test(email) && email.length <= 254;
}

function newChallengeToken() {
  return randomBytes(32).toString('base64url');
}

function insertChallenge({ kind, email = null, accountId = null, challenge = null, ttl }) {
  const token = newChallengeToken();
  const t = now();
  getDb().prepare(
    `INSERT INTO auth_challenges (token, kind, email, account_id, challenge, created_at, expires_at)
     VALUES (?,?,?,?,?,?,?)`,
  ).run(token, kind, email, accountId, challenge, t, t + ttl);
  return token;
}

/** Fetch and burn a challenge in one step. Single-use is the whole point of a
 *  nonce, so consumption is not left to the caller to remember. */
function consumeChallenge(token, kind) {
  const db = getDb();
  const row = db.prepare(
    'SELECT * FROM auth_challenges WHERE token = ? AND kind = ?',
  ).get(token, kind);
  if (!row) return null;
  db.prepare('DELETE FROM auth_challenges WHERE token = ?').run(token);
  if (row.consumed_at || row.expires_at <= now()) return null;
  return row;
}

function sweepExpiredChallenges() {
  getDb().prepare('DELETE FROM auth_challenges WHERE expires_at <= ?').run(now());
}

function accountByEmail(email) {
  return getDb().prepare(
    `SELECT a.* FROM accounts a
       JOIN credentials c ON c.account_id = a.id
      WHERE c.kind = 'email' AND c.email = ?`,
  ).get(email);
}

/** Find-or-create on a VERIFIED address. Only ever called after a link has
 *  been opened, never from the request endpoint. */
function accountForVerifiedEmail(email) {
  const db = getDb();
  const existing = accountByEmail(email);
  if (existing) return { account: existing, created: false };

  const t = now();
  const placeholder = `new-user-${randomBytes(4).toString('hex')}`;
  const accountId = db.prepare(
    'INSERT INTO accounts (display_name, created_at, is_admin) VALUES (?,?,0)',
  ).run(placeholder, t).lastInsertRowid;
  // email_verified is a 0/1 flag, not a timestamp — the row is only ever
  // created after a link was opened, so it is verified by construction.
  db.prepare(
    `INSERT INTO credentials (account_id, kind, email, email_verified, created_at)
     VALUES (?, 'email', ?, 1, ?)`,
  ).run(accountId, email, t);

  return { account: db.prepare('SELECT * FROM accounts WHERE id = ?').get(accountId), created: true };
}

function signIn(reply, accountId) {
  const { token, maxAge } = createSession(accountId);
  reply.setCookie(SESSION_COOKIE, token, { ...cookieOptions(), maxAge });
}

export async function registerAuthRoutes(app) {
  // ------------------------------------------------------------ magic link

  app.post('/email/request', {
    // Tight: this endpoint sends mail to an address the caller chose. The cap
    // is what stops it being used to post someone else's inbox.
    config: { rateLimit: { max: 5, timeWindow: '10 minutes' } },
  }, async (req, reply) => {
    const email = normaliseEmail(req.body?.email);
    if (!looksLikeEmail(email)) {
      return reply.code(400).send({ error: 'bad_email', detail: 'that does not look like an email address' });
    }

    sweepExpiredChallenges();
    const token = insertChallenge({ kind: 'magic_link', email, ttl: MAGIC_TTL });
    // `next` rides in the URL rather than the challenge row: auth_challenges
    // has no column for it, and migrate() cannot ALTER an existing table. The
    // link goes to the address being proved, and the value is re-validated on
    // the way back, so carrying it here costs nothing.
    const next = safeNext(req.body?.next);
    const url = `${siteOrigin()}/auth/email/callback?token=${encodeURIComponent(token)}`
      + (next ? `&next=${encodeURIComponent(next)}` : '');

    const result = await sendMagicLink({ to: email, url, log: req.log });

    // Same answer either way — see the enumeration note at the top.
    const body = { ok: true, detail: 'if that address can receive mail, a sign-in link is on its way' };
    // Only outside production, and only when there is no mail transport at all,
    // hand the link back so a local session can complete without a mail server.
    if (!result.sent && process.env.NODE_ENV !== 'production') body.devLink = url;
    return body;
  });

  app.get('/email/callback', async (req, reply) => {
    const row = consumeChallenge(String(req.query?.token ?? ''), 'magic_link');
    if (!row) {
      return reply.redirect(`${siteOrigin()}/login?error=link_expired`);
    }
    const { account, created } = accountForVerifiedEmail(row.email);
    if (account.suspended_at) {
      return reply.redirect(`${siteOrigin()}/login?error=suspended`);
    }
    signIn(reply, account.id);

    // Naming yourself comes first — a placeholder byline must not reach a
    // publish action. Otherwise go where the visitor was heading.
    if (created || needsDisplayName(account.display_name)) {
      return reply.redirect(`${siteOrigin()}/welcome`);
    }
    return reply.redirect(`${siteOrigin()}${safeNext(req.query?.next) || '/account'}`);
  });

  // --------------------------------------------------------------- passkey

  // Registration happens INSIDE a session: you prove who you are with the
  // credential you already have (a magic link, or another passkey), then add
  // this device. That is what makes a passkey an addition rather than a second
  // unauthenticated way to claim an account.
  app.post('/passkey/register/options', { preHandler: requireSession }, async (req) => {
    const db = getDb();
    const existing = db.prepare(
      `SELECT webauthn_id, transports FROM credentials
        WHERE account_id = ? AND kind = 'passkey'`,
    ).all(req.account.id);

    const options = await generateRegistrationOptions({
      rpName: rpName(),
      rpID: rpID(),
      userName: req.account.displayName,
      userID: new TextEncoder().encode(String(req.account.id)),
      attestationType: 'none',        // we are not doing device attestation
      // Stops the same authenticator silently enrolling twice, which would
      // leave the user with two indistinguishable entries in their list.
      excludeCredentials: existing.map((c) => ({
        id: c.webauthn_id,
        transports: c.transports ? JSON.parse(c.transports) : undefined,
      })),
      authenticatorSelection: {
        residentKey: 'preferred',
        userVerification: 'preferred',
      },
    });

    const token = insertChallenge({
      kind: 'webauthn_reg', accountId: req.account.id,
      challenge: options.challenge, ttl: WEBAUTHN_TTL,
    });
    return { token, options };
  });

  app.post('/passkey/register/verify', { preHandler: requireSession }, async (req, reply) => {
    const row = consumeChallenge(String(req.body?.token ?? ''), 'webauthn_reg');
    if (!row || row.account_id !== req.account.id) {
      return reply.code(400).send({ error: 'bad_challenge', detail: 'that registration expired — try again' });
    }

    let verification;
    try {
      verification = await verifyRegistrationResponse({
        response: req.body?.response,
        expectedChallenge: row.challenge,
        expectedOrigin: expectedOrigins(),
        expectedRPID: rpID(),
      });
    } catch (err) {
      return reply.code(400).send({ error: 'verify_failed', detail: err.message });
    }
    if (!verification.verified) {
      return reply.code(400).send({ error: 'verify_failed', detail: 'the authenticator response did not verify' });
    }

    const { credential, credentialDeviceType, credentialBackedUp } = verification.registrationInfo;
    const label = String(req.body?.label ?? '').trim().slice(0, 60)
      || (credentialBackedUp ? 'Synced passkey' : `${credentialDeviceType} passkey`);

    getDb().prepare(
      `INSERT INTO credentials
         (account_id, kind, webauthn_id, public_key, sign_count, transports, label, created_at)
       VALUES (?, 'passkey', ?, ?, ?, ?, ?, ?)`,
    ).run(
      req.account.id,
      credential.id,
      Buffer.from(credential.publicKey),
      credential.counter,
      JSON.stringify(credential.transports ?? []),
      label,
      now(),
    );

    return { ok: true, label };
  });

  // Sign-IN with a passkey needs no session and no identifier: the credential
  // is discoverable, so the authenticator tells us which account it is. That is
  // why allowCredentials is left empty rather than filled from a typed email —
  // asking for the email first would reintroduce the enumeration problem the
  // magic-link endpoint carefully avoids.
  app.post('/passkey/login/options', {
    config: { rateLimit: { max: 20, timeWindow: '10 minutes' } },
  }, async () => {
    const options = await generateAuthenticationOptions({
      rpID: rpID(),
      userVerification: 'preferred',
    });
    const token = insertChallenge({
      kind: 'webauthn_auth', challenge: options.challenge, ttl: WEBAUTHN_TTL,
    });
    return { token, options };
  });

  app.post('/passkey/login/verify', {
    config: { rateLimit: { max: 20, timeWindow: '10 minutes' } },
  }, async (req, reply) => {
    const row = consumeChallenge(String(req.body?.token ?? ''), 'webauthn_auth');
    if (!row) {
      return reply.code(400).send({ error: 'bad_challenge', detail: 'that sign-in expired — try again' });
    }

    const db = getDb();
    const cred = db.prepare(
      `SELECT c.*, a.suspended_at FROM credentials c
         JOIN accounts a ON a.id = c.account_id
        WHERE c.kind = 'passkey' AND c.webauthn_id = ?`,
    ).get(String(req.body?.response?.id ?? ''));
    if (!cred) {
      return reply.code(400).send({ error: 'unknown_passkey', detail: 'that passkey is not registered here' });
    }

    let verification;
    try {
      verification = await verifyAuthenticationResponse({
        response: req.body.response,
        expectedChallenge: row.challenge,
        expectedOrigin: expectedOrigins(),
        expectedRPID: rpID(),
        credential: {
          id: cred.webauthn_id,
          publicKey: new Uint8Array(cred.public_key),
          counter: cred.sign_count,
          transports: cred.transports ? JSON.parse(cred.transports) : undefined,
        },
      });
    } catch (err) {
      return reply.code(400).send({ error: 'verify_failed', detail: err.message });
    }
    if (!verification.verified) {
      return reply.code(400).send({ error: 'verify_failed', detail: 'the authenticator response did not verify' });
    }

    // The signature counter is the replay tell: a cloned authenticator reuses
    // numbers. Storing it is only useful if it is actually stored.
    db.prepare('UPDATE credentials SET sign_count = ? WHERE id = ?')
      .run(verification.authenticationInfo.newCounter, cred.id);

    if (cred.suspended_at) {
      return reply.code(403).send({ error: 'suspended', detail: 'this account is suspended' });
    }

    signIn(reply, cred.account_id);
    const account = db.prepare('SELECT display_name FROM accounts WHERE id = ?').get(cred.account_id);
    return { ok: true, next: needsDisplayName(account.display_name) ? '/welcome' : '/account' };
  });

  // ----------------------------------------------------- device authorisation
  //
  // The extension cannot run a browser, so it cannot sign in — the same problem
  // a TV has. RFC 8628's answer: the device shows a short code, the human
  // approves it somewhere they ARE signed in, and the device collects the
  // credential itself. Nobody copies a secret, and nothing is ever mailed.

  app.post('/device/start', {
    config: { rateLimit: { max: 10, timeWindow: '10 minutes' } },
  }, async (req) => {
    sweepDeviceGrants();

    const label = String(req.body?.label ?? '').trim().slice(0, 60) || 'REAPER';
    const deviceCode = randomBytes(32).toString('base64url');
    const userCode = newUserCode();
    const t = now();

    getDb().prepare(
      `INSERT INTO device_grants (device_code, user_code, label, state, created_at, expires_at)
       VALUES (?,?,?, 'pending', ?, ?)`,
    ).run(deviceCode, userCode, label, t, t + DEVICE_TTL);

    const origin = siteOrigin();
    return {
      deviceCode,
      userCode,
      verificationUri: `${origin}/link`,
      // Prefilled, so the usual case is one click and one confirm. The page
      // still asks for an explicit confirmation — see the warning there.
      verificationUriComplete: `${origin}/link?code=${encodeURIComponent(userCode)}`,
      expiresIn: DEVICE_TTL,
      interval: 2,
    };
  });

  app.post('/device/poll', {
    // Generous: the extension polls every 2s for up to 10 minutes.
    config: { rateLimit: { max: 400, timeWindow: '10 minutes' } },
  }, async (req, reply) => {
    const code = String(req.body?.deviceCode ?? '');
    const db = getDb();
    const row = db.prepare('SELECT * FROM device_grants WHERE device_code = ?').get(code);

    if (!row) return reply.code(404).send({ status: 'unknown' });
    if (row.collected_at) return reply.code(410).send({ status: 'used' });
    if (row.expires_at <= now()) {
      db.prepare('DELETE FROM device_grants WHERE device_code = ?').run(code);
      return reply.code(410).send({ status: 'expired' });
    }
    if (row.state === 'denied') {
      db.prepare('DELETE FROM device_grants WHERE device_code = ?').run(code);
      return { status: 'denied' };
    }
    if (row.state !== 'approved') return { status: 'pending' };

    // THE TOKEN IS MINTED HERE, not at approval time. Nothing plaintext ever
    // rests in a row waiting to be picked up — which is the point of storing
    // only the hash in the first place.
    const { token } = issueToken(row.account_id, row.label);
    db.prepare('UPDATE device_grants SET collected_at = ? WHERE device_code = ?').run(now(), code);

    const account = db.prepare('SELECT display_name FROM accounts WHERE id = ?').get(row.account_id);
    return { status: 'ok', token, label: row.label, displayName: account?.display_name ?? '' };
  });

  // ----------------------------------------------------------------- misc

  app.post('/logout', async (req, reply) => {
    destroySession(req.cookies?.[SESSION_COOKIE]);
    reply.clearCookie(SESSION_COOKIE, cookieOptions());
    return { ok: true };
  });

  app.get('/me', async (req) => {
    const account = accountForSession(req.cookies?.[SESSION_COOKIE]);
    if (!account) return { signedIn: false };
    return {
      signedIn: true,
      account: {
        id: account.id,
        displayName: account.displayName,
        isAdmin: account.isAdmin,
        suspended: account.suspended,
        needsDisplayName: needsDisplayName(account.displayName),
      },
    };
  });
}
