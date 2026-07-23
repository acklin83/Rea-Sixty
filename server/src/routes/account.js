// The signed-in user's own surface: name, credentials, device tokens, maps.
//
// The reason this exists at all is the device token. The extension cannot run
// WebAuthn, so uploading from REAPER needs a bearer token, and until now the
// only way to get one was the mktoken CLI on the server — which is fine for the
// admin and useless for everybody else. This page is what makes the exchange
// usable by someone who is not Frank.

import { getDb, now } from '../db/index.js';
import { requireSession, destroyAccountSessions } from '../lib/session.js';
import { issueToken } from '../lib/auth.js';
import { needsDisplayName } from './auth.js';

const NAME_MIN = 2;
const NAME_MAX = 40;

// The display name is the public author byline, so it is deliberately narrow:
// letters, digits, space and a few joiners. Blocks the homoglyph and RTL-mark
// games that let one author's name render as another's.
const NAME_RE = /^[\p{L}\p{N}][\p{L}\p{N} ._'-]*$/u;

export async function registerAccountRoutes(app) {
  app.addHook('preHandler', requireSession);

  app.get('/me', async (req) => {
    const db = getDb();
    const creds = db.prepare(
      `SELECT id, kind, label, email, created_at, last_used_at
         FROM credentials WHERE account_id = ? ORDER BY created_at`,
    ).all(req.account.id);

    return {
      account: {
        id: req.account.id,
        displayName: req.account.displayName,
        isAdmin: req.account.isAdmin,
        needsDisplayName: needsDisplayName(req.account.displayName),
      },
      credentials: creds.map((c) => ({
        id: c.id,
        kind: c.kind,
        label: c.label,
        // The address is the user's own and they are looking at their own page,
        // but it is never exposed anywhere else — see the schema comment.
        email: c.email,
        createdAt: c.created_at,
        lastUsedAt: c.last_used_at,
      })),
    };
  });

  app.post('/name', async (req, reply) => {
    const name = String(req.body?.displayName ?? '').trim().replace(/\s+/g, ' ');
    if (name.length < NAME_MIN || name.length > NAME_MAX || !NAME_RE.test(name)) {
      return reply.code(400).send({
        error: 'bad_name',
        detail: `${NAME_MIN}–${NAME_MAX} characters; letters, digits, spaces and . _ ' -`,
      });
    }
    if (needsDisplayName(name)) {
      return reply.code(400).send({ error: 'bad_name', detail: 'that name is reserved' });
    }
    try {
      getDb().prepare('UPDATE accounts SET display_name = ? WHERE id = ?').run(name, req.account.id);
    } catch (err) {
      // display_name is UNIQUE. Report the collision plainly rather than as a 500.
      if (String(err.message).includes('UNIQUE')) {
        return reply.code(409).send({ error: 'name_taken', detail: 'someone already uses that name' });
      }
      throw err;
    }
    return { ok: true, displayName: name };
  });

  // ---------------------------------------------------------- device tokens

  app.get('/tokens', async (req) => {
    const rows = getDb().prepare(
      `SELECT id, label, created_at, last_used_at, revoked_at
         FROM account_tokens WHERE account_id = ? ORDER BY created_at DESC`,
    ).all(req.account.id);
    return {
      tokens: rows.map((t) => ({
        id: t.id,
        label: t.label,
        createdAt: t.created_at,
        lastUsedAt: t.last_used_at,
        revokedAt: t.revoked_at,
      })),
    };
  });

  app.post('/tokens', {
    config: { rateLimit: { max: 10, timeWindow: '1 hour' } },
  }, async (req, reply) => {
    const active = getDb().prepare(
      'SELECT COUNT(*) c FROM account_tokens WHERE account_id = ? AND revoked_at IS NULL',
    ).get(req.account.id).c;
    // One per machine is the real use; a cap keeps a stolen session from
    // minting an unbounded set of credentials that outlive it.
    if (active >= 10) {
      return reply.code(409).send({ error: 'too_many_tokens', detail: 'revoke one first (limit 10)' });
    }

    const label = String(req.body?.label ?? '').trim().slice(0, 60) || 'device';
    // issueToken returns { token, id } — the plaintext AND the row id. Sending
    // the object straight out would nest it as token.token and the UI would
    // print "[object Object]" for the one value the user must copy.
    const { token, id } = issueToken(req.account.id, label);

    // THE ONLY TIME THIS VALUE EXISTS IN PLAINTEXT ANYWHERE. Only its SHA-256
    // is stored, so a lost token is re-issued, never recovered.
    return { ok: true, id, token, label, showOnce: true };
  });

  app.delete('/tokens/:id', async (req, reply) => {
    const id = Number(req.params.id);
    const info = getDb().prepare(
      `UPDATE account_tokens SET revoked_at = ?
        WHERE id = ? AND account_id = ? AND revoked_at IS NULL`,
    ).run(now(), id, req.account.id);
    if (!info.changes) return reply.code(404).send({ error: 'not_found' });
    return { ok: true };
  });

  // ------------------------------------------------------------ credentials

  app.delete('/credentials/:id', async (req, reply) => {
    const db = getDb();
    const id = Number(req.params.id);
    const mine = db.prepare(
      'SELECT id, kind FROM credentials WHERE id = ? AND account_id = ?',
    ).get(id, req.account.id);
    if (!mine) return reply.code(404).send({ error: 'not_found' });

    const total = db.prepare(
      'SELECT COUNT(*) c FROM credentials WHERE account_id = ?',
    ).get(req.account.id).c;
    // Removing the last credential would lock the account out for good — there
    // is no support desk here to undo it.
    if (total <= 1) {
      return reply.code(409).send({
        error: 'last_credential',
        detail: 'add another sign-in method before removing this one',
      });
    }

    db.prepare('DELETE FROM credentials WHERE id = ?').run(id);
    return { ok: true };
  });

  // ------------------------------------------------------------------ maps

  app.get('/maps', async (req) => {
    const rows = getDb().prepare(
      `SELECT m.id, m.surfaces, m.domain, m.description, m.uploaded_at, m.published,
              m.unpublish_note, m.coverage_n, m.coverage_d,
              m.param_cov_n, m.param_cov_d,
              p.name AS plugin_name, p.slug AS plugin_slug,
              (SELECT COUNT(*) FROM works_for_me w WHERE w.map_id = m.id) AS works_count,
              (SELECT COUNT(*) FROM reports r WHERE r.map_id = m.id AND r.resolved_at IS NULL)
                AS open_reports
         FROM maps m JOIN plugins p ON p.id = m.plugin_id
        WHERE m.account_id = ?
        ORDER BY m.uploaded_at DESC`,
    ).all(req.account.id);

    return {
      maps: rows.map((m) => ({
        id: m.id,
        pluginName: m.plugin_name,
        pluginSlug: m.plugin_slug,
        surfaces: m.surfaces,
        domain: m.domain,
        description: m.description,
        uploadedAt: m.uploaded_at,
        published: !!m.published,
        unpublishNote: m.unpublish_note,
        coverage: { n: m.coverage_n, d: m.coverage_d },
        paramCoverage: m.param_cov_d ? { n: m.param_cov_n, d: m.param_cov_d } : null,
        worksCount: m.works_count,
        openReports: m.open_reports,
      })),
    };
  });

  // Withdrawing your own map unpublishes it; it is never deleted, because
  // another map may record it as the one it replaces and the id has to keep
  // resolving.
  app.post('/maps/:id/withdraw', async (req, reply) => {
    const info = getDb().prepare(
      `UPDATE maps SET published = 0, unpublished_at = ?, unpublish_note = 'withdrawn by the author'
        WHERE id = ? AND account_id = ? AND published = 1`,
    ).run(now(), Number(req.params.id), req.account.id);
    if (!info.changes) return reply.code(404).send({ error: 'not_found' });
    return { ok: true };
  });

  app.post('/maps/:id/republish', async (req, reply) => {
    const db = getDb();
    const map = db.prepare('SELECT unpublish_note FROM maps WHERE id = ? AND account_id = ?')
      .get(Number(req.params.id), req.account.id);
    if (!map) return reply.code(404).send({ error: 'not_found' });
    // An author may undo their own withdrawal, but must not undo a moderator's.
    if (map.unpublish_note && map.unpublish_note !== 'withdrawn by the author') {
      return reply.code(403).send({ error: 'moderated', detail: 'this map was unpublished by a moderator' });
    }
    db.prepare('UPDATE maps SET published = 1, unpublished_at = NULL, unpublish_note = NULL WHERE id = ?')
      .run(Number(req.params.id));
    return { ok: true };
  });

  // -------------------------------------------------- device authorisation
  //
  // ⚠ THE PHISHING SHAPE OF THIS FLOW. An attacker can start a grant on their
  // own machine and send YOU the code; approving it hands them a token on your
  // account. That is inherent to every device flow, and the only real defence
  // is that the human is told plainly what they are approving. Hence the
  // lookup endpoint: the page shows what the code is for BEFORE the button,
  // and the button is never pre-clicked by a link.

  app.get('/device/:code', async (req, reply) => {
    const code = String(req.params.code ?? '').toUpperCase();
    const row = getDb().prepare(
      "SELECT user_code, label, state, created_at, expires_at FROM device_grants WHERE user_code = ?",
    ).get(code);

    if (!row || row.expires_at <= now()) {
      return reply.code(404).send({ error: 'unknown_code', detail: 'that code is unknown or has expired' });
    }
    if (row.state !== 'pending') {
      return reply.code(409).send({ error: 'already_decided', detail: 'that code has already been dealt with' });
    }
    return {
      userCode: row.user_code,
      label: row.label,
      requestedAt: row.created_at,
      expiresAt: row.expires_at,
    };
  });

  app.post('/device/:code/approve', {
    // A human clicking a button. The cap is here to blunt code guessing, which
    // is the one way this endpoint could be abused from a stolen session.
    config: { rateLimit: { max: 20, timeWindow: '10 minutes' } },
  }, async (req, reply) => {
    const code = String(req.params.code ?? '').toUpperCase();
    const deny = req.body?.deny === true;
    const db = getDb();

    const row = db.prepare('SELECT * FROM device_grants WHERE user_code = ?').get(code);
    if (!row || row.expires_at <= now()) {
      return reply.code(404).send({ error: 'unknown_code' });
    }
    if (row.state !== 'pending') {
      return reply.code(409).send({ error: 'already_decided' });
    }

    if (deny) {
      db.prepare("UPDATE device_grants SET state = 'denied' WHERE user_code = ?").run(code);
      return { ok: true, state: 'denied' };
    }

    // The label is the user's chance to say which machine this is; it becomes
    // the token's name on the account page, where it is the only way to tell
    // two tokens apart when revoking one.
    const label = String(req.body?.label ?? '').trim().slice(0, 60) || row.label || 'REAPER';
    db.prepare(
      "UPDATE device_grants SET state = 'approved', account_id = ?, label = ?, approved_at = ? WHERE user_code = ?",
    ).run(req.account.id, label, now(), code);

    // No token in the response. The extension collects it by polling with the
    // device code it has held all along — this browser never touches it.
    return { ok: true, state: 'approved', label };
  });

  app.post('/close-all-sessions', async (req, reply) => {
    destroyAccountSessions(req.account.id);
    reply.clearCookie('rs_session', { path: '/' });
    return { ok: true };
  });
}
