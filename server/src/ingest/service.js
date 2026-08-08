// Turn a validated .rea60map into rows.
//
// Everything here runs inside one transaction per map: a half-ingested map
// (metadata but no bindings) would render as a legitimate-looking 0-coverage
// entry, which is the silent-wrong-number failure this project keeps hitting.

import { createHash } from 'node:crypto';
import { writeFileSync, mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { getDb, now, MAPS_DIR } from '../db/index.js';
import { parseRea60Map, IngestError } from '../lib/rea60map.js';
import {
  vendorFromName, normVendor, canonicalVendor, pluginNameFromMatch, normPlugin, slugify, fxTypeOf,
} from '../lib/vendor.js';

/**
 * Fingerprint the MAPPING — the set of bindings, not the file. Two uploads are
 * "identical" when they bind the same controls/slots to the same parameters,
 * regardless of author, description, timestamp or cached param NAMES (a stale
 * name doesn't change what the knob does). Sorted so key order never matters.
 */
export function mappingFingerprint(domain, bindings, uf8, uf1) {
  const slots = (bindings ?? [])
    .map((b) => `${b.linkIdx}:${b.vst3Param}:${b.modLayer}`).sort();
  const vpots = (uf8?.vpots ?? [])
    .map((v) => `${v.faderBank}:${v.vpotBank}:${v.strip}:${v.vst3Param}`).sort();
  const strips = (uf8?.strips ?? [])
    .map((s) => `${s.kind}:${s.faderBank}:${s.strip}:${s.vst3Param}`).sort();
  // UF1 positions (v11) MUST be in the hash. Without them two maps that are
  // identical on the UC1 but completely different on the UF1 fingerprint the
  // same, and the duplicate guard rejects the second with 409 "identical
  // mapping already exists" — which is precisely the map a UC1+UF1 owner wants
  // to publish. Appended as its own field so existing hashes are unchanged for
  // maps without a UF1 layer (an empty list yields the same canon as before).
  const uf1s = (uf1 ?? [])
    .map((u) => `${u.kind}:${u.pos}:${u.vst3Param}`).sort();
  const canon = [
    `domain=${domain}`,
    `slots=${slots.join(',')}`,
    `vpots=${vpots.join(',')}`,
    `strips=${strips.join(',')}`,
  ].join('|') + (uf1s.length ? `|uf1=${uf1s.join(',')}` : '');
  return createHash('sha256').update(canon, 'utf8').digest('hex');
}

/** Resolve or create a vendor, honouring the alias table. */
export function resolveVendor(db, rawName) {
  // Fold known aliases to one canonical name first ("UADx" / "Universal Audio
  // (UADx)" -> "Universal Audio"), so they land on one vendor row.
  const name = canonicalVendor(rawName);
  if (!name) return null;
  const norm = normVendor(name);

  // Dynamic aliases from admin vendor merges (mergeVendors) — the raw name's
  // norm may point at a canonical vendor even if it's not in the static table.
  const alias = db.prepare('SELECT vendor_id FROM vendor_aliases WHERE norm = ?')
    .get(normVendor(rawName));
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
export function ingestMap(text, { accountId, preferredPluginName = null, replacesId = null, replaceMine = false } = {}) {
  const parsed = parseRea60Map(text);
  const db = getDb();

  const { envelope, map, bindings, coverage, uf8, uf1, extFuncs, paramCoverage } = parsed;

  // Identity source, best-first: the v2 `original_name` is the full factory
  // name and carries the vendor; `match` is a user-editable substring that may
  // be shortened ("Pro-C 2") or truncated mid-vendor. Prefer original_name for
  // both the fx_type and the derived name/vendor; fall back to match on v1.
  const identityName = envelope.originalName || map.match;
  const fxType = fxTypeOf(identityName);
  // Envelope vendor wins if the user typed one (JSFX has no vendor in the
  // name); otherwise derive it from original_name, then match.
  const vendorName = envelope.vendor || vendorFromName(identityName, fxType);

  const sha = createHash('sha256').update(text, 'utf8').digest('hex');
  const bytes = Buffer.byteLength(text, 'utf8');
  const contentHash = mappingFingerprint(map.domain, bindings, uf8, uf1);

  const tx = db.transaction(() => {
    const vendorId = resolveVendor(db, vendorName);
    const pluginId = resolvePlugin(db, {
      // Derive the plug-in name from original_name when present — it is the
      // full factory name, so "SSL Native Channel Strip 2 (SSL)" beats the
      // shortened "match" the user may have typed. preferredName (upload form)
      // still wins over both.
      match: identityName, preferredName: preferredPluginName, vendorId, fxType,
    });

    // Opt-in auto-replace (`replaceMine`, the extension's ?replace_mine=1): the
    // in-app "Publish" has no map-picker, so it declares "this is an UPDATE of my
    // map for this plug-in" and the server finds the map to replace. Without it
    // the extension gets a 409 on an unchanged re-upload and spawns a duplicate on
    // a changed one. Gated on the flag ON PURPOSE — the seed and the diff feature
    // keep multiple maps per plug-in under one account (they don't send it); an
    // explicit ?replaces still wins over this.
    if (replacesId == null && replaceMine) {
      const mine = db.prepare(
        `SELECT id FROM maps
          WHERE plugin_id = ? AND account_id = ? AND published = 1
          ORDER BY id DESC LIMIT 1`,
      ).get(pluginId, accountId);
      if (mine) replacesId = mine.id;
    }

    // Reject a duplicate: an identical mapping of the SAME plug-in already
    // published (by anyone). A user replacing their own map is exempt for that
    // one map, so re-uploading an unchanged map as a replacement is a no-op,
    // not an error.
    const dupe = db.prepare(
      `SELECT id FROM maps
        WHERE plugin_id = ? AND content_hash = ? AND published = 1 AND id != ?`,
    ).get(pluginId, contentHash, replacesId ?? -1);
    if (dupe) {
      throw new IngestError('duplicate_mapping', 'identical mapping already exists');
    }

    const ts = now();
    const info = db.prepare(`
      INSERT INTO maps (plugin_id, account_id, replaces_id, surfaces, domain,
                        author_name, description, licence, created_at, uploaded_at,
                        coverage_n, coverage_d, uf8_vpots, uf8_strips,
                        param_cov_n, param_cov_d,
                        file_path, file_sha256, file_bytes, content_hash)
      VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
    `).run(
      pluginId, accountId, replacesId, envelope.surfaces, map.domain,
      envelope.author, envelope.description, envelope.licence || 'CC0-1.0',
      envelope.createdAt || ts, ts,
      coverage.n, coverage.d, coverage.uf8Vpots, coverage.uf8Strips,
      paramCoverage?.n ?? null, paramCoverage?.d ?? null,
      '', sha, bytes, contentHash,
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

    // UF8 grid bindings, one row per bound slot.
    const insU = db.prepare(`
      INSERT INTO uf8_slots (map_id, kind, fader_bank, vpot_bank, strip, label, param_name, vst3_param, mode)
      VALUES (?,?,?,?,?,?,?,?,?)
    `);
    for (const v of uf8?.vpots ?? []) {
      insU.run(mapId, 'vpot', v.faderBank, v.vpotBank, v.strip, v.label, v.paramName, v.vst3Param, v.mode);
    }
    for (const s of uf8?.strips ?? []) {
      insU.run(mapId, s.kind, s.faderBank, null, s.strip, s.label, s.paramName, s.vst3Param, null);
    }

    // UF1 plugin-mode positions (v11), one row per bound position. Persisted for
    // every domain — a UC1+UF1 map keeps its CS/BC domain.
    const insU1 = db.prepare(`
      INSERT INTO uf1_slots (map_id, kind, pos, page, idx, label, param_name, vst3_param, inverted)
      VALUES (?,?,?,?,?,?,?,?,?)
    `);
    for (const u of uf1 ?? []) {
      insU1.run(mapId, u.kind, u.pos, u.page, u.idx, u.label, u.paramName, u.vst3Param, u.inverted ? 1 : 0);
    }

    // UC1 EXT FUNCS — the hidden BACK-menu slots (CS mode). One row per curated
    // slot; empty slots were already dropped in extractExtFuncs.
    const insE = db.prepare(`
      INSERT INTO ext_funcs (map_id, slot, name, param_name, vst3_param)
      VALUES (?,?,?,?,?)
    `);
    for (const e of extFuncs ?? []) {
      insE.run(mapId, e.slot, e.name, e.param, e.vst3Param);
    }

    if (replacesId) {
      db.prepare('UPDATE maps SET published = 0, unpublished_at = ?, unpublish_note = ? WHERE id = ? AND account_id = ?')
        .run(ts, 'replaced by a newer upload', replacesId, accountId);
    }
    return { mapId, pluginId, coverage };
  });

  return tx();
}
