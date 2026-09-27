// The RME side-car's soft keys and where their banks come from:
// src/RmeSoftKeys.cpp and the side-car source in src/Bindings.cpp.
//
// ⛔ WHAT IS PINNED, AND WHY EACH ONE:
//  · Rea-Sixty takes banks 10..19 from ORC's file and NOTHING else from it. A
//    DAW bank that ORC's orc.json also carries (its inherited factory) must not
//    land on Rea-Sixty's DAW view.
//  · once the banks are foreign, our own save leaves them out. Otherwise the two
//    files hold the same banks again and the next edit in ORC is overwritten.
//  · a load of our own file puts them back from the source, because load
//    replaces the whole config, banks 10.. included.
//  · no file = empty banks (no ORC, no RME banks).
//  · 5-8 shows the bank's second half: the Shift set on a static bank, items
//    5..8 on a TotalMix bank, and SHIFT does the same (Frank 25.09.).
//  · a transport key fires only a TotalMix builtin from ORC's file; a REAPER
//    action there is ORC's inherited factory and the key stays REAPER's.
//  · 5-8 in the side-car switches the half, not the V-Pot bank any more.

#include "Bindings.h"
#include "BindingsHost.h"
#include "RmeInput.h"
#include "RmeNames.h"
#include "RmeSoftKeys.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace bnd = uf8::bindings;
namespace sk  = reasixty::rme::softkeys;

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

static std::string g_dir;

static void writeText(const std::string& path, const std::string& text)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
}
static std::string readText(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ORC's file: bank 10 with a Plain and a Shift key, bank 11 TotalMix
// snapshots, a DAW bank 0 that must NOT come across, and a transport layer with
// a TotalMix builtin on Play and a REAPER action on Stop.
static const char* kOrcJson = R"({
  "version": 49,
  "active_layer": 0,
  "layers": [
    { "bindings": {
      "uf1_play": { "behavior": "momentary", "label": "PLAY",
        "short": { "plain": { "type": "builtin", "action": "rme_durec_play", "param": 0 } } },
      "uf1_stop": { "behavior": "momentary", "label": "STOP",
        "short": { "plain": { "type": "reaper", "action": "1016", "param": 0 } } }
    } }
  ],
  "uf1_soft_banks": [
    {"bank": 0, "slot": 0, "body": {"behavior": "momentary", "label": "ORC DAW",
      "short": {"plain": {"type": "builtin", "action": "rme_dim", "param": 0}}}},
    {"bank": 10, "slot": 0, "body": {"behavior": "momentary", "label": "Dim",
      "short": {"plain": {"type": "builtin", "action": "rme_dim", "param": 0},
                "shift": {"type": "builtin", "action": "rme_ext_in", "param": 0, "label": "Ext In"}}}}
  ],
  "uf1_soft_bank_dynamic": [ {"bank": 11, "mod": 0, "kind": 10} ]
})";

static int g_durecPlay = 0;
static int g_dim = 0;

