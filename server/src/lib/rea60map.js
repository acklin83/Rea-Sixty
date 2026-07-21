// Parse and validate a .rea60map file into the rows the exchange indexes.
//
// SECURITY RULE, and it is the whole reason this file is strict:
// the platform hosts SINGLE PLUGIN MAPS ONLY, NEVER .rea60config bundles.
// A UserPluginMap is pure data — ExtFuncEntry is {name, vst3Param}, a label
// and a param index (UserPluginCatalog.h:507). A .rea60config embeds
// bindings.json (SetupBundle.cpp:196-216), which CAN carry keyboard macros
// and REAPER action IDs, so importing one runs whatever the author put in it.
// That difference is what makes a map safe to pass between strangers.
//
// The extension's own bundle reader is lenient — SetupBundle.cpp:235 only
// validates `format` when the key is PRESENT. This server is strict in the
// other direction: exact match required, absence rejected.

import {
  lookupSlot, denominatorFor, UF8_VPOT_SLOTS, UF8_STRIPS,
} from './controls.js';

export const MAP_FORMAT = 'rea-sixty-map';
export const BUNDLE_FORMAT = 'rea-sixty-setup';   // rejected on sight
export const MAX_BYTES = 2 * 1024 * 1024;         // see the size table in the
                                                  // plan: pathological is
                                                  // ~195 KB, so 2 MB is slack.

export class IngestError extends Error {
  constructor(code, message) {
    super(message);
    this.code = code;
  }
}

/** Highest envelope version this server understands. */
export const MAX_VERSION = 3;

/**
 * Envelope keys, verified against serialize_ at UserPluginCatalog.cpp:1395.
 * `original_name` is v2, `functional_params` is v3 (both additive); older files
 * simply omit them and the server falls back.
 */
const ENVELOPE_KEYS = [
  'format', 'version', 'plugin', 'original_name', 'functional_params',
  'vendor', 'surfaces', 'author', 'description', 'licence', 'created_at', 'map',
];

const VALID_SURFACES = new Set(['uc1', 'uf8', 'uc1+uf8']);
const VALID_DOMAINS = new Set(['ChannelStrip', 'BusComp', 'None']);

/**
 * Count bound UF8 slots.
 *
 * TWO TRAPS, both hit while prototyping and both encoded here:
 *
 * 1. The ON-DISK keys are `banksByFaderBank` / `stripsByFaderBank`, NOT the
 *    `banks` / `strips` of the C++ struct (UserPluginCatalog.cpp:497). Reading
 *    the struct names yields zero and every UF8 map renders 0/0 — which looks
 *    like "nobody mapped anything" rather than like a bug.
 *
 * 2. Do NOT recurse looking for any `vst3Param`. Some slots carry nested
 *    structures (pushSteps, modLayers, travel) with their own vst3Param, and a
 *    recursive count returned 130 bound out of a possible 128. A slot is a
 *    slot: walk the fixed three-level shape and assert. An impossible number
 *    is what exposed the bug; on a map that happened to stay under the cap it
 *    would have shipped as a plausible wrong figure.
 */
export function uf8Coverage(map) {
  const u = map.uf8 ?? {};
  let vpots = 0;
  for (const faderBank of u.banksByFaderBank ?? []) {
    for (const vpotBank of faderBank ?? []) {
      for (const slot of vpotBank ?? []) {
        if (slot && typeof slot === 'object' && (slot.vst3Param ?? -1) >= 0) vpots++;
      }
    }
  }
  // Strips are NESTED on disk — {"fader":{"vst3Param":N}, "solo":{…}, …}
  // (UserPluginCatalog.cpp:567), NOT flat faderVst3Param keys. Reading the flat
  // names counted zero strips on every map — the same on-disk-vs-struct trap the
  // bank comment above warns about, fallen into one level down. Fixed 2026-07-19.
  let strips = 0;
  for (const faderBank of u.stripsByFaderBank ?? []) {
    for (const st of faderBank ?? []) {
      if (st && typeof st === 'object'
        && ['fader', 'solo', 'cut', 'sel'].some((k) => (st[k]?.vst3Param ?? -1) >= 0)) strips++;
    }
  }
  if (vpots > UF8_VPOT_SLOTS) {
    throw new IngestError('impossible_coverage',
      `${vpots} bound V-Pot slots exceeds the ${UF8_VPOT_SLOTS} that exist`);
  }
  if (strips > UF8_STRIPS) {
    throw new IngestError('impossible_coverage',
      `${strips} bound strips exceeds the ${UF8_STRIPS} that exist`);
  }
  return { vpots, strips };
}

