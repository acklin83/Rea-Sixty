// Upgrading a REAL database to the UF1 surfaces.
//
// Re-running schema.sql does not update a CHECK on an existing table — SQLite
// keeps the constraint the table was created with. So a UF1 map inserted fine
// on a fresh test DB and would have died with SQLITE_CONSTRAINT_CHECK on the
// live one. This pins the rebuild: constraint widened, rows kept, and the
// constraint still enforced afterwards (a rebuild that quietly dropped it would
// pass a naive "does the insert work now" check).

import test from 'node:test';
import assert from 'node:assert/strict';
import { rmSync, mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

const DATA_DIR = join(tmpdir(), `rea60-uf1mig-${process.pid}`);
rmSync(DATA_DIR, { recursive: true, force: true });
mkdirSync(DATA_DIR, { recursive: true });
process.env.DATA_DIR = DATA_DIR;

const { migrate, getDb, closeDb } = await import('../src/db/index.js');

// The pre-UF1 `maps` table: the old surfaces CHECK plus the columns a row needs.
const OLD_MAPS = `CREATE TABLE maps (
  id INTEGER PRIMARY KEY, plugin_id INTEGER NOT NULL, account_id INTEGER NOT NULL,
  replaces_id INTEGER,
  surfaces TEXT NOT NULL CHECK (surfaces IN ('uc1', 'uf8', 'uc1+uf8')),
  domain TEXT NOT NULL CHECK (domain IN ('ChannelStrip','BusComp','None')),
  author_name TEXT NOT NULL, description TEXT NOT NULL DEFAULT '', licence TEXT NOT NULL,
  created_at INTEGER NOT NULL, uploaded_at INTEGER NOT NULL,
  coverage_n INTEGER NOT NULL, coverage_d INTEGER NOT NULL,
  uf8_vpots INTEGER NOT NULL DEFAULT 0, uf8_strips INTEGER NOT NULL DEFAULT 0,
  param_cov_n INTEGER, param_cov_d INTEGER,
  file_path TEXT NOT NULL, file_sha256 TEXT NOT NULL, file_bytes INTEGER NOT NULL,
  content_hash TEXT, published INTEGER NOT NULL DEFAULT 1,
  unpublished_at INTEGER, unpublish_note TEXT,
  CHECK (coverage_n <= coverage_d))`;

test('migrate() widens the surfaces CHECK on a pre-UF1 database', () => {
  closeDb();
  rmSync(DATA_DIR, { recursive: true, force: true });
  mkdirSync(DATA_DIR, { recursive: true });
  const d = getDb();
  d.exec(OLD_MAPS);
  d.prepare(`INSERT INTO maps (id,plugin_id,account_id,surfaces,domain,author_name,licence,
    created_at,uploaded_at,coverage_n,coverage_d,file_path,file_sha256,file_bytes)
    VALUES (1,1,1,'uc1','BusComp','frank','CC0-1.0',0,0,3,8,'0/1.rea60map','ab',10)`).run();

  // Before: the old constraint refuses a UF1 surface.
  assert.throws(() => d.prepare("UPDATE maps SET surfaces='uc1+uf1' WHERE id=1").run());

  migrate();

  // The row survived the table rebuild, values and all.
  const row = d.prepare('SELECT surfaces, author_name, coverage_d FROM maps WHERE id=1').get();
  assert.equal(row.author_name, 'frank');
  assert.equal(row.coverage_d, 8);
  assert.equal(d.prepare('SELECT COUNT(*) c FROM maps').get().c, 1);

  // After: the UF1 surfaces are accepted...
  d.prepare("UPDATE maps SET surfaces='uc1+uf8+uf1' WHERE id=1").run();
  assert.equal(d.prepare('SELECT surfaces FROM maps WHERE id=1').get().surfaces, 'uc1+uf8+uf1');
  // ...and the CHECK is still a CHECK, not silently dropped by the rebuild.
  assert.throws(() => d.prepare("UPDATE maps SET surfaces='garbage' WHERE id=1").run());

  // Idempotent: a second migrate() must not rebuild again or lose the row.
  migrate();
  assert.equal(d.prepare('SELECT COUNT(*) c FROM maps').get().c, 1);
  closeDb();
});
