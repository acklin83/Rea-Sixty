// orc_manual_dump — what ORC knows about itself, as JSON, for the manual.
//
// ⇨ WHY (Frank 29.09.2026: "Handbuch brauchen wir ein schlaues!"). The lists a
// manual keeps by hand are the ones that drift: which actions a soft key can
// run, what the factory banks hold, which parameters a STRIP page shows and in
// what range, which reverb type shows which control. This program asks the
// code ORC runs for every one of them, and tools/orc_manual.py turns the
// answer into the manual. Plan: docs/orc-manual-plan.md.
//
// Nothing here opens a device or a socket. The bindings engine gets a
// throwaway folder, so the factory banks are seeded into a temp file and never
// into the user's own orc.json.

#include "Bindings.h"
#include "BindingsHost.h"
#include "PushQuiet.h"
#include "RmeBuiltins.h"
#include "RmeInput.h"
#include "RmeManager.h"
#include "RmeState.h"
#include "RmeStrip.h"
#include "RmeUf1.h"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>

namespace bnd  = uf8::bindings;
namespace rme  = reasixty::rme;
namespace rmes = reasixty::rme::strip;
namespace rmeu = reasixty::rme::uf1;

namespace {

std::string esc(const std::string& s)
{
    std::string o = "\"";
    for (const char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                else o += c;
        }
    }
    return o + "\"";
}
std::string esc(const char* s) { return esc(std::string(s ? s : "")); }

std::string num(double v)
{
    char b[32];
    std::snprintf(b, sizeof b, "%.6g", v);
    return b;
}

const char* kindName(rmes::Kind k)
{
    switch (k) {
        case rmes::Kind::Toggle:  return "toggle";
        case rmes::Kind::List:    return "list";
        case rmes::Kind::Db:      return "db";
        case rmes::Kind::Hz:      return "hz";
        case rmes::Kind::Q:       return "q";
        case rmes::Kind::Ms:      return "ms";
        case rmes::Kind::Sec:     return "sec";
        case rmes::Kind::Ratio:   return "ratio";
        case rmes::Kind::Width:   return "width";
        case rmes::Kind::Pan:     return "pan";
        case rmes::Kind::Int:     return "int";
        case rmes::Kind::Pct:     return "pct";
        case rmes::Kind::Factor:  return "factor";
        case rmes::Kind::FxWidth: return "fxwidth";
        case rmes::Kind::Bpm:     return "bpm";
    }
    return "?";
}

const char* needName(rmes::Need n)
{
    switch (n) {
        case rmes::Need::Any:    return "any";
        case rmes::Need::Stereo: return "stereo";
        case rmes::Need::Reverb: return "reverb";
        case rmes::Need::Echo:   return "echo";
    }
    return "?";
}

// The row a page is shown on, for asking the code about one of its parameters.
rmeu::Row rowFor(const std::string& rows, const rmes::Param& p)
{
    if (p.need == rmes::Need::Reverb || p.need == rmes::Need::Echo) return rmeu::Row::Fx;
    if (rows == "out") return rmeu::Row::Output;
    return rmeu::Row::Input;
}

// ⇨ THE PUSH DEFAULT, ASKED OF THE CODE. A mixer where this one parameter sits
// away from any default, then RmeStrip::resetWrites: what it writes is what a
// V-Pot push sets. Empty = the push leaves it alone.
bool pushDefault(const rmes::Param& p, rmeu::Row r, double& out)
{
    rme::State st;
    rme::Channel c;
    c.name = "x"; c.colour = 1; c.seen = true; c.stereo = (p.need == rmes::Need::Stereo);
    // A value no default is likely to have, inside the range.
    const double odd = (p.kind == rmes::Kind::List || p.kind == rmes::Kind::Toggle)
        ? 0.0 : p.lo + (p.hi - p.lo) * 0.37;
    c.setLeaf(p.leaf, odd);
    if (p.revTypes != 0) {
        for (int t = 0; t < 15; ++t)
            if (p.revTypes & (1u << t)) { c.setLeaf("type", t); break; }
    }
    const int ch = (r == rmeu::Row::Fx) ? (p.need == rmes::Need::Echo ? 1 : 0) : 0;
    auto& map = r == rmeu::Row::Fx ? st.fx : r == rmeu::Row::Output ? st.outputs : st.inputs;
    map[ch] = c;
    if (c.stereo) map[ch + 1] = c;
    const auto w = rmes::resetWrites(st, r, ch, p);
    if (w.empty()) return false;
    out = w[0].second;
    return true;
}

}  // namespace

