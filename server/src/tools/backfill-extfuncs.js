// Backfill the ext_funcs table for maps ingested before EXT FUNCS were surfaced
// (2026-07-21). Re-parses each map's stored .rea60map file — the EXT FUNCS were
// never lost, only dropped at ingest, so the file still carries them — and
// populates ext_funcs. Idempotent: it deletes a map's rows before re-inserting,
// so re-running is safe. No re-upload needed.
//
//   docker compose exec exchange node src/tools/backfill-extfuncs.js
//
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { getDb, migrate, MAPS_DIR } from '../db/index.js';
import { parseRea60Map } from '../lib/rea60map.js';

migrate();                       // ensure the ext_funcs table exists
const db = getDb();

const maps = db.prepare(
  "SELECT id, file_path FROM maps WHERE file_path IS NOT NULL AND file_path != ''",
).all();

const del = db.prepare('DELETE FROM ext_funcs WHERE map_id = ?');
const ins = db.prepare(
  'INSERT INTO ext_funcs (map_id, slot, name, param_name, vst3_param) VALUES (?,?,?,?,?)',
);

let mapsWith = 0, rows = 0, skipped = 0;
for (const m of maps) {
  let parsed;
  try {
    parsed = parseRea60Map(readFileSync(join(MAPS_DIR, m.file_path), 'utf8'));
  } catch (e) {
    skipped++;
    console.warn(`  skip map ${m.id}: ${e.code ?? e.message}`);
    continue;
  }
  const ef = parsed.extFuncs ?? [];
  db.transaction(() => {
    del.run(m.id);
    for (const e of ef) ins.run(m.id, e.slot, e.name, e.param, e.vst3Param);
  })();
  if (ef.length) { mapsWith++; rows += ef.length; }
}

console.log(
  `backfill-extfuncs: ${maps.length} maps scanned, ${mapsWith} carry EXT FUNCS, `
  + `${rows} rows written${skipped ? `, ${skipped} skipped` : ''}.`,
);
