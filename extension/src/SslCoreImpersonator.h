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

// Copy the most recent values for `dataType` (a sslmeter::DataType) into out.
// Returns false if that type hasn't been seen yet. Thread-safe.
bool getMeter(int dataType, std::vector<float>& current, std::vector<float>& peak);

// Milliseconds since the last meter datagram of any kind (INT64_MAX if none).
long long msSinceLastData();

} // namespace sslcore
