// Merging two plug-in rows that are really the same plug-in.
//
// WHY THIS IS NEEDED AND NOT AUTOMATIC
// The index lists one row per plug-in, but a map's idea of its plug-in is
// `match` — a USER-EDITABLE substring. The live catalog holds both
// "FabFilter Pro-C 2" and "Pro-C 2". Those are the same plug-in and they are
// also the plan's flagship example: the two maps look interchangeable (both
// "7 of 8, Bus Comp") and differ only on one MIX binding. Split across two
// rows, the per-control diff — the screen that decides whether this is a
// service or a file dump — has nothing to compare.
//
// Fuzzy auto-merge is NOT the answer: "Pro-C 2" is a substring of "FabFilter
// Pro-C 2" and merging on substring would also fold "EQ" into "4-Band EQ".
// So the mechanism is explicit — the uploader confirms the plug-in on the
// upload form, and an admin merges what slips through. This is that merge.

import { getDb } from '../db/index.js';
import { normPlugin, normVendor } from '../lib/vendor.js';

/**
 * Fold `sourceId` into `targetId`: move its maps, repoint its aliases, and
 * leave an alias behind so the old name still resolves on a later upload.
 * Returns { movedMaps, aliases }.
 */
export function mergePlugins(targetId, sourceId) {
  const db = getDb();
  if (targetId === sourceId) throw new Error('cannot merge a plug-in into itself');

  const tx = db.transaction(() => {
    const target = db.prepare('SELECT * FROM plugins WHERE id = ?').get(targetId);
    const source = db.prepare('SELECT * FROM plugins WHERE id = ?').get(sourceId);
    if (!target) throw new Error(`no plug-in ${targetId}`);
    if (!source) throw new Error(`no plug-in ${sourceId}`);

    const moved = db.prepare('UPDATE maps SET plugin_id = ? WHERE plugin_id = ?')
      .run(targetId, sourceId).changes;

    // Existing aliases of the source now point at the target.
    db.prepare('UPDATE plugin_aliases SET plugin_id = ? WHERE plugin_id = ?')
      .run(targetId, sourceId);

    // The source's own name becomes an alias, so the next upload carrying that
    // match lands on the merged row instead of recreating the split.
    db.prepare('INSERT OR REPLACE INTO plugin_aliases (norm, plugin_id) VALUES (?, ?)')
      .run(normPlugin(source.name), targetId);

    // Inherit a vendor the target lacked.
    if (!target.vendor_id && source.vendor_id) {
      db.prepare('UPDATE plugins SET vendor_id = ? WHERE id = ?').run(source.vendor_id, targetId);
    }

    db.prepare('DELETE FROM plugins WHERE id = ?').run(sourceId);
    const aliases = db.prepare('SELECT COUNT(*) c FROM plugin_aliases WHERE plugin_id = ?')
      .get(targetId).c;
    return { movedMaps: moved, aliases };
  });

  return tx();
}

/**
 * Fold vendor `sourceId` into `targetId`: move its plug-ins, and leave a
 * dynamic alias (source name -> target) so a later upload of the source name
 * resolves to the target. For dupes the static vendor-aliases.json didn't
 * catch. Returns { movedPlugins }.
 */
export function mergeVendors(targetId, sourceId) {
  const db = getDb();
  if (targetId === sourceId) throw new Error('cannot merge a vendor into itself');

  const tx = db.transaction(() => {
    const target = db.prepare('SELECT * FROM vendors WHERE id = ?').get(targetId);
    const source = db.prepare('SELECT * FROM vendors WHERE id = ?').get(sourceId);
    if (!target) throw new Error(`no vendor ${targetId}`);
    if (!source) throw new Error(`no vendor ${sourceId}`);

    const moved = db.prepare('UPDATE plugins SET vendor_id = ? WHERE vendor_id = ?')
      .run(targetId, sourceId).changes;

    // The source's name becomes a dynamic alias of the target.
    db.prepare('INSERT OR REPLACE INTO vendor_aliases (norm, vendor_id) VALUES (?, ?)')
      .run(normVendor(source.name), targetId);

    db.prepare('DELETE FROM vendors WHERE id = ?').run(sourceId);
    return { movedPlugins: moved };
  });

  return tx();
}

/**
 * Candidate pairs for an admin to review. Deliberately SUGGESTS rather than
 * acts: one name being a prefix/suffix of another is good evidence for
 * "FabFilter Pro-C 2" / "Pro-C 2" and bad evidence for "EQ" / "4-Band EQ",
 * and only a human can tell those apart.
 */
export function suggestMerges({ minLength = 5 } = {}) {
  const db = getDb();
  const rows = db.prepare('SELECT id, name, norm FROM plugins ORDER BY length(norm) DESC').all();
  const out = [];
  for (const a of rows) {
    for (const b of rows) {
      if (a.id === b.id || b.norm.length < minLength || b.norm.length >= a.norm.length) continue;
      // Require a word boundary so "eq" does not match inside "4-band eq"
      // by accident of position.
      if (a.norm.endsWith(` ${b.norm}`) || a.norm.startsWith(`${b.norm} `)) {
        out.push({ keep: { id: a.id, name: a.name }, fold: { id: b.id, name: b.name } });
      }
    }
  }
  return out;
}
