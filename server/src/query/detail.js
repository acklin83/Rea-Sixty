// Screen 2 (per-plug-in) and screen 3 (one map) data.
//
// The picture is carried WITHOUT a faceplate (dies at 3.6 px on a phone): a
// coverage strip (one cell per on-face control, grouped by section, no text so
// it scales), a section-grouped control table with per-section n/total, and a
// domain-scoped coverage number. Off-face bindings never vanish — they surface
// in a separate "also mapped" list.

import { getDb } from '../db/index.js';
import { faceSlots, denominatorFor, lookupSlot, SECTION_ORDER, UF8_VPOT_SLOTS, UF8_STRIPS } from '../lib/controls.js';

/** Normal-layer bindings for a map, keyed by linkIdx. */
function normalBindings(db, mapId) {
  const rows = db.prepare(
    `SELECT link_idx, param_name, on_face FROM map_bindings
      WHERE map_id = ? AND mod_layer = 'normal'`,
  ).all(mapId);
  return new Map(rows.map((r) => [r.link_idx, r]));
}

/**
 * Section-grouped coverage for one CS/BC map: every on-face control in the
 * domain, lit where bound. This is both the strip (cells) and the table
 * (per-section n/total). UF8-only maps have no face controls — callers use the
 * uf8 counts instead.
 */
export function coverageSections(mapId, domain) {
  const db = getDb();
  const bound = normalBindings(db, mapId);
  const sections = new Map();
  for (const slot of faceSlots(domain)) {
    const s = slot.section ?? 'Channel';
    if (!sections.has(s)) sections.set(s, { name: s, total: 0, hit: 0, cells: [] });
    const g = sections.get(s);
    const b = bound.get(slot.linkIdx);
    const isHit = !!b;
    g.total += 1;
    g.hit += isHit ? 1 : 0;
    g.cells.push({
      linkIdx: slot.linkIdx,
      name: slot.name,
      legend: slot.legend,
      bound: isHit,
      param: b?.param_name ?? null,
    });
  }
  // Stable section order for the strip and the table.
  return [...sections.values()].sort(
    (a, b) => SECTION_ORDER.indexOf(a.name) - SECTION_ORDER.indexOf(b.name),
  );
}

/** Off-face normal-layer bindings — the "also mapped" list. Never dropped. */
export function alsoMapped(mapId) {
  const db = getDb();
  return db.prepare(
    `SELECT link_idx, param_name FROM map_bindings
      WHERE map_id = ? AND mod_layer = 'normal' AND on_face = 0
      ORDER BY link_idx`,
  ).all(mapId).map((r) => ({ linkIdx: r.link_idx, param: r.param_name }));
}

/** Modifier-layer bindings, so a control mapped only on a layer is not hidden.
 *  Carries the resolved control name so the detail page can table them. */
function modifierBindings(mapId, domain) {
  const db = getDb();
  return db.prepare(
    `SELECT link_idx, mod_layer, param_name FROM map_bindings
      WHERE map_id = ? AND mod_layer != 'normal'
      ORDER BY mod_layer, link_idx`,
  ).all(mapId).map((r) => {
    const slot = lookupSlot(domain, r.link_idx);
    return {
      linkIdx: r.link_idx,
      layer: r.mod_layer,
      control: slot?.name ?? `linkIdx ${r.link_idx}`,
      param: r.param_name,
    };
  });
}

function coverageOf(row) {
  const pct = row.coverage_d ? Math.round((100 * row.coverage_n) / row.coverage_d) : 0;
  return { n: row.coverage_n, d: row.coverage_d, pct };
}

