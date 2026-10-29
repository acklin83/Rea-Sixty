#include "Uf8Surface.h"

#include "Bindings.h"
#include "DynaMountManager.h"
#include "HueManager.h"
#include "ObsManager.h"
#include "OrcConfig.h"
#include "OrcLog.h"
#include "Palette.h"
#include "Protocol.h"
#include "RoomCells.h"
#include "RmeSoftKeys.h"
#include "SelsetBank.h"
#include "Uf1SoftKeys.h"
#include "Uf8Fader.h"
#include "Uf8ModeViews.h"
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
constexpr std::uint8_t kSendPlugin1 = 0x48;  // ..0x4F: SEND/PLUG-IN, the submix
constexpr std::uint8_t kStripKeys  = 0x20;   // ..0x37: per strip SOLO, CUT, SEL
constexpr std::uint8_t kLayer1     = 0x40;
constexpr std::uint8_t kLayer2     = 0x41;
constexpr std::uint8_t kLayer3     = 0x42;
constexpr std::uint8_t kBtn360     = 0x46;
constexpr std::uint8_t kPlugin     = 0x50;
constexpr std::uint8_t kPageLeft   = 0x52;
constexpr std::uint8_t kPageRight  = 0x53;
constexpr std::uint8_t kFlip       = 0x54;
constexpr std::uint8_t kVpotBank   = 0x68;   // V-POT, then 1-5 = 0x69..0x6D
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
// A soft key held this long is a long press (the bindings engine's 500 ms).
constexpr std::int64_t kLongPressMs = 500;

constexpr int kPages = 6;   // V-POT, 1-5

// ⇨ THE KEYS THE USER BINDS (Frank 29.10.2026): QUICK 1-3, NAV / NUDGE / FOCUS,
// NORM / REC / AUTO. Each runs what the Soft Keys page puts on it, through the
// bindings engine, in every mode. Sleep on QUICK 1 from the factory.
constexpr std::uint8_t kQuick1 = 0x43, kQuick2 = 0x44, kQuick3 = 0x45;
constexpr std::uint8_t kSelNorm = 0x70, kSelRec = 0x71, kSelAuto = 0x72;
constexpr std::uint8_t kNav = 0x73, kNudge = 0x74, kFocus = 0x75;
struct UserKey { std::uint8_t id; uf8::Uf8GlobalLed led; };
constexpr UserKey kUserKeys[Uf8Surface::kUserKeyCount] = {
    { kQuick1,  uf8::Uf8GlobalLed::Quick1 }, { kQuick2, uf8::Uf8GlobalLed::Quick2 },
    { kQuick3,  uf8::Uf8GlobalLed::Quick3 },
    { kNav,     uf8::Uf8GlobalLed::Nav },    { kNudge,  uf8::Uf8GlobalLed::Nudge },
    { kFocus,   uf8::Uf8GlobalLed::Focus },
    { kSelNorm, uf8::Uf8GlobalLed::Norm },   { kSelRec, uf8::Uf8GlobalLed::Rec },
    { kSelAuto, uf8::Uf8GlobalLed::Auto },
};

// What ORC can run: a builtin it registered (TotalMix, sleep, the modifiers).
// Anything else on these keys is the factory layer orc.json inherited from
// Rea-Sixty (domain_cs, the encoder modes ...) and does nothing here.
const uf8::bindings::ActionSlot* orcSlot(const uf8::bindings::Binding& bd, int mod)
{
    namespace bnd = uf8::bindings;
    if (mod < 0 || mod >= bnd::kModifierCount) return nullptr;
    const bnd::ActionSlot& sl = bd.shortPress[mod];
    if (sl.type != bnd::ActionType::Builtin || !bnd::builtinRegistered(sl.action)) return nullptr;
    return &sl;
}

// The colour bar's palette index for an RGB, as ColorSync does it.
std::uint8_t barIndex(std::uint32_t rgb)
{
    return rgb == 0 ? 0x00 : uf8::quantize(rgb);
}

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
    cLed_.fill(-1);
    cUser_.fill(-1);
}

