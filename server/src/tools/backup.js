// Snapshot the exchange database to a file, safely, while the server is running.
//
//   node src/tools/backup.js /backups/exchange-20260720T120000Z.db
//
// WHY NOT `cp exchange.db`. The database runs in WAL mode (db/index.js sets
// `journal_mode = WAL`), so recent commits live in `exchange.db-wal` and not in
// the main file yet. Copying the main file alone silently loses them, and
// copying all three while a write is in flight can produce a torn snapshot.
// This uses SQLite's own online backup API via better-sqlite3's `db.backup()`,
// which is built for exactly this: it reads a consistent image of a database
// that is being written to, and produces a single self-contained file with no
// WAL sidecar to carry along.
//
// The output is a plain SQLite database. Restore is a copy back into place —
// see docs/deploy-exchange.md.

import { resolve } from 'node:path';
import { getDb, closeDb, DB_PATH } from '../db/index.js';

const dest = process.argv[2];
if (!dest) {
  console.error('usage: node src/tools/backup.js <destination.db>');
  process.exit(2);
}

// Deliberately NOT migrate() — a backup must never alter the schema of the
// thing it is backing up. getDb() only opens.
const db = getDb();

try {
  const result = await db.backup(resolve(dest));
  // `result` reports the pages copied; a zero-page backup means we pointed at
  // something that is not the live database, which is worth failing loudly on
  // rather than writing a plausible-looking empty file into the retention set.
  if (!result || !result.totalPages) {
    console.error(`backup produced no pages from ${DB_PATH} — refusing to call that a backup`);
    process.exit(1);
  }
  console.log(`${dest} (${result.totalPages} pages from ${DB_PATH})`);
} catch (err) {
  console.error(`backup failed: ${err.message}`);
  process.exit(1);
} finally {
  closeDb();
}
