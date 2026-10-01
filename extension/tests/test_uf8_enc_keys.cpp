// The UF8 zoom pad and ENC PUSH per encoder mode (Baustein H, plan
// docs/fixed-lanes-plan.md). Runs the real bindings engine: the id arithmetic,
// the factory seed, the v51 upgrade on every layer, and that a mode's own
// binding survives it.

#include "Bindings.h"
#include "BindingsHost.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace bnd = uf8::bindings;
using bnd::ButtonId;

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

static std::string g_dir;

static const ButtonId kBases[] = {
    ButtonId::ZoomUp, ButtonId::ZoomDown, ButtonId::ZoomLeft,
    ButtonId::ZoomRight, ButtonId::ZoomCenter, ButtonId::ChannelPush,
};
static const int P = static_cast<int>(bnd::Modifier::Plain);

static std::string plainAction(int layer, ButtonId id)
{
    return bnd::getBinding(layer, id).shortPress[P].action;
}

static void setVersion(int v)
{
    const std::string path = g_dir + "/bindings.json";
    std::ifstream in(path);
    std::stringstream ss; ss << in.rdbuf();
    std::string js = ss.str();
    const size_t k = js.find("\"version\"");
    EXPECT(k != std::string::npos);
    if (k == std::string::npos) return;
    size_t a = js.find(':', k) + 1;
    while (a < js.size() && js[a] == ' ') ++a;
    size_t b = a;
    while (b < js.size() && js[b] >= '0' && js[b] <= '9') ++b;
    js.replace(a, b - a, std::to_string(v));
    std::ofstream(path) << js;
}

