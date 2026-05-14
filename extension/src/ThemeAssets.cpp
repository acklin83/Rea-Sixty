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

// WALTER conventions — see https://www.reaper.fm/sdk/walter/images.php.
// Filenames are case-sensitive on Linux; REAPER themes ship lowercase.
constexpr const char* kSlotFile[(int)Slot::kCount] = {
    "mcp_vu.png",
    "tcp_vu.png",
    "mcp_fader.png",
    "mcp_fader_handle.png",
    "mcp_knob.png",
    "mcp_panknob.png",
    "mcp_recarm.png",
    "mcp_mute.png",
    "mcp_solo.png",
};

// Cached state. File-scope rather than struct/class because we want a
// single instance per dylib and the API surface is namespace-based.
std::string                            s_themePath;
int64_t                                s_themeMtime = 0;
bool                                   s_isZip = false;
std::vector<std::vector<uint8_t>>      s_rawBytes((int)Slot::kCount);
ImGui_Context*                         s_lastCtx = nullptr;
ImGui_Image*                           s_images[(int)Slot::kCount] = {};

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

bool readFromZip(const std::string& zipPath, const char* entryName,
                 std::vector<uint8_t>& out)
{
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, zipPath.c_str(), 0)) return false;
    int idx = mz_zip_reader_locate_file(&zip, entryName, nullptr, 0);
    if (idx < 0) { mz_zip_reader_end(&zip); return false; }
    mz_zip_archive_file_stat fs{};
    if (!mz_zip_reader_file_stat(&zip, (mz_uint)idx, &fs)) {
        mz_zip_reader_end(&zip);
        return false;
    }
    out.resize((size_t)fs.m_uncomp_size);
    const mz_bool ok = mz_zip_reader_extract_to_mem(
        &zip, (mz_uint)idx, out.data(), out.size(), 0);
    mz_zip_reader_end(&zip);
    return ok != 0;
}

// For an unpacked .ReaperTheme the PNG resources live in a sibling folder
// named exactly like the .ReaperTheme file minus the extension. E.g.
//   /path/MyTheme.ReaperTheme + /path/MyTheme/mcp_vu.png
std::string folderResourcePath(const std::string& themePath, const char* pngName)
{
    std::string base = themePath;
    if (endsWith(base, ".ReaperTheme")) {
        base.resize(base.size() - std::strlen(".ReaperTheme"));
    }
    base.push_back('/');
    base.append(pngName);
    return base;
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
    for (int i = 0; i < (int)Slot::kCount; ++i) {
        const char* fn = kSlotFile[i];
        if (s_isZip) {
            (void)readFromZip(s_themePath, fn, s_rawBytes[i]);
        } else {
            (void)readWholeFile(folderResourcePath(s_themePath, fn),
                                s_rawBytes[i]);
        }
    }
}

} // namespace

void tick(ImGui_Context* ctx)
{
    // Theme-change probe — cheap fast path when nothing changed.
    const std::string current = resolveActiveTheme();
    const int64_t mt = fileMtime(current);
    if (current != s_themePath || mt != s_themeMtime) {
        s_themePath = current;
        s_themeMtime = mt;
        reloadRawBytes();
        // Drop stale handles so they get rebuilt with fresh PNG data below.
        for (auto& h : s_images) h = nullptr;
    }

    // Context-change probe — MixerWindow recreates the ctx on every
    // toggle, so we must rebuild image handles. ReaImGui auto-GCs the
    // orphaned old handles once the old ctx falls out of the defer
    // cycle (we don't have ImGui_Detach exposed in v0.10 reliably).
    if (ctx != s_lastCtx) {
        for (auto& h : s_images) h = nullptr;
        s_lastCtx = ctx;
    }

    if (!ctx) return;

    // Lazy materialisation. Only allocates handles that don't exist yet
    // and have raw bytes available.
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
        }
    }
}

ImGui_Image* get(Slot s)
{
    const int i = (int)s;
    if (i < 0 || i >= (int)Slot::kCount) return nullptr;
    return s_images[i];
}

bool getUv(Slot s, int frame,
           double* u0, double* v0, double* u1, double* v1)
{
    *u0 = 0.0; *v0 = 0.0; *u1 = 1.0; *v1 = 1.0;
    switch (s) {
        case Slot::kMcpVU: {
            // 4 vertical strips, each W/4 wide × full height.
            // Frame 0..3 selects strip (typically L-active, R-active,
            // L-clip, R-clip — depends on the theme's WALTER layout).
            constexpr int N = 4;
            const int f = std::clamp(frame, 0, N - 1);
            *u0 = (double)f / N;
            *u1 = (double)(f + 1) / N;
            return true;
        }
        case Slot::kTcpVU: {
            // 8 horizontal slices, each W wide × H/8 tall. Frame index
            // selects the level row (0 = silent → 7 = peak/clip).
            constexpr int N = 8;
            const int f = std::clamp(frame, 0, N - 1);
            *v0 = (double)f / N;
            *v1 = (double)(f + 1) / N;
            return true;
        }
        default:
            return false;  // single-frame asset; caller uses uv 0..1.
    }
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
