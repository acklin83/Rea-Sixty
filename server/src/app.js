// api.reasixty.com — the Fastify app factory.
//
// buildApp() returns an un-listened instance so tests can drive it with
// inject(); server.js calls listen(). Browse and download stay anonymous;
// everything that writes needs an account.
//
// TWO CREDENTIALS, ONE ACCOUNT MODEL. The extension sends a bearer device
// token (it cannot run WebAuthn inside an ImGui window); the website sends a
// session cookie. /auth, /account and /admin are the website's; /v1 is shared.
//
// THE WEBSITE PROXIES /auth AND /v1 ONTO ITS OWN ORIGIN rather than calling
// api.reasixty.com from the browser. That is not a style choice: a passkey is
// bound to the RP ID, and a cross-origin WebAuthn call is refused by the
// browser before it reaches any server. Same-origin also keeps the session
// cookie a plain SameSite=Lax one instead of a third-party cookie.

import Fastify from 'fastify';
import rateLimit from '@fastify/rate-limit';
import cookie from '@fastify/cookie';
import multipart from '@fastify/multipart';
import { migrate } from './db/index.js';
import { MAX_BYTES } from './lib/rea60map.js';
import { registerBrowseRoutes } from './routes/browse.js';
import { registerMapRoutes } from './routes/maps.js';
import { registerUploadRoutes } from './routes/upload.js';
import { registerAuthRoutes } from './routes/auth.js';
import { registerAccountRoutes } from './routes/account.js';
import { registerAdminRoutes } from './routes/admin.js';
import { registerCommunityRoutes } from './routes/community.js';

/**
 * @param rateLimits  Set false ONLY in tests. Every request in a test arrives
 *   from the same (absent) IP, so a suite that signs in six times trips the
 *   sign-in cap and starts failing for a reason that has nothing to do with
 *   what it is testing. Skipping the plugin makes the per-route `config
 *   .rateLimit` blocks inert metadata. One test builds an app WITH limits on
 *   and proves the cap actually fires, so turning it off here does not leave
 *   the limits unverified.
 */
export async function buildApp({ logger = false, rateLimits = true } = {}) {
  const app = Fastify({
    logger,
    bodyLimit: 3 * 1024 * 1024,        // envelope max is 2 MB; a little slack
  });

  migrate();

  // A token is not a spam wall on its own — throwaway addresses and software
  // authenticators are cheap, and the moderation queue is the real defence.
  // But a per-IP cap keeps a single script from flooding the corpus.
  if (rateLimits) {
    await app.register(rateLimit, {
      global: false,                   // opt in per route; reads must stay free
      max: 30,
      timeWindow: '1 minute',
    });
  }

  // Session cookies. No secret: the cookie holds an opaque random token that
  // is looked up in the sessions table, so there is nothing to sign — a signed
  // cookie here would only add a key to lose.
  await app.register(cookie);

  // Browser uploads. The extension's raw-bytes path has its own content-type
  // parser inside the upload plugin; this is for the web form.
  await app.register(multipart, {
    limits: { fileSize: MAX_BYTES, files: 1, fields: 8 },
  });

  app.get('/health', async () => ({ ok: true, service: 'reasixty-exchange' }));

  app.register(registerBrowseRoutes, { prefix: '/v1' });
  app.register(registerMapRoutes, { prefix: '/v1' });
  app.register(registerUploadRoutes, { prefix: '/v1' });
  app.register(registerCommunityRoutes, { prefix: '/v1' });

  app.register(registerAuthRoutes, { prefix: '/auth' });
  // Under /v1, NOT /account and /admin. The website has PAGES at those paths,
  // and the browser reaches this API through the same origin — so a bare
  // /account here would collide with the account page and one of the two would
  // shadow the other depending on proxy-rule order. One API namespace, and the
  // page namespace stays the site's.
  app.register(registerAccountRoutes, { prefix: '/v1/account' });
  app.register(registerAdminRoutes, { prefix: '/v1/admin' });

  return app;
}
