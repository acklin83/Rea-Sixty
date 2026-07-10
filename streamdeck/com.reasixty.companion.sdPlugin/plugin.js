// Rea-Sixty Companion — Stream Deck plugin (zero-dependency Node).
//
// Two connections:
//   1. Elgato Stream Deck app  — WebSocket (Node 22+ global WebSocket), the
//      standard plugin registration handshake. Receives key events, sends
//      titles/images.
//   2. Rea-Sixty extension bridge — raw TCP (net.Socket) to 127.0.0.1:49900,
//      newline-delimited JSON. keyDown -> {"cmd":"action"|"reaper"}; the bridge
//      pushes {"ev":"state"} which we mirror onto key titles.
//
// No npm dependencies, no build step. The Stream Deck app supplies the Node
// runtime declared in manifest.json ("Nodejs".Version = "24").

"use strict";

const net = require("net");
const fs  = require("fs");
const path = require("path");
// Node 20 (the Stream Deck manifest-mandated runtime) has no global WebSocket,
// so the Elgato connection uses the bundled `ws` package. The bridge side uses
// raw TCP (net) and needs no dependency.
const WebSocket = require("ws");

// ------------------------------------------------------------------ logging
const LOG = path.join(__dirname, "logs", "plugin.log");
try { fs.mkdirSync(path.join(__dirname, "logs"), { recursive: true }); } catch {}
function log(...a) {
  const line = "[" + new Date().toISOString() + "] " + a.join(" ") + "\n";
  try { fs.appendFileSync(LOG, line); } catch {}
}

// --------------------------------------------------------------- CLI args
function argVal(name) {
  const i = process.argv.indexOf(name);
  return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : null;
}
const sdPort         = argVal("-port");
const pluginUUID     = argVal("-pluginUUID");
const registerEvent  = argVal("-registerEvent");

log("boot", "port=" + sdPort, "uuid=" + pluginUUID, "regEvent=" + registerEvent,
    "node=" + process.version);

// ------------------------------------------------------- Stream Deck socket
let sd = null;
// context -> { settings, coordinates }
const contexts = new Map();
// Real Rea-Sixty built-in catalogue, fetched from the bridge on connect.
let builtins = [];               // [{ n: name, d: displayName, c: category }]
let builtinCats = [];            // ordered category names (picker order)
let pendingPi = null;            // { context, action } awaiting the list

function sdSend(obj) {
  if (sd && sd.readyState === 1) sd.send(JSON.stringify(obj));
}
function setTitle(ctx, title) {
  sdSend({ event: "setTitle", context: ctx, payload: { title: String(title), target: "both" } });
}

function connectStreamDeck() {
  sd = new WebSocket("ws://127.0.0.1:" + sdPort);
  sd.on("open", () => {
    log("SD open -> register");
    sdSend({ event: registerEvent, uuid: pluginUUID });
  });
  sd.on("message", (data) => {
    let m;
    try { m = JSON.parse(data.toString()); } catch { return; }
    onSdMessage(m);
  });
  sd.on("close", () => log("SD closed"));
  sd.on("error", (e) => log("SD error", e && e.message ? e.message : ""));
}

function onSdMessage(m) {
  switch (m.event) {
    case "willAppear":
      contexts.set(m.context, {
        action: m.action,
        settings: (m.payload && m.payload.settings) || {},
        coordinates: m.payload && m.payload.coordinates,
      });
      applyTitle(m.context);
      recomputeMeters();
      break;
    case "willDisappear":
      contexts.delete(m.context);
      recomputeMeters();
      break;
    case "didReceiveSettings": {
      const c = contexts.get(m.context) || {};
      c.settings = (m.payload && m.payload.settings) || {};
      contexts.set(m.context, c);
      applyTitle(m.context);
      recomputeMeters();
      break;
    }
    case "keyDown":
      onKeyDown(m);
      break;
    case "sendToPlugin": {
      // Property Inspector asking for data (the built-in catalogue).
      const p = m.payload || {};
      if (p.request === "builtins") {
        if (builtins.length) {
          sendBuiltinsToPi({ context: m.context, action: m.action });
        } else {
          pendingPi = { context: m.context, action: m.action };
          if (bridge && bridge.writable) bridge.write('{"cmd":"list"}\n');
        }
      }
      break;
    }
    default:
      break;
  }
}

