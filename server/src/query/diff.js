// The per-control diff — screen 2, the one that decides whether this is a
// service or a file dump.
//
// It must show a per-control DIFF, not N per-map summaries. The evidence is
// the only genuine same-plug-in pair in the seed corpus: both Pro-C 2 maps
// read "7 of 8, Bus Comp" and look interchangeable. They are not — MIX carries
// `Wet` in one and `Dry Gain` in the other. A summary that hides the only
// difference is worse than no comparison: it actively misinforms.
//
// Comparison is a SELECTION, not the default view. A diff table stops being
// readable past about four columns, so a plug-in with fifteen maps gets
// checkboxes and the caller passes the two or three that were ticked.

import { getDb } from '../db/index.js';
import { lookupSlot, SECTION_ORDER } from '../lib/controls.js';

export const MAX_DIFF_COLUMNS = 4;

/**
 * Diff N maps of the same plug-in.
 *
 * Rows are the union of controls bound by ANY of them, so a control one map
 * binds and another does not still gets a row — that absence is a real
 * difference and dropping the row would hide it.
 */
export function diffMaps(mapIds, { layer = 'normal' } = {}) {
  if (!Array.isArray(mapIds) || mapIds.length < 2) {
    throw new Error('a diff needs at least two maps');
  }
  if (mapIds.length > MAX_DIFF_COLUMNS) {
    throw new Error(`a diff table is unreadable past ${MAX_DIFF_COLUMNS} columns`);
  }
  const db = getDb();
  const ph = mapIds.map(() => '?').join(',');

  const maps = db.prepare(
    `SELECT id, author_name, surfaces, domain, coverage_n, coverage_d
       FROM maps WHERE id IN (${ph}) ORDER BY id`,
  ).all(...mapIds);
  if (maps.length !== mapIds.length) throw new Error('unknown map id in diff set');

  const domains = new Set(maps.map((m) => m.domain));
  if (domains.size > 1) {
    // linkIdx is namespaced per domain — CS 1 is FaderLevel, BC 1 is
    // Threshold. Diffing across domains would line up unrelated controls.
    throw new Error(`cannot diff across domains: ${[...domains].join(', ')}`);
  }
  const domain = maps[0].domain;

  const bindings = db.prepare(
    `SELECT map_id, link_idx, param_name, on_face
       FROM map_bindings WHERE map_id IN (${ph}) AND mod_layer = ?`,
  ).all(...mapIds, layer);

  const byControl = new Map();
  for (const b of bindings) {
    if (!byControl.has(b.link_idx)) byControl.set(b.link_idx, new Map());
    byControl.get(b.link_idx).set(b.map_id, b.param_name);
  }

  const rows = [];
  for (const [linkIdx, cells] of byControl) {
    const slot = lookupSlot(domain, linkIdx);
    const values = maps.map((m) => cells.get(m.id) ?? null);
    const present = values.filter((v) => v !== null);
    // Differs if any map omits the control, or the bound params disagree.
    const differs = present.length !== values.length
      || new Set(present).size > 1;
    rows.push({
      linkIdx,
      // A linkIdx with no control must never silently vanish — it renders in
      // an "also mapped" list rather than being dropped.
      name: slot?.name ?? `linkIdx ${linkIdx}`,
      section: slot?.section ?? null,
      onFace: slot?.on_face === true,
      values,
      differs,
    });
  }

  const order = (r) => {
    const i = SECTION_ORDER.indexOf(r.section);
    return [r.onFace ? 0 : 1, i < 0 ? SECTION_ORDER.length : i, r.linkIdx];
  };
  rows.sort((a, b) => {
    const [x, y] = [order(a), order(b)];
    return x[0] - y[0] || x[1] - y[1] || x[2] - y[2];
  });

  const differing = rows.filter((r) => r.differs).length;
  return {
    maps,
    domain,
    rows,
    summary: {
      bound: rows.length,
      identical: rows.length - differing,
      different: differing,
      // The plan's headline, verbatim: "8 bound controls between them —
      // 6 identical, 2 different".
      headline: `${rows.length} bound controls between them — `
        + `${rows.length - differing} identical, ${differing} different`,
    },
  };
}
