// Turn a validated .rea60map into rows.
//
// Everything here runs inside one transaction per map: a half-ingested map
// (metadata but no bindings) would render as a legitimate-looking 0-coverage
// entry, which is the silent-wrong-number failure this project keeps hitting.

import { createHash } from 'node:crypto';
import { writeFileSync, mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { getDb, now, MAPS_DIR } from '../db/index.js';
import { parseRea60Map } from '../lib/rea60map.js';
import {
  vendorFromName, normVendor, pluginNameFromMatch, normPlugin, slugify, fxTypeOf,
} from '../lib/vendor.js';

/** Resolve or create a vendor, honouring the alias table. */
export function resolveVendor(db, rawName) {
  const name = String(rawName ?? '').trim().replace(/\s+/g, ' ');
  if (!name) return null;
  const norm = normVendor(name);

  const alias = db.prepare('SELECT vendor_id FROM vendor_aliases WHERE norm = ?').get(norm);
  if (alias) return alias.vendor_id;

  const hit = db.prepare('SELECT id FROM vendors WHERE norm = ?').get(norm);
  if (hit) return hit.id;

  return db.prepare(
    'INSERT INTO vendors (name, norm, created_at) VALUES (?, ?, ?)',
  ).run(name, norm, now()).lastInsertRowid;
}

/**
 * Resolve or create the plug-in row — the unit the index lists.
 *
 * `preferredName` is what the uploader confirmed on the form. Without it we
 * fall back to deriving from `match`, which is a user-editable substring and
 * WILL under-merge ("FabFilter Pro-C 2" and "Pro-C 2" are the same plug-in but
 * normalise differently). That is why the upload form asks. Admin merge via
 * plugin_aliases closes the rest.
 */
export function resolvePlugin(db, { match, preferredName, vendorId, fxType }) {
  const name = (preferredName ?? pluginNameFromMatch(match)).trim();
  const norm = normPlugin(name);
  if (!norm) throw new Error('cannot derive a plug-in name');

  const alias = db.prepare('SELECT plugin_id FROM plugin_aliases WHERE norm = ?').get(norm);
  if (alias) return alias.plugin_id;

  const hit = db.prepare('SELECT id FROM plugins WHERE norm = ?').get(norm);
  if (hit) {
    // Backfill a vendor learned later — the first upload of a plug-in may have
    // come from a JSFX map with no vendor in the name.
    if (vendorId) {
      db.prepare('UPDATE plugins SET vendor_id = COALESCE(vendor_id, ?) WHERE id = ?')
        .run(vendorId, hit.id);
    }
    return hit.id;
  }

  // Slugs must be unique; disambiguate rather than collide.
  const base = slugify(name);
  let slug = base;
  for (let i = 2; db.prepare('SELECT 1 FROM plugins WHERE slug = ?').get(slug); i++) {
    slug = `${base}-${i}`;
  }
  return db.prepare(
    'INSERT INTO plugins (slug, name, norm, vendor_id, fx_type, created_at) VALUES (?,?,?,?,?,?)',
  ).run(slug, name, norm, vendorId ?? null, fxType ?? null, now()).lastInsertRowid;
}

/**
 * Ingest one file. Returns { mapId, pluginId, coverage }.
 * Throws IngestError (from parseRea60Map) on anything malformed or unsafe.
 */
export function ingestMap(text, { accountId, preferredPluginName = null, replacesId = null } = {}) {
  const parsed = parseRea60Map(text);
  const db = getDb();

  const { envelope, map, bindings, coverage } = parsed;
  const fxType = fxTypeOf(map.match);
  // Envelope vendor is authoritative (captured at export from original_name).
  // Fall back to parsing the match only when the file carries none.
  const vendorName = envelope.vendor || vendorFromName(map.match, fxType);

  const sha = createHash('sha256').update(text, 'utf8').digest('hex');
  const bytes = Buffer.byteLength(text, 'utf8');

  const tx = db.transaction(() => {
    const vendorId = resolveVendor(db, vendorName);
    const pluginId = resolvePlugin(db, {
      match: map.match, preferredName: preferredPluginName, vendorId, fxType,
    });

    const ts = now();
    const info = db.prepare(`
      INSERT INTO maps (plugin_id, account_id, replaces_id, surfaces, domain,
                        author_name, description, licence, created_at, uploaded_at,
                        coverage_n, coverage_d, uf8_vpots, uf8_strips,
                        file_path, file_sha256, file_bytes)
      VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
    `).run(
      pluginId, accountId, replacesId, envelope.surfaces, map.domain,
      envelope.author, envelope.description, envelope.licence || 'CC0-1.0',
      envelope.createdAt || ts, ts,
      coverage.n, coverage.d, coverage.uf8Vpots, coverage.uf8Strips,
      '', sha, bytes,
    );
    const mapId = Number(info.lastInsertRowid);

    // Write the blob under an id-derived path, then record it. Doing it inside
    // the transaction keeps a failed insert from leaving an orphan file
    // referenced by nothing.
    const rel = join(String(Math.floor(mapId / 1000)), `${mapId}.rea60map`);
    mkdirSync(join(MAPS_DIR, String(Math.floor(mapId / 1000))), { recursive: true });
    writeFileSync(join(MAPS_DIR, rel), text, 'utf8');
    db.prepare('UPDATE maps SET file_path = ? WHERE id = ?').run(rel, mapId);

    const ins = db.prepare(`
      INSERT INTO map_bindings (map_id, link_idx, domain, param_name, vst3_param, on_face, mod_layer)
      VALUES (?,?,?,?,?,?,?)
      ON CONFLICT(map_id, link_idx, mod_layer) DO NOTHING
    `);
    for (const b of bindings) {
      ins.run(mapId, b.linkIdx, b.domain, b.paramName, b.vst3Param, b.onFace ? 1 : 0, b.modLayer);
    }

    if (replacesId) {
      db.prepare('UPDATE maps SET published = 0, unpublished_at = ?, unpublish_note = ? WHERE id = ? AND account_id = ?')
        .run(ts, 'replaced by a newer upload', replacesId, accountId);
    }
    return { mapId, pluginId, coverage };
  });

  return tx();
}