/**
 * Extract every bound UF8 slot with its coordinates, so the detail page can
 * draw the full per-bank grid — the actual V-Pot → parameter mappings, not just
 * a count. Walks the fixed three-level shape (fader bank → V-Pot bank → strip)
 * and the nested strip bindings; resolves param names via paramSnapshot.
 *
 * Only bound slots are returned; the client fills an 8×8 grid and leaves the
 * rest blank. The `label` is the user's own V-Pot label ("InputGain"), stored
 * right on the slot — good for a compact cell; the param name is the tooltip.
 */
export function extractUf8(map) {
  const u = map.uf8 ?? {};
  const paramNames = new Map((map.paramSnapshot ?? []).map((p) => [p.vst3Param, p.name ?? '']));
  const nameOf = (v) => paramNames.get(v) ?? '';

  const vpots = [];
  (u.banksByFaderBank ?? []).forEach((faderBank, fb) => {
    (faderBank ?? []).forEach((vpotBank, vb) => {
      (vpotBank ?? []).forEach((slot, strip) => {
        const v = slot?.vst3Param ?? -1;
        if (v >= 0) {
          vpots.push({
            faderBank: fb, vpotBank: vb, strip,
            label: slot.label ?? '', vst3Param: v, paramName: nameOf(v),
            // How the V-Pot behaves: Value (turn), StepCycle (step), Toggle.
            mode: slot.vpotMode ?? 'Value',
          });
        }
      });
    });
  });

  const strips = [];
  (u.stripsByFaderBank ?? []).forEach((faderBank, fb) => {
    (faderBank ?? []).forEach((st, strip) => {
      for (const kind of ['fader', 'solo', 'cut', 'sel']) {
        const v = st?.[kind]?.vst3Param ?? -1;
        if (v >= 0) {
          strips.push({ faderBank: fb, strip, kind, label: st[kind].label ?? '', vst3Param: v, paramName: nameOf(v) });
        }
      }
    });
  });

  return { vpots, strips };
}

/**
 * Extract the curated UC1 EXT FUNCS — the hidden BACK-menu the user fills with
 * up to 10 plug-in params (CS mode). DECOUPLED from `slots` (an EXT FUNCS param
 * need not be on any physical control), so the bindings extractor never sees
 * them; without this they vanish from the exchange entirely, which is exactly
 * how they went missing until 2026-07-21. Each entry is `{name, vst3Param}`;
 * empty slots (vst3Param < 0) are skipped. Param name resolved via paramSnapshot,
 * `slot` kept so the grid order is stable.
 */
export function extractExtFuncs(map) {
  const paramNames = new Map(
    (map.paramSnapshot ?? []).map((p) => [p.vst3Param, p.name ?? '']),
  );
  const out = [];
  (map.extFuncs ?? []).forEach((e, i) => {
    const vp = e?.vst3Param ?? -1;
    if (!(vp >= 0)) return;
    out.push({ slot: i, name: e.name ?? '', vst3Param: vp, param: paramNames.get(vp) ?? '' });
  });
  return out;
}

/**
 * Extract one row per bound control.
 *
 * A linkIdx with no UC1 control (Width, Pan, Out Trim, QA1-6, SAT, GRP…) is a
 * PERFECTLY VALID binding and gets on_face=false rather than being dropped —
 * dropping them quietly is what would make a thorough map look sparse.
 */