int main()
{
    // A throwaway folder for the bindings engine: the factory banks land here.
    char tmpl[] = "/tmp/orc_manual_dumpXXXXXX";
    const char* dir = mkdtemp(tmpl);
    if (!dir) { std::fprintf(stderr, "orc_manual_dump: no temp folder\n"); return 1; }
    const std::string cfgDir = dir;
    {
        bnd::Host bh;
        bh.configDir  = [cfgDir]() -> std::string { return cfgDir; };
        bh.configFile = []() -> std::string { return "orc.json"; };
        bnd::setHost(std::move(bh));
    }
    bnd::load();
    static rme::input::State in;
    static const rme::input::Host host{};
    rme::registerBuiltins(in, host);

    std::string j = "{\n";

    // ── the actions a soft key or a transport key can run in ORC ──────────
    // The ORC window's own rule (rmeActions in OrcSettingsWindow.mm): the
    // builtins in the RME category.
    j += "  \"actions\": [\n";
    {
        bool first = true;
        for (const auto& n : bnd::builtinNames()) {
            if (std::string(bnd::builtinCategory(n)) != "RME") continue;
            std::string title = bnd::builtinDisplayName(n);
            if (title.rfind("RME: ", 0) == 0) title.erase(0, 5);
            const auto k = bnd::builtinKind(n);
            j += std::string(first ? "" : ",\n") + "    {\"id\": " + esc(n)
               + ", \"title\": " + esc(title)
               + ", \"label\": " + esc(bnd::builtinShortLabel(n))
               + ", \"switch\": " + (k == bnd::BuiltinKind::Switch ? "true" : "false")
               + ", \"description\": " + esc(bnd::builtinDescription(n)) + "}";
            first = false;
        }
    }
    j += "\n  ],\n";

    // ── the factory soft-key banks ────────────────────────────────────────
    j += "  \"banks\": [\n";
    {
        const int base  = bnd::uf1SideCarBankBase(bnd::kUf1SideCarSetRme);
        const int count = bnd::uf1SideCarBankInUseCount(bnd::kUf1SideCarSetRme);
        for (int b = 0; b < count; ++b) {
            const int abs = base + b;
            const auto kind = bnd::getUf1SoftBankDynamic(abs, 0);
            const char* k = kind == bnd::DynamicBankKind::RmeSnapshots ? "snapshots"
                          : kind == bnd::DynamicBankKind::RmeLayouts   ? "layouts" : "keys";
            j += std::string(b ? ",\n" : "") + "    {\"number\": " + std::to_string(b + 1)
               + ", \"kind\": " + esc(k) + ", \"name\": " + esc(bnd::getUf1SoftBankName(abs, 0))
               + ", \"keys\": [";
            for (int half = 0; half < 2; ++half)
                for (int s = 0; s < 4; ++s) {
                    const bnd::Binding bd = bnd::getUf1SoftBankSlot(abs, s);
                    const auto& sp = bd.shortPress[half];
                    std::string action = sp.type == bnd::ActionType::Builtin ? sp.action : "";
                    std::string label = (half == 0 && sp.label.empty()) ? bd.label : sp.label;
                    if (label.empty() && !action.empty()) label = bnd::softKeyFallbackLabel(sp);
                    j += std::string(half || s ? ", " : "") + "{\"key\": "
                       + std::to_string(half * 4 + s + 1) + ", \"action\": " + esc(action)
                       + ", \"label\": " + esc(label) + "}";
                }
            j += "]}";
        }
    }
    j += "\n  ],\n";

    // ── the STRIP pages and every parameter on them ───────────────────────
    const auto pages = rme::defaultStripPages();
    std::vector<std::string> ids;
    std::set<std::string> seen;
    j += "  \"pages\": [\n";
    for (std::size_t i = 0; i < pages.size(); ++i) {
        const auto& pg = pages[i];
        auto list = [&](const std::string (&a)[4]) {
            std::string o = "[";
            for (int k = 0; k < 4; ++k) {
                o += std::string(k ? ", " : "") + esc(a[k]);
                if (!a[k].empty() && seen.insert(a[k]).second) ids.push_back(a[k]);
            }
            return o + "]";
        };
        j += std::string(i ? ",\n" : "") + "    {\"name\": " + esc(pg.name)
           + ", \"rows\": " + esc(pg.rows) + ", \"pots\": " + list(pg.pots)
           + ", \"keys\": " + list(pg.keys) + ", \"graph\": "
           + (rmes::pageShowsGraph(pg) ? "true" : "false") + "}";
    }
    j += "\n  ],\n";

    j += "  \"params\": [\n";
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const rmes::Param* p = rmes::find(ids[i]);
        if (!p) continue;
        std::string rows;
        for (const auto& pg : pages) {
            for (int k = 0; k < 4; ++k)
                if (pg.pots[k] == ids[i] || pg.keys[k] == ids[i]) { rows = pg.rows; break; }
            if (!rows.empty()) break;
        }
        const rmeu::Row r = rowFor(rows, *p);
        std::string names = "[";
        for (std::size_t n = 0; n < p->names.size(); ++n)
            names += std::string(n ? ", " : "") + esc(p->names[n]);
        names += "]";
        std::string namesOut = "[";
        for (std::size_t n = 0; n < p->namesOut.size(); ++n)
            namesOut += std::string(n ? ", " : "") + esc(p->namesOut[n]);
        namesOut += "]";
        std::string types = "[";
        if (p->revTypes != 0)
            for (int t = 0, c = 0; t < 15; ++t)
                if (p->revTypes & (1u << t)) types += std::string(c++ ? ", " : "") + std::to_string(t);
        types += "]";
        double def = 0.0;
        const bool hasDef = pushDefault(*p, r, def);
        j += std::string(i ? ",\n" : "") + "    {\"id\": " + esc(p->id)
           + ", \"leaf\": " + esc(p->leaf) + ", \"label\": " + esc(p->label)
           + ", \"kind\": " + esc(kindName(p->kind)) + ", \"lo\": " + num(p->lo)
           + ", \"hi\": " + num(p->hi) + ", \"step\": " + num(p->step)
           + ", \"need\": " + esc(needName(p->need))
           + ", \"loText\": " + esc(rmes::format(*p, r, p->lo))
           + ", \"hiText\": " + esc(rmes::format(*p, r, p->hi))
           + ", \"names\": " + names + ", \"namesOut\": " + namesOut
           + ", \"reverbTypes\": " + types
           + ", \"pushDefault\": " + (hasDef ? esc(rmes::format(*p, r, def)) : "null") + "}";
    }
    j += "\n  ],\n";

    // ── rows, defaults, the push guard ────────────────────────────────────
    j += "  \"rows\": [";
    for (int r = 0; r < rmeu::kRowCount; ++r)
        j += std::string(r ? ", " : "") + esc(rmeu::rowName(static_cast<rmeu::Row>(r)));
    j += "],\n";

    const rme::Config def;
    j += "  \"defaults\": {\"host\": " + esc(def.host) + ", \"sendPort\": " + std::to_string(def.sendPort)
       + ", \"recvPort\": " + std::to_string(def.recvPort)
       + ", \"jogTarget\": " + esc(def.jogTarget) + ", \"jogStepDb\": " + num(def.jogStepDb)
       + ", \"potStepDb\": " + num(def.vpotStepDb) + ", \"pots\": [";
    for (int i = 0; i < rme::Config::kVpotSlots; ++i)
        j += std::string(i ? ", " : "") + "{\"target\": " + esc(def.vpots[i].target)
           + ", \"push\": " + esc(def.vpots[i].push) + "}";
    j += "]},\n";
    j += "  \"colours\": [";
    for (int i = 0; i < 9; ++i) j += std::string(i ? ", " : "") + esc(rmeu::colourName(i));
    j += "],\n";
    j += "  \"pushQuietMs\": " + std::to_string(reasixty::kPushQuietMs) + "\n";
    j += "}\n";

    std::fputs(j.c_str(), stdout);
    // The throwaway folder goes again.
    std::remove((cfgDir + "/orc.json").c_str());
    ::rmdir(cfgDir.c_str());
    return 0;
}
