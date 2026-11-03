#include "OrcSettingsModel.h"

#include "Bindings.h"
#include "DynaMountManager.h"
#include "HueManager.h"
#include "JsonTree.h"
#include "ObsManager.h"
#include "OrcConfig.h"
#include "OrcModes.h"
#include "Palette.h"
#include "PhonesServer.h"
#include "Protocol.h"
#include "RmeManager.h"
#include "RmeSoftKeys.h"
#include "RmeState.h"
#include "RmeUf1.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <vector>

#if defined(_WIN32)
#include "WinUsbDriver.h"
#include <chrono>
#endif

namespace orc::settings {

namespace bnd  = uf8::bindings;
namespace dyna = uf8::dynamount;
namespace hue  = uf8::hue;
namespace ph   = orc::phones;
namespace rme  = reasixty::rme;
namespace rmeu = reasixty::rme::uf1;

namespace {

// ── JSON out ─────────────────────────────────────────────────────────────────

std::string jstr(const std::string& s)
{
    std::string o = "\"";
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o.push_back(ch);
    }
    return o + "\"";
}

const char* jbool(bool v) { return v ? "true" : "false"; }

std::string jnum(double v)
{
    char b[32];
    snprintf(b, sizeof(b), "%.2f", v);
    return b;
}

std::string hexColour(uint8_t r, uint8_t g, uint8_t b)
{
    char t[8];
    snprintf(t, sizeof(t), "#%02x%02x%02x", r, g, b);
    return t;
}

template <typename T, typename F>
std::string arr(const T& items, F each)
{
    std::string o = "[";
    bool first = true;
    for (const auto& x : items) {
        if (!first) o += ",";
        first = false;
        o += each(x);
    }
    return o + "]";
}

std::string indexed(int n, const std::function<std::string(int)>& each)
{
    std::string o = "[";
    for (int i = 0; i < n; ++i) {
        if (i) o += ",";
        o += each(i);
    }
    return o + "]";
}

// ── the menus ────────────────────────────────────────────────────────────────

// Every target a pot or the jog wheel can carry: the control-room roles first,
// then whatever this remote can actually see, named as TotalMix names it.
// ⇨ Built from the live state, so the list is the mixer's answer rather than one
// we keep in step by hand.
// ⇨ WORDS, NOT SPECS (Frank 27.09.2026: "liest sich wie code", "nicht
// wiederholen"). A role reads as its name, with the channel only where TotalMix
// calls it something else; a channel reads as its TotalMix name under a heading
// for its row. The spec stays behind the entry. A heading has no spec.
struct Choice {
    std::string spec;
    std::string label;
    bool header = false;
};

std::vector<Choice> targetChoices(const rme::State& st)
{
    std::vector<Choice> out;
    out.push_back({ "", "None" });
    struct Role { const char* spec; const char* name; };
    const Role roles[] = { { "main", "Main" }, { "mainB", "Main B" },
                           { "phones1", "Phones 1" }, { "phones2", "Phones 2" },
                           { "phones3", "Phones 3" }, { "phones4", "Phones 4" },
                           { "talk", "Talkback" } };
    for (const auto& r : roles) {
        const auto t = rmeu::resolveTarget(st, r.spec);
        std::string label = r.name;
        if (!t.assigned)     label += " (not assigned)";
        else if (!t.visible) label += " (hidden)";
        else {
            const std::string ch = rmeu::displayName(st, t.row, t.ch);
            if (!ch.empty() && ch != r.name) label += " (" + ch + ")";
        }
        out.push_back({ r.spec, label });
    }
    // TotalMix' channel rows only: an effect is no V-Pot or jog target, and
    // these two arrays have three entries (kRowCount became 4 with the FX row).
    const char* kinds[]   = { "input", "playback", "output" };
    const char* headers[] = { "Inputs", "Playback", "Outputs" };
    static_assert(rmeu::kMixerRowCount == 3, "kinds[] and headers[] name each mixer row");
    for (int r = 0; r < rmeu::kMixerRowCount; ++r) {
        const auto row = static_cast<rmeu::Row>(r);
        // An output with a role is the role above, under the same name.
        const auto chans = row == rmeu::Row::Output ? rmeu::outputsWithoutRole(st)
                                                    : rmeu::visibleChannels(st, row);
        if (chans.empty()) continue;
        out.push_back({ "", headers[r], true });
        for (int ch : chans) {
            char spec[32];
            snprintf(spec, sizeof spec, "%s:%d", kinds[r], ch);
            out.push_back({ spec, rmeu::displayName(st, row, ch) });
        }
    }
    return out;
}

// What a key can pick: the output half of the same list (RmeUf1::keyOutput).
std::vector<Choice> outputChoices(const rme::State& st)
{
    std::vector<Choice> out;
    bool headed = false, outputs = false;
    for (const auto& c : targetChoices(st)) {
        if (c.header) {
            headed  = true;
            outputs = c.label == "Outputs";
            if (outputs) out.push_back(c);
        } else if (c.spec != "talk" && (!headed || outputs)) {
            out.push_back(c);
        }
    }
    return out;
}

std::string choicesJson(const std::vector<Choice>& v)
{
    return arr(v, [](const Choice& c) {
        return c.header ? "{\"h\":" + jstr(c.label) + "}"
                        : "{\"s\":" + jstr(c.spec) + ",\"l\":" + jstr(c.label) + "}";
    });
}

// The entry a spec shows as, or null. ⛔ A spec the live list cannot contain (the
// link is down, or the channel is hidden) shows no entry at all and leaves the
// file alone. The page must not fall to whatever sits first in the menu.
std::string specSelected(const rme::State& st, const std::string& spec, const std::vector<Choice>& choices)
{
    const auto has = [&](const std::string& s) {
        return std::any_of(choices.begin(), choices.end(),
                           [&](const Choice& c) { return !c.header && c.spec == s; });
    };
    if (has(spec)) return jstr(spec);
    // A fixed output that has since got a role is listed as that role.
    const std::string asRole = rmeu::menuSpec(st, spec);
    if (asRole != spec && has(asRole)) return jstr(asRole);
    return "null";
}

// A pot's push, stored as the word on the left, shown as the one on the right.
struct PushChoice { const char* value; const char* title; };
const PushChoice kPushes[] = {
    { "submix", "Submix" }, { "select", "Select" }, { "mute", "Mute" }, { "none", "Nothing" },
};

// The five transport keys the side-car hands to ORC's bindings (Bindings.cpp,
// dispatchSideCarKey), in the order they sit on the surface, then the UF8's
// keys the user binds (Frank 29.10.2026, orc/Uf8Surface.cpp kUserKeys).
struct KeyName { bnd::ButtonId id; const char* name; };
constexpr int kTransportCount = 5;
constexpr int kKeyCount = kTransportCount + 9;
const KeyName kKeys[kKeyCount] = {
    { bnd::ButtonId::Uf1Rwd,  "Rewind" },
    { bnd::ButtonId::Uf1Ffw,  "Forward" },
    { bnd::ButtonId::Uf1Stop, "Stop" },
    { bnd::ButtonId::Uf1Play, "Play" },
    { bnd::ButtonId::Uf1Rec,  "Record" },
    { bnd::ButtonId::Quick1,        "QUICK 1" },
    { bnd::ButtonId::Quick2,        "QUICK 2" },
    { bnd::ButtonId::Quick3,        "QUICK 3" },
    { bnd::ButtonId::Nav,           "NAV" },
    { bnd::ButtonId::Nudge,         "NUDGE" },
    { bnd::ButtonId::EncFocus,      "FOCUS" },
    { bnd::ButtonId::SelectionNorm, "NORM" },
    { bnd::ButtonId::SelectionRec,  "REC" },
    { bnd::ButtonId::SelectionAuto, "AUTO" },
};

// Every action ORC offers is TotalMix', so the "RME: " in front of each would
// only repeat itself down the menu.
std::string actionTitle(const std::string& builtin)
{
    std::string d = bnd::builtinDisplayName(builtin);
    const std::string prefix = "RME: ";
    if (d.rfind(prefix, 0) == 0) d.erase(0, prefix.size());
    return d;
}

bool isOurAction(const std::vector<std::string>& actions, const std::string& a)
{
    return std::find(actions.begin(), actions.end(), a) != actions.end();
}

// ── lamps ────────────────────────────────────────────────────────────────────

// A lamp colour in a colour menu: Off (0), or the SSL entry it is. A colour that
// is no entry (set elsewhere) shows no selection rather than a wrong one (null).
std::string ledSelected(const uint8_t (&rgb)[3], bnd::Brightness bri)
{
    if (bri == bnd::Brightness::Off) return "0";
    for (int p = 1; p < 16; ++p) {
        const auto s = uf8::paletteSwatch(static_cast<uint8_t>(p));
        if (s && s->r == rgb[0] && s->g == rgb[1] && s->b == rgb[2]) return std::to_string(p);
    }
    return "null";
}

// The menu's choice into a lamp colour. Active is bright, Inactive dim, the way
// a fresh binding has them (white, Bright / Dim); Off is the lamp's own Off.
void ledFromEntry(int entry, bool active, uint8_t (&rgb)[3], bnd::Brightness& bri)
{
    if (entry <= 0) { bri = bnd::Brightness::Off; return; }
    const auto s = uf8::paletteSwatch(static_cast<uint8_t>(entry));
    if (!s) return;
    rgb[0] = s->r; rgb[1] = s->g; rgb[2] = s->b;
    bri = active ? bnd::Brightness::Bright : bnd::Brightness::Dim;
}

std::string paletteJson()
{
    std::string o = "[{\"i\":0,\"n\":\"Off\",\"c\":null}";
    // ⇨ Only what lights: 0x0D-0x0F show nothing on a panel.
    for (int p = 1; p < 16; ++p) {
        const auto idx = static_cast<uint8_t>(p);
        const auto rgb = uf8::paletteSwatch(idx);
        if (!rgb) continue;
        o += ",{\"i\":" + std::to_string(p) + ",\"n\":" + jstr(uf8::paletteName(idx))
           + ",\"c\":" + jstr(hexColour(rgb->r, rgb->g, rgb->b)) + "}";
    }
    return o + "]";
}

// ── the soft keys ────────────────────────────────────────────────────────────

int kindIndex(bnd::DynamicBankKind k)
{
    switch (k) {
        case bnd::DynamicBankKind::None:         return 0;
        case bnd::DynamicBankKind::RmeSnapshots: return 1;
        case bnd::DynamicBankKind::RmeLayouts:   return 2;
        case bnd::DynamicBankKind::ObsScenes:    return 3;
        case bnd::DynamicBankKind::RmeSubmixes:  return 4;
        default:                                 return -1;
    }
}

bnd::DynamicBankKind kindFromIndex(int i)
{
    return i == 1 ? bnd::DynamicBankKind::RmeSnapshots
         : i == 2 ? bnd::DynamicBankKind::RmeLayouts
         : i == 3 ? bnd::DynamicBankKind::ObsScenes
         : i == 4 ? bnd::DynamicBankKind::RmeSubmixes
                  : bnd::DynamicBankKind::None;
}

std::string softKeysJson(const rme::State& st, const std::vector<Choice>& outs, const std::vector<std::string>& actions)
{
    return indexed(bnd::kUf1RmeBankCount, [&](int rel) {
        const int bank = bnd::kUf1RmeBankBase + rel;
        const auto kind = bnd::getUf1SoftBankDynamic(bank, 0);
        const int  ki   = kindIndex(kind);
        const bool dyn  = ki >= 1 && ki <= 3;
        const bool subs = ki == 4;
        const std::string halves = indexed(2, [&](int half) {
            return indexed(4, [&](int i) {
                const bnd::Binding bd = bnd::getUf1SoftBankSlot(bank, i);
                const auto& sp = bd.shortPress[half];
                const std::string own = (half == 0 && sp.label.empty()) ? bd.label : sp.label;
                std::string action;
                if (subs)     action = specSelected(st, rme::softkeys::submixSpec(bank, half * 4 + i), outs);
                else if (dyn) action = "null";
                else if (sp.type != bnd::ActionType::Builtin || sp.action.empty())
                    action = (sp.type == bnd::ActionType::Noop && sp.action.empty()) ? "\"\"" : "null";
                else
                    // Something ORC cannot run (a REAPER action from the inherited
                    // factory) shows no entry rather than pretending to be None.
                    action = isOurAction(actions, sp.action) ? jstr(sp.action) : "null";
                // A snapshot or layout key takes its colours from here too (Frank
                // 02.10.2026, RmeSoftKeys::dynKeyLamp): Active = the loaded one,
                // Inactive = the others. Only the label and the action are fixed.
                uint8_t rgb[3]; bnd::Brightness bri;
                bnd::effectiveLedActive(bd, sp, rgb, bri);
                const std::string on = ledSelected(rgb, bri);
                bnd::effectiveLedInactive(bd, sp, rgb, bri);
                const std::string off = ledSelected(rgb, bri);
                return "{\"label\":" + jstr(dyn ? std::string() : own)
                     + ",\"ph\":" + jstr(dyn || subs ? std::string() : bnd::softKeyFallbackLabel(sp))
                     + ",\"action\":" + action + ",\"on\":" + on + ",\"off\":" + off + "}";
            });
        });
        return "{\"kind\":" + (ki < 0 ? std::string("null") : std::to_string(ki))
             + ",\"name\":" + jstr(bnd::getUf1SoftBankName(bank, 0))
             + ",\"halves\":" + halves + "}";
    });
}

std::string keysJson(const std::vector<std::string>& actions)
{
    const int layer = bnd::getActiveLayer();
    return indexed(kKeyCount, [&](int k) {
        const bnd::Binding bd = bnd::getBinding(layer, kKeys[k].id);
        const auto& sp = bd.shortPress[0];
        // A REAPER action there is ORC's inherited factory and does nothing in
        // ORC: it reads as None, which is also what Rea-Sixty makes of it.
        const bool ours = sp.type == bnd::ActionType::Builtin && isOurAction(actions, sp.action);
        return "{\"n\":" + jstr(kKeys[k].name) + ",\"action\":" + jstr(ours ? sp.action : std::string())
             + ",\"on\":" + ledSelected(bd.color, bd.brightness)
             + ",\"off\":" + ledSelected(bd.inactiveColor, bd.inactiveBrightness) + "}";
    });
}

// ── reading a message ────────────────────────────────────────────────────────

std::string text(const wdl_json_element* e, const char* k)
{
    const char* v = e->get_string_by_name(k, true);
    return v ? v : "";
}

bool flag(const wdl_json_element* e, const char* k)
{
    const std::string v = text(e, k);
    return v == "1" || v == "true";
}

bool number(const std::string& s, double& out)
{
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || !std::isfinite(v)) return false;
    out = v;
    return true;
}

