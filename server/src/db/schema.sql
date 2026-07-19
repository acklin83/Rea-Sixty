-- api.reasixty.com — the Rea-Sixty mapping exchange.
--
-- SQLite plus files on disk, per docs/mapping-exchange-plan.md: the whole
-- corpus stays in the low single-digit megabytes for a long time, so this is
-- the right size of tool. The .rea60map blobs live on disk under DATA_DIR;
-- only their metadata and their per-control bindings live here.
--
-- WHY BINDINGS ARE A TABLE AND NOT JUST THE FILE
-- The plug-in page's whole reason to exist is a per-control DIFF across maps
-- (the two real Pro-C 2 maps both read "7 of 8, Bus Comp" and differ only in
-- one MIX binding — a summary that hides that actively misinforms). Serving
-- that from files would mean unescaping and parsing N payloads per request.
-- As rows it is one indexed query.

PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

-- ---------------------------------------------------------------- accounts

CREATE TABLE IF NOT EXISTS accounts (
  id            INTEGER PRIMARY KEY,
  display_name  TEXT    NOT NULL UNIQUE,   -- shown as the author; user-chosen
  created_at    INTEGER NOT NULL,
  is_admin      INTEGER NOT NULL DEFAULT 0,
  -- Rate limiting and the report queue are the actual spam defence; auth only
  -- raises the floor. A suspended account keeps its maps but cannot upload.
  suspended_at  INTEGER
);

-- One account = display name + N credentials. Passkey primary, verified email
-- an EQUAL fallback (Linux generally has no OS passkey store, and Linux is a
-- shipped platform — passkey-only would gate it behind a second device).
-- A user can add a passkey later and drop the email, or the reverse.
CREATE TABLE IF NOT EXISTS credentials (
  id             INTEGER PRIMARY KEY,
  account_id     INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  kind           TEXT    NOT NULL CHECK (kind IN ('passkey', 'email')),

  -- passkey (WebAuthn)
  webauthn_id    TEXT    UNIQUE,           -- base64url credential ID
  public_key     BLOB,
  sign_count     INTEGER NOT NULL DEFAULT 0,
  transports     TEXT,                     -- JSON array, e.g. ["internal"]
  label          TEXT,                     -- "MacBook Touch ID" — user-facing

  -- email (magic link). Never shown publicly; sign-in and recovery only.
  email          TEXT    UNIQUE,
  email_verified INTEGER NOT NULL DEFAULT 0,

  created_at     INTEGER NOT NULL,
  last_used_at   INTEGER,

  CHECK ((kind = 'passkey' AND webauthn_id IS NOT NULL AND public_key IS NOT NULL)
      OR (kind = 'email'   AND email IS NOT NULL))
);
CREATE INDEX IF NOT EXISTS idx_credentials_account ON credentials(account_id);

