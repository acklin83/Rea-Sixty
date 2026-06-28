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
    std::atomic<int>  tgtH{50};
    std::atomic<int>  tgtR{90};
    std::atomic<int>  tgtV{0};
    std::atomic<bool> dirty{false};
    std::atomic<bool> online{false};
    std::atomic<Proto> proto{Proto::Unknown};

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
    // Normalized fader f in [0,1] → device units via calibration.
    void setDistance(int idx, double f01);    // → h
    void setHorizontal(int idx, double f01);   // → v
    void nudgeRotation(int idx, int deltaClicks); // → r (clamped 0..180)

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

} // namespace uf8::dynamount