bool validPort(const std::string& s, int& out)
{
    double v = 0;
    if (!number(s, v) || v < 1 || v > 65535 || v != std::floor(v)) return false;
    out = static_cast<int>(v);
    return true;
}

// A PIN is digits, four to eight of them.
bool validPin(const std::string& p)
{
    if (p.size() < 4 || p.size() > 8) return false;
    return std::all_of(p.begin(), p.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// ── writing ──────────────────────────────────────────────────────────────────

// ⛔ EVERY EDIT GOES THROUGH setConfig AND THEN saveRmeConfigIfDirty: the manager
// is read when the page is drawn and handed a whole Config back on a change.
template <typename F>
void editRme(Context& c, F mutate)
{
    auto& mgr = rme::manager();
    auto cfg = mgr.config();
    mutate(cfg);
    mgr.setConfig(cfg);
    c.error = orc::saveRmeConfigIfDirty();
}

void saveDynaDevices()
{
    orc::modes::write("dynamount_devices", dyna::manager().serialize());
}

void storeMoreme(Context& c, const ph::Settings& s, const std::string& pin)
{
    std::string err;
    if (!ph::saveSettings(s)) err = "Cannot write " + ph::settingsPath();
    else if (!pin.empty() && pin != ph::loadPin() && !ph::savePin(pin))
        err = "Cannot write the PIN to " + orc::supportDir();
    ph::server().apply(s, pin);
    c.error = err;
}

void applyRme(const wdl_json_element* m, Context& c)
{
    const std::string f = text(m, "f"), v = text(m, "v");
    double n = 0;
    const bool on = v == "1" || v == "true";
    if (f == "enabled")         editRme(c, [&](rme::Config& x) { x.enabled = on; });
    else if (f == "host")       editRme(c, [&](rme::Config& x) { x.host = v; });
    else if (f == "sendPort" || f == "recvPort") {
        int p = 0;
        if (!validPort(v, p)) { c.error = "A port is a number from 1 to 65535."; return; }
        editRme(c, [&](rme::Config& x) { (f == "sendPort" ? x.sendPort : x.recvPort) = p; });
    }
    else if (f == "jogTarget")  editRme(c, [&](rme::Config& x) { x.jogTarget = v; });
    else if (f == "jogStepDb" && number(v, n))  editRme(c, [&](rme::Config& x) { x.jogStepDb = n; });
    else if (f == "vpotStepDb" && number(v, n)) editRme(c, [&](rme::Config& x) { x.vpotStepDb = n; });
    else if ((f == "uf1PotSpeed" || f == "uf8PotSpeed") && number(v, n)) {
        const double s = std::clamp(n, rme::Config::kPotSpeedMin, rme::Config::kPotSpeedMax);
        editRme(c, [&](rme::Config& x) { (f == "uf1PotSpeed" ? x.uf1PotSpeed : x.uf8PotSpeed) = s; });
    }
    else if (f == "sleepEnabled") editRme(c, [&](rme::Config& x) { x.sleepEnabled = on; });
    else if (f == "sleepMinutes" && number(v, n)) {
        const int mins = std::clamp(static_cast<int>(n), 1, 99);
        editRme(c, [&](rme::Config& x) { x.sleepMinutes = mins; });
    }
    else if (f == "uf1Enabled") editRme(c, [&](rme::Config& x) { x.uf1Enabled = on; });
    else if (f == "uf8Enabled") editRme(c, [&](rme::Config& x) { x.uf8Enabled = on; });
    else if (f == "uf8ShowMainOnChange") editRme(c, [&](rme::Config& x) { x.uf8ShowMainOnChange = on; });
}

void applySoftKey(const std::string& t, const wdl_json_element* m)
{
    const int rel = std::atoi(text(m, "b").c_str());
    if (rel < 0 || rel >= bnd::kUf1RmeBankCount) return;
    const int bank = bnd::kUf1RmeBankBase + rel;
    if (t == "skFactory") { bnd::restoreRmeSideCarBank(rel); return; }
    if (t == "skKind") {
        const auto kind = kindFromIndex(std::atoi(text(m, "v").c_str()));
        // Leaving a submix bank takes its outputs along: on a plain bank a slot
        // naming no action of ours would be a key that shows the wrong thing.
        if (kind != bnd::DynamicBankKind::RmeSubmixes)
            for (int item = 0; item < 8; ++item)
                if (!rme::softkeys::submixSpec(bank, item).empty())
                    rme::softkeys::setSubmixSpec(bank, item, {});
        bnd::setUf1SoftBankDynamic(bank, 0, kind);
        // The kind belongs to the bank, both halves (RmeSoftKeys::rmeKind).
        bnd::setUf1SoftBankDynamic(bank, 1, bnd::DynamicBankKind::None);
        return;
    }
    if (t == "skName") {
        // One name for the bank, whichever half is showing when it is announced.
        const std::string n = text(m, "v");
        bnd::setUf1SoftBankName(bank, 0, n);
        bnd::setUf1SoftBankName(bank, 1, n);
        return;
    }
    const int half = std::atoi(text(m, "h").c_str());
    const int i    = std::atoi(text(m, "i").c_str());
    if (half < 0 || half > 1 || i < 0 || i > 3) return;
    const std::string v = text(m, "v");
    bnd::Binding bd = bnd::getUf1SoftBankSlot(bank, i);
    if (t == "skLabel") {
        // The first half's name is the key's own (Binding::label), the second
        // half's lives in its set (ActionSlot::label): uf1SoftBankKeyLabel.
        if (half == 0) {
            bd.label = v;
            bd.labelIsUserSet = !v.empty();
            bd.shortPress[0].label.clear();
        } else {
            bd.shortPress[1].label = v;
        }
        bnd::setUf1SoftBankSlot(bank, i, bd);
    } else if (t == "skAction") {
        if (bnd::getUf1SoftBankDynamic(bank, 0) == bnd::DynamicBankKind::RmeSubmixes) {
            rme::softkeys::setSubmixSpec(bank, half * 4 + i, v);
            return;
        }
        auto& sp = bd.shortPress[half];
        const std::string keepLabel = sp.label;
        const bnd::LedOverride keepLed = sp.led;   // the colour belongs to the key
        sp = bnd::ActionSlot{};
        sp.led = keepLed;
        if (!v.empty()) {
            sp.type   = bnd::ActionType::Builtin;
            sp.action = v;
        }
        if (half == 1) sp.label = v.empty() ? std::string() : keepLabel;
        // A label nobody typed follows the new action (the placeholder shows it).
        if (half == 0 && !bd.labelIsUserSet) bd.label.clear();
        bnd::setUf1SoftBankSlot(bank, i, bd);
    } else if (t == "skLed") {
        const bool active = flag(m, "active");
        auto& led = bd.shortPress[half].led;
        if (active) {
            led.hasActive = true;
            ledFromEntry(std::atoi(v.c_str()), true, led.color, led.brightness);
        } else {
            led.hasInactive = true;
            ledFromEntry(std::atoi(v.c_str()), false, led.inactiveColor, led.inactiveBrightness);
        }
        bnd::setUf1SoftBankSlot(bank, i, bd);
    }
}

void applyKey(const std::string& t, const wdl_json_element* m)
{
    const int k = std::atoi(text(m, "k").c_str());
    if (k < 0 || k >= kKeyCount) return;
    const int layer = bnd::getActiveLayer();
    bnd::Binding bd = bnd::getBinding(layer, kKeys[k].id);
    const std::string v = text(m, "v");
    if (t == "keyAction") {
        bd.shortPress[0] = bnd::ActionSlot{};
        if (!v.empty()) {
            bd.shortPress[0].type   = bnd::ActionType::Builtin;
            bd.shortPress[0].action = v;
        }
        bd.label.clear();
    } else if (flag(m, "active")) {
        ledFromEntry(std::atoi(v.c_str()), true, bd.color, bd.brightness);
    } else {
        ledFromEntry(std::atoi(v.c_str()), false, bd.inactiveColor, bd.inactiveBrightness);
    }
    bnd::setBinding(layer, kKeys[k].id, bd);
}

void applyObs(const std::string& t, const wdl_json_element* m, Context& c)
{
    auto& om = reasixty::obs::manager();
    const std::string v = text(m, "v");
    if (t == "obsPassword") { om.setPassword(v); return; }
    auto cfg = om.config();
    const std::string f = text(m, "f");
    if (f == "enabled")   cfg.enabled = v == "1" || v == "true";
    else if (f == "host") cfg.host = v;
    else if (f == "port") {
        int p = 0;
        if (!validPort(v, p)) { c.error = "A port is a number from 1 to 65535."; return; }
        cfg.port = p;
    }
    om.setConfig(cfg);
}

void applyDyna(const std::string& t, const wdl_json_element* m)
{
    auto& dm = dyna::manager();
    const int i = std::atoi(text(m, "i").c_str());
    const std::string v = text(m, "v");
    if (t == "dynaFill") {
        const bool right = v == "1";
        dm.setFillDir(right ? dyna::FillDir::Right : dyna::FillDir::Left);
        orc::modes::write("dynamount_fill", right ? "R" : "L");
    } else if (t == "dyna") {
        if (i < 0 || i >= dyna::kMaxMounts) return;
        const auto in = dm.info(i);
        const std::string f = text(m, "f");
        dm.setMount(i, f == "on" ? (v == "1") : in.enabled, f == "name" ? v : in.name,
                    f == "ip" ? v : in.ip, f == "colour" ? std::max(0, std::atoi(v.c_str())) : in.color);
        saveDynaDevices();
    } else if (t == "dynaDetect") dm.requestDetect(i);
    else if (t == "dynaHome")     dm.home(i);
    else if (t == "presetName")   dm.setPresetName(i, v);
    else if (t == "presetGo")     dm.recallPreset(i);
    else if (t == "presetStore")  dm.storePreset(i);
    else if (t == "presetClear")  dm.clearPreset(i);
}

void applyHue(const std::string& t, const wdl_json_element* m)
{
    auto& hm = hue::manager();
    const int i = std::atoi(text(m, "i").c_str());
    const std::string v = text(m, "v");
    if (t == "hueFind")         hm.requestDiscovery();
    else if (t == "huePair")    hm.requestPairing();
    else if (t == "hueCancel")  hm.cancelPairing();
    else if (t == "hueRefresh") hm.requestRefresh();
    else if (t == "hueForget")  hm.forget();
    else if (t == "hueIp")      hm.setBridgeIp(v);
    else if (t == "hueFill")
        hm.setFillDir(v == "1" ? hue::HueManager::FillDir::Right : hue::HueManager::FillDir::Left);
    else if (t == "hueSlot") {
        if (i < 0 || i >= hue::kMaxSlots) return;
        hue::SlotConfig sc = hm.slot(i);
        if (text(m, "f") == "on") sc.enabled = v == "1";
        else                      sc.label   = v.substr(0, hue::kLabelChars);
        hm.setSlot(i, sc);
    } else if (t == "hueTarget") {
        if (i < 0 || i >= hue::kMaxSlots) return;
        const hue::SlotConfig sc = hm.slot(i);
        if (v.empty()) { hm.setSlot(i, hue::slotWithNothing(sc)); return; }
        const std::string id = v.substr(1);
        if (v[0] == 'L') {
            for (const auto& L : hm.lights())
                if (L.id == id) { hm.setSlot(i, hue::slotWithLight(sc, L)); return; }
        } else {
            for (const auto& g : hm.groups())
                if (g.groupedLightId == id) { hm.setSlot(i, hue::slotWithGroup(sc, g)); return; }
        }
    } else if (t == "hueScene") {
        if (i < 0 || i >= hue::kMaxScenes) return;
        if (v.empty()) { hm.setSceneSlot(i, hue::SceneSlot{}); return; }
        for (const auto& sc : hm.scenes())
            if (sc.id == v) { hm.setSceneSlot(i, hue::sceneSlotWith(hm.sceneSlot(i), sc)); return; }
    } else if (t == "hueSceneLabel") {
        if (i < 0 || i >= hue::kMaxScenes) return;
        hue::SceneSlot s = hm.sceneSlot(i);
        s.label = v.substr(0, 12);
        hm.setSceneSlot(i, s);
    } else if (t == "hueControls") {
        hue::Controls ctl = hm.controls();
        const auto role = static_cast<hue::PotRole>(std::clamp(std::atoi(v.c_str()), 0, 3));
        if (text(m, "f") == "pot") ctl.pot = role;
        else                       ctl.potFlip = role;
        hm.setControls(ctl);
    }
}

void applyMoreme(const std::string& t, const wdl_json_element* m, Context& c)
{
    ph::Settings s = ph::server().settings();
    std::string pin = ph::server().pin();
    const std::string v = text(m, "v");
    if (t == "mmOn") {
        s.enabled = v == "1";
        // ⛔ A PIN AS SOON AS IT IS ON (Frank 29.10.2026): the first switch-on
        // makes one, and the page shows it.
        if (s.enabled && !validPin(pin)) pin = ph::generatePin();
    } else if (t == "mmPin") {
        if (!validPin(v)) { c.error = "The PIN is 4 to 8 digits."; return; }
        pin = v;
    } else if (t == "mmPort") {
        int p = 0;
        if (!validPort(v, p)) { c.error = "A port is a number from 1 to 65535."; return; }
        const std::string f = text(m, "f");
        if (f == "webPort") {
            if (p == c.ownPort) { c.error = "Port " + v + " is in use by this window."; return; }
            s.webPort = p;
        }
        else if (f == "sendPort") s.sendPort = p;
        else if (f == "recvPort") s.recvPort = p;
    } else if (t == "mmStudio") {
        s.studio = v;
    } else if (t == "mmLogoDefault") {
        s.logo.clear();
        ++c.logoStamp;
    } else {
        return;
    }
    storeMoreme(c, s, pin);
}

} // namespace

std::vector<std::string> rmeActions()
{
    std::vector<std::string> out;
    for (const auto& n : bnd::builtinNames())
        // Plus Sleep and the brightness steps (category Brightness), which ORC
        // registers itself (Surface::start).
        if (std::string(bnd::builtinCategory(n)) == "RME"
            || std::string(bnd::builtinCategory(n)) == "Brightness") out.push_back(n);
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        return bnd::builtinDisplayName(a) < bnd::builtinDisplayName(b);
    });
    return out;
}

