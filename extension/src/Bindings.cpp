//
// Bindings — Phase A implementation. See Bindings.h for the architecture.
//
// JSON is loaded with WDL's wdl_json_parser (already vendored under
// extension/vendor/WDL). Writing is a small hand-written serializer
// since the schema is shallow.
//
// Config path: <REAPER resource path>/rea_sixty/bindings.json
//   macOS:   ~/Library/Application Support/REAPER/rea_sixty/bindings.json
//   Windows: %APPDATA%/REAPER/rea_sixty/bindings.json
//

#include "Bindings.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <vector>

#include "reaper_plugin_functions.h"

#include "WDL/jsonparse.h"

namespace uf8::bindings {

namespace {

// ---- ButtonId <-> snake_case name -----------------------------------------

struct NameEntry {
    ButtonId id;
    const char* name;
};

constexpr NameEntry kNames[] = {
    { ButtonId::BankLeft,    "bank_left"    },
    { ButtonId::BankRight,   "bank_right"   },
    { ButtonId::PageLeft,    "page_left"    },
    { ButtonId::PageRight,   "page_right"   },
    { ButtonId::Layer1,      "layer_1"      },
    { ButtonId::Layer2,      "layer_2"      },
    { ButtonId::Layer3,      "layer_3"      },
    { ButtonId::Quick1,      "quick_1"      },
    { ButtonId::Quick2,      "quick_2"      },
    { ButtonId::Quick3,      "quick_3"      },
    { ButtonId::PluginBtn,   "plugin_btn"   },
    { ButtonId::Flip,        "flip"         },
    { ButtonId::Pan,         "pan"          },
    { ButtonId::Fine,        "fine"         },
    { ButtonId::Btn360,      "btn_360"      },
    { ButtonId::AutoOff,     "auto_off"     },
    { ButtonId::AutoRead,    "auto_read"    },
    { ButtonId::AutoWrite,   "auto_write"   },
    { ButtonId::AutoTrim,    "auto_trim"    },
    { ButtonId::AutoLatch,   "auto_latch"   },
    { ButtonId::AutoTouch,   "auto_touch"   },
    { ButtonId::ZoomUp,      "zoom_up"      },
    { ButtonId::ZoomDown,    "zoom_down"    },
    { ButtonId::ZoomLeft,    "zoom_left"    },
    { ButtonId::ZoomRight,   "zoom_right"   },
    { ButtonId::ZoomCenter,  "zoom_center"  },
    { ButtonId::Nav,         "nav"          },
    { ButtonId::Nudge,       "nudge"        },
    { ButtonId::EncFocus,    "focus"        },
    { ButtonId::ChannelPush, "channel_push" },
    { ButtonId::SendPlugin1, "send_plugin_1" },
    { ButtonId::SendPlugin2, "send_plugin_2" },
    { ButtonId::SendPlugin3, "send_plugin_3" },
    { ButtonId::SendPlugin4, "send_plugin_4" },
    { ButtonId::SendPlugin5, "send_plugin_5" },
    { ButtonId::SendPlugin6, "send_plugin_6" },
    { ButtonId::SendPlugin7, "send_plugin_7" },
    { ButtonId::SendPlugin8, "send_plugin_8" },
    { ButtonId::Channel,     "channel"      },
    { ButtonId::TopSoftKey1, "top_soft_1"   },
    { ButtonId::TopSoftKey2, "top_soft_2"   },
    { ButtonId::TopSoftKey3, "top_soft_3"   },
    { ButtonId::TopSoftKey4, "top_soft_4"   },
    { ButtonId::TopSoftKey5, "top_soft_5"   },
    { ButtonId::TopSoftKey6, "top_soft_6"   },
    { ButtonId::TopSoftKey7, "top_soft_7"   },
    { ButtonId::TopSoftKey8, "top_soft_8"   },
    { ButtonId::VPotBank,      "vpot_bank"      },
    { ButtonId::SoftKey1Bank,  "softkey_bank_1" },
    { ButtonId::SoftKey2Bank,  "softkey_bank_2" },
    { ButtonId::SoftKey3Bank,  "softkey_bank_3" },
    { ButtonId::SoftKey4Bank,  "softkey_bank_4" },
    { ButtonId::SoftKey5Bank,  "softkey_bank_5" },
    { ButtonId::SelectionNorm, "selection_norm" },
    { ButtonId::SelectionRec,  "selection_rec"  },
    { ButtonId::SelectionAuto, "selection_auto" },
    { ButtonId::ChannelEncoder, "channel_encoder" },
    { ButtonId::Uc1Encoder2,      "uc1_encoder_2"      },
    { ButtonId::Uc1Encoder2Push,  "uc1_encoder_2_push" },
    { ButtonId::Uc1Magnifier,     "uc1_magnifier"      },
    { ButtonId::Uc1Btn360,        "uc1_btn_360"        },
};

} // namespace

const char* toName(ButtonId id)
{
    for (auto& e : kNames) if (e.id == id) return e.name;
    return "";
}

ButtonId fromName(const char* name)
{
    if (!name) return ButtonId::None;
    for (auto& e : kNames) if (std::strcmp(e.name, name) == 0) return e.id;
    return ButtonId::None;
}

ButtonId fromUf8DeviceId(uint8_t id)
{
    switch (id) {
        case 0x6F: return ButtonId::Fine;
        case 0x73: return ButtonId::Nav;
        case 0x74: return ButtonId::Nudge;
        case 0x75: return ButtonId::EncFocus;
        case 0x76: return ButtonId::ChannelPush;
        case 0x58: return ButtonId::AutoOff;
        case 0x59: return ButtonId::AutoRead;
        case 0x5A: return ButtonId::AutoWrite;
        case 0x5B: return ButtonId::AutoTrim;
        case 0x5C: return ButtonId::AutoLatch;
        case 0x5D: return ButtonId::AutoTouch;
        case 0x7A: return ButtonId::ZoomUp;
        case 0x7E: return ButtonId::ZoomDown;
        case 0x7B: return ButtonId::ZoomLeft;
        case 0x7D: return ButtonId::ZoomRight;
        case 0x7C: return ButtonId::ZoomCenter;
        case 0x54: return ButtonId::Flip;
        case 0x50: return ButtonId::PluginBtn;
        case 0x46: return ButtonId::Btn360;
        case 0x6E: return ButtonId::Pan;
        case 0x43: return ButtonId::Quick1;
        case 0x44: return ButtonId::Quick2;
        case 0x45: return ButtonId::Quick3;
        case 0x52: return ButtonId::PageLeft;
        case 0x53: return ButtonId::PageRight;
        case 0x78: return ButtonId::BankLeft;
        case 0x79: return ButtonId::BankRight;
        case 0x40: return ButtonId::Layer1;
        case 0x41: return ButtonId::Layer2;
        case 0x42: return ButtonId::Layer3;
        // Send/Plugin row 0x48..0x4F (docs/buttons-leds-quickref.md).
        case 0x48: return ButtonId::SendPlugin1;
        case 0x49: return ButtonId::SendPlugin2;
        case 0x4A: return ButtonId::SendPlugin3;
        case 0x4B: return ButtonId::SendPlugin4;
        case 0x4C: return ButtonId::SendPlugin5;
        case 0x4D: return ButtonId::SendPlugin6;
        case 0x4E: return ButtonId::SendPlugin7;
        case 0x4F: return ButtonId::SendPlugin8;
        case 0x51: return ButtonId::Channel;
        // Top-soft-keys 0x18..0x1F (one per strip, above the V-Pots).
        case 0x18: return ButtonId::TopSoftKey1;
        case 0x19: return ButtonId::TopSoftKey2;
        case 0x1A: return ButtonId::TopSoftKey3;
        case 0x1B: return ButtonId::TopSoftKey4;
        case 0x1C: return ButtonId::TopSoftKey5;
        case 0x1D: return ButtonId::TopSoftKey6;
        case 0x1E: return ButtonId::TopSoftKey7;
        case 0x1F: return ButtonId::TopSoftKey8;
        // SSL plug-in soft-key bank selectors 0x68..0x6D.
        case 0x68: return ButtonId::VPotBank;
        case 0x69: return ButtonId::SoftKey1Bank;
        case 0x6A: return ButtonId::SoftKey2Bank;
        case 0x6B: return ButtonId::SoftKey3Bank;
        case 0x6C: return ButtonId::SoftKey4Bank;
        case 0x6D: return ButtonId::SoftKey5Bank;
        // Selection-mode row 0x70/0x71/0x72 (Norm/CLEAR, Rec/ALL, Auto/ZERO).
        case 0x70: return ButtonId::SelectionNorm;
        case 0x71: return ButtonId::SelectionRec;
        case 0x72: return ButtonId::SelectionAuto;
        default:   return ButtonId::None;
    }
}

namespace {

// ---- Behavior / ActionType <-> string -------------------------------------

const char* behaviorName(Behavior b)
{
    switch (b) {
        case Behavior::Momentary: return "momentary";
        case Behavior::Toggle:    return "toggle";
        case Behavior::Hold:      return "hold";
    }
    return "momentary";
}

Behavior behaviorFromName(const char* s)
{
    if (!s) return Behavior::Momentary;
    if (std::strcmp(s, "toggle") == 0) return Behavior::Toggle;
    if (std::strcmp(s, "hold")   == 0) return Behavior::Hold;
    return Behavior::Momentary;
}

const char* actionTypeName(ActionType t)
{
    switch (t) {
        case ActionType::Noop:     return "noop";
        case ActionType::Reaper:   return "reaper";
        case ActionType::Keyboard: return "keyboard";
        case ActionType::Builtin:  return "builtin";
        case ActionType::Midi:     return "midi";
    }
    return "noop";
}

ActionType actionTypeFromName(const char* s)
{
    if (!s) return ActionType::Noop;
    if (std::strcmp(s, "reaper")   == 0) return ActionType::Reaper;
    if (std::strcmp(s, "keyboard") == 0) return ActionType::Keyboard;
    if (std::strcmp(s, "builtin")  == 0) return ActionType::Builtin;
    if (std::strcmp(s, "midi")     == 0) return ActionType::Midi;
    return ActionType::Noop;
}

const char* brightnessName(Brightness b)
{
    switch (b) {
        case Brightness::Off:    return "off";
        case Brightness::Dim:    return "dim";
        case Brightness::Bright: return "bright";
    }
    return "bright";
}

Brightness brightnessFromName(const char* s)
{
    if (!s) return Brightness::Bright;
    if (std::strcmp(s, "off") == 0) return Brightness::Off;
    if (std::strcmp(s, "dim") == 0) return Brightness::Dim;
    return Brightness::Bright;
}

// ---- Module state ---------------------------------------------------------

std::mutex                                 g_cfgMutex;
Config                                     g_cfg;
std::unordered_map<std::string, BuiltinDescriptor> g_builtins;

// Mixer auto-switch save slot. -1 means "no transient swap in effect".
// When the mixer opens and a Layer 2/3 has auto_when_mixer_visible=true,
// we stash the currently-active layer here and flip activeLayer to the
// flagged one. On mixer close (or a manual layer press in the meantime)
// we restore (or invalidate) this slot.
int g_savedLayer = -1;

// Long-press support — measured from press-edge to release-edge per
// (layer, button-id). dispatch() runs single-threaded on the libusb
// worker thread, so this map needs no locking. Threshold is 500 ms
// (matches generic "tap vs hold" UX expectations).
constexpr std::chrono::milliseconds kLongPressThreshold{500};

// Per-press record so the long-press path knows both WHEN the press
// started AND WHICH modifier was held at press time. Snapshot stays
// stable across the press window even if the user releases the
// modifier mid-hold — gives predictable Shift+button semantics.
struct PressRecord {
    std::chrono::steady_clock::time_point start;
    Modifier                              mod;
};
std::unordered_map<uint32_t, PressRecord> g_pressStart;

// Separate tracker for Toggle / Hold + long-press. The Momentary path
// reuses g_pressStart (deferred-primary semantics); Toggle and Hold need
// their own map because the standard path consumes g_pressStart for Hold
// before we can read the held duration. Keys are pressKey(layer, id).
std::unordered_map<uint32_t, PressRecord> g_longPressStart;

// Modifier state, set by main.cpp's mod_shift / mod_cmd / mod_ctrl
// builtin handlers. dispatch reads currentModifierSnapshot() at press
// edge; precedence at snapshot time is Ctrl > Cmd > Shift > Plain so
// the most specific bind wins when multiple modifiers are held.
std::atomic<bool> g_modShiftHeld{false};
std::atomic<bool> g_modCmdHeld  {false};
std::atomic<bool> g_modCtrlHeld {false};

// Monotonic counter bumped on every mutation of g_cfg (setBinding,
// clearBinding, layer setters, load, importFrom). main.cpp reads this
// in pushUf8GlobalLeds and invalidates its dedup cache on a delta so
// LED-colour edits in Settings → Bindings reach the hardware on the
// next tick instead of waiting for a press to dirty the state.
std::atomic<uint64_t> g_bindingsGen{0};

// Per-ButtonId modifier of the last action that ACTUALLY fired (slot
// type != Noop). Lets the LED pusher resolve the active-state colour
// from the slot whose action is engaged — Shift+press of a Toggle
// button now keeps the LED showing the Shift slot's active colour
// after release, instead of falling back to Plain. Sized to 256 to
// cover any future ButtonId additions without resizing.
constexpr size_t kLastFiredModSize = 256;
std::array<std::atomic<uint8_t>, kLastFiredModSize> g_lastFiredMod{};

uint32_t pressKey(int layer, ButtonId id)
{
    return (static_cast<uint32_t>(layer) << 16)
         | static_cast<uint32_t>(static_cast<uint16_t>(id));
}

// ---- Factory defaults -----------------------------------------------------

Binding mkBuiltin(const char* name, Behavior b, const char* label,
                  uint8_t r = 255, uint8_t g = 255, uint8_t b_ = 255,
                  int param = 0)
{
    Binding bd;
    bd.behavior = b;
    bd.label    = label;
    bd.color[0] = r; bd.color[1] = g; bd.color[2] = b_;
    bd.inactiveColor[0] = r;
    bd.inactiveColor[1] = g;
    bd.inactiveColor[2] = b_;
    auto& s = bd.shortPress[static_cast<int>(Modifier::Plain)];
    s.type   = ActionType::Builtin;
    s.action = name;
    s.param  = param;
    return bd;
}

void seedFactoryDefaults_(Config& c)
{
    c = Config{};
    c.version     = 2;
    c.activeLayer = 0;
    c.layers[0].name = "Layer 1";
    c.layers[1].name = "Layer 2";
    c.layers[2].name = "Layer 3";

    // Layer-select bindings live on ALL three layers so the user can
    // always navigate back even on the otherwise-empty Layer 2/3
    // scaffolds. Each press commits through setActiveLayer → persists.
    // Layer button LED state is driven by main.cpp's pushUf8GlobalLeds
    // based on getActiveLayer().
    for (int li = 0; li < 3; ++li) {
        auto& L = c.layers[li].bindings;
        L[ButtonId::Layer1] = mkBuiltin("layer_select_1", Behavior::Momentary, "LAYER 1");
        L[ButtonId::Layer2] = mkBuiltin("layer_select_2", Behavior::Momentary, "LAYER 2");
        L[ButtonId::Layer3] = mkBuiltin("layer_select_3", Behavior::Momentary, "LAYER 3");
    }

    auto& L1 = c.layers[0].bindings;

    // Fine / Shift modifier (hold).
    L1[ButtonId::Fine] = mkBuiltin("fine_modifier", Behavior::Hold, "FINE");

    // Encoder modes (momentary press = enter mode).
    L1[ButtonId::Nav]         = mkBuiltin("encoder_nav",   Behavior::Momentary, "NAV");
    L1[ButtonId::Nudge]       = mkBuiltin("encoder_nudge", Behavior::Momentary, "NUDGE");
    L1[ButtonId::EncFocus]    = mkBuiltin("encoder_focus", Behavior::Momentary, "FOCUS");
    L1[ButtonId::ChannelPush] = mkBuiltin("encoder_nav",   Behavior::Momentary, "");

    // Channel encoder rotation. Plain = mode-dispatch (preserves the
    // legacy Nav/Nudge/Focus/Instance mode system). Shift = direct
    // instance cycle (was hardcoded). Cmd / Ctrl = unbound, user picks
    // any builtin in Settings → Bindings → Channel Encoder.
    {
        auto& ce = L1[ButtonId::ChannelEncoder];
        ce.behavior = Behavior::Momentary;
        ce.label    = "Encoder";
        auto& spPlain = ce.shortPress[static_cast<int>(Modifier::Plain)];
        spPlain.type   = ActionType::Builtin;
        spPlain.action = "encoder_mode_dispatch";
        auto& spShift = ce.shortPress[static_cast<int>(Modifier::Shift)];
        spShift.type   = ActionType::Builtin;
        spShift.action = "instance_cycle";
    }

    // UC1 Encoder 2 rotation. Plain = BC track scroll (legacy default
    // SSL behaviour). Shift = instance cycle with the new instance
    // carousel. Cmd/Ctrl free.
    {
        auto& e2 = L1[ButtonId::Uc1Encoder2];
        e2.behavior = Behavior::Momentary;
        e2.label    = "UC1 Enc 2";
        auto& spPlain = e2.shortPress[static_cast<int>(Modifier::Plain)];
        spPlain.type   = ActionType::Builtin;
        spPlain.action = "bc_track_scroll";
        auto& spShift = e2.shortPress[static_cast<int>(Modifier::Shift)];
        spShift.type   = ActionType::Builtin;
        spShift.action = "instance_cycle";
    }

    // UC1 Encoder 2 push. Plain = toggle floating GUI of the focused
    // plug-in instance. Mode-specific behaviour (Presets confirm,
    // ExtFuncs toggle, Transport exit) is handled inside UC1Surface
    // before dispatch ever runs.
    L1[ButtonId::Uc1Encoder2Push] =
        mkBuiltin("show_focused_plugin_gui", Behavior::Momentary, "");

    // UC1 Magnifier (CCP 0x13). No factory action — user assigns it via
    // Settings → Bindings → UC1. Behavior::Momentary so a one-shot
    // builtin fires on press; user can switch to Toggle in the editor
    // and the LedOverride visualises the toggle state on the mockup.
    {
        auto& mg = L1[ButtonId::Uc1Magnifier];
        mg.behavior = Behavior::Momentary;
        mg.label    = "MAGNIFY";
    }

    // UC1 360 button — factory default mirrors UF8's Btn360
    // (`mixer_toggle`) so the physical button behaves as it did before
    // it became bindable. Independent slot from UF8's Btn360: rebinding
    // one does not affect the other.
    L1[ButtonId::Uc1Btn360] =
        mkBuiltin("mixer_toggle", Behavior::Momentary, "360");

    // Automation row — one builtin per mode. Factory colours all white;
    // the user sets each LED themselves via Settings → Bindings (Frank
    // 2026-05-07: explicitly does NOT want hardware-default colours
    // imposed). The hardware LED table in Protocol.cpp is now only a
    // fallback for the rare 2-arg buildUf8GlobalLed call paths that
    // bypass resolveLed_.
    L1[ButtonId::AutoOff]   = mkBuiltin("auto_off",   Behavior::Momentary, "OFF");
    L1[ButtonId::AutoRead]  = mkBuiltin("auto_read",  Behavior::Momentary, "READ");
    L1[ButtonId::AutoWrite] = mkBuiltin("auto_write", Behavior::Momentary, "WRITE");
    L1[ButtonId::AutoTrim]  = mkBuiltin("auto_trim",  Behavior::Momentary, "TRIM");
    L1[ButtonId::AutoLatch] = mkBuiltin("auto_latch", Behavior::Momentary, "LATCH");
    L1[ButtonId::AutoTouch] = mkBuiltin("auto_touch", Behavior::Momentary, "TOUCH");

    // Zoom pad — bundled builtins. Factory colours all white; user
    // chooses per LED.
    L1[ButtonId::ZoomUp]     = mkBuiltin("zoom_up",     Behavior::Momentary, "ZOOM UP");
    L1[ButtonId::ZoomDown]   = mkBuiltin("zoom_down",   Behavior::Momentary, "ZOOM DOWN");
    L1[ButtonId::ZoomLeft]   = mkBuiltin("zoom_left",   Behavior::Momentary, "ZOOM LEFT");
    L1[ButtonId::ZoomRight]  = mkBuiltin("zoom_right",  Behavior::Momentary, "ZOOM RIGHT");
    L1[ButtonId::ZoomCenter] = mkBuiltin("zoom_center", Behavior::Momentary, "FIT");

    // Mode toggles.
    L1[ButtonId::Flip]      = mkBuiltin("flip",                Behavior::Toggle,    "FLIP");
    L1[ButtonId::PluginBtn] = mkBuiltin("plugin_mode_toggle",  Behavior::Toggle,    "PLUGIN");
    L1[ButtonId::Btn360]    = mkBuiltin("mixer_toggle",        Behavior::Momentary, "360");
    L1[ButtonId::Pan]       = mkBuiltin("pan_force",           Behavior::Toggle,    "PAN");

    // Shift+Plugin: toggle the GUI-open preference. Pairs with the
    // plain Plugin toggle so the user has both "enter Plugin Mode" and
    // "should it open the plug-in window" on the same button.
    {
        auto& spShift = L1[ButtonId::PluginBtn]
            .shortPress[static_cast<int>(Modifier::Shift)];
        spShift.type   = ActionType::Builtin;
        spShift.action = "plugin_mode_open_gui";
        spShift.param  = 0;
        spShift.label  = "PLUG+UI";
    }

    // Flip long-press routes the focused track's sends/receives onto
    // V-Pots: long alone = sends (LED green when active), long+Shift =
    // receives (LED red). Behavior must be Momentary for long-press to
    // arm; the regular FLIP toggle still works on a quick press.
    {
        auto& fl = L1[ButtonId::Flip];
        fl.behavior     = Behavior::Momentary;
        fl.hasLongPress = true;
        auto& lpPlain = fl.longPress[static_cast<int>(Modifier::Plain)];
        lpPlain.type   = ActionType::Builtin;
        lpPlain.action = "send_this";
        lpPlain.param  = 1;   // Flip → V-Pots (this track's sends spread across V-Pots)
        auto& lpShift = fl.longPress[static_cast<int>(Modifier::Shift)];
        lpShift.type   = ActionType::Builtin;
        lpShift.action = "recv_this";
        lpShift.param  = 1;
    }

    // Send/Plugin row — each button switches to the matching send
    // index. Plain = Send N for all tracks, Shift+ = Receive N. Param
    // 0 routes onto Faders by default; the user can flip onto V-Pots
    // via the per-binding "Flip" checkbox.
    static const ButtonId kSendPluginIds[8] = {
        ButtonId::SendPlugin1, ButtonId::SendPlugin2,
        ButtonId::SendPlugin3, ButtonId::SendPlugin4,
        ButtonId::SendPlugin5, ButtonId::SendPlugin6,
        ButtonId::SendPlugin7, ButtonId::SendPlugin8,
    };
    for (int i = 0; i < 8; ++i) {
        char nameSend[20], nameRecv[20], label[8];
        std::snprintf(nameSend, sizeof(nameSend), "send_all_%d", i + 1);
        std::snprintf(nameRecv, sizeof(nameRecv), "recv_all_%d", i + 1);
        std::snprintf(label,    sizeof(label),    "S/P %d",    i + 1);
        Binding bd;
        bd.behavior = Behavior::Momentary;
        bd.label    = label;
        auto& sp = bd.shortPress[static_cast<int>(Modifier::Plain)];
        sp.type   = ActionType::Builtin;
        sp.action = nameSend;
        sp.param  = 0;   // default: Faders
        auto& shft = bd.shortPress[static_cast<int>(Modifier::Shift)];
        shft.type   = ActionType::Builtin;
        shft.action = nameRecv;
        shft.param  = 0;
        L1[kSendPluginIds[i]] = bd;
    }

    // CHANNEL — defaults to "home": one press clears every routing
    // toggle (send/recv on V-Pots and Faders) so the strips return to
    // their normal track-volume + pan view.
    L1[ButtonId::Channel] = mkBuiltin("home", Behavior::Momentary, "HOME");

    // Top-soft-keys (one per strip, above the V-Pots) — default to the
    // SSL Channel-Strip plug-in's softkey-focus behaviour. param =
    // strip 0..7. Press fires `ssl_softkey` which looks up the
    // current PAGE bank + focused-domain plugin map and calls
    // setFocus on the slot's linkIdx — same as SSL 360°.
    static const ButtonId kTopSoftKeyIds[8] = {
        ButtonId::TopSoftKey1, ButtonId::TopSoftKey2,
        ButtonId::TopSoftKey3, ButtonId::TopSoftKey4,
        ButtonId::TopSoftKey5, ButtonId::TopSoftKey6,
        ButtonId::TopSoftKey7, ButtonId::TopSoftKey8,
    };
    for (int i = 0; i < 8; ++i) {
        char label[12];
        std::snprintf(label, sizeof(label), "Soft-Key %d", i + 1);
        L1[kTopSoftKeyIds[i]] = mkBuiltin("ssl_softkey",
                                          Behavior::Momentary, label,
                                          255, 255, 255, /*param*/ i);
    }

    // Soft-Key Bank selectors (V-POT + Soft 1..5). All three layers
    // get the same factory binding — the handler picks SSL plug-in
    // bank or user-Quick sub-bank automatically based on whether a
    // Quick is engaged. Without entries on L2/L3 the buttons were
    // dead there (Frank's complaint, 2026-05-13).
    static const ButtonId kBankIds[6] = {
        ButtonId::VPotBank,
        ButtonId::SoftKey1Bank, ButtonId::SoftKey2Bank,
        ButtonId::SoftKey3Bank, ButtonId::SoftKey4Bank,
        ButtonId::SoftKey5Bank,
    };
    static const char* kBankLabels[6] = {
        "V-POT", "BANK 1", "BANK 2", "BANK 3", "BANK 4", "BANK 5",
    };
    for (int li = 0; li < 3; ++li) {
        auto& L = c.layers[li].bindings;
        for (int i = 0; i < 6; ++i) {
            L[kBankIds[i]] = mkBuiltin("softkey_bank_select",
                                       Behavior::Momentary, kBankLabels[i],
                                       255, 255, 255, /*param*/ i);
        }
    }

    // Quick keys on Layer 1: Q1/Q2 stay hardcoded SSL CS/BC focus —
    // exact pre-2026-05-13 behaviour (Momentary, no user-Quick engage).
    // Q3 is the only user-fillable Quick on Layer 1; defaults to
    // softkey_bank_3 (= L1 Q3 direct jump, always-engage).
    L1[ButtonId::Quick1] = mkBuiltin("domain_cs",      Behavior::Momentary, "CS");
    L1[ButtonId::Quick2] = mkBuiltin("domain_bc",      Behavior::Momentary, "BC");
    L1[ButtonId::Quick3] = mkBuiltin("softkey_bank_3", Behavior::Momentary, "Q3");

    // Bank scroll (8-strip) and soft-key bank navigation (page).
    L1[ButtonId::BankLeft]  = mkBuiltin("bank_left",  Behavior::Momentary, "BANK <");
    L1[ButtonId::BankRight] = mkBuiltin("bank_right", Behavior::Momentary, "BANK >");
    L1[ButtonId::PageLeft]  = mkBuiltin("page_left",  Behavior::Momentary, "PAGE <");
    L1[ButtonId::PageRight] = mkBuiltin("page_right", Behavior::Momentary, "PAGE >");

    // Layer 2 + 3 — Quick buttons engage the matching (Layer, Quick)
    // user position via the softkey_bank_N direct-jump builtin.
    // Without these factory entries the LED resolver returns Off
    // ("no binding → dark", Frank 2026-05-07) and pressing Q1/Q2/Q3
    // has no effect.
    //   L2 Q1=softkey_bank_4, Q2=5, Q3=6
    //   L3 Q1=softkey_bank_7, Q2=8, Q3=9
    for (int li = 1; li <= 2; ++li) {
        auto& L = c.layers[li].bindings;
        char nA[24], nB[24], nC[24];
        std::snprintf(nA, sizeof(nA), "softkey_bank_%d", li * 3 + 1);
        std::snprintf(nB, sizeof(nB), "softkey_bank_%d", li * 3 + 2);
        std::snprintf(nC, sizeof(nC), "softkey_bank_%d", li * 3 + 3);
        L[ButtonId::Quick1] = mkBuiltin(nA, Behavior::Momentary, "Q1");
        L[ButtonId::Quick2] = mkBuiltin(nB, Behavior::Momentary, "Q2");
        L[ButtonId::Quick3] = mkBuiltin(nC, Behavior::Momentary, "Q3");
    }
}

// ---- JSON serialization ---------------------------------------------------

void appendEscaped(std::ostringstream& os, const std::string& s)
{
    os << '"';
    for (char c : s) {
        switch (c) {
            case '"':  os << "\\\""; break;
            case '\\': os << "\\\\"; break;
            case '\n': os << "\\n";  break;
            case '\r': os << "\\r";  break;
            case '\t': os << "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    os << buf;
                } else {
                    os << c;
                }
                break;
        }
    }
    os << '"';
}

// Write one layer's body — name, flags, vpot mode, bindings map. The
// `pad` strings are the indentation prefixes for the wrapping object's
// fields and the bindings map keys, so the same helper formats both
// the full-Config layer-array entry (4 / 8 spaces) and the standalone
// per-layer file (2 / 4 spaces).

const char* modifierKeyName_(int m)
{
    switch (m) {
        case static_cast<int>(Modifier::Plain): return "plain";
        case static_cast<int>(Modifier::Shift): return "shift";
        case static_cast<int>(Modifier::Cmd):   return "cmd";
        case static_cast<int>(Modifier::Ctrl):  return "ctrl";
    }
    return "plain";
}

int modifierFromKey_(const char* s)
{
    if (!s) return -1;
    if (std::strcmp(s, "plain") == 0) return static_cast<int>(Modifier::Plain);
    if (std::strcmp(s, "shift") == 0) return static_cast<int>(Modifier::Shift);
    if (std::strcmp(s, "cmd")   == 0) return static_cast<int>(Modifier::Cmd);
    if (std::strcmp(s, "ctrl")  == 0) return static_cast<int>(Modifier::Ctrl);
    return -1;
}

bool slotIsEmpty_(const ActionSlot& s)
{
    if (s.type != ActionType::Noop || !s.action.empty()) return false;
    for (const auto& st : s.extraSteps) {
        if (st.type != ActionType::Noop || !st.action.empty()) return false;
    }
    return true;
}

// Emit a single step's flat fields inline (no surrounding braces) —
// caller owns the wrapping object. Used by both the legacy single-step
// slot writer and the new step-array writer.
void serializeStepFields_(const ActionStep& s, std::ostringstream& os)
{
    os << "\"type\": ";   appendEscaped(os, actionTypeName(s.type));
    os << ", \"action\": "; appendEscaped(os, s.action);
    os << ", \"param\": " << s.param;
    if (!s.label.empty()) {
        os << ", \"label\": ";
        appendEscaped(os, s.label);
    }
    if (s.type == ActionType::Midi) {
        os << ", \"midi\": {";
        os << "\"device\": ";   appendEscaped(os, s.midiDevice);
        os << ", \"channel\": " << s.midiChannel;
        os << ", \"msg\": "     << s.midiMsgType;
        os << ", \"d1\": "      << s.midiData1;
        os << ", \"d2\": "      << s.midiData2;
        os << "}";
    }
    if (s.wait_ms > 0) {
        os << ", \"wait_ms\": " << s.wait_ms;
    }
    if (s.fireOnInactive) {
        os << ", \"fire_on_inactive\": true";
    }
}

bool slotHasLedOverride_(const ActionSlot& s)
{
    return s.led.hasActive || s.led.hasInactive;
}

void serializeLedOverride_(const LedOverride& lo, std::ostringstream& os)
{
    os << "{";
    bool first = true;
    if (lo.hasActive) {
        os << "\"color\": ["
           << int(lo.color[0]) << ", "
           << int(lo.color[1]) << ", "
           << int(lo.color[2]) << "]";
        os << ", \"brightness\": ";
        appendEscaped(os, brightnessName(lo.brightness));
        first = false;
    }
    if (lo.hasInactive) {
        if (!first) os << ", ";
        os << "\"inactive_color\": ["
           << int(lo.inactiveColor[0]) << ", "
           << int(lo.inactiveColor[1]) << ", "
           << int(lo.inactiveColor[2]) << "]";
        os << ", \"inactive_brightness\": ";
        appendEscaped(os, brightnessName(lo.inactiveBrightness));
    }
    os << "}";
}

// Emit a slot's body. Single-step slot with no LED override and no
// wait_ms collapses to the legacy flat shape (type/action/param/label/
// midi at the slot level). Anything richer emits {steps:[...], led:{}}.
void serializeSlotFields_(const ActionSlot& s, std::ostringstream& os)
{
    const bool useNew = !s.extraSteps.empty()
                     || s.wait_ms > 0
                     || slotHasLedOverride_(s);
    if (!useNew) {
        serializeStepFields_(static_cast<const ActionStep&>(s), os);
        return;
    }
    os << "\"steps\": [";
    const int n = stepCount(s);
    for (int i = 0; i < n; ++i) {
        if (i) os << ", ";
        os << "{";
        serializeStepFields_(stepAt(s, i), os);
        os << "}";
    }
    os << "]";
    if (slotHasLedOverride_(s)) {
        os << ", \"led\": ";
        serializeLedOverride_(s.led, os);
    }
}

// Emit the {plain, shift, cmd, ctrl} matrix object for one row of the
// short/long matrix. Slots with no action set are omitted to keep the
// JSON compact for the common case (most bindings only use plain).
void serializeMatrixRow_(const ActionSlot (&row)[kModifierCount],
                         std::ostringstream& os)
{
    os << "{";
    bool first = true;
    for (int m = 0; m < kModifierCount; ++m) {
        if (slotIsEmpty_(row[m])) continue;
        if (!first) os << ", ";
        first = false;
        os << "\"" << modifierKeyName_(m) << "\": {";
        serializeSlotFields_(row[m], os);
        os << "}";
    }
    os << "}";
}

// Emit a Binding's body inline (no surrounding braces, no leading
// "name": prefix) — caller owns the wrapping object. Used by both the
// per-layer binding map and the user-bank slot serializers so the
// schema stays in one place.
void serializeBindingBody_(const Binding& bd, std::ostringstream& os)
{
    os << "\"behavior\": "; appendEscaped(os, behaviorName(bd.behavior));
    os << ", \"label\": ";  appendEscaped(os, bd.label);
    os << ", \"color\": ["
       << int(bd.color[0]) << ", "
       << int(bd.color[1]) << ", "
       << int(bd.color[2]) << "]";
    os << ", \"brightness\": ";
    appendEscaped(os, brightnessName(bd.brightness));
    os << ", \"inactive_color\": ["
       << int(bd.inactiveColor[0]) << ", "
       << int(bd.inactiveColor[1]) << ", "
       << int(bd.inactiveColor[2]) << "]";
    os << ", \"inactive_brightness\": ";
    appendEscaped(os, brightnessName(bd.inactiveBrightness));
    if (bd.ledShowWhenEmpty) {
        os << ", \"led_show_when_empty\": true";
    }
    os << ", \"short\": ";
    serializeMatrixRow_(bd.shortPress, os);
    if (bd.hasLongPress) {
        os << ", \"long\": ";
        serializeMatrixRow_(bd.longPress, os);
    }
}

void serializeLayerBody_(const Layer& L, std::ostringstream& os,
                         const char* fieldPad, const char* bindingPad)
{
    os << fieldPad << "\"name\": ";
    appendEscaped(os, L.name);
    os << ",\n";
    os << fieldPad << "\"auto_when_mixer_visible\": "
       << (L.autoWhenMixerVisible ? "true" : "false") << ",\n";
    os << fieldPad << "\"vpot_default_mode\": ";
    appendEscaped(os, L.vpotDefaultMode);
    os << ",\n";
    os << fieldPad << "\"bindings\": {";
    bool first = true;
    for (auto& kv : L.bindings) {
        const char* name = toName(kv.first);
        if (!name || !*name) continue;
        if (!first) os << ",";
        first = false;
        os << "\n" << bindingPad << "\"" << name << "\": {";
        serializeBindingBody_(kv.second, os);
        os << "}";
    }
    if (!first) os << "\n" << fieldPad;
    os << "}\n";
}

// Emits all populated (layer, quick, subBank, slot) entries as a flat
// list. Empty slots are skipped so a default-constructed Config writes
// nothing here (no 432-line empty matrix in the JSON).
void serializeUserQuicks_(const Config& c, std::ostringstream& os)
{
    auto slotIsEmptyAll_ = [](const Binding& bd) {
        for (int m = 0; m < kModifierCount; ++m) {
            if (!slotIsEmpty_(bd.shortPress[m])) return false;
            if (!slotIsEmpty_(bd.longPress[m]))  return false;
        }
        return bd.label.empty();
    };

    bool anyData = false;
    for (int li = 0; li < 3 && !anyData; ++li) {
        for (int qi = 0; qi < kQuicksPerLayer && !anyData; ++qi) {
            for (int bi = 0; bi < kSubBanksPerQuick && !anyData; ++bi) {
                for (int si = 0; si < kSlotsPerSubBank && !anyData; ++si) {
                    if (!slotIsEmptyAll_(
                            c.userQuicks[li].quicks[qi].subBanks[bi].slots[si])) {
                        anyData = true;
                    }
                }
            }
        }
    }
    if (!anyData) return;

    os << ",\n  \"user_quicks\": [";
    bool first = true;
    for (int li = 0; li < 3; ++li) {
        for (int qi = 0; qi < kQuicksPerLayer; ++qi) {
            for (int bi = 0; bi < kSubBanksPerQuick; ++bi) {
                for (int si = 0; si < kSlotsPerSubBank; ++si) {
                    const auto& bd = c.userQuicks[li].quicks[qi]
                                        .subBanks[bi].slots[si];
                    if (slotIsEmptyAll_(bd)) continue;
                    if (!first) os << ",";
                    first = false;
                    os << "\n    {\"layer\": " << li
                       << ", \"quick\": " << qi
                       << ", \"sub_bank\": " << bi
                       << ", \"slot\": " << si
                       << ", \"binding\": {";
                    serializeBindingBody_(bd, os);
                    os << "}}";
                }
            }
        }
    }
    os << "\n  ]";
}

// Per-(Layer, Quick) Sub-Bank LED overrides. Emitted as a flat list of
// entries — one per (L, Q, SB) with non-default appearance. Default
// is white/bright/dim, so an unmodified config writes nothing here.
void serializeSubBankLeds_(const Config& c, std::ostringstream& os)
{
    auto isDefault_ = [](const SubBankLed& a) {
        return a.color[0] == 255 && a.color[1] == 255 && a.color[2] == 255
            && a.brightness == Brightness::Bright
            && a.inactiveColor[0] == 255 && a.inactiveColor[1] == 255
            && a.inactiveColor[2] == 255
            && a.inactiveBrightness == Brightness::Dim;
    };

    bool anyData = false;
    for (int li = 0; li < 3 && !anyData; ++li)
        for (int qi = 0; qi < kQuicksPerLayer && !anyData; ++qi)
            for (int bi = 0; bi < kSubBanksPerQuick && !anyData; ++bi)
                if (!isDefault_(c.userQuicks[li].quicks[qi].subBankLeds[bi]))
                    anyData = true;
    if (!anyData) return;

    os << ",\n  \"sub_bank_leds\": [";
    bool first = true;
    for (int li = 0; li < 3; ++li) {
        for (int qi = 0; qi < kQuicksPerLayer; ++qi) {
            for (int bi = 0; bi < kSubBanksPerQuick; ++bi) {
                const auto& a = c.userQuicks[li].quicks[qi].subBankLeds[bi];
                if (isDefault_(a)) continue;
                if (!first) os << ",";
                first = false;
                os << "\n    {\"layer\": " << li
                   << ", \"quick\": " << qi
                   << ", \"sub_bank\": " << bi
                   << ", \"color\": [" << int(a.color[0]) << ", "
                                       << int(a.color[1]) << ", "
                                       << int(a.color[2]) << "]"
                   << ", \"brightness\": \"" << brightnessName(a.brightness) << "\""
                   << ", \"inactive_color\": [" << int(a.inactiveColor[0]) << ", "
                                                 << int(a.inactiveColor[1]) << ", "
                                                 << int(a.inactiveColor[2]) << "]"
                   << ", \"inactive_brightness\": \""
                   << brightnessName(a.inactiveBrightness) << "\""
                   << "}";
            }
        }
    }
    os << "\n  ]";
}

// Named Sub-Bank snapshots — flat list, each entry holds the preset's
// name + an array of 8 Binding bodies. Empty list writes nothing.
void serializeBankPresets_(const Config& c, std::ostringstream& os)
{
    if (c.bankPresets.empty()) return;
    os << ",\n  \"bank_presets\": [";
    bool first = true;
    for (const auto& p : c.bankPresets) {
        if (!first) os << ",";
        first = false;
        os << "\n    {\"name\": ";
        appendEscaped(os, p.name);
        os << ", \"slots\": [";
        for (int s = 0; s < kSlotsPerSubBank; ++s) {
            if (s) os << ",";
            os << "\n      {";
            serializeBindingBody_(p.slots[s], os);
            os << "}";
        }
        os << "\n    ]}";
    }
    os << "\n  ]";
}

std::string serialize(const Config& c)
{
    std::ostringstream os;
    os << "{\n";
    os << "  \"version\": " << c.version << ",\n";
    os << "  \"active_layer\": " << c.activeLayer << ",\n";
    os << "  \"layers\": [\n";
    for (int i = 0; i < 3; ++i) {
        os << "    {\n";
        serializeLayerBody_(c.layers[i], os, "      ", "        ");
        os << "    }" << (i < 2 ? "," : "") << "\n";
    }
    os << "  ]";
    serializeUserQuicks_(c, os);
    serializeSubBankLeds_(c, os);
    serializeBankPresets_(c, os);
    os << "\n}\n";
    return os.str();
}

// Standalone per-layer envelope. The "type" / "index" fields let
// importLayerFrom verify the file before applying it (and let users
// recognise the file at a glance).
std::string serializeOneLayer_(const Layer& L, int idx)
{
    std::ostringstream os;
    os << "{\n";
    os << "  \"version\": 1,\n";
    os << "  \"type\": \"layer\",\n";
    os << "  \"index\": " << idx << ",\n";
    os << "  \"layer\": {\n";
    serializeLayerBody_(L, os, "    ", "      ");
    os << "  }\n";
    os << "}\n";
    return os.str();
}

// Read a single ActionStep's fields from a JSON object.
bool parseStepFields_(wdl_json_element* obj, ActionStep& out)
{
    if (!obj || !obj->is_object()) return false;
    if (auto* v = obj->get_item_by_name("type"))
        out.type = actionTypeFromName(v->get_string_value());
    if (auto* v = obj->get_item_by_name("action"))
        if (auto* s = v->get_string_value()) out.action = s;
    if (auto* v = obj->get_item_by_name("param"))
        if (auto* s = v->get_string_value(true)) out.param = std::atoi(s);
    if (auto* v = obj->get_item_by_name("label"))
        if (auto* s = v->get_string_value()) out.label = s;
    if (auto* v = obj->get_item_by_name("wait_ms"))
        if (auto* s = v->get_string_value(true)) out.wait_ms = std::atoi(s);
    if (auto* v = obj->get_item_by_name("fire_on_inactive"))
        if (auto* s = v->get_string_value(true))
            out.fireOnInactive = (std::atoi(s) != 0);
    if (auto* mi = obj->get_item_by_name("midi"); mi && mi->is_object()) {
        if (auto* v = mi->get_item_by_name("device"))
            if (auto* s = v->get_string_value()) out.midiDevice = s;
        if (auto* v = mi->get_item_by_name("channel"))
            if (auto* s = v->get_string_value(true)) out.midiChannel = std::atoi(s);
        if (auto* v = mi->get_item_by_name("msg"))
            if (auto* s = v->get_string_value(true)) out.midiMsgType = std::atoi(s);
        if (auto* v = mi->get_item_by_name("d1"))
            if (auto* s = v->get_string_value(true)) out.midiData1 = std::atoi(s);
        if (auto* v = mi->get_item_by_name("d2"))
            if (auto* s = v->get_string_value(true)) out.midiData2 = std::atoi(s);
    }
    return true;
}

void parseLedOverride_(wdl_json_element* obj, LedOverride& out)
{
    if (!obj || !obj->is_object()) return;
    if (auto* v = obj->get_item_by_name("color"); v && v->is_array()) {
        for (int k = 0; k < 3 && k < v->m_array->GetSize(); ++k) {
            if (auto* s = v->enum_item(k)->get_string_value(true)) {
                int x = std::atoi(s);
                if (x < 0) x = 0; else if (x > 255) x = 255;
                out.color[k] = static_cast<uint8_t>(x);
            }
        }
        out.hasActive = true;
    }
    if (auto* v = obj->get_item_by_name("brightness")) {
        out.brightness = brightnessFromName(v->get_string_value());
        out.hasActive = true;
    }
    if (auto* v = obj->get_item_by_name("inactive_color"); v && v->is_array()) {
        for (int k = 0; k < 3 && k < v->m_array->GetSize(); ++k) {
            if (auto* s = v->enum_item(k)->get_string_value(true)) {
                int x = std::atoi(s);
                if (x < 0) x = 0; else if (x > 255) x = 255;
                out.inactiveColor[k] = static_cast<uint8_t>(x);
            }
        }
        out.hasInactive = true;
    }
    if (auto* v = obj->get_item_by_name("inactive_brightness")) {
        out.inactiveBrightness = brightnessFromName(v->get_string_value());
        out.hasInactive = true;
    }
}

// Read a single ActionSlot's fields. Accepts both legacy single-action
// shape (type/action/param/label/midi at the slot level) and the new
// {steps:[...], led:{}} shape. Missing keys leave defaults intact.
bool parseSlotFields_(wdl_json_element* obj, ActionSlot& out)
{
    if (!obj || !obj->is_object()) return false;
    if (auto* steps = obj->get_item_by_name("steps");
        steps && steps->is_array() && steps->m_array) {
        const int n = steps->m_array->GetSize();
        for (int i = 0; i < n; ++i) {
            if (i == 0) {
                parseStepFields_(steps->enum_item(0),
                                 static_cast<ActionStep&>(out));
            } else {
                ActionStep st;
                parseStepFields_(steps->enum_item(i), st);
                out.extraSteps.push_back(std::move(st));
            }
        }
    } else {
        parseStepFields_(obj, static_cast<ActionStep&>(out));
    }
    if (auto* led = obj->get_item_by_name("led"); led && led->is_object()) {
        parseLedOverride_(led, out.led);
    }
    return true;
}

// Read a {plain, shift, cmd, ctrl} matrix-row object into the 4-element
// slot array. Missing modifier keys leave their slot at default (Noop).
void parseMatrixRow_(wdl_json_element* obj, ActionSlot (&row)[kModifierCount])
{
    if (!obj || !obj->is_object()) return;
    const int n = obj->m_array ? obj->m_array->GetSize() : 0;
    for (int i = 0; i < n; ++i) {
        const char* key = obj->enum_item_name(i);
        wdl_json_element* it = obj->enum_item(i);
        const int m = modifierFromKey_(key);
        if (m < 0) continue;
        parseSlotFields_(it, row[m]);
    }
}

// Parse a Binding from its JSON object (new-schema only — no
// type/action/param/midi/long_press fallback). Used by parseUserQuicks_.
// parseLayer_ has its own inline logic that also covers the old
// pre-matrix schema, so it doesn't go through this helper.
void parseBindingBody_(wdl_json_element* be, Binding& bd)
{
    if (!be || !be->is_object()) return;
    if (auto* v = be->get_item_by_name("behavior"))
        bd.behavior = behaviorFromName(v->get_string_value());
    if (auto* v = be->get_item_by_name("label"))
        if (auto* s = v->get_string_value()) bd.label = s;
    if (auto* v = be->get_item_by_name("color"); v && v->is_array()) {
        for (int k = 0; k < 3 && k < v->m_array->GetSize(); ++k) {
            if (auto* s = v->enum_item(k)->get_string_value(true)) {
                int x = std::atoi(s);
                if (x < 0) x = 0; else if (x > 255) x = 255;
                bd.color[k] = static_cast<uint8_t>(x);
            }
        }
    }
    if (auto* v = be->get_item_by_name("brightness"))
        bd.brightness = brightnessFromName(v->get_string_value());
    if (auto* v = be->get_item_by_name("inactive_color"); v && v->is_array()) {
        for (int k = 0; k < 3 && k < v->m_array->GetSize(); ++k) {
            if (auto* s = v->enum_item(k)->get_string_value(true)) {
                int x = std::atoi(s);
                if (x < 0) x = 0; else if (x > 255) x = 255;
                bd.inactiveColor[k] = static_cast<uint8_t>(x);
            }
        }
    } else {
        bd.inactiveColor[0] = bd.color[0];
        bd.inactiveColor[1] = bd.color[1];
        bd.inactiveColor[2] = bd.color[2];
    }
    if (auto* v = be->get_item_by_name("inactive_brightness"))
        bd.inactiveBrightness = brightnessFromName(v->get_string_value());
    if (auto* v = be->get_item_by_name("led_show_when_empty"))
        if (auto* s = v->get_string_value(true))
            bd.ledShowWhenEmpty = (std::strcmp(s, "true") == 0
                                || std::strcmp(s, "1") == 0);
    if (auto* v = be->get_item_by_name("short"))
        parseMatrixRow_(v, bd.shortPress);
    if (auto* v = be->get_item_by_name("long"); v && v->is_object()) {
        bd.hasLongPress = true;
        parseMatrixRow_(v, bd.longPress);
    }
}

void parseUserQuicks_(wdl_json_element* root, Config& out)
{
    auto* arr = root->get_item_by_name("user_quicks");
    if (!arr || !arr->is_array() || !arr->m_array) return;
    const int n = arr->m_array->GetSize();
    for (int i = 0; i < n; ++i) {
        wdl_json_element* eo = arr->enum_item(i);
        if (!eo || !eo->is_object()) continue;
        int layer = -1, quick = -1, subBank = -1, slot = -1;
        if (auto* v = eo->get_item_by_name("layer"))
            if (auto* s = v->get_string_value(true)) layer = std::atoi(s);
        if (auto* v = eo->get_item_by_name("quick"))
            if (auto* s = v->get_string_value(true)) quick = std::atoi(s);
        if (auto* v = eo->get_item_by_name("sub_bank"))
            if (auto* s = v->get_string_value(true)) subBank = std::atoi(s);
        if (auto* v = eo->get_item_by_name("slot"))
            if (auto* s = v->get_string_value(true)) slot = std::atoi(s);
        if (layer   < 0 || layer   >= 3)                 continue;
        if (quick   < 0 || quick   >= kQuicksPerLayer)   continue;
        if (subBank < 0 || subBank >= kSubBanksPerQuick) continue;
        if (slot    < 0 || slot    >= kSlotsPerSubBank)  continue;
        auto* bodyObj = eo->get_item_by_name("binding");
        if (!bodyObj || !bodyObj->is_object()) continue;
        Binding& bd = out.userQuicks[layer].quicks[quick]
                          .subBanks[subBank].slots[slot];
        bd = Binding{};
        parseBindingBody_(bodyObj, bd);
    }
}

void parseSubBankLeds_(wdl_json_element* root, Config& out)
{
    auto* arr = root->get_item_by_name("sub_bank_leds");
    if (!arr || !arr->is_array() || !arr->m_array) return;
    const int n = arr->m_array->GetSize();
    for (int i = 0; i < n; ++i) {
        wdl_json_element* eo = arr->enum_item(i);
        if (!eo || !eo->is_object()) continue;
        int layer = -1, quick = -1, subBank = -1;
        if (auto* v = eo->get_item_by_name("layer"))
            if (auto* s = v->get_string_value(true)) layer = std::atoi(s);
        if (auto* v = eo->get_item_by_name("quick"))
            if (auto* s = v->get_string_value(true)) quick = std::atoi(s);
        if (auto* v = eo->get_item_by_name("sub_bank"))
            if (auto* s = v->get_string_value(true)) subBank = std::atoi(s);
        if (layer   < 0 || layer   >= 3)                 continue;
        if (quick   < 0 || quick   >= kQuicksPerLayer)   continue;
        if (subBank < 0 || subBank >= kSubBanksPerQuick) continue;
        SubBankLed& a = out.userQuicks[layer].quicks[quick]
                            .subBankLeds[subBank];
        a = SubBankLed{};
        if (auto* v = eo->get_item_by_name("color"); v && v->is_array()) {
            for (int k = 0; k < 3 && k < v->m_array->GetSize(); ++k) {
                if (auto* s = v->enum_item(k)->get_string_value(true)) {
                    int x = std::atoi(s);
                    if (x < 0) x = 0; else if (x > 255) x = 255;
                    a.color[k] = static_cast<uint8_t>(x);
                }
            }
        }
        if (auto* v = eo->get_item_by_name("brightness"))
            a.brightness = brightnessFromName(v->get_string_value());
        if (auto* v = eo->get_item_by_name("inactive_color");
            v && v->is_array()) {
            for (int k = 0; k < 3 && k < v->m_array->GetSize(); ++k) {
                if (auto* s = v->enum_item(k)->get_string_value(true)) {
                    int x = std::atoi(s);
                    if (x < 0) x = 0; else if (x > 255) x = 255;
                    a.inactiveColor[k] = static_cast<uint8_t>(x);
                }
            }
        }
        if (auto* v = eo->get_item_by_name("inactive_brightness"))
            a.inactiveBrightness = brightnessFromName(v->get_string_value());
    }
}

void parseBankPresets_(wdl_json_element* root, Config& out)
{
    auto* arr = root->get_item_by_name("bank_presets");
    if (!arr || !arr->is_array() || !arr->m_array) return;
    const int n = arr->m_array->GetSize();
    for (int i = 0; i < n; ++i) {
        wdl_json_element* eo = arr->enum_item(i);
        if (!eo || !eo->is_object()) continue;
        SoftKeyBankPreset p;
        if (auto* v = eo->get_item_by_name("name"))
            if (auto* s = v->get_string_value()) p.name = s;
        if (p.name.empty()) continue;     // skip nameless garbage
        auto* slots = eo->get_item_by_name("slots");
        if (slots && slots->is_array() && slots->m_array) {
            const int sn = slots->m_array->GetSize();
            for (int s = 0; s < sn && s < kSlotsPerSubBank; ++s) {
                wdl_json_element* sl = slots->enum_item(s);
                if (sl && sl->is_object()) {
                    parseBindingBody_(sl, p.slots[s]);
                }
            }
        }
        out.bankPresets.push_back(std::move(p));
    }
}

bool parseLayer_(wdl_json_element* lobj, Layer& out)
{
    if (!lobj || !lobj->is_object()) return false;
    if (auto* v = lobj->get_item_by_name("name"))
        if (auto* s = v->get_string_value()) out.name = s;
    if (auto* v = lobj->get_item_by_name("auto_when_mixer_visible"))
        if (auto* s = v->get_string_value(true))
            out.autoWhenMixerVisible = (std::strcmp(s, "true") == 0 || std::strcmp(s, "1") == 0);
    if (auto* v = lobj->get_item_by_name("vpot_default_mode"))
        if (auto* s = v->get_string_value()) out.vpotDefaultMode = s;
    auto* bobj = lobj->get_item_by_name("bindings");
    if (!bobj || !bobj->is_object()) return true;
    const int n = bobj->m_array ? bobj->m_array->GetSize() : 0;
    for (int i = 0; i < n; ++i) {
        const char* key = bobj->enum_item_name(i);
        wdl_json_element* be = bobj->enum_item(i);
        if (!key || !be || !be->is_object()) continue;
        ButtonId bid = fromName(key);
        if (bid == ButtonId::None) continue;  // forward-compat: skip unknown keys
        Binding bd;

        if (auto* v = be->get_item_by_name("behavior"))
            bd.behavior = behaviorFromName(v->get_string_value());
        if (auto* v = be->get_item_by_name("label"))
            if (auto* s = v->get_string_value()) bd.label = s;
        if (auto* v = be->get_item_by_name("color"); v && v->is_array()) {
            for (int k = 0; k < 3 && k < v->m_array->GetSize(); ++k) {
                if (auto* s = v->enum_item(k)->get_string_value(true)) {
                    int x = std::atoi(s);
                    if (x < 0) x = 0; else if (x > 255) x = 255;
                    bd.color[k] = static_cast<uint8_t>(x);
                }
            }
        }
        if (auto* v = be->get_item_by_name("brightness"))
            bd.brightness = brightnessFromName(v->get_string_value());
        if (auto* v = be->get_item_by_name("inactive_color"); v && v->is_array()) {
            for (int k = 0; k < 3 && k < v->m_array->GetSize(); ++k) {
                if (auto* s = v->enum_item(k)->get_string_value(true)) {
                    int x = std::atoi(s);
                    if (x < 0) x = 0; else if (x > 255) x = 255;
                    bd.inactiveColor[k] = static_cast<uint8_t>(x);
                }
            }
        } else {
            // Pre-split configs only carried `color`. Mirror it into
            // inactiveColor so quantising into the same palette entry
            // keeps the old visual identity.
            bd.inactiveColor[0] = bd.color[0];
            bd.inactiveColor[1] = bd.color[1];
            bd.inactiveColor[2] = bd.color[2];
        }
        if (auto* v = be->get_item_by_name("inactive_brightness"))
            bd.inactiveBrightness = brightnessFromName(v->get_string_value());
        if (auto* v = be->get_item_by_name("led_show_when_empty"))
            if (auto* s = v->get_string_value(true))
                bd.ledShowWhenEmpty = (std::strcmp(s, "true") == 0
                                    || std::strcmp(s, "1") == 0);

        // New-schema matrix. Both `short` and `long` are optional —
        // missing slots stay at default (Noop).
        if (auto* v = be->get_item_by_name("short"))
            parseMatrixRow_(v, bd.shortPress);
        if (auto* v = be->get_item_by_name("long"); v && v->is_object()) {
            bd.hasLongPress = true;
            parseMatrixRow_(v, bd.longPress);
        }

        // Old-schema fallback: pre-modifier-matrix configs carried bare
        // `type`/`action`/`param`/`midi` + `long_press` at the binding
        // level. Both the binding object and the `long_press` object
        // happen to use the same {type,action,param,midi:{}} shape that
        // parseSlotFields_ already understands — re-use it. Skipped if
        // the new matrix already populated the corresponding plain slot.
        ActionSlot& sp = bd.shortPress[static_cast<int>(Modifier::Plain)];
        if (slotIsEmpty_(sp)) parseSlotFields_(be, sp);
        if (auto* lp = be->get_item_by_name("long_press"); lp && lp->is_object()
            && slotIsEmpty_(bd.longPress[static_cast<int>(Modifier::Plain)])) {
            bd.hasLongPress = true;
            parseSlotFields_(lp, bd.longPress[static_cast<int>(Modifier::Plain)]);
        }

        // Migration: rename the legacy `fine_modifier` builtin to the
        // generic `mod_shift` so it slots into the new modifier framework.
        if (sp.type == ActionType::Builtin && sp.action == "fine_modifier") {
            sp.action = "mod_shift";
        }
        // Migration: send/receive routing builtins were originally split
        // by physical output (`send_all_3_vpot`, `send_all_3_fader`,
        // `send_this_vpot`, `send_this_fader`, plus recv_* twins). They
        // collapsed to a single name + a "Flip" param (0 = Faders,
        // 1 = V-Pots) — strip the suffix and set the param accordingly.
        if (sp.type == ActionType::Builtin) {
            auto endsWith = [](const std::string& s, const char* suffix) {
                const size_t n = std::strlen(suffix);
                return s.size() >= n
                    && std::strncmp(s.c_str() + s.size() - n, suffix, n) == 0;
            };
            if ((sp.action.rfind("send_all_", 0) == 0
              || sp.action.rfind("recv_all_", 0) == 0
              || sp.action == "send_this_vpot" || sp.action == "send_this_fader"
              || sp.action == "recv_this_vpot" || sp.action == "recv_this_fader")) {
                if (endsWith(sp.action, "_vpot")) {
                    sp.action.resize(sp.action.size() - 5);
                    sp.param = 1;   // Flip → V-Pots
                } else if (endsWith(sp.action, "_fader")) {
                    sp.action.resize(sp.action.size() - 6);
                    sp.param = 0;   // Default → Faders
                }
            }
        }

        out.bindings[bid] = std::move(bd);
    }
    return true;
}

// v5 → v6: convert "type=Builtin, action=empty" entries to Noop so
// they stop silently no-op'ing on press and are visible-to-fix in
// the Settings editor. This ALSO walks every modifier slot and the
// long-press matrix.
void upgradeEmptyBuiltinSlots_(Layer& L)
{
    auto fix = [](ActionStep& sp) {
        if (sp.type == ActionType::Builtin && sp.action.empty()) {
            sp.type = ActionType::Noop;
        }
    };
    for (auto& kv : L.bindings) {
        Binding& bd = kv.second;
        for (int m = 0; m < kModifierCount; ++m) {
            fix(bd.shortPress[m]);
            for (auto& step : bd.shortPress[m].extraSteps) fix(step);
            fix(bd.longPress[m]);
            for (auto& step : bd.longPress[m].extraSteps) fix(step);
        }
    }
}

// v13 → v14 (2026-05-15): collapse the four legacy Plugin Mode toggle
// builtins into one. Walks every modifier slot + long-press matrix +
// extraSteps so a chain step that references the old name also gets
// rewritten. Returns true if any *_with_gui variant was found — the
// caller persists that into the new pluginModeOpensGui ExtState so
// users who had the GUI variant bound preserve their preference.
bool upgradePluginModeBuiltins_(Layer& L)
{
    bool sawWithGui = false;
    auto fix = [&](ActionStep& sp) {
        if (sp.type != ActionType::Builtin) return;
        if (sp.action == "ssl_strip_mode_toggle"
         || sp.action == "uf8_plugin_mode_toggle") {
            sp.action = "plugin_mode_toggle";
        } else if (sp.action == "ssl_strip_mode_toggle_with_gui"
                || sp.action == "uf8_plugin_mode_toggle_with_gui") {
            sp.action  = "plugin_mode_toggle";
            sawWithGui = true;
        }
    };
    for (auto& kv : L.bindings) {
        Binding& bd = kv.second;
        for (int m = 0; m < kModifierCount; ++m) {
            fix(bd.shortPress[m]);
            for (auto& step : bd.shortPress[m].extraSteps) fix(step);
            fix(bd.longPress[m]);
            for (auto& step : bd.longPress[m].extraSteps) fix(step);
        }
    }
    return sawWithGui;
}

// v4 → v5 reset: wipe ALL Auto-row + Zoom-pad colours to white.
// Frank 2026-05-07: factory hardware-default colours are not wanted —
// every LED is user-chosen via Settings → Bindings. Configs created
// before this rule had auto_*/zoom_* coloured by seedFactoryDefaults_
// and/or the old in-parseLayer migration. This one-shot upgrade
// resets them to white so the editor presents a blank canvas.
// Buttons whose binding the user has explicitly recoloured to
// something OTHER than the previous factory value are left alone
// (the upgrade only touches bindings whose colour exactly matches
// the historical hardcoded swatch).
void upgradeStripFactoryColours_(Layer& L)
{
    struct Reset { const char* action; uint8_t r, g, b; };
    static constexpr Reset kResets[] = {
        {"auto_read",    0,   255,   0},
        {"auto_write",   255, 0,     0},
        {"auto_trim",    255, 128,   0},
        {"auto_latch",   255, 0,     0},
        {"auto_touch",   255, 255,   0},
        {"zoom_up",      0,   255,   0},
        {"zoom_down",    255, 255,   0},
        {"zoom_center",  255, 0,     0},
    };
    for (auto& kv : L.bindings) {
        Binding& bd = kv.second;
        ActionStep& sp = bd.shortPress[
            static_cast<int>(Modifier::Plain)];
        if (sp.type != ActionType::Builtin) continue;
        for (const auto& rs : kResets) {
            if (sp.action != rs.action) continue;
            const bool matchesOld =
                (bd.color[0] == rs.r && bd.color[1] == rs.g && bd.color[2] == rs.b);
            if (matchesOld) {
                bd.color[0] = 0xFF; bd.color[1] = 0xFF; bd.color[2] = 0xFF;
                bd.inactiveColor[0] = 0xFF;
                bd.inactiveColor[1] = 0xFF;
                bd.inactiveColor[2] = 0xFF;
            }
            break;
        }
    }
}

// ---- Path helpers ---------------------------------------------------------

std::string configDir_()
{
    const char* base = GetResourcePath ? GetResourcePath() : nullptr;
    if (!base || !*base) base = ".";
    std::string d = base;
    d += "/rea_sixty";
    return d;
}

std::string configPath_()
{
    return configDir_() + "/bindings.json";
}

void ensureConfigDir_()
{
    const std::string d = configDir_();
    struct stat st{};
    if (stat(d.c_str(), &st) == 0) return;
#ifdef _WIN32
    _mkdir(d.c_str());
#else
    mkdir(d.c_str(), 0755);
#endif
}

bool readFile_(const std::string& path, std::string& out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n < 0) { std::fclose(f); return false; }
    out.resize(static_cast<size_t>(n));
    if (n > 0) std::fread(out.data(), 1, static_cast<size_t>(n), f);
    std::fclose(f);
    return true;
}

