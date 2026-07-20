// Outbound mail — magic-link sign-in only. Nothing else on this site sends
// email, and nothing here is a mailing list.
//
// WHY IT DEGRADES TO THE LOG INSTEAD OF FAILING. Without SMTP credentials the
// transport logs the link and reports success, so the whole sign-in flow can be
// exercised locally with no mail server and no secrets on disk. That is a
// development convenience with one sharp edge, so it is loud: the server refuses
// to start in this mode unless MAIL_DEV_LOG is set explicitly (see server.js).
// A production box that quietly "sent" every login link to its own log would be
// a silent outage of the only way in.

import { createTransport } from 'nodemailer';

let transport;          // lazily built; undefined = not built yet, null = dev mode

export function mailConfigured() {
  return Boolean(process.env.SMTP_HOST && process.env.SMTP_USER && process.env.SMTP_PASS);
}

function getTransport(log) {
  if (transport !== undefined) return transport;

  if (!mailConfigured()) {
    transport = null;
    return transport;
  }

  const port = Number(process.env.SMTP_PORT ?? 465);
  transport = createTransport({
    host: process.env.SMTP_HOST,
    port,
    // Hostinger: 465 is implicit TLS, 587 is STARTTLS. `secure` means "TLS from
    // the first byte", so it tracks the port rather than being a separate knob
    // someone can set inconsistently.
    secure: port === 465,
    auth: { user: process.env.SMTP_USER, pass: process.env.SMTP_PASS },
  });
  log?.info({ host: process.env.SMTP_HOST, port }, 'mail: SMTP transport ready');
  return transport;
}

export function mailFrom() {
  return process.env.MAIL_FROM ?? 'Rea-Sixty <frank@reasixty.com>';
}

/**
 * Send a sign-in link. Returns { sent: true } on delivery, or
 * { sent: false, devLink } when running without SMTP so the caller can decide
 * whether to surface the link (it does so only outside production).
 */
export async function sendMagicLink({ to, url, log }) {
  const t = getTransport(log);

  if (!t) {
    log?.warn({ to, url }, 'mail: no SMTP configured — sign-in link NOT sent, logged instead');
    return { sent: false, devLink: url };
  }

  await t.sendMail({
    from: mailFrom(),
    to,
    subject: 'Your Rea-Sixty sign-in link',
    // Plain text only. An HTML mail from a project whose whole pitch is "no
    // telemetry" invites tracking-pixel questions it does not need to answer,
    // and a sign-in link is one sentence.
    text: [
      'Sign in to the Rea-Sixty mapping exchange:',
      '',
      url,
      '',
      'The link works once and expires in 15 minutes.',
      'If you did not ask for it, ignore this message — nothing happens until',
      'the link is opened.',
    ].join('\n'),
  });

  log?.info({ to }, 'mail: sign-in link sent');
  return { sent: true };
}

/** Test seam: forget the cached transport so a test can flip the env. */
export function resetMailTransport() {
  transport = undefined;
}
