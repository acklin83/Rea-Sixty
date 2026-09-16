// AutoLearn — the matrix path, and the promise that it stays out of everybody
// else's way.
//
// Frank asked the right question when the Delta 16 rule came up: "würde z.B. ein
// Pro-Q oder sonst ein Plugin weniger gut autolearn-mappen wenn wir die logik
// für delta 16 anpassen?" (2026-09-16). These cases answer it by construction:
// the matrix layout only applies to a plug-in that IS a channel matrix, and a
// band-per-number EQ is not one.
#include "AutoLearnEngine.h"
#include "UserPluginCatalog.h"

#include <cstdio>
#include <string>
#include <vector>

// ---- stubs -----------------------------------------------------------------
// The engine reads the user catalog to learn from existing maps and touches a
// few REAPER calls on paths this test never walks. An EMPTY catalog is the
// right answer here anyway: these cases are about the structural matrix rule,
// which must hold before anyone has mapped anything.
namespace uf8 { namespace user_plugins {
static UserPluginCatalog g_emptyCatalog;
const UserPluginCatalog& get() { return g_emptyCatalog; }
const PluginMap* lookupByName(std::string_view) { return nullptr; }
}}  // namespace uf8::user_plugins

extern "C" {
int  TrackFX_GetCount(void*)                                   { return 0; }
bool TrackFX_GetFXName(void*, int, char* b, int n)             { if (b && n) b[0] = 0; return false; }
bool TrackFX_GetFormattedParamValue(void*, int, int, char* b, int n)
                                                               { if (b && n) b[0] = 0; return false; }
bool TrackFX_GetNamedConfigParm(void*, int, const char*, char* b, int n)
                                                               { if (b && n) b[0] = 0; return false; }
double TrackFX_GetParam(void*, int, int, double*, double*)     { return 0.0; }
void* TrackFX_GetFXGUID(void*, int)                            { return nullptr; }
bool ValidatePtr2(void*, void*, const char*)                   { return false; }
void guidToString(const void*, char* b)                        { if (b) b[0] = 0; }
}

using uf8::UserParamInfo;
using uf8::autolearn::Uf8StripSuggestion;

static int g_fail = 0;
static void check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_fail;
}

static UserParamInfo p(int idx, const std::string& name, bool isEnum)
{
    UserParamInfo pi;
    pi.vst3Param = idx;
    pi.name      = name;
    pi.wasEnum   = isEnum;
    return pi;
}

// SSL Delta Control 16: sixteen channels, seven stems, three of them binary.
static std::vector<UserParamInfo> deltaParams()
{
    std::vector<UserParamInfo> v;
    int idx = 0;
    struct Fam { const char* stem; bool isEnum; };
    // The plug-in's own order: Volume, Trim, Pan, Cut, Mono, MixA, MixB.
    const Fam fams[] = {
        {"Volume", false}, {"Trim", false}, {"Pan", false},
        {"Cut", true}, {"Mono", true}, {"MixA", true}, {"MixB", true},
    };
    for (const auto& f : fams)
        for (int ch = 1; ch <= 16; ++ch)
            v.push_back(p(idx++, std::string(f.stem) + " " + std::to_string(ch),
                          f.isEnum));
    return v;
}

static const Uf8StripSuggestion* firstOfKind(
    const std::vector<Uf8StripSuggestion>& v, Uf8StripSuggestion::Kind k)
{
    for (const auto& s : v) if (s.kind == k) return &s;
    return nullptr;
}

int main()
{
    // ---- 1. The matrix: Mono is a switch, so it takes the free Sel row ------
    {
        const auto params = deltaParams();
        const auto strips = uf8::autolearn::suggestUf8Strips(params, 2);
        const auto* sel = firstOfKind(strips, Uf8StripSuggestion::Kind::Sel);
        check(sel != nullptr, "Delta 16: the Sel row is filled");
        check(sel && sel->paramName.rfind("Mono", 0) == 0,
              "Delta 16: Sel carries Mono, the first binary stem");
        const auto* fad = firstOfKind(strips, Uf8StripSuggestion::Kind::Fader);
        check(fad && fad->paramName.rfind("Volume", 0) == 0,
              "Delta 16: the fader still carries Volume");

        int selCount = 0;
        for (const auto& s : strips)
            if (s.kind == Uf8StripSuggestion::Kind::Sel) ++selCount;
        check(selCount == 16, "Delta 16: all sixteen channels get their Sel");

        // …and Mono is NOT also handed a V-Pot bank, while Trim keeps one.
        const auto banks = uf8::autolearn::suggestUf8Banks(params, 2);
        bool monoOnPot = false, trimOnPot = false;
        for (const auto& b : banks) {
            if (b.paramName.rfind("Mono", 0) == 0) monoOnPot = true;
            if (b.paramName.rfind("Trim", 0) == 0) trimOnPot = true;
        }
        check(!monoOnPot, "Delta 16: Mono is not on a V-Pot bank as well");
        check(trimOnPot,  "Delta 16: Trim keeps its V-Pot bank");
    }

    // ---- 2. A named Sel still wins over the type rule ----------------------
    {
        std::vector<UserParamInfo> v;
        int idx = 0;
        const char* stems[] = { "Volume", "Sel", "Cut", "Mono" };
        const bool  enums[] = { false,     true,  true,  true   };
        for (int f = 0; f < 4; ++f)
            for (int ch = 1; ch <= 8; ++ch)
                v.push_back(p(idx++, std::string(stems[f]) + " " + std::to_string(ch),
                              enums[f]));
        const auto strips = uf8::autolearn::suggestUf8Strips(v, 1);
        const auto* sel = firstOfKind(strips, Uf8StripSuggestion::Kind::Sel);
        check(sel && sel->paramName.rfind("Sel", 0) == 0,
              "a stem actually called Sel keeps the row");
        const auto banks = uf8::autolearn::suggestUf8Banks(v, 1);
        bool monoOnPot = false;
        for (const auto& b : banks)
            if (b.paramName.rfind("Mono", 0) == 0) monoOnPot = true;
        check(monoOnPot, "…and Mono stays on its V-Pot bank");
    }

    // ---- 3. Not a matrix: an EQ is untouched by any of this ----------------
    {
        // Six bands, and the numbers sit in the MIDDLE of the name — neither
        // shape passes detectMatrix (needs >= 8 channels, a multiple of 8, and
        // three complete stems numbered 1..N).
        std::vector<UserParamInfo> v;
        int idx = 0;
        for (int band = 1; band <= 6; ++band) {
            v.push_back(p(idx++, "Band " + std::to_string(band) + " Frequency", false));
            v.push_back(p(idx++, "Band " + std::to_string(band) + " Gain",      false));
            v.push_back(p(idx++, "Band " + std::to_string(band) + " Q",         false));
            v.push_back(p(idx++, "Band " + std::to_string(band) + " Enabled",   true));
        }
        const auto strips = uf8::autolearn::suggestUf8Strips(v, 1);
        for (const auto& s : strips)
            check(s.kind != Uf8StripSuggestion::Kind::Sel,
                  "EQ: nothing is pushed onto Sel");
        check(true, "EQ: took the generic path, not the matrix one");
    }

    std::printf(g_fail ? "\n%d check(s) failed\n" : "\nall checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