bool writeFile_(const std::string& path, const std::string& contents)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(contents.data(), 1, contents.size(), f);
    std::fclose(f);
    return true;
}

bool tryParse_(const std::string& json, Config& out)
{
    wdl_json_parser p;
    wdl_json_element* root = p.parse(json.c_str(), static_cast<int>(json.size()));
    if (!root || !root->is_object()) return false;

    if (auto* v = root->get_item_by_name("version"))
        if (auto* s = v->get_string_value(true)) out.version = std::atoi(s);
    if (auto* v = root->get_item_by_name("active_layer"))
        if (auto* s = v->get_string_value(true)) out.activeLayer = std::atoi(s);
    if (out.activeLayer < 0 || out.activeLayer > 2) out.activeLayer = 0;

    if (auto* arr = root->get_item_by_name("layers"); arr && arr->is_array()) {
        const int n = arr->m_array ? arr->m_array->GetSize() : 0;
        for (int i = 0; i < n && i < 3; ++i) {
            parseLayer_(arr->enum_item(i), out.layers[i]);
        }
    }
    parseUserQuicks_(root, out);
    parseSubBankLeds_(root, out);
    parseBankPresets_(root, out);
    return true;
}

} // namespace

void registerBuiltin(const char* name, BuiltinDescriptor desc)
{
    if (!name || !*name) return;
    g_builtins[name] = std::move(desc);
}

