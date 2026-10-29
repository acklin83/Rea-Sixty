#include "Uf8Surface.h"

#include "Bindings.h"
#include "OrcConfig.h"
#include "OrcLog.h"
#include "Protocol.h"
#include "RmeSoftKeys.h"
#include "Uf8Fader.h"
#include "Uf8Strips.h"

#include <algorithm>
#include <cstdio>

namespace rme  = reasixty::rme;
namespace rmeu = reasixty::rme::uf1;
namespace rmei = reasixty::rme::input;
namespace u8s  = reasixty::rme::uf8side;

namespace orc {

namespace {

// UF8 button ids (Bindings.cpp fromUf8DeviceId, Protocol.h).
constexpr std::uint8_t kVpotPush1  = 0x08;   // ..0x0F
constexpr std::uint8_t kTopSoft1   = 0x18;   // ..0x1F
constexpr std::uint8_t kStripKeys  = 0x20;   // ..0x37: per strip SOLO, CUT, SEL
constexpr std::uint8_t kLayer1     = 0x40;
constexpr std::uint8_t kBtn360     = 0x46;
constexpr std::uint8_t kPlugin     = 0x50;
constexpr std::uint8_t kPageLeft   = 0x52;
constexpr std::uint8_t kPageRight  = 0x53;
constexpr std::uint8_t kFine       = 0x6F;
constexpr std::uint8_t kBankLeft   = 0x78;
constexpr std::uint8_t kBankRight  = 0x79;
constexpr std::uint8_t kZoomUp     = 0x7A;
constexpr std::uint8_t kZoomLeft   = 0x7B;
constexpr std::uint8_t kZoomCenter = 0x7C;
constexpr std::uint8_t kZoomRight  = 0x7D;
constexpr std::uint8_t kZoomDown   = 0x7E;
constexpr std::uint8_t kChannelEncoder = 0x08;   // V-Pot rotation id

// Release held this long counts as touched still (RmeFace, the UF1 side-car).
constexpr std::int64_t kTouchHoldMs = 150;

} // namespace

Uf8Surface::Uf8Surface(rmei::State& in, std::atomic<int>& scBank,
                       surfsleep::Clock& sleep, std::atomic<bool>& settings)
    : in_(in), scBank_(scBank), sleep_(sleep), settings_(settings)
{
    resetCache_();
    dev_.setRawInputHandler([this](const std::uint8_t* d, std::size_t n) { onRaw_(d, n); });
}

Uf8Surface::~Uf8Surface() { close(); }

std::int64_t Uf8Surface::nowMs_() const
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Uf8Surface::close()
{
    if (dev_.isOpen()) dev_.close();
    std::lock_guard<std::mutex> lk(mu_);
    st_.open = false;
}

Uf8Status Uf8Surface::status() const
{
    std::lock_guard<std::mutex> lk(mu_);
    return st_;
}

void Uf8Surface::resetCache_()
{
    for (int s = 0; s < 8; ++s) {
        cUpper_[s].assign(1, '\x01'); cCs_[s].assign(1, '\x01'); cChan_[s].assign(1, '\x01');
        cFt_[s].assign(1, '\x01');    cFu_[s].assign(1, '\x01'); cVl_[s].assign(1, '\x01');
        cSl_[s].assign(1, '\x01');
        cTop_[s] = -1; cSel_[s] = -1; cCut_[s] = -1; cSolo_[s] = -1;
        cPb_[s] = 0xFFFF;
        cColour_[s] = -1;
        engaged_[s].store(false);
    }
    ringInit_ = false;
    cVu_.fill(0xFF);
    cSelMask_ = -1;
}

void Uf8Surface::send_(const u8s::Writes& w)
{
    auto& mgr = rme::manager();
    for (const auto& [addr, val] : w) mgr.send(addr, val);
}

// ── open, retry, paint ───────────────────────────────────────────────────────

void Uf8Surface::tick(bool yielding)
{
    if (yielding) {
        if (dev_.isOpen()) {
            dev_.close();
            orc::logLine("ORC: UF8 handed over to REAPER\n");
            std::fflush(stdout);
        }
        std::lock_guard<std::mutex> lk(mu_);
        st_.open  = false;
        st_.error = "handed over to REAPER";
        return;
    }
    if (dev_.isOpen() && dev_.needsReopen()) {
        orc::logLine("ORC: reopening the UF8\n");
        std::fflush(stdout);
        dev_.close();
        lastTry_ = {};
    }
    if (!dev_.isOpen()) {
        const auto now = std::chrono::steady_clock::now();
        if (now - lastTry_ < std::chrono::seconds(2)) return;
        lastTry_ = now;
        asm_.residual.clear();
        const bool ok = dev_.open();
        {
            std::lock_guard<std::mutex> lk(mu_);
            st_.open  = ok;
            st_.error = ok ? std::string() : dev_.lastError();
        }
        if (!ok) {
            if (dev_.lastError() != lastErr_) {
                lastErr_ = dev_.lastError();
                orc::logLine("ORC: UF8 not open: %s\n", lastErr_.c_str());
                std::fflush(stdout);
            }
            return;
        }
        lastErr_.clear();
        orc::logLine("ORC: UF8 open.\n");
        std::fflush(stdout);
        // A device that just came up believes nothing we cached.
        resetCache_();
        repaint_.store(true);
        if (sleep_.asleep.load()) pushSleep(true);
    }
    paint_();
}

void Uf8Surface::pushSleep(bool asleep)
{
    if (!dev_.isOpen()) return;
    for (auto& f : surfsleep::uf8Frames(asleep, surfsleep::kUf8FullLed, surfsleep::kUf8FullLcd))
        dev_.send(std::move(f));
}

void Uf8Surface::paint_()
{
    if (repaint_.exchange(false)) {
        // Start from a dark unit, then every cell again.
        for (auto& f : uf8::blankUnitFrames()) dev_.send(std::move(f));
        resetCache_();
    }
    auto& mgr = rme::manager();
    const rme::Config cfg = mgr.config();
    const rme::State  st  = mgr.snapshot();

    u8s::follow(st, in_, side_);

    // The soft keys in the overview: the side-car's current RME bank, both
    // halves at once (keys 1-4 the first, 5-8 the second; plan 2.1).
    std::array<u8s::SoftCell, 8> soft{};
    {
        namespace bnd = uf8::bindings;
        const int bank = bnd::uf1SideCarBankBase(bnd::kUf1SideCarSetRme) + scBank_.load();
        for (int h = 0; h < 2; ++h) {
            if (h == 1 && !rme::softkeys::hasSecondHalf(bank)) break;
            const auto row = rme::softkeys::row(bank, h);
            for (int k = 0; k < 4; ++k) {
                const auto& c = row[static_cast<size_t>(k)];
                u8s::SoftCell& o = soft[static_cast<size_t>(h * 4 + k)];
                if (!c.haveLabel) continue;
                o.label = c.label;
                o.lamp  = (c.on || c.colBright) ? 2 : 1;
                o.rgb   = c.hasColour ? c.colRgb : 0xFFFFFFu;
            }
        }
    }

    const u8s::Frame fr = u8s::frame(st, in_, side_, cfg, soft);
    const std::int64_t now = nowMs_();
    std::array<std::uint16_t, 8> bar{};
    std::array<std::uint8_t, 8>  mode{};
    std::uint16_t selMask = 0;
    for (int s = 0; s < 8; ++s) {
        const auto& v = fr.strips[static_cast<size_t>(s)];
        const bool touched = touched_[s].load() || (now - releasedMs_[s].load() < kTouchHoldMs);
        bool engaged = engaged_[s].load();
        uf8strips::paintStrip(
            uf8strips::Cells{ cUpper_[s], cCs_[s], cChan_[s], cFt_[s], cFu_[s], cVl_[s], cSl_[s],
                              cTop_[s], cSel_[s], cCut_[s], cSolo_[s], cPb_[s] },
            v, static_cast<std::uint8_t>(s), touched, engaged,
            [this](std::vector<std::uint8_t> f) { dev_.send(std::move(f)); },
            [this](uf8::LedColourFrames f) {
                if (!f.ff38.empty())   dev_.send(std::move(f.ff38));
                if (!f.ff39.empty())   dev_.send(std::move(f.ff39));
                if (!f.legacy.empty()) dev_.send(std::move(f.legacy));
            });
        engaged_[s].store(engaged);
        bar[static_cast<size_t>(s)]  = v.ring;
        mode[static_cast<size_t>(s)] = v.ringMode;
        if (v.sel) selMask = static_cast<std::uint16_t>(selMask | (1u << s));
    }
    uf8strips::paintRings(cBar_, cMode_, ringInit_, bar, mode,
                          [this](std::vector<std::uint8_t> f) { dev_.send(std::move(f)); });
    ringInit_ = true;

    // The colour bars: TotalMix' colour through colourMap, one frame for eight.
    {
        std::array<std::uint8_t, 8> pal{};
        bool changed = false;
        for (int s = 0; s < 8; ++s) {
            pal[static_cast<size_t>(s)] = static_cast<std::uint8_t>(fr.colour[static_cast<size_t>(s)]);
            if (cColour_[s] != fr.colour[static_cast<size_t>(s)]) changed = true;
        }
        if (changed) {
            for (int s = 0; s < 8; ++s) cColour_[s] = fr.colour[static_cast<size_t>(s)];
            dev_.send(uf8::buildColorCommand(pal));
        }
    }
    // Which SEL is the lit one (Protocol.h: without it the firmware draws white).
    if (selMask != cSelMask_) {
        cSelMask_ = selMask;
        dev_.send(uf8::buildSelectedStripBitmask(selMask));
    }
    // The meters: TotalMix' peak levels, when it sends them.
    if (fr.vu != cVu_) {
        cVu_ = fr.vu;
        auto vu = uf8::buildVuMeter(fr.vu);
        dev_.send(std::move(vu[0]));
        dev_.send(std::move(vu[1]));
    }
}

// ── input, on the device thread ──────────────────────────────────────────────

void Uf8Surface::onRaw_(const std::uint8_t* data, std::size_t len)
{
    std::vector<uf8frames::Frame> frames;
    uf8frames::feed(asm_, data, len, frames, [](uf8frames::Skip, const std::uint8_t*, std::size_t) {});
    for (const auto& f : frames) onFrame_(f);
}

void Uf8Surface::onFrame_(const uf8frames::Frame& f)
{
    const std::uint8_t cmd = f.b[1];
    auto& mgr = rme::manager();

    if (cmd == 0x20 && f.b[2] == 0x02) {               // fader touch: FF 20 02 strip state
        const int s = f.b[3];
        if (s > 7) return;
        const bool on = f.b[4] != 0;
        // A hand on a dark surface wakes it and is used at once, as on the UF1.
        (void)sleep_.wake(nowMs_(), on);
        if (on) {
            touched_[s].store(true);
            engaged_[s].store(false);
            dev_.sendPriority(uf8fader::limp(static_cast<std::uint8_t>(s)));
        } else {
            touched_[s].store(false);
            releasedMs_[s].store(nowMs_());
        }
        return;
    }
    if (cmd == 0x21 && f.b[2] == 0x03) {               // fader position, under the hand only
        const int s = f.b[3];
        if (s > 7 || !touched_[s].load()) return;
        const std::uint16_t pb14 = static_cast<std::uint16_t>((f.b[4] & 0x7F) | ((f.b[5] & 0x7F) << 7));
        // No grab here (a controller, like the MIDI CC layer): the stream with
        // bit 7 and the stops.
        const uf8fader::Read rd = uf8fader::read(pb14, (f.b[4] & 0x80) != 0, false);
        if (!rd.usable) return;
        dev_.send(uf8fader::echo(static_cast<std::uint8_t>(s), rd.pbOut));
        const double lin = static_cast<double>(rd.pbOut) / static_cast<double>(uf8fader::kPbMax);
        const rme::State st = mgr.snapshot();
        send_(u8s::fader(st, in_, side_, s, lin));
        return;
    }
    if (cmd == 0x24 && f.b[2] == 0x02) {               // V-Pot rotation
        if (sleep_.wake(nowMs_(), true)) return;       // a turn on a dark surface only wakes
        const int pot   = f.b[3];
        const int delta = uf8frames::vpotDelta(f.b[4]);
        const rme::State st = mgr.snapshot();
        const rme::Config cfg = mgr.config();
        if (pot == kChannelEncoder) {
            send_(u8s::mainVolume(st, in_, cfg, delta, scale_()));
            return;
        }
        if (pot < 8) send_(u8s::vpot(st, in_, side_, cfg, pot, delta, scale_(), nowMs_()));
        return;
    }
    if (cmd == 0x22 && f.b[2] == 0x03) {               // button: FF 22 03 id 00 state
        const std::uint8_t id = f.b[3];
        const bool pressed = f.b[5] == 0x01;
        const bool woke = sleep_.wake(nowMs_(), pressed);
        if (wakeSwallow_.button(id, pressed, woke)) return;
        onButton_(id, pressed);
        return;
    }
}

void Uf8Surface::onButton_(std::uint8_t id, bool pressed)
{
    auto& mgr = rme::manager();
    const rme::State  st  = mgr.snapshot();
    const rme::Config cfg = mgr.config();

    if (id == kFine) { fineHeld_.store(pressed); return; }
    if (!pressed) {
        // A soft key's release belongs to its press (Press-type bindings).
        if (id >= kTopSoft1 && id < kTopSoft1 + 8 && !side_.chanView.load()) {
            namespace bnd = uf8::bindings;
            const int k = id - kTopSoft1;
            rme::softkeys::press(bnd::uf1SideCarBankBase(bnd::kUf1SideCarSetRme) + scBank_.load(),
                                 k / 4, k % 4, false);
        }
        return;
    }

    // SOLO / CUT / SEL per strip.
    if (id >= kStripKeys && id < kStripKeys + 24) {
        const int s = (id - kStripKeys) / 3;
        switch ((id - kStripKeys) % 3) {
            case 0: send_(u8s::solo(st, in_, side_, s)); break;
            case 1: send_(u8s::cut(st, in_, side_, s));  break;
            case 2: u8s::sel(st, in_, side_, s);          break;
        }
        return;
    }
    if (id >= kVpotPush1 && id < kVpotPush1 + 8) {
        send_(u8s::vpotPush(st, in_, side_, cfg, id - kVpotPush1, nowMs_()));
        return;
    }
    if (id >= kTopSoft1 && id < kTopSoft1 + 8) {
        const int k = id - kTopSoft1;
        if (side_.chanView.load()) {
            u8s::pickPage(st, in_, side_, cfg, k);
        } else {
            namespace bnd = uf8::bindings;
            rme::softkeys::press(bnd::uf1SideCarBankBase(bnd::kUf1SideCarSetRme) + scBank_.load(),
                                 k / 4, k % 4, true);
        }
        return;
    }
    rmei::Writes w;
    switch (id) {
        case kBankLeft:   u8s::shiftWindow(st, in_, side_, -8); break;
        case kBankRight:  u8s::shiftWindow(st, in_, side_, +8); break;
        case kPageLeft:
            if (side_.chanView.load()) u8s::stepPageGroup(st, in_, side_, cfg, -1);
            else                       u8s::shiftWindow(st, in_, side_, -1);
            break;
        case kPageRight:
            if (side_.chanView.load()) u8s::stepPageGroup(st, in_, side_, cfg, +1);
            else                       u8s::shiftWindow(st, in_, side_, +1);
            break;
        // The nav cross, as on the UF1 (RmeInput::button, Frank 29.10.): rows,
        // submix, TotalMix' window.
        case kZoomUp:     rmei::stepRow(in_, -1); break;
        case kZoomDown:   rmei::stepRow(in_, +1); break;
        case kZoomLeft:   rmei::stepSubmix(in_, st, cfg, -1); break;
        case kZoomRight:  rmei::stepSubmix(in_, st, cfg, +1); break;
        case kZoomCenter: {
            rmei::Host h;
            h.online = [] { return rme::manager().link() == rme::LinkState::Online; };
            rmei::toggleWindow(in_, h, w);
            break;
        }
        case kPlugin:     u8s::toggleChannelView(st, in_, side_); break;
        // Layer 1 is TotalMix, ORC's ground state; Hue and DynaMount come on
        // Layer 2 and 3 with stage 6.
        case kLayer1:     side_.chanView.store(false); break;
        case kBtn360:     settings_.store(true); break;
        default: break;
    }
    send_(w);
}

} // namespace orc
