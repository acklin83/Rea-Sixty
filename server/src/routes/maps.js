// Plug-in page (with the diff selection), one-map detail, and download.

import { createReadStream } from 'node:fs';
import { join } from 'node:path';
import { z } from 'zod';
import { getPlugin, getMap, getMapFile } from '../query/detail.js';
import { diffMaps, MAX_DIFF_COLUMNS } from '../query/diff.js';
import { MAPS_DIR } from '../db/index.js';

const diffQuery = z.object({
  maps: z.string().regex(/^\d+(,\d+)*$/, 'maps must be comma-separated ids'),
});

export async function registerMapRoutes(app) {
  // Screen 2 — one plug-in, its maps, ready to tick 2-3 and diff.
  app.get('/plugins/:slug', async (req, reply) => {
    const plugin = getPlugin(req.params.slug);
    if (!plugin) return reply.code(404).send({ error: 'unknown_plugin' });
    return plugin;
  });

  // The per-control diff — a SELECTION, capped at MAX_DIFF_COLUMNS because a
  // diff table is unreadable past ~4 columns.
  app.get('/plugins/:slug/diff', async (req, reply) => {
    const plugin = getPlugin(req.params.slug);
    if (!plugin) return reply.code(404).send({ error: 'unknown_plugin' });

    const parsed = diffQuery.safeParse(req.query);
    if (!parsed.success) {
      return reply.code(400).send({ error: 'bad_query', detail: parsed.error.issues });
    }
    const ids = [...new Set(parsed.data.maps.split(',').map(Number))];
    if (ids.length < 2) {
      return reply.code(400).send({ error: 'need_two_maps' });
    }
    if (ids.length > MAX_DIFF_COLUMNS) {
      return reply.code(400).send({ error: 'too_many_maps', max: MAX_DIFF_COLUMNS });
    }
    // Every id must belong to THIS plug-in — diffing maps of different plug-ins
    // is meaningless and would line up unrelated controls.
    const own = new Set(plugin.maps.map((m) => m.id));
    const foreign = ids.filter((id) => !own.has(id));
    if (foreign.length) {
      return reply.code(400).send({ error: 'maps_not_in_plugin', ids: foreign });
    }
    try {
      return diffMaps(ids);
    } catch (e) {
      return reply.code(400).send({ error: 'diff_failed', detail: e.message });
    }
  });

  // Screen 3 — one map in full.
  app.get('/maps/:id', async (req, reply) => {
    const id = Number(req.params.id);
    if (!Number.isInteger(id) || id < 1) return reply.code(400).send({ error: 'bad_id' });
    const map = getMap(id);
    if (!map) return reply.code(404).send({ error: 'unknown_map' });
    return map;
  });

  // Download serves the stored .rea60map; the user imports it via the
  // extension's Import map… (Phase 1). Pure data — no actions, no macros.
  app.get('/maps/:id/download', async (req, reply) => {
    const id = Number(req.params.id);
    if (!Number.isInteger(id) || id < 1) return reply.code(400).send({ error: 'bad_id' });
    const file = getMapFile(id);
    if (!file) return reply.code(404).send({ error: 'unknown_map' });

    reply
      .header('Content-Type', 'application/json; charset=utf-8')
      .header('Content-Disposition', `attachment; filename="${file.filename}"`)
      .header('Content-Length', file.bytes);
    return reply.send(createReadStream(join(MAPS_DIR, file.relPath)));
  });
}