// Bumped each time we ship a default-binding change that needs to
// reach existing configs. load() runs every defined upgrade step in
// order, then writes the bumped version back so the upgrade is
// idempotent across REAPER restarts.
// v7 (2026-05-13): Quick = user-key-bank container refactor. The flat
// 12 × 8 UserBank model is gone; per-layer LayerUserQuicks (3 quicks ×
// 6 sub-banks × 8 slots) replaces it. Old v6 "user_banks" data is
// discarded on upgrade (clean slate per Frank's resolved Q6).
// Quick factory bindings become quick_select_1/2/3 (Layer 0 Q1/Q2 keep
// the SSL CS/BC focus side-effect; everywhere else they are pure
// user-Quick toggles).
// v6 (2026-05-07): clean up corrupt "type=Builtin, action=empty"
// entries left behind by the Settings UI's combo-picker race (user
// clicks Built-in radio, picks no name in the combo, dirty flag
// triggers setBinding with an unsalvageable entry that silently
// no-ops on press). Convert those entries to Noop so they're at
// least UI-fixable (currently they sit corrupt forever).
// v5 (2026-05-07): zoom + auto factory colours abolished — every LED
// is user-chosen. v4→v5 upgrade resets the historical hardcoded
// swatches (auto_read/green, zoom_up/green, …) back to white.
// v4 (2026-05-07): unused — bumped only to gate the colour-migration
// fix that landed mid-day; superseded by v5 the same day.
// v8 (2026-05-13): undo the Layer-1 Q1/Q2 Quick-select mapping from v7.
// v7's factory swap to quick_select_1/2 broke the SSL CS/BC plug-in
// labels + soft-key dispatch on Layer 1 (engaging g_activeQuick[0]
// routed the top-soft-key row to empty user-Quick slots). Q1/Q2 go
// back to domain_cs/domain_bc Momentary. Q3 stays quick_select_3.
// v9 (2026-05-13): backfill the Quick + Layer-select bindings that
// seedFactoryDefaults_ now puts on Layer 2 + 3 but that historical
// configs are missing. Without them the layer-indicator + Quick-
// button LEDs sit dark on Layer 2/3 (resolveLed_ returns Off when
// the active layer has no binding for the button) and the user-
// Quick render never engages because pressing Q1/Q2/Q3 finds no
// builtin to fire. Also rewrites stale domain_cs / domain_bc that
// older factories planted on Layer 2 + 3's Q1/Q2 — those make no
// sense outside Layer 1 and were Frank's surface complaint
// ("Quick 1 + 2 show same values as Layer 1 instead of empty").
// v10 (2026-05-13): drop the dead-builtin set entirely
// (quick_select_X / user_domain_X / show_user_bank / layer_select
// param-form) and migrate every binding that referenced them to
// the surviving builtins: quick_select_N → softkey_bank_(L×3+N) for
// the binding's owning layer; user_domain_N → same; show_user_bank
// → Noop; "layer_select" + param → layer_select_(param+1). Toggle
// behaviour for Quick is gone — Frank 2026-05-13: "Toggle für Quick
// macht null Sinn".
// v11 (2026-05-13): backfill the Soft-Key Bank selectors
// (VPotBank / SoftKey1Bank..SoftKey5Bank) on Layer 2 + 3 in
// historical configs. Without those entries the buttons were
// dead on the upper layers — pressing them did nothing.
// v12 (2026-05-13): add per-(Layer, Quick) Sub-Bank LED overrides
// (SubBankLed). Lets each (L, Q) coordinate define its own 6
// V-POT/Soft 1-5 LED colours so engaged Quick contexts are
// visually distinguishable. Default-construct on existing configs
// (white/bright/dim) — no behaviour change until the user starts
// setting overrides in the editor.
// v13 (2026-05-14): introduce named Soft-Key Bank presets — a flat
// list of {name, slots[8]} entries stored alongside the userQuicks.
// Persisted under the top-level "bank_presets" key. Older configs
// load with an empty list (no migration needed; presets are an
// additive feature). The Bindings → Sub-Bank cell editor exposes
// Save/Recall/Rename/Delete.
// v14 (2026-05-15): Plugin Mode unification — the legacy four toggle
// builtins (ssl_strip_mode_toggle, ssl_strip_mode_toggle_with_gui,
// uf8_plugin_mode_toggle, uf8_plugin_mode_toggle_with_gui) collapse
// into a single plugin_mode_toggle. The "with GUI" variants are
// folded into a separate plugin_mode_open_gui preference. If ANY of
// the legacy _with_gui actions is found at load, the preference is
// set to true; otherwise it stays at its default (also true) unless
// an ExtState override exists. Migration touches every modifier slot
// + long-press matrix + extraSteps.
constexpr int kCurrentBindingsVersion = 14;

