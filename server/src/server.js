// Entry point. Behind the host Caddy on the VPS: Caddy terminates TLS for
// api.reasixty.com and reverse_proxies to localhost:PORT, so this binds
// loopback only (the newer-service pattern on that box — ddl-website, listmonk
// — never 0.0.0.0).

import { buildApp } from './app.js';
import { closeDb } from './db/index.js';
import { mailConfigured } from './lib/mail.js';

const PORT = Number(process.env.PORT ?? 8010);
const HOST = process.env.HOST ?? '127.0.0.1';

// Magic-link mail is one of only two ways into an account, and the other one
// (a passkey) cannot be enrolled without first getting in. So a box with no
// SMTP credentials has no working sign-up at all — and the failure is silent,
// because the transport politely logs the link and reports success.
//
// Refuse to start rather than serve a door that does not open. MAIL_DEV_LOG=1
// is the deliberate opt-out for local work, and has to be typed on purpose.
if (!mailConfigured() && process.env.MAIL_DEV_LOG !== '1') {
  console.error([
    'refusing to start: no SMTP credentials and MAIL_DEV_LOG is not set.',
    '',
    'Sign-in links would be written to the log instead of delivered, which',
    'means nobody can create an account — silently.',
    '',
    '  production : set SMTP_HOST, SMTP_USER, SMTP_PASS (and MAIL_FROM)',
    '  local      : MAIL_DEV_LOG=1 node src/server.js',
  ].join('\n'));
  process.exit(2);
}

const app = await buildApp({ logger: true });

app.listen({ port: PORT, host: HOST })
  .catch((err) => { app.log.error(err); process.exit(1); });

for (const sig of ['SIGINT', 'SIGTERM']) {
  process.on(sig, async () => {
    await app.close();
    closeDb();
    process.exit(0);
  });
}
