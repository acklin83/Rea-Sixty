// Prove the SMTP credentials actually work.
//
//   docker compose exec -T exchange node src/tools/check-mail.js
//   docker compose exec -T exchange node src/tools/check-mail.js --send you@example.com
//
// WHY THIS IS NOT REDUNDANT with "mail: SMTP transport ready" in the log. That
// line only means credentials were present when the transport was constructed;
// nothing has talked to Hostpoint yet. SMTP authenticates on the first send, so
// a typo in the password stays invisible until the first person tries to sign
// in — and the symptom then is "nobody can create an account", days later.
//
// verify() opens the connection, upgrades to TLS and performs AUTH. It proves
// the password. --send additionally delivers a real message, which is the only
// way to see whether it arrives and passes SPF.
//
// Prints no secrets, on any path.

import { createTransport } from 'nodemailer';
import { mailConfigured, mailFrom } from '../lib/mail.js';

if (!mailConfigured()) {
  console.error('no SMTP credentials in the environment (SMTP_HOST, SMTP_USER, SMTP_PASS)');
  process.exit(2);
}

const port = Number(process.env.SMTP_PORT ?? 587);
const transport = createTransport({
  host: process.env.SMTP_HOST,
  port,
  secure: port === 465,
  requireTLS: port !== 465,
  auth: { user: process.env.SMTP_USER, pass: process.env.SMTP_PASS },
});

console.log(`host   ${process.env.SMTP_HOST}:${port} (${port === 465 ? 'implicit TLS' : 'STARTTLS, required'})`);
console.log(`user   ${process.env.SMTP_USER}`);
console.log(`from   ${mailFrom()}`);

try {
  await transport.verify();
  console.log('AUTH   OK — the server accepted these credentials');
} catch (err) {
  console.error(`AUTH   FAILED — ${err.message}`);
  process.exit(1);
}

const i = process.argv.indexOf('--send');
const to = i >= 0 ? process.argv[i + 1] : null;
if (to) {
  const info = await transport.sendMail({
    from: mailFrom(),
    to,
    subject: 'Rea-Sixty exchange — mail check',
    text: [
      'This is the mapping exchange checking that it can send mail.',
      '',
      'If you are reading it, sign-in links will arrive too. Check the headers',
      'for spf=pass on the sending domain.',
    ].join('\n'),
  });
  console.log(`SENT   ${info.messageId} -> ${to}`);
  if (info.rejected?.length) console.log(`REJECTED ${info.rejected.join(', ')}`);
}
