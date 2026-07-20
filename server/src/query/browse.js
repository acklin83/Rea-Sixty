// Screen 1 — /mappings. ONE ROW PER PLUG-IN, not per map. Listing maps means
// "FabFilter Pro-Q 3" fourteen times down the page; at a few hundred maps that
// is noise, not a list. Search-first, facet rail (surface · vendor), sort,
// paginated.

import { getDb } from '../db/index.js';

const SORTS = {
  name: 'p.name COLLATE NOCASE ASC',
  maps: 'map_count DESC, p.name COLLATE NOCASE ASC',
  newest: 'newest_at DESC, p.name COLLATE NOCASE ASC',
  // "coverage" sorts by the displayed metric — plug-in (parameter) coverage.
  coverage: 'best_param_coverage DESC, p.name COLLATE NOCASE ASC',
};

const SURFACES = new Set(['uc1', 'uf8', 'uc1+uf8']);

/**
 * Browse plug-ins.
 *
 * `surface` filters on a plug-in having AT LEAST ONE map of that surface — the
 * index column is a union, so filtering must match the union, not a single
 * map. `vendor` is an exact vendor slug/id (the combobox resolves the name to
 * an id first). Returns { rows, total, page, pageSize }.
 */
export function listPlugins({
  search = '', vendor = null, surface = null, sort = 'name',
  page = 1, pageSize = 60,
} = {}) {
  const db = getDb();
  const where = ['p.id IN (SELECT plugin_id FROM maps WHERE published = 1)'];
  const params = {};

  // Additive, token-based search across BOTH name and vendor: every token must
  // match one or the other. So "fab" finds all FabFilter plug-ins (the vendor
  // carries "FabFilter" even when the name is just "Pro-L 2"), and "fab Q"
  // finds "FabFilter Pro-Q" (fab -> vendor, Q -> name).
  const tokens = search.trim().split(/\s+/).filter(Boolean).slice(0, 8);
  tokens.forEach((tok, i) => {
    where.push(`(p.name LIKE :t${i} ESCAPE '\\' OR COALESCE(p.vendor, '') LIKE :t${i} ESCAPE '\\')`);
    params[`t${i}`] = `%${tok.replace(/[\\%_]/g, '\\$&')}%`;
  });
  if (vendor != null) {
    where.push('p.vendor_id = :vendor');
    params.vendor = vendor;
  }
  if (surface && SURFACES.has(surface)) {
    // "has at least one map of this surface" — matches the union column.
    where.push(`EXISTS (SELECT 1 FROM maps m2
                 WHERE m2.plugin_id = p.id AND m2.published = 1
                   AND m2.surfaces = :surface)`);
    params.surface = surface;
  }

  const whereSql = where.length ? `WHERE ${where.join(' AND ')}` : '';
  const orderSql = SORTS[sort] ?? SORTS.name;
  const size = Math.min(Math.max(1, pageSize | 0), 200);
  const p = Math.max(1, page | 0);

  const total = db.prepare(
    `SELECT COUNT(*) c FROM plugin_index p ${whereSql}`,
  ).get(params).c;

  const rows = db.prepare(`
    SELECT p.id, p.slug, p.name, p.fx_type, p.vendor,
           p.map_count, p.best_coverage, p.best_param_coverage, p.works_count, p.surfaces
      FROM plugin_index p
      ${whereSql}
     ORDER BY ${orderSql}
     LIMIT :limit OFFSET :offset
  `).all({ ...params, limit: size, offset: (p - 1) * size });

  return {
    rows: rows.map((r) => ({
      slug: r.slug,
      name: r.name,
      fxType: r.fx_type,
      vendor: r.vendor,
      mapCount: r.map_count,
      // surfaces is a GROUP_CONCAT of DISTINCT values — split to an array.
      surfaces: r.surfaces ? [...new Set(r.surfaces.split(','))].sort() : [],
      bestCoverage: r.best_coverage == null ? null : Math.round(r.best_coverage * 100),
      bestParamCoverage: r.best_param_coverage == null ? null : Math.round(r.best_param_coverage * 100),
      worksCount: r.works_count,
    })),
    total,
    page: p,
    pageSize: size,
  };
}

/**
 * Vendor facet data — the searchable combobox. 80 distinct vendors on one dev
 * machine, several hundred in a real corpus, so this is type-ahead with counts,
 * never a checkbox rail. Counts are published maps' plug-ins per vendor.
 */
export function listVendors({ search = '' } = {}) {
  const db = getDb();
  const rows = db.prepare(`
    SELECT v.id, v.name, COUNT(DISTINCT p.id) AS plugin_count
      FROM vendors v
      JOIN plugins p ON p.vendor_id = v.id
     WHERE p.id IN (SELECT plugin_id FROM maps WHERE published = 1)
       AND (:q = '' OR v.name LIKE :like)
     GROUP BY v.id
     ORDER BY plugin_count DESC, v.name COLLATE NOCASE ASC
  `).all({ q: search.trim(), like: `%${search.trim()}%` });
  return rows.map((r) => ({ id: r.id, name: r.name, pluginCount: r.plugin_count }));
}
