#include "DynaMountManager.h"

#include <algorithm>
#include <chrono>

namespace uf8::dynamount {

using clock = std::chrono::steady_clock;

DynaMountManager::~DynaMountManager() { stop(); }

void DynaMountManager::start() {
    if (run_.exchange(true)) return;        // already running
    worker_ = std::thread([this] { workerLoop(); });
}

void DynaMountManager::stop() {
    if (!run_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
}

// ---- config -----------------------------------------------------------------

void DynaMountManager::setMount(int idx, bool enabled, std::string name,
                                std::string ip, int color) {
    if (idx < 0 || idx >= kMaxMounts) return;
    std::lock_guard<std::mutex> lk(cfgMx_);
    Mount& m = mounts_[idx];
    m.enabled = enabled;
    m.name    = std::move(name);
    m.color   = color;
    if (ip != m.ip) {                       // IP changed → re-detect, drop online
        m.ip = std::move(ip);
        m.proto.store(Proto::Unknown);
        m.online.store(false);
    }
}

void DynaMountManager::setCalibration(int idx, const Calibration& cal) {
    if (idx < 0 || idx >= kMaxMounts) return;
    std::lock_guard<std::mutex> lk(cfgMx_);
    mounts_[idx].cal = cal;
}

int DynaMountManager::definedCount() {
    std::lock_guard<std::mutex> lk(cfgMx_);
    int n = 0;
    for (auto& m : mounts_) if (m.enabled) ++n;
    return n;
}

DynaMountManager::Info DynaMountManager::info(int idx) {
    Info i{};
    if (idx < 0 || idx >= kMaxMounts) return i;
    std::lock_guard<std::mutex> lk(cfgMx_);
    Mount& m = mounts_[idx];
    i.enabled = m.enabled;
    i.name    = m.name;
    i.ip      = m.ip;
    i.color   = m.color;
    i.proto   = m.proto.load();
    i.online  = m.online.load();
    i.cal     = m.cal;
    return i;
}

// ---- strip mapping ----------------------------------------------------------

std::vector<int> DynaMountManager::enabledOrder() {
    std::vector<int> v;
    std::lock_guard<std::mutex> lk(cfgMx_);
    for (int i = 0; i < kMaxMounts; ++i)
        if (mounts_[i].enabled) v.push_back(i);
    return v;
}

int DynaMountManager::mountForStrip(int strip) {
    if (strip < 0 || strip >= kMaxMounts) return -1;
    std::vector<int> order = enabledOrder();
    const int n = static_cast<int>(order.size());
    if (n == 0) return -1;
    // k = position of this strip within the pinned block.
    int k;
    if (fill_.load() == FillDir::Left) {
        if (strip >= n) return -1;          // beyond the pinned block
        k = strip;
    } else { // Right-anchored: strips [8-n .. 7]
        const int first = kMaxMounts - n;
        if (strip < first) return -1;
        k = strip - first;
    }
    return order[k];
}

// ---- control ----------------------------------------------------------------

int DynaMountManager::mapFader(const Calibration& c, double f01, bool horizontal) {
    f01 = std::clamp(f01, 0.0, 1.0);
    const int lo = horizontal ? c.vMin : c.hMin;
    const int hi = horizontal ? c.vMax : c.hMax;
    return clampi(static_cast<int>(lo + f01 * (hi - lo) + 0.5),
                  std::min(lo, hi), std::max(lo, hi));
}

void DynaMountManager::setDistance(int idx, double f01) {
    if (idx < 0 || idx >= kMaxMounts) return;
    Calibration c; { std::lock_guard<std::mutex> lk(cfgMx_); c = mounts_[idx].cal; }
    mounts_[idx].tgtH.store(mapFader(c, f01, /*horizontal=*/false));
    mounts_[idx].dirty.store(true);
}

void DynaMountManager::setHorizontal(int idx, double f01) {
    if (idx < 0 || idx >= kMaxMounts) return;
    Calibration c; { std::lock_guard<std::mutex> lk(cfgMx_); c = mounts_[idx].cal; }
    mounts_[idx].tgtV.store(mapFader(c, f01, /*horizontal=*/true));
    mounts_[idx].dirty.store(true);
}

void DynaMountManager::nudgeRotation(int idx, int deltaClicks) {
    if (idx < 0 || idx >= kMaxMounts || deltaClicks == 0) return;
    int r = clampi(mounts_[idx].tgtR.load() + deltaClicks, kRMin, kRMax);
    mounts_[idx].tgtR.store(r);
    mounts_[idx].dirty.store(true);
}

// ---- feedback ---------------------------------------------------------------

int  DynaMountManager::targetH(int idx) { return (idx>=0&&idx<kMaxMounts) ? mounts_[idx].tgtH.load() : 0; }
int  DynaMountManager::targetR(int idx) { return (idx>=0&&idx<kMaxMounts) ? mounts_[idx].tgtR.load() : 0; }
int  DynaMountManager::targetV(int idx) { return (idx>=0&&idx<kMaxMounts) ? mounts_[idx].tgtV.load() : 0; }
bool DynaMountManager::online(int idx)  { return (idx>=0&&idx<kMaxMounts) && mounts_[idx].online.load(); }
Proto DynaMountManager::proto(int idx)  { return (idx>=0&&idx<kMaxMounts) ? mounts_[idx].proto.load() : Proto::Unknown; }

double DynaMountManager::faderNorm(int idx, bool flipped) {
    if (idx < 0 || idx >= kMaxMounts) return 0.0;
    Calibration c; { std::lock_guard<std::mutex> lk(cfgMx_); c = mounts_[idx].cal; }
    const int lo = flipped ? c.vMin : c.hMin;
    const int hi = flipped ? c.vMax : c.hMax;
    const int val = flipped ? mounts_[idx].tgtV.load() : mounts_[idx].tgtH.load();
    if (hi == lo) return 0.0;
    return std::clamp(double(val - lo) / double(hi - lo), 0.0, 1.0);
}

void DynaMountManager::requestDetect(int idx) {
    if (idx < 0 || idx >= kMaxMounts) return;
    detectReq_.fetch_or(1u << idx);
}

// ---- worker -----------------------------------------------------------------

void DynaMountManager::workerLoop() {
    // Per-mount minimum send spacing (coalescing rate-limit) ~ 15 Hz.
    constexpr auto kMinSpacing = std::chrono::milliseconds(66);
    std::array<clock::time_point, kMaxMounts> lastSend{};

    while (run_.load()) {
        const auto now = clock::now();
        const uint32_t toDetect = detectReq_.exchange(0);

        for (int i = 0; i < kMaxMounts; ++i) {
            Mount& m = mounts_[i];

            // config snapshot
            bool enabled; std::string ip;
            { std::lock_guard<std::mutex> lk(cfgMx_); enabled = m.enabled; ip = m.ip; }
            if (!enabled || ip.empty()) continue;

            // detection request
            if (toDetect & (1u << i)) {
                Proto p = detectPassive(ip);
                m.proto.store(p);
                m.online.store(p == Proto::Gen1Http || p == Proto::Gen2Tcp);
            }

            // motion: only if dirty and rate-limit elapsed
            if (!m.dirty.load()) continue;
            if (now - lastSend[i] < kMinSpacing) continue;

            const int h = m.tgtH.load(), r = m.tgtR.load(), v = m.tgtV.load();
            if (h == m.sentH && r == m.sentR && v == m.sentV) { m.dirty.store(false); continue; }

            // Gen1 path (Gen2 motion added in a later phase).
            Proto p = m.proto.load();
            if (p == Proto::Unknown) { p = detectPassive(ip); m.proto.store(p); }
            bool ok = false;
            if (p == Proto::Gen1Http) {
                Result res = gen1Move(ip, h, r, v);
                ok = res.success;
            }
            m.online.store(ok);
            if (ok) { m.sentH = h; m.sentR = r; m.sentV = v; }
            m.dirty.store(false);
            lastSend[i] = clock::now();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

} // namespace uf8::dynamount
