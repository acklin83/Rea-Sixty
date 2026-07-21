# api.reasixty.com — the mapping exchange.
#
# Runs as its own container on the Hostinger VPS, publishing to loopback only;
# the host's Caddy terminates TLS and is the single public listener. That is
# the pattern the newer services on that box already follow (ddl-website,
# listmonk) — do not expose 0.0.0.0 here.
#
# Two stages so the compiler that better-sqlite3 may need never ships in the
# running image.

FROM node:24-bookworm-slim AS deps

# better-sqlite3 is a native module. It publishes prebuilt binaries, but they
# do not cover every Node ABI — keeping a toolchain in THIS stage means a
# missing prebuild falls back to a source build instead of failing the deploy.
RUN apt-get update \
 && apt-get install -y --no-install-recommends python3 make g++ \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci --omit=dev


FROM node:24-bookworm-slim

ENV NODE_ENV=production
WORKDIR /app

COPY --from=deps /app/node_modules ./node_modules
COPY package.json ./
COPY src  ./src
# Committed config, not state: the generated control table and the vendor
# alias groups. src/lib/*.js resolves these relative to itself, so they must
# live at /app/data — which is NOT the /data volume the runtime state uses.
COPY data ./data

# The image carries no state. The SQLite database and the uploaded .rea60map
# blobs live under DATA_DIR, which docker-compose mounts from the host.
ENV DATA_DIR=/data

USER node
EXPOSE 8010
CMD ["node", "src/server.js"]