void logoChosen(const std::string& path, Context& c)
{
    std::string err;
    const std::string name = ph::importLogo(path, err);
    if (name.empty()) { c.error = err; return; }
    ph::Settings s = ph::server().settings();
    s.logo = name;
    ++c.logoStamp;   // same name, new picture: draw it again
    storeMoreme(c, s, ph::server().pin());
}

#if defined(_WIN32)
namespace {

const std::vector<winusb::Device> kUsbDevices = {
    { "VID_31E9&PID_0025", "UF1" },
    { "VID_31E9&PID_0021", "UF8" },
};

// A walk over the USB tree: every few seconds, and at once after anything happened.
std::string usbJson(const Context& c)
{
    const int job = winusb::jobState();
    if (job == 2) {
        c.usbMessage = winusb::jobMessage();
        winusb::jobClear();
        c.usbStateAt = 0;
    } else if (job == 1) {
        c.usbMessage = winusb::jobMessage();
    }
    const long long now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (c.usbState.empty() || c.usbStateAt == 0 || now - c.usbStateAt >= 3) {
        c.usbStateAt = now;
        std::string list;
        for (const winusb::DeviceState& d : winusb::status(kUsbDevices)) {
            if (!list.empty()) list += ",";
            list += "{\"n\":" + jstr(d.name) + ",\"s\":"
                  + jstr(!d.connected ? "not connected"
                         : d.ready ? "ready"
                         : d.service.empty() ? "no driver" : "needs the driver, on " + d.service)
                  + ",\"ok\":" + jbool(d.ready) + "}";
        }
        c.usbState = "[" + list + "]";
    }
    return "{\"devices\":" + c.usbState + ",\"busy\":" + jbool(job == 1) + ",\"msg\":" + jstr(c.usbMessage) + "}";
}

} // namespace
#endif

