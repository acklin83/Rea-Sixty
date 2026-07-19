// Mint a device token for an account, printed ONCE. Bootstraps the admin's
// in-app upload before the website's login exists to issue tokens to normal
// users.
//
//   node src/tools/mktoken.js --account Frank [--label MacBook]
//
// Paste the printed token into the extension's Exchange settings. It is stored
// only as a hash here; if lost, mint a new one.

import { migrate, getDb, now } from '../db/index.js';
import { issueToken } from '../lib/auth.js';

function arg(name, fallback = null) {
  const i = process.argv.indexOf(`--${name}`);
  return i >= 0 && process.argv[i + 1] ? process.argv[i + 1] : fallback;
}

const accountName = arg('account');
const label = arg('label', 'device');
if (!accountName) {
  console.error('usage: node src/tools/mktoken.js --account <display_name> [--label <text>]');
  process.exit(2);
}

migrate();
const db = getDb();
let acct = db.prepare('SELECT id FROM accounts WHERE display_name = ?').get(accountName);
if (!acct) {
  const id = db.prepare('INSERT INTO accounts (display_name, created_at, is_admin) VALUES (?,?,1)')
    .run(accountName, now()).lastInsertRowid;
  acct = { id: Number(id) };
  console.error(`(created admin account "${accountName}", id ${acct.id})`);
}

const { token } = issueToken(acct.id, label);
console.error(`token for "${accountName}" [${label}] — paste into the extension, shown once:\n`);
console.log(token);
