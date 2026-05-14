#include "ThemeAssets.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "reaper_plugin_functions.h"
#include "reaper_imgui_functions.h"
#include "miniz.h"

namespace uf8::theme_assets {

namespace {

// HiDPI subfolders tried in priority order. Themes (Reapertips, Default
// 6+, WT family) ship 100/150/200% variants in `100/` `150/` `200/`
// subdirectories of the resource folder. We prefer larger sprites first
// because today's primary target is a Retina MacBook — high-density
// sources rendered at lower logical sizes are crisp; the inverse looks
// blurry. Empty string = base folder (no subfolder prefix).
constexpr const char* kDpiSubdirs[] = { "200/", "150/", "" };

// Per-slot candidate filename list, tried in order across kDpiSubdirs.
// First (subdir × filename) pair that resolves to a readable PNG wins.
// Filenames are case-sensitive on Linux; themes always ship lowercase.
struct SlotSpec {
    Slot                slot;
    const char* const*  candidates;
    int                 nCandidates;
};

constexpr const char* kFaderBgFiles[] = {
    "mcp_volbg.png",          // some themes ship vertical-named here
    "gen_volbg_vert.png",     // WALTER generic vertical fader bg
    "gen_volbg_horz.png",     // last resort — horizontal-only theme
};
constexpr const char* kFaderHandleFiles[] = {
    "mcp_volthumb.png",
    "gen_volthumb_vert.png",
    "gen_volthumb_horz.png",
};
constexpr const char* kMuteOffFiles[]  = {
    "gen_mute_off.png", "mcp_mute_off.png", "mcp_mute.png",
};
constexpr const char* kMuteOnFiles[]   = {
    "gen_mute_on.png",  "mcp_mute_on.png",
};
constexpr const char* kSoloOffFiles[]  = {
    "gen_solo_off.png", "mcp_solo_off.png", "mcp_solo.png",
};
constexpr const char* kSoloOnFiles[]   = {
    "gen_solo_on.png",  "mcp_solo_on.png",
};
constexpr const char* kRecArmOffFiles[] = {
    "mcp_recarm_off.png", "mcp_recarm_norec.png", "mcp_recarm.png",
    "gen_recarm_off.png",
};
constexpr const char* kRecArmOnFiles[]  = {
    "mcp_recarm_on.png",  "mcp_recarm.png",
    "gen_recarm_on.png",
};
constexpr const char* kMeterBgFiles[] = {
    "meter_bg_mcp.png", "mcp_meter_bg.png", "mcp_vu.png",
};
constexpr const char* kKnobFiles[] = {
    "gen_knob_bg_small.png", "mcp_pan_knob_small.png",
    "gen_knob_bg.png", "mcp_knob.png",
};
constexpr const char* kKnobLargeFiles[] = {
    "gen_knob_bg_large.png", "gen_knob_bg.png", "mcp_knob.png",
};

template <int N>
constexpr SlotSpec mk(Slot s, const char* const (&arr)[N]) {
    return { s, arr, N };
}

constexpr SlotSpec kSlotSpecs[] = {
    mk(Slot::kMcpFaderBg,     kFaderBgFiles),
    mk(Slot::kMcpFaderHandle, kFaderHandleFiles),
    mk(Slot::kMcpMuteOff,     kMuteOffFiles),
    mk(Slot::kMcpMuteOn,      kMuteOnFiles),
    mk(Slot::kMcpSoloOff,     kSoloOffFiles),
    mk(Slot::kMcpSoloOn,      kSoloOnFiles),
    mk(Slot::kMcpRecArmOff,   kRecArmOffFiles),
    mk(Slot::kMcpRecArmOn,    kRecArmOnFiles),
    mk(Slot::kMcpMeterBg,     kMeterBgFiles),
    mk(Slot::kMcpKnob,        kKnobFiles),
    mk(Slot::kMcpKnobLarge,   kKnobLargeFiles),
};
static_assert(sizeof(kSlotSpecs) / sizeof(kSlotSpecs[0]) ==
              (size_t)Slot::kCount,
              "kSlotSpecs must cover every Slot");

// Cached state.
std::string                            s_themePath;
int64_t                                s_themeMtime = 0;
bool                                   s_isZip = false;
std::vector<std::vector<uint8_t>>      s_rawBytes((int)Slot::kCount);
ImGui_Context*                         s_lastCtx = nullptr;
ImGui_Image*                           s_images[(int)Slot::kCount] = {};
double                                 s_imgW[(int)Slot::kCount] = {};
double                                 s_imgH[(int)Slot::kCount] = {};

bool endsWith(const std::string& s, const char* suffix)
{
    const size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    return std::memcmp(s.data() + s.size() - n, suffix, n) == 0;
}

int64_t fileMtime(const std::string& p)
{
    if (p.empty()) return 0;
    struct stat st{};
    if (::stat(p.c_str(), &st) != 0) return 0;
    return (int64_t)st.st_mtime;
}

bool readWholeFile(const std::string& p, std::vector<uint8_t>& out)
{
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < 0) { std::fclose(f); return false; }
    out.resize((size_t)sz);
    const bool ok = std::fread(out.data(), 1, (size_t)sz, f) == (size_t)sz;
    std::fclose(f);
    return ok;
}

// Find a zip entry matching `<resourceRoot><subdir><filename>` for any of
// the kDpiSubdirs in priority order. resourceRoot is the top-level folder
// inside the zip (e.g. "Reapertips_unpacked/").
bool readFromZip(const std::string& zipPath,
                 const std::string& resourceRoot,
                 const char* filename,
                 std::vector<uint8_t>& out)
{
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, zipPath.c_str(), 0)) return false;
    bool ok = false;
    for (const char* sub : kDpiSubdirs) {
        std::string path;
        path.reserve(resourceRoot.size() + std::strlen(sub) + std::strlen(filename));
        path.append(resourceRoot);
        path.append(sub);
        path.append(filename);
        const int idx = mz_zip_reader_locate_file(&zip, path.c_str(),
                                                  nullptr, 0);
        if (idx < 0) continue;
        mz_zip_archive_file_stat fs{};
        if (!mz_zip_reader_file_stat(&zip, (mz_uint)idx, &fs)) continue;
        out.resize((size_t)fs.m_uncomp_size);
        if (mz_zip_reader_extract_to_mem(&zip, (mz_uint)idx,
                                         out.data(), out.size(), 0)) {
            ok = true;
            break;
        }
    }
    mz_zip_reader_end(&zip);
    return ok;
}

