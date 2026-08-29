#pragma once
//
// UF1Device — libusb wrapper for the SSL UF1 (VID 0x31E9 / PID 0x0025).
// Parallel to UF8Device/UC1Device. The UF1 is much simpler than the UF8:
// no fader-tanz, no layer-switch flood, no FF 66 / FF 5B liveness streams.
// Init = replay the cap66 cold-start burst (uf1_init_sequence.inc); steady-
// state liveness = the FF 1B 01 keepalive at 150 ms (counter 0..3). Screen
// content (FF 67) is pushed by our own code, not the keepalive.
//
// Threading: all libusb calls happen on a dedicated worker thread. send() is
// producer-side (thread-safe, buffered). The parsed InputHandler fires on the
// worker thread — hop to REAPER's main thread (queueInput) before touching
// REAPER/ImGui state (see feedback-reaper-api-input-thread).
//

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "UF1Protocol.h"

struct libusb_context;
struct libusb_device_handle;
struct libusb_transfer;

namespace uf1 {

class UF1Device {
public:
    using RawInputHandler = std::function<void(const uint8_t* data, size_t len)>;

    UF1Device();
    ~UF1Device();

    UF1Device(const UF1Device&) = delete;
    UF1Device& operator=(const UF1Device&) = delete;

    // Open + claim the UF1. Returns true on success; lastError() explains a
    // failure (most commonly SSL 360 holding the device — claiming takes it).
    bool open();
    void close();
    bool isOpen() const { return handle_ != nullptr; }

    // Fire-and-forget send (thread-safe; worker drains over EP 0x02 OUT).
    void send(std::vector<uint8_t> frame);
    // Enqueue a group of frames atomically (one lock), so nothing of ours can
    // land between them. Used for the 35-chunk goniometer image: SSL never
    // emits anything inside an image burst (cap89/cap100 diff).
    void sendBurst(std::vector<std::vector<uint8_t>> frames);
    // ⇨ HOLD ORDINARY SENDS FOR THE DURATION OF A CYCLE.
    // sendBurst keeps ONE group atomic, but the Overview cycle is THREE groups
    // with deliberate silences between them (image, meters, trailer), and those
    // silences are the device's render windows. A send() landing in one is a
    // frame inside the image burst, which cap89/cap100 show SSL never does --
    // and the firmware answers by dropping to lazy render-on-idle, i.e. the
    // whole display stops updating until a 0x0100 layout re-assert wakes it.
    // Frank 2026-08-29: everything froze, including the channel peak meter that
    // has nothing to do with the meter view, and a channel-encoder move (the one
    // path that re-sends 0x0100) always fixed it.
    // Between begin and end, send() queues into a holding area that is released
    // in one go afterwards. sendPriority and sendBurst are unaffected.
    void beginCycle();
    void endCycle();

    // Front-of-queue send for latency-sensitive frames (e.g. motor-limp on touch).
    void sendPriority(std::vector<uint8_t> frame);

    // Parsed control events (fader/encoder/button). Fires on the worker thread.
    void setInputHandler(InputHandler h) { inputHandler_ = std::move(h); }
    // Raw bulk-IN payloads (for diagnostics / logging). Fires on the worker thread.
    void setRawInputHandler(RawInputHandler h) { rawInputHandler_ = std::move(h); }

    const std::string& lastError() const { return lastError_; }
    const std::string& serial() const { return serial_; }

    // When on, every IN + OUT frame is appended to <log dir>/reaper_uf1_frames.log
    // (uf8::logPath — %TEMP% on Windows, /tmp elsewhere).
    void setFrameTrace(bool on) { frameTrace_.store(on, std::memory_order_relaxed); }
    bool frameTrace() const { return frameTrace_.load(std::memory_order_relaxed); }

    // True after ~1 s of consecutive NO_DEVICE / NOT_FOUND / IO on bulk OUT —
    // the handle is stale (USB re-enumerated under us). Main thread polls this
    // and reopens. Same pattern as UF8Device/UC1Device.
    bool needsReopen() const { return needsReopen_.load(); }

private:
    void workerLoop_();
    void runInit_();
    void startBulkRead_();
    static void readCallback_(libusb_transfer* xfer);
    void traceFrame_(char dir, const uint8_t* data, size_t len, int rc) const;

    // Guarded by pending_->mu. cycleDepth_ > 0 means a cycle is mid-flight and
    // ordinary sends wait in held_ until it closes.
    int                              cycleDepth_ = 0;
    std::vector<std::vector<uint8_t>> held_;

    libusb_context*       ctx_    = nullptr;
    libusb_device_handle* handle_ = nullptr;
    std::string           serial_;
    std::string           lastError_;
    std::atomic<bool>     shuttingDown_{false};
    std::atomic<bool>     frameTrace_{false};
    // Worker skips draining the user-send queue while the init replay runs so
    // REAPER's onTimer pushes don't interleave with init frames; the FF 1B
    // keepalive + IN reads continue throughout.
    std::atomic<bool>     initInProgress_{false};
    std::thread           worker_;
    std::thread           initThread_;
    InputHandler          inputHandler_;
    RawInputHandler       rawInputHandler_;
    // Cross-URB residual: a frame (or header) split by a bulk-transfer boundary
    // is kept here and prepended to the next URB so no input event is lost when
    // two events land across two transfers. Touched only in readCallback_ (the
    // single libusb event thread). Capped so a desync can't grow it unbounded.
    std::vector<uint8_t>  readResidual_;

    struct PendingSend;
    std::unique_ptr<PendingSend> pending_;

    std::atomic<int>   consecutiveErrors_{0};
    std::atomic<bool>  needsReopen_{false};
    // Diagnostic full-session replay (uf1_blast.bin): while set, the worker
    // drops every queued user frame and sends no keepalive — the recording
    // owns the wire. See runInit_.
    std::atomic<bool>  blastActive_{false};
};

} // namespace uf1