int main()
{
    char tmpl[] = "/tmp/rme_softkeys_XXXXXX";
    g_dir = mkdtemp(tmpl);
    const std::string orc = g_dir + "/orc.json";

    bnd::Host h;
    h.configDir  = [] { return g_dir; };
    h.configFile = [] { return std::string("bindings.json"); };
    bnd::setHost(std::move(h));

    bnd::registerBuiltin("rme_durec_play", bnd::BuiltinDescriptor{
        [](bool firing, bool, int) { if (firing) ++g_durecPlay; }, nullptr, "DuRec", false });
    bnd::registerBuiltin("rme_dim", bnd::BuiltinDescriptor{
        [](bool firing, bool, int) { if (firing) ++g_dim; }, nullptr, "Dim", false });

    // Our own file first, with a DAW bank 0 of its own and an RME bank 10 that
    // is about to become someone else's.
    bnd::load();
    bnd::Binding own;
    own.label = "OWN DAW";
    own.shortPress[0].type   = bnd::ActionType::Builtin;
    own.shortPress[0].action = "rme_dim";
    bnd::setUf1SoftBankSlot(0, 0, own);
    bnd::Binding stale;
    stale.label = "STALE";
    stale.shortPress[0].type   = bnd::ActionType::Builtin;
    stale.shortPress[0].action = "rme_dim";
    bnd::setUf1SoftBankSlot(bnd::kUf1RmeBankBase, 1, stale);

    // ── no file: the RME banks are empty, the DAW bank is ours ──────────────
    bnd::setSideCarSource(orc);
    EXPECT(bnd::refreshSideCarSource(true));
    EXPECT(bnd::getUf1SoftBankSlot(bnd::kUf1RmeBankBase, 1).label.empty());
    EXPECT(bnd::getUf1SoftBankSlot(0, 0).label == "OWN DAW");

    // ── the file appears: banks 10.. from it, nothing else ──────────────────
    writeText(orc, kOrcJson);
    EXPECT(bnd::refreshSideCarSource(true));
    EXPECT(bnd::getUf1SoftBankSlot(bnd::kUf1RmeBankBase, 0).label == "Dim");
    EXPECT(bnd::getUf1SoftBankSlot(bnd::kUf1RmeBankBase, 1).label.empty());   // stale gone
    EXPECT(bnd::getUf1SoftBankSlot(0, 0).label == "OWN DAW");                 // not "ORC DAW"
    EXPECT(bnd::getUf1SoftBankDynamic(bnd::kUf1RmeBankBase + 1, 0)
           == bnd::DynamicBankKind::RmeSnapshots);
    // Unchanged file, nothing edited: nothing to do.
    EXPECT(!bnd::refreshSideCarSource(false));

    // ── our save leaves them out ─────────────────────────────────────────────
    bnd::save();
    const std::string saved = readText(g_dir + "/bindings.json");
    EXPECT(saved.find("\"bank\": 10") == std::string::npos);
    EXPECT(saved.find("\"bank\": 11") == std::string::npos);
    EXPECT(saved.find("OWN DAW") != std::string::npos);

    // ── a load of our file replaces the config; the next look puts them back ─
    // load() seeds the factory under what it parses, so bank 10 comes back with
    // the factory's four keys (Mono on key 2); ORC's file has only key 1.
    bnd::load();
    EXPECT(bnd::getUf1SoftBankSlot(bnd::kUf1RmeBankBase, 1).label == "Mono");
    EXPECT(bnd::refreshSideCarSource(false));
    EXPECT(bnd::getUf1SoftBankSlot(bnd::kUf1RmeBankBase, 0).label == "Dim");
    EXPECT(bnd::getUf1SoftBankSlot(bnd::kUf1RmeBankBase, 1).label.empty());

    // ── the two halves ───────────────────────────────────────────────────────
    reasixty::rme::input::State in;
    EXPECT(sk::half(in) == 0);
    auto r0 = sk::row(bnd::kUf1RmeBankBase, 0);
    auto r1 = sk::row(bnd::kUf1RmeBankBase, 1);
    EXPECT(r0[0].label == "Dim");
    EXPECT(r1[0].label == "Ext In");
    in.skHalf.store(1);
    EXPECT(sk::half(in) == 1);
    in.skHalf.store(0);
    bnd::setModifierHeld(bnd::Modifier::Shift, true);        // SHIFT on the surface
    EXPECT(sk::half(in) == 1);
    bnd::setModifierHeld(bnd::Modifier::Shift, false);
    EXPECT(sk::half(in) == 0);
    // A TotalMix bank: kind from Plain, halves are items 1-4 and 5-8.
    EXPECT(sk::rmeKind(bnd::kUf1RmeBankBase + 1) == bnd::DynamicBankKind::RmeSnapshots);
    EXPECT(sk::rmeKind(bnd::kUf1RmeBankBase) == bnd::DynamicBankKind::None);
    auto s0 = sk::row(bnd::kUf1RmeBankBase + 1, 0);
    auto s1 = sk::row(bnd::kUf1RmeBankBase + 1, 1);
    // Names from TotalMix' own file where there is one (this Mac's), else
    // "Snapshot n": either way the same function says it.
    const auto& nm = reasixty::rme::namesFromDisk();
    EXPECT(s0[0].label == reasixty::rme::snapshotName(nm, 0));
    EXPECT(s1[3].label == reasixty::rme::snapshotName(nm, 7));
    // A press on the static half fires that half's action: Plain = Dim.
    const int dimBefore = g_dim;
    sk::press(bnd::kUf1RmeBankBase, 0, 0, true);
    sk::press(bnd::kUf1RmeBankBase, 0, 0, false);
    EXPECT(g_dim == dimBefore + 1);
    // A TotalMix key hands its item to the host's loader, 5-8 = items 4..7.
    int loaded = -1;
    sk::press(bnd::kUf1RmeBankBase + 1, 1, 2, true,
              [&](bnd::DynamicBankKind, int slot) { loaded = slot; });
    EXPECT(loaded == 6);

    // ── transport keys ───────────────────────────────────────────────────────
    EXPECT(bnd::dispatchSideCarKey(bnd::ButtonId::Uf1Play, true));
    EXPECT(g_durecPlay == 1);
    EXPECT(bnd::dispatchSideCarKey(bnd::ButtonId::Uf1Play, false));
    EXPECT(!bnd::dispatchSideCarKey(bnd::ButtonId::Uf1Stop, true));    // REAPER's
    EXPECT(!bnd::dispatchSideCarKey(bnd::ButtonId::Uf1Rec, true));     // unbound
    EXPECT(!bnd::dispatchSideCarKey(bnd::ButtonId::Uf1Btn360, true));  // never

    // ── Bank ◄ ► pick the half, 5-8 flips it (Frank 27.09.) ────────────────
    {
        namespace sk2 = reasixty::rme::softkeys;
        reasixty::rme::input::State s;
        reasixty::rme::input::Host host;
        reasixty::rme::input::Writes w;
        std::atomic<int> bankIdx{0};
        const int base = bnd::kUf1RmeBankBase;
        ::uf1::InputEvent ev{};
        ev.kind = ::uf1::InputKind::Button;
        ev.pressed = true;

        EXPECT(sk2::hasSecondHalf(base));      // bank 1: Ext In, Main, TotalMix
        // 5-8 flips the half and leaves the V-Pot bank alone; its release does nothing.
        ev.id = ::uf1::btn::k5to8;
        EXPECT(sk2::stepKey(s, bankIdx, base, 3, ev));
        EXPECT(s.skHalf.load() == 1 && s.vpotBank.load() == 0);
        ev.pressed = false;
        EXPECT(sk2::stepKey(s, bankIdx, base, 3, ev));
        EXPECT(s.skHalf.load() == 1);
        ev.pressed = true;
        EXPECT(sk2::stepKey(s, bankIdx, base, 3, ev));
        EXPECT(s.skHalf.load() == 0);
        // In STRIP it does nothing, like Bank ◄ ►.
        s.strip.store(true);
        EXPECT(sk2::stepKey(s, bankIdx, base, 3, ev));
        EXPECT(s.skHalf.load() == 0);
        s.strip.store(false);

        EXPECT(sk2::hasSecondHalf(base + 1));  // snapshots: always eight
        ev.id = ::uf1::btn::kBankRight;
        EXPECT(sk2::stepKey(s, bankIdx, base, 3, ev));
        EXPECT(s.skHalf.load() == 1);
        EXPECT(sk2::stepKey(s, bankIdx, base, 3, ev));   // picks, does not toggle
        EXPECT(s.skHalf.load() == 1);
        ev.id = ::uf1::btn::kBankLeft;
        EXPECT(sk2::stepKey(s, bankIdx, base, 3, ev));
        EXPECT(s.skHalf.load() == 0);

        // < > step the bank, a new bank starts on its first half, no wrap.
        ev.id = ::uf1::btn::kBankRight;
        sk2::stepKey(s, bankIdx, base, 3, ev);
        ev.id = ::uf1::btn::kArrowRight;
        EXPECT(sk2::stepKey(s, bankIdx, base, 3, ev));
        EXPECT(bankIdx.load() == 1 && s.skHalf.load() == 0);
        ev.id = ::uf1::btn::kArrowLeft;
        sk2::stepKey(s, bankIdx, base, 3, ev);
        sk2::stepKey(s, bankIdx, base, 3, ev);
        EXPECT(bankIdx.load() == 0);

        // A static bank with an empty Shift set has no second half: Bank ► stays.
        EXPECT(!sk2::hasSecondHalf(base + 5));
        sk2::pickHalf(s, base + 5, 1);
        EXPECT(s.skHalf.load() == 0);
        // Nor does 5-8.
        bankIdx.store(5);
        ev.id = ::uf1::btn::k5to8;
        EXPECT(sk2::stepKey(s, bankIdx, base, 8, ev));
        EXPECT(s.skHalf.load() == 0);

        // In STRIP Bank ◄ ► are used up and change nothing.
        s.strip.store(true);
        ev.id = ::uf1::btn::kBankRight;
        EXPECT(sk2::stepKey(s, bankIdx, base, 3, ev));
        EXPECT(s.skHalf.load() == 0);
    }

    // ── SHIFT in ORC: mod_shift is registered, momentary and latching ───────
    // (Frank 27.09.: "mod_shift in ORC fixen"). Nothing had registered it in
    // ORC, so the factory SHIFT binding fired a name that did nothing.
    {
        std::atomic<bool> mirror{false};
        bnd::registerModifierBuiltins(&mirror);
        const auto shiftKey = bnd::ButtonId::Uf1Shift;
        bnd::dispatch(shiftKey, true);
        EXPECT(bnd::modifierHeld(bnd::Modifier::Shift) && mirror.load());
        EXPECT(bnd::builtinStateOf("mod_shift", 0));
        reasixty::rme::input::State hs;
        EXPECT(sk::half(hs) == 1);                     // SHIFT shows the second half
        bnd::dispatch(shiftKey, false);
        EXPECT(!bnd::modifierHeld(bnd::Modifier::Shift) && !mirror.load());
        // Second press within 400 ms of the release: latched after its release.
        bnd::dispatch(shiftKey, true);
        bnd::dispatch(shiftKey, false);
        EXPECT(bnd::modifierHeld(bnd::Modifier::Shift));
        bnd::dispatch(shiftKey, true);                 // the next press clears it
        bnd::dispatch(shiftKey, false);
        EXPECT(!bnd::modifierHeld(bnd::Modifier::Shift) && !mirror.load());
    }

    std::remove(orc.c_str());
    std::remove((g_dir + "/bindings.json").c_str());
    rmdir(g_dir.c_str());
    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("rme_softkeys: all passed\n");
    return 0;
}
