#pragma once

// DynaMountManager — owns the up-to-8 mount states and a single background
// worker thread that pushes position updates to the devices.
//
// Threading contract:
//   * The REAPER main/timer thread only ever calls the cheap, non-blocking
//     setters (setTargetH/V, nudgeR) and reads target*/online for feedback.
//   * The worker thread does ALL socket I/O (DynaMountClient), so a slow or
//     dead device never stalls the UI. Targets are atomics; the worker reads
//     the *latest* value each pass, so a continuous fader drag coalesces into
//     one in-flight request per mount carrying the most recent position.
//   * Config (name/ip/color/enabled/calibration/fill) is edited from Settings
//     on the main thread and guarded by a mutex; the worker takes brief copies.
//
// See dynamount-sniffing/REA-SIXTY-INTEGRATION.md for the full design.

#include "DynaMountClient.h"

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace uf8::dynamount {

// Per-mount calibration: maps a normalized fader 0..1 onto the device's usable
// travel, plus a rotation zero offset. Captured via the Settings wizard.
struct Calibration {
    int  hMin = kHMin, hMax = kHMax;   // distance travel
    int  vMin = kVMin, vMax = kVMax;   // left-right travel
    int  rOffset = 0;                  // rotation zero (0..359)
    bool valid = false;
};

// Which side of the surface the pinned DynaMount strips anchor to.
enum class FillDir : uint8_t { Left, Right };

struct Mount {
    // --- config (guarded by DynaMountManager::cfgMx_) -----------------------
    bool        enabled = false;
    std::string name;
    std::string ip;
    int         color = 0;             // palette index → colorbar
    Calibration cal;

    // --- live (atomic): main thread writes targets, worker reads ------------
    // Defaults = the home pose so a never-homed mount reads X50 Y0 R90:
    // tgtH = distance (Y) 0 = nearest, tgtV = left/right (X) 50 = centre.
    std::atomic<int>  tgtH{0};
    std::atomic<int>  tgtR{90};
    std::atomic<int>  tgtV{50};
    std::atomic<bool> dirty{false};
    std::atomic<bool> online{false};
    std::atomic<Proto> proto{Proto::Unknown};
    // Rotation debounce: nudgeRotation stamps this (steady-clock ms) instead of
    // setting dirty; the worker only commits the rotation after 1s of no
    // further turning, so spinning the V-Pot doesn't stream commands. 0 = none.
    std::atomic<int64_t> rotPendingMs{0};

    // --- worker-only bookkeeping --------------------------------------------
    int sentH = -1, sentR = -1, sentV = -1;
};

class DynaMountManager {
public:
    DynaMountManager() = default;
    ~DynaMountManager();

    DynaMountManager(const DynaMountManager&)            = delete;
    DynaMountManager& operator=(const DynaMountManager&) = delete;

    // Worker lifecycle.
    void start();
    void stop();

    // ---- Config (main thread) ----------------------------------------------
    void setMount(int idx, bool enabled, std::string name, std::string ip, int color);
    void setCalibration(int idx, const Calibration& cal);
    void setFillDir(FillDir d) { fill_.store(d); }
    FillDir fillDir() const    { return fill_.load(); }

    // Number of enabled mounts (= number of pinned DynaMount strips).
    int definedCount();
    // Cheap per-index enabled check (no string copy, unlike info()).
    bool mountEnabled(int idx);

    // Snapshot of a mount's config fields for the Settings UI / feedback.
    struct Info { bool enabled; std::string name, ip; int color; Proto proto; bool online; Calibration cal; };
    Info info(int idx);

    // ---- Strip <-> mount mapping (depends on definedCount + fillDir) --------
    // The k-th enabled mount maps to strip k (Left) or 8-N+k (Right).
    // Returns the mount index controlling a UF8 strip, or -1 if that strip is
    // a normal track strip.
    int  mountForStrip(int strip);
    bool isDynaStrip(int strip) { return mountForStrip(strip) >= 0; }

    // ---- Control (main thread, cheap) --------------------------------------
    // Normalized fader f in [0,1] → device units via calibration. The distance
    // (Y) fader is INVERTED: f=1 (fader top) = nearest = device h-min, so the
    // user pulls up to bring the mic closer (Frank 2026-06-28).
    void setDistance(int idx, double f01);    // → h (Y), inverted
    void setHorizontal(int idx, double f01);   // → v (X)
    void nudgeRotation(int idx, int deltaClicks); // → r (R, clamped 0..180)

    // Set absolute device targets directly (h/r/v). markDirty=true queues a
    // send to the mount; false just updates our state (e.g. restoring the
    // persisted position on load — the mount already physically holds it).
    void setTargets(int idx, int h, int r, int v, bool markDirty);
    // Calibration home: drive (or assume) the mount at X(v)=50 centre, Y(h)=0
    // nearest, R(r)=90 straight. The DynaMount nomenclature reference pose.
    void home(int idx) { setTargets(idx, /*h*/0, /*r*/90, /*v*/50, true); }

    // ---- Feedback reads (main thread) --------------------------------------
    int  targetH(int idx);
    int  targetR(int idx);
    int  targetV(int idx);
    // Normalized 0..1 of the active axis, for driving the motor fader.
    double faderNorm(int idx, bool flipped);
    bool online(int idx);
    Proto proto(int idx);

    // ---- Detection (async) -------------------------------------------------
    // Ask the worker to probe this mount's IP; result lands in proto + online.
    void requestDetect(int idx);

    // ---- Persistence (pure; REAPER-free, unit-tested) ----------------------
    // Compact tab/newline-delimited form of the mount config + calibration.
    // Fill direction is persisted separately by the caller.
    std::string serialize();
    void deserialize(const std::string& s);

private:
    void   workerLoop();
    int    mapFader(const Calibration& c, double f01, bool horizontal);
    std::vector<int> enabledOrder();  // config indices of enabled mounts, in slot order

    std::array<Mount, kMaxMounts> mounts_;
    std::mutex          cfgMx_;
    std::atomic<FillDir> fill_{FillDir::Left};
    std::atomic<uint32_t> detectReq_{0};  // bitmask of mounts to detect
    std::thread         worker_;
    std::atomic<bool>   run_{false};
};

// Process-wide manager singleton. First access lazily inits sockets and starts
// the worker thread. Shared by the Settings UI and the (future) UF8 mode code.
DynaMountManager& manager();

} // namespace uf8::dynamount