// v7→v8: restore Layer-1 Q1/Q2 to the SSL CS/BC Momentary builtins.
// Only touches bindings that exactly match the v7 factory swap (so
// users who customised these slots themselves keep their choice).
void upgradeRestoreLayer1Quicks_(Layer& L1)
{
    auto restore = [&](ButtonId id, const char* fromAction,
                       const char* toAction, const char* label) {
        auto it = L1.bindings.find(id);
        if (it == L1.bindings.end()) return;
        Binding& bd = it->second;
        auto& sp = bd.shortPress[static_cast<int>(Modifier::Plain)];
        if (sp.type != ActionType::Builtin) return;
        if (sp.action != fromAction) return;
        // Reset short-press to a clean Builtin slot pointing at the
        // canonical CS/BC focus builtin. Behavior flips back to
        // Momentary; label restored to the SSL-style 2-char tag.
        sp = ActionSlot{};
        sp.type     = ActionType::Builtin;
        sp.action   = toAction;
        bd.label    = label;
        bd.behavior = Behavior::Momentary;
    };
    restore(ButtonId::Quick1, "quick_select_1", "domain_cs", "CS");
    restore(ButtonId::Quick2, "quick_select_2", "domain_bc", "BC");
}

// v8→v9: ensure every layer carries the factory-baseline bindings
// for Layer-select + Quick. Missing entries are filled; stale entries
// on L2/L3 (Quick1=domain_cs, Quick2=domain_bc) get rewritten to the
// canonical user-Quick toggles. Layer-1 user customisations survive
// because we only touch L1 Q1/Q2 if they're already the v7-style
// quick_select_* (handled by v7→v8 above) — v9 doesn't re-touch L1.
void upgradeBackfillQuickAndLayerLeds_(Config& c)
{
    auto fillIfMissing = [](Layer& L, ButtonId id, const char* action,
                            Behavior beh, const char* label) {
        if (L.bindings.find(id) != L.bindings.end()) return;
        Binding bd;
        bd.behavior = beh;
        bd.label    = label;
        auto& sp = bd.shortPress[static_cast<int>(Modifier::Plain)];
        sp.type   = ActionType::Builtin;
        sp.action = action;
        L.bindings[id] = bd;
    };
    auto rewriteIfMatches = [](Layer& L, ButtonId id, const char* fromAction,
                               const char* toAction, const char* label) {
        auto it = L.bindings.find(id);
        if (it == L.bindings.end()) return false;
        Binding& bd = it->second;
        auto& sp = bd.shortPress[static_cast<int>(Modifier::Plain)];
        if (sp.type != ActionType::Builtin) return false;
        if (sp.action != fromAction) return false;
        sp = ActionSlot{};
        sp.type     = ActionType::Builtin;
        sp.action   = toAction;
        bd.label    = label;
        bd.behavior = Behavior::Toggle;
        return true;
    };

    for (int li = 0; li < 3; ++li) {
        Layer& L = c.layers[li];
        fillIfMissing(L, ButtonId::Layer1,
                      "layer_select_1", Behavior::Momentary, "LAYER 1");
        fillIfMissing(L, ButtonId::Layer2,
                      "layer_select_2", Behavior::Momentary, "LAYER 2");
        fillIfMissing(L, ButtonId::Layer3,
                      "layer_select_3", Behavior::Momentary, "LAYER 3");
    }
    // Layer 1: keep the canonical domain_cs/bc on Q1/Q2 + quick_select_3
    // on Q3. Only fill missing slots — don't stomp user customisations.
    {
        Layer& L1 = c.layers[0];
        fillIfMissing(L1, ButtonId::Quick1,
                      "domain_cs",      Behavior::Momentary, "CS");
        fillIfMissing(L1, ButtonId::Quick2,
                      "domain_bc",      Behavior::Momentary, "BC");
        fillIfMissing(L1, ButtonId::Quick3,
                      "quick_select_3", Behavior::Toggle,    "Q3");
    }
    // Layers 2 + 3: rewrite stale domain_cs / domain_bc → quick_select_*
    // (Layer 1's CS/BC focus only makes sense on Layer 1), then fill
    // anything still missing.
    for (int li = 1; li <= 2; ++li) {
        Layer& L = c.layers[li];
        rewriteIfMatches(L, ButtonId::Quick1, "domain_cs",
                         "quick_select_1", "Q1");
        rewriteIfMatches(L, ButtonId::Quick2, "domain_bc",
                         "quick_select_2", "Q2");
        fillIfMissing(L, ButtonId::Quick1,
                      "quick_select_1", Behavior::Toggle, "Q1");
        fillIfMissing(L, ButtonId::Quick2,
                      "quick_select_2", Behavior::Toggle, "Q2");
        fillIfMissing(L, ButtonId::Quick3,
                      "quick_select_3", Behavior::Toggle, "Q3");
    }
}

