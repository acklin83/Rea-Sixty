#pragma once
//
// SslCoreImpersonator — stand in for SSL 360°Core so SSL Meter (Pro) plugins
// stream their live meter data to US, letting the UF1 drive its Meter/Analyzer
// view (and CS/BC GR, EQ curve) without SSL 360° running.
//
// Protocol reverse-engineered 2026-07-10 and proven end-to-end with a live
// plugin (see analysis/ssl360-protobuf/, memory ssl360-plugin-protobuf-protocol).
// Flow: announce our TCP port to the discovery ports (16008/16009) → accept the
// plugin's TCP connection → send the Core opening handshake (ServerConfigMessage
// assigning a UDP data port) + periodic "server heartbeat" → receive
// PluginMeterDataMessage protobufs on the UDP data port → decode (SslMeterProtocol).
//
// Runs on its own worker thread; the public API is thread-safe. No REAPER API is
// touched from the worker — the main thread pulls snapshots for painting.

#include "SslMeterProtocol.h"

#include <cstdint>
#include <vector>
#include <string>

namespace sslcore {

// Start the impersonator. tcpPort = the TCP control port we bind + announce (0 =
// pick an ephemeral port and announce whatever the OS assigns). dataPort = the
// UDP port we tell the plugin to stream meter data to (we bind + listen on it).
// Returns false if a socket bind failed or it's already running.
bool start(uint16_t tcpPort = 0, uint16_t dataPort = 16010);
void stop();
bool isRunning();

// True while at least one plugin is TCP-connected.
bool pluginConnected();

// Select which meter view the plug-in should compute: 0 Overview, 1 Analogue,
// 2 RTA — same order as kUf1MeterScreens.
//
// This is NOT cosmetic, it decides what you are allowed to receive. The plug-in
// only computes the meters its selected view needs, and withholds the rest:
// on view 0 the Lissajous (t10) streams and the RTA (t8/t9) does not; on view 2
// it is the other way round. Proven 2026-07-15 — setting this to 2 is what made
// t8[31]/t9[31] appear after five days of hunting a "missing subscribe" that
// never existed. So the view MUST track whatever the UF1 is showing, or the
// screen the user is looking at is fed by a stream the plug-in isn't producing.
//
// Safe to call from any thread; takes effect on the worker's next pass.
void setView(int view);

// Copy the most recent values for `dataType` (a sslmeter::DataType) into out.
// Returns false if that type hasn't been seen yet. Thread-safe.
// Reads the SSL Meter (Pro) plug-in's stream specifically — a channel strip on
// the same track publishes into overlapping indices (see below).
// seq (optional): monotonic per-slot counter, bumped on every completed store.
// Compare against the last seen value to paint data-driven (once per plugin
// frame, like SSL) instead of timer-driven with stale repeats.
bool getMeter(int dataType, std::vector<float>& current, std::vector<float>& peak,
              uint64_t* seq = nullptr);

// Copy the overload flags for `dataType`: f5 OverloadValues (instantaneous — it
// flashes) and f6 OverloadInfHoldValues (latched until reset), one entry per
// channel. Returns false if that type hasn't been seen yet. Thread-safe.
// Only VuPpm(0) ever carries these — MEASURED (cap98): the same 0 dBFS clip sets
// overload on NO data type at all while the Overview view is selected, because
// the plug-in doesn't compute VuPpm there. Matches the property name the plug-in
// announces in clear text: AnalogueMetersLedOverload.
bool getOverload(int dataType, std::vector<uint8_t>& ovl, std::vector<uint8_t>& ovlHold);

// UF1 V-Pot1 instance selector — SESSION-WIDE, not per track. The plug-in
// streams one Meter instance per loaded plug-in (on ANY track); getMeter /
// getOverload normally AUTO-pick the live one. These let the surface override
// that: cycle a pin through the streaming instances, and the pinned one's data
// is what every meter/goniometer read returns. Thread-safe.
int  meterInstanceCount();            // Meter instances currently streaming
int  meterSelection();                // -1 = auto, else 0-based pinned ordinal
void cycleMeterSelection(int delta);  // advance the pin over [auto, 0..N-1], wrapping
void setAutoTrackIndex(int trackIndex1); // 1-based selected-track index; auto-mode follows it
void setTransportStopped(bool stopped);  // gates the frozen-at-stop level-meter blanking
// Track name of the instance currently being READ (the pin, or the auto-picked
// one). Empty if not yet correlated. For the UF1 V-Pot1 label.
std::string currentMeterName();
// 1-based HostTrackIndex of that same instance (0 = unknown), so V-Pot2/3/4 edit
// and read the instance V-Pot1 selected, not the focused track.
int currentMeterTrackIndex();

// True if the meter instance being read is an SSL Meter PRO — i.e. it has streamed
// a Loudness DataType (LoudMomentary=11 .. Histogram=27); a plain Meter never does.
// The UF1 uses this to include the Loudness screen in the Screen-Selector cycle only
// for Meter Pro (there is no PluginType on the wire — MEASURED absent). Thread-safe.
bool meterProAvailable();

// ChannelStripMeterType (AssignerArgsTypes.proto). An SSL channel strip numbers
// its meters with THIS vocabulary, not MeterPluginDataType — the two overlap on
// the wire and are told apart per plug-in instance (see SslCoreImpersonator.cpp).
enum class ChannelStripMeter : int {
    Input = 0, InputRms = 1, Output = 2, OutputRms = 3,
    CompGain = 4,          // compressor gain reduction, dB (0 = not reducing)
    GateGain = 5,          // gate attenuation, dB (0 = open, negative = closing)
    MicPreSaturation = 6,
};

// Copy the most recent channel-strip meter values for `csType` (a
// ChannelStripMeter) into `current`. Returns false if no channel-strip instance
// has published that type yet. Thread-safe.
//
// CompGain/GateGain are THE gain-reduction source: REAPER's GainReduction_dB
// named-config parm exposes only one number per FX and nothing at all for the
// gate. Verified on the wire 2026-07-14: GateGain swept 0 dB (open) to -42 dB
// (closed) with the ramp in between, CompGain tracked the compressor.
bool getChannelStripMeter(int csType, std::vector<float>& current);

// Like getChannelStripMeter, but only the channel-strip instances that announced
// the given 1-based REAPER track index (HostTrackIndex, correlated per UDP source
// port at connect). Returns false when no strip on that track has published —
// so the caller's GR goes dark rather than showing another channel's reduction.
// A track may run SEVERAL strips; this un-keyed form takes the FIRST instance
// on the track. Prefer getChannelStripMeterForTrackInstance whenever the caller
// knows which instance is active.
bool getChannelStripMeterForTrackIndex(int csType, int trackIndex,
                                       std::vector<float>& current);

// The same read, but for ONE named instance on the track: `instanceOrdinal` is
// the position of the plug-in among the track's SSL 360° plug-ins in FX-chain
// order (uf8::sslCoreInstanceOrdinal), which is the order they connect to Core
// in. Pass the ACTIVE instance's ordinal — the one Rea-Sixty rings in the MCP
// and drives the surfaces with — and the gate GR is that plug-in's own, not the
// other strip's. Returns false (→ meter dark) when that instance has no live
// stream, rather than falling back to a neighbour's reading. Thread-safe.
bool getChannelStripMeterForTrackInstance(int csType, int trackIndex,
                                          int instanceOrdinal,
                                          std::vector<float>& current);

// ★ PREFER THIS over the ordinal form. Same read, but it picks the strip by its
// MODEL — "4K E", "4K B", … — which is the only per-instance discriminator the
// protocol carries (the plug-in declares its EQ-curve object as "4KEEQCurveData"
// / "4KBEQCurveData" at connect; SlotIndex, PluginIdent, UniqueId and
// SessionDataId are identical across instances). The ordinal form assumes the
// track's UDP ports sort into FX-chain order, and a single plug-in reconnect
// breaks that — which is how a 4K B's gate reduction ended up on the 4K E
// (Frank, HW 2026-08-10). Pass the ACTIVE strip's short name; `instanceOrdinal`
// is the fallback used when the model is unknown or the track runs two strips of
// the SAME model, where the wire cannot tell them apart. Thread-safe.
bool getChannelStripMeterForTrackModel(int csType, int trackIndex,
                                       const char* model, int instanceOrdinal,
                                       std::vector<float>& current);

// Milliseconds since the last meter datagram of any kind (INT64_MAX if none).
long long msSinceLastData();

// Milliseconds since the most recent NEW plugin connection (INT64_MAX if none).
// A project load makes every plug-in connect in a burst — the surface uses this
// to freeze REAPER's track selection so the channels don't "count through" as
// each connecting plug-in selects its own track.
long long msSinceLastNewConn();

} // namespace sslcore