/** Screen 2 — one plug-in and its maps (the diff SELECTION list). */
export function getPlugin(slug) {
  const db = getDb();
  const plugin = db.prepare(`
    SELECT p.id, p.slug, p.name, p.fx_type, v.name AS vendor
      FROM plugins p LEFT JOIN vendors v ON v.id = p.vendor_id
     WHERE p.slug = ?
  `).get(slug);
  if (!plugin) return null;

  const maps = db.prepare(`
    SELECT m.id, m.surfaces, m.domain, m.author_name, m.description,
           m.coverage_n, m.coverage_d, m.uf8_vpots, m.uf8_strips, m.uploaded_at,
           (SELECT COUNT(*) FROM works_for_me w WHERE w.map_id = m.id) AS works
      FROM maps m
     WHERE m.plugin_id = ? AND m.published = 1
     ORDER BY (CAST(m.coverage_n AS REAL) / m.coverage_d) DESC, m.uploaded_at DESC
  `).all(plugin.id);

  return {
    slug: plugin.slug,
    name: plugin.name,
    fxType: plugin.fx_type,
    vendor: plugin.vendor,
    maps: maps.map((m) => ({
      id: m.id,
      surfaces: m.surfaces,
      domain: m.domain,
      author: m.author_name,
      description: m.description,
      coverage: coverageOf(m),
      uf8: m.domain === 'None' ? { vpots: m.uf8_vpots, strips: m.uf8_strips } : null,
      works: m.works,
      // The per-map strip belongs here, where rows genuinely are maps.
      sections: m.domain === 'None' ? [] : coverageSections(m.id, m.domain),
    })),
  };
}

/** Screen 3 — one map in full. */
export function getMap(id) {
  const db = getDb();
  const m = db.prepare(`
    SELECT m.*, p.slug AS plugin_slug, p.name AS plugin_name, v.name AS vendor
      FROM maps m
      JOIN plugins p ON p.id = m.plugin_id
      LEFT JOIN vendors v ON v.id = p.vendor_id
     WHERE m.id = ? AND m.published = 1
  `).get(id);
  if (!m) return null;

  const isUf8 = m.domain === 'None';
  return {
    id: m.id,
    plugin: { slug: m.plugin_slug, name: m.plugin_name },
    vendor: m.vendor,
    surfaces: m.surfaces,
    domain: m.domain,
    author: m.author_name,
    description: m.description,
    licence: m.licence,
    createdAt: m.created_at,
    uploadedAt: m.uploaded_at,
    fileBytes: m.file_bytes,
    coverage: coverageOf(m),
    uf8: isUf8 ? uf8Grid(m.id, m.uf8_vpots, m.uf8_strips) : null,
    sections: isUf8 ? [] : coverageSections(m.id, m.domain),
    alsoMapped: alsoMapped(m.id),
    modifierLayers: modifierBindings(m.id, m.domain),
  };
}

/** The bound UF8 slots for the detail page's per-bank grid. */
function uf8Grid(mapId, vpots, strips) {
  const db = getDb();
  const rows = db.prepare(
    `SELECT kind, fader_bank, vpot_bank, strip, label, param_name
       FROM uf8_slots WHERE map_id = ? ORDER BY fader_bank, vpot_bank, strip`,
  ).all(mapId);
  return {
    vpots, vpotSlots: UF8_VPOT_SLOTS, strips, stripSlots: UF8_STRIPS,
    vpotBindings: rows.filter((r) => r.kind === 'vpot').map((r) => ({
      faderBank: r.fader_bank, vpotBank: r.vpot_bank, strip: r.strip,
      label: r.label ?? '', param: r.param_name ?? '',
    })),
    stripBindings: rows.filter((r) => r.kind !== 'vpot').map((r) => ({
      faderBank: r.fader_bank, strip: r.strip, kind: r.kind,
      label: r.label ?? '', param: r.param_name ?? '',
    })),
  };
}

/** File path + name for the download route. */
export function getMapFile(id) {
  const db = getDb();
  const m = db.prepare(
    `SELECT m.file_path, m.file_bytes, p.slug FROM maps m
       JOIN plugins p ON p.id = m.plugin_id
      WHERE m.id = ? AND m.published = 1`,
  ).get(id);
  if (!m) return null;
  return { relPath: m.file_path, bytes: m.file_bytes, filename: `${m.slug}-${id}.rea60map` };
}
