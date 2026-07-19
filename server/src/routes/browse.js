// Browse + facets — screen 1, and the vendor combobox.

import { z } from 'zod';
import { listPlugins, listVendors } from '../query/browse.js';
import { getDb } from '../db/index.js';

const browseQuery = z.object({
  search: z.string().max(120).optional().default(''),
  vendor: z.coerce.number().int().positive().optional(),
  surface: z.enum(['uc1', 'uf8', 'uc1+uf8']).optional(),
  sort: z.enum(['name', 'maps', 'newest', 'coverage']).optional().default('name'),
  page: z.coerce.number().int().positive().optional().default(1),
  pageSize: z.coerce.number().int().positive().max(200).optional().default(60),
});

export async function registerBrowseRoutes(app) {
  app.get('/plugins', async (req, reply) => {
    const parsed = browseQuery.safeParse(req.query);
    if (!parsed.success) {
      return reply.code(400).send({ error: 'bad_query', detail: parsed.error.issues });
    }
    const { search, vendor, surface, sort, page, pageSize } = parsed.data;

    // A vendor filter must name a real vendor — an unknown id would silently
    // return an empty page and read as "no maps" rather than "bad filter".
    if (vendor != null) {
      const exists = getDb().prepare('SELECT 1 FROM vendors WHERE id = ?').get(vendor);
      if (!exists) return reply.code(404).send({ error: 'unknown_vendor' });
    }

    return listPlugins({ search, vendor: vendor ?? null, surface: surface ?? null, sort, page, pageSize });
  });

  app.get('/vendors', async (req) => {
    const search = typeof req.query.search === 'string' ? req.query.search.slice(0, 120) : '';
    return { vendors: listVendors({ search }) };
  });
}
