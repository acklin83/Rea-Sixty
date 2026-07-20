// The two things a signed-in visitor can do to somebody else's map: confirm it
// works, and report it.
//
// EITHER CREDENTIAL WORKS HERE. A browser arrives with a session cookie, the
// extension with a device token, and both are the same account underneath —
// so these routes accept whichever is present rather than forcing a REAPER user
// to open a browser to say "this one works". Both preHandlers normalise onto
// req.account.accountId (see the note in lib/session.js about why that key
// exists on both shapes).
//
// "Works for me" and a confirmed plug-in version, NOT five stars: with a
// handful of votes per map a star average is noise, while "this worked on
// Pro-C 2 version 2.1.4" is the thing that actually helps the next person.

import { getDb, now } from '../db/index.js';
import { accountForSession, SESSION_COOKIE } from '../lib/session.js';
import { accountForToken } from '../lib/auth.js';

/** Session cookie or Bearer token, whichever is offered. */
function requireAnyCredential(req, reply, done) {
  const session = accountForSession(req.cookies?.[SESSION_COOKIE]);
  if (session) {
    if (session.suspended) {
      reply.code(403).send({ error: 'suspended', detail: 'this account cannot post' });
      return;
    }
    req.account = session;
    done();
    return;
  }

  const m = /^Bearer\s+(.+)$/i.exec(req.headers.authorization ?? '');
  const token = m ? accountForToken(m[1].trim()) : null;
  if (!token) {
    reply.code(401).send({ error: 'unauthorized', detail: 'sign in, or send a device token' });
    return;
  }
  req.account = token;
  done();
}

function publishedMap(id) {
  return getDb().prepare('SELECT id, account_id FROM maps WHERE id = ? AND published = 1').get(id);
}

export async function registerCommunityRoutes(app) {
  app.post('/maps/:id/works', {
    preHandler: requireAnyCredential,
    config: { rateLimit: { max: 60, timeWindow: '1 hour' } },
  }, async (req, reply) => {
    const map = publishedMap(Number(req.params.id));
    if (!map) return reply.code(404).send({ error: 'not_found' });

    const version = String(req.body?.pluginVersion ?? '').trim().slice(0, 60) || null;
    // Idempotent: pressing it twice updates the version rather than erroring,
    // which is what a user re-confirming after a plug-in update actually means.
    getDb().prepare(
      `INSERT INTO works_for_me (map_id, account_id, plugin_version, created_at)
       VALUES (?,?,?,?)
       ON CONFLICT(map_id, account_id)
       DO UPDATE SET plugin_version = excluded.plugin_version`,
    ).run(map.id, req.account.accountId, version, now());

    const count = getDb().prepare('SELECT COUNT(*) c FROM works_for_me WHERE map_id = ?').get(map.id).c;
    return { ok: true, worksCount: count };
  });

  app.delete('/maps/:id/works', { preHandler: requireAnyCredential }, async (req) => {
    getDb().prepare('DELETE FROM works_for_me WHERE map_id = ? AND account_id = ?')
      .run(Number(req.params.id), req.account.accountId);
    const count = getDb().prepare('SELECT COUNT(*) c FROM works_for_me WHERE map_id = ?')
      .get(Number(req.params.id)).c;
    return { ok: true, worksCount: count };
  });

  app.post('/maps/:id/report', {
    preHandler: requireAnyCredential,
    // Reporting is cheap for the reporter and expensive for the moderator, so
    // this is the tightest limit on the site.
    config: { rateLimit: { max: 10, timeWindow: '1 hour' } },
  }, async (req, reply) => {
    const map = publishedMap(Number(req.params.id));
    if (!map) return reply.code(404).send({ error: 'not_found' });

    const reason = String(req.body?.reason ?? '').trim().slice(0, 1000);
    if (reason.length < 10) {
      return reply.code(400).send({ error: 'bad_reason', detail: 'say what is wrong with it (10 characters or more)' });
    }
    if (map.account_id === req.account.accountId) {
      // Withdrawing is the right tool for your own map; a self-report would
      // just put work in the moderation queue for no reason.
      return reply.code(400).send({ error: 'own_map', detail: 'withdraw it from your account page instead' });
    }

    const db = getDb();
    const dupe = db.prepare(
      'SELECT 1 FROM reports WHERE map_id = ? AND account_id = ? AND resolved_at IS NULL',
    ).get(map.id, req.account.accountId);
    if (dupe) return { ok: true, detail: 'already reported — a moderator will look at it' };

    db.prepare(
      'INSERT INTO reports (map_id, account_id, reason, created_at) VALUES (?,?,?,?)',
    ).run(map.id, req.account.accountId, reason, now());
    return { ok: true, detail: 'reported — a moderator will look at it' };
  });
}