void Uf8Surface::send_(const u8s::Writes& w)
{
    auto& mgr = rme::manager();
    for (const auto& [addr, val] : w) mgr.send(addr, val);
}

void Uf8Surface::setMode_(Mode m)
{
    const Mode cur = static_cast<Mode>(mode_.load());
    // The same key again: back to TotalMix, ORC's ground state (plan 2.2).
    const Mode next = (m == cur && m != Mode::TotalMix) ? Mode::TotalMix : m;
    if (next == cur) return;
    mode_.store(static_cast<int>(next));
    side_.chanView.store(false);
    for (int s = 0; s < 8; ++s) dynPending_[s].store(false);
    // ⛔ THE EDGE REPAINTS EVERYTHING: both painters dedup against what they
    // believe the device shows, so leaving one mode without this would leave its
    // text on the glass (the Hue lesson of 27.08.2026).
    repaint_.store(true);
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

// ⇨ THE SOFT KEYS BELONG TO THE MODE (Frank 29.10.2026: "Hue Presets sollten im
// Hue mode erscheinen ... Dyna Sachen im Dyna Mode"). TotalMix: the bank V-POT /
// 1-5 picked, which is the UF1's bank too, both halves at once (keys 1-4 the
// first, 5-8 the second); OBS scenes are a bank kind there. Hue: the eight
// scenes. DynaMount: the eight presets. The same three-state lamps as everywhere.
std::array<u8s::SoftCell, 8> Uf8Surface::softCells_(Mode m)
{
    namespace bnd = uf8::bindings;
    if (m == Mode::TotalMix)
        return rme::softkeys::uf8Row(bnd::uf1SideCarBankBase(bnd::kUf1SideCarSetRme) + scBank_.load());
    std::array<u8s::SoftCell, 8> soft{};
    for (int k = 0; k < 8; ++k) {
        const roomcells::Cell c = (m == Mode::Hue) ? roomcells::hueScene(k)
                                                   : roomcells::dynaPreset(k);
        if (!c.present) continue;
        auto& o = soft[static_cast<size_t>(k)];
        o.label = c.label;
        o.lamp  = c.led;
        o.rgb   = c.hasRgb ? c.rgb : 0xFFFFFFu;
    }
    return soft;
}

void Uf8Surface::paintTotalMix_(std::array<uf8strips::View, 8>& views, std::array<int, 8>& colour,
                                std::array<std::uint8_t, 16>& vu)
{
    auto& mgr = rme::manager();
    const rme::Config cfg = mgr.config();
    const rme::State  st  = mgr.snapshot();
    u8s::follow(st, in_, side_);
    const u8s::Frame fr = u8s::frame(st, in_, side_, cfg, softCells_(Mode::TotalMix));
    views  = fr.strips;
    colour = fr.colour;
    vu     = fr.vu;
}

// Hue and DynaMount: the cells are Uf8ModeViews' (Rea-Sixty's own), the strips
// without a lamp or a stand dark, the soft keys the current page.
void Uf8Surface::paintMode_(Mode m, std::array<uf8strips::View, 8>& views, std::array<int, 8>& colour)
{
    const auto soft = softCells_(m);
    const bool flip = flip_.load();
    for (int s = 0; s < 8; ++s) {
        uf8strips::View& v = views[static_cast<size_t>(s)];
        const auto& k = soft[static_cast<size_t>(s)];
        v.slotLabel = utf8ToLatin1(k.label);
        if (v.slotLabel.size() > uf8::kUf8ScribbleChars) v.slotLabel.resize(uf8::kUf8ScribbleChars);
        v.topKey    = k.lamp;
        v.topKeyRgb = k.rgb;
        uf8modes::Cells c;
        std::uint32_t rgb = 0;
        if (m == Mode::Hue) {
            auto& hm = uf8::hue::manager();
            const int slot = hm.slotForStrip(s);
            if (slot < 0) { v.ringMode = 0x03; continue; }
            c   = uf8modes::hue(hm, slot, flip);
            rgb = uf8modes::hueRgb(hm, slot);
        } else {
            auto& dm = uf8::dynamount::manager();
            const int mnt = dm.mountForStrip(s);
            if (mnt < 0) { v.ringMode = 0x03; continue; }
            c   = uf8modes::dyna(dm, mnt, flip, dynPending_[s].load(), dynPendingNorm_[s].load());
            rgb = uf8modes::dynaRgb(dm, mnt);
        }
        v.upper     = c.upper;
        v.csType    = c.csType;
        v.chanNum   = c.chanNum;
        v.faderText = c.faderText;
        v.faderUnit = "";
        v.valueLine = c.valueLine;
        v.drive     = true;
        v.faderPb   = static_cast<std::uint16_t>(std::clamp(c.fader01, 0.0, 1.0) * uf8fader::kPbMax + 0.5);
        v.ring      = uf8::vpotPosUnipolar(c.ring);
        v.ringMode  = 0x01;
        colour[static_cast<size_t>(s)] = barIndex(rgb);
    }
}

void Uf8Surface::paintLeds_()
{
    using G = uf8::Uf8GlobalLed;
    using S = uf8::GlobalLedState;
    const int mode = mode_.load();
    int sp[8] = {};
    if (mode == static_cast<int>(Mode::TotalMix)) {
        const rme::State st = rme::manager().snapshot();
        for (int k = 0; k < 8; ++k) sp[k] = u8s::submixKeyLamp(st, in_, k);
    }
    const struct { G led; int state; } want[] = {
        { G::Layer1,   mode == 0 ? 2 : 1 },
        { G::Layer2,   mode == 1 ? 2 : 1 },
        { G::Layer3,   mode == 2 ? 2 : 1 },
        { G::Flip,     flip_.load() ? 2 : 1 },
        { G::Plugin,   side_.chanView.load() ? 2 : 1 },
        { G::Fine,     fineHeld_.load() ? 2 : 1 },
        { G::VPotBank, bankKeyLamp_(0) },
        { G::Soft1,    bankKeyLamp_(1) },
        { G::Soft2,    bankKeyLamp_(2) },
        { G::Soft3,    bankKeyLamp_(3) },
        { G::Soft4,    bankKeyLamp_(4) },
        { G::Soft5,    bankKeyLamp_(5) },
        // SEND/PLUG-IN: the submix keys, in TotalMix only.
        { G::SendPlugin1, sp[0] }, { G::SendPlugin2, sp[1] },
        { G::SendPlugin3, sp[2] }, { G::SendPlugin4, sp[3] },
        { G::SendPlugin5, sp[4] }, { G::SendPlugin6, sp[5] },
        { G::SendPlugin7, sp[6] }, { G::SendPlugin8, sp[7] },
    };
    static_assert(std::size(want) == std::tuple_size<decltype(cLed_)>::value);
    for (size_t i = 0; i < std::size(want); ++i) {
        if (cLed_[i] == want[i].state) continue;
        cLed_[i] = want[i].state;
        auto f = uf8::buildUf8GlobalLed(want[i].led,
            want[i].state == 2 ? S::Bright : want[i].state == 1 ? S::Dim : S::Off);
        if (!f.ff38.empty())   dev_.send(std::move(f.ff38));
        if (!f.ff39.empty())   dev_.send(std::move(f.ff39));
        if (!f.legacy.empty()) dev_.send(std::move(f.legacy));
    }

    // The user's keys: the binding's colours, active bright, otherwise dim, dark
    // when ORC runs nothing there (as the UF1's transport keys, Surface.cpp).
    namespace bnd = uf8::bindings;
    const int layer = bnd::getActiveLayer();
    const int mod   = static_cast<int>(bnd::currentModifierSnapshot());
    for (int i = 0; i < kUserKeyCount; ++i) {
        const auto& u = kUserKeys[i];
        const bnd::Binding bd = bnd::getBinding(layer, bnd::fromUf8DeviceId(u.id));
        std::int64_t want = 0;
        S state = S::Off;
        std::uint32_t rgb = 0;
        if (const bnd::ActionSlot* sl = orcSlot(bd, mod)) {
            const bool on = bnd::bindingHasActiveSlotForSet(bd, mod);
            rgb = uf1sk::bindingLedColour(bd, *sl, on);
            if (rgb) state = on ? S::Bright : S::Dim;
        }
        want = (static_cast<std::int64_t>(state) << 32) | rgb;
        if (cUser_[static_cast<size_t>(i)] == want) continue;
        cUser_[static_cast<size_t>(i)] = want;
        auto f = state == S::Off ? uf8::buildUf8GlobalLed(u.led, S::Off)
                                 : uf8::buildUf8GlobalLed(u.led, state, uf8::ledColourForTrackRgb(rgb));
        if (!f.ff38.empty())   dev_.send(std::move(f.ff38));
        if (!f.ff39.empty())   dev_.send(std::move(f.ff39));
        if (!f.legacy.empty()) dev_.send(std::move(f.legacy));
    }
}

void Uf8Surface::seedUserKeys()
{
    namespace bnd = uf8::bindings;
    const bnd::ButtonId q1 = bnd::fromUf8DeviceId(kQuick1);
    for (int layer = 0; layer < 3; ++layer) {
        bnd::Binding bd = bnd::getBinding(layer, q1);
        bnd::ActionSlot& sl = bd.shortPress[static_cast<int>(bnd::Modifier::Plain)];
        // Only over the inherited factory: an empty key is the user's choice.
        if (bnd::slotIsEmpty(sl) || orcSlot(bd, static_cast<int>(bnd::Modifier::Plain))) continue;
        sl = bnd::ActionSlot{};
        sl.type   = bnd::ActionType::Builtin;
        sl.action = "sleep_now";
        bd.label.clear();
        bnd::setBinding(layer, q1, bd);
    }
}

void Uf8Surface::paint_()
{
    if (repaint_.exchange(false)) {
        // Start from a dark unit, then every cell again.
        for (auto& f : uf8::blankUnitFrames()) dev_.send(std::move(f));
        resetCache_();
    }
    const Mode m = static_cast<Mode>(mode_.load());
    std::array<uf8strips::View, 8> views{};
    std::array<int, 8>             colour{};
    std::array<std::uint8_t, 16>   vu{};
    if (m == Mode::TotalMix) paintTotalMix_(views, colour, vu);
    else                     paintMode_(m, views, colour);

    const std::int64_t now = nowMs_();
    std::array<std::uint16_t, 8> bar{};
    std::array<std::uint8_t, 8>  mode{};
    std::uint16_t selMask = 0;
    for (int s = 0; s < 8; ++s) {
        const auto& v = views[static_cast<size_t>(s)];
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

    // The colour bars, one frame for eight.
    {
        std::array<std::uint8_t, 8> pal{};
        bool changed = false;
        for (int s = 0; s < 8; ++s) {
            pal[static_cast<size_t>(s)] = static_cast<std::uint8_t>(colour[static_cast<size_t>(s)]);
            if (cColour_[s] != colour[static_cast<size_t>(s)]) changed = true;
        }
        if (changed) {
            for (int s = 0; s < 8; ++s) cColour_[s] = colour[static_cast<size_t>(s)];
            dev_.send(uf8::buildColorCommand(pal));
        }
    }
    // Which SEL is the lit one (Protocol.h: without it the firmware draws white).
    if (selMask != cSelMask_) {
        cSelMask_ = selMask;
        dev_.send(uf8::buildSelectedStripBitmask(selMask));
    }
    // The meters: TotalMix' peak levels, when it sends them.
    if (vu != cVu_) {
        cVu_ = vu;
        auto f = uf8::buildVuMeter(vu);
        dev_.send(std::move(f[0]));
        dev_.send(std::move(f[1]));
    }
    paintLeds_();
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
    const Mode m = static_cast<Mode>(mode_.load());

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
            return;
        }
        touched_[s].store(false);
        releasedMs_[s].store(nowMs_());
        // Release: a stand moves once now (FLIP picks the axis); a lamp gets its
        // last value as the released one, as Rea-Sixty does.
        if (m == Mode::Dyna) {
            auto& dm = uf8::dynamount::manager();
            const int mnt = dm.mountForStrip(s);
            if (mnt >= 0 && dynPending_[s].exchange(false)) {
                const double norm = dynPendingNorm_[s].load();
                if (flip_.load()) dm.setDistance(mnt, norm);
                else              dm.setHorizontal(mnt, norm);
            }
        } else if (m == Mode::Hue) {
            auto& hm = uf8::hue::manager();
            const int slot = hm.slotForStrip(s);
            if (slot >= 0) hm.setFader(slot, hm.liveBri01(slot), /*released=*/true);
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
        if (m == Mode::Hue) {
            auto& hm = uf8::hue::manager();
            const int slot = hm.slotForStrip(s);
            if (slot >= 0) hm.setFader(slot, lin, /*released=*/false);   // streamed, the manager paces it
        } else if (m == Mode::Dyna) {
            if (uf8::dynamount::manager().mountForStrip(s) >= 0) {
                dynPendingNorm_[s].store(lin);
                dynPending_[s].store(true);
            }
        } else {
            const rme::State st = mgr.snapshot();
            send_(u8s::fader(st, in_, side_, s, lin));
        }
        return;
    }
    if (cmd == 0x24 && f.b[2] == 0x02) {               // V-Pot rotation
        if (sleep_.wake(nowMs_(), true)) return;       // a turn on a dark surface only wakes
        const int pot   = f.b[3];
        const int delta = uf8frames::vpotDelta(f.b[4]);
        const rme::State st = mgr.snapshot();
        const rme::Config cfg = mgr.config();
        // The channel encoder is Main in every mode: monitor volume never hides
        // behind a light.
        if (pot == kChannelEncoder) {
            send_(u8s::mainVolume(st, in_, cfg, delta, scale_()));
            return;
        }
        if (pot >= 8) return;
        if (m == Mode::Hue) {
            auto& hm = uf8::hue::manager();
            const int slot = hm.slotForStrip(pot);
            if (slot >= 0) hm.nudgePot(slot, delta, flip_.load());
        } else if (m == Mode::Dyna) {
            auto& dm = uf8::dynamount::manager();
            const int mnt = dm.mountForStrip(pot);
            if (mnt >= 0) dm.nudgeRotation(mnt, delta);
        } else {
            send_(u8s::vpot(st, in_, side_, cfg, pot, delta, scale_(), nowMs_()));
        }
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

// A soft key of the mode. TotalMix keys follow press and release (the bindings
// decide); scenes and presets act on release, so a long press can mean
// something else.
void Uf8Surface::softKey_(Mode m, int k, bool pressed)
{
    if (m == Mode::TotalMix) {
        namespace bnd = uf8::bindings;
        rme::softkeys::press(bnd::uf1SideCarBankBase(bnd::kUf1SideCarSetRme) + scBank_.load(),
                             k / 4, k % 4, pressed);
        return;
    }
    if (pressed) { keyDownMs_[k].store(nowMs_()); return; }
    const bool longPress = nowMs_() - keyDownMs_[k].load() >= kLongPressMs;
    if (m == Mode::Hue) {
        // Hue: push recalls, long starts it moving (Rea-Sixty's HueScenes bank).
        uf8::hue::manager().recallSceneSlot(k, /*dynamic=*/longPress);
    } else if (m == Mode::Dyna) {
        // DynaMount: the Selection Sets' gestures (selbank::op, as Rea-Sixty's
        // DynaMountPresets bank): push recalls, long stores into an empty slot,
        // FINE + push stores over.
        auto& dm = uf8::dynamount::manager();
        const int gesture = longPress ? 4 : (fineHeld_.load() ? 1 : 0);
        switch (selbank::op(gesture, dm.preset(k).defined())) {
            case selbank::Op::Recall: dm.recallPreset(k); break;
            case selbank::Op::Save:   if (dm.definedCount() > 0) dm.storePreset(k); break;
            case selbank::Op::Clear:  dm.clearPreset(k); break;
            default: break;
        }
    }
}

// V-POT / 1-5 in TotalMix: the bank, 1-6, when there is one (Soft Keys page).
// The UF1 shows the same bank, from its first half, as after its own < >.
int Uf8Surface::bankKeyLamp_(int idx) const
{
    namespace bnd = uf8::bindings;
    if (static_cast<Mode>(mode_.load()) != Mode::TotalMix) return 0;
    if (idx >= bnd::uf1SideCarBankInUseCount(bnd::kUf1SideCarSetRme)) return 0;
    return scBank_.load() == idx ? 2 : 1;
}

void Uf8Surface::onButton_(std::uint8_t id, bool pressed)
{
    auto& mgr = rme::manager();
    const rme::State  st  = mgr.snapshot();
    const rme::Config cfg = mgr.config();
    const Mode m = static_cast<Mode>(mode_.load());

    if (id == kFine) { fineHeld_.store(pressed); return; }
    for (const auto& u : kUserKeys) {
        if (u.id != id) continue;
        namespace bnd = uf8::bindings;
        const bnd::ButtonId b = bnd::fromUf8DeviceId(id);
        const int mod = static_cast<int>(bnd::currentModifierSnapshot());
        if (orcSlot(bnd::getBinding(bnd::getActiveLayer(), b), mod)) bnd::dispatch(b, pressed);
        return;
    }
    if (id >= kTopSoft1 && id < kTopSoft1 + 8) {
        const int k = id - kTopSoft1;
        if (m == Mode::TotalMix && side_.chanView.load()) {
            if (pressed) u8s::pickPage(st, in_, side_, cfg, k);
            return;
        }
        softKey_(m, k, pressed);
        return;
    }
    if (!pressed) return;

    // The keys that are the same in every mode.
    switch (id) {
        case kLayer1: setMode_(Mode::TotalMix); return;
        case kLayer2: setMode_(Mode::Hue);      return;
        case kLayer3: setMode_(Mode::Dyna);     return;
        case kFlip:   flip_.store(!flip_.load()); return;
        case kBtn360: settings_.store(true);    return;
        default: break;
    }
    if (id >= kVpotBank && id < kVpotBank + kPages) {   // V-POT, 1-5: the TotalMix bank
        if (bankKeyLamp_(id - kVpotBank) != 0) {
            scBank_.store(id - kVpotBank);
            in_.skHalf.store(0);
        }
        return;
    }

    if (m == Mode::Hue) {
        auto& hm = uf8::hue::manager();
        if (id >= kStripKeys && id < kStripKeys + 24) {
            const int slot = hm.slotForStrip((id - kStripKeys) / 3);
            if (slot < 0) return;
            switch ((id - kStripKeys) % 3) {
                case 0: hm.soloLight(slot); break;   // SOLO: this one on, the others dark
                case 1: hm.toggleOn(slot);  break;   // CUT: on / off
                default: break;                       // SEL picks the UF1's lamp in Rea-Sixty only
            }
            return;
        }
        if (id >= kVpotPush1 && id < kVpotPush1 + 8) {
            const int slot = hm.slotForStrip(id - kVpotPush1);
            if (slot >= 0) hm.pushPot(slot);
        }
        return;
    }
    if (m == Mode::Dyna) return;   // a stand has no keys of its own (as in Rea-Sixty)

    // TotalMix.
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
    if (id >= kSendPlugin1 && id < kSendPlugin1 + 8) {
        u8s::pickSubmix(st, in_, cfg, id - kSendPlugin1);
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
        default: break;
    }
    send_(w);
}

} // namespace orc
