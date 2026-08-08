import Database from 'better-sqlite3';
import { readFileSync, mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';

export const DATA_DIR = resolve(process.env.DATA_DIR ?? join(process.cwd(), 'var'));
export const DB_PATH = process.env.DB_PATH ?? join(DATA_DIR, 'exchange.db');
export const MAPS_DIR = join(DATA_DIR, 'maps');

let db;

export function getDb() {
  if (db) return db;
  mkdirSync(dirname(DB_PATH), { recursive: true });
  mkdirSync(MAPS_DIR, { recursive: true });
  db = new Database(DB_PATH);
  db.pragma('journal_mode = WAL');
  db.pragma('foreign_keys = ON');
  return db;
}

/**
 * Widen the `maps.surfaces` CHECK on a database created before the UF1 existed.
 *
 * Re-running schema.sql does NOT update a constraint on an existing table —
 * SQLite keeps whatever the table was created with — so a UF1 map would insert
 * fine on a fresh DB and die with SQLITE_CONSTRAINT_CHECK on a real one. New
 * TABLES arrive on their own via CREATE TABLE IF NOT EXISTS; changed CHECKs and
 * new COLUMNS do not.
 *
 * The rebuild is SQLite's standard procedure: create the table under a temp
 * name, copy, drop, rename. Foreign keys are off for the duration; the child
 * tables reference `maps(id)` by NAME, so they resolve again once the rename
 * puts the new table there. Guarded on the constraint text, so it runs once.
 */
function widenSurfacesCheck_(d) {
  const row = d.prepare(
    "SELECT sql FROM sqlite_master WHERE type = 'table' AND name = 'maps'",
  ).get();
  if (!row?.sql || row.sql.includes("'uc1+uf8+uf1'")) return;   // fresh or done

  const cols = d.prepare('PRAGMA table_info(maps)').all().map((c) => c.name).join(', ');
  const schema = readFileSync(new URL('./schema.sql', import.meta.url), 'utf8');
  const m = schema.match(/CREATE TABLE IF NOT EXISTS maps \(([\s\S]*?)\n\);/);
  if (!m) throw new Error('cannot locate the maps table definition in schema.sql');

  d.pragma('foreign_keys = OFF');
  try {
    d.transaction(() => {
      // Any VIEW over `maps` blocks the DROP ("error in view …: no such table").
      // They are all CREATE VIEW IF NOT EXISTS, so the schema re-run below puts
      // them back — dropping them here is the whole handling they need.
      for (const v of d.prepare("SELECT name FROM sqlite_master WHERE type = 'view'").all()) {
        d.exec(`DROP VIEW IF EXISTS ${v.name};`);
      }
      d.exec(`CREATE TABLE maps__new (${m[1]}\n);`);
      d.exec(`INSERT INTO maps__new (${cols}) SELECT ${cols} FROM maps;`);
      d.exec('DROP TABLE maps;');
      d.exec('ALTER TABLE maps__new RENAME TO maps;');
    })();
  } finally {
    d.pragma('foreign_keys = ON');
  }
  // The indexes lived on the dropped table; re-running the schema recreates
  // them (all CREATE INDEX IF NOT EXISTS).
  d.exec(schema);
}

export function migrate() {
  const d = getDb();
  const schema = readFileSync(new URL('./schema.sql', import.meta.url), 'utf8');
  d.exec(schema);
  widenSurfacesCheck_(d);
  return d;
}

/** Close the connection and drop the singleton — for graceful shutdown and
 *  for tests that recreate DATA_DIR between cases. */
export function closeDb() {
  if (db) {
    db.close();
    db = undefined;
  }
}

export function now() {
  return Math.floor(Date.now() / 1000);
}