export function extractBindings(map) {
  const domain = map.domain;
  const paramNames = new Map(
    (map.paramSnapshot ?? []).map((p) => [p.vst3Param, p.name ?? '']),
  );
  const rows = [];

  const push = (linkIdx, vst3Param, modLayer) => {
    if (!(vst3Param >= 0)) return;
    const slot = lookupSlot(domain, linkIdx);
    rows.push({
      linkIdx,
      domain,
      vst3Param,
      paramName: paramNames.get(vst3Param) ?? '',
      // No canonical slot at all (an ext:: synthetic the table omits) is still
      // off-face, not absent.
      onFace: slot?.on_face === true,
      slotName: slot?.name ?? null,
      section: slot?.section ?? null,
      modLayer,
    });
  };

  for (const s of map.slots ?? []) {
    if (typeof s?.linkIdx !== 'number') continue;
    push(s.linkIdx, s.vst3Param ?? -1, 'normal');
    // FX-Learn modifier overlays (v9, additive). A control mapped ONLY on a
    // modifier layer must look different from an unmapped one, or the Normal
    // view understates the map.
    for (const [key, layer] of Object.entries(s.modLayers ?? {})) {
      if (layer && typeof layer === 'object') push(s.linkIdx, layer.vst3Param ?? -1, key);
    }
  }
  return rows;
}

/** Coverage, domain-scoped. UF8-only maps use a different denominator entirely. */
export function computeCoverage(map, bindings) {
  const isUf8Only = map.domain === 'None';
  const { vpots, strips } = uf8Coverage(map);

  if (isUf8Only) {
    return { n: vpots, d: UF8_VPOT_SLOTS, uf8Vpots: vpots, uf8Strips: strips };
  }
  // Only Normal-layer, on-face bindings count toward the face denominator;
  // a modifier-layer binding on an already-counted control must not double it.
  const covered = new Set(
    bindings.filter((b) => b.onFace && b.modLayer === 'normal').map((b) => b.linkIdx),
  );
  const d = denominatorFor(map.domain);
  const n = covered.size;
  if (n > d) {
    throw new IngestError('impossible_coverage',
      `${n} covered controls exceeds the ${d} that exist for ${map.domain}`);
  }
  return { n, d, uf8Vpots: vpots, uf8Strips: strips };
}

/**
 * The single entry point. Takes raw file text, returns everything the DB needs,
 * or throws IngestError with a code the route can map to a status.
 */
