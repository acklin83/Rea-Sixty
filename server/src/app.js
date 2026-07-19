// api.reasixty.com — the Fastify app factory.
//
// buildApp() returns an un-listened instance so tests can drive it with
// inject(); server.js calls listen(). The read surface (browse, facets,
// plug-in page, diff, map detail, download) is here and works against the
// seeded DB with no auth. Upload/rate/report/admin require accounts and land
// in a later routes module.

import Fastify from 'fastify';
import rateLimit from '@fastify/rate-limit';
import { migrate } from './db/index.js';
import { registerBrowseRoutes } from './routes/browse.js';
import { registerMapRoutes } from './routes/maps.js';
import { registerUploadRoutes } from './routes/upload.js';

export async function buildApp({ logger = false } = {}) {
  const app = Fastify({
    logger,
    bodyLimit: 3 * 1024 * 1024,        // envelope max is 2 MB; a little slack
  });

  migrate();

  // A token is not a spam wall on its own — throwaway addresses and software
  // authenticators are cheap, and the moderation queue is the real defence.
  // But a per-IP cap keeps a single script from flooding the corpus.
  await app.register(rateLimit, {
    global: false,                     // opt in per route; reads must stay free
    max: 30,
    timeWindow: '1 minute',
  });

  app.get('/health', async () => ({ ok: true, service: 'reasixty-exchange' }));

  app.register(registerBrowseRoutes, { prefix: '/v1' });
  app.register(registerMapRoutes, { prefix: '/v1' });
  app.register(registerUploadRoutes, { prefix: '/v1' });

  return app;
}
