// api.reasixty.com — the Fastify app factory.
//
// buildApp() returns an un-listened instance so tests can drive it with
// inject(); server.js calls listen(). The read surface (browse, facets,
// plug-in page, diff, map detail, download) is here and works against the
// seeded DB with no auth. Upload/rate/report/admin require accounts and land
// in a later routes module.

import Fastify from 'fastify';
import { migrate } from './db/index.js';
import { registerBrowseRoutes } from './routes/browse.js';
import { registerMapRoutes } from './routes/maps.js';

export function buildApp({ logger = false } = {}) {
  const app = Fastify({
    logger,
    bodyLimit: 3 * 1024 * 1024,        // envelope max is 2 MB; a little slack
  });

  migrate();

  app.get('/health', async () => ({ ok: true, service: 'reasixty-exchange' }));

  app.register(registerBrowseRoutes, { prefix: '/v1' });
  app.register(registerMapRoutes, { prefix: '/v1' });

  return app;
}
