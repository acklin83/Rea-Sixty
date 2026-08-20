#include "SslPresetLibrary.h"

#include "PluginChunkPatch.h"
#include "LogPath.h"
#include "reaper_plugin_functions.h"
#include "PluginMap.h"   // uf8::fxIdentityName

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sys/stat.h>
#if defined(_WIN32)
#  include <windows.h>
#else
#  include <dirent.h>
#endif

namespace sslpreset {

// ── SSL Meter / Meter Pro preset library ───────────────────────────────────
// The SSL Meter plug-ins expose NO factory presets to the host (REAPER's
// TrackFX_GetPreset is empty) — which is why the UF1 PRESETS browser showed
// blank entries. SSL keeps them as its OWN XML files on disk, e.g.
//   /Library/Application Support/Solid State Logic/PlugIns/Presets/MeterPro/*.xml
// Each is <PARAM id=".." value=".."/> carrying the plug-in's native PLAIN param
// values. We enumerate the files for the pinned instance and load one by setting
// the matching host params (SSL id → REAPER param NAME → index → raw value).
//
// The SSL preset `id` is NOT the host param name (JUCE hashes it into the VST3
// ParamID), so we map id → the exact GetParamName string. Those strings are
// verbatim from docs/ssl-native-params/VST3__SSL_Meter_Pro_(SSL).md (that dump
// WAS made with GetParamName, so they match at runtime). Ids omitted here are
// pure UI-view state (row-expanded / selected-view / readout-slot) — not host
// params, nothing to restore. The basic "SSL Meter" is a subset; its missing
// (loudness) names simply don't resolve and are skipped.
struct SslMeterPresetMap { const char* id; const char* name; };
static const SslMeterPresetMap kSslMeterPresetMap[] = {
    {"AnalogueCustomMeterLeft",         "Analogue Custom Meter - Left"},
    {"AnalogueCustomMeterRight",        "Analogue Custom Meter - Right"},
    {"AnalogueMetersDualFormat",        "Analogue Dual Format"},
    {"AnalogueMetersLedOverload",       "Analogue Meters LED Overload"},
    {"AnalogueMetersMode",              "Analogue Mode"},
    {"AnalogueMetersPeakHold",          "Analogue Max Needle"},
    {"AnalogueMetersRefLevel",          "Analogue Reference Level"},
    {"AnalogueMetersVUOffset",          "0 VU Line-Up"},
    {"Bypass",                          "Bypass"},
    {"ChannelFormat",                   "Channel Format"},
    {"DigitalMetersPeakHold",           "Digital Peak Hold"},
    {"DigitalMetersRmsIntegrationTime", "RMS Integration"},
    {"DigitalMetersTruePeak",           "Digital True Peak"},
    {"DigitalMetersType",               "Digital Type"},
    {"GlobalDelay",                     "Global Delay"},
    {"LissajousFadeTime",               "Lissajous Fade Time"},
    {"LoudnessAlertDialogueI",          "Int Loudness Dialogue Alert"},
    {"LoudnessAlertDialogueRangeMax",   "Dialogue Range Max Alert"},
    {"LoudnessAlertDialogueRangeMin",   "Dialogue Range Min Alert"},
    {"LoudnessAlertI",                  "Integrated Loudness Alert"},
    {"LoudnessAlertM",                  "Momentary Max Alert"},
    {"LoudnessAlertRangeMax",           "Loudness Range Max Alert"},
    {"LoudnessAlertRangeMin",           "Loudness Range Min Alert"},
    {"LoudnessAlertS",                  "Short Term Max Alert"},
    {"LoudnessAlertTruePeak",           "True Peak Max Alert"},
    {"LoudnessDialogueDetection",       "Dialogue Detection"},
    {"LoudnessGateModeI",               "Int Loudness Gating Mode"},
    {"LoudnessHistoryAutoScroll",       "Loudness History Scroll"},
    {"LoudnessHistoryViewSize",         "Loudness History Window Size"},
    {"LoudnessIntegrationTimeM",        "Momentary Integration Time"},
    {"LoudnessIntegrationTimeS",        "Short-Term Integration Time"},
    {"LoudnessMeterDisplayRelative",    "Loudness Display Type"},
    {"LoudnessMeterScaleRange",         "Loudness Meter Scale Range"},
    {"LoudnessMinDialogueContent",      "Minimum Dialogue Content"},
    {"LoudnessModeLeqm",                "Loudness Mode"},
    {"LoudnessOperation",               "Loudness Measurement Operation"},
    {"LoudnessOverlapIEnabled",         "Integrated Overlap"},
    {"LoudnessSurroundWeighting",       "Loudness Surround Weighting"},
    {"LoudnessTargetI",                 "Loudness Integrated Target"},
    {"LoudnessTargetIVariance",         "Integrated Target Variance"},
    {"LoudnessTerminologyK",            "Loudness Terminology"},
    {"LoudnessTruePeakEnabled",         "Loudness True Peak"},
    {"RtaAveraging",                    "RTA Averaging"},
    {"RtaPeakHold",                     "RTA Peak Hold"},
    {"RtaScaleBottom",                  "RTA Scale Bottom"},
    {"RtaScaleTop",                     "RTA Scale Top"},
    {"RtaSource",                       "RTA Analysis Source"},
    {"RtaWeighting",                    "RTA Weighting"},
    {"SumCompensation",                 "Ana/RTA Sum Compensation"},
    {"TruePeakHighQuality",             "True Peak High Quality"},
};

// Directory holding the pinned Meter instance's SSL preset XMLs, or "" if the FX
// is not an SSL Meter or this platform isn't wired.
std::string presetDir(MediaTrack* tr, int fx)
{
    // fxIdentityName, not TrackFX_GetFXName: it prefers the plug-in's FACTORY
    // name, so renaming the FX in REAPER does not silently empty the preset
    // browser. Same reason uf1FindMeterFx_ uses it.
    char nm[256] = {0};
    if (!uf8::fxIdentityName(tr, fx, nm, sizeof(nm))) return std::string();
    const std::string s = nm;
    // SSL files every plug-in's presets under its own leaf, and the folder names
    // are NOT the plug-in names — they are these. Longest match first, or
    // "Meter Pro" lands in the plain Meter's folder and "360 Link Bus
    // Compressor" in 360 Link's.
    static const struct { const char* part; const char* leaf; } kDirs[] = {
        { "Meter Pro",                 "MeterPro"                },
        { "Meter",                     "Meter"                   },
        { "Channel Strip 2",           "ChannelStrip2"           },
        { "360 Link Bus Compressor",   "SSL360LinkBusCompressor" },
        { "360 Link",                  "SSL360Link"              },
        { "Bus Compressor 2",          "BusCompressor2"          },
        { "4K B",                      "SSL4KB"                  },
        { "4K E",                      "SSL4KE"                  },
        { "4K G",                      "SSL4KG"                  },
    };
    const char* leaf = nullptr;
    for (const auto& d : kDirs)
        if (s.find(d.part) != std::string::npos) { leaf = d.leaf; break; }
    if (!leaf) return std::string();
#if defined(__APPLE__)
    return std::string("/Library/Application Support/Solid State Logic/PlugIns/Presets/") + leaf;
#elif defined(_WIN32)
    // VERIFIED on the Windows rig 2026-08-20 (StoerPC): the same tree, rooted at
    // ProgramData —  C:\ProgramData\Solid State Logic\PlugIns\Presets\MeterPro
    // holds the XMLs, byte for byte the layout macOS has. Read the root from the
    // ENVIRONMENT rather than hardcoding C:\ProgramData: it is relocatable, and
    // a hardcoded drive letter is the classic way this breaks on someone else's
    // machine while working on ours.
    const char* pd = std::getenv("ProgramData");
    if (!pd || !*pd) pd = "C:\\ProgramData";
    return std::string(pd) + "\\Solid State Logic\\PlugIns\\Presets\\" + leaf;
#else
    // Linux gets NOTHING, and that is the finished answer rather than a gap: SSL
    // ships no Linux plug-ins at all (their own system requirements, checked
    // 2026-08-20, list macOS and Windows only). There is no folder to point at,
    // so the browser correctly comes back empty instead of hunting for one.
    (void)leaf;
    return std::string();
#endif
}

// Enumerate *.xml preset names (without extension) in `dir`, NATURAL sort (so
// "+9" precedes "+18" — matches SSL 360's own list order, cap uf1_rp).
std::vector<Entry> scan(const std::string& dir)
{
    std::vector<Entry> out;
    if (dir.empty()) return out;
#if defined(_WIN32)
    const char* kSep = "\\";
#else
    const char* kSep = "/";
#endif
    // ⚠ A correct PATH is only half of it — this scan was POSIX-only, so even
    // with the right folder Windows came back empty (2026-08-20).
    // ⚠ AND ONE LEVEL IS NOT ENOUGH. The 4K E ships 114 presets and only 18 of
    // them are in the first two levels: its "Producer Presets" folder holds one
    // folder PER PRODUCER. A one-level scan showed a sixth of the library and
    // looked perfectly complete doing it. Recursive, with a depth cap so a
    // symlink loop cannot hang the paint thread.
    // The display name is the DEEPEST folder plus the file ("Adrian Hall/Kick
    // In"), not the whole relative path: the row is 24 bytes, and the deepest
    // folder is the one that says something.
    auto take = [&](const std::string& fn, const std::string& in,
                    const std::string& group) {
        if (fn.size() <= 4) return;
        const std::string ext = fn.substr(fn.size() - 4);
        if (ext != ".xml" && ext != ".XML") return;
        const std::string bare = fn.substr(0, fn.size() - 4);
        out.push_back({ bare, group, in + kSep + fn });
    };
    struct Walk {
        decltype(take)& take;
        const char* sep;
        // `group` accumulates the path BELOW the root, '/'-joined, so a surface
        // can rebuild the folder tree from it.
        void go(const std::string& in, const std::string& group, int depth) {
            if (depth > 4) return;
#if defined(_WIN32)
            WIN32_FIND_DATAA fd;
            HANDLE h = FindFirstFileA((in + "\\*").c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) return;
            do {
                const std::string fn = fd.cFileName;
                if (fn == "." || fn == "..") continue;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    go(in + sep + fn, group.empty() ? fn : group + "/" + fn,
                       depth + 1);
                else
                    take(fn, in, group);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
#else
            DIR* d = opendir(in.c_str());
            if (!d) return;
            while (dirent* e = readdir(d)) {
                const std::string fn = e->d_name;
                if (fn == "." || fn == "..") continue;
                const std::string full = in + sep + fn;
                struct stat st{};
                if (stat(full.c_str(), &st) != 0) continue;
                if (S_ISDIR(st.st_mode))
                    go(full, group.empty() ? fn : group + "/" + fn, depth + 1);
                else                     take(fn, in, group);
            }
            closedir(d);
#endif
        }
    } walk{take, kSep};
    walk.go(dir, std::string(), 0);
    std::sort(out.begin(), out.end(), [](const Entry& ea,
                                         const Entry& eb) {
        const std::string a = ea.group.empty() ? ea.name : ea.group + "/" + ea.name;
        const std::string b = eb.group.empty() ? eb.name : eb.group + "/" + eb.name;
        size_t i = 0, j = 0;
        while (i < a.size() && j < b.size()) {
            const unsigned char ca = a[i], cb = b[j];
            if (std::isdigit(ca) && std::isdigit(cb)) {
                size_t i2 = i, j2 = j;                 // spans of the two digit runs
                while (i2 < a.size() && std::isdigit((unsigned char)a[i2])) ++i2;
                while (j2 < b.size() && std::isdigit((unsigned char)b[j2])) ++j2;
                size_t is = i;  while (is < i2 - 1 && a[is] == '0') ++is;   // strip leading 0s
                size_t js = j;  while (js < j2 - 1 && b[js] == '0') ++js;
                if (i2 - is != j2 - js) return (i2 - is) < (j2 - js);       // fewer digits = smaller
                for (size_t k = 0; k < i2 - is; ++k)
                    if (a[is + k] != b[js + k]) return a[is + k] < b[js + k];
                i = i2; j = j2;
            } else {
                const int la = std::tolower(ca), lb = std::tolower(cb);
                if (la != lb) return la < lb;
                ++i; ++j;
            }
        }
        return (a.size() - i) < (b.size() - j);
    });
    return out;
}

// Resolve the dir + scan the list for the pinned instance. Main-thread.

// ── SSL preset value → REAPER's parameter domain ─────────────────────────
// ⛔ THE NUMBER IN THE FILE IS NOT A NORMALISED VALUE, and normalised is the only
// domain REAPER has for a VST3: the format exposes every parameter as 0..1 plus a
// string formatter. So TrackFX_SetParam(…, 500.0) for "500 ms" does not say
// 500 ms, it says far past the top of the range — and the parameter rails. Loading
// a preset wrote a wall of extremes into the plug-in instead of the preset
// (Frank 2026-08-20: "presets werden nicht geladen").
//
// SSL writes each value in the PLUG-IN's own domain, and JUCE has two of those:
//   * a CHOICE parameter stores its INDEX. RtaScaleBottom="11.0" is the twelfth
//     entry (-120 dB), not eleven decibels — and RtaPeakHold="4.0" is the fifth
//     entry, which reads "3 sec", so the index is not the seconds either.
//   * a FLOAT parameter stores its PLAIN value in its own unit.
//     AnalogueMetersRefLevel="-18.0" really is -18.0 dBFS.
// Both halves are verified rather than assumed: the factory "Default Preset.xml"
// read against the 57-parameter dump reproduces every stepped id as
// index/(steps-1) and every continuous one as its plain value inside its own
// range (RMS Integration 500 → (500-10)/4990 = 0.0982, the dump's figure to the
// last digit).
//
// WHICH of the two a parameter is comes from REAPER (TrackFX_GetParameterStepSizes),
// never from a table, and the continuous ones are inverted through the plug-in's
// OWN formatter: read the number it prints at each end of the range, aim, bisect,
// then check that it reads back as the number the file asked for. Nothing here
// hardcodes a range, and whatever fails that check is SKIPPED instead of guessed
// — the same rule, for the same reason, as the parameter NAMES above.

// Leading number of a formatted value ("-18.0 dBFS", "+4 dBu", "500.0 ms").
// False for the strings that carry no number at all ("Off", "Infinite",
// "L+R Sum") — exactly the ones no number may be guessed out of.
static bool sslFmtNumber_(const char* s, double& out)
{
    if (!s) return false;
    while (*s == ' ' || *s == '\t' || *s == '+') ++s;
    const char* d = (*s == '-') ? s + 1 : s;
    if (!((*d >= '0' && *d <= '9') || *d == '.')) return false;  // keeps strtod off
    char* end = nullptr;                                         // "inf" / "nan"
    const double v = std::strtod(s, &end);
    if (end == s) return false;
    out = v;
    return true;
}

// One reading of the plug-in's own value formatter at a normalised position.
// Prefers the non-destructive host call and falls back to putting the parameter
// there and reading it back, because TrackFX_FormatParamValueNormalized is
// documented to need the Cockos VST extensions, which a VST3 need not carry.
// The fallback is acceptable HERE and nowhere else: we are mid-preset-load, the
// parameter is about to be overwritten anyway, and the caller puts it back when
// the search fails.
struct SslFmtProbe {
    MediaTrack* tr; int fx; int idx; bool destructive;
    bool at(double norm, double& num) const
    {
        char b[128] = {0};
        if (!destructive) {
            if (!TrackFX_FormatParamValueNormalized ||          // null-guarded like
                !TrackFX_FormatParamValueNormalized(tr, fx, idx, // GetParamFromIdent
                                                    norm, b, sizeof(b))
                || !b[0]) return false;
        } else {
            TrackFX_SetParamNormalized(tr, fx, idx, norm);
            if (!TrackFX_GetFormattedParamValue(tr, fx, idx, b, sizeof(b))
                || !b[0]) return false;
        }
        return sslFmtNumber_(b, num);
    }
};

// Preset value (choice index or plain unit) → REAPER's 0..1 for THIS parameter on
// THIS instance. false = could not be established, and then the caller must leave
// the parameter alone rather than write an approximation of it.
static bool sslPresetNorm_(MediaTrack* tr, int fx, int idx, double v, double& out)
{
    // 1. Stepped (choice / toggle): the file value is the INDEX, and REAPER
    //    reports the step as 1/(steps-1) of the normalised range.
    double step = 0.0, smallStep = 0.0, largeStep = 0.0; bool isToggle = false;
    if (TrackFX_GetParameterStepSizes &&
        TrackFX_GetParameterStepSizes(tr, fx, idx, &step, &smallStep, &largeStep,
                                      &isToggle)
        && step > 0.0 && step <= 1.0) {
        const double spanF = 1.0 / step;               // = steps - 1
        const double span  = std::round(spanF);
        const double i     = std::round(v);
        if (span >= 1.0 && std::fabs(spanF - span) < 0.01 &&
            std::fabs(v - i) < 0.01 && i >= 0.0 && i <= span) {
            out = std::clamp(i / span, 0.0, 1.0);
            return true;
        }
        // Not one of this parameter's indices, so it is not an index at all
        // (REAPER also reports a step for some continuous parameters). Fall
        // through and read the number as a plain value.
    }

    // 2. Continuous: invert the formatter. Any probing that MOVED the parameter
    //    is undone here — the write below is the only one that may stand.
    const double before = TrackFX_GetParamNormalized(tr, fx, idx);
    SslFmtProbe pr{tr, fx, idx, false};
    double lo = 0.0, hi = 0.0;
    if (!pr.at(0.0, lo) || !pr.at(1.0, hi)) {
        pr.destructive = true;
        if (!pr.at(0.0, lo) || !pr.at(1.0, hi)) {
            TrackFX_SetParamNormalized(tr, fx, idx, before);
            return false;
        }
    }
    bool ok = false;
    double bestN = 0.0, bestV = 0.0;
    if (hi != lo) {
        const double tol = std::fabs(hi - lo) * 0.02;
        const bool   asc = hi > lo;
        double a = 0.0, b = 1.0;
        double n   = std::clamp((v - lo) / (hi - lo), 0.0, 1.0);  // linear first —
        double num = 0.0;                                         // most ranges are
        if (pr.at(n, num)) {
            bestN = n; bestV = num;
            for (int it = 0; it < 16 && std::fabs(bestV - v) > 1e-9; ++it) {
                if ((num < v) == asc) a = n; else b = n;
                n = 0.5 * (a + b);
                if (!pr.at(n, num)) break;
                if (std::fabs(num - v) < std::fabs(bestV - v)) { bestN = n; bestV = num; }
            }
            ok = std::fabs(bestV - v) <= tol;
        }
    }
    if (pr.destructive) TrackFX_SetParamNormalized(tr, fx, idx, before);
    if (!ok) return false;
    out = bestN;
    return true;
}

// Load one SSL preset XML into the pinned instance: every id the plug-in still
// has as a host parameter is set to the preset's value, converted into REAPER's
// domain by sslPresetNorm_ above. Returns the number of params applied.
// Main-thread.
int load(MediaTrack* tr, int fx, const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        // Silence here used to look exactly like "the surface never called me".
        if (FILE* lg = std::fopen(uf8::logPath("rea_sixty.log").c_str(), "a")) {
            std::fprintf(lg, "[preset] cannot open (%s)\n", path.c_str());
            std::fclose(lg);
        }
        return 0;
    }
    std::string xml;
    { char buf[4096]; size_t n; while ((n = fread(buf, 1, sizeof(buf), f)) > 0) xml.append(buf, n); }
    fclose(f);

    // ⇨ THE PLUG-IN'S OWN ROUTE FIRST. Setting host parameters (below) reproduces
    // the preset's VALUES, and the plug-in still reads "the last preset,
    // modified" — because SSL does not derive the loaded preset from the values,
    // it keeps it as an attribute in its state (<A LastLoadedPreset="…">). Frank
    // 2026-08-20: "er soll doch im plugin auch das preset laden! jetzt ist es
    // einfach das geladene preset mit änderungen!". So write the state, and keep
    // the parameter path as the fallback for a chunk this cannot safely rewrite.
    char chunkDiag[256] = {0};
    if (const int viaChunk = uf8::loadSslPresetIntoInstance(
            tr, fx, path.c_str(), xml.c_str(), chunkDiag, sizeof(chunkDiag))) {
        if (FILE* lg = std::fopen(uf8::logPath("rea_sixty.log").c_str(), "a")) {
            std::fprintf(lg,
                         "[uf1] meter preset: %d values via chunk  track=%d fx=%d"
                         "  (%s)\n",
                         viaChunk,
                         (int)GetMediaTrackInfo_Value(tr, "IP_TRACKNUMBER"), fx,
                         path.c_str());
            std::fclose(lg);
        }
        return viaChunk;
    }

    // name → param-index for this FX (GetParamName strings match the map's `name`).
    std::map<std::string, int> nameToIdx;
    const int nParams = TrackFX_GetNumParams(tr, fx);
    for (int i = 0; i < nParams; ++i) {
        char pn[128] = {0};
        if (TrackFX_GetParamName(tr, fx, i, pn, sizeof(pn)) && pn[0])
            nameToIdx.emplace(pn, i);   // first wins → the two "Bypass" resolve to index 0
    }
    auto idToIdx = [&](const std::string& id) -> int {
        // Fast path: a host that exposes the plug-in's own ident resolves directly
        // (null-guarded — older REAPER lacks the import).
        if (TrackFX_GetParamFromIdent) {
            const int di = TrackFX_GetParamFromIdent(tr, fx, id.c_str());
            if (di >= 0) return di;
        }
        for (const auto& m : kSslMeterPresetMap)
            if (id == m.id) {
                const auto it = nameToIdx.find(m.name);
                return it != nameToIdx.end() ? it->second : -1;
            }
        return -1;
    };

    int applied = 0, skipped = 0;
    for (size_t pos = 0; (pos = xml.find("<PARAM", pos)) != std::string::npos; ) {
        const size_t end = xml.find("/>", pos);
        if (end == std::string::npos) break;
        const std::string tag = xml.substr(pos, end - pos);
        pos = end + 2;
        const size_t ip = tag.find("id=\"");
        const size_t vp = tag.find("value=\"");
        if (ip == std::string::npos || vp == std::string::npos) continue;
        const size_t ie = tag.find('"', ip + 4);
        const size_t ve = tag.find('"', vp + 7);
        if (ie == std::string::npos || ve == std::string::npos) continue;
        const int idx = idToIdx(tag.substr(ip + 4, ie - (ip + 4)));
        if (idx < 0) continue;   // view state, or a variant without this parameter
        const double v = atof(tag.substr(vp + 7, ve - (vp + 7)).c_str());
        double norm = 0.0;
        if (!sslPresetNorm_(tr, fx, idx, v, norm)) { ++skipped; continue; }
        TrackFX_SetParamNormalized(tr, fx, idx, norm);
        ++applied;
    }
    // One line per load, because the COUNTS are the diagnosis if a preset ever
    // looks wrong again: "0 applied" is a different bug from "41 applied,
    // 6 skipped", and neither is visible on the surface.
    if (FILE* lg = std::fopen(uf8::logPath("rea_sixty.log").c_str(), "a")) {
        std::fprintf(lg,
                     "[uf1] meter preset (param fallback: %s): %d applied, "
                     "%d skipped  track=%d fx=%d  (%s)\n",
                     chunkDiag[0] ? chunkDiag : "?", applied, skipped,
                     (int)GetMediaTrackInfo_Value(tr, "IP_TRACKNUMBER"), fx,
                     path.c_str());
        std::fclose(lg);
    }
    return applied;
}

// Apply a Meter-view V-Pot detent. id = uf1::enc::kVpot1..kVpot4. Main-thread
// only (drained). No-op when not in Meter view or no SSL Meter plug-in is found.

std::vector<Entry> listFor(MediaTrack* tr, int fx)
{
    return scan(presetDir(tr, fx));
}

}  // namespace sslpreset