function sendBuiltinsToPi(pi) {
  sdSend({
    event: "sendToPropertyInspector",
    context: pi.context,
    action: pi.action,
    payload: { event: "builtins", items: builtins, cats: builtinCats },
  });
}

// Title precedence: explicit label > live track name (if showTrack) > action id.
function isMeter(c) { return c && typeof c.action === "string" && c.action.endsWith(".meter"); }

function applyTitle(ctx) {
  const c = contexts.get(ctx);
  if (!c || isMeter(c)) return;   // meter keys render an image, not a title
  const s = c.settings || {};
  // Default key title = EMPTY (readability — Frank 2026-07-05). Only show text
  // when the user typed a fixed Title, or opted into the live track name.
  let t = "";
  if (s.showTrack && lastState && lastState.sel) {
    t = lastState.sel.name && lastState.sel.name.length
        ? lastState.sel.name
        : (lastState.sel.num > 0 ? "Trk " + lastState.sel.num : "");
  } else if (s.label) {
    t = s.label;
  }
  setTitle(ctx, t);
}

function onKeyDown(m) {
  const c = contexts.get(m.context) || {};
  const s = c.settings || (m.payload && m.payload.settings) || {};
  const rid = parseInt(s.reaperId, 10);
  let sent = false;
  if (s.reaperId && !isNaN(rid) && rid > 0) {
    sent = bridgeSend({ cmd: "reaper", id: rid });
  } else if (s.reaperAction) {
    sent = bridgeSend({ cmd: "reaper", action: String(s.reaperAction) });
  } else if (s.action) {
    const param = parseInt(s.param, 10);
    sent = bridgeSend({ cmd: "action", name: String(s.action), param: isNaN(param) ? 0 : param });
  } else {
    log("keyDown with no action configured for ctx", m.context);
  }
  // A meter key with no click action is display-only — no alert, and force a
  // redraw so a showOk tick doesn't linger (the dead-band would skip it).
  if (isMeter(c)) {
    c.meterSig = null;
    if (!sent) return;
  }
  sdSend({ event: sent ? "showOk" : "showAlert", context: m.context });
}

// ------------------------------------------------------------ bridge socket
const BRIDGE_HOST = "127.0.0.1";
const BRIDGE_PORT = 49900;
let bridge = null;
let bridgeBuf = "";
let lastState = null;
let reconnectTimer = null;
let isConnected = false;   // true between a successful connect and its close

function connectBridge() {
  bridge = net.createConnection({ host: BRIDGE_HOST, port: BRIDGE_PORT }, () => {
    log("bridge connected");
    isConnected = true;
    bridge.write('{"cmd":"subscribe"}\n');
    bridge.write('{"cmd":"list"}\n');   // warm the built-in catalogue cache
    meterTargetsSent = "";              // re-arm metering on the fresh bridge
    recomputeMeters();
  });
  bridge.setEncoding("utf8");
  bridge.on("data", (d) => {
    bridgeBuf += d;
    let i;
    while ((i = bridgeBuf.indexOf("\n")) >= 0) {
      const line = bridgeBuf.slice(0, i);
      bridgeBuf = bridgeBuf.slice(i + 1);
      if (line.trim().length) onBridgeLine(line);
    }
  });
  bridge.on("error", (e) => log("bridge error", e && e.code ? e.code : ""));
  bridge.on("close", () => {
    bridge = null;
    if (isConnected) { isConnected = false; onBridgeLost(); }
    if (!reconnectTimer) {
      reconnectTimer = setTimeout(() => { reconnectTimer = null; connectBridge(); }, 1500);
    }
  });
}

// REAPER quit / bridge dropped: clear the frozen meter images and stale titles
// so keys don't keep showing the last values. Frank 2026-07-07.
function onBridgeLost() {
  log("bridge lost — clearing meters");
  lastState = null;
  for (const [ctx, c] of contexts) {
    if (isMeter(c)) {
      c.meterSig = "__lost__";   // force a live re-render once the bridge is back
      c.m = {};                  // reset ballistics
      sdSend({ event: "setImage", context: ctx,
               payload: { image: meterOfflineSvg(), target: "hardware" } });
    } else {
      applyTitle(ctx);           // showTrack titles clear (lastState is null)
    }
  }
}

