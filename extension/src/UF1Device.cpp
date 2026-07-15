#include "UF1Device.h"

#include <libusb.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <span>
#include <thread>

#include "uf1_init_sequence.inc"

namespace uf1 {

namespace {
constexpr uint16_t kVid       = 0x31E9;
constexpr uint16_t kPid       = 0x0025;   // "SSL 18 Comms Device" — the UF1 control IF
constexpr uint8_t  kInterface = 0;
constexpr uint8_t  kEpOut     = 0x02;
constexpr uint8_t  kEpIn      = 0x81;
constexpr size_t   kInBufSize = 64;
constexpr int      kStaleThreshold = 50;  // ~1 s of failures at the 50 Hz worker tick
}

struct UF1Device::PendingSend {
    std::mutex              mu;
    std::condition_variable cv;
    std::deque<std::vector<uint8_t>> q;
};

UF1Device::UF1Device()
: pending_(std::make_unique<PendingSend>())
{
}

UF1Device::~UF1Device()
{
    close();
}

bool UF1Device::open()
{
    if (handle_) return true;

    if (libusb_init(&ctx_) < 0) {
        lastError_ = "libusb_init failed";
        return false;
    }
    handle_ = libusb_open_device_with_vid_pid(ctx_, kVid, kPid);
    if (!handle_) {
        lastError_ = "UF1 not found or not accessible — is SSL 360 running? Close it first.";
        libusb_exit(ctx_);
        ctx_ = nullptr;
        return false;
    }

    if (libusb_kernel_driver_active(handle_, kInterface) == 1) {
        if (libusb_detach_kernel_driver(handle_, kInterface) < 0) {
            lastError_ = "Could not detach kernel driver (likely SSL 360 owns the UF1)";
            libusb_close(handle_);
            libusb_exit(ctx_);
            handle_ = nullptr;
            ctx_ = nullptr;
            return false;
        }
    }

    int rc = libusb_claim_interface(handle_, kInterface);
    if (rc < 0) {
        lastError_ = std::string("libusb_claim_interface failed: ") + libusb_error_name(rc);
        libusb_close(handle_);
        libusb_exit(ctx_);
        handle_ = nullptr;
        ctx_ = nullptr;
        return false;
    }

    // iSerialNumber for Settings → Device display (best-effort).
    {
        libusb_device_descriptor desc{};
        if (libusb_device* d = libusb_get_device(handle_)) {
            if (libusb_get_device_descriptor(d, &desc) >= 0 && desc.iSerialNumber != 0) {
                unsigned char sbuf[256] = {0};
                const int n = libusb_get_string_descriptor_ascii(
                    handle_, desc.iSerialNumber, sbuf, sizeof(sbuf));
                if (n > 0) serial_.assign(reinterpret_cast<char*>(sbuf), n);
            }
        }
    }

    // SSL 360 opens the comms bridge with a full FTDI-style D2XX sequence on
    // EP0 BEFORE its first bulk byte (cap84 t=54.05..54.23): reset + modem-
    // status twice ~150 ms apart, data 8N1, flow control off, baud divisor
    // 0xc068 idx 0x0200 three times, latency timer 2 ms, purge RX six times,
    // purge TX once. We never sent ANY of these. Replicated verbatim — counts,
    // order and the 150 ms pause included. Best-effort: a failed request is
    // traced ('C' lines) but does not abort the open, since bulk I/O has
    // always worked without the sequence.
    {
        auto vendorOut = [this](uint8_t bRequest, uint16_t wValue, uint16_t wIndex) {
            const int rc = libusb_control_transfer(
                handle_, 0x40, bRequest, wValue, wIndex, nullptr, 0, 1000);
            const uint8_t rec[6] = { 0x40, bRequest,
                                     static_cast<uint8_t>(wValue & 0xff),
                                     static_cast<uint8_t>(wValue >> 8),
                                     static_cast<uint8_t>(wIndex & 0xff),
                                     static_cast<uint8_t>(wIndex >> 8) };
            traceFrame_('C', rec, sizeof(rec), rc);
        };
        auto vendorModemStatus = [this]() {
            uint8_t st[2] = {0, 0};
            const int rc = libusb_control_transfer(
                handle_, 0xc0, 5, 0x0000, 0, st, sizeof(st), 1000);
            traceFrame_('C', st, sizeof(st), rc);
        };
        vendorOut(0, 0x0000, 0);                                    // reset
        vendorModemStatus();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        vendorOut(0, 0x0000, 0);                                    // reset, round 2
        vendorModemStatus();
        vendorOut(4, 0x0008, 0);                                    // data: 8N1
        vendorOut(2, 0x0000, 0);                                    // flow control: none
        for (int i = 0; i < 3; ++i) vendorOut(3, 0xc068, 0x0200);   // baud divisor
        vendorOut(9, 0x0002, 0);                                    // latency timer 2 ms
        for (int i = 0; i < 6; ++i) vendorOut(0, 0x0001, 0);        // purge RX
        vendorOut(0, 0x0002, 0);                                    // purge TX
    }

    libusb_clear_halt(handle_, kEpOut);
    libusb_clear_halt(handle_, kEpIn);

    // Worker before init: it pumps the async IN reads (a drained IN endpoint
    // keeps OUT flow-control moving) and emits the FF 1B keepalive throughout.
    shuttingDown_ = false;
    initInProgress_ = true;
    worker_ = std::thread([this]{ workerLoop_(); });
    initThread_ = std::thread([this]{ runInit_(); });
    return true;
}

void UF1Device::runInit_()
{
    // Let the worker post its first IN transfer + a keepalive.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // SSL's very FIRST bulk frame after the EP0 vendor init is 256 zero bytes,
    // before the FF01 wake (cap101 t=26.16, cap84 likewise starts with an
    // all-zero block previously dismissed as a "descriptor artifact"). It can
    // only be a parser flush/resync: 0x00 is no opcode, so a firmware parser
    // stuck mid-frame from a previous session swallows it and lands on a frame
    // boundary. We never sent it.
    {
        std::vector<uint8_t> zeros(256, 0x00);
        int transferred = 0;
        int zrc = libusb_bulk_transfer(handle_, kEpOut, zeros.data(),
                                       static_cast<int>(zeros.size()),
                                       &transferred, 1000);
        traceFrame_('O', zeros.data(), zeros.size(), zrc);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // Replay the cap66 cold-start burst: wake/mode opcodes -> per-LED init
    // sweep -> initial FF 67 screen paint. 2 ms inter-frame pacing matches
    // SSL's observed gap; explicit delay_ms_before honours any load-bearing
    // pause (the UF1 init has none, but the field is respected for parity).
    for (const auto& f : kUf1InitSequence) {
        if (shuttingDown_.load()) { initInProgress_ = false; return; }
        if (f.delay_ms_before > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(f.delay_ms_before));
        }
        int transferred = 0;
        int ircode = libusb_bulk_transfer(
            handle_, kEpOut,
            const_cast<uint8_t*>(f.bytes),
            static_cast<int>(f.size),
            &transferred, 1000);
        traceFrame_('O', f.bytes, f.size, ircode);
        if (ircode < 0) {
            lastError_ = std::string("UF1 init bulk OUT failed: ") + libusb_error_name(ircode);
            initInProgress_ = false;
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // Plugin-layout override (2026-06-05) — LEADING hypothesis for the
    // colour-bar-won't-render + MODE-button-inert symptoms. The cap66 init we
    // replay is the MCU-mode cold start: its first paints of 0x0000/0x000d/0x0011
    // carry MCU flag values (02 00 / 04 / 00). cap84 (Plugin cold start) sets
    // them to 03 00 / 03 / 01. We re-send the Plugin values at init-time (device
    // thread, before any content) so the firmware enters the channel-strip/meter
    // layout the fake-CS population needs. 0x0000 02/03 mirrors the UF8 slot
    // empty/populated flag — suggestive, NOT proven (cross-frame-type value
    // match). Re-asserted on `changed` in uf1PaintChannel_ (latched-or-dynamic).
    {
        auto sendModeFrame = [&](uint16_t addr, std::span<const uint8_t> payload) {
            if (shuttingDown_.load()) return;
            auto frame = uf1::buildScreen(addr, payload);
            int transferred = 0;
            int ircode = libusb_bulk_transfer(handle_, kEpOut, frame.data(),
                                              static_cast<int>(frame.size()),
                                              &transferred, 1000);
            traceFrame_('O', frame.data(), frame.size(), ircode);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        };
        // Full PLUGIN large-display state, mirrored byte-for-byte from cap85 —
        // the live SSL 360 init that DOES render the channel-strip + EQ graph.
        // The cap66 init we replay is MCU: it leaves 0x0100=8003 (MCU large
        // display) and the 0x01xx structural bytes at 0. Setting 0x0000=03 alone
        // got the colour bar + CS params (channel-info plane) but the LARGE LCD
        // (0x01xx, where the 0x0122 EQ graph lives) stayed in MCU layout → EQ
        // never painted. cap85-vs-cap66 diff: these are the EQ-render-specific
        // structural bytes we were missing. 0x0100=0300 is the big one (plugin
        // large-display layout). cap84 (plugin idle, no plug-in) reaches the same
        // state but shows nothing for lack of EQ data — we have both now.
        sendModeFrame(0x0000, std::vector<uint8_t>{0x03, 0x00});
        // 03, not 01: cap84 AND cap101 both hold 0x000d=03 in plugin mode; the
        // 01 that used to be here contradicted this block's own source comment
        // and left the device in a layout neither MCU (04) nor plugin (03).
        sendModeFrame(0x000d, std::vector<uint8_t>{0x03});
        sendModeFrame(0x0011, std::vector<uint8_t>{0x01});
        sendModeFrame(0x0100, std::vector<uint8_t>{0x03, 0x00});
        sendModeFrame(0x0101, std::vector<uint8_t>{0x05});
        sendModeFrame(0x010d, std::vector<uint8_t>{0x02, 0x02, 0x08, 0x02});
        sendModeFrame(0x0110, std::vector<uint8_t>{0x0f});
        sendModeFrame(0x011a, std::vector<uint8_t>{0x02});
        // 0x011d is the cycle-closing view-state byte (cap101: 00 on Overview,
        // 11 in channel idle). The 0x19 that used to be seeded here was cap77's
        // session value — we entered the first image cycle with a state SSL
        // never has on this path. SSL's own first connect write is 00.
        sendModeFrame(0x011d, std::vector<uint8_t>{0x00});
        sendModeFrame(0x011e, std::vector<uint8_t>{0x18});
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // Init complete — worker can drain the user-send queue.
    initInProgress_ = false;
}

void UF1Device::close()
{
    if (!handle_) return;
    shuttingDown_ = true;
    pending_->cv.notify_all();
    if (initThread_.joinable()) initThread_.join();
    if (worker_.joinable()) worker_.join();

    libusb_release_interface(handle_, kInterface);
    libusb_close(handle_);
    handle_ = nullptr;

    libusb_exit(ctx_);
    ctx_ = nullptr;

    consecutiveErrors_.store(0);
    needsReopen_.store(false);
    shuttingDown_.store(false);
}

void UF1Device::send(std::vector<uint8_t> frame)
{
    {
        std::lock_guard<std::mutex> lk(pending_->mu);
        pending_->q.push_back(std::move(frame));
    }
    pending_->cv.notify_one();
}

void UF1Device::sendBurst(std::vector<std::vector<uint8_t>> frames)
{
    if (frames.empty()) return;
    {
        std::lock_guard<std::mutex> lk(pending_->mu);
        for (auto& f : frames) pending_->q.push_back(std::move(f));
    }
    pending_->cv.notify_one();
}

void UF1Device::sendPriority(std::vector<uint8_t> frame)
{
    {
        std::lock_guard<std::mutex> lk(pending_->mu);
        pending_->q.push_front(std::move(frame));
    }
    pending_->cv.notify_one();
}

void UF1Device::workerLoop_()
{
    startBulkRead_();

    // FF 1B 01 <ctr> <ck> keepalive every 150 ms (measured exactly 150 ms in
    // cap66 steady-state; counter cycles 0..3). This is the UF1's liveness —
    // the FF 67 screen stream is content, pushed by our own code.
    constexpr auto kKeepaliveInterval = std::chrono::milliseconds(150);
    auto lastKeepalive = std::chrono::steady_clock::now();
    uint8_t kaCounter = 0;

    auto recordRc = [this](int rc) {
        if (rc < 0) {
            if (rc == LIBUSB_ERROR_NO_DEVICE
                || rc == LIBUSB_ERROR_NOT_FOUND
                || rc == LIBUSB_ERROR_IO) {
                if (consecutiveErrors_.fetch_add(1) + 1 >= kStaleThreshold) {
                    needsReopen_.store(true);
                }
            }
        } else {
            consecutiveErrors_.store(0);
        }
    };

    while (!shuttingDown_) {
        std::vector<std::vector<uint8_t>> batch;
        bool queueDrained = true;
        {
            std::unique_lock<std::mutex> lk(pending_->mu);
            pending_->cv.wait_for(lk, std::chrono::milliseconds(20),
                [&] { return !pending_->q.empty() || shuttingDown_; });
            if (shuttingDown_) break;
            // Hold user frames until the init replay finishes (else REAPER's
            // onTimer pushes interleave with init frames and corrupt boot).
            if (!initInProgress_.load()) {
                // Big enough for a whole Overview cycle (35 image chunks + 10
                // trailer elements), so a burst enqueued via sendBurst() goes
                // out in ONE iteration.
                constexpr size_t kMaxBatch = 64;
                while (!pending_->q.empty() && batch.size() < kMaxBatch) {
                    batch.push_back(std::move(pending_->q.front()));
                    pending_->q.pop_front();
                }
            }
            queueDrained = pending_->q.empty();
        }

        // Content first, keepalive after — and only when the queue is empty.
        // SSL never sends ANYTHING inside an image burst; our FF1B used to land
        // mid-image in ~11% of images (cap100). The 500 ms override keeps the
        // liveness beat under a hypothetical sustained backlog (normal bursts
        // are ~7 ms, so deferral is invisible).
        for (auto& frame : batch) {
            int transferred = 0;
            int rc = libusb_bulk_transfer(handle_, kEpOut, frame.data(),
                                          static_cast<int>(frame.size()),
                                          &transferred, 500);
            if (rc < 0) lastError_ = std::string("bulk OUT failed: ") + libusb_error_name(rc);
            traceFrame_('O', frame.data(), frame.size(), rc);
        }

        auto now = std::chrono::steady_clock::now();
        const bool overdue = (now - lastKeepalive) >= std::chrono::milliseconds(500);
        if ((queueDrained || overdue) && now - lastKeepalive >= kKeepaliveInterval) {
            std::vector<uint8_t> ka = buildKeepalive(kaCounter);
            int t = 0;
            int rc = libusb_bulk_transfer(handle_, kEpOut, ka.data(),
                                          static_cast<int>(ka.size()), &t, 100);
            recordRc(rc);
            traceFrame_('O', ka.data(), ka.size(), rc);
            kaCounter = (kaCounter + 1) & 0x03;
            lastKeepalive = now;
        }

        timeval tv{0, 1000};  // 1 ms
        libusb_handle_events_timeout_completed(ctx_, &tv, nullptr);
    }
}

void UF1Device::traceFrame_(char dir, const uint8_t* data, size_t len, int rc) const
{
    if (!frameTrace_.load(std::memory_order_relaxed)) return;
    FILE* lg = std::fopen("/tmp/reaper_uf1_frames.log", "a");
    if (!lg) return;
    const auto t  = std::chrono::system_clock::now().time_since_epoch();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t).count();
    std::fprintf(lg, "[%lld] %c rc=%d len=%zu ", static_cast<long long>(ms), dir, rc, len);
    for (size_t i = 0; i < len; ++i) std::fprintf(lg, "%02x", data[i]);
    std::fputc('\n', lg);
    std::fclose(lg);
}

void UF1Device::startBulkRead_()
{
    auto* xfer = libusb_alloc_transfer(0);
    auto* buf = new uint8_t[kInBufSize];
    libusb_fill_bulk_transfer(
        xfer, handle_, kEpIn, buf, kInBufSize,
        &UF1Device::readCallback_, this, 0 /* unlimited timeout */);
    libusb_submit_transfer(xfer);
}

void UF1Device::readCallback_(libusb_transfer* xfer)
{
    auto* self = static_cast<UF1Device*>(xfer->user_data);

    if (xfer->status == LIBUSB_TRANSFER_COMPLETED && xfer->actual_length > 0) {
        const size_t len = static_cast<size_t>(xfer->actual_length);
        self->traceFrame_('I', xfer->buffer, len, 0);
        if (self->rawInputHandler_) self->rawInputHandler_(xfer->buffer, len);
        if (self->inputHandler_) {
            parseInputStream(std::span<const uint8_t>{xfer->buffer, len}, self->inputHandler_);
        }
    }

    if (!self->shuttingDown_ && xfer->status != LIBUSB_TRANSFER_CANCELLED) {
        libusb_submit_transfer(xfer);
    } else {
        delete[] xfer->buffer;
        libusb_free_transfer(xfer);
    }
}

} // namespace uf1
