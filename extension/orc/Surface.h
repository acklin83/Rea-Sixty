#pragma once
//
// ORC's surface thread: the UF1 half, running on its own.
//
// Up to stage 6 this was the body of main(). Stage 7 gives ORC a window, and a
// window owns the main thread on macOS, so the tick moved here. What crosses
// between the two threads is one small struct behind one mutex, plus the atomic
// that says which view has the screen.
//
// ⛔ THE UI NEVER TOUCHES THE DEVICE. It reads status() and sets setStrip();
// every libusb call and every frame stays on this thread. UF1Device::send is
// documented thread-safe (UF1Device.h), but "thread-safe" is not a licence to
// have two owners, and one owner is cheaper to reason about than a mutex.
//
// ⇨ A CLOSED SURFACE IS A STATE, NOT AN EXIT. Before stage 7, ORC quit with
// status 1 when the UF1 would not open. It cannot do that any more: the window
// has to come up so it can SAY that the surface is held by something else, and
// ORC has to be able to run next to REAPER while Rea-Sixty holds the hardware.

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "RmeInput.h"
#include "Uf1Pacer.h"
#include "UF1Device.h"
#include "Uf1Spread.h"

namespace orc {

// What the surface knows about itself. Copied out under the lock; the window
// gets a snapshot, never a reference.
struct Status {
    bool        open   = false;
    std::string serial;
    // Why it is not open, verbatim from UF1Device::lastError(). ⛔ Not
    // interpreted: libusb cannot tell "no UF1 on the bus" from "another program
    // holds it" at the open step, so ORC repeats what it was told instead of
    // inventing a third state it cannot prove.
    std::string error;
    long        frames = 0;
};

class Surface {
  public:
    ~Surface();

    void start();
    void stop();

    Status status() const;

    // ⇨ THE VIEW IS PART OF THE INPUT STATE, not a flag of its own. The channel
    // encoder's push and the settings window are two ways to the same switch,
    // and a second copy of it would be one of them going stale.
    bool strip() const { return in_.strip.load(); }
    void setStrip(bool on) { in_.strip.store(on); }

  private:
    void loop_();

    void onEvent_(const ::uf1::InputEvent& ev);
    void pacerLoop_();
    bool softKeys_(const ::uf1::InputEvent& ev);

    mutable std::mutex mu_;
    Status             st_;
    std::atomic<bool>  quit_{false};
    std::thread        th_;
    std::thread        pacer_;
    ::uf1::UF1Device   dev_;

    // Header and meter for the pacer, published by the painter each pass.
    std::mutex                              cycMx_;
    std::shared_ptr<const uf1pace::Parts>   cycle_;
    std::atomic<bool>                       cycleActive_{false};
    std::atomic<long>                       sent_{0};   // frames the painter put out

    // ⇨ WHAT THE SURFACE HAS SELECTED, and the rules that act on it. Shared
    // with the extension: src/RmeInput.{h,cpp}, not a second copy. ORC leaves
    // two of the three host callbacks unset, because it has no MODE menu and no
    // fine mode; an unset callback is a state, not a failure.
    reasixty::rme::input::State in_;
    reasixty::rme::input::Host  host_;

    // The fader: whether a hand is on it, and where it stands. While it is held
    // the surface writes to TotalMix and the motor is left alone; let go and the
    // painter drives it again.
    // The side-car's soft-key bank, relative within the RME set.
    std::atomic<int>      scBank_{0};

    std::atomic<bool>     faderTouched_{false};
    std::atomic<uint16_t> faderPos_{0};
    std::atomic<bool>     faderHasPos_{false};
};

} // namespace orc