export function parseRea60Map(text) {
  if (typeof text !== 'string' || text.length === 0) {
    throw new IngestError('empty', 'file is empty');
  }
  if (Buffer.byteLength(text, 'utf8') > MAX_BYTES) {
    throw new IngestError('too_large', `file exceeds ${MAX_BYTES} bytes`);
  }

  let root;
  try {
    root = JSON.parse(text);
  } catch {
    throw new IngestError('not_json', 'file is not valid JSON');
  }
  if (root === null || typeof root !== 'object' || Array.isArray(root)) {
    throw new IngestError('not_object', 'file is not a JSON object');
  }

  // --- THE SECURITY GATE ------------------------------------------------
  // Named rejection first, so a user who grabbed the wrong file gets told what
  // happened rather than a generic parse failure.
  if (root.format === BUNDLE_FORMAT) {
    throw new IngestError('bundle_rejected',
      'This is a .rea60config setup bundle, not a mapping. The exchange hosts '
      + 'single plug-in mappings only: a bundle embeds bindings.json, which can '
      + 'carry keyboard macros and REAPER action IDs, so importing one would run '
      + 'whatever its author put in it. Export the single mapping instead.');
  }
  if (root.format !== MAP_FORMAT) {
    throw new IngestError('wrong_format', 'not a Rea-Sixty mapping file');
  }
  // Accept v1 and v2. The envelope is deliberately forward-compatible — v2 only
  // adds `original_name` — so a stricter check than "known range" would reject
  // files needlessly. A version above what we know is the real error.
  if (!Number.isInteger(root.version) || root.version < 1 || root.version > MAX_VERSION) {
    throw new IngestError('wrong_version', `unsupported envelope version ${root.version}`);
  }

  const unknown = Object.keys(root).filter((k) => !ENVELOPE_KEYS.includes(k));
  if (unknown.length) {
    throw new IngestError('unknown_keys',
      `envelope carries unexpected keys: ${unknown.join(', ')}`);
  }

  // --- the payload ------------------------------------------------------
  // `map` is an ESCAPED JSON STRING, not a nested object — the envelope is
  // built so the server can index the top level without unescaping, but
  // coverage needs the payload, so we do parse it here.
  if (typeof root.map !== 'string' || root.map.length === 0) {
    throw new IngestError('no_payload', 'file carries no map payload');
  }
  let catalog;
  try {
    catalog = JSON.parse(root.map);
  } catch {
    throw new IngestError('bad_payload', 'map payload is not valid JSON');
  }

  const maps = catalog?.plugins;
  if (!Array.isArray(maps)) {
    throw new IngestError('bad_payload', 'map payload has no plugins array');
  }
  // parse_ silently drops entries with an empty match or the invalid
  // (domain=None, uf8Mode=false) pair, so a syntactically fine file can still
  // yield nothing. Check the count, do not assume it.
  if (maps.length !== 1) {
    throw new IngestError('not_single_map',
      `expected exactly one map, got ${maps.length}`);
  }
  const map = maps[0];

  if (!VALID_DOMAINS.has(map.domain)) {
    throw new IngestError('bad_domain', `unknown domain ${map.domain}`);
  }
  // (domain=None, uf8Mode=false) is the invalid pair the extension filters at
  // load/save — UserPluginCatalog.h:492-499.
  if (map.domain === 'None' && map.uf8Mode !== true) {
    throw new IngestError('bad_domain',
      'domain None with uf8Mode false is not a valid map');
  }
  if (!VALID_SURFACES.has(root.surfaces)) {
    throw new IngestError('bad_surfaces', `unknown surfaces value ${root.surfaces}`);
  }
  if (typeof map.match !== 'string' || map.match.trim() === '') {
    throw new IngestError('no_match', 'map carries no plug-in match string');
  }

  const bindings = extractBindings(map);
  const coverage = computeCoverage(map, bindings);
  // UF8 maps (domain None) carry their bindings in the V-Pot/strip grid, not
  // the `slots` array — extract them so the detail page can draw every bank.
  const uf8 = map.domain === 'None' ? extractUf8(map) : { vpots: [], strips: [] };
  // The curated UC1 EXT FUNCS (CS mode) — separate from `slots`, so extracted
  // on their own and counted toward parameter coverage below.
  const extFuncs = extractExtFuncs(map);

  // Parameter coverage: how many distinct plug-in params the map controls, out
  // of the plug-in's functional params (v3 envelope). The pruned paramSnapshot
  // can't give the denominator, so it rides in the envelope; null when absent.
  const mappedParams = new Set();
  for (const b of bindings) if (b.vst3Param >= 0) mappedParams.add(b.vst3Param);
  for (const v of uf8.vpots) if (v.vst3Param >= 0) mappedParams.add(v.vst3Param);
  for (const s of uf8.strips) if (s.vst3Param >= 0) mappedParams.add(s.vst3Param);
  for (const e of extFuncs) mappedParams.add(e.vst3Param);
  const functionalParams = Number.isInteger(root.functional_params) && root.functional_params > 0
    ? root.functional_params : null;
  const paramCoverage = functionalParams
    ? { n: Math.min(mappedParams.size, functionalParams), d: functionalParams }
    : null;

  return {
    envelope: {
      plugin: root.plugin ?? map.match,
      // The full factory name (v2). Empty on v1 files and on maps learned
      // before the extension captured it — the server falls back to `plugin`
      // for both vendor and identity in that case.
      originalName: (root.original_name ?? '').trim(),
      vendor: (root.vendor ?? '').trim(),
      surfaces: root.surfaces,
      author: (root.author ?? '').trim(),
      description: (root.description ?? '').trim(),
      licence: (root.licence ?? '').trim(),
      createdAt: Number(root.created_at) || 0,
    },
    map,
    domain: map.domain,
    bindings,
    uf8,
    extFuncs,
    coverage,
    paramCoverage,
  };
}