void apply(const std::string& json, Context& c)
{
    wdl_json_parser jp;
    const wdl_json_element* m = jp.parse(json.c_str(), static_cast<int>(json.size()));
    JsonTreeGuard guard{jp, m};
    if (!m || !m->is_object()) return;
    const std::string t = text(m, "t");
    const std::string v = text(m, "v");
    c.error.clear();
    const auto starts = [&](const char* p) { return t.rfind(p, 0) == 0; };

    if (t == "rme")        applyRme(m, c);
    else if (t == "ask")   rme::manager().refresh();
#if defined(_WIN32)
    else if (t == "usbInstall") {
        // ORC binds what it can use. The UC1 stays with SSL 360.
        std::string err;
        if (!winusb::install(kUsbDevices, &err)) c.usbMessage = "Driver install could not start: " + err;
        c.usbStateAt = 0;
    }
    else if (t == "usbRemove") {
        std::string err;
        if (!winusb::uninstall(&err)) c.usbMessage = "Driver uninstall could not start: " + err;
        c.usbStateAt = 0;
    }
    else if (t == "usbRefresh") c.usbStateAt = 0;
#endif
    else if (t == "login") { if (c.host.setLogin && c.loginAvailable) c.host.setLogin(v == "1"); }
    else if (t == "pot") {
        const int i = std::atoi(text(m, "i").c_str());
        if (i < 0 || i >= rme::Config::kVpotSlots) return;
        if (text(m, "f") == "target") editRme(c, [&](rme::Config& x) { x.vpots[i].target = v; });
        else {
            const bool known = std::any_of(std::begin(kPushes), std::end(kPushes),
                                           [&](const PushChoice& p) { return v == p.value; });
            if (known) editRme(c, [&](rme::Config& x) { x.vpots[i].push = v; });
        }
    }
    else if (t == "submixKey") {
        const int i = std::atoi(text(m, "i").c_str());
        if (i >= 0 && i < rme::Config::kUf8SubmixKeys)
            editRme(c, [&](rme::Config& x) { x.uf8SubmixKeys[i] = v; });
    }
    else if (t == "colour") {
        const int i = std::atoi(text(m, "i").c_str());
        const int e = std::atoi(v.c_str());
        if (i >= 0 && i < 9 && e >= 0 && e < 16) editRme(c, [&](rme::Config& x) { x.colourMap[i] = e; });
    }
    else if (t == "keyAction" || t == "keyLed") applyKey(t, m);
    else if (starts("sk"))   applySoftKey(t, m);
    else if (t == "import") {
        c.importResult = std::to_string(orc::modes::importFromReaper()) + " values imported";
    }
    else if (starts("obs"))  applyObs(t, m, c);
    else if (starts("dyna") || starts("preset")) applyDyna(t, m);
    else if (starts("hue"))  applyHue(t, m);
    else if (t == "chooseLogo") { if (c.host.chooseLogo) c.host.chooseLogo(); }
    else if (starts("mm"))   applyMoreme(t, m, c);
}

