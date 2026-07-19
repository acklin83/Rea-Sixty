// Write surface — upload a mapping straight from the extension, no file dance.
//
// The extension serialises the current map (the same .rea60map envelope the
// Share… dialog writes) and POSTs it here with its device token. ingestMap does
// the security gate, coverage, identity and diff-row extraction — the exact
// same path the seed and the file import use, so there is one ingest, not two.

import { requireToken } from '../lib/auth.js';
import { ingestMap } from '../ingest/service.js';
import { IngestError, MAX_BYTES } from '../lib/rea60map.js';
import { getDb } from '../db/index.js';

// IngestError codes → HTTP status. The security rejections and malformed input
// are the client's fault (400/413); nothing here is a 500.
const STATUS = {
  bundle_rejected: 422,
  too_large: 413,
  duplicate_mapping: 409,
};

export async function registerUploadRoutes(app) {
  // The envelope is itself JSON, but we want the RAW bytes (for the stored
  // file + sha), so take it as text rather than a parsed object. Scoped to a
  // content type the browse routes never use.
  app.addContentTypeParser(
    'application/octet-stream', { parseAs: 'string', bodyLimit: MAX_BYTES + 4096 },
    (_req, body, done) => done(null, body),
  );

  app.post('/maps', {
    preHandler: requireToken,
    config: { rateLimit: { max: 30, timeWindow: '1 minute' } },
  }, async (req, reply) => {
    const text = typeof req.body === 'string' ? req.body : null;
    if (!text) {
      return reply.code(400).send({ error: 'empty_body', detail: 'POST the .rea60map as application/octet-stream' });
    }

    // Optional metadata the extension may pass as query params — the plug-in
    // the uploader confirmed, and whether this replaces an earlier map of
    // theirs. Both are advisory; ingest works without them.
    const preferredPluginName = typeof req.query.plugin === 'string'
      ? req.query.plugin.slice(0, 200) : null;
    let replacesId = null;
    if (req.query.replaces != null) {
      const rid = Number(req.query.replaces);
      // You may only replace YOUR OWN map — checked against the token's account.
      const owned = Number.isInteger(rid) && getDb().prepare(
        'SELECT 1 FROM maps WHERE id = ? AND account_id = ?',
      ).get(rid, req.account.accountId);
      if (!owned) return reply.code(400).send({ error: 'replaces_not_yours' });
      replacesId = rid;
    }

    try {
      const { mapId, pluginId, coverage } = ingestMap(text, {
        accountId: req.account.accountId,
        preferredPluginName,
        replacesId,
      });
      const slug = getDb().prepare('SELECT slug FROM plugins WHERE id = ?').get(pluginId).slug;
      return reply.code(201).send({
        mapId,
        pluginSlug: slug,
        coverage: { n: coverage.n, d: coverage.d },
        // So the extension can deep-link the user to their fresh upload.
        url: `/mappings/${slug}`,
      });
    } catch (e) {
      if (e instanceof IngestError) {
        return reply.code(STATUS[e.code] ?? 400).send({ error: e.code, detail: e.message });
      }
      req.log.error(e);
      return reply.code(500).send({ error: 'ingest_failed' });
    }
  });
}