// Dim placeholder shown on a meter key while the bridge is unreachable.
function meterOfflineSvg() {
  const F = 'font-family="Helvetica,Arial,sans-serif"';
  const svg = '<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100" viewBox="0 0 100 100">' +
    '<rect width="100" height="100" fill="#141416"/>' +
    '<text x="50" y="52" ' + F + ' font-size="34" font-weight="700" fill="#555" text-anchor="middle">–</text>' +
    '<text x="50" y="74" ' + F + ' font-size="10" fill="#555" text-anchor="middle">no REAPER</text>' +
    '</svg>';
  return "data:image/svg+xml;charset=utf8;base64," + Buffer.from(svg).toString("base64");
}

function onBridgeLine(line) {
  let m;
  try { m = JSON.parse(line); } catch { return; }
  if (m.ev === "hello") {
    log("bridge hello proto", m.proto);
  } else if (m.ev === "builtins") {
    builtins = Array.isArray(m.items) ? m.items : [];
    builtinCats = Array.isArray(m.cats) ? m.cats : [];
    log("bridge builtins", builtins.length, "cats", builtinCats.length);
    if (pendingPi) { sendBuiltinsToPi(pendingPi); pendingPi = null; }
  } else if (m.ev === "state") {
    lastState = m;
    for (const ctx of contexts.keys()) applyTitle(ctx);
  } else if (m.ev === "meter") {
    onMeterPush(Array.isArray(m.t) ? m.t : []);
  }
}

// ------------------------------------------------------------ metering
let meterTargetsSent = "";

// The bridge target string for a meter key's settings.
function targetForCtx(c) {
  const s = c.settings || {};
  const mode = s.trackMode || "sel";
  if (mode === "master") return "num:0";               // 0 = master
  if (mode === "num" && s.trackNum) return "num:" + parseInt(s.trackNum, 10);
  if (mode === "name" && s.trackName) return "name:" + s.trackName;
  return "sel";
}

// Gather the unique set of targets across all meter keys and tell the bridge.
// Sends an empty list (→ bridge stops metering) when no meter keys exist.
function recomputeMeters() {
  const set = new Set();
  for (const c of contexts.values()) if (isMeter(c)) set.add(targetForCtx(c));
  const targets = [...set];
  const key = targets.slice().sort().join("|");
  if (key === meterTargetsSent) return;      // no change
  meterTargetsSent = key;
  bridgeSend({ cmd: "meters", targets });
}

// A meter push arrived: render each meter key from its target's data.
function onMeterPush(entries) {
  const byId = {};
  for (const e of entries) byId[e.id] = e;
  for (const [ctx, c] of contexts) {
    if (!isMeter(c)) continue;
    const e = byId[targetForCtx(c)];
    if (e) renderMeter(ctx, c, e);
  }
}

// Ballistic smoothing (instant attack, ~26.5 dB/s release) + whole-dB quantise
// with a hysteresis band, so a steady signal reads rock-steady — Frank
// 2026-07-05: constant -18 dB must not wobble. Per-component state on c.m.
function smoothQuant_(c, key, raw) {
  c.m = c.m || {};
  const st = c.m[key] || {};
  const REL = 1.8;                       // dB per ~15 Hz frame ≈ 26.5 dB/s
  let sm = st.sm;
  if (sm == null || raw >= sm) sm = raw;
  else sm = Math.max(raw, sm - REL);
  let q = st.q;
  if (q == null || Math.abs(sm - q) >= 0.6) q = Math.round(sm);
  c.m[key] = { sm, q };
  return q;
}
function peakBar_(c, rawDb) {
  const q = smoothQuant_(c, "peak", rawDb);
  return { frac: Math.max(0, Math.min(1, (q + 60) / 60)),
           db: q <= -150 ? "-∞" : String(q),
           color: q >= -6 ? "#e0402f" : (q >= -18 ? "#e0a020" : "#35c05a"),
           label: "PK" };
}
function grBar_(c, kind, rawGr) {
  const q = smoothQuant_(c, kind, rawGr);
  return { frac: Math.max(0, Math.min(1, q / 20)),
           db: q > 0 ? "-" + q : "0",
           color: kind === "comp" ? "#3fb0ff" : "#f0a030",
           topDown: true };   // GR is reduction → bar fills from the top down
}

