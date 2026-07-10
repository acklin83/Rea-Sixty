#pragma once
//
// StreamDeckBridge — Phase 1 spike of the "Rea-Sixty Companion" bridge.
//
// A loopback-only TCP server (127.0.0.1) inside the Rea-Sixty extension that
// an Elgato Stream Deck plugin (separate Node process) connects to. The point
// of the bridge over plain OSC / Web-Remote is that it is BIDIRECTIONAL: the
// Stream Deck sends action requests AND the extension pushes live surface
// state back so the LCD keys can mirror modes / selection / meters.
//
// Wire protocol: newline-delimited JSON (NDJSON). One JSON object per line,
// terminated by '\n'. Loopback only, no auth (localhost trust boundary).
//
//   Client -> Bridge:
//     {"cmd":"hello","name":"streamdeck"}      handshake (optional)
//     {"cmd":"action","name":"flip","param":0} fire a Rea-Sixty builtin
//     {"cmd":"reaper","id":40044}              fire a REAPER command id
//     {"cmd":"reaper","action":"_SWS_ABOUT"}   fire a named REAPER command
//     {"cmd":"subscribe"}                      start receiving state pushes
//     {"cmd":"ping"}
//
//   Bridge -> Client:
//     {"ev":"hello","app":"Rea-Sixty","proto":1}
//     {"ev":"state", ...}                      selection / mode deltas
//     {"ev":"pong"}
//
// THREADING: this module owns a background accept/read thread. That thread
// NEVER touches the REAPER API — it only parses lines and enqueues SdCommand
// records. main.cpp drains them on the main thread (onTimer) via
// drainCommands() and executes them there, mirroring the queueInput /
// drainInputQueue pattern used for the USB input thread. State pushes are
// produced on the main thread and handed to sendLine()/broadcast() which is
// the only thing the worker and main thread share besides the queue (both
// guarded by a mutex).
//
// This TU has ZERO REAPER-API dependencies so it can be unit-tested headless
// (see tests/test_sdbridge.cpp): connect a socket, send a command line, assert
// drainCommands() returns it; broadcast a line, assert the client receives it.
//

#include <cstdint>
#include <string>
#include <vector>

namespace uf8::sdbridge {

// Default loopback port. In the IANA dynamic/private range, unlikely to clash.
constexpr int kDefaultPort = 49900;

// A parsed inbound command. Kept REAPER-agnostic — main.cpp maps it onto the
// bindings registry / Main_OnCommand.
struct SdCommand {
    enum class Kind : uint8_t { Unknown, Hello, Action, ReaperId, ReaperName,
                                Subscribe, Ping, List, Meters };
    Kind        kind  = Kind::Unknown;
    std::string name;          // Action builtin name / ReaperName command / Hello client
    int         param = 0;     // Action param
    int         id    = 0;     // ReaperId command id
    // Meters: the set of track targets the client wants metered. Each is
    // "sel" | "num:<N>" | "name:<track name>". Empty = stop metering.
    std::vector<std::string> targets;
};

// Start the server on `port`. Idempotent — a second call while already running
// is a no-op. Returns false if the socket could not be bound (e.g. port already
// in use); the extension keeps running regardless.
//
// bindAll = false (default): bind 127.0.0.1 — loopback only, the secure default
//   for a same-machine Stream-Deck / Companion client. No auth (localhost trust
//   boundary).
// bindAll = true: bind 0.0.0.0 — reachable from the LAN, so Bitfocus Companion
//   (or Companion Satellite) running on a SEPARATE box can connect. There is
//   STILL no auth on the wire, so this must be opt-in (ExtState) and only used
//   on a trusted network. See main.cpp REAPER_PLUGIN_ENTRY.
bool start(int port = kDefaultPort, bool bindAll = false);

// Stop the server: close the listen socket, drop all clients, join the worker
// thread. Safe to call when not running. Called from the extension's unload
// path.
void stop();

bool isRunning();

// Number of currently-connected clients (diagnostics / settings readout).
int clientCount();

// MAIN THREAD ONLY. Move all commands received since the last call into `out`
// (cleared first). Non-blocking.
void drainCommands(std::vector<SdCommand>& out);

// MAIN THREAD (or any thread). Append '\n' and send `jsonLine` to every
// connected, subscribed client. A dead client is dropped. `jsonLine` must NOT
// already contain a trailing newline.
void broadcast(const std::string& jsonLine);

} // namespace uf8::sdbridge