// v10 → v11: seed the Soft-Key Bank selectors on Layer 2 + 3 for
// configs whose seedFactoryDefaults_ ran before those bindings
// moved out of L1-only territory.
void upgradeBackfillBankSelectorsAllLayers_(Config& c)
{
    static const ButtonId kBankIds[6] = {
        ButtonId::VPotBank,
        ButtonId::SoftKey1Bank, ButtonId::SoftKey2Bank,
        ButtonId::SoftKey3Bank, ButtonId::SoftKey4Bank,
        ButtonId::SoftKey5Bank,
    };
    static const char* kBankLabels[6] = {
        "V-POT", "BANK 1", "BANK 2", "BANK 3", "BANK 4", "BANK 5",
    };
    auto fillIfMissing = [](Layer& L, ButtonId id, const char* action,
                            const char* label, int param) {
        if (L.bindings.find(id) != L.bindings.end()) return;
        Binding bd;
        bd.behavior = Behavior::Momentary;
        bd.label    = label;
        auto& sp = bd.shortPress[static_cast<int>(Modifier::Plain)];
        sp.type   = ActionType::Builtin;
        sp.action = action;
        sp.param  = param;
        L.bindings[id] = bd;
    };
    for (int li = 1; li <= 2; ++li) {
        auto& L = c.layers[li];
        for (int i = 0; i < 6; ++i) {
            fillIfMissing(L, kBankIds[i], "softkey_bank_select",
                          kBankLabels[i], i);
        }
    }
}

// v9 → v10: scrub the dead builtins out of every binding slot
// (shortPress + longPress, all modifier rows). Maps each old action
// to its surviving equivalent. The replacement for quick_select_N /
// user_domain_N depends on the layer the binding lives on — Quick-N
// engaged on layer L maps to softkey_bank_(L*3+N+1) (1-indexed).
void upgradeRetireQuickSelect_(Config& c)
{
    auto migrateSlot = [](int layer, ActionSlot& s) {
        if (s.type != ActionType::Builtin) return;
        const std::string& a = s.action;
        auto isQuickFamily = [&](int& outN) {
            if (a == "quick_select_1" || a == "user_domain_1") { outN = 0; return true; }
            if (a == "quick_select_2" || a == "user_domain_2") { outN = 1; return true; }
            if (a == "quick_select_3" || a == "user_domain_3") { outN = 2; return true; }
            return false;
        };
        int qN = -1;
        if (isQuickFamily(qN)) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "softkey_bank_%d",
                          layer * 3 + qN + 1);
            s.action = buf;
            return;
        }
        if (a == "show_user_bank") {
            // The flat-bank model is gone. The action is a pure no-op.
            // Clear the slot so the picker shows it as unbound rather
            // than carrying a phantom builtin reference.
            s = ActionSlot{};
            return;
        }
        if (a == "layer_select") {
            // param 0..2 → layer_select_1..3
            const int p = s.param;
            if (p >= 0 && p <= 2) {
                char buf[24];
                std::snprintf(buf, sizeof(buf), "layer_select_%d", p + 1);
                s.action = buf;
                s.param  = 0;
            } else {
                s = ActionSlot{};
            }
            return;
        }
    };
    for (int li = 0; li < 3; ++li) {
        for (auto& kv : c.layers[li].bindings) {
            Binding& bd = kv.second;
            for (int m = 0; m < kModifierCount; ++m) {
                migrateSlot(li, bd.shortPress[m]);
                migrateSlot(li, bd.longPress[m]);
            }
            // Behavior was Toggle for the Quick buttons under the old
            // model — flip to Momentary so the new softkey_bank_N
            // press semantics match the factory default.
            const ButtonId id = kv.first;
            if (id == ButtonId::Quick1 || id == ButtonId::Quick2
             || id == ButtonId::Quick3) {
                if (bd.behavior == Behavior::Toggle) {
                    bd.behavior = Behavior::Momentary;
                }
            }
        }
    }
}

