// WebAuthn relying-party configuration.
//
// The RP ID is the site's registered domain and NOTHING else — a passkey is
// bound to it, so getting it wrong does not fail loudly at registration, it
// fails later when the browser refuses to offer the credential. It is derived
// from SITE_ORIGIN so the two can never disagree; there is no separate knob to
// set inconsistently.
//
// The exchange API lives on api.reasixty.com but the passkey belongs to
// reasixty.com, which is why the website proxies /auth/* to this server on the
// SAME origin rather than the browser talking to the API host directly. A
// cross-origin WebAuthn call would be rejected by the browser before it ever
// reached us.

import { siteOrigin } from './session.js';

export function rpID() {
  if (process.env.RP_ID) return process.env.RP_ID;
  const host = new URL(siteOrigin()).hostname;
  return host;                       // 'localhost' in development
}

export function rpName() {
  return 'Rea-Sixty Mapping Exchange';
}

/** Origins the browser may present. An array because the dev site runs on a
 *  port and production does not, and a machine can legitimately be either. */
export function expectedOrigins() {
  const set = new Set([siteOrigin()]);
  if (process.env.EXTRA_ORIGIN) set.add(process.env.EXTRA_ORIGIN);
  return [...set];
}
