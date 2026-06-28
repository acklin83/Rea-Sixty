#pragma once

// DynaMount network client for Rea-Sixty's "DynaMount Mode".
//
// Talks to DynaMount robotic mic stands over the LAN. The protocol was
// reverse-engineered from the official DynaMount desktop app (Electron v2.0.24)
// and live-verified by packet capture against two Gen1 units — see the
// dynamount-sniffing repo (PROTOCOL.md) for the full notes.
//
// Gen1 (X1-R / X2-R / V1, Webduino firmware) — LIVE-VERIFIED:
//   HTTP/1.0 GET on port 80, single endpoint /move, plain-text, no auth:
//     GET /move?h=<0-100>&r=<0-180>&v=<0-100>&s=<speed>   -> {"result": "success"}
//   h = horizontal/distance, v = vertical/left-right, r = rotation,
//   s = motor speed (Frank's units run 9). roff = rotation offset, rst = reset cmd.
//   Open-loop: no position feedback — the controller owns the h/r/v state.
//
// Gen2 (Axial-2 / RIZR) — CODE-ONLY, NOT verified (no hardware yet):
//   raw TCP on port 5000, command "a<axial>f<focus>r<rot><speed>#". Scaffolded
//   here as an enum value + detection only; motion is added in a later phase.
//
// This unit has NO REAPER / libusb dependency. The query/request builders and
// the response check are pure logic (unit-tested in tests/test_dynamount.cpp);
// only the *send* functions touch sockets.

#include <string>
#include <cstdint>

namespace uf8::dynamount {

// Max mounts = UF8 strip count.
inline constexpr int kMaxMounts = 8;

// ---- Wire constants ----------------------------------------------------------
inline constexpr int kGen1HttpPort = 80;
inline constexpr int kGen2TcpPort  = 5000;   // STUDIO_MODEL_TCP_SERVER_PORT (5E3)

// Gen1 value ranges (from the app's GEN1_* gauge constants).
inline constexpr int kHMin = 0,   kHMax = 100;   // horizontal / distance
inline constexpr int kVMin = 0,   kVMax = 100;   // vertical / left-right
inline constexpr int kRMin = 0,   kRMax = 180;   // rotation (UI gauge), clamped
inline constexpr int kRoffMax = 359;             // rotation offset

// Default motor speed. Not user-exposed: the app sends a fixed speed and the
// observed value on Frank's units is 9.
inline constexpr int kDefaultSpeed = 9;

// Gen1 rst (reset / special command) values.
inline constexpr int kRstStandardCalibration = 1;
inline constexpr int kRstNewRotationOffset   = 2;  // inferred, confirm on hardware
inline constexpr int kRstBypassCalibration   = 3;
inline constexpr int kRstReturnToHome        = 4;

// Timeouts (ms).
inline constexpr int kHttpTimeoutMs  = 3000;
inline constexpr int kCalibTimeoutMs = 75000;  // GEN1_CALIBRATION_COMMAND_TIMEOUT

// ---- Detected protocol -------------------------------------------------------
enum class Proto : uint8_t { Unknown, Gen1Http, Gen2Tcp, Offline };

const char* protoName(Proto p);

// ---- Pure helpers (no I/O — unit-tested) -------------------------------------
int clampi(int v, int lo, int hi);

// Build the "h=..&r=..&v=..&s=.." query string, clamping each axis to range.
std::string gen1MoveQuery(int h, int r, int v, int s = kDefaultSpeed);

// Build the full raw HTTP/1.0 request line+headers for a /move?<query> GET.
// Webduino is HTTP/1.0 and closes the connection after the response.
std::string gen1Request(const std::string& ip, const std::string& query);

// True if a Gen1 device response body indicates success ({"result": "success"}).
bool isSuccessBody(const std::string& body);

// ---- Network result ----------------------------------------------------------
struct Result {
    bool        reachable = false;  // did we connect + get a response at all?
    int         status    = 0;      // HTTP status code (0 if none parsed)
    std::string body;               // response body (trimmed)
    bool        success   = false;  // reachable && isSuccessBody(body)
};

// ---- Gen1 commands (blocking, with connect+IO timeout) -----------------------
// Safe to call from any thread; intended to run on a dedicated worker, never on
// REAPER's main/timer thread (a dead IP must not stall the UI).
Result gen1SendQuery(const std::string& ip, const std::string& query,
                     int timeoutMs = kHttpTimeoutMs);

Result gen1Move(const std::string& ip, int h, int r, int v,
                int s = kDefaultSpeed, int timeoutMs = kHttpTimeoutMs);

Result gen1Home(const std::string& ip, int s = kDefaultSpeed,
                int timeoutMs = kHttpTimeoutMs);

Result gen1Calibrate(const std::string& ip, int s = kDefaultSpeed,
                     int timeoutMs = kCalibTimeoutMs);

Result gen1SetRotationOffset(const std::string& ip, int roff,
                             int s = kDefaultSpeed, int timeoutMs = kHttpTimeoutMs);

// ---- Detection ---------------------------------------------------------------
// Passive: just probe which port accepts a TCP connection (no motion command).
//   port 80 open  -> Gen1Http ;  port 5000 open -> Gen2Tcp ; else Offline.
Proto detectPassive(const std::string& ip, int timeoutMs = kHttpTimeoutMs);

// One-time process init for sockets (no-op on POSIX; WSAStartup on Windows).
// Call once at plugin load before any send; cleanup() at plugin quit.
void init();
void cleanup();

} // namespace uf8::dynamount