// Sanitize the Sub-Bank-selector + Quick-button bindings against
// data corruption from previous migrations. Two distinct cases:
//
// 1) *Bank cells (VPotBank, SoftKey1-5Bank). These are HARD-CODED
//    invariants: drawSubBankCellEditor_ doesn't expose a Bindings
//    picker so a user cannot legitimately customize the action.
//    Force action='softkey_bank_select' with param = cell index.
//    Fixes the case where upgradeRetireQuickSelect_ cleared a stale
//    'show_user_bank' to an empty ActionSlot AND
//    upgradeBackfillBankSelectorsAllLayers_'s fillIfMissing then
//    refused to fill (because the binding key already existed with
//    empty action). Symptom 2026-05-13: Frank's L2.VPotBank +
//    L2.SoftKey1Bank were stuck with action='' → banks 0+1 dark.
//
// 2) Quick cells (Quick1/Quick2/Quick3). Reset to canonical only
//    in narrow cases:
//      a) action is empty (no Builtin name set).
//      b) action is "softkey_bank_N" with N != the cell's canonical
//         and the canonical is itself a "softkey_bank_M" jump (i.e.
//         L1.Q3 + L2/L3 Q1/Q2/Q3). Catches the case where
//         upgradeRetireQuickSelect_ rewrote a user's old
//         quick_select_N on a Quick-M cell into a wrong-target jump.
//         Symptom 2026-05-13: Frank's L1.Q3 = softkey_bank_1
//         (= jump to L1 Q1) → pressing Q3 yanked the ring back.
//    L1.Q1/Q2 canonicals are domain_cs/domain_bc (not jumps); we
//    only reset those when empty so user customizations to other
//    builtins / REAPER actions / cross-layer jumps survive.
//
// Both passes preserve color, brightness, inactive*, label, and
// any modifier-row / longPress slots. They only touch
// shortPress[Plain].
void upgradeSanitizeBankAndQuickActions_(Config& c)
{
    FILE* lg = std::fopen("/tmp/rea_sixty.log", "a");
    auto logReset = [&](int li, const char* cellName,
                        const std::string& oldAction,
                        const std::string& newAction) {
        if (!lg) return;
        std::fprintf(lg,
            "  [sanitize] L%d %s reset: action='%s' → '%s'\n",
            li + 1, cellName,
            oldAction.empty() ? "(empty)" : oldAction.c_str(),
            newAction.c_str());
    };

    static const ButtonId kBankIds[6] = {
        ButtonId::VPotBank,
        ButtonId::SoftKey1Bank, ButtonId::SoftKey2Bank,
        ButtonId::SoftKey3Bank, ButtonId::SoftKey4Bank,
        ButtonId::SoftKey5Bank,
    };
    static const char* kBankNames[6] = {
        "VPotBank", "SoftKey1Bank", "SoftKey2Bank",
        "SoftKey3Bank", "SoftKey4Bank", "SoftKey5Bank",
    };
    static const char* kBankLabels[6] = {
        "V-POT", "BANK 1", "BANK 2", "BANK 3", "BANK 4", "BANK 5",
    };
    for (int li = 0; li < 3; ++li) {
        auto& L = c.layers[li];
        for (int i = 0; i < 6; ++i) {
            auto it = L.bindings.find(kBankIds[i]);
            if (it == L.bindings.end()) {
                L.bindings[kBankIds[i]] = mkBuiltin(
                    "softkey_bank_select", Behavior::Momentary,
                    kBankLabels[i], 255, 255, 255, i);
                if (lg) {
                    std::fprintf(lg,
                        "  [sanitize] L%d %s created: "
                        "softkey_bank_select param=%d\n",
                        li + 1, kBankNames[i], i);
                }
                continue;
            }
            Binding& bd = it->second;
            auto& sp = bd.shortPress[static_cast<int>(Modifier::Plain)];
            const bool needsAction =
                sp.type != ActionType::Builtin
                || sp.action != "softkey_bank_select";
            const bool needsParam = sp.param != i;
            if (needsAction || needsParam) {
                const std::string oldAction = sp.action;
                sp.type   = ActionType::Builtin;
                sp.action = "softkey_bank_select";
                sp.param  = i;
                if (bd.label.empty()) bd.label = kBankLabels[i];
                if (bd.behavior == Behavior::Toggle) {
                    bd.behavior = Behavior::Momentary;
                }
                logReset(li, kBankNames[i], oldAction,
                         "softkey_bank_select");
            }
        }
    }

    static const ButtonId kQuickIds[3] = {
        ButtonId::Quick1, ButtonId::Quick2, ButtonId::Quick3,
    };
    static const char* kQuickNames[3] = {"Quick1", "Quick2", "Quick3"};
    for (int li = 0; li < 3; ++li) {
        auto& L = c.layers[li];
        for (int qN = 0; qN < 3; ++qN) {
            std::string canonical;
            const char* canonLabel = nullptr;
            bool canonIsJump = false;
            if (li == 0 && qN == 0) {
                canonical  = "domain_cs";
                canonLabel = "CS";
            } else if (li == 0 && qN == 1) {
                canonical  = "domain_bc";
                canonLabel = "BC";
            } else {
                char buf[24];
                std::snprintf(buf, sizeof(buf),
                              "softkey_bank_%d", li * 3 + qN + 1);
                canonical    = buf;
                canonIsJump  = true;
                canonLabel   = (qN == 0) ? "Q1"
                             : (qN == 1) ? "Q2"
                                         : "Q3";
            }

            auto it = L.bindings.find(kQuickIds[qN]);
            if (it == L.bindings.end()) {
                L.bindings[kQuickIds[qN]] = mkBuiltin(
                    canonical.c_str(), Behavior::Momentary, canonLabel);
                if (lg) {
                    std::fprintf(lg,
                        "  [sanitize] L%d %s created: '%s'\n",
                        li + 1, kQuickNames[qN], canonical.c_str());
                }
                continue;
            }
            Binding& bd = it->second;
            auto& sp = bd.shortPress[static_cast<int>(Modifier::Plain)];

            const bool actionEmpty =
                sp.type != ActionType::Builtin
                || sp.action.empty();

            // Mismatched-jump: action is "softkey_bank_N" (a layer-
            // jump) pointing elsewhere than this cell's canonical
            // self-jump. Only fires when canonical is itself a jump
            // (otherwise we'd nuke valid cross-jump customizations
            // on L1.Q1/Q2).
            bool mismatchedJump = false;
            if (canonIsJump
                && sp.type == ActionType::Builtin
                && sp.action.rfind("softkey_bank_", 0) == 0
                && sp.action != "softkey_bank_select"
                && sp.action != canonical) {
                mismatchedJump = true;
            }

            if (actionEmpty || mismatchedJump) {
                const std::string oldAction = sp.action;
                sp = ActionSlot{};
                sp.type   = ActionType::Builtin;
                sp.action = canonical;
                sp.param  = 0;
                if (bd.label.empty()) bd.label = canonLabel;
                if (bd.behavior == Behavior::Toggle) {
                    bd.behavior = Behavior::Momentary;
                }
                logReset(li, kQuickNames[qN], oldAction, canonical);
            }
        }
    }

    if (lg) std::fclose(lg);
}

// Upgrade hook: existing configs get factory long-press defaults on
// the FLIP button (send_this / recv_this+Shift) without touching any
// other field. Skipped if the user has already set their own
// long-press for FLIP — explicit assignments always win.
void upgradeFlipLongPress_(Layer& L)
{
    auto it = L.bindings.find(ButtonId::Flip);
    if (it == L.bindings.end()) return;
    Binding& bd = it->second;
    if (bd.hasLongPress) return;
    bd.behavior     = Behavior::Momentary;
    bd.hasLongPress = true;
    auto& lpPlain = bd.longPress[static_cast<int>(Modifier::Plain)];
    lpPlain.type   = ActionType::Builtin;
    lpPlain.action = "send_this";
    lpPlain.param  = 1;
    auto& lpShift = bd.longPress[static_cast<int>(Modifier::Shift)];
    lpShift.type   = ActionType::Builtin;
    lpShift.action = "recv_this";
    lpShift.param  = 1;
}

// v3 upgrade: a previous editor version auto-filled per-binding
// labels for ssl_softkey from the V-POT bank's slot names. ssl_softkey
// is bank-aware though — that label then mis-displayed on every other
// PAGE bank. Clear those auto-filled labels so the runtime falls
// back to the live SSL softkey label per current bank. Only sweeps
// shortPress[Plain] since that's the only slot the auto-fill could
// have touched.
void upgradeSslSoftkeyLabels_(Layer& L)
{
    for (auto& kv : L.bindings) {
        Binding& bd = kv.second;
        auto& sp = bd.shortPress[static_cast<int>(Modifier::Plain)];
        if (sp.type == ActionType::Builtin && sp.action == "ssl_softkey") {
            sp.label.clear();
        }
    }
}

