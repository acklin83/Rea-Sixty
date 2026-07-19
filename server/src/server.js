// Entry point. Behind the host Caddy on the VPS: Caddy terminates TLS for
// api.reasixty.com and reverse_proxies to localhost:PORT, so this binds
// loopback only (the newer-service pattern on that box — ddl-website, listmonk
// — never 0.0.0.0).

import { buildApp } from './app.js';
import { closeDb } from './db/index.js';

const PORT = Number(process.env.PORT ?? 8010);
const HOST = process.env.HOST ?? '127.0.0.1';

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
