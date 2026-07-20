// Grant (or revoke) moderator rights on an existing account.
//
//   node src/tools/mkadmin.js --email frank@reasixty.com
//   node src/tools/mkadmin.js --name "Frank A" --revoke
//
// WHY THIS EXISTS. Signing in creates a NEW account keyed on the address — it
// has no way to know it is you. So the seed's admin row and the account you
// actually log in with are two different rows, and without this the moderation
// console is unreachable by anyone, including its owner. Found by signing in
// as frank@reasixty.com on a seeded database and landing on a plain account.
//
// Deliberately a CLI: promoting an account is a shell-on-the-box decision, and
// an admin who can appoint other admins over the web is a much larger blast
// radius than this site needs.

import { migrate, getDb } from '../db/index.js';

function arg(name, fallback = null) {
  const i = process.argv.indexOf(`--${name}`);
  return i >= 0 && process.argv[i + 1] ? process.argv[i + 1] : fallback;
}

const email = arg('email');
const name = arg('name');
const revoke = process.argv.includes('--revoke');

if (!email && !name) {
  console.error('usage: node src/tools/mkadmin.js (--email <address> | --name <display name>) [--revoke]');
  process.exit(2);
}

migrate();
const db = getDb();

const row = email
  ? db.prepare(
      `SELECT a.id, a.display_name, a.is_admin FROM accounts a
         JOIN credentials c ON c.account_id = a.id
        WHERE c.kind = 'email' AND c.email = ?`,
    ).get(email.trim().toLowerCase())
  : db.prepare('SELECT id, display_name, is_admin FROM accounts WHERE display_name = ?').get(name);

if (!row) {
  console.error(
    email
      ? `no account signs in with ${email} — sign in once on the website first, then run this`
      : `no account is called "${name}"`,
  );
  process.exit(1);
}

db.prepare('UPDATE accounts SET is_admin = ? WHERE id = ?').run(revoke ? 0 : 1, row.id);
console.log(
  `${revoke ? 'revoked moderator rights on' : 'granted moderator rights to'} "${row.display_name}" (id ${row.id})`,
);