int main()
{
    char tmpl[] = "/tmp/rs_uf8enckeysXXXXXX";
    g_dir = mkdtemp(tmpl);
    bnd::Host h;
    h.configDir  = [] { return g_dir; };
    h.configFile = [] { return std::string("bindings.json"); };
    bnd::setHost(std::move(h));
    bnd::registerModifierBuiltins(nullptr);
    bnd::load();

    // ── ids: six groups, mode-minor, and back ──────────────────────────────────
    for (const ButtonId base : kBases) {
        EXPECT(bnd::followsUf8EncMode(base));
        for (int m = 0; m < bnd::kUf8EncModeCountForKeys; ++m) {
            const ButtonId id = bnd::perEncModeUf8Id(base, m);
            EXPECT(id != base);
            ButtonId b = ButtonId::None; int mm = -1;
            EXPECT(bnd::splitPerEncModeUf8Id(id, &b, &mm));
            EXPECT(b == base && mm == m);
            // A name, or it would not be saved.
            EXPECT(bnd::fromName(bnd::toName(id)) == id);
        }
        EXPECT(bnd::perEncModeUf8Id(base, -1) == base);
        EXPECT(bnd::perEncModeUf8Id(base, bnd::kUf8EncModeCountForKeys) == base);
    }
    EXPECT(bnd::perEncModeUf8Id(ButtonId::Fine, 3) == ButtonId::Fine);
    EXPECT(!bnd::followsUf8EncMode(ButtonId::Fine));
    EXPECT(!bnd::splitPerEncModeUf8Id(ButtonId::ZoomUp, nullptr, nullptr));
    EXPECT(!bnd::splitPerEncModeUf8Id(ButtonId::Uf1Jog, nullptr, nullptr));
    // UF8 controls, not UF1 (the `<= Uf1Jog` range checks).
    EXPECT(bnd::perEncModeUf8Id(ButtonId::ZoomUp, 0) > ButtonId::Uf1Jog);

    // ── factory: every mode does what the key did, except Lanes (the last mode),
    // which brings its own cross ───────────────────────────────────────────────
    const int kLanes = bnd::kUf8EncModeCountForKeys - 1;
    for (int L = 0; L < 3; ++L) {
        EXPECT(plainAction(L, bnd::perEncModeUf8Id(ButtonId::ZoomUp, kLanes)) == "lane_prev");
        EXPECT(plainAction(L, bnd::perEncModeUf8Id(ButtonId::ZoomDown, kLanes)) == "lane_next");
        EXPECT(plainAction(L, bnd::perEncModeUf8Id(ButtonId::ZoomCenter, kLanes)) == "lane_comp_here");
        EXPECT(plainAction(L, bnd::perEncModeUf8Id(ButtonId::ChannelPush, kLanes)) == "lane_comp_here");
        EXPECT(bnd::getBinding(L, bnd::perEncModeUf8Id(ButtonId::ZoomRight, kLanes))
                   .shortPress[static_cast<int>(bnd::Modifier::Shift)].action == "lane_ab");
    }
    for (int m = 0; m < kLanes; ++m) {
        EXPECT(plainAction(0, bnd::perEncModeUf8Id(ButtonId::ZoomUp, m)) == "zoom_up");
        EXPECT(plainAction(0, bnd::perEncModeUf8Id(ButtonId::ZoomCenter, m)) == "zoom_center");
        EXPECT(plainAction(0, bnd::perEncModeUf8Id(ButtonId::ChannelPush, m))
               == "show_focused_plugin_gui");
    }

    // ── v51 upgrade: a v50 file has no per-mode ids. Each layer's own zoom
    // binding is copied into that layer's slots, and a slot with an action of
    // its own is left alone. ─────────────────────────────────────────────────
    {
        for (int L = 0; L < 3; ++L)
            for (const ButtonId base : kBases)
                for (int m = 0; m < bnd::kUf8EncModeCountForKeys; ++m)
                    bnd::clearBinding(L, bnd::perEncModeUf8Id(base, m));
        // Layer 2 zooms with something else.
        bnd::Binding up2 = bnd::getBinding(0, ButtonId::ZoomUp);
        up2.shortPress[P].action = "mod_shift";
        bnd::setBinding(1, ButtonId::ZoomUp, up2);
        bnd::save();
        setVersion(50);
        bnd::load();
        EXPECT(plainAction(0, bnd::perEncModeUf8Id(ButtonId::ZoomUp, 4)) == "zoom_up");
        EXPECT(plainAction(1, bnd::perEncModeUf8Id(ButtonId::ZoomUp, 4)) == "mod_shift");
        // Layer 3: a copy exactly when its key has a binding there; without
        // one the slot stays empty and dispatch falls back to the key.
        EXPECT(bnd::bindingHasAnyAction(
                   bnd::getBinding(2, bnd::perEncModeUf8Id(ButtonId::ZoomUp, 4)))
               == bnd::bindingHasAnyAction(bnd::getBinding(2, ButtonId::ZoomUp)));

        // A mode's own cross survives a later fill (the next appended mode).
        bnd::Binding own = bnd::getBinding(0, bnd::perEncModeUf8Id(ButtonId::ZoomLeft, 6));
        own.shortPress[P].action = "mod_shift";
        bnd::setBinding(0, bnd::perEncModeUf8Id(ButtonId::ZoomLeft, 6), own);
        bnd::save();
        setVersion(50);
        bnd::load();
        EXPECT(plainAction(0, bnd::perEncModeUf8Id(ButtonId::ZoomLeft, 6)) == "mod_shift");
        EXPECT(plainAction(0, bnd::perEncModeUf8Id(ButtonId::ZoomLeft, 5)) == "zoom_left");
        // And it round-trips through the file at the current version.
        bnd::save();
        bnd::load();
        EXPECT(plainAction(0, bnd::perEncModeUf8Id(ButtonId::ZoomLeft, 6)) == "mod_shift");
    }

    // ── v52: a v51 file has no Lanes slots. The UF1 cross gets its five, the
    // UF8 its Lanes cross; a Lanes slot of the user's own is left alone. ───────
    {
        for (int L = 0; L < 3; ++L)
            for (const ButtonId base : kBases)
                bnd::clearBinding(L, bnd::perEncModeUf8Id(base, kLanes));
        for (const ButtonId id : { ButtonId::Uf1NavUpLanes, ButtonId::Uf1NavDownLanes,
                                   ButtonId::Uf1NavLeftLanes, ButtonId::Uf1NavRightLanes,
                                   ButtonId::Uf1NavCentreLanes })
            bnd::clearBinding(0, id);
        bnd::Binding own = bnd::getBinding(1, ButtonId::ZoomUp);
        own.shortPress[P].action = "mod_shift";
        bnd::setBinding(1, bnd::perEncModeUf8Id(ButtonId::ZoomUp, kLanes), own);
        bnd::save();
        setVersion(51);
        bnd::load();
        EXPECT(plainAction(0, bnd::perEncModeUf8Id(ButtonId::ZoomUp, kLanes)) == "lane_prev");
        EXPECT(plainAction(1, bnd::perEncModeUf8Id(ButtonId::ZoomUp, kLanes)) == "mod_shift");
        EXPECT(plainAction(2, bnd::perEncModeUf8Id(ButtonId::ZoomLeft, kLanes)) == "lane_edge_prev");
        EXPECT(plainAction(0, ButtonId::Uf1NavCentreLanes) == "lane_comp_here");
        EXPECT(bnd::getBinding(0, ButtonId::Uf1NavUpLanes)
                   .shortPress[static_cast<int>(bnd::Modifier::Shift)].action == "lane_comp_area_up");
        const auto c = bnd::getBinding(0, ButtonId::Uf1NavDownLanes).color;
        EXPECT(c[0] == 0x00 && c[1] == 0xFF && c[2] == 0x66);
        // UF1 cross ids by mode: Lanes is the seventh jog mode.
        EXPECT(bnd::perModeNavId(ButtonId::Uf1NavUp, 6) == ButtonId::Uf1NavUpLanes);
        EXPECT(bnd::fromName(bnd::toName(ButtonId::Uf1NavDownLanes)) == ButtonId::Uf1NavDownLanes);
    }

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("uf8_enc_keys: all passed\n");
    return 0;
}