std::string stateJson(const Context& c)
{
    auto& mgr = rme::manager();
    const auto cfg = mgr.config();
    const auto st  = mgr.snapshot();
    const auto targets = targetChoices(st);
    const auto outs    = outputChoices(st);
    const auto actions = rmeActions();

    std::string linkText = orc::linkSummary();
    if (!linkText.empty()) linkText[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(linkText[0])));

    std::string o = "{\"t\":\"state\"";
    o += ",\"error\":" + jstr(c.error);
    o += ",\"general\":{\"enabled\":" + std::string(jbool(cfg.enabled))
       + ",\"host\":" + jstr(cfg.host)
       + ",\"sendPort\":" + std::to_string(cfg.sendPort)
       + ",\"recvPort\":" + std::to_string(cfg.recvPort)
       + ",\"status\":" + jstr(linkText)
       + ",\"online\":" + jbool(mgr.link() == rme::LinkState::Online)
       + ",\"loginAvailable\":" + jbool(c.loginAvailable)
#if defined(_WIN32)
       + ",\"loginHint\":" + jstr("Only for the installed ORC.")
#else
       + ",\"loginHint\":" + jstr("Only for the ORC in Applications.")
#endif
       + ",\"loginOn\":" + jbool(c.loginOn)
       + ",\"sleepEnabled\":" + jbool(cfg.sleepEnabled)
       + ",\"sleepMinutes\":" + std::to_string(cfg.sleepMinutes)
