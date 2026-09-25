#include "Surface.h"

#include "OrcConfig.h"
#include "Uf1Text.h"
#include "RmeBuiltins.h"
#include "Bindings.h"
#include "RmeFace.h"
#include "RmeInput.h"
#include "Uf1Pacer.h"
#include "RmeManager.h"
#include "RmeState.h"
#include "RmeStrip.h"
#include "RmeUf1.h"
#include "UF1Protocol.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace rme  = reasixty::rme;
namespace rmeu = reasixty::rme::uf1;
namespace rmes = reasixty::rme::strip;
namespace rmei = reasixty::rme::input;
namespace rmef = reasixty::rme::face;

namespace orc {
namespace {

// The extension's default fine factor (g_fineFactorUf1 in main.cpp). ORC has no
// setting for it yet.
constexpr double kFineFactor = 0.25;

const char* kindName(uf1::InputKind k)
{
    switch (k) {
        case uf1::InputKind::FaderTouch:    return "fader-touch";
        case uf1::InputKind::FaderPosition: return "fader-pos";
        case uf1::InputKind::EncoderRotate: return "encoder";
        case uf1::InputKind::EncoderTouch:  return "encoder-touch";
        case uf1::InputKind::Button:        return "button";
    }
    return "?";
}

const char* linkName(rme::LinkState st)
{
    switch (st) {
        case rme::LinkState::Off:      return "off";
        case rme::LinkState::PortBusy: return "port busy";
        case rme::LinkState::Waiting:  return "waiting";
        case rme::LinkState::Online:   return "online";
        case rme::LinkState::Silent:   return "silent";
    }
    return "?";
}

// What TotalMix has told us so far. Printed once when the link comes up. Up to
// stage 6 this repeated every second because there was nowhere else to look;
// the window shows the same counts now, so the console says it once.
void dumpModel(const rme::State& st)
{
    const auto cr = rme::manager().controlRoom();
    std::printf("\n-- control room -------------------------------------------\n");
    std::printf("   main out %d   main B %d   phones %d %d %d %d\n",
                st.mainOut, st.mainOutB, st.phones[0], st.phones[1],
                st.phones[2], st.phones[3]);
    std::printf("   dim %d  mono %d  speaker B %d  talkback %d\n",
                cr.dim, cr.mono, cr.speakerB, cr.talkback);

    // ⛔ visibleChannels, not the raw map. A parameter that exists per side of a
    // stereo pair arrives under index+1, so counting anything that ever sent a
    // value reported 94 outputs where the mixer shows 5. A strip announces
    // itself with a NAME; that is what makes it a channel.
    for (auto row : { rmeu::Row::Input, rmeu::Row::Playback, rmeu::Row::Output }) {
        const auto list = rmeu::visibleChannels(st, row);
        std::printf("-- %-8s %2zu channels", rmeu::rowName(row), list.size());
        for (size_t i = 0; i < list.size() && i < 6; ++i)
            std::printf("  %d:%s", list[i], rmeu::displayName(st, row, list[i]).c_str());
        std::printf("%s\n", list.size() > 6 ? "  ..." : "");
    }
    std::fflush(stdout);
}

// ⇨ NO GATHERER HERE ANY MORE. Until 2026-09-25 ORC built its own view of the
// mixer for the painter (viewFor / stripViewFor), from memory, and it showed
// no pot labels, no colour bars and text in the seven-segment field. It paints
// with the extension's side-car painter now, src/RmeFace.cpp, word for word.

} // namespace

// ⇨ WHERE ORC ACTS. Until 2026-09-25 this printed the event and did nothing
// else: ORC read the mixer and painted the surface, and no knob reached
// TotalMix. The rules are the extension's, out of src/RmeInput.{h,cpp}, so
// there is one description of what a pot does and not two.
//
// ⛔ Runs on the device's worker thread. Everything it touches is either atomic
// (the input state) or has its own lock (the manager), and it never calls into
// the window.
void Surface::onEvent_(const ::uf1::InputEvent& ev)
{
    auto& mgr = rme::manager();
    const auto st  = mgr.snapshot();
    const auto cfg = mgr.config();
    rmei::Writes w;

    switch (ev.kind) {
        case ::uf1::InputKind::Button:
            // The extension's order (onUf1Event): STRIP's page keys, then the
            // side-car's soft-key banks, then the side-car's own keys, and what
            // it lets through (transport, SHIFT, 360) goes to the bindings.
            if (rmei::stripSoftKey(in_, host_, st, cfg, ev, w)) break;
            if (softKeys_(ev)) break;
            if (rmei::button(in_, host_, st, cfg, ev, w)) break;
            if (auto b = uf8::bindings::fromUf1DeviceId(ev.id);
                b != uf8::bindings::ButtonId::None)
                uf8::bindings::dispatch(b, ev.pressed);
            break;
        case ::uf1::InputKind::EncoderRotate:
            rmei::encoder(in_, host_, st, cfg, ev.id, ev.delta, w);
            break;
        case ::uf1::InputKind::FaderTouch:
            faderTouched_.store(ev.pressed);
            // Let the motor go the instant a hand arrives, so it does not fight
            // the finger. The painter takes it back when the hand leaves.
            if (ev.pressed) dev_.sendPriority(::uf1::buildMotorEnable(false));
            break;
        case ::uf1::InputKind::FaderPosition:
            faderPos_.store(ev.position);
            faderHasPos_.store(true);
            break;
        default:
            break;
    }

    for (const auto& [addr, val] : w) mgr.send(addr, val);
}

// ⇨ THE SIDE-CAR'S SOFT-KEY BANKS, the extension's uf1SideCarSoftKeys_ minus
// what ORC has not got: no MODE menu owning the keys, and no dynamic banks
// (those list REAPER's tracks, FX and sends). The four keys fire their slot of
// the RME set's current bank through the bindings engine; < > step the bank,
// with no wrap, because the lamp says whether there is more that way.
bool Surface::softKeys_(const ::uf1::InputEvent& ev)
{
    namespace bnd = uf8::bindings;
    const int set = bnd::kUf1SideCarSetRme;
    const std::uint8_t id = ev.id;
    if (id >= ::uf1::btn::kDisplaySoft1 && id <= ::uf1::btn::kDisplaySoft4) {
        bnd::dispatchUf1SoftBankSlot(bnd::uf1SideCarBankBase(set) + scBank_.load(),
                                     static_cast<int>(id - ::uf1::btn::kDisplaySoft1),
                                     ev.pressed);
        return true;
    }
    if (id == ::uf1::btn::kArrowLeft || id == ::uf1::btn::kArrowRight) {
        if (ev.pressed) {
            const int nb  = std::max(1, bnd::uf1SideCarBankInUseCount(set));
            const int dir = (id == ::uf1::btn::kArrowRight) ? 1 : -1;
            scBank_.store(std::clamp(scBank_.load() + dir, 0, nb - 1));
        }
        return true;
    }
    return false;
}

Surface::~Surface() { stop(); }

void Surface::start()
{
    if (th_.joinable()) return;

    // ⇨ ORC'S HOST ANSWERS. It has no MODE menu and no fine mode, so those two
    // stay unset and RmeInput reads them as "no" and "1.0". The link question it
    // can answer.
    host_.online = [] { return rme::manager().link() == rme::LinkState::Online; };
    // ⇨ FINE = THE SHIFT MODIFIER, AS THE BINDINGS ENGINE HOLDS IT. The UF1's
    // SHIFT key is bound to mod_shift in orc.json by default, exactly as in the
    // extension, so rebinding it in orc.json is what makes it "bindable"
    // (Frank 25.09.: "schalts ein und machs bindable"). Factor: the extension's
    // default, g_fineFactorUf1 = 0.25.
    host_.knobScale = [] {
        return uf8::bindings::modifierHeld(uf8::bindings::Modifier::Shift) ? kFineFactor : 1.0;
    };

    // The six rme_* builtins, the extension's own (src/RmeBuiltins.cpp), against
    // this surface's side-car state. Without them the factory bank's rme_dim
    // and friends in orc.json were names that did nothing.
    reasixty::rme::registerBuiltins(in_, host_);

    // The handler fires on the device's worker thread.
    dev_.setInputHandler([this](const ::uf1::InputEvent& ev) {
        onEvent_(ev);
        switch (ev.kind) {
            case uf1::InputKind::FaderPosition:
                std::printf("%-14s pos=%5u\n", kindName(ev.kind), ev.position);
                break;
            case uf1::InputKind::EncoderRotate:
                std::printf("%-14s id=0x%02X delta=%+d\n", kindName(ev.kind), ev.id, ev.delta);
                break;
            default:
                std::printf("%-14s id=0x%02X %s\n", kindName(ev.kind), ev.id,
                            ev.pressed ? "down" : "up");
                break;
        }
        std::fflush(stdout);
    });

    // ⇨ THE SAME FRAME TRACE THE EXTENSION HAS (UF1Device::setFrameTrace), so
    // the two can be compared frame for frame. ORC_TRACE=1 switches it on; it
    // writes to the same file name the extension does, in the same log folder.
    if (const char* t = std::getenv("ORC_TRACE"); t && *t && std::string(t) != "0")
        dev_.setFrameTrace(true);

    quit_.store(false);
    th_    = std::thread([this] { loop_(); });
    pacer_ = std::thread([this] { pacerLoop_(); });
}

void Surface::stop()
{
    quit_.store(true);
    if (pacer_.joinable()) pacer_.join();
    if (th_.joinable()) th_.join();
    dev_.close();
}

Status Surface::status() const
{
    std::lock_guard<std::mutex> lk(mu_);
    Status s = st_;
    s.frames = sent_.load();
    return s;
}

void Surface::loop_()
{
    auto lastLink = rme::LinkState::Off;
    auto lastOpenTry = std::chrono::steady_clock::now() - std::chrono::seconds(5);
    bool force = true;

    // ⇨ THE HOST'S ANSWERS TO THE SHARED PAINTER. Each one is a thing the
    // extension answers from its own globals; unset ones are states (no MODE
    // menu, one soft-key bank, no flash text, no lamps of ours yet).
    rmef::Host host;
    host.faderTouched = [this] { return faderTouched_.load(); };
    host.faderHasPos  = [this] { return faderHasPos_.load(); };
    host.faderPos     = [this] { return faderPos_.load(); };
    // The device has ONE V-Pot row and one cache for it.
    uf1spread::VpotCache vpotCache;
    host.emitVpotRow = [this, &vpotCache](const uf1spread::VpotRow& r, bool f) {
        if (f) vpotCache = uf1spread::VpotCache{};
        uf1spread::paintVpotRow(r, vpotCache,
            [this](std::vector<std::uint8_t> fr) { dev_.send(std::move(fr)); });
    };
    // ⚠ Soft keys: the extension paints its side-car bank from the bindings, and
    // ORC does not run keys through the bindings engine yet (the next step).
    // Until then the overview's four names are emptied once, and STRIP's page
    // keys are written as plain labels, so nothing of REAPER's stays standing.
    std::array<std::string, 4> skShown{};
    bool skKnown = false;
    auto writeLabels = [this, &skShown, &skKnown](const std::array<std::string, 4>& want,
                                                  bool f) {
        for (std::uint8_t i = 0; i < 4; ++i) {
            if (!f && skKnown && want[i] == skShown[i]) continue;
            std::vector<std::uint8_t> p{ i };
            p.insert(p.end(), want[i].begin(), want[i].end());
            dev_.send(::uf1::buildScreen(::uf1::scr::kSoftKeyLabel, p));
            skShown[i] = want[i];
        }
        skKnown = true;
    };
    // The side-car bank, as the extension counts it.
    host.bankNow   = [this] { return scBank_.load(); };
    host.bankCount = [] {
        return std::max(1, uf8::bindings::uf1SideCarBankInUseCount(
                               uf8::bindings::kUf1SideCarSetRme));
    };
    // The overview's four names, from the bindings, through the same two rules
    // the extension uses (uf1SoftBankKeyLabel, uf1SoftKeyText).
    // ⚠ Names only: the key lamps still need the soft-key emitter, which the
    // extension shares with REAPER's own UF1 mode and has not moved yet.
    host.sideCarSoftKeys = [this, &writeLabels](bool f) {
        namespace bnd = uf8::bindings;
        const int bank = bnd::uf1SideCarBankBase(bnd::kUf1SideCarSetRme) + scBank_.load();
        std::array<std::string, 4> want{};
        for (int i = 0; i < 4; ++i)
            want[static_cast<std::size_t>(i)] =
                uf1SoftKeyText(bnd::uf1SoftBankKeyLabel(bank, i));
        writeLabels(want, f);
    };
    host.stripSoftKeys = [&writeLabels](const std::array<uf1spread::SkCell, 4>& cells,
                                        bool f, bool, bool) {
        std::array<std::string, 4> want{};
        for (std::size_t i = 0; i < 4; ++i)
            if (cells[i].haveLabel) want[i] = uf1SoftKeyText(cells[i].label);
        writeLabels(want, f);
    };
    // Header and meter: handed to the pacer thread below.
    host.publishCycle = [this](std::vector<std::vector<std::uint8_t>> meters,
                               std::vector<std::vector<std::uint8_t>> tail) {
        auto p = std::make_shared<uf1pace::Parts>();
        p->meters = std::move(meters);
        p->tail   = std::move(tail);
        std::lock_guard<std::mutex> lk(cycMx_);
        cycle_ = std::move(p);
    };

    const rmef::Out out{
        [this](std::vector<std::uint8_t> f) { ++sent_; dev_.send(std::move(f)); },
        [this](std::vector<std::uint8_t> f) { ++sent_; dev_.sendPriority(std::move(f)); },
    };
    rmef::Cache faceCache;
    // The handover check's own state, local to this loop (one surface, one loop).
    auto lastCheck = std::chrono::steady_clock::time_point{};
    bool yielding  = false;
    std::string lastOpenErr;

    while (!quit_.load()) {
        // Same rule as the extension's tick: a stale handle after a USB
        // re-enumeration is reopened rather than nursed. A device that was never
        // open is retried on the same path, every two seconds, for as long as
        // ORC runs — SSL 360 or REAPER may be holding it and may let go.
        // ⇨ REAPER ASKED FOR THE UF1 (Rea-Sixty's handover marker, OrcConfig.h).
        // Checked twice a second: let go, and do not reopen while it stands.
        {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastCheck >= std::chrono::milliseconds(500)) {
                lastCheck = now;
                const bool want = reaperWantsUf1();
                // ⛔ THE UF1 AND THE TOTALMIX PORT GO TOGETHER (Frank 25.09.:
                // "orc muss die totalmix osc bindung zu machen bei der
                // übergabe"). Both programs use Remote 3, so while ORC held
                // 7006, REAPER's side-car had the surface and no mixer: "RME
                // no TotalMix". Rea-Sixty retries a busy port every 3 s, so
                // closing ours is all it needs.
                if (want && !yielding) {
                    std::printf("ORC: REAPER asked for the UF1, letting go of it and of TotalMix\n");
                    std::fflush(stdout);
                    rme::manager().stop();
                }
                if (!want && yielding) {
                    std::printf("ORC: REAPER let go of the UF1, taking it and TotalMix back\n");
                    std::fflush(stdout);
                    rme::manager().start();
                    lastOpenTry = now - std::chrono::seconds(5);   // try at once
                }
                yielding = want;
            }
            if (yielding) {
                if (dev_.isOpen()) {
                    cycleActive_.store(false);
                    dev_.close();
                }
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    st_.open = false;
                    st_.serial.clear();
                    st_.error = "handed over to REAPER";
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
                continue;
            }
        }

        if (dev_.isOpen() && dev_.needsReopen()) {
            std::printf("ORC: the handle went stale, reopening\n");
            std::fflush(stdout);
            dev_.close();
        }
        if (!dev_.isOpen()) {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastOpenTry >= std::chrono::seconds(2)) {
                lastOpenTry = now;
                const bool ok = dev_.open();
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    st_.open   = ok;
                    st_.serial = ok ? dev_.serial() : std::string();
                    st_.error  = ok ? std::string() : dev_.lastError();
                }
                // Why it did not open, once per new reason, not every 2 s.
                if (!ok && dev_.lastError() != lastOpenErr) {
                    lastOpenErr = dev_.lastError();
                    std::printf("ORC: UF1 not open: %s\n", lastOpenErr.c_str());
                    std::fflush(stdout);
                }
                if (ok) {
                    lastOpenErr.clear();
                    std::printf("ORC: UF1 open, serial %s.\n", dev_.serial().c_str());
                    std::fflush(stdout);
                    // The device that just came up is blank: the painter must
                    // believe nothing it cached (the extension's g_uf1Gen bump).
                    force = true;
                    faceCache = rmef::Cache{};
                }
            }
        }

        const auto link = rme::manager().link();
        if (link != lastLink) {
            std::printf("ORC: link %s (%s)\n", linkName(link),
                        rme::manager().status().c_str());
            std::fflush(stdout);
            lastLink = link;
            if (link == rme::LinkState::Online) dumpModel(rme::manager().snapshot());
        }

        // ⇨ ONE PASS OF THE SIDE-CAR PAINTER, the extension's own. It paints
        // "no TotalMix" when the link is down, so it runs whenever the surface
        // is open, not only when the mixer answers.
        if (dev_.isOpen()) {
            rmef::paint(faceCache, in_, host, out, force);
            force = false;
            cycleActive_.store(true);
        } else {
            cycleActive_.store(false);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    cycleActive_.store(false);

    std::printf("\nORC: surface closing.\n");
    std::fflush(stdout);
}

// ⇨ THE PACER, ORC'S HALF. The cycle itself is the extension's
// (uf1pace::emitCycle); this is only the loop around it. The extension's loop
// is locked to the meter plug-in's frames and falls back to the ceiling when
// none arrive, which in the side-car is always, because the side-car never
// sets `seq`. So ORC runs on the ceiling, and the wire sees the same spacing.
void Surface::pacerLoop_()
{
    using namespace std::chrono;
    auto slot = steady_clock::now();
    while (!quit_.load()) {
        if (!cycleActive_.load() || !dev_.isOpen()) {
            std::this_thread::sleep_for(milliseconds(5));
            slot = steady_clock::now();
            continue;
        }
        std::this_thread::sleep_until(slot + uf1pace::kCeiling);
        slot = steady_clock::now();
        std::shared_ptr<const uf1pace::Parts> snap;
        {
            std::lock_guard<std::mutex> lk(cycMx_);
            snap = cycle_;
        }
        if (snap && dev_.isOpen()) uf1pace::emitCycle(dev_, *snap, slot);
    }
}

} // namespace orc
