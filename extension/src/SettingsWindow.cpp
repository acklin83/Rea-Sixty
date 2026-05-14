#include "SettingsWindow.h"
#include "SettingsScreen.h"
#include "ThemeBridge.h"

#include <cstdio>

// One translation unit must define REAIMGUIAPI_IMPLEMENT before including the
// header — this materialises the storage for the lazy-resolved ReaImGuiFunc
// instances. The MixerWindow TU (the other ReaImGui host) does NOT define
// this — only one TU per dylib may, otherwise the storage is multiply-
// defined at link time.
#define REAIMGUIAPI_IMPLEMENT
#include "reaper_imgui_functions.h"

namespace uf8 {

namespace {

// Left-rail navigation. Mixer is no longer here — it lives in its own window
// (uf8::MixerWindow). Order chosen to put the most-frequently-touched
// configuration screen (Device) first and the static info (About) last.
enum Section : int {
    kSecDevice = 0,
    kSecBindings,
    kSecFxLearn,
    kSecModes,
    kSecSelectionSets,
    kSecAbout,
    kSecCount,
};

struct RailEntry {
    const char* label;
    Section     section;
    bool        separatorBefore;
    void (*draw)(ImGui_Context*);
};

constexpr RailEntry kRail[] = {
    { "Device",         kSecDevice,        false, &SettingsScreen::drawDevice        },
    { "Bindings",       kSecBindings,      false, &SettingsScreen::drawBindings      },
    { "FX Learn",       kSecFxLearn,       false, &SettingsScreen::drawFxLearn       },
    { "Modes",          kSecModes,         false, &SettingsScreen::drawModes         },
    { "Selection Sets", kSecSelectionSets, false, &SettingsScreen::drawSelectionSets },
    { "About",          kSecAbout,         true,  &SettingsScreen::drawAbout         },
};

constexpr double kRailWidthPx = 160.0;

} // namespace

struct SettingsWindow::Impl {
    ImGui_Context* ctx = nullptr;
    bool           visible = false;
    int            selected = kSecDevice;
    int            sessionGen = 0;

    void ensureCtx()
    {
        if (ctx) return;
        // 1280×720 host context is sacred — see memory/reaimgui-host-size-
        // bisect.md. Inner window resizes freely via SetNextWindowSize.
        int sizeW = 1280;
        int sizeH = 720;
        ctx = ImGui_CreateContext(
            "Rea-Sixty Settings v1",
            &sizeW, &sizeH,
            /*pos_x*/ nullptr, /*pos_y*/ nullptr);
    }
};

SettingsWindow::SettingsWindow()  : impl_(new Impl) {}
SettingsWindow::~SettingsWindow() { delete impl_; }

void SettingsWindow::toggle()
{
    const bool wasOpen = impl_->visible;
    impl_->visible = !wasOpen;
    if (impl_->visible) {
        ++impl_->sessionGen;
        // Drop the old context pointer so ensureCtx() creates a brand
        // new ImGui_Context on the next onRunTick. ReaImGui v0.10 GCs
        // contexts that go unused for a defer cycle, so the orphaned
        // previous context cleans up on its own. A fresh ctx per open
        // guarantees zero state carry-over.
        impl_->ctx = nullptr;
    }
}

bool SettingsWindow::isOpen() const { return impl_->visible; }

void SettingsWindow::onRunTick()
{
    impl_->ensureCtx();
    if (!impl_->ctx) return;

    int condFirst = ImGui_Cond_FirstUseEver;
    ImGui_SetNextWindowSize(impl_->ctx, /*w*/ 1500, /*h*/ 1080,
                            &condFirst);
    ImGui_SetNextWindowPos(impl_->ctx, /*x*/ 60, /*y*/ 60,
                           &condFirst, /*pivot_x*/ nullptr,
                           /*pivot_y*/ nullptr);

    const int pushed = ThemeBridge::pushAll(impl_->ctx);

    int winFlags = ImGui_WindowFlags_NoSavedSettings
                 | ImGui_WindowFlags_NoCollapse;
    char winId[64];
    std::snprintf(winId, sizeof(winId),
                  "Rea-Sixty Settings##session_%d", impl_->sessionGen);
    bool open = impl_->visible;
    if (impl_->visible) {
        int condAlways = ImGui_Cond_Always;
        ImGui_SetNextWindowCollapsed(impl_->ctx, false, &condAlways);
        const bool bodyVisible =
            ImGui_Begin(impl_->ctx, winId, &open, &winFlags);
        if (bodyVisible) {
            // -- Left rail: section list ---------------------------------
            double railW = kRailWidthPx;
            const bool railVisible =
                ImGui_BeginChild(impl_->ctx, "rail", &railW,
                                 /*size_h*/ nullptr, /*border*/ nullptr,
                                 /*flags*/ nullptr);
            if (railVisible) {
                for (const RailEntry& e : kRail) {
                    if (e.separatorBefore) ImGui_Separator(impl_->ctx);
                    bool isSelected = (impl_->selected == e.section);
                    if (ImGui_Selectable(impl_->ctx, e.label, &isSelected,
                                         /*flags*/ nullptr,
                                         /*size_w*/ nullptr,
                                         /*size_h*/ nullptr)) {
                        impl_->selected = e.section;
                    }
                }
            }
            ImGui_EndChild(impl_->ctx);

            ImGui_SameLine(impl_->ctx, /*offset_from_start_x*/ nullptr,
                           /*spacing*/ nullptr);

            // -- Right content pane --------------------------------------
            const bool contentVisible =
                ImGui_BeginChild(impl_->ctx, "content", /*size_w*/ nullptr,
                                 /*size_h*/ nullptr, /*border*/ nullptr,
                                 /*flags*/ nullptr);
            if (contentVisible) {
                for (const RailEntry& e : kRail) {
                    if (e.section == impl_->selected) {
                        e.draw(impl_->ctx);
                        break;
                    }
                }
            }
            ImGui_EndChild(impl_->ctx);
        }
        ImGui_End(impl_->ctx);
        impl_->visible = open;
    }

    ThemeBridge::popAll(impl_->ctx, pushed);
}

} // namespace uf8