bool readFromFolder(const std::string& folderRoot,
                    const char* filename,
                    std::vector<uint8_t>& out)
{
    for (const char* sub : kDpiSubdirs) {
        std::string p;
        p.reserve(folderRoot.size() + std::strlen(sub) + std::strlen(filename) + 1);
        p.append(folderRoot);
        if (!folderRoot.empty() && folderRoot.back() != '/') p.push_back('/');
        p.append(sub);
        p.append(filename);
        if (readWholeFile(p, out)) return true;
    }
    return false;
}

// Returns the top-level resource folder name inside the .ReaperThemeZip
// (e.g. "Reapertips_unpacked/" — themes name it differently from the
// .ReaperTheme filename). Empty when the archive layout is flat.
std::string findResourceRoot(const std::string& zipPath)
{
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, zipPath.c_str(), 0)) return {};
    const mz_uint n = mz_zip_reader_get_num_files(&zip);
    std::string root;
    for (mz_uint i = 0; i < n; ++i) {
        char name[260] = {};
        mz_zip_reader_get_filename(&zip, i, name, sizeof(name));
        // We want a top-level directory entry (ends in '/' with no
        // further slashes before the trailing one). Skip __MACOSX
        // metadata folders ZIP utilities add on macOS.
        const size_t L = std::strlen(name);
        if (L == 0 || name[L-1] != '/') continue;
        if (std::strstr(name, "__MACOSX") != nullptr) continue;
        // Count slashes — top-level dir has exactly 1.
        int slashes = 0;
        for (size_t j = 0; j < L; ++j) if (name[j] == '/') ++slashes;
        if (slashes != 1) continue;
        root = name;
        break;
    }
    mz_zip_reader_end(&zip);
    return root;
}

// For an unpacked .ReaperTheme: REAPER unzips a .ReaperThemeZip into a
// folder next to the .ReaperTheme file. The folder is named after the
// resource root inside the original zip, not the .ReaperTheme filename
// (themes commonly mismatch the two — see Reapertips_unpacked/ vs
// "Reapertips Theme v1.82.ReaperTheme"). We probe a few candidate folder
// names in the same parent directory and pick the first that exists.
std::vector<std::string> folderCandidates(const std::string& themePath)
{
    std::vector<std::string> out;
    if (themePath.empty()) return out;

    // Strip the .ReaperTheme extension to get the basename.
    std::string base = themePath;
    if (endsWith(base, ".ReaperTheme")) {
        base.resize(base.size() - std::strlen(".ReaperTheme"));
    }

    // Parent directory + basename → try a few common patterns.
    out.push_back(base);                              // /path/Foo
    out.push_back(base + "_unpacked");                // /path/Foo_unpacked
    // Last path segment of base, plus a few common skin folder names —
    // covers themes where the resource folder is named e.g. "WT_Imperial"
    // even though .ReaperTheme is "WT_Imperial 1.5.ReaperTheme".
    const size_t slash = base.find_last_of('/');
    const std::string parent = (slash == std::string::npos) ? std::string{}
                                                            : base.substr(0, slash + 1);
    const std::string leaf   = (slash == std::string::npos) ? base
                                                            : base.substr(slash + 1);
    // strip trailing version-numbers/whitespace from leaf to get a probable
    // folder name (e.g. "Reapertips Theme v1.82" → "Reapertips Theme")
    auto trimRight = [](std::string s) {
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
        return s;
    };
    if (!leaf.empty()) {
        out.push_back(parent + trimRight(leaf));
        // Replace spaces with underscores for the "_unpacked" pattern
        std::string und = leaf;
        for (auto& c : und) if (c == ' ') c = '_';
        out.push_back(parent + und);
        out.push_back(parent + und + "_unpacked");
    }
    return out;
}

