// Moderation. Small on purpose: a report queue, unpublish, suspend, and the
// two merge tools that already existed as CLI helpers.
//
// The whole surface answers 404 to anyone who is not an admin (requireAdmin),
// not 403 — a moderation console should not confirm its own existence to
// someone who cannot use it.
//
// NOTHING HERE DELETES. Unpublishing keeps the row: another map can record it
// as the one it replaces, and that id has to keep resolving. Suspending keeps
// the maps. The only destructive action in the whole system is a user removing
// their own credential, which is guarded separately.

import { getDb, now } from '../db/index.js';
import { requireAdmin, destroyAccountSessions } from '../lib/session.js';
import { mergePlugins, mergeVendors, suggestMerges } from '../ingest/merge.js';

export async function registerAdminRoutes(app) {
  app.addHook('preHandler', requireAdmin);

  // --------------------------------------------------------------- reports

  app.get('/reports', async (req) => {
    const state = req.query?.state === 'closed' ? 'closed' : 'open';
    const rows = getDb().prepare(
      `SELECT r.id, r.reason, r.created_at, r.resolved_at, r.resolution,
              r.map_id, m.published, m.description AS map_description,
              p.name AS plugin_name, p.slug AS plugin_slug,
              ra.display_name AS reporter, ma.display_name AS author
         FROM reports r
         JOIN maps m     ON m.id = r.map_id
         JOIN plugins p  ON p.id = m.plugin_id
         JOIN accounts ma ON ma.id = m.account_id
    LEFT JOIN accounts ra ON ra.id = r.account_id
        WHERE r.resolved_at IS ${state === 'open' ? 'NULL' : 'NOT NULL'}
        ORDER BY r.created_at DESC
        LIMIT 200`,
    ).all();

    return {
      state,
      reports: rows.map((r) => ({
        id: r.id,
        reason: r.reason,
        createdAt: r.created_at,
        resolvedAt: r.resolved_at,
        resolution: r.resolution,
        reporter: r.reporter ?? '(deleted account)',
        map: {
          id: r.map_id,
          published: !!r.published,
          description: r.map_description,
          pluginName: r.plugin_name,
          pluginSlug: r.plugin_slug,
          author: r.author,
        },
      })),
    };
  });

  app.post('/reports/:id/resolve', async (req, reply) => {
    const resolution = String(req.body?.resolution ?? '').trim().slice(0, 500);
    if (!resolution) {
      return reply.code(400).send({ error: 'bad_resolution', detail: 'say what you decided' });
    }
    const info = getDb().prepare(
      'UPDATE reports SET resolved_at = ?, resolution = ? WHERE id = ? AND resolved_at IS NULL',
    ).run(now(), resolution, Number(req.params.id));
    if (!info.changes) return reply.code(404).send({ error: 'not_found' });
    return { ok: true };
  });

  // ------------------------------------------------------------------ maps

  app.post('/maps/:id/unpublish', async (req, reply) => {
    const note = String(req.body?.note ?? '').trim().slice(0, 500);
    if (!note) {
      // The note is shown to the author on their own page. Unpublishing
      // silently would leave them with a map that vanished for no stated
      // reason, which is how a small community turns sour.
      return reply.code(400).send({ error: 'note_required', detail: 'the author sees this note — say why' });
    }
    const info = getDb().prepare(
      'UPDATE maps SET published = 0, unpublished_at = ?, unpublish_note = ? WHERE id = ? AND published = 1',
    ).run(now(), note, Number(req.params.id));
    if (!info.changes) return reply.code(404).send({ error: 'not_found' });
    return { ok: true };
  });

  app.post('/maps/:id/publish', async (req, reply) => {
    const info = getDb().prepare(
      'UPDATE maps SET published = 1, unpublished_at = NULL, unpublish_note = NULL WHERE id = ?',
    ).run(Number(req.params.id));
    if (!info.changes) return reply.code(404).send({ error: 'not_found' });
    return { ok: true };
  });

  // -------------------------------------------------------------- accounts

  app.get('/accounts', async (req) => {
    const search = String(req.query?.search ?? '').trim();
    const rows = getDb().prepare(
      `SELECT a.id, a.display_name, a.created_at, a.is_admin, a.suspended_at,
              (SELECT COUNT(*) FROM maps m WHERE m.account_id = a.id) AS map_count
         FROM accounts a
        WHERE (? = '' OR a.display_name LIKE '%' || ? || '%')
        ORDER BY a.created_at DESC LIMIT 200`,
    ).all(search, search);
    return {
      accounts: rows.map((a) => ({
        id: a.id,
        displayName: a.display_name,
        createdAt: a.created_at,
        isAdmin: !!a.is_admin,
        suspendedAt: a.suspended_at,
        mapCount: a.map_count,
      })),
    };
  });

  app.post('/accounts/:id/suspend', async (req, reply) => {
    const id = Number(req.params.id);
    if (id === req.account.id) {
      return reply.code(400).send({ error: 'self', detail: 'you cannot suspend yourself' });
    }
    const info = getDb().prepare(
      'UPDATE accounts SET suspended_at = ? WHERE id = ? AND suspended_at IS NULL',
    ).run(now(), id);
    if (!info.changes) return reply.code(404).send({ error: 'not_found' });
    // Leaving their cookies alive would make the suspension cosmetic until
    // each one happened to expire.
    destroyAccountSessions(id);
    return { ok: true };
  });

  app.post('/accounts/:id/unsuspend', async (req, reply) => {
    const info = getDb().prepare(
      'UPDATE accounts SET suspended_at = NULL WHERE id = ?',
    ).run(Number(req.params.id));
    if (!info.changes) return reply.code(404).send({ error: 'not_found' });
    return { ok: true };
  });

  // ----------------------------------------------------------------- merge

  // These propose; they never act on their own. Auto-merging on a substring
  // stays refused — the rule that folds "Pro-C 2" into "FabFilter Pro-C 2"
  // also folds "The Analog Molecule" into "… Deluxe", which are different
  // JSFX by different authors.
  app.get('/merge/suggestions', async () => {
    return suggestMerges();
  });

  app.post('/merge/plugins', async (req, reply) => {
    const target = Number(req.body?.targetId);
    const source = Number(req.body?.sourceId);
    if (!target || !source || target === source) {
      return reply.code(400).send({ error: 'bad_ids', detail: 'need two different plugin ids' });
    }
    try {
      return { ok: true, ...mergePlugins(target, source) };
    } catch (err) {
      return reply.code(400).send({ error: 'merge_failed', detail: err.message });
    }
  });

  app.post('/merge/vendors', async (req, reply) => {
    const target = Number(req.body?.targetId);
    const source = Number(req.body?.sourceId);
    if (!target || !source || target === source) {
      return reply.code(400).send({ error: 'bad_ids', detail: 'need two different vendor ids' });
    }
    try {
      return { ok: true, ...mergeVendors(target, source) };
    } catch (err) {
      return reply.code(400).send({ error: 'merge_failed', detail: err.message });
    }
  });

  // ---------------------------------------------------------------- digest

  app.get('/overview', async () => {
    const db = getDb();
    const one = (sql) => db.prepare(sql).get().c;
    return {
      openReports: one('SELECT COUNT(*) c FROM reports WHERE resolved_at IS NULL'),
      maps: one('SELECT COUNT(*) c FROM maps WHERE published = 1'),
      unpublished: one('SELECT COUNT(*) c FROM maps WHERE published = 0'),
      plugins: one('SELECT COUNT(*) c FROM plugins'),
      vendors: one('SELECT COUNT(*) c FROM vendors'),
      accounts: one('SELECT COUNT(*) c FROM accounts'),
      suspended: one('SELECT COUNT(*) c FROM accounts WHERE suspended_at IS NOT NULL'),
    };
  });
}