#if defined(_WIN32)
       + ",\"usb\":" + usbJson(c)
#endif
       + "}";

    o += ",\"targets\":" + choicesJson(targets) + ",\"outputs\":" + choicesJson(outs);
    o += ",\"pushes\":" + arr(kPushes, [](const PushChoice& p) {
        return "{\"v\":" + jstr(p.value) + ",\"l\":" + jstr(p.title) + "}";
    });
    o += ",\"actions\":" + arr(actions, [](const std::string& a) {
        return "{\"a\":" + jstr(a) + ",\"l\":" + jstr(actionTitle(a)) + "}";
    });
    o += ",\"palette\":" + paletteJson();
    o += ",\"keys\":" + keysJson(actions) + ",\"transportCount\":" + std::to_string(kTransportCount);

    o += ",\"uf1\":{\"use\":" + std::string(jbool(cfg.uf1Enabled))
       + ",\"pots\":" + indexed(rme::Config::kVpotSlots, [&](int i) {
             return "{\"target\":" + specSelected(st, cfg.vpots[i].target, targets)
                  + ",\"push\":" + jstr(cfg.vpots[i].push) + "}";
         })
       + ",\"jogTarget\":" + specSelected(st, cfg.jogTarget, targets)
       + ",\"jogStepDb\":" + jstr(jnum(cfg.jogStepDb))
       + ",\"vpotStepDb\":" + jstr(jnum(cfg.vpotStepDb))
       + ",\"speed\":" + jstr(jnum(cfg.uf1PotSpeed)) + "}";

    o += ",\"uf8\":{\"use\":" + std::string(jbool(cfg.uf8Enabled))
       + ",\"showMain\":" + jbool(cfg.uf8ShowMainOnChange)
       + ",\"speed\":" + jstr(jnum(cfg.uf8PotSpeed))
       + ",\"submix\":" + indexed(rme::Config::kUf8SubmixKeys, [&](int i) {
             return specSelected(st, cfg.uf8SubmixKeys[i], outs);
         }) + "}";

    o += ",\"banks\":" + softKeysJson(st, outs, actions);

    // Who wears a TotalMix colour: mixer channels only. The effects carry a
    // colour we set ourselves (RmeState, State::fx), not one from TotalMix.
    std::array<std::string, 9> wearers;
    for (int r = 0; r < rmeu::kMixerRowCount; ++r) {
        const auto row = static_cast<rmeu::Row>(r);
        for (int ch : rmeu::visibleChannels(st, row)) {
            const auto* cc = rmeu::channelOf(st, row, ch);
            if (!cc) continue;
            const int idx = cc->colour < 0 ? 0 : (cc->colour > 8 ? 8 : cc->colour);
            auto& w = wearers[static_cast<size_t>(idx)];
            if (w.size() > 48) continue;
            if (!w.empty()) w += ", ";
            w += rmeu::displayName(st, row, ch);
        }
    }
    o += ",\"colours\":" + indexed(9, [&](int i) {
        // ⇨ TotalMix' own word for the number, from RmeUf1::colourName.
        std::string name = rmeu::colourName(i);
        if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        const int cur = cfg.colourMap[i] < 0 ? 0 : (cfg.colourMap[i] > 15 ? 15 : cfg.colourMap[i]);
        const bool known = cur == 0 || uf8::paletteSwatch(static_cast<uint8_t>(cur)).has_value();
        return "{\"n\":" + jstr(name) + ",\"v\":" + (known ? std::to_string(cur) : std::string("null"))
             + ",\"w\":" + jstr(wearers[static_cast<size_t>(i)]) + "}";
    });

    // OBS. ⛔ The password never comes back to the page, only whether there is one.
    {
        auto& om = reasixty::obs::manager();
        const auto oc = om.config();
        std::string line = om.status();
        o += ",\"obs\":{\"enabled\":" + std::string(jbool(oc.enabled)) + ",\"host\":" + jstr(oc.host)
           + ",\"port\":" + std::to_string(oc.port) + ",\"hasPassword\":" + jbool(!om.password().empty())
           + ",\"status\":" + jstr(line.empty() ? std::string("off") : line) + "}";
    }
    // DynaMount
    {
        auto& dm = dyna::manager();
        int palCount = 0;
        const uf8::PaletteRgb* pal = uf8::selPaletteRgb(&palCount);
        o += ",\"dyna\":{\"fillRight\":" + std::string(jbool(dm.fillDir() == dyna::FillDir::Right))
           + ",\"swatches\":" + indexed(palCount, [&](int k) { return jstr(hexColour(pal[k].r, pal[k].g, pal[k].b)); })
           + ",\"mounts\":" + indexed(dyna::kMaxMounts, [&](int i) {
                 const auto in = dm.info(i);
                 return "{\"on\":" + std::string(jbool(in.enabled)) + ",\"name\":" + jstr(in.name)
                      + ",\"ip\":" + jstr(in.ip) + ",\"colour\":" + std::to_string(in.color)
                      + ",\"status\":" + jstr(in.online ? std::string(dyna::protoName(in.proto))
                                                        : std::string(in.enabled ? "offline" : "--")) + "}";
             })
           + ",\"presets\":" + indexed(dyna::DynaMountManager::kMaxPresets, [&](int k) {
                 const auto p = dm.preset(k);
                 return "{\"name\":" + jstr(p.name) + ",\"stands\":"
                      + jstr(p.defined() ? std::to_string(p.pos.size()) : std::string("--")) + "}";
             }) + "}";
    }
    // Hue
    {
        auto& hm = hue::manager();
        const auto lights = hm.lights();
        const auto groups = hm.groups();
        const auto scenes = hm.scenes();
        std::string tg = "[{\"v\":\"\",\"l\":\"(none)\"}";
        for (const auto& L : lights) tg += ",{\"v\":" + jstr("L" + L.id) + ",\"l\":" + jstr(L.name) + "}";
        for (const auto& g : groups)
            if (!g.groupedLightId.empty())
                tg += ",{\"v\":" + jstr("G" + g.groupedLightId) + ",\"l\":"
                    + jstr(std::string(g.isZone ? "Zone > " : "Room > ") + g.name) + "}";
        tg += "]";
        std::string sc = "[{\"v\":\"\",\"l\":\"(none)\"}";
        for (const auto& s : scenes)
            sc += ",{\"v\":" + jstr(s.id) + ",\"l\":" + jstr(hue::sceneMenuLabel(s, groups)) + "}";
        sc += "]";
        const hue::Controls ctl = hm.controls();
        o += ",\"hue\":{\"status\":" + jstr(hm.statusLine()) + ",\"ip\":" + jstr(hm.bridgeIp())
           + ",\"fillRight\":" + jbool(hm.fillDir() == hue::HueManager::FillDir::Right)
           + ",\"targets\":" + tg + ",\"scenes\":" + sc
           + ",\"slots\":" + indexed(hue::kMaxSlots, [&](int i) {
                 const hue::SlotConfig s = hm.slot(i);
                 const char* pre = s.kind == hue::TargetKind::Light ? "L" : s.kind == hue::TargetKind::Group ? "G" : "";
                 const std::string val = *pre && !s.rid.empty() ? pre + s.rid : std::string();
                 return "{\"on\":" + std::string(jbool(s.enabled)) + ",\"label\":" + jstr(s.label)
                      + ",\"target\":" + jstr(val) + "}";
             })
           + ",\"sceneSlots\":" + indexed(hue::kMaxScenes, [&](int i) {
                 const hue::SceneSlot s = hm.sceneSlot(i);
                 return "{\"id\":" + jstr(s.id) + ",\"label\":" + jstr(s.label) + "}";
             })
           + ",\"pot\":" + std::to_string(static_cast<int>(ctl.pot))
           + ",\"potFlip\":" + std::to_string(static_cast<int>(ctl.potFlip)) + "}";
    }
    o += ",\"importResult\":" + jstr(c.importResult);
    // MoreMe
    {
        const ph::Settings s = ph::server().settings();
        const ph::Status   ms = ph::server().status();
        const bool serving = s.enabled && ms.listening;
        o += ",\"moreme\":{\"enabled\":" + std::string(jbool(s.enabled))
           + ",\"address\":" + jstr(serving ? ph::pageAddress(s.webPort) : std::string())
           + ",\"pin\":" + jstr(ph::server().pin())
           + ",\"webPort\":" + std::to_string(s.webPort)
           + ",\"sendPort\":" + std::to_string(s.sendPort)
           + ",\"recvPort\":" + std::to_string(s.recvPort)
           + ",\"status\":" + jstr(ph::summary(ms))
           + ",\"good\":" + jbool(serving && ms.tmSeen)
           + ",\"studio\":" + jstr(s.studio)
           + ",\"logoStamp\":" + std::to_string(c.logoStamp)
           + ",\"desk\":" + jstr(serving ? "http://127.0.0.1:" + std::to_string(s.webPort) + "/desk?embed=1"
                                         : std::string())
           + "}";
    }
    return o + "}";
}

} // namespace orc::settings