function renderMeter(ctx, c, e) {
  const src = (c.settings && c.settings.source) || "peak";
  const wantPeak = src === "peak" || src === "peak_comp" || src === "peak_bc";
  const grKind = (src === "comp" || src === "peak_comp") ? "comp"
               : (src === "bc"   || src === "peak_bc")   ? "bc" : null;
  const bars = [];
  if (wantPeak) bars.push(peakBar_(c, Math.max(e.peak[0], e.peak[1])));
  if (grKind)   bars.push(grBar_(c, grKind, grKind === "comp" ? e.comp : e.bc));
  if (!bars.length) bars.push(peakBar_(c, Math.max(e.peak[0], e.peak[1])));

  // Track name comes resolved from the bridge (e.nm) so it works for ANY
  // track mode — selected, master, fixed number or name. Frank 2026-07-05.
  let name = "";
  if (c.settings && c.settings.showName) name = e.nm || "";

  // Optional: darkened track colour as the key background (readability keeps
  // the bars/text on top). [-1,…] from the bridge = no custom colour set.
  let bg = "#1c1c1f";
  if (c.settings && c.settings.followColor && Array.isArray(e.rgb) && e.rgb[0] >= 0) {
    const d = (v) => Math.max(0, Math.round(v * 0.42));
    bg = "rgb(" + d(e.rgb[0]) + "," + d(e.rgb[1]) + "," + d(e.rgb[2]) + ")";
  }

  // Font size for the generated readouts (dB values + track name). Stored as a
  // scale factor; default 1.
  const fontScale = parseFloat(c.settings && c.settings.fontScale) || 1;

  // Dead-band: only push a new image when the rendered result changes.
  const sig = bars.map((b) => b.db + b.color).join("/") + "|" + name + "|" + bg + "|" + fontScale;
  if (c.meterSig === sig) return;
  c.meterSig = sig;
  sdSend({ event: "setImage", context: ctx,
           payload: { image: meterSvg(bars, name, bg, fontScale), target: "hardware" } });
}

function xmlEsc_(s) {
  return String(s).replace(/[&<>"]/g,
    (ch) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[ch]));
}

// One or two vertical bars + dB readouts. The bottom line shows the track
// NAME (when enabled) instead of a "PK"/"GR" label — the source is obvious
// from the value, and the name is what the user actually wants there (Frank
// 2026-07-05). SVG data URI.
// fontScale (default 1) scales every generated text — the dB values, the "dB"
// label and the track name — so the user can make the readouts bigger/smaller
// (Property Inspector "Font size"). Base sizes are tuned for scale 1.
function meterSvg(bars, name, bg, fontScale) {
  const y0 = 20, H = 60, bx = [10, 30];
  const s = fontScale > 0 ? fontScale : 1;
  const fs = (base) => Math.max(6, Math.round(base * s));
  const F = 'font-family="Helvetica,Arial,sans-serif"';
  let g = '<rect width="100" height="100" fill="' + (bg || "#1c1c1f") + '"/>';
  bars.forEach((b, i) => {
    const barH = Math.round(H * b.frac);
    const fillY = b.topDown ? y0 : (y0 + H - barH);   // GR fills from the top
    g += '<rect x="' + bx[i] + '" y="' + y0 + '" width="14" height="' + H + '" rx="3" fill="#2e2e33"/>';
    g += '<rect x="' + bx[i] + '" y="' + fillY + '" width="14" height="' + barH + '" rx="3" fill="' + b.color + '"/>';
  });
  if (bars.length === 1) {
    g += '<text x="34" y="' + (y0 + 26) + '" ' + F + ' font-size="' + fs(24) + '" font-weight="700" fill="#eaeaea">' + bars[0].db + '</text>';
    g += '<text x="34" y="' + (y0 + 44) + '" ' + F + ' font-size="' + fs(12) + '" fill="#8a8a8a">dB</text>';
  } else {
    bars.forEach((b, i) => {
      g += '<text x="50" y="' + (y0 + 18 + i * 26) + '" ' + F + ' font-size="' + fs(18) + '" font-weight="700" fill="' + b.color + '">' + b.db + '</text>';
    });
  }
  if (name)
    g += '<text x="50" y="95" ' + F + ' font-size="' + fs(13) + '" font-weight="600" fill="#dcdcdc" text-anchor="middle">' + xmlEsc_(name.slice(0, 12)) + '</text>';
  return "data:image/svg+xml;charset=utf8;base64," +
    Buffer.from('<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100" viewBox="0 0 100 100">' + g + '</svg>').toString("base64");
}

function bridgeSend(obj) {
  if (bridge && !bridge.destroyed && bridge.writable) {
    bridge.write(JSON.stringify(obj) + "\n");
    return true;
  }
  return false;
}

// ----------------------------------------------------------------- go
connectStreamDeck();
connectBridge();