void load()
{
    std::lock_guard<std::mutex> lk(g_cfgMutex);

    std::string contents;
    if (readFile_(configPath_(), contents) && !contents.empty()) {
        Config tmp;
        seedFactoryDefaults_(tmp);     // start from factories so missing fields fall back
        if (tryParse_(contents, tmp)) {
            // One-shot upgrades for configs from older versions. Each
            // step is idempotent (re-running a completed step is a
            // no-op) so the conditional guard is mostly a perf hint.
            if (tmp.version < 2) {
                for (auto& L : tmp.layers) upgradeFlipLongPress_(L);
            }
            if (tmp.version < 3) {
                for (auto& L : tmp.layers) upgradeSslSoftkeyLabels_(L);
            }
            if (tmp.version < 5) {
                for (auto& L : tmp.layers) upgradeStripFactoryColours_(L);
            }
            if (tmp.version < 6) {
                for (auto& L : tmp.layers) upgradeEmptyBuiltinSlots_(L);
            }
            if (tmp.version < 8) {
                upgradeRestoreLayer1Quicks_(tmp.layers[0]);
            }
            if (tmp.version < 9) {
                upgradeBackfillQuickAndLayerLeds_(tmp);
            }
            if (tmp.version < 10) {
                upgradeRetireQuickSelect_(tmp);
            }
            if (tmp.version < 11) {
                upgradeBackfillBankSelectorsAllLayers_(tmp);
            }
            // v13 → v14: Plugin Mode unification. Idempotent — once the
            // legacy names are rewritten they're invisible to the next
            // pass, so running it on already-migrated configs is safe.
            if (tmp.version < 14) {
                bool sawWithGui = false;
                for (auto& L : tmp.layers) {
                    if (upgradePluginModeBuiltins_(L)) sawWithGui = true;
                }
                // Carry the legacy *_with_gui preference into the new
                // shared pluginModeOpensGui ExtState. We don't touch the
                // ExtState if no _with_gui variant was found — that
                // leaves the default (= true) in place for fresh configs
                // and respects any explicit user setting from later
                // sessions.
                if (sawWithGui) {
                    SetExtState("ReaSixty", "pluginModeOpensGui", "1", true);
                }
            }
            // Belt-and-suspenders sanitize. Always runs, regardless of
            // version, so any stale references to removed builtins
            // (quick_select_X / user_domain_X / show_user_bank /
            // layer_select param-form) get rewritten even in configs
            // that somehow ended up past v10 without the action-name
            // migration sticking. Idempotent on already-migrated data.
            upgradeRetireQuickSelect_(tmp);

            // Bank/Quick action sanitize. Always runs. Fixes two
            // 2026-05-13 data-corruption patterns: (1) *Bank cells
            // left empty after show_user_bank → '' cleared and
            // fillIfMissing wouldn't re-fill an existing-but-empty
            // entry; (2) Quick cells with mismatched softkey_bank_N
            // jumps produced by the quick_select_N migration. See
            // upgradeSanitizeBankAndQuickActions_ above for rules.
            upgradeSanitizeBankAndQuickActions_(tmp);

            // Diagnostic snapshot. Dumps the resolved bindings for the
            // load-bearing buttons so "press did nothing" / "LED dark"
            // reports can be diagnosed from a single shared file.
            if (FILE* lg = std::fopen("/tmp/rea_sixty.log", "a")) {
                std::fprintf(lg,
                    "[bindings::load] version_read=%d → migrated_to=%d\n",
                    tmp.version, kCurrentBindingsVersion);
                auto dumpBtn = [&](int li, ButtonId id, const char* name) {
                    auto it = tmp.layers[li].bindings.find(id);
                    if (it == tmp.layers[li].bindings.end()) {
                        std::fprintf(lg,
                            "  L%d %-14s : (missing)\n", li + 1, name);
                        return;
                    }
                    const Binding& bd = it->second;
                    const auto& sp =
                        bd.shortPress[static_cast<int>(Modifier::Plain)];
                    std::fprintf(lg,
                        "  L%d %-14s : action='%s' param=%d "
                        "behav=%d  rgb=(%d,%d,%d) bri=%d  "
                        "inact_rgb=(%d,%d,%d) inact_bri=%d\n",
                        li + 1, name,
                        sp.action.c_str(), sp.param,
                        static_cast<int>(bd.behavior),
                        bd.color[0], bd.color[1], bd.color[2],
                        static_cast<int>(bd.brightness),
                        bd.inactiveColor[0], bd.inactiveColor[1],
                        bd.inactiveColor[2],
                        static_cast<int>(bd.inactiveBrightness));
                };
                for (int li = 0; li < 3; ++li) {
                    dumpBtn(li, ButtonId::Layer1, "Layer1");
                    dumpBtn(li, ButtonId::Layer2, "Layer2");
                    dumpBtn(li, ButtonId::Layer3, "Layer3");
                    dumpBtn(li, ButtonId::Quick1, "Quick1");
                    dumpBtn(li, ButtonId::Quick2, "Quick2");
                    dumpBtn(li, ButtonId::Quick3, "Quick3");
                    dumpBtn(li, ButtonId::VPotBank,     "VPotBank");
                    dumpBtn(li, ButtonId::SoftKey1Bank, "SoftKey1Bank");
                    dumpBtn(li, ButtonId::SoftKey2Bank, "SoftKey2Bank");
                    dumpBtn(li, ButtonId::SoftKey3Bank, "SoftKey3Bank");
                    dumpBtn(li, ButtonId::SoftKey4Bank, "SoftKey4Bank");
                    dumpBtn(li, ButtonId::SoftKey5Bank, "SoftKey5Bank");
                }
                std::fclose(lg);
            }
            tmp.version = kCurrentBindingsVersion;
            g_cfg = std::move(tmp);
            // Persist the upgraded config so the next load doesn't
            // re-walk the upgrade chain.
            ensureConfigDir_();
            writeFile_(configPath_(), serialize(g_cfg));
            g_bindingsGen.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }

    // First run, missing file, or parse error: seed factories + persist.
    seedFactoryDefaults_(g_cfg);
    ensureConfigDir_();
    writeFile_(configPath_(), serialize(g_cfg));
    g_bindingsGen.fetch_add(1, std::memory_order_relaxed);
}

void save()
{
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    ensureConfigDir_();
    writeFile_(configPath_(), serialize(g_cfg));
}

bool exportTo(const std::string& path)
{
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    return writeFile_(path, serialize(g_cfg));
}

bool importFrom(const std::string& path)
{
    std::string contents;
    if (!readFile_(path, contents) || contents.empty()) return false;

    Config tmp;
    seedFactoryDefaults_(tmp);
    if (!tryParse_(contents, tmp)) return false;

    {
        std::lock_guard<std::mutex> lk(g_cfgMutex);
        g_cfg = std::move(tmp);
        ensureConfigDir_();
        writeFile_(configPath_(), serialize(g_cfg));
        g_bindingsGen.fetch_add(1, std::memory_order_relaxed);
    }
    return true;
}

bool exportLayerTo(int layer, const std::string& path)
{
    if (layer < 0 || layer > 2) return false;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    return writeFile_(path, serializeOneLayer_(g_cfg.layers[layer], layer));
}

bool importLayerFrom(int layer, const std::string& path)
{
    if (layer < 0 || layer > 2) return false;
    std::string contents;
    if (!readFile_(path, contents) || contents.empty()) return false;

    wdl_json_parser p;
    wdl_json_element* root = p.parse(contents.c_str(),
                                     static_cast<int>(contents.size()));
    if (!root || !root->is_object()) return false;

    // Accept both the wrapped {"type":"layer", ...} form and a bare layer
    // object (no "type" field) for forward-compat with hand-edited files.
    wdl_json_element* layerObj = nullptr;
    if (auto* t = root->get_item_by_name("type"); t) {
        const char* ts = t->get_string_value();
        if (!ts || std::strcmp(ts, "layer") != 0) return false;
        layerObj = root->get_item_by_name("layer");
    } else if (root->get_item_by_name("name")) {
        layerObj = root;  // bare layer
    }
    if (!layerObj || !layerObj->is_object()) return false;

    Layer tmp;
    if (!parseLayer_(layerObj, tmp)) return false;

    {
        std::lock_guard<std::mutex> lk(g_cfgMutex);
        g_cfg.layers[layer] = std::move(tmp);
        ensureConfigDir_();
        writeFile_(configPath_(), serialize(g_cfg));
        g_bindingsGen.fetch_add(1, std::memory_order_relaxed);
    }
    return true;
}

const Config& get()
{
    return g_cfg;
}

namespace {

// ---- MIDI output dispatch ------------------------------------------------
//
// We use StuffMIDIMessage rather than CreateMIDIOutput + midi_Output::Send.
// StuffMIDIMessage(16+N, …) writes to MIDI hardware output device N
// without needing the device to be "enabled for output" in REAPER's
// MIDI prefs (Preferences → MIDI Devices). CreateMIDIOutput returns
// nullptr for not-enabled devices — which Frank hit with his RME
// Fireface UFX+ on 2026-05-14: the device enumerated via
// GetMIDIOutputName but CreateMIDIOutput failed silently, and no MIDI
// reached the destination. StuffMIDIMessage matches the behaviour of
// the equivalent Lua (`reaper.StuffMIDIMessage(28, status, cc, val)` →
// device 12, 28-16=12) so any device the user can see in the dropdown
// is reachable.

// Build the MIDI status byte for the binding's MidiMsgType + channel
// (1..16). Returns -1 on invalid input. Program Change is single-data-
// byte and uses only midiData1 (the program number); midiData2 is
// ignored at dispatch.
int midiStatusByte_(int msgType, int channel1Based)
{
    if (channel1Based < 1 || channel1Based > 16) return -1;
    const uint8_t chBits = static_cast<uint8_t>(channel1Based - 1);
    switch (static_cast<MidiMsgType>(msgType)) {
        case MidiMsgType::NoteOn:        return 0x90 | chBits;
        case MidiMsgType::NoteOff:       return 0x80 | chBits;
        case MidiMsgType::ControlChange: return 0xB0 | chBits;
        case MidiMsgType::ProgramChange: return 0xC0 | chBits;
    }
    return -1;
}

void dispatchMidi_(const ActionStep& a)
{
    const int status = midiStatusByte_(a.midiMsgType, a.midiChannel);
    if (status < 0) return;
    const int d1 = std::clamp(a.midiData1, 0, 127);
    const bool isPC = (static_cast<MidiMsgType>(a.midiMsgType)
                       == MidiMsgType::ProgramChange);
    // Program Change has only one data byte; force d2=0 so we don't
    // send stray velocity-shaped trailing bytes.
    const int d2 = isPC ? 0 : std::clamp(a.midiData2, 0, 127);

    const int n = GetNumMIDIOutputs();
    if (a.midiDevice.empty()) {
        // "(all enabled outputs)" — iterate every enumerated device.
        // StuffMIDIMessage silently drops messages destined for
        // unmapped indices, so over-shooting is harmless.
        for (int i = 0; i < n; ++i) {
            char nm[256] = {0};
            if (!GetMIDIOutputName(i, nm, sizeof(nm))) continue;
            if (!*nm) continue;
            StuffMIDIMessage(16 + i, status, d1, d2);
        }
        return;
    }
    for (int i = 0; i < n; ++i) {
        char nm[256] = {0};
        if (!GetMIDIOutputName(i, nm, sizeof(nm))) continue;
        if (a.midiDevice == nm) {
            StuffMIDIMessage(16 + i, status, d1, d2);
            return;
        }
    }
    // Bound device name no longer enumerated (unplugged or renamed) —
    // log once for diagnosis but don't surface a UI error every press.
    if (FILE* lg = std::fopen("/tmp/rea_sixty.log", "a")) {
        std::fprintf(lg,
            "[midi] bound device '%s' not in current MIDI output list "
            "(unplugged or renamed)\n",
            a.midiDevice.c_str());
        std::fclose(lg);
    }
}

void runStep_(const ActionStep& a, bool firing, bool pressed)
{
    switch (a.type) {
        case ActionType::Noop:
            break;
        case ActionType::Reaper: {
            // Named commands (ReaScripts, custom actions) are stored as
            // "_RS<hash>" / "_<name>" — atoi would yield 0. Resolve via
            // NamedCommandLookup so script bindings dispatch correctly.
            int actionId = 0;
            if (!a.action.empty() && a.action[0] == '_') {
                actionId = NamedCommandLookup(a.action.c_str());
            } else {
                actionId = std::atoi(a.action.c_str());
            }
            if (actionId <= 0) break;
            // Inactive-edge gate: REAPER actions fire only on the press
            // edge (firing=true) by default. Behavior::Hold sends
            // firing=true on BOTH press and release (engine forces it),
            // so Hold-bindings naturally fire the action twice per press
            // — perfect for "active while held" toggles. For non-Hold
            // bindings (Momentary / Toggle) the user opts in to the
            // double-fire via ActionStep::fireOnInactive.
            //
            // The earlier "auto-fire on toggle" heuristic (Frank
            // 2026-05-07) overrode the user's Behavior choice and
            // collapsed Momentary toggles to a no-op (ON+OFF on a
            // single press) — Frank 2026-05-08, removed.
            if (!firing && !a.fireOnInactive) break;
            auto it = g_builtins.find("__reaper_action__");
            if (it != g_builtins.end() && it->second.run) {
                it->second.run(true, pressed, actionId);
            }
            break;
        }
        case ActionType::Keyboard:
            // Phase D — needs platform key-event injection.
            break;
        case ActionType::Builtin: {
            auto it = g_builtins.find(a.action);
            if (it != g_builtins.end() && it->second.run) {
                it->second.run(firing, pressed, a.param);
            } else if (firing) {
                // Diagnostic: an action name references a builtin that
                // isn't registered. Usually means a stale dead-builtin
                // reference (e.g. quick_select_3 in a not-fully-migrated
                // config) that dispatch silently no-op'd. Logging once
                // per firing gives a paper trail for "press did nothing"
                // bug reports.
                if (FILE* lg = std::fopen("/tmp/rea_sixty.log", "a")) {
                    std::fprintf(lg,
                        "[dispatch] unknown builtin action='%s' (firing press); "
                        "config may need re-migration or rebind\n",
                        a.action.c_str());
                    std::fclose(lg);
                }
            }
            break;
        }
        case ActionType::Midi: {
            // Fire on the press edge only. Note Off as a message type
            // is a deliberate user choice (separate option in the
            // editor); we don't auto-pair a Note Off with a preceding
            // Note On. For paired note-on/note-off behaviour the user
            // can build a multi-step chain (Note On step + Note Off
            // step with a wait_ms) in the editor.
            if (!firing) break;
            dispatchMidi_(a);
            break;
        }
    }
}

// Pending multi-step chain. Held in g_pendingChains until each step's
// `fireAt` elapses on the main-thread timer drain. Single-step chains
// short-circuit in runSlot_ and never sit on the queue.
struct PendingChain {
    ActionSlot                            snapshot;
    int                                   nextStepIdx;
    bool                                  firing;
    bool                                  pressed;
    std::chrono::steady_clock::time_point fireAt;
};

std::mutex                 g_pendingMutex;
std::vector<PendingChain>  g_pendingChains;

// Run a slot's chain. Single-step slots fire synchronously (preserving
// the legacy zero-latency path); multi-step chains run step 0 inline
// and queue the rest for tickPending_ to drain on the main thread.
void runSlot_(const ActionSlot& slot, bool firing, bool pressed)
{
    const int n = stepCount(slot);
    if (n <= 1) {
        runStep_(static_cast<const ActionStep&>(slot), firing, pressed);
        return;
    }
    runStep_(stepAt(slot, 0), firing, pressed);
    // Queue the remaining steps for the main-thread tick. Both the firing
    // (press) and inactive (release) edges schedule the chain — runStep_
    // gates per-step what actually happens on inactive (REAPER toggles
    // re-fire automatically; one-shot REAPER actions opt-in via
    // ActionStep::fireOnInactive; builtins follow their own firing
    // semantics). Earlier this short-circuited on !firing, which left
    // multi-step REAPER bindings with their second-step action unreached
    // on release — Frank 2026-05-07.
    const int wait0 = slot.wait_ms < 0 ? 0 : slot.wait_ms;
    PendingChain pc;
    pc.snapshot    = slot;
    pc.nextStepIdx = 1;
    pc.firing      = firing;
    pc.pressed     = pressed;
    pc.fireAt      = std::chrono::steady_clock::now()
                   + std::chrono::milliseconds(wait0);
    std::lock_guard<std::mutex> lk(g_pendingMutex);
    g_pendingChains.push_back(std::move(pc));
}

} // namespace

void tickPending()
{
    const auto now = std::chrono::steady_clock::now();
    std::vector<PendingChain> ready;
    {
        std::lock_guard<std::mutex> lk(g_pendingMutex);
        for (auto it = g_pendingChains.begin(); it != g_pendingChains.end(); ) {
            if (it->fireAt <= now) {
                ready.push_back(std::move(*it));
                it = g_pendingChains.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& pc : ready) {
        const int n = stepCount(pc.snapshot);
        if (pc.nextStepIdx < 0 || pc.nextStepIdx >= n) continue;
        const ActionStep& st = stepAt(pc.snapshot, pc.nextStepIdx);
        runStep_(st, pc.firing, pc.pressed);
        const int next = pc.nextStepIdx + 1;
        if (next < n) {
            const int wait = st.wait_ms < 0 ? 0 : st.wait_ms;
            pc.nextStepIdx = next;
            pc.fireAt      = std::chrono::steady_clock::now()
                           + std::chrono::milliseconds(wait);
            std::lock_guard<std::mutex> lk(g_pendingMutex);
            g_pendingChains.push_back(std::move(pc));
        }
    }
}

void effectiveLedActive(const Binding& bd, const ActionSlot& slot,
                        uint8_t (&rgb)[3], Brightness& bri)
{
    if (slot.led.hasActive) {
        rgb[0] = slot.led.color[0];
        rgb[1] = slot.led.color[1];
        rgb[2] = slot.led.color[2];
        bri    = slot.led.brightness;
    } else {
        rgb[0] = bd.color[0];
        rgb[1] = bd.color[1];
        rgb[2] = bd.color[2];
        bri    = bd.brightness;
    }
}

void effectiveLedInactive(const Binding& bd, const ActionSlot& slot,
                          uint8_t (&rgb)[3], Brightness& bri)
{
    if (slot.led.hasInactive) {
        rgb[0] = slot.led.inactiveColor[0];
        rgb[1] = slot.led.inactiveColor[1];
        rgb[2] = slot.led.inactiveColor[2];
        bri    = slot.led.inactiveBrightness;
    } else {
        rgb[0] = bd.inactiveColor[0];
        rgb[1] = bd.inactiveColor[1];
        rgb[2] = bd.inactiveColor[2];
        bri    = bd.inactiveBrightness;
    }
}

void setModifierHeld(Modifier m, bool held)
{
    switch (m) {
        case Modifier::Shift: g_modShiftHeld.store(held); break;
        case Modifier::Cmd:   g_modCmdHeld.store(held);   break;
        case Modifier::Ctrl:  g_modCtrlHeld.store(held);  break;
        case Modifier::Plain: break;  // no state to set
    }
}

bool modifierHeld(Modifier m)
{
    switch (m) {
        case Modifier::Shift: return g_modShiftHeld.load();
        case Modifier::Cmd:   return g_modCmdHeld.load();
        case Modifier::Ctrl:  return g_modCtrlHeld.load();
        case Modifier::Plain: return false;
    }
    return false;
}

Modifier currentModifierSnapshot()
{
    // Precedence Ctrl > Cmd > Shift > Plain. Most-specific-modifier-wins
    // matches typical keyboard-shortcut conventions; the editor will let
    // the user route Ctrl+Shift+button via the Ctrl slot only.
    if (g_modCtrlHeld.load())  return Modifier::Ctrl;
    if (g_modCmdHeld.load())   return Modifier::Cmd;
    if (g_modShiftHeld.load()) return Modifier::Shift;
    return Modifier::Plain;
}

bool dispatch(ButtonId id, bool pressed)
{
    if (id == ButtonId::None) return false;

    Binding bd;
    int layer;
    {
        std::lock_guard<std::mutex> lk(g_cfgMutex);
        layer = g_cfg.activeLayer;
        if (layer < 0 || layer > 2) layer = 0;
        auto it = g_cfg.layers[layer].bindings.find(id);
        // Release-edge stuck-key guard: when the active layer changes
        // mid-hold (mixer-visibility auto-switch, manual layer flip,
        // SSL Strip Mode toggle, etc.), the release would otherwise
        // fire against the NEW layer's binding for this button — or
        // return early when the new layer has none. Both leave the
        // press-time binding (e.g. mod_shift) without its release,
        // and a Momentary modifier stays stuck "on" forever.
        //
        // Rule: on release, ALWAYS prefer the layer that recorded the
        // press for this button. g_pressStart / g_longPressStart keys
        // by (press-time-layer, button-id), so finding any entry
        // pinpoints which layer originally handled this press.
        // (Frank 2026-05-12: stuck Shift; first fix only covered the
        // "active layer has no binding" subcase, not the
        // "different binding on active layer" subcase that's actually
        // common with rebind-on-layer setups.)
        if (!pressed) {
            for (int L = 0; L < 3; ++L) {
                const uint32_t k = pressKey(L, id);
                if (g_pressStart.find(k)     == g_pressStart.end()
                 && g_longPressStart.find(k) == g_longPressStart.end()) {
                    continue;
                }
                auto altIt = g_cfg.layers[L].bindings.find(id);
                if (altIt != g_cfg.layers[L].bindings.end()) {
                    it    = altIt;
                    layer = L;
                    break;
                }
            }
        }
        if (it == g_cfg.layers[layer].bindings.end()) return false;
        bd = it->second;   // copy under lock so the rest runs lock-free
    }

    // Long-press support (Momentary primary only). Defer the primary-
    // action fire until release-edge so we can choose between primary
    // and long-press based on the held duration. Modifier snapshot is
    // taken at PRESS time and re-used on release / threshold so the
    // press commits to a slot even if the user releases the modifier
    // mid-hold.
    const bool longPressArmed =
        bd.hasLongPress && bd.behavior == Behavior::Momentary;
    if (longPressArmed) {
        const uint32_t k = pressKey(layer, id);
        if (pressed) {
            g_pressStart[k] = { std::chrono::steady_clock::now(),
                                currentModifierSnapshot() };
        } else {
            auto it = g_pressStart.find(k);
            if (it != g_pressStart.end()) {
                const auto held = std::chrono::steady_clock::now() - it->second.start;
                const int  m    = static_cast<int>(it->second.mod);
                g_pressStart.erase(it);
                if (held >= kLongPressThreshold) {
                    runSlot_(bd.longPress[m],
                               /*firing*/ true, /*pressed*/ false);
                    // Tag lastFired with the long-press marker (high bit)
                    // so the LED resolver reads from longPress[m] rather
                    // than shortPress[m]. Without this, a long-press
                    // action that toggles state ON (e.g. send_this) would
                    // render the LED via shortPress's LedOverride and
                    // ignore the user's per-long-press colour choice.
                    if (bd.longPress[m].type != ActionType::Noop) {
                        const auto idx = static_cast<size_t>(id);
                        if (idx < g_lastFiredMod.size()) {
                            g_lastFiredMod[idx].store(
                                static_cast<uint8_t>(m | 0x80),
                                std::memory_order_relaxed);
                        }
                    }
                } else {
                    runSlot_(bd.shortPress[m],
                               /*firing*/ true, /*pressed*/ false);
                    if (bd.shortPress[m].type != ActionType::Noop) {
                        const auto idx = static_cast<size_t>(id);
                        if (idx < g_lastFiredMod.size()) {
                            g_lastFiredMod[idx].store(
                                static_cast<uint8_t>(m),
                                std::memory_order_relaxed);
                        }
                    }
                }
            }
        }
        return true;
    }

    // Standard (no long-press) path — fire per behavior. Modifier slot
    // is selected at the press edge and re-used for the release edge so
    // a binding's release matches its press even if the user dropped
    // the modifier between. All three behaviours honour the modifier
    // now (Toggle and Hold used to fall back to Plain — Frank
    // 2026-05-06: Shift+Press should fire the Shift slot regardless
    // of the binding's behaviour).
    int slotIdx = static_cast<int>(Modifier::Plain);
    if (bd.behavior == Behavior::Momentary || bd.behavior == Behavior::Hold) {
        const uint32_t k = pressKey(layer, id);
        if (pressed) {
            g_pressStart[k] = { std::chrono::steady_clock::now(),
                                currentModifierSnapshot() };
            slotIdx = static_cast<int>(g_pressStart[k].mod);
        } else {
            auto it = g_pressStart.find(k);
            if (it != g_pressStart.end()) {
                slotIdx = static_cast<int>(it->second.mod);
                g_pressStart.erase(it);
            }
        }
    } else {  // Behavior::Toggle — fires only on press-edge, uses live mod.
        if (pressed) slotIdx = static_cast<int>(currentModifierSnapshot());
    }
    // Noop-slot fallback: a Hold-bound modifier button (mod_shift on Fine)
    // sees its own held state in currentModifierSnapshot(), selects the
    // (empty) Shift slot, and the release handler never fires — leaving
    // the modifier stuck permanently. Fall back to Plain so both press
    // and release edges always reach the handler.
    if (bd.shortPress[slotIdx].type == ActionType::Noop
        && slotIdx != static_cast<int>(Modifier::Plain)
        && bd.shortPress[static_cast<int>(Modifier::Plain)].type != ActionType::Noop)
    {
        slotIdx = static_cast<int>(Modifier::Plain);
    }
    bool firing;
    switch (bd.behavior) {
        case Behavior::Momentary: firing = pressed; break;
        case Behavior::Toggle:    firing = pressed; break;
        case Behavior::Hold:      firing = true;    break;
    }
    const auto& slot = bd.shortPress[slotIdx];
    if (firing && slot.type != ActionType::Noop) {
        // Remember which modifier slot this button last actually fired
        // — main.cpp's LED pusher reads this so the active-state
        // colour matches the slot whose action is engaged. Without it,
        // a Shift+press fired the Shift slot's action but the LED kept
        // showing the Plain slot's active colour after release.
        const auto idx = static_cast<size_t>(id);
        if (idx < g_lastFiredMod.size()) {
            g_lastFiredMod[idx].store(static_cast<uint8_t>(slotIdx),
                                       std::memory_order_relaxed);
        }
    }
    runSlot_(slot, firing, pressed);

    // Long-press additive fire for Toggle / Hold. Primary already fired in
    // the standard path above; this fires the long-press slot on release
    // if held >= threshold. Momentary's defer-and-choose semantics is
    // handled in the longPressArmed branch up top and never reaches here.
    if (bd.hasLongPress && bd.behavior != Behavior::Momentary) {
        const uint32_t k = pressKey(layer, id);
        if (pressed) {
            g_longPressStart[k] = { std::chrono::steady_clock::now(),
                                    static_cast<Modifier>(slotIdx) };
        } else {
            auto lit = g_longPressStart.find(k);
            if (lit != g_longPressStart.end()) {
                const auto held = std::chrono::steady_clock::now() - lit->second.start;
                const int  m    = static_cast<int>(lit->second.mod);
                g_longPressStart.erase(lit);
                if (held >= kLongPressThreshold
                    && bd.longPress[m].type != ActionType::Noop)
                {
                    runSlot_(bd.longPress[m], /*firing*/ true, /*pressed*/ false);
                    // Tag lastFired with the long-press marker so the LED
                    // resolver picks up the long-press slot's LedOverride.
                    const auto idx = static_cast<size_t>(id);
                    if (idx < g_lastFiredMod.size()) {
                        g_lastFiredMod[idx].store(
                            static_cast<uint8_t>(m | 0x80),
                            std::memory_order_relaxed);
                    }
                }
            }
        }
    }

    return true;
}

int getActiveLayer()
{
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    int n = g_cfg.activeLayer;
    if (n < 0 || n > 2) n = 0;
    return n;
}

namespace {
// Factories all the mutators below funnel through. Caller must already
// hold g_cfgMutex.
void persistLocked_()
{
    ensureConfigDir_();
    writeFile_(configPath_(), serialize(g_cfg));
    g_bindingsGen.fetch_add(1, std::memory_order_relaxed);
}
}

Binding getBinding(int layer, ButtonId id)
{
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    if (layer < 0 || layer > 2) return {};
    auto it = g_cfg.layers[layer].bindings.find(id);
    if (it == g_cfg.layers[layer].bindings.end()) return {};
    return it->second;
}

bool hasBinding(int layer, ButtonId id)
{
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    if (layer < 0 || layer > 2 || id == ButtonId::None) return false;
    return g_cfg.layers[layer].bindings.find(id)
        != g_cfg.layers[layer].bindings.end();
}

void setBinding(int layer, ButtonId id, const Binding& bd)
{
    if (layer < 0 || layer > 2 || id == ButtonId::None) return;
    // No sanitize-on-write: the editor commits on every keystroke /
    // radio click, including the transient state right after the user
    // flips a slot's type to Builtin but before they pick an action.
    // Coercing Builtin-with-empty-action back to Noop here would
    // collapse the editor's "Native Action" view on the next frame
    // (radio button drops off, combo disappears). Dispatch already
    // no-ops empty-action Builtin slots (g_builtins.find("") returns
    // end()), so leaving them as-is is safe; the v5→v6 load-time
    // migration handles any legacy corrupt entries on disk.
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    g_cfg.layers[layer].bindings[id] = bd;
    persistLocked_();
}

void clearBinding(int layer, ButtonId id)
{
    if (layer < 0 || layer > 2 || id == ButtonId::None) return;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    g_cfg.layers[layer].bindings.erase(id);
    persistLocked_();
}

bool dispatchEncoder(ButtonId id, int stepDelta)
{
    if (id == ButtonId::None || stepDelta == 0) return false;
    Binding bd;
    int layer;
    {
        std::lock_guard<std::mutex> lk(g_cfgMutex);
        layer = g_cfg.activeLayer;
        if (layer < 0 || layer > 2) layer = 0;
        auto it = g_cfg.layers[layer].bindings.find(id);
        if (it == g_cfg.layers[layer].bindings.end()) return false;
        bd = it->second;
    }
    const int slotIdx = static_cast<int>(currentModifierSnapshot());
    if (slotIdx < 0 || slotIdx >= kModifierCount) return false;
    const ActionSlot& slot = bd.shortPress[slotIdx];
    if (slot.type == ActionType::Noop || slot.action.empty()) return false;
    if (slot.type != ActionType::Builtin) {
        // REAPER actions / keyboard / MIDI: fire once per detent. Not
        // step-aware. Acceptable trade-off — for delta-aware behaviour
        // the user picks an encoder-aware builtin.
        runSlot_(slot, /*firing*/ true, /*pressed*/ false);
        return true;
    }
    auto bit = g_builtins.find(slot.action);
    if (bit == g_builtins.end() || !bit->second.run) return false;
    // Delta-aware builtins read `param` as the signed step. Trigger-only
    // builtins (toggles etc.) ignore it and fire once per detent.
    bit->second.run(/*firing*/ true, /*pressed*/ false, /*param*/ stepDelta);
    return true;
}

static bool userQuickSlotInRange_(int layer, int quick, int subBank, int slot)
{
    return layer    >= 0 && layer    < 3
        && quick    >= 0 && quick    < kQuicksPerLayer
        && subBank  >= 0 && subBank  < kSubBanksPerQuick
        && slot     >= 0 && slot     < kSlotsPerSubBank;
}

Binding getUserQuickSlot(int layer, int quick, int subBank, int slot)
{
    if (!userQuickSlotInRange_(layer, quick, subBank, slot)) return {};
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    return g_cfg.userQuicks[layer].quicks[quick]
              .subBanks[subBank].slots[slot];
}

void setUserQuickSlot(int layer, int quick, int subBank, int slot,
                      const Binding& bd)
{
    if (!userQuickSlotInRange_(layer, quick, subBank, slot)) return;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    g_cfg.userQuicks[layer].quicks[quick]
        .subBanks[subBank].slots[slot] = bd;
    persistLocked_();
}

static bool subBankLedInRange_(int layer, int quick, int subBank)
{
    return layer    >= 0 && layer    < 3
        && quick    >= 0 && quick    < kQuicksPerLayer
        && subBank  >= 0 && subBank  < kSubBanksPerQuick;
}

SubBankLed getSubBankLed(int layer, int quick, int subBank)
{
    if (!subBankLedInRange_(layer, quick, subBank)) return {};
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    return g_cfg.userQuicks[layer].quicks[quick].subBankLeds[subBank];
}

void setSubBankLed(int layer, int quick, int subBank,
                   const SubBankLed& app)
{
    if (!subBankLedInRange_(layer, quick, subBank)) return;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    g_cfg.userQuicks[layer].quicks[quick].subBankLeds[subBank] = app;
    persistLocked_();
}

// ---- Soft-Key Bank presets -----------------------------------------------

int bankPresetCount()
{
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    return static_cast<int>(g_cfg.bankPresets.size());
}

SoftKeyBankPreset bankPresetAt(int idx)
{
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    if (idx < 0 || idx >= static_cast<int>(g_cfg.bankPresets.size()))
        return {};
    return g_cfg.bankPresets[idx];
}

int findBankPreset(const std::string& name)
{
    if (name.empty()) return -1;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    for (int i = 0; i < static_cast<int>(g_cfg.bankPresets.size()); ++i) {
        if (g_cfg.bankPresets[i].name == name) return i;
    }
    return -1;
}

bool saveBankPreset(const std::string& name,
                    int layer, int quick, int subBank)
{
    if (name.empty()) return false;
    if (!userQuickSlotInRange_(layer, quick, subBank, 0)) return false;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    SoftKeyBankPreset p;
    p.name = name;
    for (int s = 0; s < kSlotsPerSubBank; ++s) {
        p.slots[s] = g_cfg.userQuicks[layer].quicks[quick]
                        .subBanks[subBank].slots[s];
    }
    int existing = -1;
    for (int i = 0; i < static_cast<int>(g_cfg.bankPresets.size()); ++i) {
        if (g_cfg.bankPresets[i].name == name) { existing = i; break; }
    }
    if (existing >= 0) g_cfg.bankPresets[existing] = std::move(p);
    else               g_cfg.bankPresets.push_back(std::move(p));
    persistLocked_();
    return true;
}

bool renameBankPreset(int idx, const std::string& newName)
{
    if (newName.empty()) return false;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    if (idx < 0 || idx >= static_cast<int>(g_cfg.bankPresets.size()))
        return false;
    for (int i = 0; i < static_cast<int>(g_cfg.bankPresets.size()); ++i) {
        if (i == idx) continue;
        if (g_cfg.bankPresets[i].name == newName) return false;   // duplicate
    }
    g_cfg.bankPresets[idx].name = newName;
    persistLocked_();
    return true;
}

bool deleteBankPreset(int idx)
{
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    if (idx < 0 || idx >= static_cast<int>(g_cfg.bankPresets.size()))
        return false;
    g_cfg.bankPresets.erase(g_cfg.bankPresets.begin() + idx);
    persistLocked_();
    return true;
}

bool recallBankPreset(int idx, int layer, int quick, int subBank)
{
    if (!userQuickSlotInRange_(layer, quick, subBank, 0)) return false;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    if (idx < 0 || idx >= static_cast<int>(g_cfg.bankPresets.size()))
        return false;
    const SoftKeyBankPreset& p = g_cfg.bankPresets[idx];
    for (int s = 0; s < kSlotsPerSubBank; ++s) {
        g_cfg.userQuicks[layer].quicks[quick]
            .subBanks[subBank].slots[s] = p.slots[s];
    }
    persistLocked_();
    return true;
}

bool dispatchUserQuickSlot(int layer, int quick, int subBank,
                           int slot, bool pressed)
{
    if (!userQuickSlotInRange_(layer, quick, subBank, slot)) return false;

    Binding bd;
    {
        std::lock_guard<std::mutex> lk(g_cfgMutex);
        bd = g_cfg.userQuicks[layer].quicks[quick]
                .subBanks[subBank].slots[slot];
    }

    // Press-key namespace uses a synthetic "layer" derived from
    // (layer, quick, subBank) so the keys never collide with the real
    // (layer 0..2, id) press-timer keyspace.
    const int  syntheticLayer = 100
                              + layer * (kQuicksPerLayer * kSubBanksPerQuick)
                              + quick * kSubBanksPerQuick
                              + subBank;
    const ButtonId pseudoId   = static_cast<ButtonId>(0x4000 + slot);
    const uint32_t k = pressKey(syntheticLayer, pseudoId);

    const bool longPressArmed =
        bd.hasLongPress && bd.behavior == Behavior::Momentary;
    const auto& shortP = bd.shortPress;
    const auto& longP  = bd.longPress;

    if (longPressArmed) {
        if (pressed) {
            g_pressStart[k] = { std::chrono::steady_clock::now(),
                                currentModifierSnapshot() };
        } else {
            auto it = g_pressStart.find(k);
            if (it != g_pressStart.end()) {
                const auto held = std::chrono::steady_clock::now() - it->second.start;
                const int  m    = static_cast<int>(it->second.mod);
                g_pressStart.erase(it);
                if (held >= kLongPressThreshold) {
                    runSlot_(longP[m], /*firing*/ true, /*pressed*/ false);
                } else {
                    runSlot_(shortP[m], /*firing*/ true, /*pressed*/ false);
                }
            }
        }
        return true;
    }

    int slotMod = static_cast<int>(Modifier::Plain);
    if (bd.behavior == Behavior::Momentary) {
        if (pressed) {
            g_pressStart[k] = { std::chrono::steady_clock::now(),
                                currentModifierSnapshot() };
            slotMod = static_cast<int>(g_pressStart[k].mod);
        } else {
            auto it = g_pressStart.find(k);
            if (it != g_pressStart.end()) {
                slotMod = static_cast<int>(it->second.mod);
                g_pressStart.erase(it);
            }
        }
    }
    bool firing;
    switch (bd.behavior) {
        case Behavior::Momentary: firing = pressed; break;
        case Behavior::Toggle:    firing = pressed; break;
        case Behavior::Hold:      firing = true;    break;
    }
    runSlot_(shortP[slotMod], firing, pressed);
    return shortP[slotMod].type != ActionType::Noop
        || !shortP[slotMod].action.empty();
}

void setLayerName(int layer, const std::string& name)
{
    if (layer < 0 || layer > 2) return;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    g_cfg.layers[layer].name = name;
    persistLocked_();
}

void setLayerVpotDefaultMode(int layer, const std::string& mode)
{
    if (layer < 0 || layer > 2) return;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    g_cfg.layers[layer].vpotDefaultMode = mode;
    persistLocked_();
}

void setLayerAutoMixer(int layer, bool flag)
{
    // Layer 0 (Layer 1) doesn't carry the flag per resolved Q5.
    if (layer < 1 || layer > 2) return;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    g_cfg.layers[layer].autoWhenMixerVisible = flag;
    if (flag) {
        // Architectural invariant: at most one layer flagged.
        const int other = (layer == 1) ? 2 : 1;
        g_cfg.layers[other].autoWhenMixerVisible = false;
    }
    persistLocked_();
}

void resetLayerToDefaults(int layer)
{
    if (layer < 0 || layer > 2) return;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    Config tmp;
    seedFactoryDefaults_(tmp);
    g_cfg.layers[layer] = std::move(tmp.layers[layer]);
    persistLocked_();
}

std::vector<std::string> builtinNames()
{
    // No lock — g_builtins is populated once at startup before the USB
    // thread starts and never mutated thereafter. Safe to read.
    std::vector<std::string> out;
    out.reserve(g_builtins.size());
    for (auto& kv : g_builtins) {
        if (kv.first.rfind("__", 0) == 0) continue;  // skip internal sentinels
        out.push_back(kv.first);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string builtinDisplayName(const std::string& name)
{
    auto it = g_builtins.find(name);
    if (it == g_builtins.end() || it->second.displayName.empty()) return name;
    return it->second.displayName;
}

bool builtinUsesParam(const std::string& name)
{
    auto it = g_builtins.find(name);
    if (it == g_builtins.end()) return false;
    return it->second.usesParam;
}

bool builtinStateOf(const std::string& name, int param)
{
    auto it = g_builtins.find(name);
    if (it == g_builtins.end() || !it->second.stateOf) return false;
    return it->second.stateOf(param);
}

bool builtinHasState(const std::string& name)
{
    auto it = g_builtins.find(name);
    if (it == g_builtins.end()) return false;
    return static_cast<bool>(it->second.stateOf);
}

void setActiveLayer(int layer)
{
    if (layer < 0 || layer > 2) return;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    if (g_cfg.activeLayer == layer) return;
    g_cfg.activeLayer = layer;
    // Manual switch wins over a pending mixer-auto save; otherwise
    // closing the mixer would override the user's deliberate choice.
    g_savedLayer = -1;
    ensureConfigDir_();
    writeFile_(configPath_(), serialize(g_cfg));
}

void onMixerVisibilityChanged(bool visible)
{
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    if (visible) {
        // Walk Layers 2/3 (index 1, 2). Layer 1 doesn't carry the flag
        // per resolved Q5. First match wins; UI invariant (Phase C) is
        // "at most one layer flagged".
        for (int i = 1; i <= 2; ++i) {
            if (g_cfg.layers[i].autoWhenMixerVisible) {
                if (g_savedLayer < 0) g_savedLayer = g_cfg.activeLayer;
                g_cfg.activeLayer = i;
                return;
            }
        }
    } else {
        if (g_savedLayer >= 0) {
            g_cfg.activeLayer = g_savedLayer;
            g_savedLayer = -1;
        }
    }
}

uint64_t generation()
{
    return g_bindingsGen.load(std::memory_order_relaxed);
}

Modifier lastFiredModifier(ButtonId id)
{
    const auto idx = static_cast<size_t>(id);
    if (idx >= g_lastFiredMod.size()) return Modifier::Plain;
    const auto v = g_lastFiredMod[idx].load(std::memory_order_relaxed);
    const uint8_t mod = v & 0x7F;  // strip long-press bit
    if (mod >= kModifierCount) return Modifier::Plain;
    return static_cast<Modifier>(mod);
}

bool lastFiredWasLongPress(ButtonId id)
{
    const auto idx = static_cast<size_t>(id);
    if (idx >= g_lastFiredMod.size()) return false;
    return (g_lastFiredMod[idx].load(std::memory_order_relaxed) & 0x80) != 0;
}

} // namespace uf8::bindings
