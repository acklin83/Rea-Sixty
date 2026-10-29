#pragma once
//
// ORC's UF8 (docs/orc-uf8-plan.md, stage 5): TotalMix on its eight strips, next
// to the UF1 or on its own.
//
// ⛔ NOTHING HERE IS A SECOND COPY. The frames are the extension's: framing
// (Uf8Frames.h), the fader read and the motor (Uf8Fader.h), the cells
// (Uf8Strips.h), the dark unit and the builders (Protocol). The rules are the
// extension's too: what a strip shows and writes (Uf8Rme), the selection, row,
// submix and window (RmeInput), the soft-key bank (RmeSoftKeys). This file is
// only the loop around them: which key goes where, the touch state, the cache.
//
// Threads: input arrives on the UF8 device's worker thread (onRaw_), painting
// runs on the Surface's loop thread (tick). Between the two: atomics, and the
// TotalMix manager, which has its own lock.

#include "RmeInput.h"
#include "RmeManager.h"
#include "SurfaceSleep.h"
#include "UF8Device.h"
#include "Uf8Frames.h"
#include "Uf8Rme.h"
#include "Uf8Strips.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace orc {

struct Uf8Status {
    bool        open = false;
    std::string error;
};

class Uf8Surface {
  public:
    // `in` is the UF1's selection (one per program), `scBank` the side-car's
    // soft-key bank, `sleep` the one clock for both surfaces, `settings` the
    // 360 request the app picks up.
    Uf8Surface(reasixty::rme::input::State& in, std::atomic<int>& scBank,
               surfsleep::Clock& sleep, std::atomic<bool>& settings);
    ~Uf8Surface();

    // One pass of the Surface's loop: retry the device every 2 s while it is
    // shut, paint while it is open. `yielding`: REAPER holds the surfaces, the
    // UF8 is closed and left alone (the handover, OrcConfig.h).
    void tick(bool yielding);
    void close();

    // Asleep or awake, as the shared clock says. Loop thread.
    void pushSleep(bool asleep);

    Uf8Status status() const;

    // ⇨ THE UF8's SIDE-CARS IN ORC (plan 2.2): Layer 1 TotalMix, 2 Hue, 3
    // DynaMount; the same key again goes back to TotalMix, ORC's ground state.
    enum class Mode : int { TotalMix = 0, Hue = 1, Dyna = 2 };

  private:
    void onRaw_(const std::uint8_t* data, std::size_t len);
    void paintTotalMix_(std::array<uf8strips::View, 8>& views, std::array<int, 8>& colour,
                        std::array<std::uint8_t, 16>& vu);
    void paintMode_(Mode m, std::array<uf8strips::View, 8>& views, std::array<int, 8>& colour);
    std::array<reasixty::rme::uf8side::SoftCell, 8> softCells_(int page);
    void softKey_(int key, bool pressed);
    void paintLeds_();
    void setMode_(Mode m);
    void onFrame_(const uf8frames::Frame& f);
    void onButton_(std::uint8_t id, bool pressed);
    void paint_();
    void resetCache_();
    std::int64_t nowMs_() const;
    double scale_() const { return fineHeld_.load() ? 0.25 : 1.0; }
    void send_(const reasixty::rme::uf8side::Writes& w);

    reasixty::rme::input::State& in_;
    std::atomic<int>&            scBank_;
    surfsleep::Clock&            sleep_;
    std::atomic<bool>&           settings_;
    surfsleep::WakeSwallow       wakeSwallow_;

    uf8::UF8Device               dev_;
    uf8frames::Assembler         asm_;          // device thread only
    reasixty::rme::uf8side::Side side_;

    // Touch: a hand on the cap. The release is held 150 ms, as on the UF1
    // (RmeFace, "Touch-Entprellung"): the sensor bounces under a held finger,
    // and a motor driven on every bounce fights the hand.
    std::array<std::atomic<bool>, 8>         touched_{};
    std::array<std::atomic<std::int64_t>, 8> releasedMs_{};
    std::array<std::atomic<bool>, 8>         engaged_{};
    std::atomic<bool>                        fineHeld_{false};
    std::atomic<int>                         mode_{0};       // Mode
    std::atomic<bool>                        flip_{false};
    // Soft-key page on V-POT / 1-5 (plan 2.3): 0 the TotalMix bank, 1 Hue
    // scenes, 2 DynaMount presets, 3 OBS scenes, 4-5 empty.
    std::atomic<int>                         skPage_{0};
    std::array<std::atomic<std::int64_t>, 8> keyDownMs_{};    // soft keys, for the long press
    // A stand's fader is sent on release (Frank 2026-06-28): the position under
    // the hand waits here.
    std::array<std::atomic<bool>, 8>         dynPending_{};
    std::array<std::atomic<double>, 8>       dynPendingNorm_{};
    std::array<int, 20>                      cLed_{};
    std::atomic<bool>                        repaint_{true};

    // What the device shows (Uf8Strips::Cells point in here). Loop thread.
    std::array<std::string, 8>   cUpper_, cCs_, cChan_, cFt_, cFu_, cVl_, cSl_;
    std::array<std::int32_t, 8>  cTop_{};
    std::array<std::int8_t, 8>   cSel_{}, cCut_{}, cSolo_{};
    std::array<std::uint16_t, 8> cPb_{};
    std::uint16_t                cBar_[8] = {};
    std::uint8_t                 cMode_[8] = {};
    bool                         ringInit_ = false;
    std::array<int, 8>           cColour_{};
    std::array<std::uint8_t, 16> cVu_{};
    int                          cSelMask_ = -1;

    std::chrono::steady_clock::time_point lastTry_{};
    std::string                            lastErr_;
    mutable std::mutex                     mu_;
    Uf8Status                              st_;
};

} // namespace orc