std::string resolveExistingFolder(const std::string& themePath)
{
    for (const auto& cand : folderCandidates(themePath)) {
        struct stat st{};
        if (::stat(cand.c_str(), &st) == 0 && (st.st_mode & S_IFDIR)) {
            return cand;
        }
    }
    return {};
}

std::string resolveActiveTheme()
{
    const char* p = GetLastColorThemeFile();
    if (!p || !*p) return {};
    return std::string{p};
}

void reloadRawBytes()
{
    for (auto& v : s_rawBytes) v.clear();
    if (s_themePath.empty()) return;
    s_isZip = endsWith(s_themePath, ".ReaperThemeZip");

    std::string resourceRoot;
    std::string folderRoot;
    if (s_isZip) {
        resourceRoot = findResourceRoot(s_themePath);
    } else {
        folderRoot = resolveExistingFolder(s_themePath);
    }

    for (const SlotSpec& spec : kSlotSpecs) {
        auto& bytes = s_rawBytes[(int)spec.slot];
        // Per-slot: walk candidate filenames; for each, try DPI subfolders
        // in priority order. First hit wins.
        for (int c = 0; c < spec.nCandidates && bytes.empty(); ++c) {
            const char* fn = spec.candidates[c];
            if (s_isZip) {
                (void)readFromZip(s_themePath, resourceRoot, fn, bytes);
            } else if (!folderRoot.empty()) {
                (void)readFromFolder(folderRoot, fn, bytes);
            }
        }
    }
}

} // namespace

void tick(ImGui_Context* ctx)
{
    // Theme-change probe (cheap fast path: stat + string compare).
    const std::string current = resolveActiveTheme();
    const int64_t mt = fileMtime(current);
    if (current != s_themePath || mt != s_themeMtime) {
        s_themePath = current;
        s_themeMtime = mt;
        reloadRawBytes();
        for (auto& h : s_images) h = nullptr;
        for (auto& d : s_imgW)   d = 0;
        for (auto& d : s_imgH)   d = 0;
    }

    // Context-change probe — MixerWindow recreates ctx on every toggle,
    // forcing handle rebuild against the fresh context.
    if (ctx != s_lastCtx) {
        for (auto& h : s_images) h = nullptr;
        for (auto& d : s_imgW)   d = 0;
        for (auto& d : s_imgH)   d = 0;
        s_lastCtx = ctx;
    }

    if (!ctx) return;

    // Lazy materialisation.
    for (int i = 0; i < (int)Slot::kCount; ++i) {
        if (s_images[i]) continue;
        const auto& bytes = s_rawBytes[(size_t)i];
        if (bytes.empty()) continue;
        s_images[i] = ImGui_CreateImageFromMem(
            reinterpret_cast<const char*>(bytes.data()),
            (int)bytes.size(),
            /*flagsInOptional*/ nullptr);
        if (s_images[i]) {
            ImGui_Attach(ctx, s_images[i]);
            double w = 0, h = 0;
            ImGui_Image_GetSize(s_images[i], &w, &h);
            s_imgW[i] = w;
            s_imgH[i] = h;
        }
    }
}

ImGui_Image* get(Slot s)
{
    const int i = (int)s;
    if (i < 0 || i >= (int)Slot::kCount) return nullptr;
    return s_images[i];
}

bool getSize(Slot s, double* w, double* h)
{
    const int i = (int)s;
    if (w) *w = 0;
    if (h) *h = 0;
    if (i < 0 || i >= (int)Slot::kCount) return false;
    if (!s_images[i]) return false;
    if (w) *w = s_imgW[i];
    if (h) *h = s_imgH[i];
    return true;
}

const char* activeThemePath()
{
    return s_themePath.empty() ? "" : s_themePath.c_str();
}

bool slotLoaded(Slot s)
{
    const int i = (int)s;
    if (i < 0 || i >= (int)Slot::kCount) return false;
    return s_images[i] != nullptr;
}

} // namespace uf8::theme_assets