CREATE TABLE IF NOT EXISTS sessions (
  token       TEXT    PRIMARY KEY,          -- random, httpOnly cookie
  account_id  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  created_at  INTEGER NOT NULL,
  expires_at  INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_sessions_expiry ON sessions(expires_at);

-- Device tokens — the extension's credential. Browse and download are
-- anonymous; only UPLOAD needs a token, pasted once into the extension's
-- Exchange settings (the "CLI auth" pattern — no WebAuthn in ImGui, no
-- per-upload browser trip). Issued by the website after a passkey/magic-link
-- login, or by the mktoken CLI for the admin. We store only the SHA-256 of
-- the token, never the token itself, so a DB leak cannot be replayed.
CREATE TABLE IF NOT EXISTS account_tokens (
  id           INTEGER PRIMARY KEY,
  account_id   INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  token_sha256 TEXT    NOT NULL UNIQUE,
  label        TEXT,                          -- "MacBook", user-facing
  created_at   INTEGER NOT NULL,
  last_used_at INTEGER,
  revoked_at   INTEGER
);
CREATE INDEX IF NOT EXISTS idx_tokens_account ON account_tokens(account_id);

-- Single-use, short-lived. Covers both magic-link sign-in and the WebAuthn
-- challenge, which has the same shape: a nonce we minted and must see back.
CREATE TABLE IF NOT EXISTS auth_challenges (
  token       TEXT    PRIMARY KEY,
  kind        TEXT    NOT NULL CHECK (kind IN ('magic_link', 'webauthn_reg', 'webauthn_auth')),
  email       TEXT,
  account_id  INTEGER REFERENCES accounts(id) ON DELETE CASCADE,
  challenge   TEXT,
  created_at  INTEGER NOT NULL,
  expires_at  INTEGER NOT NULL,
  consumed_at INTEGER
);
CREATE INDEX IF NOT EXISTS idx_challenges_expiry ON auth_challenges(expires_at);

-- ----------------------------------------------------------------- vendors

-- The alias table is NOT hypothetical. One dev machine produces "Universal
-- Audio (UADx)" (72 plug-ins) and "UADx" (70) as separate vendors, and two AU
-- vendors ("Celemony ", "Synchro Arts ") carry a trailing space that counted
-- them twice until trimmed. TRIM FIRST, then alias.
CREATE TABLE IF NOT EXISTS vendors (
  id           INTEGER PRIMARY KEY,
  name         TEXT    NOT NULL UNIQUE,     -- canonical display name
  norm         TEXT    NOT NULL UNIQUE,     -- trimmed + casefolded match key
  created_at   INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS vendor_aliases (
  norm       TEXT    PRIMARY KEY,           -- normalised alias
  vendor_id  INTEGER NOT NULL REFERENCES vendors(id) ON DELETE CASCADE
);

-- ----------------------------------------------------------------- plugins

-- THE INDEX UNIT. /mappings lists one row per plug-in, not per map — listing
-- maps means "FabFilter Pro-Q 3" fourteen times down the page.
--
-- Identity is a real problem and cannot be solved from the file alone: the
-- envelope's `plugin` field mirrors the map's own `match`, which is a
-- USER-EDITABLE substring. The live 29-map catalog contains both
-- "FabFilter Pro-C 2" and "Pro-C 2" for the same plug-in, and
-- "UADx 1176 Rev A Compressor (Universal Audio" truncated mid-vendor.
-- So: `norm` auto-merges the easy cases, plugin_aliases absorbs the rest, and
-- the uploader confirms the plug-in on the upload form (typeahead over
-- existing rows) rather than the server guessing silently.
CREATE TABLE IF NOT EXISTS plugins (
  id          INTEGER PRIMARY KEY,
  slug        TEXT    NOT NULL UNIQUE,      -- URL: /mappings/plugin/<slug>
  name        TEXT    NOT NULL,             -- canonical display name
  norm        TEXT    NOT NULL UNIQUE,      -- match key
  vendor_id   INTEGER REFERENCES vendors(id) ON DELETE SET NULL,
  fx_type     TEXT,                         -- VST3 | VST | AU | CLAP | JS
  created_at  INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_plugins_vendor ON plugins(vendor_id);

CREATE TABLE IF NOT EXISTS plugin_aliases (
  norm       TEXT    PRIMARY KEY,
  plugin_id  INTEGER NOT NULL REFERENCES plugins(id) ON DELETE CASCADE
);

-- -------------------------------------------------------------------- maps

CREATE TABLE IF NOT EXISTS maps (
  id             INTEGER PRIMARY KEY,       -- THE SERVER MINTS THIS. The
                                            -- envelope carries no id and no
                                            -- revision; re-upload is an
                                            -- explicit "replaces my earlier
                                            -- map" step, never an auto-bump.
  plugin_id      INTEGER NOT NULL REFERENCES plugins(id) ON DELETE CASCADE,
  account_id     INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  replaces_id    INTEGER REFERENCES maps(id) ON DELETE SET NULL,

  -- From the envelope. `surfaces` is derived in the extension by surfaceScope()
  -- from the (domain, uf8Mode) pair; the server stores both so it can filter on
  -- the surface and still pick the right coverage denominator.
  surfaces       TEXT    NOT NULL CHECK (surfaces IN ('uc1', 'uf8', 'uc1+uf8')),
  domain         TEXT    NOT NULL CHECK (domain IN ('ChannelStrip', 'BusComp', 'None')),
  author_name    TEXT    NOT NULL,          -- as typed in the file
  description    TEXT    NOT NULL DEFAULT '',
  licence        TEXT    NOT NULL,          -- SPDX; CC0-1.0 for uploads
  created_at     INTEGER NOT NULL,          -- from the envelope
  uploaded_at    INTEGER NOT NULL,

  -- Coverage, computed at ingest. DOMAIN-SCOPED denominators: CS /33, BC /8,
  -- UF8-only /128 V-Pot slots. Scoring a complete BC map out of 41 would show
  -- good work as 20%.
  coverage_n     INTEGER NOT NULL,
  coverage_d     INTEGER NOT NULL,
  uf8_vpots      INTEGER NOT NULL DEFAULT 0,
  uf8_strips     INTEGER NOT NULL DEFAULT 0,

  file_path      TEXT    NOT NULL,          -- relative to DATA_DIR
  file_sha256    TEXT    NOT NULL,
  file_bytes     INTEGER NOT NULL,

  -- Fingerprint of the MAPPING itself (the bindings — which control/slot maps
  -- to which parameter), NOT the file bytes (those carry author, description
  -- and a timestamp, so two identical mappings would still differ). Used to
  -- reject a duplicate identical mapping of the same plug-in.
  content_hash   TEXT,

  published      INTEGER NOT NULL DEFAULT 1,
  unpublished_at INTEGER,
  unpublish_note TEXT,

  CHECK (coverage_n <= coverage_d)          -- the 130-of-128 guard, in the DB
);
CREATE INDEX IF NOT EXISTS idx_maps_plugin ON maps(plugin_id, published);
CREATE INDEX IF NOT EXISTS idx_maps_account ON maps(account_id);
CREATE INDEX IF NOT EXISTS idx_maps_content ON maps(plugin_id, content_hash);

-- One row per bound control. This is what the diff reads.
CREATE TABLE IF NOT EXISTS map_bindings (
  map_id      INTEGER NOT NULL REFERENCES maps(id) ON DELETE CASCADE,
  link_idx    INTEGER NOT NULL,
  -- linkIdx IS NAMESPACED PER DOMAIN: CS 1 is FaderLevel, BC 1 is Threshold.
  -- Carried per row so a join never has to reach back to maps.domain.
  domain      TEXT    NOT NULL,
  param_name  TEXT    NOT NULL,             -- resolved via paramSnapshot
  vst3_param  INTEGER NOT NULL,
  -- false => no UC1 control for this slot (Width, Pan, QA1-6, SAT, GRP…).
  -- Legitimate bindings. They render in an "also mapped" list and MUST NOT be
  -- dropped, or a thorough map looks sparse.
  on_face     INTEGER NOT NULL,
  mod_layer   TEXT    NOT NULL DEFAULT 'normal',
  PRIMARY KEY (map_id, link_idx, mod_layer)
);
CREATE INDEX IF NOT EXISTS idx_bindings_map ON map_bindings(map_id);

-- UF8 maps bind by grid coordinate, not linkIdx: 2 fader banks × 8 V-Pot banks
-- × 8 strips (V-Pot slots), plus 2 × 8 strip bindings (fader/solo/cut/sel).
-- One row per BOUND slot so the detail page can draw the full per-bank grid.
CREATE TABLE IF NOT EXISTS uf8_slots (
  map_id      INTEGER NOT NULL REFERENCES maps(id) ON DELETE CASCADE,
  kind        TEXT    NOT NULL,   -- 'vpot' | 'fader' | 'solo' | 'cut' | 'sel'
  fader_bank  INTEGER NOT NULL,   -- 0..1
  vpot_bank   INTEGER,            -- 0..7 for vpot; NULL for strip bindings
  strip       INTEGER NOT NULL,   -- 0..7
  label       TEXT,               -- the user's own V-Pot label
  param_name  TEXT,
  vst3_param  INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_uf8_map ON uf8_slots(map_id);

-- ------------------------------------------------------- rating + moderation

-- "Works for me" plus a confirmed plug-in version, NOT 5 stars. With few votes
-- per item a star average is noise; a version confirmation is the signal that
-- actually helps the next person.
CREATE TABLE IF NOT EXISTS works_for_me (
  map_id         INTEGER NOT NULL REFERENCES maps(id) ON DELETE CASCADE,
  account_id     INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  plugin_version TEXT,
  created_at     INTEGER NOT NULL,
  PRIMARY KEY (map_id, account_id)
);

CREATE TABLE IF NOT EXISTS reports (
  id           INTEGER PRIMARY KEY,
  map_id       INTEGER NOT NULL REFERENCES maps(id) ON DELETE CASCADE,
  account_id   INTEGER REFERENCES accounts(id) ON DELETE SET NULL,
  reason       TEXT    NOT NULL,
  created_at   INTEGER NOT NULL,
  resolved_at  INTEGER,
  resolution   TEXT
);
CREATE INDEX IF NOT EXISTS idx_reports_open ON reports(resolved_at);

-- Convenience view for /mappings: one row per plug-in, which is the unit the
-- index lists. Kept as a view so the row shape lives next to the schema.
CREATE VIEW IF NOT EXISTS plugin_index AS
SELECT
  p.id, p.slug, p.name, p.fx_type,
  v.name AS vendor,
  COUNT(m.id) AS map_count,
  MAX(CAST(m.coverage_n AS REAL) / m.coverage_d) AS best_coverage,
  (SELECT COUNT(*) FROM works_for_me w
     JOIN maps m2 ON m2.id = w.map_id
    WHERE m2.plugin_id = p.id) AS works_count,
  GROUP_CONCAT(DISTINCT m.surfaces) AS surfaces,
  MAX(m.uploaded_at) AS newest_at
FROM plugins p
LEFT JOIN vendors v ON v.id = p.vendor_id
LEFT JOIN maps m ON m.plugin_id = p.id AND m.published = 1
GROUP BY p.id;
