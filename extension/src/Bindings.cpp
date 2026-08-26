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
#include "LogPath.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <initializer_list>
#include <sstream>
#include <string>
#include <tuple>
#include <sys/stat.h>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#endif

#include "reaper_plugin_functions.h"

#include "WDL/jsonparse.h"

#include "KeyMacro.h"

namespace uf8::bindings {

namespace {

// Crash-isolation breadcrumb appended to %TEMP%\rea_sixty_init.log
// (Win) or /tmp/rea_sixty_init.log (POSIX). Defined here so every
// helper in the file can poke at it; kept short on purpose.
inline void crumb_(const char* msg)
{
#ifdef _WIN32
    char tmp[260] = {0};
    char path[260] = {0};
    if (GetTempPathA(260, tmp)) {
        snprintf(path, sizeof(path), "%srea_sixty_init.log", tmp);
    } else {
        std::strcpy(path, "C:\\Windows\\Temp\\rea_sixty_init.log");
    }
    FILE* f = std::fopen(path, "a");
#else
    FILE* f = std::fopen(uf8::logPath("rea_sixty_init.log").c_str(), "a");
#endif
    if (f) { std::fprintf(f, "  bindings:%s\n", msg); std::fclose(f); }
}

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
    { ButtonId::Uf8Select,     "uf8_select"     },
    { ButtonId::ChannelEncoder, "channel_encoder" },
    { ButtonId::Uc1Encoder1,      "uc1_encoder_1"      },
    { ButtonId::Uc1Encoder2,      "uc1_encoder_2"      },
    { ButtonId::Uc1Encoder2Push,  "uc1_encoder_2_push" },
    { ButtonId::Uc1Magnifier,     "uc1_magnifier"      },
    { ButtonId::Foot1,            "foot_1"             },
    { ButtonId::Foot2,            "foot_2"             },
    { ButtonId::Uc1Btn360,        "uc1_btn_360"        },
    // UF1 buttons (Phase 1).
    { ButtonId::Uf1VpotAbovePush, "uf1_vpot_above_push" },
    { ButtonId::Uf1Vpot1Push,     "uf1_vpot_1_push"     },
    { ButtonId::Uf1Vpot2Push,     "uf1_vpot_2_push"     },
    { ButtonId::Uf1Vpot3Push,     "uf1_vpot_3_push"     },
    { ButtonId::Uf1Vpot4Push,     "uf1_vpot_4_push"     },
    { ButtonId::Uf1ChannelPush,   "uf1_channel_push"    },
    { ButtonId::Uf1ChannelSoftKey,"uf1_channel_softkey" },
    { ButtonId::Uf1DisplaySoft1,  "uf1_display_soft_1"  },
    { ButtonId::Uf1DisplaySoft2,  "uf1_display_soft_2"  },
    { ButtonId::Uf1DisplaySoft3,  "uf1_display_soft_3"  },
    { ButtonId::Uf1DisplaySoft4,  "uf1_display_soft_4"  },
    { ButtonId::Uf1Solo,          "uf1_solo"            },
    { ButtonId::Uf1Cut,           "uf1_cut"             },
    { ButtonId::Uf1Sel,           "uf1_sel"             },
    { ButtonId::Uf1BankLeft,      "uf1_bank_left"       },
    { ButtonId::Uf1FiveToEight,   "uf1_5_to_8"          },
    { ButtonId::Uf1BankRight,     "uf1_bank_right"      },
    { ButtonId::Uf1ArrowLeft,     "uf1_arrow_left"      },
    { ButtonId::Uf1Btn360,        "uf1_btn_360"         },
    { ButtonId::Uf1ArrowRight,    "uf1_arrow_right"     },
    { ButtonId::Uf1Scrub,         "uf1_scrub"           },
    { ButtonId::Uf1NavUp,         "uf1_nav_up"          },
    { ButtonId::Uf1NavLeft,       "uf1_nav_left"        },
    { ButtonId::Uf1NavCentre,     "uf1_nav_centre"      },
    { ButtonId::Uf1NavRight,      "uf1_nav_right"       },
    { ButtonId::Uf1NavDown,       "uf1_nav_down"        },
    // Per-mode nav cross — 5 keys x 6 modes. Order MUST match the enum block
    // in Bindings.h (mode-minor: Playhead, Scrub, Items, Envelope, Razor, Fades).
    // Without a name a binding does not serialise and the editor collapses,
    // so all 25 are listed even though they are never typed by hand.
    { ButtonId::Uf1NavUpPlayhead, "uf1_nav_up_playhead" },
    { ButtonId::Uf1NavUpScrub, "uf1_nav_up_scrub" },
    { ButtonId::Uf1NavUpItems, "uf1_nav_up_items" },
    { ButtonId::Uf1NavUpEnvelope, "uf1_nav_up_envelope" },
    { ButtonId::Uf1NavUpRazor, "uf1_nav_up_razor" },
    { ButtonId::Uf1NavUpFades, "uf1_nav_up_fades" },
    { ButtonId::Uf1NavLeftPlayhead, "uf1_nav_left_playhead" },
    { ButtonId::Uf1NavLeftScrub, "uf1_nav_left_scrub" },
    { ButtonId::Uf1NavLeftItems, "uf1_nav_left_items" },
    { ButtonId::Uf1NavLeftEnvelope, "uf1_nav_left_envelope" },
    { ButtonId::Uf1NavLeftRazor, "uf1_nav_left_razor" },
    { ButtonId::Uf1NavLeftFades, "uf1_nav_left_fades" },
    { ButtonId::Uf1NavCentrePlayhead, "uf1_nav_centre_playhead" },
    { ButtonId::Uf1NavCentreScrub, "uf1_nav_centre_scrub" },
    { ButtonId::Uf1NavCentreItems, "uf1_nav_centre_items" },
    { ButtonId::Uf1NavCentreEnvelope, "uf1_nav_centre_envelope" },
    { ButtonId::Uf1NavCentreRazor, "uf1_nav_centre_razor" },
    { ButtonId::Uf1NavCentreFades, "uf1_nav_centre_fades" },
    { ButtonId::Uf1NavRightPlayhead, "uf1_nav_right_playhead" },
    { ButtonId::Uf1NavRightScrub, "uf1_nav_right_scrub" },
    { ButtonId::Uf1NavRightItems, "uf1_nav_right_items" },
    { ButtonId::Uf1NavRightEnvelope, "uf1_nav_right_envelope" },
    { ButtonId::Uf1NavRightRazor, "uf1_nav_right_razor" },
    { ButtonId::Uf1NavRightFades, "uf1_nav_right_fades" },
    { ButtonId::Uf1NavDownPlayhead, "uf1_nav_down_playhead" },
    { ButtonId::Uf1NavDownScrub, "uf1_nav_down_scrub" },
    { ButtonId::Uf1NavDownItems, "uf1_nav_down_items" },
    { ButtonId::Uf1NavDownEnvelope, "uf1_nav_down_envelope" },
    { ButtonId::Uf1NavDownRazor, "uf1_nav_down_razor" },
    { ButtonId::Uf1NavDownFades, "uf1_nav_down_fades" },
    { ButtonId::Uf1SecLeft,       "uf1_sec_left"        },
    { ButtonId::Uf1SecRight,      "uf1_sec_right"       },
    { ButtonId::Uf1Cycle,         "uf1_cycle"           },
    { ButtonId::Uf1Click,         "uf1_click"           },
    { ButtonId::Uf1SecKey1,       "uf1_sec_key_1"       },
    { ButtonId::Uf1SecKey2,       "uf1_sec_key_2"       },
    { ButtonId::Uf1Shift,         "uf1_shift"           },
    { ButtonId::Uf1Flip,          "uf1_flip"            },
    { ButtonId::Uf1Master,        "uf1_master"          },
    { ButtonId::Uf1Rwd,           "uf1_rwd"             },
    { ButtonId::Uf1Ffw,           "uf1_ffw"             },
    { ButtonId::Uf1Stop,          "uf1_stop"            },
    { ButtonId::Uf1Play,          "uf1_play"            },
    { ButtonId::Uf1Rec,           "uf1_rec"             },
    { ButtonId::Uf1Jog,           "uf1_jog"             },
};

} // namespace

bool splitPerModeNavId(ButtonId id, ButtonId* baseOut, int* modeOut)
{
    struct Grp { ButtonId first; ButtonId base; };
    static const Grp kGrp[] = {
        { ButtonId::Uf1NavUpPlayhead,     ButtonId::Uf1NavUp     },
        { ButtonId::Uf1NavLeftPlayhead,   ButtonId::Uf1NavLeft   },
        { ButtonId::Uf1NavCentrePlayhead, ButtonId::Uf1NavCentre },
        { ButtonId::Uf1NavRightPlayhead,  ButtonId::Uf1NavRight  },
        { ButtonId::Uf1NavDownPlayhead,   ButtonId::Uf1NavDown   },
    };
    for (const auto& g : kGrp) {
        const int lo = static_cast<int>(g.first);
        const int v  = static_cast<int>(id);
        if (v >= lo && v < lo + kUf1JogModeCountForNav) {
            if (baseOut) *baseOut = g.base;
            if (modeOut) *modeOut = v - lo;
            return true;
        }
    }
    return false;
}

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
        case 0x00: return ButtonId::Foot1;
        case 0x01: return ButtonId::Foot2;
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

ButtonId fromUf1DeviceId(uint8_t id)
{
    switch (id) {
        // V-Pot + channel-encoder pushes 0x08..0x0D.
        case 0x08: return ButtonId::Uf1VpotAbovePush;
        case 0x09: return ButtonId::Uf1Vpot1Push;
        case 0x0A: return ButtonId::Uf1Vpot2Push;
        case 0x0B: return ButtonId::Uf1Vpot3Push;
        case 0x0C: return ButtonId::Uf1Vpot4Push;
        case 0x0D: return ButtonId::Uf1ChannelPush;
        // Soft keys 0x18..0x1C.
        case 0x18: return ButtonId::Uf1ChannelSoftKey;
        case 0x19: return ButtonId::Uf1DisplaySoft1;
        case 0x1A: return ButtonId::Uf1DisplaySoft2;
        case 0x1B: return ButtonId::Uf1DisplaySoft3;
        case 0x1C: return ButtonId::Uf1DisplaySoft4;
        // Channel strip 0x1D..0x1F.
        case 0x1D: return ButtonId::Uf1Solo;
        case 0x1E: return ButtonId::Uf1Cut;
        case 0x1F: return ButtonId::Uf1Sel;
        // Nav block 0x21..0x27 (0x20 MODE handled locally, returns None).
        case 0x21: return ButtonId::Uf1BankLeft;
        case 0x22: return ButtonId::Uf1FiveToEight;
        case 0x23: return ButtonId::Uf1BankRight;
        case 0x24: return ButtonId::Uf1ArrowLeft;
        case 0x25: return ButtonId::Uf1Btn360;
        case 0x26: return ButtonId::Uf1ArrowRight;
        case 0x27: return ButtonId::Uf1Scrub;
        // NAV cross 0x28..0x2C.
        case 0x28: return ButtonId::Uf1NavUp;
        case 0x29: return ButtonId::Uf1NavLeft;
        case 0x2A: return ButtonId::Uf1NavCentre;
        case 0x2B: return ButtonId::Uf1NavRight;
        case 0x2C: return ButtonId::Uf1NavDown;
        // Secondary transport 0x30..0x36.
        case 0x30: return ButtonId::Uf1SecLeft;
        case 0x31: return ButtonId::Uf1SecRight;
        case 0x32: return ButtonId::Uf1Cycle;
        case 0x33: return ButtonId::Uf1Click;
        case 0x34: return ButtonId::Uf1SecKey1;
        case 0x35: return ButtonId::Uf1SecKey2;
        case 0x36: return ButtonId::Uf1Shift;
        // Flip / Master / primary transport 0x38..0x3E.
        case 0x38: return ButtonId::Uf1Flip;
        case 0x39: return ButtonId::Uf1Master;
        case 0x3A: return ButtonId::Uf1Rwd;
        case 0x3B: return ButtonId::Uf1Ffw;
        case 0x3C: return ButtonId::Uf1Stop;
        case 0x3D: return ButtonId::Uf1Play;
        case 0x3E: return ButtonId::Uf1Rec;
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

// Long-press support — measured from press-edge per (layer, button-id).
// Threshold is 500 ms (matches generic "tap vs hold" UX expectations).
constexpr std::chrono::milliseconds kLongPressThreshold{500};

// dispatch() runs on the INPUT threads (UF8 / UC1 / UF1 — three distinct
// threads), and tickLongPressThreshold() polls these same two maps from
// the MAIN thread (onTimer). So every access to g_pressStart /
// g_longPressStart MUST hold g_pressMx. The old "needs no locking"
// comment predated the main-thread threshold poll and is no longer true.
// Never call runSlot_ while holding this lock — a fired builtin can
// re-enter dispatch on the same thread; collect under the lock, fire
// after unlocking.
std::mutex g_pressMx;

// Per-press record so the long-press path knows both WHEN the press
// started AND WHICH modifier was held at press time. Snapshot stays
// stable across the press window even if the user releases the
// modifier mid-hold — gives predictable Shift+button semantics.
//
// The long-press now fires WHILE HELD at the 0.5 s threshold (via
// tickLongPressThreshold), not on the release edge — the UF1 sometimes
// drops/reorders the button-release event, so a release-edge fire could
// be lost. When a press arms a long-press, longArmed is set and longSlot
// holds a COPY of the resolved long slot so the timer can fire it with no
// binding lookup. longFired flips true the moment the timer fires it, so
// the release edge cleans up without re-firing (short OR long).
struct PressRecord {
    std::chrono::steady_clock::time_point start;
    Modifier                              mod       = Modifier::Plain;
    bool                                  longArmed = false;
    bool                                  longFired = false;
    ActionSlot                            longSlot;
    ButtonId                              id        = ButtonId::None;
};
std::unordered_map<uint32_t, PressRecord> g_pressStart;

// Separate tracker for Toggle / Hold + long-press. The Momentary path
// reuses g_pressStart (deferred-primary semantics); Toggle and Hold need
// their own map because the standard path consumes g_pressStart for Hold
// before we can read the held duration. Keys are pressKey(layer, id).
std::unordered_map<uint32_t, PressRecord> g_longPressStart;

// Double-press support. A 2nd press of the same (layer, button) within
// kDoubleClickMs of the previous PRESS fires the binding's doublePress
// slot ADDITIVELY (the single press already fired normally). Mirrors the
// host-keyboard Shift double-click window (main.cpp kShiftDoubleClickMs).
// Guarded by g_pressMx like the other press maps. The map holds the last
// press timestamp; it's reset the moment a double fires so a triple-tap
// doesn't fire the double twice on the 3rd press.
constexpr std::chrono::milliseconds kDoubleClickMs{400};
std::unordered_map<uint32_t, std::chrono::steady_clock::time_point>
    g_lastPressAt;

// Modifier state, set by main.cpp's mod_shift / mod_cmd / mod_ctrl
// builtin handlers. dispatch reads currentModifierSnapshot() at press
// edge; precedence at snapshot time is Ctrl > Cmd > Shift > Plain so
// the most specific bind wins when multiple modifiers are held.
std::atomic<bool> g_modShiftHeld{false};
std::atomic<bool> g_modCmdHeld  {false};
std::atomic<bool> g_modCtrlHeld {false};
// Keyboard-modifier mirrors — fed from main.cpp's onTimer host-OS poll
// (CGEventSourceFlagsState on macOS, GetAsyncKeyState on Windows,
// no-op on Linux). OR'd with the matching HW flag at the read sites
// below, so a `mod_*` HW binding AND the keyboard key can each
// independently engage the corresponding slot. Cmd has no Windows
// keyboard source (the Windows key is OS-reserved and not claimed).
// Frank 2026-05-22.
std::atomic<bool> g_modShiftKbHeld{false};
std::atomic<bool> g_modCmdKbHeld  {false};
std::atomic<bool> g_modCtrlKbHeld {false};

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
// MSVC can't instantiate std::array<std::atomic<T>, N> (deleted copy
// ctor on std::atomic propagates through std::array's aggregate init);
// the raw C array works on all compilers.
std::atomic<uint8_t> g_lastFiredMod[kLastFiredModSize] = {};

uint32_t pressKey(int layer, ButtonId id)
{
    return (static_cast<uint32_t>(layer) << 16)
         | static_cast<uint32_t>(static_cast<uint16_t>(id));
}

// ⇨ THE PER-MODE NAV-CROSS FACTORY TABLE — used by the factory seed AND by the
// backfill that teaches an existing config about the 30 per-mode ids. ONE list, or
// the two would drift and a user upgrading would get a different cross than a
// fresh install (Frank 2026-08-18).
// Playhead and Scrub get the plain zoom cross back, because that is what the key
// does when no editing mode is engaged; the four editing modes get the named
// action that mode's half of applyUf1JogNav_ used to run, so nothing changes
// hands out of the box. The centre in Razor and Items is the HELD content drag
// and therefore seeds as Behavior::Hold — a Momentary seed would fire once and
// never let go.
// ⚠ Labels are EMPTY on purpose. The UF1 prints nothing next to the nav cross,
// so a label there is data nobody can ever see, and with labelIsUserSet it would
// additionally stop following the action you bind (Frank 2026-08-18: "wieso sind
// da labels drin? können wir ja gar nirgends anzeigen").
// `shiftAction` (nullptr = leave Shift empty) seeds the key's SHIFT slot. The
// add-to-selection gestures live there rather than inside the plain action, so
// what Shift does is visible in the editor and rebindable like anything else
// (Frank 2026-08-18). An empty Shift slot falls back to Plain at dispatch, so
// the keys without one behave exactly as before.
struct NavSeed { ButtonId id; const char* action; const char* label; Behavior beh;
                 const char* shiftAction; };
const NavSeed kNavSeed[] = {
        // Playhead / Scrub: the zoom cross, centre zooms to fit.
        { ButtonId::Uf1NavUpPlayhead,     "zoom_up", "", Behavior::Momentary , nullptr },
        { ButtonId::Uf1NavDownPlayhead,   "zoom_down", "", Behavior::Momentary , nullptr },
        { ButtonId::Uf1NavLeftPlayhead,   "zoom_left", "", Behavior::Momentary , nullptr },
        { ButtonId::Uf1NavRightPlayhead,  "zoom_right", "", Behavior::Momentary , nullptr },
        { ButtonId::Uf1NavCentrePlayhead, "zoom_center", "",            Behavior::Momentary , nullptr },
        { ButtonId::Uf1NavUpScrub,        "zoom_up", "", Behavior::Momentary , nullptr },
        { ButtonId::Uf1NavDownScrub,      "zoom_down", "", Behavior::Momentary , nullptr },
        { ButtonId::Uf1NavLeftScrub,      "zoom_left", "", Behavior::Momentary , nullptr },
        { ButtonId::Uf1NavRightScrub,     "zoom_right", "", Behavior::Momentary , nullptr },
        { ButtonId::Uf1NavCentreScrub,    "zoom_center", "",            Behavior::Momentary , nullptr },
        // Items: prev/next item, item on the track above/below, centre drags.
        { ButtonId::Uf1NavLeftItems,      "jog_item_prev", "", Behavior::Momentary , "jog_item_prev_add" },
        { ButtonId::Uf1NavRightItems,     "jog_item_next", "", Behavior::Momentary , "jog_item_next_add" },
        { ButtonId::Uf1NavUpItems,        "jog_item_track_up", "",  Behavior::Momentary , "jog_item_track_up_add" },
        { ButtonId::Uf1NavDownItems,      "jog_item_track_down", "",  Behavior::Momentary , "jog_item_track_down_add" },
        { ButtonId::Uf1NavCentreItems,    "jog_content_drag", "",           Behavior::Hold , nullptr },
        // Envelope: prev/next point, lane above/below, centre switches what
        // the wheel edits.
        { ButtonId::Uf1NavLeftEnvelope,   "jog_env_point_prev", "",   Behavior::Momentary , "jog_env_point_prev_add" },
        { ButtonId::Uf1NavRightEnvelope,  "jog_env_point_next", "",   Behavior::Momentary , "jog_env_point_next_add" },
        { ButtonId::Uf1NavUpEnvelope,     "jog_env_lane_up", "", Behavior::Momentary , nullptr },
        { ButtonId::Uf1NavDownEnvelope,   "jog_env_lane_down", "", Behavior::Momentary , nullptr },
        { ButtonId::Uf1NavCentreEnvelope, "jog_env_target_toggle", "",         Behavior::Toggle , nullptr },
        // Razor: the four edges, centre takes the whole area and drags it.
        { ButtonId::Uf1NavLeftRazor,      "jog_razor_left", "", Behavior::Momentary , nullptr },
        { ButtonId::Uf1NavRightRazor,     "jog_razor_right", "", Behavior::Momentary , nullptr },
        { ButtonId::Uf1NavUpRazor,        "jog_razor_top", "", Behavior::Momentary , nullptr },
        { ButtonId::Uf1NavDownRazor,      "jog_razor_bottom", "", Behavior::Momentary , nullptr },
        { ButtonId::Uf1NavCentreRazor,    "jog_content_drag", "",           Behavior::Hold , nullptr },
        // Fades: the arrows carry ONE action each that means two things, because
        // the centre switches what the cross is for (aim at a fade / walk the
        // items) exactly the way Envelope's centre switches what the wheel edits.
        // Splitting them into four aim-only and four walk-only actions would put
        // eight bindings on four keys, and the key could still only hold one.
        { ButtonId::Uf1NavLeftFades,      "jog_fade_left", "", Behavior::Momentary , "jog_fade_left_add" },
        { ButtonId::Uf1NavRightFades,     "jog_fade_right", "", Behavior::Momentary , "jog_fade_right_add" },
        { ButtonId::Uf1NavUpFades,        "jog_fade_up", "", Behavior::Momentary , "jog_fade_up_add" },
        { ButtonId::Uf1NavDownFades,      "jog_fade_down", "", Behavior::Momentary , "jog_fade_down_add" },
        { ButtonId::Uf1NavCentreFades,    "jog_fade_nav_toggle", "",        Behavior::Toggle , nullptr },
    };

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
    crumb_("seed: enter");
    // `c = Config{}` materialises the temporary on the stack, and
    // sizeof(Config) is 937 KB against Windows's 1 MB main-thread stack —
    // MSVC's prologue probes the whole frame before the first instruction,
    // so this crashes in __chkstk with no breadcrumb (0xc00000fd). 699a0d0
    // heap-allocated every named Config local for exactly this reason but
    // could not see this one: it was a temporary, and at the time Config
    // was 578 KB and fit. The UF1 fields took sizeof(Binding) from 1336 to
    // 1984 bytes, which is what pushed it over. Move-assign from the heap:
    // Config is maps and vectors, so the move itself is cheap.
    { auto fresh = std::make_unique<Config>(); c = std::move(*fresh); }
    crumb_("seed: fresh Config done");
    c.version     = 2;
    c.activeLayer = 0;
    c.layers[0].name = "Layer 1";
    c.layers[1].name = "Layer 2";
    c.layers[2].name = "Layer 3";
    crumb_("seed: layer names set");

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
    // Nav / Nudge / EncFocus / ChannelPush ship UNBOUND post-2026-05-19
    // so the factory layout matches SSL 360°: no encoder-mode LED lit by
    // default, the channel encoder behaves as Channel-Select (prev / next
    // strip) which is the default EncoderMode. Users can rebind these
    // buttons in Settings → Bindings to any of the new modes (Markers /
    // Bank by 1ch / Last Param / Mousewheel / etc.).

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

    // UC1 Encoder 1 rotation. Plain = track_scroll (the pre-bind
    // hardcoded "step focused-track + force CS focus + UC1 setFocusedTrack"
    // behaviour, now a builtin). Shift = instance_cycle for symmetry
    // with Encoder 2. Cmd/Ctrl free.
    {
        auto& e1 = L1[ButtonId::Uc1Encoder1];
        e1.behavior = Behavior::Momentary;
        e1.label    = "UC1 Enc 1";
        auto& spPlain = e1.shortPress[static_cast<int>(Modifier::Plain)];
        spPlain.type   = ActionType::Builtin;
        spPlain.action = "track_scroll";
        auto& spShift = e1.shortPress[static_cast<int>(Modifier::Shift)];
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
    L1[ButtonId::Flip]      = mkBuiltin("flip",                  Behavior::Toggle,    "FLIP");
    L1[ButtonId::PluginBtn] = mkBuiltin("ssl_strip_mode_toggle", Behavior::Toggle,    "PLUGIN");
    L1[ButtonId::Btn360]    = mkBuiltin("mixer_toggle",          Behavior::Momentary, "360");
    // PAN → Nav Mode (Markers & Regions) — ROADMAP Phase 2.8 factory
    // default. pan_force stays available as a builtin; users who'd
    // rather keep PAN forcing V-Pot Pan can rebind via Settings →
    // Bindings.
    L1[ButtonId::Pan]       = mkBuiltin("marker_overlay_toggle", Behavior::Toggle,    "NAV");

    // Shift+Plugin: same toggle plus open/close the focused track's
    // user-mapped plug-in GUI.
    {
        auto& spShift = L1[ButtonId::PluginBtn]
            .shortPress[static_cast<int>(Modifier::Shift)];
        spShift.type   = ActionType::Builtin;
        spShift.action = "ssl_strip_mode_toggle_with_gui";
        spShift.param  = 0;
        spShift.label  = "PLUG+UI";
    }

    // Shift+360 → toggle the Learn-HUD (focused plug-in's assignments). Plain
    // 360 stays the Plug-in Mixer toggle. Mirrored onto the UC1 360 button so
    // the gesture works from either surface. Frank 2026-06-16.
    for (ButtonId id : { ButtonId::Btn360, ButtonId::Uc1Btn360 }) {
        auto& spShift = L1[id].shortPress[static_cast<int>(Modifier::Shift)];
        spShift.type   = ActionType::Builtin;
        spShift.action = "learn_hud_toggle";
        spShift.param  = 0;
        spShift.label  = "LEARN";
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
        snprintf(nameSend, sizeof(nameSend), "send_all_%d", i + 1);
        snprintf(nameRecv, sizeof(nameRecv), "recv_all_%d", i + 1);
        snprintf(label,    sizeof(label),    "S/P %d",    i + 1);
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
        snprintf(label, sizeof(label), "Soft-Key %d", i + 1);
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
        snprintf(nA, sizeof(nA), "softkey_bank_%d", li * 3 + 1);
        snprintf(nB, sizeof(nB), "softkey_bank_%d", li * 3 + 2);
        snprintf(nC, sizeof(nC), "softkey_bank_%d", li * 3 + 3);
        L[ButtonId::Quick1] = mkBuiltin(nA, Behavior::Momentary, "Q1");
        L[ButtonId::Quick2] = mkBuiltin(nB, Behavior::Momentary, "Q2");
        L[ButtonId::Quick3] = mkBuiltin(nC, Behavior::Momentary, "Q3");
    }

    // ---- UF1 buttons (Phase 1) -----------------------------------------
    // Primary transport + Cycle/Click default to STOCK REAPER actions and
    // stay user-rebindable (Frank 2026-07-30 "reaper actions als default").
    // No bespoke uf1_* builtins — REAPER already owns these
    // ([[feedback-dont-reinvent-reaper-builtins]]). Solo/Cut/Sel are the
    // exception: NOT seeded, fired natively on the focused track from
    // onUf1Event's direct handler (locked, not rebindable).
    {
        auto mkReaper = [](const char* id, const char* label) {
            Binding bd; bd.behavior = Behavior::Momentary; bd.label = label;
            auto& s = bd.shortPress[static_cast<int>(Modifier::Plain)];
            s.type = ActionType::Reaper; s.action = id;
            return bd;
        };
        // Transport: Play 1007 / Stop 1016 / Record 1013; RWD = Go to start
        // of project 40042 / FFW = Go to end of project 40043.
        L1[ButtonId::Uf1Play] = mkReaper("1007",  "PLAY");
        L1[ButtonId::Uf1Stop] = mkReaper("1016",  "STOP");
        L1[ButtonId::Uf1Rec]  = mkReaper("1013",  "REC");
        L1[ButtonId::Uf1Rwd]  = mkReaper("40042", "RWD");
        L1[ButtonId::Uf1Ffw]  = mkReaper("40043", "FFW");
        L1[ButtonId::Uf1Cycle] = mkReaper("1068",  "CYCLE");
        L1[ButtonId::Uf1Click] = mkReaper("40364", "CLICK");
    }
    // 360 key defaults to the UF1 time-display format step (rea-sixty
    // built-in — REAPER has no equivalent). Rebindable like any other.
    L1[ButtonId::Uf1Btn360] = mkBuiltin("uf1_time_display_step",
                                        Behavior::Momentary, "360\xC2\xB0");
    // NAV cross defaults to ZOOM (Frank 2026-07-30: "nav macht schon Sinn"),
    // Up/Down/Left/Right → zoom in that axis, Centre → zoom-to-fit. Global
    // REAPER-view actions, rebindable like any other UF1 control.
    L1[ButtonId::Uf1NavUp]     = mkBuiltin("jog_nav_up",     Behavior::Momentary, "JOG \xE2\x96\xB2");
    L1[ButtonId::Uf1NavDown]   = mkBuiltin("jog_nav_down",   Behavior::Momentary, "JOG \xE2\x96\xBC");
    L1[ButtonId::Uf1NavLeft]   = mkBuiltin("jog_nav_left",   Behavior::Momentary, "JOG \xE2\x97\x82");
    L1[ButtonId::Uf1NavRight]  = mkBuiltin("jog_nav_right",  Behavior::Momentary, "JOG \xE2\x96\xB8");
    L1[ButtonId::Uf1NavCentre] = mkBuiltin("jog_nav_center", Behavior::Momentary, "FOCUS");
    // ⇨ AND THE SAME CROSS, ONCE PER JOG MODE (Frank 2026-08-18).
    // The five ids above stay as the never-dispatched fallback; the remap in
    // main.cpp sends every press to the id of the ACTIVE mode, which is what
    // these seed. Playhead gets the plain zoom cross back, because that is what
    // the key does when no editing mode is engaged ("Playhead = standard-
    // belegung"); the four editing modes get the named action that mode's half
    // of applyUf1JogNav_ used to run, so out of the box nothing changes hands.
    // The centre in Razor and Items is the HELD content drag, so it seeds with
    // Behavior::Hold — a Momentary seed there would fire once and never let go.
    for (const auto& n : kNavSeed) {
        // Green ACTIVE, the default white stays for inactive. The cross lights
        // green when its mode has something picked (razor edge, envelope points,
        // a held item drag, the fade edge), and that colour belongs in the
        // BINDING so the editor can show it and the user can change it. It used
        // to be a constant inside the painter, which is why the editor had
        // nothing to display (Frank 2026-08-20).
        L1[n.id] = mkBuiltin(n.action, n.beh, n.label);
        L1[n.id].color[0] = 0x00; L1[n.id].color[1] = 0xFF; L1[n.id].color[2] = 0x66;
        if (n.shiftAction) {
            auto& sh = L1[n.id].shortPress[static_cast<int>(Modifier::Shift)];
            sh.type   = ActionType::Builtin;
            sh.action = n.shiftAction;
        }
    }
    // Controls that used to be HARDCODED fall-throughs in onUf1Event now ship
    // as real, rebindable factory defaults (their worker-safe builtins live in
    // main.cpp) so the bindings editor shows the action instead of "none":
    //  · SHIFT   → mod_shift  (Hold — same shared Shift modifier as UF8/UC1,
    //                          incl. the double-click Fine latch).
    //  · ENC push→ show_focused_plugin_gui (toggle the focused plug-in GUI).
    //  · FLIP    → uf1_flip / MASTER → uf1_master (Toggle; LED = state).
    //  · 5-8     → uf1_five_to_eight (DAW channel group 1-4/5-8 · Sends window).
    //  · V-Pot 1-4 push → uf1_vpot_reset (param 0..3 = which V-Pot).
    //  · Bank ◄ ►→ uf1_bank_step  (param -1/+1 = ±8 tracks).
    //  · Page ◄ ►→ uf1_page_step  (param -1/+1 = soft-key bank / plug-in page),
    //                          LONG press → uf1_dyn_bank_page (inside a dynamic
    //                          bank).
    L1[ButtonId::Uf1Shift]       = mkBuiltin("mod_shift",              Behavior::Hold,      "SHIFT");
    L1[ButtonId::Uf1ChannelPush] = mkBuiltin("show_focused_plugin_gui", Behavior::Momentary, "ENC PUSH");
    // …plus a LONG-PRESS on the channel encoder → back to Channel Select
    // (Frank 2026-07-31). Short push still toggles the plug-in GUI; a hold
    // past the long-press threshold snaps the UF1 encoder home. Fully
    // rebindable/clearable like any other long-press slot.
    {
        Binding& encPush   = L1[ButtonId::Uf1ChannelPush];
        encPush.hasLongPress = true;
        auto& lp   = encPush.longPress[static_cast<int>(Modifier::Plain)];
        lp.type    = ActionType::Builtin;
        lp.action  = "uf1_encoder_ch_select";
    }
    // The single SOFT key above the fader display. It shipped unbound — the
    // only free key in that cluster, and the only one with an LED going spare.
    // Pin Set is the UF1-shaped default (Frank 2026-08-10): the UF1 is the
    // surface that PARKS on a Focus Set, and the key sits directly above the
    // track name that changes when you pin. Toggle → the LED shows pinned.
    // User-rebindable like every other key; see upgradeBackfillUf1SoftKey_
    // for why an existing config gets it without losing an own assignment.
    L1[ButtonId::Uf1ChannelSoftKey] =
        mkBuiltin("temp_selset_pin_uf1_channel", Behavior::Toggle, "PIN SET");
    L1[ButtonId::Uf1Flip]        = mkBuiltin("uf1_flip",              Behavior::Toggle,    "FLIP");
    L1[ButtonId::Uf1Master]      = mkBuiltin("uf1_master",            Behavior::Toggle,    "MASTER");
    L1[ButtonId::Uf1FiveToEight] = mkBuiltin("uf1_five_to_eight",     Behavior::Momentary, "5-8");
    L1[ButtonId::Uf1Vpot1Push]   = mkBuiltin("uf1_vpot_reset", Behavior::Momentary, "V-POT 1 PUSH", 255, 255, 255, 0);
    L1[ButtonId::Uf1Vpot2Push]   = mkBuiltin("uf1_vpot_reset", Behavior::Momentary, "V-POT 2 PUSH", 255, 255, 255, 1);
    L1[ButtonId::Uf1Vpot3Push]   = mkBuiltin("uf1_vpot_reset", Behavior::Momentary, "V-POT 3 PUSH", 255, 255, 255, 2);
    L1[ButtonId::Uf1Vpot4Push]   = mkBuiltin("uf1_vpot_reset", Behavior::Momentary, "V-POT 4 PUSH", 255, 255, 255, 3);
    L1[ButtonId::Uf1BankLeft]    = mkBuiltin("uf1_bank_step", Behavior::Momentary, "BANK \xE2\x97\x82", 255, 255, 255, -1);
    L1[ButtonId::Uf1BankRight]   = mkBuiltin("uf1_bank_step", Behavior::Momentary, "BANK \xE2\x96\xB8", 255, 255, 255, +1);
    L1[ButtonId::Uf1ArrowLeft]   = mkBuiltin("uf1_page_step", Behavior::Momentary, "PAGE \xE2\x97\x82", 255, 255, 255, -1);
    L1[ButtonId::Uf1ArrowRight]  = mkBuiltin("uf1_page_step", Behavior::Momentary, "PAGE \xE2\x96\xB8", 255, 255, 255, +1);
    // …and their LONG press pages INSIDE a dynamic soft-key bank. Two paging
    // levels on one key pair: short = which bank, long = which page of it. See
    // upgradeBackfillUf1ArrowLongPress_ for how an existing config gets it.
    {
        struct ArrowLp { ButtonId id; int param; };
        for (const ArrowLp& a : { ArrowLp{ButtonId::Uf1ArrowLeft,  -1},
                                  ArrowLp{ButtonId::Uf1ArrowRight, +1} }) {
            Binding& bd = L1[a.id];
            bd.hasLongPress = true;
            auto& lp  = bd.longPress[static_cast<int>(Modifier::Plain)];
            lp.type   = ActionType::Builtin;
            lp.action = "uf1_dyn_bank_page";
            lp.param  = a.param;
        }
    }
    // Secondary transport +SHIFT = the 6 REAPER automation modes. The SSL UF1
    // silk labels (OFF/READ/WRT/TRIM/LTCH/TCH) ARE this mapping (Frank 2026-07-31
    // "steht schon in den abkürzungen der buttons"). Only the SHIFT slot is set —
    // Plain stays user-bindable; the modifier-aware LED lights the active mode.
    {
        struct AutoSeed { ButtonId id; const char* action; const char* label; };
        static const AutoSeed kAuto[6] = {
            { ButtonId::Uf1SecLeft,  "auto_off",   "OFF"  },
            { ButtonId::Uf1SecRight, "auto_read",  "READ" },
            { ButtonId::Uf1Cycle,    "auto_write", "WRT"  },
            { ButtonId::Uf1Click,    "auto_trim",  "TRIM" },
            { ButtonId::Uf1SecKey1,  "auto_latch", "LTCH" },
            { ButtonId::Uf1SecKey2,  "auto_touch", "TCH"  },
        };
        for (const auto& a : kAuto) {
            auto& sp = L1[a.id].shortPress[static_cast<int>(Modifier::Shift)];
            sp.type   = ActionType::Builtin;
            sp.action = a.action;
            sp.param  = 0;
            sp.label  = a.label;
        }
    }
    // Key 2 PLAIN = Fine. Not a free choice: the UF1's firmware prints
    // "FINE CTRL 2" over this key with its own On/Off readout, and that caption
    // is not ours to change (it is drawn by the device, not sent). Anything else
    // bound here makes the display lie. It stays rebindable — but the factory
    // default has to be the one the hardware already promises.
    {
        Binding& bd = L1[ButtonId::Uf1SecKey2];
        auto& sp = bd.shortPress[static_cast<int>(Modifier::Plain)];
        sp.type   = ActionType::Builtin;
        sp.action = "uf1_fine_toggle";
        sp.param  = 0;
        sp.label  = "FINE";
        bd.behavior = Behavior::Toggle;
    }
    // Key 1 PLAIN = REAPER's "Unsolo all tracks" (40340) — SSL's own factory
    // assignment for this key, and the one the display already assumes: the
    // SOLO CLR 1 caption sits over it and its SOLO ACTIVE indicator lights by
    // itself whenever anything is soloed. The key was unbound, so the lit
    // indicator pointed at a key that did nothing. Rebindable like every other.
    {
        auto& sp = L1[ButtonId::Uf1SecKey1].shortPress[static_cast<int>(Modifier::Plain)];
        sp.type   = ActionType::Reaper;
        sp.action = "40340";
        sp.param  = 0;
        sp.label  = "SOLO CLR";
    }
    // The remaining UF1 buttons (above-fader V-Pot push, display soft-keys,
    // channel soft-key, secondary transport, Scrub) ship UNBOUND — the user
    // assigns them in Settings → Bindings → UF1, matching how UF8 ships
    // Nav/Nudge unbound.

    // SEL DOUBLE-press factory default (UF1 SEL + shared UF8 SEL) →
    // `show_fx_chain`. Single-press select stays NATIVE on both surfaces;
    // this only makes the double-tap open the FX chain of the just-
    // selected track. Matches upgradeBackfillSelDouble_ (which fills the
    // same default into pre-v19 configs). Frank 2026-08-03.
    {
        auto seedSelDouble = [&](ButtonId id) {
            Binding& bd = L1[id];   // default-creates if missing
            bd.hasDoublePress = true;
            auto& dp  = bd.doublePress[static_cast<int>(Modifier::Plain)];
            dp.type   = ActionType::Builtin;
            dp.action = "show_fx_chain";
            dp.label  = "FX Chain";
        };
        seedSelDouble(ButtonId::Uf1Sel);
        seedSelDouble(ButtonId::Uf8Select);
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
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
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

// A step counts as empty when it would fire nothing on dispatch:
// Reaper/Builtin/Keyboard with no action string are inert (the picker
// shows "Pick one" until the user selects something), Noop is always
// inert, MIDI carries its payload in numeric fields so it's never
// considered empty just from action being unset.
bool stepIsEmpty_(const ActionStep& st)
{
    switch (st.type) {
        case ActionType::Noop:     return true;
        case ActionType::Reaper:
        case ActionType::Builtin:
        case ActionType::Keyboard: return st.action.empty();
        case ActionType::Midi:     return false;
    }
    return true;
}

// ⚠ TWO QUESTIONS, TWO PREDICATES. Do not merge them again.
//   slotIsEmpty_   — "does this slot DO anything?" Used by DISPATCH:
//                    effectiveLongSlot_'s Plain fallback and the long/double
//                    arming sites. A slot carrying only a colour must count as
//                    empty here, or Shift+long-press stops firing and the arm
//                    swallows the press. Widening this broke exactly that.
//   slotHasNoData_ — "is there anything worth WRITING?" Used by the serialiser,
//                    which drops empty slots. A label-only slot is real data:
//                    that is what a soft-key's Shift set looks like, and what
//                    recallBankPreset writes into it (Frank 2026-08-18).
bool slotIsEmpty_(const ActionSlot& s)
{
    if (!stepIsEmpty_(s)) return false;
    for (const auto& st : s.extraSteps) {
        if (!stepIsEmpty_(st)) return false;
    }
    return true;
}

bool slotHasNoData_(const ActionSlot& s)
{
    if (!s.label.empty())                     return false;
    if (s.led.hasActive || s.led.hasInactive) return false;
    return slotIsEmpty_(s);
}

// The same question one level up: "is there anything worth WRITING about this
// whole key?" A soft-key carries more than its actions — a colour, a Behaviour,
// the LED-when-empty tick — and both soft-key stores decided emptiness from the
// ACTIONS alone. So the one case where those fields are the only thing the user
// set is exactly the case that dropped them: tick "LED when empty" on a key you
// deliberately left unassigned, restart, gone (Frank 2026-08-18). Belongs to the
// slotHasNoData_ family — it answers what to WRITE, never what DISPATCH should
// treat as empty. Do not hand it to a dispatch path.
bool bindingHasNoData_(const Binding& bd)
{
    if (!bd.label.empty())                    return false;
    if (bd.labelIsUserSet)                    return false;
    if (bd.behavior != Behavior::Momentary)   return false;
    if (bd.ledShowWhenEmpty)                  return false;
    if (bd.hasLongPress || bd.hasDoublePress) return false;
    // Factory LED appearance = white/Bright over white/Dim (struct defaults).
    if (bd.color[0] != 0xFF || bd.color[1] != 0xFF || bd.color[2] != 0xFF)
        return false;
    if (bd.brightness != Brightness::Bright)  return false;
    if (bd.inactiveColor[0] != 0xFF || bd.inactiveColor[1] != 0xFF
        || bd.inactiveColor[2] != 0xFF)
        return false;
    if (bd.inactiveBrightness != Brightness::Dim) return false;
    for (int m = 0; m < kModifierCount; ++m) {
        if (!slotHasNoData_(bd.shortPress[m]))  return false;
        if (!slotHasNoData_(bd.longPress[m]))   return false;
        if (!slotHasNoData_(bd.doublePress[m])) return false;
    }
    return true;
}

} // namespace

bool slotIsEmpty(const ActionSlot& s)
{
    return slotIsEmpty_(s);
}

namespace {

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
    // ⇨ KEEP THE MIDI FIELDS EVEN WHEN THE STEP IS NOT (CURRENTLY) MIDI.
    // The editor saves on every keystroke, so flipping a configured MIDI step to
    // REAPER for a moment wrote the step without its midi block and the device,
    // channel and data bytes were gone from disk — switching back gave you an
    // empty form (Frank 2026-08-18). Written whenever anything differs from the
    // struct defaults; a step that never was MIDI still writes nothing.
    const bool midiWorthKeeping =
        s.type == ActionType::Midi
     || !s.midiDevice.empty()
     || s.midiChannel != 1 || s.midiMsgType != 0
     || s.midiData1 != 60  || s.midiData2 != 127;
    if (midiWorthKeeping) {
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
    // Stepped-builtin fields (fx_param_inc / fx_param_dec). Only written
    // when non-default to keep the JSON tight for the common case.
    if (s.stepValue != 0.0f) {
        os << ", \"step_value\": " << s.stepValue;
    }
    if (s.wrap) {
        os << ", \"wrap\": true";
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
        if (slotHasNoData_(row[m])) continue;
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
    // Only emitted when true — an absent key reads as false, which is what a
    // factory-seeded label is.
    if (bd.labelIsUserSet) os << ", \"label_user\": true";
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
    if (bd.hasDoublePress) {
        os << ", \"double\": ";
        serializeMatrixRow_(bd.doublePress, os);
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
    // ⇨ STABLE ORDER — walk kNames, not the map.
    // L.bindings is an unordered_map, so its iteration order shifts whenever the
    // map is rebuilt. Every load therefore rewrote the file with the same
    // bindings in a different order, and a config diff was mostly reshuffling
    // with any real change buried in it (Frank 2026-08-18, checking exactly that
    // after the v26 migration). kNames is the declaration order and already
    // serves as the stable order for findFirstBoundTo, for the same reason.
    // Nothing is lost by the switch: an entry without a name was skipped before
    // too, and a name is what kNames membership means.
    for (const auto& e : kNames) {
        if (!e.name || !*e.name) continue;
        const auto it = L.bindings.find(e.id);
        if (it == L.bindings.end()) continue;
        if (!first) os << ",";
        first = false;
        os << "\n" << bindingPad << "\"" << e.name << "\": {";
        serializeBindingBody_(it->second, os);
        os << "}";
    }
    if (!first) os << "\n" << fieldPad;
    os << "}\n";
}

// Emits all populated (layer, quick, subBank, slot) entries as a flat
// list. Empty slots are skipped so a default-constructed Config writes
// nothing here (no 432-line empty matrix in the JSON).
// onlyLayer >= 0 restricts the list to that layer and DROPS the "layer" field,
// so a per-layer export can be imported onto any layer. In that mode the array
// is emitted even when empty: absent means "this file says nothing about the
// soft-keys" (a pre-v2 layer file), empty means "this layer has none", and the
// importer has to be able to tell those apart.
void serializeUserQuicks_(const Config& c, std::ostringstream& os,
                          int onlyLayer = -1)
{
    // The local lambda that used to sit here asked slotIsEmpty_ — the DISPATCH
    // predicate — so a label-only or colour-only slot counted as empty and never
    // reached the file. bindingHasNoData_ is the writer's question.
    const int loLayer = (onlyLayer >= 0) ? onlyLayer : 0;
    const int hiLayer = (onlyLayer >= 0) ? onlyLayer + 1 : 3;

    bool anyData = false;
    for (int li = loLayer; li < hiLayer && !anyData; ++li) {
        for (int qi = 0; qi < kQuicksPerLayer && !anyData; ++qi) {
            for (int bi = 0; bi < kSubBanksPerQuick && !anyData; ++bi) {
                for (int si = 0; si < kSlotsPerSubBank && !anyData; ++si) {
                    if (!bindingHasNoData_(
                            c.userQuicks[li].quicks[qi].subBanks[bi].slots[si])) {
                        anyData = true;
                    }
                }
            }
        }
    }
    if (!anyData && onlyLayer < 0) return;

    os << ",\n  \"user_quicks\": [";
    bool first = true;
    for (int li = loLayer; li < hiLayer; ++li) {
        for (int qi = 0; qi < kQuicksPerLayer; ++qi) {
            for (int bi = 0; bi < kSubBanksPerQuick; ++bi) {
                for (int si = 0; si < kSlotsPerSubBank; ++si) {
                    const auto& bd = c.userQuicks[li].quicks[qi]
                                        .subBanks[bi].slots[si];
                    if (bindingHasNoData_(bd)) continue;
                    if (!first) os << ",";
                    first = false;
                    os << "\n    {";
                    if (onlyLayer < 0) os << "\"layer\": " << li << ", ";
                    os << "\"quick\": " << qi
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
void serializeSubBankLeds_(const Config& c, std::ostringstream& os,
                           int onlyLayer = -1)
{
    auto isDefault_ = [](const SubBankLed& a) {
        return a.color[0] == 255 && a.color[1] == 255 && a.color[2] == 255
            && a.brightness == Brightness::Bright
            && a.inactiveColor[0] == 255 && a.inactiveColor[1] == 255
            && a.inactiveColor[2] == 255
            && a.inactiveBrightness == Brightness::Dim;
    };

    const int loLayer = (onlyLayer >= 0) ? onlyLayer : 0;
    const int hiLayer = (onlyLayer >= 0) ? onlyLayer + 1 : 3;

    bool anyData = false;
    for (int li = loLayer; li < hiLayer && !anyData; ++li)
        for (int qi = 0; qi < kQuicksPerLayer && !anyData; ++qi)
            for (int bi = 0; bi < kSubBanksPerQuick && !anyData; ++bi)
                for (int m = 0; m < kSoftKeyModifierSets && !anyData; ++m)
                    if (!isDefault_(
                            c.userQuicks[li].quicks[qi].subBankLeds[bi][m]))
                        anyData = true;
    if (!anyData && onlyLayer < 0) return;

    os << ",\n  \"sub_bank_leds\": [";
    bool first = true;
    for (int li = loLayer; li < hiLayer; ++li) {
        for (int qi = 0; qi < kQuicksPerLayer; ++qi) {
            for (int bi = 0; bi < kSubBanksPerQuick; ++bi)
            for (int m = 0; m < kSoftKeyModifierSets; ++m) {
                const auto& a = c.userQuicks[li].quicks[qi].subBankLeds[bi][m];
                if (isDefault_(a)) continue;
                // A modifier set that inherits Plain has nothing of its own to
                // write; only a claimed one is real data.
                if (m != 0 && !a.isSet) continue;
                if (!first) os << ",";
                first = false;
                os << "\n    {";
                if (onlyLayer < 0) os << "\"layer\": " << li << ", ";
                os << "\"quick\": " << qi
                   << ", \"sub_bank\": " << bi
                   << ", \"mod\": " << m
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

// Per-(Layer, Quick, Sub-Bank) dynamic-kind flags. Flat list, one entry
// per Sub-Bank whose kind != None. A dynamic Sub-Bank can have all-empty
// static slots (that's the point), so this can't piggy-back on the
// user_quicks slot list — it needs its own array. Default None writes
// nothing.
void serializeSubBankDynamic_(const Config& c, std::ostringstream& os,
                              int onlyLayer = -1)
{
    const int loLayer = (onlyLayer >= 0) ? onlyLayer : 0;
    const int hiLayer = (onlyLayer >= 0) ? onlyLayer + 1 : 3;

    bool anyData = false;
    for (int li = loLayer; li < hiLayer && !anyData; ++li)
        for (int qi = 0; qi < kQuicksPerLayer && !anyData; ++qi)
            for (int bi = 0; bi < kSubBanksPerQuick && !anyData; ++bi)
                for (int m = 0; m < kSoftKeyModifierSets && !anyData; ++m)
                    if (c.userQuicks[li].quicks[qi].subBanks[bi].dynamic[m]
                        != DynamicBankKind::None)
                        anyData = true;
    if (!anyData && onlyLayer < 0) return;

    os << ",\n  \"sub_bank_dynamic\": [";
    bool first = true;
    for (int li = loLayer; li < hiLayer; ++li)
        for (int qi = 0; qi < kQuicksPerLayer; ++qi)
            for (int bi = 0; bi < kSubBanksPerQuick; ++bi)
                for (int m = 0; m < kSoftKeyModifierSets; ++m) {
                    const auto k =
                        c.userQuicks[li].quicks[qi].subBanks[bi].dynamic[m];
                    if (k == DynamicBankKind::None) continue;
                    if (!first) os << ",";
                    first = false;
                    // "mod" absent means Plain, so a pre-v26 file reads back
                    // onto layer 0 exactly where it used to live.
                    os << "\n    {";
                    if (onlyLayer < 0) os << "\"layer\": " << li << ", ";
                    os << "\"quick\": " << qi
                       << ", \"sub_bank\": " << bi
                       << ", \"mod\": " << m
                       << ", \"kind\": " << static_cast<int>(k) << "}";
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

// True when a UF1 soft-key bank slot carries nothing (all modifier
// short/long slots Noop + no label) — such slots are skipped on save.
static bool uf1BankSlotEmpty_(const Binding& bd)
{
    // A per-SET label counts, not just the key's own name: naming a UF1 soft-key
    // on the Shift set with no action assigned is a legitimate thing to do, and
    // this predicate dropping it is why that vanished on restart. It used to
    // spell out the slot loop itself and so missed the colour / Behaviour /
    // LED-when-empty fields the editor offers on an unassigned key; it now
    // shares the one writer predicate with the UF8 store.
    return bindingHasNoData_(bd);
}

// UF1 soft-key banks — flat list of {bank, slot, body}; only non-empty
// slots are written, so a virgin config stays clean.
void serializeUf1SoftBanks_(const Config& c, std::ostringstream& os)
{
    bool any = false;
    for (int b = 0; b < kUf1SoftBankCount && !any; ++b)
        for (int s = 0; s < kUf1SoftBankSlots; ++s)
            if (!uf1BankSlotEmpty_(c.uf1SoftBanks[b][s])) { any = true; break; }
    if (!any) return;
    os << ",\n  \"uf1_soft_banks\": [";
    bool first = true;
    for (int b = 0; b < kUf1SoftBankCount; ++b)
        for (int s = 0; s < kUf1SoftBankSlots; ++s) {
            const Binding& bd = c.uf1SoftBanks[b][s];
            if (uf1BankSlotEmpty_(bd)) continue;
            if (!first) os << ",";
            first = false;
            os << "\n    {\"bank\": " << b << ", \"slot\": " << s
               << ", \"body\": {";
            serializeBindingBody_(bd, os);
            os << "}}";
        }
    os << "\n  ]";
}

// Per-bank UF1 dynamic-kind flags. Flat list, only non-None entries written
// (mirrors serializeSubBankDynamic_). Empty ⇒ nothing emitted.
void serializeUf1SoftBankDynamic_(const Config& c, std::ostringstream& os)
{
    bool any = false;
    for (int b = 0; b < kUf1SoftBankCount && !any; ++b)
        for (int m = 0; m < kSoftKeyModifierSets && !any; ++m)
            if (c.uf1SoftBankDynamic[b][m] != DynamicBankKind::None) any = true;
    if (!any) return;
    os << ",\n  \"uf1_soft_bank_dynamic\": [";
    bool first = true;
    for (int b = 0; b < kUf1SoftBankCount; ++b)
        for (int m = 0; m < kSoftKeyModifierSets; ++m) {
            const auto k = c.uf1SoftBankDynamic[b][m];
            if (k == DynamicBankKind::None) continue;
            if (!first) os << ",";
            first = false;
            os << "\n    {\"bank\": " << b
               << ", \"mod\": " << m
               << ", \"kind\": " << static_cast<int>(k) << "}";
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
    serializeSubBankDynamic_(c, os);
    serializeBankPresets_(c, os);
    serializeUf1SoftBanks_(c, os);
    serializeUf1SoftBankDynamic_(c, os);
    os << "\n}\n";
    return os.str();
}

// Standalone per-layer envelope. The "type" / "index" fields let
// importLayerFrom verify the file before applying it (and let users
// recognise the file at a glance).
std::string serializeOneLayer_(const Config& c, int idx)
{
    std::ostringstream os;
    os << "{\n";
    // v2 carries the layer's 144 top-soft-key slots as well. v1 files only ever
    // held the layer's button map, so "save layer" handed back a file that
    // silently rebuilt half the layer on import — with a success message
    // (Frank 2026-08-18). The three arrays below are layer-relative (no "layer"
    // field), so a layer saved from L1 can be loaded onto L3.
    os << "  \"version\": 2,\n";
    os << "  \"type\": \"layer\",\n";
    os << "  \"index\": " << idx << ",\n";
    os << "  \"layer\": {\n";
    serializeLayerBody_(c.layers[idx], os, "    ", "      ");
    os << "  }";
    serializeUserQuicks_(c, os, idx);
    serializeSubBankLeds_(c, os, idx);
    serializeSubBankDynamic_(c, os, idx);
    os << "\n}\n";
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
    if (auto* v = obj->get_item_by_name("step_value"))
        if (auto* s = v->get_string_value(true))
            out.stepValue = static_cast<float>(std::atof(s));
    if (auto* v = obj->get_item_by_name("wrap"))
        if (auto* s = v->get_string_value(true))
            out.wrap = (std::atoi(s) != 0);
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
    if (auto* v = be->get_item_by_name("label_user"))
        if (auto* s = v->get_string_value(true))
            bd.labelIsUserSet = (std::strcmp(s, "true") == 0
                              || std::strcmp(s, "1") == 0);
    if (auto* v = be->get_item_by_name("short"))
        parseMatrixRow_(v, bd.shortPress);
    if (auto* v = be->get_item_by_name("long"); v && v->is_object()) {
        bd.hasLongPress = true;
        parseMatrixRow_(v, bd.longPress);
    }
    if (auto* v = be->get_item_by_name("double"); v && v->is_object()) {
        bd.hasDoublePress = true;
        parseMatrixRow_(v, bd.doublePress);
    }
}

// forceLayer >= 0 ignores each entry's "layer" field (a per-layer export
// omits it) and writes everything onto that layer instead.
void parseUserQuicks_(wdl_json_element* root, Config& out, int forceLayer = -1)
{
    auto* arr = root->get_item_by_name("user_quicks");
    if (!arr || !arr->is_array() || !arr->m_array) return;
    const int n = arr->m_array->GetSize();
    for (int i = 0; i < n; ++i) {
        wdl_json_element* eo = arr->enum_item(i);
        if (!eo || !eo->is_object()) continue;
        int layer = forceLayer, quick = -1, subBank = -1, slot = -1;
        if (forceLayer < 0)
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

// forceLayer >= 0 ignores each entry's "layer" field (a per-layer export
// omits it) and writes everything onto that layer instead.
void parseSubBankLeds_(wdl_json_element* root, Config& out, int forceLayer = -1)
{
    auto* arr = root->get_item_by_name("sub_bank_leds");
    if (!arr || !arr->is_array() || !arr->m_array) return;
    const int n = arr->m_array->GetSize();
    for (int i = 0; i < n; ++i) {
        wdl_json_element* eo = arr->enum_item(i);
        if (!eo || !eo->is_object()) continue;
        int layer = forceLayer, quick = -1, subBank = -1;
        if (forceLayer < 0)
            if (auto* v = eo->get_item_by_name("layer"))
                if (auto* s = v->get_string_value(true)) layer = std::atoi(s);
        if (auto* v = eo->get_item_by_name("quick"))
            if (auto* s = v->get_string_value(true)) quick = std::atoi(s);
        if (auto* v = eo->get_item_by_name("sub_bank"))
            if (auto* s = v->get_string_value(true)) subBank = std::atoi(s);
        if (layer   < 0 || layer   >= 3)                 continue;
        if (quick   < 0 || quick   >= kQuicksPerLayer)   continue;
        if (subBank < 0 || subBank >= kSubBanksPerQuick) continue;
        int mod = 0;   // absent = Plain (pre-v27 shape)
        if (auto* v = eo->get_item_by_name("mod"))
            if (auto* t = v->get_string_value(true)) mod = std::atoi(t);
        if (mod < 0 || mod >= kSoftKeyModifierSets) continue;
        SubBankLed& a = out.userQuicks[layer].quicks[quick]
                            .subBankLeds[subBank][mod];
        a = SubBankLed{};
        a.isSet = (mod != 0);
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

// forceLayer >= 0 ignores each entry's "layer" field (a per-layer export
// omits it) and writes everything onto that layer instead.
void parseSubBankDynamic_(wdl_json_element* root, Config& out, int forceLayer = -1)
{
    auto* arr = root->get_item_by_name("sub_bank_dynamic");
    if (!arr || !arr->is_array() || !arr->m_array) return;
    const int n = arr->m_array->GetSize();
    for (int i = 0; i < n; ++i) {
        wdl_json_element* eo = arr->enum_item(i);
        if (!eo || !eo->is_object()) continue;
        int layer = forceLayer, quick = -1, subBank = -1, kind = 0;
        if (forceLayer < 0)
            if (auto* v = eo->get_item_by_name("layer"))
                if (auto* s = v->get_string_value(true)) layer = std::atoi(s);
        if (auto* v = eo->get_item_by_name("quick"))
            if (auto* s = v->get_string_value(true)) quick = std::atoi(s);
        if (auto* v = eo->get_item_by_name("sub_bank"))
            if (auto* s = v->get_string_value(true)) subBank = std::atoi(s);
        if (auto* v = eo->get_item_by_name("kind"))
            if (auto* s = v->get_string_value(true)) kind = std::atoi(s);
        if (layer   < 0 || layer   >= 3)                 continue;
        if (quick   < 0 || quick   >= kQuicksPerLayer)   continue;
        if (subBank < 0 || subBank >= kSubBanksPerQuick) continue;
        if (kind < 0 || kind > static_cast<int>(DynamicBankKind::Favourites))
            continue;
        int mod = 0;   // absent = Plain (pre-v26 shape)
        if (auto* v = eo->get_item_by_name("mod"))
            if (auto* t = v->get_string_value(true)) mod = std::atoi(t);
        if (mod < 0 || mod >= kSoftKeyModifierSets) continue;
        out.userQuicks[layer].quicks[quick].subBanks[subBank].dynamic[mod] =
            static_cast<DynamicBankKind>(kind);
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

void parseUf1SoftBanks_(wdl_json_element* root, Config& out)
{
    auto* arr = root->get_item_by_name("uf1_soft_banks");
    if (!arr || !arr->is_array() || !arr->m_array) return;
    const int n = arr->m_array->GetSize();
    for (int i = 0; i < n; ++i) {
        wdl_json_element* eo = arr->enum_item(i);
        if (!eo || !eo->is_object()) continue;
        int bank = -1, slot = -1;
        if (auto* v = eo->get_item_by_name("bank"))
            if (auto* s = v->get_string_value(true)) bank = std::atoi(s);
        if (auto* v = eo->get_item_by_name("slot"))
            if (auto* s = v->get_string_value(true)) slot = std::atoi(s);
        if (bank < 0 || bank >= kUf1SoftBankCount) continue;
        if (slot < 0 || slot >= kUf1SoftBankSlots) continue;
        auto* bodyObj = eo->get_item_by_name("body");
        if (!bodyObj || !bodyObj->is_object()) continue;
        Binding& bd = out.uf1SoftBanks[bank][slot];
        bd = Binding{};
        parseBindingBody_(bodyObj, bd);
    }
}

void parseUf1SoftBankDynamic_(wdl_json_element* root, Config& out)
{
    auto* arr = root->get_item_by_name("uf1_soft_bank_dynamic");
    if (!arr || !arr->is_array() || !arr->m_array) return;
    const int n = arr->m_array->GetSize();
    for (int i = 0; i < n; ++i) {
        wdl_json_element* eo = arr->enum_item(i);
        if (!eo || !eo->is_object()) continue;
        int bank = -1, kind = 0;
        if (auto* v = eo->get_item_by_name("bank"))
            if (auto* s = v->get_string_value(true)) bank = std::atoi(s);
        if (auto* v = eo->get_item_by_name("kind"))
            if (auto* s = v->get_string_value(true)) kind = std::atoi(s);
        if (bank < 0 || bank >= kUf1SoftBankCount) continue;
        if (kind < 0 || kind > static_cast<int>(DynamicBankKind::Favourites))
            continue;
        int mod = 0;   // absent = Plain (pre-v26 shape)
        if (auto* v = eo->get_item_by_name("mod"))
            if (auto* t = v->get_string_value(true)) mod = std::atoi(t);
        if (mod < 0 || mod >= kSoftKeyModifierSets) continue;
        out.uf1SoftBankDynamic[bank][mod] = static_cast<DynamicBankKind>(kind);
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
        if (auto* v = be->get_item_by_name("label_user"))
            if (auto* s = v->get_string_value(true))
                bd.labelIsUserSet = (std::strcmp(s, "true") == 0
                                  || std::strcmp(s, "1") == 0);

        // New-schema matrix. Both `short` and `long` are optional —
        // missing slots stay at default (Noop).
        if (auto* v = be->get_item_by_name("short"))
            parseMatrixRow_(v, bd.shortPress);
        if (auto* v = be->get_item_by_name("long"); v && v->is_object()) {
            bd.hasLongPress = true;
            parseMatrixRow_(v, bd.longPress);
        }
        if (auto* v = be->get_item_by_name("double"); v && v->is_object()) {
            bd.hasDoublePress = true;
            parseMatrixRow_(v, bd.doublePress);
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
        // Migration: the UF1's Strip Mode builtin was `uf1_strip_mode`; it is
        // now named after its UF8 counterpart (ssl_strip_mode_toggle) because
        // the two are one feature with a half per device, and the UF1 gained
        // the with-GUI variant too (Frank 2026-08-09).
        if (sp.type == ActionType::Builtin && sp.action == "uf1_strip_mode") {
            sp.action = "uf1_strip_mode_toggle";
        }
        // Migration: the UF1 nav cross moved from the zoom pad to the Jog-Mode
        // nav (Frank 2026-08-06). Rename the legacy zoom_* default in place on
        // those FIVE buttons only — the UF8 zoom pad keeps zoom_*.
        if (sp.type == ActionType::Builtin) {
            struct NavMig { ButtonId id; const char* from; const char* to; };
            static const NavMig kNavMig[] = {
                { ButtonId::Uf1NavUp,     "zoom_up",     "jog_nav_up"     },
                { ButtonId::Uf1NavDown,   "zoom_down",   "jog_nav_down"   },
                { ButtonId::Uf1NavLeft,   "zoom_left",   "jog_nav_left"   },
                { ButtonId::Uf1NavRight,  "zoom_right",  "jog_nav_right"  },
                { ButtonId::Uf1NavCentre, "zoom_center", "jog_nav_center" },
            };
            for (const auto& m : kNavMig)
                if (bid == m.id && sp.action == m.from) { sp.action = m.to; break; }
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
            fix(bd.doublePress[m]);
            for (auto& step : bd.doublePress[m].extraSteps) fix(step);
        }
    }
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
    parseSubBankDynamic_(root, out);
    parseBankPresets_(root, out);
    parseUf1SoftBanks_(root, out);
    parseUf1SoftBankDynamic_(root, out);
    return true;
}

} // namespace

void registerBuiltin(const char* name, BuiltinDescriptor desc)
{
    if (!name || !*name) return;
    g_builtins[name] = std::move(desc);
}

bool invokeBuiltin(const std::string& name, int param)
{
    if (name.empty()) return false;
    auto it = g_builtins.find(name);
    if (it == g_builtins.end() || !it->second.run) return false;
    it->second.run(/*firing*/ true, /*pressed*/ false, param);
    return true;
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
// v14 (2026-06-16): Shift+360 → learn_hud_toggle backfill (see
// upgradeBackfillShift360LearnHud_) — owned by main / the Learn-HUD work.
// v15 (2026-06-10): UF1 buttons routed through Bindings (new UF1 ButtonIds).
// Backfill Layer-1 transport + Solo/Cut/Sel factory defaults into older
// configs (upgradeBackfillUf1Buttons_) so the UF1 keeps working post-upgrade.
// (Was authored as v14 on the UF1 branch; renumbered to v15 on rebase since
// main already took v14 for the Shift+360 migration.)
// v16 (2026-07-31): UF1 controls that were hardcoded fall-throughs in
// onUf1Event (SHIFT, ENC push, FLIP, MASTER, 5-8, V-Pot pushes, Bank ◄ ►,
// Page ◄ ►) become real factory-default bindings. Same upgradeBackfillUf1Buttons_
// backfills their MISSING slots into older configs (find-guarded → idempotent,
// so re-running the v15 transport/nav fills is a no-op).
// v17 (2026-07-31): secondary transport +SHIFT = the 6 REAPER automation modes
// (SSL UF1 silk labels OFF/READ/WRT/TRIM/LTCH/TCH). upgradeBackfillUf1Automation_
// fills the empty Shift slots into older configs (leaves Plain + any user edit).
// v24 (2026-08-11): the UF1 Page ◄ ► gain a LONG press → uf1_dyn_bank_page, so
// paging INSIDE a dynamic soft-key bank has its own gesture instead of taking
// over "5-8" (which is the channel group again). upgradeBackfillUf1ArrowLongPress_
// fills only an UNTOUCHED long-press slot.
// 26: soft-key modifier SETS. sub_bank_dynamic / uf1_soft_bank_dynamic gained a
// "mod" key (absent reads as Plain, so old files need no migration step). This
// sat at 25 by accident for a few hours: the revert of the bank-swap experiment
// took the bump with it while the schema changes stayed.
// 27: sub_bank_leds gained the same "mod" key — a modifier set can wear its own
// Sub-Bank cell colour. Absent reads as Plain, and a set only writes an entry
// once it has claimed one, so an old file restores to exactly what it painted
// before: both sets on Plain's colour. No migration step.
// 30: the Fades jog mode. The per-mode nav block grew from 25 to 30 ids, so
// upgradeBackfillUf1Buttons_ runs once more to seed the five Fades keys on a
// config that predates them.
// 29: the per-mode nav ids seeded with labels ("ZOOM \xE2\x96\xB2" and friends) for the
// few hours v28 existed. The UF1 prints nothing beside the cross, so those are
// invisible AND they stop the label following the action once bound. Cleared for
// anyone who caught v28.
// 28: the nav cross gained 25 per-mode ButtonIds. Dispatch resolves the cross
// through the active Jog Mode, so an older config has NO binding for it until
// upgradeBackfillUf1Buttons_ seeds them — that pass now runs for < 28 too.
constexpr int kCurrentBindingsVersion = 31;

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

// v13 → v14: backfill Shift+360 → Learn-HUD on the UF8 + UC1 360 buttons for
// configs seeded before this default existed. Only fills an EMPTY Shift slot,
// so a user who already bound Shift+360 to something keeps it. Frank 2026-06-16.
void upgradeBackfillShift360LearnHud_(Config& c)
{
    auto fill = [](Layer& L, ButtonId id) {
        auto it = L.bindings.find(id);
        if (it == L.bindings.end()) return;   // button unbound entirely → leave
        auto& sp = it->second.shortPress[static_cast<int>(Modifier::Shift)];
        if (!sp.action.empty()) return;        // user already uses Shift+360
        sp.type   = ActionType::Builtin;
        sp.action = "learn_hud_toggle";
        sp.param  = 0;
        sp.label  = "LEARN";
    };
    fill(c.layers[0], ButtonId::Btn360);
    fill(c.layers[0], ButtonId::Uc1Btn360);
}

// ⇨ EVERY ACTION SLOT IN THE CONFIG, NOT JUST THE LAYER MAPS.
// A soft-key does not live in layers[L].bindings — it lives in one of the two
// soft-key stores. The cleanup migrations walked the layer maps only, so a
// soft-key left sitting on a retired builtin (quick_select_N, show_user_bank,
// uf1_transport) was never rewritten and fired nothing for ever, with the picker
// still showing a plausible name (Frank 2026-08-18).
// `fn` gets the layer the slot belongs to — the retire-quick migration needs it
// to compute the replacement jump. The UF1 bank store and the bank presets are
// global/coordinate-free, so they are handed layer 0, where their factory
// assignments live.
template <typename F>
void forEachActionSlot_(Config& c, F&& fn)
{
    auto doBinding = [&fn](int layer, Binding& bd) {
        for (int m = 0; m < kModifierCount; ++m) {
            fn(layer, bd.shortPress[m]);
            fn(layer, bd.longPress[m]);
            fn(layer, bd.doublePress[m]);
        }
    };
    for (int li = 0; li < 3; ++li) {
        for (auto& kv : c.layers[li].bindings) doBinding(li, kv.second);
        for (int qi = 0; qi < kQuicksPerLayer; ++qi)
            for (int bi = 0; bi < kSubBanksPerQuick; ++bi)
                for (int si = 0; si < kSlotsPerSubBank; ++si)
                    doBinding(li, c.userQuicks[li].quicks[qi]
                                     .subBanks[bi].slots[si]);
    }
    for (int b = 0; b < kUf1SoftBankCount; ++b)
        for (int si = 0; si < kUf1SoftBankSlots; ++si)
            doBinding(0, c.uf1SoftBanks[b][si]);
    for (auto& p : c.bankPresets)
        for (int si = 0; si < kSlotsPerSubBank; ++si)
            doBinding(0, p.slots[si]);
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
            snprintf(buf, sizeof(buf), "softkey_bank_%d",
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
                snprintf(buf, sizeof(buf), "layer_select_%d", p + 1);
                s.action = buf;
                s.param  = 0;
            } else {
                s = ActionSlot{};
            }
            return;
        }
    };
    forEachActionSlot_(c, migrateSlot);
    for (int li = 0; li < 3; ++li) {
        for (auto& kv : c.layers[li].bindings) {
            Binding& bd = kv.second;
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

// Rewrite bindings that reference the RETIRED UF1 builtins (removed
// 2026-07-30 — REAPER already owns those functions). A stale reference
// would dispatch to a missing builtin and silently no-op, which is exactly
// the "transport geht nicht" bug on configs seeded by the earlier build.
//   uf1_transport (param = Uf1TransportOp Play0/Stop1/Rec2/Rwd3/Ffw4)
//     → the stock REAPER action it now defaults to (label preserved).
//   uf1_solo_focused / uf1_mute_focused / uf1_select_focused
//     → cleared; Solo/Cut/Sel now fire natively from onUf1Event's direct
//       handler, so the binding is dead weight.
// Idempotent (once rewritten to Reaper / cleared, nothing matches) — runs
// on every load via the always-on sanitize pass.
void upgradeRetireUf1Builtins_(Config& c)
{
    auto migrateSlot = [](ActionSlot& s) {
        if (s.type != ActionType::Builtin) return;
        const std::string& a = s.action;
        if (a == "uf1_transport") {
            static const char* kAct[5] =
                { "1007", "1016", "1013", "40042", "40043" };
            const int p = s.param;
            s.type   = ActionType::Reaper;
            s.action = (p >= 0 && p <= 4) ? kAct[p] : "1007";
            s.param  = 0;
            return;
        }
        if (a == "uf1_solo_focused" || a == "uf1_mute_focused"
         || a == "uf1_select_focused") {
            s = ActionSlot{};
            return;
        }
    };
    forEachActionSlot_(c, [&](int /*layer*/, ActionSlot& s) { migrateSlot(s); });
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
// v14→v15 (2026-06-10): UF1 buttons moved into the shared Bindings system.
// Transport + Cycle/Click default to stock REAPER actions and are backfilled
// here if an old config lacks them (missing slots only, so a user's own
// customisation survives). Solo/Cut/Sel are NOT backfilled — they fire
// natively on the focused track from onUf1Event's direct handler (Frank
// 2026-07-30, don't reinvent REAPER's own functions), so they can't go dead.
void upgradeBackfillUf1Buttons_(Config& c)
{
    Layer& L1 = c.layers[0];
    auto fillBuiltin = [&](ButtonId id, const char* action, const char* label,
                           Behavior beh = Behavior::Momentary, int param = 0) {
        if (L1.bindings.find(id) != L1.bindings.end()) return;
        Binding bd;
        bd.behavior = beh;
        bd.label    = label;
        auto& sp = bd.shortPress[static_cast<int>(Modifier::Plain)];
        sp.type   = ActionType::Builtin;
        sp.action = action;
        sp.param  = param;
        L1.bindings[id] = bd;
    };
    auto fillReaper = [&](ButtonId id, const char* actionId, const char* label) {
        if (L1.bindings.find(id) != L1.bindings.end()) return;
        Binding bd;
        bd.behavior = Behavior::Momentary;
        bd.label    = label;
        auto& sp = bd.shortPress[static_cast<int>(Modifier::Plain)];
        sp.type   = ActionType::Reaper;
        sp.action = actionId;
        L1.bindings[id] = bd;
    };
    // Transport + Cycle/Click backfill to stock REAPER actions (Frank
    // 2026-07-30). Solo/Cut/Sel intentionally NOT filled — fired natively
    // on the focused track from onUf1Event's direct handler, not a binding.
    fillReaper(ButtonId::Uf1Play, "1007",  "PLAY");
    fillReaper(ButtonId::Uf1Stop, "1016",  "STOP");
    fillReaper(ButtonId::Uf1Rec,  "1013",  "REC");
    fillReaper(ButtonId::Uf1Rwd,  "40042", "RWD");
    fillReaper(ButtonId::Uf1Ffw,  "40043", "FFW");
    fillReaper(ButtonId::Uf1Cycle, "1068",  "CYCLE");
    fillReaper(ButtonId::Uf1Click, "40364", "CLICK");
    fillBuiltin(ButtonId::Uf1Btn360, "uf1_time_display_step", "360\xC2\xB0");
    fillBuiltin(ButtonId::Uf1NavUp,     "jog_nav_up",     "JOG \xE2\x96\xB2");
    fillBuiltin(ButtonId::Uf1NavDown,   "jog_nav_down",   "JOG \xE2\x96\xBC");
    fillBuiltin(ButtonId::Uf1NavLeft,   "jog_nav_left",   "JOG \xE2\x97\x82");
    fillBuiltin(ButtonId::Uf1NavRight,  "jog_nav_right",  "JOG \xE2\x96\xB8");
    fillBuiltin(ButtonId::Uf1NavCentre, "jog_nav_center", "FOCUS");
    // ⇨ AND THE 30 PER-MODE NAV IDS (v27→v28 for the first 25, v29→v30 for
    // the five Fades ones).
    // A config written before either of those lacks the ids entirely, and since
    // the dispatch resolves the cross through the active mode, a missing entry
    // means a DEAD cross — not a fallback. Backfilled from the same table the
    // factory seed uses, and only where the slot is missing, so anyone who has
    // already bound one keeps it.
    for (const auto& n : kNavSeed) {
        const bool wasMissing = L1.bindings.find(n.id) == L1.bindings.end();
        fillBuiltin(n.id, n.action, n.label, n.beh);
        // Only on a slot we just created — never overwrite a Shift the user set.
        if (wasMissing && n.shiftAction) {
            auto& sh = L1.bindings[n.id].shortPress[static_cast<int>(Modifier::Shift)];
            sh.type   = ActionType::Builtin;
            sh.action = n.shiftAction;
        }
    }
    // v15→v16 (2026-07-31): controls that were hardcoded fall-throughs in
    // onUf1Event are now real factory-default bindings. Backfill the MISSING
    // slots on older configs (a user's own customisation, if any, survives;
    // a slot the user explicitly cleared to empty stays a stored empty entry
    // and so is NOT refilled — intended, since the hardcoded default is gone).
    fillBuiltin(ButtonId::Uf1Shift,       "mod_shift",               "SHIFT",       Behavior::Hold);
    fillBuiltin(ButtonId::Uf1ChannelPush, "show_focused_plugin_gui", "ENC PUSH");
    fillBuiltin(ButtonId::Uf1Flip,        "uf1_flip",                "FLIP",        Behavior::Toggle);
    fillBuiltin(ButtonId::Uf1Master,      "uf1_master",              "MASTER",      Behavior::Toggle);
    fillBuiltin(ButtonId::Uf1FiveToEight, "uf1_five_to_eight",       "5-8");
    fillBuiltin(ButtonId::Uf1Vpot1Push, "uf1_vpot_reset", "V-POT 1 PUSH", Behavior::Momentary, 0);
    fillBuiltin(ButtonId::Uf1Vpot2Push, "uf1_vpot_reset", "V-POT 2 PUSH", Behavior::Momentary, 1);
    fillBuiltin(ButtonId::Uf1Vpot3Push, "uf1_vpot_reset", "V-POT 3 PUSH", Behavior::Momentary, 2);
    fillBuiltin(ButtonId::Uf1Vpot4Push, "uf1_vpot_reset", "V-POT 4 PUSH", Behavior::Momentary, 3);
    fillBuiltin(ButtonId::Uf1BankLeft,  "uf1_bank_step", "BANK \xE2\x97\x82", Behavior::Momentary, -1);
    fillBuiltin(ButtonId::Uf1BankRight, "uf1_bank_step", "BANK \xE2\x96\xB8", Behavior::Momentary, +1);
    fillBuiltin(ButtonId::Uf1ArrowLeft, "uf1_page_step", "PAGE \xE2\x97\x82", Behavior::Momentary, -1);
    fillBuiltin(ButtonId::Uf1ArrowRight,"uf1_page_step", "PAGE \xE2\x96\xB8", Behavior::Momentary, +1);
}

// v16→v17: seed the SHIFT slots of the 6 secondary-transport buttons with the
// automation modes (SSL UF1 silk labels OFF/READ/WRT/TRIM/LTCH/TCH). Fills ONLY
// an EMPTY Shift slot, so a user's own Shift edit — and the Plain slot — survive.
void upgradeBackfillUf1Automation_(Config& c)
{
    Layer& L1 = c.layers[0];
    auto fillShift = [&](ButtonId id, const char* action, const char* label) {
        Binding& bd = L1.bindings[id];   // default-creates the binding if missing
        auto& sp = bd.shortPress[static_cast<int>(Modifier::Shift)];
        if (sp.type != ActionType::Noop || !sp.action.empty()) return;  // already assigned
        sp.type   = ActionType::Builtin;
        sp.action = action;
        sp.label  = label;
    };
    fillShift(ButtonId::Uf1SecLeft,  "auto_off",   "OFF");
    fillShift(ButtonId::Uf1SecRight, "auto_read",  "READ");
    fillShift(ButtonId::Uf1Cycle,    "auto_write", "WRT");
    fillShift(ButtonId::Uf1Click,    "auto_trim",  "TRIM");
    fillShift(ButtonId::Uf1SecKey1,  "auto_latch", "LTCH");
    fillShift(ButtonId::Uf1SecKey2,  "auto_touch", "TCH");
}

// v17→v18 (2026-07-31): the channel encoder gains a LONG-PRESS factory
// default → uf1_encoder_ch_select (snap back to Channel Select). Backfill
// only when the user hasn't already set their own long-press on the push —
// explicit assignments always win, and a slot the user deliberately cleared
// stays cleared (hasLongPress off with an empty Plain slot is left alone,
// matching the "don't resurrect a cleared default" rule).
void upgradeBackfillUf1EncoderLong_(Config& c)
{
    Layer& L1 = c.layers[0];
    auto it = L1.bindings.find(ButtonId::Uf1ChannelPush);
    // Missing entirely → default-create so the push still gets the long-press
    // (mirrors upgradeBackfillUf1Buttons_ default-create behaviour).
    Binding& bd = (it == L1.bindings.end())
                      ? L1.bindings[ButtonId::Uf1ChannelPush]
                      : it->second;
    if (bd.hasLongPress) return;   // user (or a prior run) already set one
    bd.hasLongPress = true;
    auto& lp  = bd.longPress[static_cast<int>(Modifier::Plain)];
    lp.type   = ActionType::Builtin;
    lp.action = "uf1_encoder_ch_select";
}

// v18→v19 (2026-08-03): SEL becomes first-class bindable on UF1 + UF8.
// The single-press select stays NATIVE (unchanged); this only seeds the
// new DOUBLE-press factory default → `show_fx_chain` (open the FX chain
// of the just-selected track). Seeded on Layer 1 (the UF8/UF1 default
// layer) for both the UF1 SEL (ButtonId::Uf1Sel) and the shared UF8 SEL
// (ButtonId::Uf8Select). Only fills an EMPTY double slot — an explicit
// user assignment (or a slot they deliberately cleared) is never
// resurrected (matches upgradeBackfillUf1EncoderLong_).
// v19→v20 (2026-08-10): the UF1's single SOFT key (above the fader display)
// gets a factory default of Pin Set. It shipped unbound, so a config written
// before today carries either nothing for it or an assignment its owner made
// on purpose. Fill ONLY the empty case — an own binding, or a slot deliberately
// cleared to Noop with an action string, is left exactly as it is.
void upgradeBackfillUf1SoftKey_(Config& c)
{
    Layer& L1 = c.layers[0];
    Binding& bd = L1.bindings[ButtonId::Uf1ChannelSoftKey];   // default-creates
    auto& sp = bd.shortPress[static_cast<int>(Modifier::Plain)];
    if (sp.type != ActionType::Noop || !sp.action.empty()) return;
    sp.type     = ActionType::Builtin;
    sp.action   = "temp_selset_pin_uf1_channel";
    // ⇨ THE KEY'S NAME, NOT THE SET'S. The factory seed puts it in
    // Binding::label (mkBuiltin does), this backfill put it in ActionSlot::label
    // — and the two mean different things: a slot label belongs to a modifier
    // SET, a Binding label names the key. On a config that came through here the
    // auto-label refresh then had nothing of its own to replace, so rebinding
    // the key left "PIN SET" on the display over an action it no longer fires:
    // forum 4.2 all over again, for pre-v20 configs only.
    bd.label    = "PIN SET";
    bd.behavior = Behavior::Toggle;
}

// v20→v21 (2026-08-10, same day): the SOFT key's v20 seed was
// temp_selset_recall, which only flips the clutch — press it on a fresh set and
// nothing is pinned, because nothing is IN the set. The seed is now
// temp_selset_pin_uf1_channel, which pins the shown channel AND engages in one
// press. Rewrites ONLY the exact v20 seed on the exact button: v20 existed for
// about an hour and never shipped, so that value there is ours, not a choice.
// Any other action on the key — including one the user picked — is left alone.
void upgradeUf1SoftKeyPinChannel_(Config& c)
{
    Layer& L1 = c.layers[0];
    auto it = L1.bindings.find(ButtonId::Uf1ChannelSoftKey);
    if (it == L1.bindings.end()) return;
    auto& sp = it->second.shortPress[static_cast<int>(Modifier::Plain)];
    if (sp.type != ActionType::Builtin) return;
    if (sp.action != "temp_selset_recall") return;
    sp.action = "temp_selset_pin_uf1_channel";
}

// v21→v22 (2026-08-10): key 1 (the one under the SOLO CLR caption) gains its
// SSL factory assignment, REAPER action 40340 "Unsolo all tracks". Backfill
// ONLY when the plain slot is still empty — a key the user has bound is theirs,
// and this runs on every existing config. The Shift slot (LTCH / auto_latch) is
// untouched.
// v28→v29 (2026-08-18): drop the labels v28 seeded onto the nav cross. Only
// where the user has not claimed one — a name someone typed is theirs, even on a
// key that cannot show it.
// v30→v31 (2026-08-20): the per-mode nav ids exist on these configs but carry
// the default white ACTIVE colour, because the green the cross actually lit with
// was a constant in the painter. Now that the painter reads the binding, an
// untouched config would light white and the editor would agree with it — right,
// but not what anyone has been looking at. So: paint the factory green in, and
// ONLY where the binding still carries the factory white, so a colour someone
// chose is never overwritten.
void upgradeUf1NavActiveColour_(Config& c)
{
    Layer& L1 = c.layers[0];
    for (const auto& n : kNavSeed) {
        auto it = L1.bindings.find(n.id);
        if (it == L1.bindings.end()) continue;
        Binding& bd = it->second;
        if (bd.color[0] != 0xFF || bd.color[1] != 0xFF || bd.color[2] != 0xFF)
            continue;                                  // the user picked one
        bd.color[0] = 0x00; bd.color[1] = 0xFF; bd.color[2] = 0x66;
    }
}

void upgradeClearUf1NavLabels_(Config& c)
{
    Layer& L1 = c.layers[0];
    for (const auto& n : kNavSeed) {
        auto it = L1.bindings.find(n.id);
        if (it == L1.bindings.end()) continue;
        if (it->second.labelIsUserSet) continue;
        it->second.label.clear();
    }
}

void upgradeBackfillUf1SoloClear_(Config& c)
{
    Layer& L1 = c.layers[0];
    Binding& bd = L1.bindings[ButtonId::Uf1SecKey1];   // default-creates
    auto& sp = bd.shortPress[static_cast<int>(Modifier::Plain)];
    if (sp.type != ActionType::Noop || !sp.action.empty()) return;
    sp.type   = ActionType::Reaper;
    sp.action = "40340";
    sp.label  = "SOLO CLR";
}

// v22→v23 (2026-08-10): Fine stops being a hardcoded special case on key 2 and
// becomes the `uf1_fine_toggle` builtin, so it shows up in the editor with a
// label and can be moved like anything else. Backfill only into an empty plain
// slot. Frank's reason for keeping it THERE by default: the firmware prints
// "FINE CTRL 2" above that key and we cannot change that text.
void upgradeBackfillUf1Fine_(Config& c)
{
    Layer& L1 = c.layers[0];
    Binding& bd = L1.bindings[ButtonId::Uf1SecKey2];   // default-creates
    auto& sp = bd.shortPress[static_cast<int>(Modifier::Plain)];
    if (sp.type != ActionType::Noop || !sp.action.empty()) return;
    sp.type     = ActionType::Builtin;
    sp.action   = "uf1_fine_toggle";
    sp.label    = "FINE";
    bd.behavior = Behavior::Toggle;
}

// v23→v24 (2026-08-11): the UF1 Page ◄ ► gain a LONG press → uf1_dyn_bank_page,
// so paging INSIDE a dynamic soft-key bank stops fighting "5-8" for the key.
// Backfill ONLY into an untouched long-press slot — a long press the user has
// assigned is theirs.
void upgradeBackfillUf1ArrowLongPress_(Config& c)
{
    Layer& L1 = c.layers[0];
    auto seedLong = [&](ButtonId id, int param) {
        Binding& bd = L1.bindings[id];        // default-creates if missing
        if (bd.hasLongPress) return;          // user / prior run already set it
        auto& lp = bd.longPress[static_cast<int>(Modifier::Plain)];
        if (lp.type != ActionType::Noop || !lp.action.empty()) return;
        bd.hasLongPress = true;
        lp.type   = ActionType::Builtin;
        lp.action = "uf1_dyn_bank_page";
        lp.param  = param;
    };
    seedLong(ButtonId::Uf1ArrowLeft,  -1);
    seedLong(ButtonId::Uf1ArrowRight, +1);
}

// v24→v25 (2026-08-18): `labelIsUserSet` is new, so for every config written
// before it the label's ORIGIN has to be inferred once — and it can only be
// inferred by comparing against the factory seed. Anything that differs was
// typed by a human and must never be auto-refreshed from the bound action
// again. Anything identical to the seed is ours, and stays refreshable.
//
// Getting this wrong in the safe-looking direction (mark nothing) would have
// silently renamed keys people had named themselves on their first rebind, so
// the comparison is worth the extra Config. That Config is HEAP-allocated —
// ~940 KB on the stack is an instant Windows load crash
// ([[windows-stack-overflow-config]]), which this project has now paid for
// twice.
void upgradeMarkUserLabels_(Config& c)
{
    auto facPtr = std::make_unique<Config>();
    Config& fac = *facPtr;
    seedFactoryDefaults_(fac);

    auto mark = [](Binding& bd, const Binding* factory) {
        if (bd.label.empty()) return;
        if (factory && factory->label == bd.label) return;   // ours, not theirs
        bd.labelIsUserSet = true;
    };

    for (int li = 0; li < 3; ++li) {
        const auto& facL = fac.layers[li].bindings;
        for (auto& kv : c.layers[li].bindings) {
            const auto it = facL.find(kv.first);
            mark(kv.second, it == facL.end() ? nullptr : &it->second);
        }
    }
    for (int b = 0; b < kUf1SoftBankCount; ++b)
        for (int s = 0; s < kUf1SoftBankSlots; ++s)
            mark(c.uf1SoftBanks[b][s], &fac.uf1SoftBanks[b][s]);
    for (int li = 0; li < 3; ++li)
        for (int q = 0; q < kQuicksPerLayer; ++q)
            for (int sb = 0; sb < kSubBanksPerQuick; ++sb)
                for (int sl = 0; sl < kSlotsPerSubBank; ++sl)
                    mark(c.userQuicks[li].quicks[q].subBanks[sb].slots[sl],
                         &fac.userQuicks[li].quicks[q].subBanks[sb].slots[sl]);
}

void upgradeBackfillSelDouble_(Config& c)
{
    Layer& L1 = c.layers[0];
    auto seedDouble = [&](ButtonId id) {
        Binding& bd = L1.bindings[id];   // default-creates if missing
        if (bd.hasDoublePress) return;   // user / prior run already set it
        auto& dp = bd.doublePress[static_cast<int>(Modifier::Plain)];
        if (dp.type != ActionType::Noop || !dp.action.empty()) return;
        bd.hasDoublePress = true;
        dp.type   = ActionType::Builtin;
        dp.action = "show_fx_chain";
        dp.label  = "FX Chain";
    };
    seedDouble(ButtonId::Uf1Sel);
    seedDouble(ButtonId::Uf8Select);
}

// Both passes preserve color, brightness, inactive*, label, and
// any modifier-row / longPress slots. They only touch
// shortPress[Plain].
void upgradeSanitizeBankAndQuickActions_(Config& c)
{
    FILE* lg = std::fopen(uf8::logPath("rea_sixty.log").c_str(), "a");
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
                snprintf(buf, sizeof(buf),
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
    // Diagnostic: write a raw breadcrumb that bypasses crumb_() entirely,
    // so we can tell whether the crash is in the call to crumb_ (e.g.
    // inline-in-anon-namespace weirdness from e2d4866) or in entering
    // load() at all. If LOAD_ENTERED_RAW appears but no other crumbs do,
    // crumb_ itself is the failure point at this call site.
    {
#ifdef _WIN32
        char tmp[260] = {0};
        char path[260] = {0};
        if (GetTempPathA(260, tmp)) {
            snprintf(path, sizeof(path), "%srea_sixty_init.log", tmp);
        } else {
            std::strcpy(path, "C:\\Windows\\Temp\\rea_sixty_init.log");
        }
        FILE* f = std::fopen(path, "a");
#else
        FILE* f = std::fopen(uf8::logPath("rea_sixty_init.log").c_str(), "a");
#endif
        if (f) { std::fprintf(f, "  bindings:LOAD_ENTERED_RAW\n"); std::fclose(f); }
    }
    crumb_("load enter");
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    crumb_("got mutex");

    std::string contents;
    const std::string cp = configPath_();
    crumb_(cp.c_str());
    if (readFile_(cp, contents) && !contents.empty()) {
        crumb_("file read OK, entering parse/upgrade");
        // Heap-allocate: sizeof(Config) ≈ 600 KB (3 layers × 3 quicks ×
        // 6 sub-banks × 8 bindings × ~1.4 KB each). A stack local blows
        // past Windows's 1 MB main-thread stack in the function
        // prologue before any code runs — manifested as silent
        // STATUS_STACK_OVERFLOW on Windows only (macOS thread stack is
        // larger).
        auto tmpPtr = std::make_unique<Config>();
        Config& tmp = *tmpPtr;
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
            if (tmp.version < 14) {
                upgradeBackfillShift360LearnHud_(tmp);
            }
            // Runs for < 28 as well: v27 configs predate the per-mode nav ids,
            // and without them the cross has no binding to find at all.
            if (tmp.version < 29) {
                upgradeClearUf1NavLabels_(tmp);
            }
            // v29→v30 (2026-08-19): the Fades jog mode adds five more per-mode
            // nav ids, and a v29 config has none of them — the cross would be
            // DEAD in the new mode, not merely unbound. Same reason v27 needed
            // it, so the same backfill runs again: fillBuiltin bails on a slot
            // that already exists, so everything a v29 config carries is left
            // exactly as the user has it and only the five new ids get seeded.
            if (tmp.version < 30) {
                upgradeBackfillUf1Buttons_(tmp);
            }
            // v30→v31 (2026-08-20): give the per-mode nav cross its ACTIVE
            // colour in the binding, where the editor can see it. Runs after the
            // backfill above so the ids it seeds are covered too.
            if (tmp.version < 31) {
                upgradeUf1NavActiveColour_(tmp);
            }
            if (tmp.version < 17) {
                upgradeBackfillUf1Automation_(tmp);
            }
            if (tmp.version < 18) {
                upgradeBackfillUf1EncoderLong_(tmp);
            }
            if (tmp.version < 19) {
                upgradeBackfillSelDouble_(tmp);
            }
            if (tmp.version < 20) {
                upgradeBackfillUf1SoftKey_(tmp);
            }
            if (tmp.version < 21) {
                upgradeUf1SoftKeyPinChannel_(tmp);
            }
            if (tmp.version < 22) {
                upgradeBackfillUf1SoloClear_(tmp);
            }
            if (tmp.version < 23) {
                upgradeBackfillUf1Fine_(tmp);
            }
            if (tmp.version < 24) {
                upgradeBackfillUf1ArrowLongPress_(tmp);
            }
            if (tmp.version < 25) {
                upgradeMarkUserLabels_(tmp);
            }
            // Belt-and-suspenders sanitize. Always runs, regardless of
            // version, so any stale references to removed builtins
            // (quick_select_X / user_domain_X / show_user_bank /
            // layer_select param-form) get rewritten even in configs
            // that somehow ended up past v10 without the action-name
            // migration sticking. Idempotent on already-migrated data.
            upgradeRetireQuickSelect_(tmp);
            // Same, for the retired UF1 builtins (uf1_transport /
            // uf1_*_focused) — fixes transport buttons stuck on the removed
            // uf1_transport builtin in configs seeded by the earlier build.
            upgradeRetireUf1Builtins_(tmp);

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
            if (FILE* lg = std::fopen(uf8::logPath("rea_sixty.log").c_str(), "a")) {
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
    crumb_("first-run path: seedFactoryDefaults_");
    seedFactoryDefaults_(g_cfg);
    crumb_("first-run path: ensureConfigDir_");
    ensureConfigDir_();
    crumb_("first-run path: writeFile_");
    writeFile_(configPath_(), serialize(g_cfg));
    crumb_("first-run path: done");
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

std::string configPath() { return configPath_(); }

bool importFrom(const std::string& path)
{
    std::string contents;
    if (!readFile_(path, contents) || contents.empty()) return false;

    // Heap-allocate — see load() for sizeof(Config) rationale.
    auto tmpPtr = std::make_unique<Config>();
    Config& tmp = *tmpPtr;
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
    return writeFile_(path, serializeOneLayer_(g_cfg, layer));
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

    // v2 files also carry the layer's 144 top-soft-key slots (+ sub-bank LEDs
    // and dynamic kinds). Parse them onto a scratch Config's layer 0 — the
    // parsers write into a Config, and Config is far too big for the stack
    // (Windows load crash, see load()).
    // A v1 file says NOTHING about the soft-keys, so it must leave the target
    // layer's alone; an EMPTY v2 array means "this layer has none" and must
    // clear them. Hence the presence check rather than a count check.
    const bool hasSoftKeys =
        root->get_item_by_name("user_quicks")     != nullptr
     || root->get_item_by_name("sub_bank_leds")   != nullptr
     || root->get_item_by_name("sub_bank_dynamic") != nullptr;
    std::unique_ptr<Config> quicksPtr;
    if (hasSoftKeys) {
        quicksPtr = std::make_unique<Config>();
        parseUserQuicks_(root, *quicksPtr, /*forceLayer*/ 0);
        parseSubBankLeds_(root, *quicksPtr, /*forceLayer*/ 0);
        parseSubBankDynamic_(root, *quicksPtr, /*forceLayer*/ 0);
    }

    {
        std::lock_guard<std::mutex> lk(g_cfgMutex);
        g_cfg.layers[layer] = std::move(tmp);
        if (quicksPtr)
            g_cfg.userQuicks[layer] = std::move(quicksPtr->userQuicks[0]);
        ensureConfigDir_();
        writeFile_(configPath_(), serialize(g_cfg));
        g_bindingsGen.fetch_add(1, std::memory_order_relaxed);
    }
    return true;
}

// ---- UC1-only bindings export / import ----------------------------------
// The UC1 has no user-facing layer concept, so its Save/Load operates on
// just the five UC1 controls of one layer (the active layer in practice).
// The file is a small {"type":"uc1","bindings":{…}} container that reuses
// the per-binding serialiser; import replaces only the UC1 controls in the
// target layer, leaving UF8 bindings and every other layer untouched.
static bool isUc1Id_(ButtonId id)
{
    return id == ButtonId::Uc1Encoder1 || id == ButtonId::Uc1Encoder2
        || id == ButtonId::Uc1Encoder2Push || id == ButtonId::Uc1Magnifier
        || id == ButtonId::Uc1Btn360;
}

bool exportUc1To(int layer, const std::string& path)
{
    if (layer < 0 || layer > 2) return false;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    const Layer& L = g_cfg.layers[layer];
    std::ostringstream os;
    os << "{\n  \"version\": 1,\n  \"type\": \"uc1\",\n  \"bindings\": {";
    bool first = true;
    // Stable order, same reasoning as serializeLayerBody_.
    for (const auto& e : kNames) {
        if (!isUc1Id_(e.id) || !e.name || !*e.name) continue;
        const auto it = L.bindings.find(e.id);
        if (it == L.bindings.end()) continue;
        if (!first) os << ",";
        first = false;
        os << "\n    \"" << e.name << "\": {";
        serializeBindingBody_(it->second, os);
        os << "}";
    }
    if (!first) os << "\n  ";
    os << "}\n}\n";
    return writeFile_(path, os.str());
}

bool importUc1From(int layer, const std::string& path)
{
    if (layer < 0 || layer > 2) return false;
    std::string contents;
    if (!readFile_(path, contents) || contents.empty()) return false;

    wdl_json_parser p;
    wdl_json_element* root = p.parse(contents.c_str(),
                                     static_cast<int>(contents.size()));
    if (!root || !root->is_object()) return false;

    auto* t = root->get_item_by_name("type");
    const char* ts = t ? t->get_string_value() : nullptr;
    if (!ts || std::strcmp(ts, "uc1") != 0) return false;

    // parseLayer_ reads the "bindings" child of the passed object, so the
    // root container (which holds "bindings" directly) works as-is.
    Layer tmp;
    if (!parseLayer_(root, tmp)) return false;

    {
        std::lock_guard<std::mutex> lk(g_cfgMutex);
        Layer& L = g_cfg.layers[layer];
        for (ButtonId id : { ButtonId::Uc1Encoder1, ButtonId::Uc1Encoder2,
                             ButtonId::Uc1Encoder2Push, ButtonId::Uc1Magnifier,
                             ButtonId::Uc1Btn360 }) {
            L.bindings.erase(id);
            auto it = tmp.bindings.find(id);
            if (it != tmp.bindings.end()) L.bindings[id] = it->second;
        }
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
    if (FILE* lg = std::fopen(uf8::logPath("rea_sixty.log").c_str(), "a")) {
        std::fprintf(lg,
            "[midi] bound device '%s' not in current MIDI output list "
            "(unplugged or renamed)\n",
            a.midiDevice.c_str());
        std::fclose(lg);
    }
}

// Keyboard-macro chords queued for main-thread delivery. runStep_ runs on the
// libusb input thread (single-step slots) or the main thread (chain steps via
// tickPending); SWELL window messages must be posted on the main thread, so a
// Keyboard step parks its chords here and tickPending() drains + sends those
// whose fireAt has elapsed. A macro = several entries each with a pre-delay, so
// they carry a scheduled time, not just a chord. Frank 2026-06-29.
struct PendingKey {
    std::chrono::steady_clock::time_point fireAt;
    keymacro::KeyChord                    chord;
};
std::mutex               g_keyMutex;
std::vector<PendingKey>  g_pendingKeys;

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
        case ActionType::Keyboard: {
            // Fire on the press edge only (like MIDI). The action string is a
            // self-contained macro: a list of (pre-delay ms, chord) entries.
            // Schedule each entry at the cumulative delay from now; tickPending
            // sends them on the main thread as their times elapse (SWELL window
            // messages can't go out from the input thread). Frank 2026-06-29.
            if (!firing) break;
            const auto entries = keymacro::parseMacro(a.action);
            const auto base = std::chrono::steady_clock::now();
            int cum = 0;
            std::lock_guard<std::mutex> lk(g_keyMutex);
            for (const auto& e : entries) {
                cum += (e.delayMs < 0 ? 0 : e.delayMs);
                keymacro::KeyChord kc;
                if (!keymacro::parseChord(e.chord, kc)) continue;   // skip invalid
                g_pendingKeys.push_back(
                    { base + std::chrono::milliseconds(cum), kc });
            }
            break;
        }
        case ActionType::Builtin: {
            auto it = g_builtins.find(a.action);
            if (it != g_builtins.end()) {
                // Step-aware handlers get the full ActionStep so they
                // can read stepValue / wrap / label — used by the
                // stepped-builtin family (fx_param_inc / fx_param_dec).
                // Plain handlers stay on the legacy `param`-only path.
                if (it->second.runWithStep) {
                    it->second.runWithStep(firing, pressed, a);
                    break;
                }
                if (it->second.run) {
                    it->second.run(firing, pressed, a.param);
                    break;
                }
            }
            if (firing) {
                // Diagnostic: an action name references a builtin that
                // isn't registered. Usually means a stale dead-builtin
                // reference (e.g. quick_select_3 in a not-fully-migrated
                // config) that dispatch silently no-op'd. Logging once
                // per firing gives a paper trail for "press did nothing"
                // bug reports.
                if (FILE* lg = std::fopen(uf8::logPath("rea_sixty.log").c_str(), "a")) {
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

    // Deliver keyboard-macro chords whose scheduled time has elapsed; keep the
    // rest queued (a macro's later entries fire on subsequent ticks per their
    // delays). Sending here keeps SWELL messages on the main thread. See
    // g_pendingKeys.
    std::vector<keymacro::KeyChord> dueKeys;
    {
        std::lock_guard<std::mutex> lk(g_keyMutex);
        const auto kn = std::chrono::steady_clock::now();
        std::vector<PendingKey> keep;
        keep.reserve(g_pendingKeys.size());
        for (auto& pk : g_pendingKeys) {
            if (pk.fireAt <= kn) dueKeys.push_back(pk.chord);
            else                 keep.push_back(pk);
        }
        g_pendingKeys.swap(keep);
    }
    for (const auto& ch : dueKeys) keymacro::sendChordToReaper(ch);
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

void setKeyboardShiftHeld(bool held) { g_modShiftKbHeld.store(held); }
void setKeyboardCmdHeld  (bool held) { g_modCmdKbHeld  .store(held); }
void setKeyboardCtrlHeld (bool held) { g_modCtrlKbHeld .store(held); }

bool modifierHeld(Modifier m)
{
    switch (m) {
        case Modifier::Shift: return g_modShiftHeld.load()
                                  || g_modShiftKbHeld.load();
        case Modifier::Cmd:   return g_modCmdHeld.load()
                                  || g_modCmdKbHeld.load();
        case Modifier::Ctrl:  return g_modCtrlHeld.load()
                                  || g_modCtrlKbHeld.load();
        case Modifier::Plain: return false;
    }
    return false;
}

Modifier currentModifierSnapshot()
{
    // Precedence Ctrl > Cmd > Shift > Plain. Most-specific-modifier-wins
    // matches typical keyboard-shortcut conventions; the editor will let
    // the user route Ctrl+Shift+button via the Ctrl slot only.
    if (g_modCtrlHeld.load()  || g_modCtrlKbHeld.load())  return Modifier::Ctrl;
    if (g_modCmdHeld.load()   || g_modCmdKbHeld.load())   return Modifier::Cmd;
    if (g_modShiftHeld.load() || g_modShiftKbHeld.load()) return Modifier::Shift;
    return Modifier::Plain;
}

// Soft-key banks only ever see Plain and Shift — see the header for why.
//
// ⚠ KEYBOARD SHIFT COUNTS, exactly like the FINE / SHIFT key. That is what
// "Keyboard Shift acts as Shift modifier" (Settings, Behaviour, Keyboard, ON by
// default) is FOR, and it holds for every modifier, not just for bindings.
// An audit called the resulting label switching a bug and I briefly cut the
// keyboard out of this path; Frank corrected it the same hour. The audit
// described a symptom correctly and diagnosed it wrongly — the switching is the
// feature working.
Modifier heldBankModifier()
{
    const Modifier m = currentModifierSnapshot();
    return (m == Modifier::Shift) ? Modifier::Shift : Modifier::Plain;
}

// -1 = nobody is pinning; otherwise the Settings editor's latched set. See the
// header. Written from the render thread, read from the USB input thread.
static std::atomic<int> g_bankModifierPin{-1};

void setBankModifierPin(int mod)
{
    const bool valid = (mod == static_cast<int>(Modifier::Plain)
                     || mod == static_cast<int>(Modifier::Shift));
    g_bankModifierPin.store(valid ? mod : -1);
}

Modifier bankModifierSnapshot()
{
    const int pin = g_bankModifierPin.load();
    if (pin >= 0) return static_cast<Modifier>(pin);
    return heldBankModifier();
}


// Long-press slot resolution with PLAIN fallback. The arm records the
// modifier held AT PRESS, so a long-press whose action lives only in the
// Plain slot would resolve to an empty longPress[Shift] when the user
// holds SHIFT before pressing the button and fire nothing. Prefer the
// modifier-specific long slot; if it's empty, fall back to the Plain
// long slot — so a Plain-only long fires whatever modifier is held,
// regardless of press order ("machs wie uf8"). A modifier-specific long
// (e.g. FLIP longPress[Shift]=recv_this) is unaffected: its slot is
// non-empty, so no fallback happens. Short press is untouched — it still
// fires shortPress[m] for the held modifier.
static const ActionSlot& effectiveLongSlot_(const Binding& bd, int m)
{
    const ActionSlot& s = bd.longPress[m];
    if (!slotIsEmpty_(s)) return s;
    return bd.longPress[static_cast<int>(Modifier::Plain)];
}

// Same Plain-fallback resolution for the double-press slot.
static const ActionSlot& effectiveDoubleSlot_(const Binding& bd, int m)
{
    const ActionSlot& s = bd.doublePress[m];
    if (!slotIsEmpty_(s)) return s;
    return bd.doublePress[static_cast<int>(Modifier::Plain)];
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
                bool tracked;
                {   // brief g_pressMx read; g_cfgMutex is the outer lock
                    std::lock_guard<std::mutex> lk(g_pressMx);
                    tracked = g_pressStart.find(k)     != g_pressStart.end()
                           || g_longPressStart.find(k) != g_longPressStart.end();
                }
                if (!tracked) continue;
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

    // ── Double-press (additive, every behaviour) ────────────────────────
    // A 2nd press of THIS (layer, button) within kDoubleClickMs of the
    // previous press fires doublePress[mod] IN ADDITION to the normal
    // single-press handling below — the single press still fires; the
    // double is an EXTRA gesture (Frank 2026-08-03: select-then-open FX
    // chain is harmless). Independent of long-press (a binding can carry
    // both). Collect the slot under g_pressMx, fire after unlock —
    // runSlot_ can re-enter dispatch() on this input thread, and we never
    // hold g_pressMx across it. Only the press edge participates.
    if (pressed && bd.hasDoublePress) {
        const int  m = static_cast<int>(currentModifierSnapshot());
        ActionSlot dblToFire;   // stays empty unless a double actually landed
        {
            const uint32_t k = pressKey(layer, id);
            std::lock_guard<std::mutex> lk(g_pressMx);
            const auto now = std::chrono::steady_clock::now();
            auto it2 = g_lastPressAt.find(k);
            const bool isDouble = it2 != g_lastPressAt.end()
                               && (now - it2->second) <= kDoubleClickMs;
            if (isDouble) {
                const ActionSlot& ds = effectiveDoubleSlot_(bd, m);
                if (!slotIsEmpty_(ds)) dblToFire = ds;   // copy for firing
                g_lastPressAt.erase(k);   // reset so a triple-tap starts fresh
            } else {
                g_lastPressAt[k] = now;
            }
        }
        if (!slotIsEmpty_(dblToFire))
            runSlot_(dblToFire, /*firing*/ true, /*pressed*/ true);
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
            const Modifier mod = currentModifierSnapshot();
            const int      m   = static_cast<int>(mod);
            std::lock_guard<std::mutex> lk(g_pressMx);
            PressRecord rec;
            rec.start = std::chrono::steady_clock::now();
            rec.mod   = mod;
            // Arm the threshold timer only when the EFFECTIVE long slot
            // (modifier-specific, else Plain fallback) is a real action —
            // otherwise a long hold does nothing anyway and we don't want
            // the timer to steal the release-edge short fire.
            const ActionSlot& ls = effectiveLongSlot_(bd, m);
            if (!slotIsEmpty_(ls)) {
                rec.longArmed = true;
                rec.longSlot  = ls;
                rec.id        = id;
            }
            g_pressStart[k] = std::move(rec);
        } else {
            // Pull the record out under the lock, then fire after unlock.
            bool                                  have  = false;
            bool                                  fired = false;
            std::chrono::steady_clock::duration   held{};
            int                                   m     = 0;
            {
                std::lock_guard<std::mutex> lk(g_pressMx);
                auto it = g_pressStart.find(k);
                if (it != g_pressStart.end()) {
                    have  = true;
                    fired = it->second.longFired;
                    held  = std::chrono::steady_clock::now() - it->second.start;
                    m     = static_cast<int>(it->second.mod);
                    g_pressStart.erase(it);
                }
            }
            // If the threshold timer already fired the long slot, the
            // release edge only cleans up — never re-fire short or long.
            if (have && !fired) {
                if (held >= kLongPressThreshold) {
                    // Fallback: timer normally beats us here, but a fast
                    // release right at the threshold can still land first.
                    // Same Plain-fallback resolution as the arm.
                    const ActionSlot& ls = effectiveLongSlot_(bd, m);
                    runSlot_(ls, /*firing*/ true, /*pressed*/ false);
                    // Tag lastFired with the long-press marker (high bit)
                    // so the LED resolver reads from longPress[m] rather
                    // than shortPress[m]. Without this, a long-press
                    // action that toggles state ON (e.g. send_this) would
                    // render the LED via shortPress's LedOverride and
                    // ignore the user's per-long-press colour choice.
                    if (!slotIsEmpty_(ls)) {
                        const auto idx = static_cast<size_t>(id);
                        if (idx < kLastFiredModSize) {
                            g_lastFiredMod[idx].store(
                                static_cast<uint8_t>(m | 0x80),
                                std::memory_order_relaxed);
                        }
                    }
                } else {
                    // Pre-threshold TAP → short, exactly as before.
                    runSlot_(bd.shortPress[m],
                               /*firing*/ true, /*pressed*/ false);
                    if (bd.shortPress[m].type != ActionType::Noop) {
                        const auto idx = static_cast<size_t>(id);
                        if (idx < kLastFiredModSize) {
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
        // No runSlot_ inside this block, so holding g_pressMx is safe.
        // This record is for modifier-slot matching only (longArmed stays
        // false); the Toggle/Hold long-press timer lives in g_longPressStart.
        std::lock_guard<std::mutex> lk(g_pressMx);
        if (pressed) {
            PressRecord rec;
            rec.start = std::chrono::steady_clock::now();
            rec.mod   = currentModifierSnapshot();
            g_pressStart[k] = std::move(rec);
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
        if (idx < kLastFiredModSize) {
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
            const int m = slotIdx;
            std::lock_guard<std::mutex> lk(g_pressMx);
            PressRecord rec;
            rec.start = std::chrono::steady_clock::now();
            rec.mod   = static_cast<Modifier>(slotIdx);
            // Arm the threshold timer only for a real EFFECTIVE long slot
            // (modifier-specific, else Plain fallback).
            const ActionSlot& ls = effectiveLongSlot_(bd, m);
            if (!slotIsEmpty_(ls)) {
                rec.longArmed = true;
                rec.longSlot  = ls;
                rec.id        = id;
            }
            g_longPressStart[k] = std::move(rec);
        } else {
            bool                                have  = false;
            bool                                fired = false;
            std::chrono::steady_clock::duration held{};
            int                                 m     = 0;
            {
                std::lock_guard<std::mutex> lk(g_pressMx);
                auto lit = g_longPressStart.find(k);
                if (lit != g_longPressStart.end()) {
                    have  = true;
                    fired = lit->second.longFired;
                    held  = std::chrono::steady_clock::now() - lit->second.start;
                    m     = static_cast<int>(lit->second.mod);
                    g_longPressStart.erase(lit);
                }
            }
            // Skip if the threshold timer already fired the additive long.
            const ActionSlot& ls = effectiveLongSlot_(bd, m);
            if (have && !fired
                && held >= kLongPressThreshold
                && !slotIsEmpty_(ls))
            {
                // Fallback for a release landing right at the threshold.
                runSlot_(ls, /*firing*/ true, /*pressed*/ false);
                // Tag lastFired with the long-press marker so the LED
                // resolver picks up the long-press slot's LedOverride.
                const auto idx = static_cast<size_t>(id);
                if (idx < kLastFiredModSize) {
                    g_lastFiredMod[idx].store(
                        static_cast<uint8_t>(m | 0x80),
                        std::memory_order_relaxed);
                }
            }
        }
    }

    return true;
}

// Fire a button's short / double slot on demand — resolve the active
// layer's binding for `id`, pick the current-modifier slot (Plain
// fallback), run it if non-empty. No press-timing, no g_pressStart / LED
// bookkeeping: the caller (UF8 per-strip SEL) owns its own per-strip
// double-tap detection, since the shared ButtonId::Uf8Select can't ride
// dispatch()'s per-(layer,button) timer without false-doubling across
// strips. Worker-thread-safe (runSlot_'s builtins defer the REAPER API).
static bool fireResolvedSlot_(ButtonId id, bool wantDouble)
{
    if (id == ButtonId::None) return false;
    Binding bd;
    {
        std::lock_guard<std::mutex> lk(g_cfgMutex);
        int layer = g_cfg.activeLayer;
        if (layer < 0 || layer > 2) layer = 0;
        auto it = g_cfg.layers[layer].bindings.find(id);
        if (it == g_cfg.layers[layer].bindings.end()) return false;
        bd = it->second;
    }
    const int m = static_cast<int>(currentModifierSnapshot());
    const ActionSlot& s = wantDouble ? effectiveDoubleSlot_(bd, m)
                                     : (!slotIsEmpty_(bd.shortPress[m])
                                            ? bd.shortPress[m]
                                            : bd.shortPress[
                                                static_cast<int>(Modifier::Plain)]);
    if (slotIsEmpty_(s)) return false;
    runSlot_(s, /*firing*/ true, /*pressed*/ true);
    return true;
}

bool fireShortPress(ButtonId id)  { return fireResolvedSlot_(id, false); }
bool fireDoublePress(ButtonId id) { return fireResolvedSlot_(id, true);  }

// Fire any armed long-press the moment it crosses the 0.5 s threshold,
// WHILE the button is still held — instead of waiting for the release
// edge. The UF1 sometimes drops or reorders the button-release event, so
// a release-edge long-press could be lost entirely; firing at the
// threshold removes all release-timing dependence. Runs on the MAIN
// thread from onTimer (~30 Hz), so worst-case latency past 500 ms is one
// tick (~33 ms) — imperceptible. Collect under g_pressMx, fire after
// unlocking: runSlot_ can re-enter dispatch on an input thread, and we
// must never hold g_pressMx across it. Applies to every surface (UF8 /
// UC1 / UF1) and every press-timer keyspace (real bindings, user-quick
// slots, UF1 soft-bank slots) because they all share these two maps.
void tickLongPressThreshold()
{
    std::vector<std::tuple<ActionSlot, ButtonId, Modifier>> toFire;
    {
        std::lock_guard<std::mutex> lk(g_pressMx);
        const auto now = std::chrono::steady_clock::now();
        for (auto& [k, rec] : g_pressStart) {
            (void)k;
            if (rec.longArmed && !rec.longFired
                && now - rec.start >= kLongPressThreshold) {
                rec.longFired = true;
                toFire.push_back({rec.longSlot, rec.id, rec.mod});
            }
        }
        for (auto& [k, rec] : g_longPressStart) {
            (void)k;
            if (rec.longArmed && !rec.longFired
                && now - rec.start >= kLongPressThreshold) {
                rec.longFired = true;
                toFire.push_back({rec.longSlot, rec.id, rec.mod});
            }
        }
    }
    for (auto& [slot, id, mod] : toFire) {
        runSlot_(slot, /*firing*/ true, /*pressed*/ false);
        const auto idx = static_cast<size_t>(id);
        if (idx < kLastFiredModSize) {
            g_lastFiredMod[idx].store(
                static_cast<uint8_t>(static_cast<int>(mod) | 0x80),
                std::memory_order_relaxed);
        }
    }
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

namespace {
// The ONE traversal behind findFirstBoundTo and findAllBoundTo. `cb` returns true
// to stop the walk. The caller holds g_cfgMutex.
// Order is deliberate and stable: the layer map in kNames declaration order (NOT
// the unordered bindings map, so the UI names the same control every time), then
// the UF8 soft-key store, then the UF1 soft-key banks.
void walkBoundTo_(const std::string& builtinName,
                  const std::function<bool(const BoundRef&)>& cb)
{
    auto matchStep = [&](const ActionStep& s) {
        return s.type == ActionType::Builtin && s.action == builtinName;
    };
    auto matchSlot = [&](const ActionSlot& slot) {
        if (matchStep(static_cast<const ActionStep&>(slot))) return true;
        for (const auto& extra : slot.extraSteps) {
            if (matchStep(extra)) return true;
        }
        return false;
    };
    auto emit = [&](int layer, ButtonId id, int m, bool longPress,
                    const char* where) {
        BoundRef r;
        r.layer     = layer;
        r.id        = id;
        r.mod       = static_cast<Modifier>(m);
        r.longPress = longPress;
        if (where) r.where = where;
        return cb(r);
    };

    for (int layer = 0; layer < 3; ++layer) {
        for (const auto& e : kNames) {
            auto it = g_cfg.layers[layer].bindings.find(e.id);
            if (it == g_cfg.layers[layer].bindings.end()) continue;
            const Binding& bd = it->second;
            for (int m = 0; m < kModifierCount; ++m) {
                if (matchSlot(bd.shortPress[m])
                    && emit(layer, e.id, m, false, nullptr)) return;
                if (bd.hasLongPress && matchSlot(bd.longPress[m])
                    && emit(layer, e.id, m, true, nullptr)) return;
            }
        }
    }

    // ---- Then the two soft-key stores -----------------------------------
    // Nothing here has a ButtonId, so a hit reports its coordinates instead.
    static const char* kSubBankNames[kSubBanksPerQuick] = {
        "V-POT", "Soft 1", "Soft 2", "Soft 3", "Soft 4", "Soft 5"
    };
    for (int layer = 0; layer < 3; ++layer) {
        for (int qi = 0; qi < kQuicksPerLayer; ++qi) {
            for (int bi = 0; bi < kSubBanksPerQuick; ++bi) {
                for (int si = 0; si < kSlotsPerSubBank; ++si) {
                    const Binding& bd = g_cfg.userQuicks[layer].quicks[qi]
                                            .subBanks[bi].slots[si];
                    for (int m = 0; m < kModifierCount; ++m) {
                        const bool isShort = matchSlot(bd.shortPress[m]);
                        if (!isShort
                            && !(bd.hasLongPress && matchSlot(bd.longPress[m])))
                            continue;
                        char buf[96];
                        snprintf(buf, sizeof(buf), "Q%d / %s / key %d",
                                      qi + 1, kSubBankNames[bi], si + 1);
                        if (emit(layer, ButtonId::None, m, !isShort, buf)) return;
                    }
                }
            }
        }
    }
    for (int b = 0; b < kUf1SoftBankCount; ++b) {
        for (int si = 0; si < kUf1SoftBankSlots; ++si) {
            const Binding& bd = g_cfg.uf1SoftBanks[b][si];
            for (int m = 0; m < kModifierCount; ++m) {
                const bool isShort = matchSlot(bd.shortPress[m]);
                if (!isShort
                    && !(bd.hasLongPress && matchSlot(bd.longPress[m])))
                    continue;
                char buf[96];
                snprintf(buf, sizeof(buf), "UF1 bank %d / key %d",
                              b + 1, si + 1);
                if (emit(0, ButtonId::None, m, !isShort, buf)) return;
            }
        }
    }
}
}  // namespace

bool findFirstBoundTo(const std::string& builtinName,
                      int* layerOut, ButtonId* idOut, Modifier* modOut,
                      bool* longPressOut, std::string* softKeyWhereOut)
{
    if (builtinName.empty()) return false;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    bool found = false;
    walkBoundTo_(builtinName, [&](const BoundRef& r) {
        if (layerOut)     *layerOut     = r.layer;
        if (idOut)        *idOut        = r.id;
        if (modOut)       *modOut       = r.mod;
        if (longPressOut) *longPressOut = r.longPress;
        // Only a soft-key hit carries a location; the layer-map path leaves the
        // caller's string untouched, exactly as it always did.
        if (softKeyWhereOut && r.id == ButtonId::None) *softKeyWhereOut = r.where;
        found = true;
        return true;                       // first hit wins — stop the walk
    });
    return found;
}

std::vector<BoundRef> findAllBoundTo(const std::string& builtinName)
{
    std::vector<BoundRef> out;
    if (builtinName.empty()) return out;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    walkBoundTo_(builtinName, [&](const BoundRef& r) {
        out.push_back(r);
        return false;                      // keep walking
    });
    return out;
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

void resetBindingToDefault(int layer, ButtonId id)
{
    if (layer < 0 || layer > 2 || id == ButtonId::None) return;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    // Seed a throwaway factory config and copy out just this one
    // button's default. Heap-allocate — see load() for the
    // sizeof(Config) stack-overflow rationale (Windows).
    auto tmpPtr = std::make_unique<Config>();
    Config& tmp = *tmpPtr;
    seedFactoryDefaults_(tmp);
    auto& defMap = tmp.layers[layer].bindings;
    auto it = defMap.find(id);
    if (it != defMap.end()) {
        g_cfg.layers[layer].bindings[id] = it->second;
    } else {
        // No factory entry for this button → erase so it returns to
        // the "untouched" state (legacy MCU passthrough / table
        // default look), matching clearBinding.
        g_cfg.layers[layer].bindings.erase(id);
    }
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

static bool uf1SoftBankInRange_(int bank, int slot)
{
    return bank >= 0 && bank < kUf1SoftBankCount
        && slot >= 0 && slot < kUf1SoftBankSlots;
}

Binding getUf1SoftBankSlot(int bank, int slot)
{
    if (!uf1SoftBankInRange_(bank, slot)) return {};
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    return g_cfg.uf1SoftBanks[bank][slot];
}

void setUf1SoftBankSlot(int bank, int slot, const Binding& bd)
{
    if (!uf1SoftBankInRange_(bank, slot)) return;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    g_cfg.uf1SoftBanks[bank][slot] = bd;
    persistLocked_();
}

DynamicBankKind getUf1SoftBankDynamic(int bank, int mod)
{
    if (bank < 0 || bank >= kUf1SoftBankCount) return DynamicBankKind::None;
    if (mod  < 0 || mod  >= kSoftKeyModifierSets) return DynamicBankKind::None;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    return g_cfg.uf1SoftBankDynamic[bank][mod];
}

// ⇨ THE KIND FOR ONE SET: ITS OWN IF IT HAS ONE, ELSE PLAIN'S.
// A set is a full bank, so it can be its own dynamic bank — FX on Plain and
// Track Colours on Shift, say (Frank 2026-08-25: "ich kann bei beiden nicht eine
// dyn bank auf plain und eine andere auf shift legen"). None on a set means
// "nothing of my own", NOT "static": it falls back to Plain, which is what keeps
// a bank that is dynamic on Plain behaving exactly as it did under a held
// modifier — same keys, same FX gestures.
// *ownsSet says the answer came from the set itself. Callers need that to tell
// "this set IS this bank" (its keys are that bank's own push) from "this set is
// looking at Plain's bank" (where the modifiers are that bank's gestures).
DynamicBankKind getUf1SoftBankDynamicFor(int bank, int mod, bool* ownsSet)
{
    if (ownsSet) *ownsSet = false;
    const DynamicBankKind own = getUf1SoftBankDynamic(bank, mod);
    if (own != DynamicBankKind::None && mod != kDynamicKindSet) {
        if (ownsSet) *ownsSet = true;
        return own;
    }
    if (mod == kDynamicKindSet) return own;
    return getUf1SoftBankDynamic(bank, kDynamicKindSet);
}

// Number of UF1 soft-key banks actually IN USE = (highest in-use bank index
// + 1), min 1. A bank is in use if it is DYNAMIC (FX / param-groups / colours)
// or has any non-empty static slot. Used for the DAW-mode header "N/M" so the
// denominator reflects the real assigned banks (Frank 2026-08-04: was a fixed
// 9/10) and to bound the DAW bank paging so you can't step past them.
int uf1SoftBankInUseCount()
{
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    int highest = -1;
    for (int b = 0; b < kUf1SoftBankCount; ++b) {
        bool inUse = false;
        for (int m = 0; m < kSoftKeyModifierSets && !inUse; ++m)
            if (g_cfg.uf1SoftBankDynamic[b][m] != DynamicBankKind::None) inUse = true;
        for (int s = 0; !inUse && s < kUf1SoftBankSlots; ++s)
            if (!uf1BankSlotEmpty_(g_cfg.uf1SoftBanks[b][s])) inUse = true;
        if (inUse) highest = b;
    }
    return highest + 1 >= 1 ? highest + 1 : 1;
}

void setUf1SoftBankDynamic(int bank, int mod, DynamicBankKind kind)
{
    if (bank < 0 || bank >= kUf1SoftBankCount) return;
    if (mod  < 0 || mod  >= kSoftKeyModifierSets) return;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    g_cfg.uf1SoftBankDynamic[bank][mod] = kind;
    persistLocked_();
}

static bool subBankLedInRange_(int layer, int quick, int subBank)
{
    return layer    >= 0 && layer    < 3
        && quick    >= 0 && quick    < kQuicksPerLayer
        && subBank  >= 0 && subBank  < kSubBanksPerQuick;
}

SubBankLed getSubBankLed(int layer, int quick, int subBank, int mod)
{
    if (!subBankLedInRange_(layer, quick, subBank)) return {};
    if (mod < 0 || mod >= kSoftKeyModifierSets)     return {};
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    const auto& row = g_cfg.userQuicks[layer].quicks[quick].subBankLeds[subBank];
    // A set that never claimed its own appearance wears Plain's.
    if (mod != 0 && !row[mod].isSet) return row[0];
    return row[mod];
}

void resetSubBankLed(int layer, int quick, int subBank, int mod)
{
    if (!subBankLedInRange_(layer, quick, subBank)) return;
    if (mod <= 0 || mod >= kSoftKeyModifierSets) return;   // Plain is the base
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    g_cfg.userQuicks[layer].quicks[quick].subBankLeds[subBank][mod] =
        SubBankLed{};
    persistLocked_();
}

void setSubBankLed(int layer, int quick, int subBank, int mod,
                   const SubBankLed& app)
{
    if (!subBankLedInRange_(layer, quick, subBank)) return;
    if (mod < 0 || mod >= kSoftKeyModifierSets)     return;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    SubBankLed v = app;
    v.isSet = (mod != 0);   // Plain is the base; the flag is a set's own claim
    g_cfg.userQuicks[layer].quicks[quick].subBankLeds[subBank][mod] = v;
    persistLocked_();
}

DynamicBankKind getSubBankDynamic(int layer, int quick, int subBank, int mod)
{
    if (!subBankLedInRange_(layer, quick, subBank)) return DynamicBankKind::None;
    if (mod < 0 || mod >= kSoftKeyModifierSets)     return DynamicBankKind::None;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    return g_cfg.userQuicks[layer].quicks[quick].subBanks[subBank].dynamic[mod];
}

// Sub-Bank twin of getUf1SoftBankDynamicFor — same rule, same reasoning.
DynamicBankKind getSubBankDynamicFor(int layer, int quick, int subBank,
                                     int mod, bool* ownsSet)
{
    if (ownsSet) *ownsSet = false;
    const DynamicBankKind own = getSubBankDynamic(layer, quick, subBank, mod);
    if (own != DynamicBankKind::None && mod != kDynamicKindSet) {
        if (ownsSet) *ownsSet = true;
        return own;
    }
    if (mod == kDynamicKindSet) return own;
    return getSubBankDynamic(layer, quick, subBank, kDynamicKindSet);
}

void setSubBankDynamic(int layer, int quick, int subBank, int mod,
                       DynamicBankKind kind)
{
    if (!subBankLedInRange_(layer, quick, subBank)) return;
    if (mod < 0 || mod >= kSoftKeyModifierSets)     return;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    g_cfg.userQuicks[layer].quicks[quick].subBanks[subBank].dynamic[mod] = kind;
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
                    int layer, int quick, int subBank, int mod)
{
    if (name.empty()) return false;
    if (!userQuickSlotInRange_(layer, quick, subBank, 0)) return false;
    if (mod < 0 || mod >= kSoftKeyModifierSets) return false;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    SoftKeyBankPreset p;
    p.name = name;
    for (int s = 0; s < kSlotsPerSubBank; ++s) {
        // ONE layer's worth. A Binding carries all four; copying the lot would
        // make "save the Shift bank" quietly capture Plain, Cmd and Ctrl too.
        const Binding& src = g_cfg.userQuicks[layer].quicks[quick]
                                .subBanks[subBank].slots[s];
        p.slots[s] = Binding{};
        p.slots[s].behavior       = src.behavior;
        p.slots[s].label          = src.label;
        p.slots[s].labelIsUserSet = src.labelIsUserSet;
        // ⇨ ALL THREE GESTURES, NOT JUST THE SHORT PRESS. Saving only
        // shortPress[mod] meant recalling preset A over a bank that held B left
        // B's long- and double-press in place: the key then did A on a tap and B
        // on a hold, a combination the user never configured anywhere
        // (Frank 2026-08-18).
        p.slots[s].hasLongPress   = src.hasLongPress;
        p.slots[s].hasDoublePress = src.hasDoublePress;
        p.slots[s].longPress[0]   = src.longPress[mod];
        p.slots[s].doublePress[0] = src.doublePress[mod];
        p.slots[s].color[0] = src.color[0];
        p.slots[s].color[1] = src.color[1];
        p.slots[s].color[2] = src.color[2];
        p.slots[s].brightness = src.brightness;
        p.slots[s].inactiveColor[0] = src.inactiveColor[0];
        p.slots[s].inactiveColor[1] = src.inactiveColor[1];
        p.slots[s].inactiveColor[2] = src.inactiveColor[2];
        p.slots[s].inactiveBrightness = src.inactiveBrightness;
        p.slots[s].ledShowWhenEmpty   = src.ledShowWhenEmpty;
        p.slots[s].shortPress[0] = src.shortPress[mod];
        // Store the label this SET actually shows: its own if it has one, the
        // key's name only when saving Plain. Otherwise saving the Shift set
        // would quietly capture the Plain name.
        if (!src.shortPress[mod].label.empty())
            p.slots[s].label = src.shortPress[mod].label;
        else if (mod != 0)
            p.slots[s].label.clear();
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

// Write one preset slot onto modifier set `mod` of `dst`. The user recall and
// the factory recall both go through here — they used to write different fields
// in different ways, so the two buttons in the same window left the bank in
// different states (Frank 2026-08-18). Caller holds g_cfgMutex.
static void applyPresetSlotLocked_(const Binding& src, int mod, Binding& dst)
{
    dst.shortPress[mod]  = src.shortPress[0];
    dst.longPress[mod]   = src.longPress[0];
    dst.doublePress[mod] = src.doublePress[0];
    // The flags are per-KEY, not per-set, so they are OR'd in: a preset that
    // brings a long press switches the column on, one without it must not
    // switch off a column another set is still using.
    dst.hasLongPress   = dst.hasLongPress   || src.hasLongPress;
    dst.hasDoublePress = dst.hasDoublePress || src.hasDoublePress;
    // ⇨ THE LABEL HAS TO COME ALONG, INTO ANY SET.
    // A preset slot's label may sit in Binding::label — that is where a key's
    // name lives, and where every preset saved before today put it. A modifier
    // set deliberately does NOT fall back to the Plain label, so recalling into
    // Shift without carrying it produced a set whose actions fired while the
    // screen stayed blank (Frank 2026-08-18: "LABELS ERSCHEINEN NICHT ABER
    // ACTION FEUERT"). Label-only slots vanished entirely.
    // Modifier sets only. On Plain the key's own name IS the label, and
    // duplicating it into the set would shadow the editor's Label field, which
    // writes Binding::label there.
    if (mod != 0 && dst.shortPress[mod].label.empty())
        dst.shortPress[mod].label = src.label;
    if (mod == 0) {
        dst.behavior       = src.behavior;
        dst.label          = src.label;
        dst.labelIsUserSet = src.labelIsUserSet;
        for (int c = 0; c < 3; ++c) {
            dst.color[c]         = src.color[c];
            dst.inactiveColor[c] = src.inactiveColor[c];
        }
        dst.brightness         = src.brightness;
        dst.inactiveBrightness = src.inactiveBrightness;
        dst.ledShowWhenEmpty   = src.ledShowWhenEmpty;
    }
}

bool recallBankPreset(int idx, int layer, int quick, int subBank, int mod)
{
    if (!userQuickSlotInRange_(layer, quick, subBank, 0)) return false;
    if (mod < 0 || mod >= kSoftKeyModifierSets) return false;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    if (idx < 0 || idx >= static_cast<int>(g_cfg.bankPresets.size()))
        return false;
    const SoftKeyBankPreset& p = g_cfg.bankPresets[idx];
    for (int s = 0; s < kSlotsPerSubBank; ++s) {
        // Lands in the set you are editing, leaving the other one alone.
        // A preset captured on Plain recalls into Shift unchanged — the stored
        // slot is one set's worth of gestures, not a fixed Plain/Shift pair.
        applyPresetSlotLocked_(p.slots[s], mod,
                               g_cfg.userQuicks[layer].quicks[quick]
                                   .subBanks[subBank].slots[s]);
    }
    persistLocked_();
    return true;
}

// ---- Factory Rea-Sixty soft-key bank presets -----------------------------
// Curated from Rea-Sixty's own built-ins only (Frank's locked curation,
// backlog 2026-06-22). Labels ≤8 chars (firmware top-soft-key cap). Built
// once, lazily, into a static table (uses mkBuiltin from this TU).

static const std::vector<SoftKeyBankPreset>& factoryReaSixtyBanks_()
{
    static const std::vector<SoftKeyBankPreset> kBanks = []() {
        using SlotDef = std::pair<const char*, const char*>;   // action, label
        auto bank = [](const char* presetName,
                       std::initializer_list<SlotDef> slots) {
            SoftKeyBankPreset p;
            p.name = presetName;
            int i = 0;
            for (const auto& s : slots) {
                if (i >= kSlotsPerSubBank) break;
                p.slots[i++] = mkBuiltin(s.first, Behavior::Momentary,
                                         s.second);
            }
            return p;
        };
        // Labels up to 12 chars — the UF8 top-soft-key LCD width
        // (buildPluginSlotName caps at 12; the resolver centre-pads to 12).
        std::vector<SoftKeyBankPreset> v;
        v.push_back(bank("Encoder Modes", {
            {"encoder_nav",      "Ch Select"},
            {"encoder_instance", "Instance"},
            {"encoder_fx_cycle", "FX Cycle"},
            {"encoder_fx_move",  "FX Move"},
            {"encoder_cs_cycle", "CS Cycle"},
            {"encoder_markers",  "Markers"},
            {"encoder_nudge",    "Nudge"},
            {"encoder_focus",    "Focus Wheel"},
        }));
        v.push_back(bank("Focus Set & Selsets", {
            {"temp_selset_recall",          "Pin Set"},
            {"temp_selset_add",             "Add to Set"},
            {"temp_selset_remove",          "Rem from Set"},
            {"temp_selset_toggle_selected", "Toggle Sel"},
            {"temp_selset_set_from_selection", "Set frm Sel"},
            {"temp_selset_pin_uf1_channel", "Pin This Ch"},
            {"temp_selset_pin_focused",     "Pin Focused"},
            {"temp_selset_clear",           "Clear Set"},
            {"selset_cycle",                "Cycle Sets"},
        }));
        v.push_back(bank("Plug-in Ops", {
            {"show_focused_plugin_gui",      "FX GUI"},
            {"show_fx_chain",                "FX Chain"},
            {"close_all_fx_guis",            "Close All FX"},
            {"plugin_bypass",                "Bypass"},
            {"plugin_offline",               "Offline"},
            {"plugin_preset_prev",           "Preset Prev"},
            {"plugin_preset_next",           "Preset Next"},
            {"ssl_strip_mode_toggle_with_gui", "SSL Strip"},
        }));
        v.push_back(bank("Learn / Master", {
            {"learn_hud_toggle",        "Learn HUD"},
            {"quick_learn",             "Quick Learn"},
            {"quick_learn_track",       "QLearn Trk"},
            {"touch_to_learn_toggle",   "Touch Learn"},
            {"master_pin_strip1",       "Master Left"},
            {"master_pin_strip8",       "Master Right"},
            {"focused_panel_toggle",    "Panel"},
            {"uc1_outgain_fader_toggle","Out Gain"},
        }));
        // CS Favourites — switch_cs_1..8. Static "CS Fav N" labels here; the
        // top-soft-key resolver overrides them with the favourite's name
        // at render time (main.cpp), falling back to "CS Fav N" when empty.
        v.push_back(bank("CS Favourites", {
            {"switch_cs_1", "CS Fav 1"},
            {"switch_cs_2", "CS Fav 2"},
            {"switch_cs_3", "CS Fav 3"},
            {"switch_cs_4", "CS Fav 4"},
            {"switch_cs_5", "CS Fav 5"},
            {"switch_cs_6", "CS Fav 6"},
            {"switch_cs_7", "CS Fav 7"},
            {"switch_cs_8", "CS Fav 8"},
        }));
        // BC Favourites — switch_bc_1..8. Same render-time label override as CS.
        v.push_back(bank("BC Favourites", {
            {"switch_bc_1", "BC Fav 1"},
            {"switch_bc_2", "BC Fav 2"},
            {"switch_bc_3", "BC Fav 3"},
            {"switch_bc_4", "BC Fav 4"},
            {"switch_bc_5", "BC Fav 5"},
            {"switch_bc_6", "BC Fav 6"},
            {"switch_bc_7", "BC Fav 7"},
            {"switch_bc_8", "BC Fav 8"},
        }));
        // Brightness — replaces the old "View / Utility" filler bank (Focus
        // CS/BC did nothing, the rest was junk). Only brightness is genuinely
        // useful here, so make the whole bank coherent: all / LCDs / LEDs ×
        // up / down (Frank 2026-06-23). Generic DAW actions (deselect, arm,
        // zoom) the user binds himself to a free Quick.
        v.push_back(bank("Brightness", {
            {"brightness_both_up",   "Bright +"},
            {"brightness_both_down", "Bright -"},
            {"brightness_lcds_up",   "LCDs +"},
            {"brightness_lcds_down", "LCDs -"},
            {"brightness_leds_up",   "LEDs +"},
            {"brightness_leds_down", "LEDs -"},
        }));
        return v;
    }();
    return kBanks;
}

int factoryBankPresetCount()
{
    return static_cast<int>(factoryReaSixtyBanks_().size());
}

SoftKeyBankPreset factoryBankPresetAt(int idx)
{
    const auto& banks = factoryReaSixtyBanks_();
    if (idx < 0 || idx >= static_cast<int>(banks.size())) return {};
    return banks[idx];
}

bool recallFactoryBankPreset(int idx, int layer, int quick, int subBank, int mod)
{
    if (!userQuickSlotInRange_(layer, quick, subBank, 0)) return false;
    if (mod < 0 || mod >= kSoftKeyModifierSets) return false;
    const auto& banks = factoryReaSixtyBanks_();
    if (idx < 0 || idx >= static_cast<int>(banks.size())) return false;
    const SoftKeyBankPreset p = banks[idx];   // copy before taking the lock
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    for (int s = 0; s < kSlotsPerSubBank; ++s) {
        // Same writer as the user recall. On Plain this used to assign the whole
        // Binding (`dst = p.slots[s]`), which took the OTHER modifier set and the
        // long/double presses down with it — so the two recall buttons in one
        // window behaved differently (Frank 2026-08-18).
        applyPresetSlotLocked_(p.slots[s], mod,
                               g_cfg.userQuicks[layer].quicks[quick]
                                   .subBanks[subBank].slots[s]);
    }
    persistLocked_();
    return true;
}

int loadFactoryReaSixtySet(int layer, int quick, int mod)
{
    if (layer < 0 || layer >= 3) return -1;
    if (quick < 0 || quick >= kQuicksPerLayer) return -1;
    if (mod < 0 || mod >= kSoftKeyModifierSets) return -1;
    const auto& banks = factoryReaSixtyBanks_();
    // std::clamp, not std::min — Windows <windows.h> defines a min() macro that
    // breaks std::min on MSVC (error C2589). banks.size() ≥ 0, so clamping the
    // low end at 0 is equivalent to min(size, kSubBanksPerQuick).
    const int n = std::clamp(static_cast<int>(banks.size()),
                             0, kSubBanksPerQuick);
    {
        std::lock_guard<std::mutex> lk(g_cfgMutex);
        for (int sb = 0; sb < n; ++sb) {
            for (int s = 0; s < kSlotsPerSubBank; ++s) {
                // ⇨ ONE SET, like every other recall. This used to assign whole
                // Bindings, so loading the factory set wiped the Shift set of all
                // 48 slots while the dialog only warned about "48 slots"
                // (Frank 2026-08-18). Its sibling recallFactoryBankPreset has
                // taken a `mod` since modifier sets existed; this one never got
                // the parameter.
                applyPresetSlotLocked_(banks[sb].slots[s], mod,
                                       g_cfg.userQuicks[layer].quicks[quick]
                                           .subBanks[sb].slots[s]);
            }
        }
        persistLocked_();
    }
    return n;
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
            const Modifier mod = bankModifierSnapshot();
            const int      m   = static_cast<int>(mod);
            std::lock_guard<std::mutex> lk(g_pressMx);
            PressRecord rec;
            rec.start = std::chrono::steady_clock::now();
            rec.mod   = mod;
            // Plain-fallback long slot (see effectiveLongSlot_).
            const ActionSlot& ls = effectiveLongSlot_(bd, m);
            if (!slotIsEmpty_(ls)) {
                rec.longArmed = true;
                rec.longSlot  = ls;
                rec.id        = pseudoId;   // > 256, so no LED tag (as before)
            }
            g_pressStart[k] = std::move(rec);
        } else {
            bool                                have  = false;
            bool                                fired = false;
            std::chrono::steady_clock::duration held{};
            int                                 m     = 0;
            {
                std::lock_guard<std::mutex> lk(g_pressMx);
                auto it = g_pressStart.find(k);
                if (it != g_pressStart.end()) {
                    have  = true;
                    fired = it->second.longFired;
                    held  = std::chrono::steady_clock::now() - it->second.start;
                    m     = static_cast<int>(it->second.mod);
                    g_pressStart.erase(it);
                }
            }
            // Skip if the threshold timer already fired the long slot.
            if (have && !fired) {
                if (held >= kLongPressThreshold)
                    runSlot_(effectiveLongSlot_(bd, m), /*firing*/ true, /*pressed*/ false);
                else
                    runSlot_(shortP[m], /*firing*/ true, /*pressed*/ false);
            }
        }
        return true;
    }

    // ⇨ TOGGLE AND HOLD PICK THE HELD SET TOO.
    // This used to be filled in only for Momentary, which was harmless while a
    // soft-key had one set. With sets AND the Behavior combo both exposed, a
    // Toggle key showed its Shift label and fired its PLAIN action, or nothing
    // at all when Plain was empty (Frank 2026-08-18).
    int slotMod = static_cast<int>(bankModifierSnapshot());
    if (bd.behavior == Behavior::Momentary) {
        std::lock_guard<std::mutex> lk(g_pressMx);
        if (pressed) {
            PressRecord rec;
            rec.start = std::chrono::steady_clock::now();
            rec.mod   = bankModifierSnapshot();
            g_pressStart[k] = std::move(rec);
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

// UF1 soft-key bank slot dispatch — same long-press + modifier-matrix
// logic as dispatchUserQuickSlot, but addressed by (bank, slot) on the
// global uf1SoftBanks store, with a distinct press-timer keyspace.
bool dispatchUf1SoftBankSlot(int bank, int slot, bool pressed)
{
    if (!uf1SoftBankInRange_(bank, slot)) return false;

    Binding bd;
    {
        std::lock_guard<std::mutex> lk(g_cfgMutex);
        bd = g_cfg.uf1SoftBanks[bank][slot];
    }

    const int      syntheticLayer = 200 + bank;   // clear of 0..2 + user-quick 100+
    const ButtonId pseudoId       = static_cast<ButtonId>(0x5000 + slot);
    const uint32_t k = pressKey(syntheticLayer, pseudoId);

    const bool longPressArmed =
        bd.hasLongPress && bd.behavior == Behavior::Momentary;
    const auto& shortP = bd.shortPress;
    const auto& longP  = bd.longPress;

    if (longPressArmed) {
        if (pressed) {
            const Modifier mod = bankModifierSnapshot();
            const int      m   = static_cast<int>(mod);
            std::lock_guard<std::mutex> lk(g_pressMx);
            PressRecord rec;
            rec.start = std::chrono::steady_clock::now();
            rec.mod   = mod;
            // Plain-fallback long slot (see effectiveLongSlot_).
            const ActionSlot& ls = effectiveLongSlot_(bd, m);
            if (!slotIsEmpty_(ls)) {
                rec.longArmed = true;
                rec.longSlot  = ls;
                rec.id        = pseudoId;   // > 256, so no LED tag (as before)
            }
            g_pressStart[k] = std::move(rec);
        } else {
            bool                                have  = false;
            bool                                fired = false;
            std::chrono::steady_clock::duration held{};
            int                                 m     = 0;
            {
                std::lock_guard<std::mutex> lk(g_pressMx);
                auto it = g_pressStart.find(k);
                if (it != g_pressStart.end()) {
                    have  = true;
                    fired = it->second.longFired;
                    held  = std::chrono::steady_clock::now() - it->second.start;
                    m     = static_cast<int>(it->second.mod);
                    g_pressStart.erase(it);
                }
            }
            // Skip if the threshold timer already fired the long slot.
            if (have && !fired) {
                if (held >= kLongPressThreshold)
                    runSlot_(effectiveLongSlot_(bd, m), /*firing*/ true, /*pressed*/ false);
                else
                    runSlot_(shortP[m], /*firing*/ true, /*pressed*/ false);
            }
        }
        return true;
    }

    // ⇨ TOGGLE AND HOLD PICK THE HELD SET TOO.
    // This used to be filled in only for Momentary, which was harmless while a
    // soft-key had one set. With sets AND the Behavior combo both exposed, a
    // Toggle key showed its Shift label and fired its PLAIN action, or nothing
    // at all when Plain was empty (Frank 2026-08-18).
    int slotMod = static_cast<int>(bankModifierSnapshot());
    if (bd.behavior == Behavior::Momentary) {
        std::lock_guard<std::mutex> lk(g_pressMx);
        if (pressed) {
            PressRecord rec;
            rec.start = std::chrono::steady_clock::now();
            rec.mod   = bankModifierSnapshot();
            g_pressStart[k] = std::move(rec);
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
    // Heap-allocate — see load() for sizeof(Config) rationale.
    auto tmpPtr = std::make_unique<Config>();
    Config& tmp = *tmpPtr;
    seedFactoryDefaults_(tmp);
    g_cfg.layers[layer] = std::move(tmp.layers[layer]);
    // ⇨ AND THE LAYER'S SOFT-KEYS. They live in a parallel store, so a reset
    // that only swapped the button map left all 144 top-soft-key slots in
    // place — "reset to defaults" that kept most of the layer
    // (Frank 2026-08-18). Same store the export/import pair forgot.
    g_cfg.userQuicks[layer] = std::move(tmp.userQuicks[layer]);
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

// ⇨ A SOFT-KEY LABEL OF LAST RESORT — NEVER THE RAW BUILTIN ID.
// When a slot carries an action but no label of its own, something has to be
// shown, and that something used to be `sp.action`: "switch_bc_8" on the LCD,
// which is code and has no business on the surface (Frank 2026-08-18). It only
// ever surfaced on a MODIFIER SET, because Plain quietly borrowed the key's own
// name — a set deliberately does not, and rightly so.
// Favourite switches get the same wording the factory bank uses ("BC Fav 8"),
// so an unassigned favourite reads the same on either set. Everything else gets
// its registered display name. Capped to the 12-char LCD width by the caller.
bool uf1ControlShowsLabel(ButtonId id)
{
    return id == ButtonId::Uf1ChannelSoftKey
        || (id >= ButtonId::Uf1DisplaySoft1 && id <= ButtonId::Uf1DisplaySoft4);
}

std::string softKeyFallbackLabel(const ActionSlot& sp)
{
    if (sp.action.empty()) return {};
    const bool isCsFav = sp.action.rfind("switch_cs_", 0) == 0;
    const bool isBcFav = sp.action.rfind("switch_bc_", 0) == 0;
    if (isCsFav || isBcFav) {
        const int n = std::atoi(sp.action.c_str() + 10);
        if (n >= 1 && n <= 8) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%s Fav %d", isCsFav ? "CS" : "BC", n);
            return buf;
        }
    }
    if (sp.type == ActionType::Builtin)
        return builtinDisplayName(sp.action);
    return sp.action;   // REAPER command ids etc. — the user can name those
}

bool builtinUsesParam(const std::string& name)
{
    auto it = g_builtins.find(name);
    if (it == g_builtins.end()) return false;
    return it->second.usesParam;
}

// Single source of truth for built-in → category grouping. The Settings
// action picker (SettingsScreen) and the Stream Deck bridge both call this so
// the two can never drift. Keep in sync with builtinCategoryOrder() below.
const char* builtinCategory(const std::string& n)
{
    if (n.rfind("__", 0) == 0) return "";

    if (n.rfind("switch_fav_", 0) == 0 || n.rfind("copy_fav_", 0) == 0
     || n == "fav_cycle"
     || n.rfind("switch_cs_", 0) == 0 || n.rfind("copy_cs_", 0) == 0
     || n == "cs_cycle"
     || n.rfind("switch_bc_", 0) == 0 || n.rfind("copy_bc_", 0) == 0
     || n == "bc_cycle"
     || n == "fav_copy_own_toggle"
     || n == "cs_copy_own_toggle" || n == "bc_copy_own_toggle")
        return "Favourites";

    if (n == "instance_cycle" || n == "fx_cycle"
     || n == "fx_scroll_all"  || n == "instance_scroll_all"
     || n == "fx_move"
     || n == "instance_next"  || n == "instance_prev"
     || n == "bc_track_scroll"
     || n == "bc_track_scroll_select"
     || n == "select_relative"
     || n == "track_scroll"
     || n == "track_select_range"
     || n == "temp_selset_scroll"
     || n == "playhead_nudge"
     || n == "mouse_scroll")
        return "Cycle Actions";

    if (n.rfind("selection_mode_", 0) == 0)
        return "Selection Modes";

    if (n.rfind("encoder_", 0) == 0 || n.rfind("uf1_encoder_", 0) == 0)
        return "Encoder Modes";

    // ⇨ jog_nav_* IS HIDDEN, NOT GONE.
    // The five collective nav builtins are what the cross fired before it became
    // bindable per mode. Their meaning depends on the active mode, which is
    // exactly the thing the Jog Actions replaced, so offering them in the picker
    // only invites the question "what does this one do?" (Frank 2026-08-18).
    // Still registered, because configs written before today point at them and a
    // dangling name would dispatch to nothing.
    if (n.rfind("jog_nav_", 0) == 0) return "";
    if (n.rfind("jog_mode_", 0) == 0)
        return "Jog Modes";

    // What the modes DO, as opposed to which mode is engaged. Kept apart from
    // "Jog Modes" so neither list becomes the leftovers drawer (Frank
    // 2026-08-18): the mode switches there, the named actions here.
    if (n.rfind("jog_", 0) == 0)
        return "Jog Actions";

    if (n == "flip" || n == "pan_force"
     || n == "mixer_toggle" || n == "home"
     || n == "folder_mode" || n == "show_only_selected"
     || n.rfind("ssl_strip_mode_", 0) == 0
     || n.rfind("uf8_plugin_mode_", 0) == 0
     || n == "uc1_outgain_fader_toggle"
     || n == "learn_hud_toggle"
     || n == "touch_to_learn_toggle"
     || n == "focused_panel_toggle"
     || n == "mode_banner_toggle"
     || n == "tcp_follows_selection_toggle"
     || n == "surface_mirror_tcp"
     || n == "surface_mirror_mcp"
     || n.rfind("marker_overlay_", 0) == 0
     || n == "uf1_time_display_step"
     || n == "uf1_flip" || n == "uf1_master"
     || n == "uf1_five_to_eight" || n == "uf1_vpot_reset"
     || n == "uf1_presets"
     || n.rfind("uf1_strip_mode_", 0) == 0
     || n.rfind("uf1_view_", 0) == 0
     || n == "uf1_extender" || n == "uf1_extender_side"
     || n == "restart")
        return "Hardware Modes";

    if (n == "show_focused_plugin_gui"
     || n == "show_fx_chain"
     || n == "close_all_fx_guis"
     || n == "quick_learn"
     || n == "quick_learn_track"
     || n.rfind("plugin_", 0) == 0)
        return "Plug-in";

    if (n.rfind("layer_select", 0) == 0)
        return "Layer";
    if (n.rfind("softkey_bank_", 0) == 0)
        return "Soft-Key Bank";

    if (n == "domain_cs" || n == "domain_bc"
     || n == "ssl_softkey"
     || n.rfind("ssl_bank_", 0) == 0)
        return "SSL";

    if (n == "bank_left"  || n == "bank_right"
     || n == "page_left"  || n == "page_right"
     || n == "bank_by_1_left" || n == "bank_by_1_right"
     || n == "uf1_bank_step" || n == "uf1_page_step"
     || n == "uf1_dyn_bank_page")
        return "Bank / Page";

    if (n.rfind("auto_", 0) == 0) return "Automation";
    if (n.rfind("zoom_", 0) == 0) return "Zoom";

    if (n == "send_this" || n == "recv_this"
     || n.rfind("send_all_", 0) == 0
     || n.rfind("recv_all_", 0) == 0)
        return "Sends / Receives";

    if (n.rfind("selset_", 0) == 0
     || n.rfind("temp_selset_", 0) == 0
     || n.rfind("focus_scope_", 0) == 0) return "Selection Sets";

    if (n.rfind("param_group_", 0) == 0
     || n == "multi_select_as_temp_group_toggle")
        return "Parameter Groups";

    if (n == "selection_clear_all"
     || n == "tracks_arm_all"
     || n == "automation_zero_all")
        return "Tracks";

    if (n.rfind("master_pin_", 0) == 0) return "Master";
    if (n.rfind("brightness_", 0) == 0) return "Brightness";

    if (n == "mod_shift" || n == "mod_cmd" || n == "mod_ctrl"
     || n == "uf1_fine_toggle")
        return "Modifiers";

    if (n.rfind("fx_param_", 0) == 0)
        return "FX Param";

    if (n.rfind("sticky_pot_", 0) == 0)
        return "Sticky Pot";

    return "";
}

// One sentence on what an action DOES, for the picker's tooltip and its search.
//
// The display name is a label, not an explanation. "8 sends of focused track"
// says nothing about the eight FADERS being taken over, and a UF1-only user
// reading it had no way to find out short of binding it and pressing (forum
// 2026-08-25). The names cannot carry the answer: they are one line on a
// hardware LCD.
//
// ⛔ EVERY LINE HERE WAS READ OFF THE HANDLER, never off the name. That is the
// whole cost of this table and the reason it is worth having. When you add a
// builtin, open its registration and describe what the lambda does, including
// what `param` means if it reads one. A plausible invention is worse than the
// name it replaces, because the name at least does not claim to explain.
//
// Coverage is checked, not assumed: every registered builtin resolves here.
// tools/check_builtin_docs.py walks main.cpp's registrations against this file
// and fails on a gap, so a new action cannot ship without a line.
struct BuiltinDoc { const char* name; const char* text; };

// Exact names.
static const BuiltinDoc kBuiltinDocs[] = {
    { "flip",
      "UF8/UC1. Faders and V-Pots swap jobs. Any fader touch in flight is "
      "dropped so the release cannot write a pan value as a volume." },
    { "pan_force",
      "UF8/UC1. The V-Pots leave whatever they were doing and ride track "
      "Pan. The escape hatch out of a cycle or REC mode." },
    { "uf1_flip",
      "UF1. Fader and V-Pot swap jobs: the fader takes Pan, or in Plugin "
      "mode the parameter of the V-Pot you last used, and the knob above "
      "it takes Volume." },
    { "uf1_master",
      "UF1. Puts the strip on the MASTER bus. The whole strip follows, "
      "not just the fader." },
    { "uf1_vpot_reset",
      "UF1. Resets one channel V-Pot's parameter to its factory default. "
      "Param 0 to 3 picks the pot. Channel view only." },
    { "uf1_fine_toggle",
      "UF1. Halves the V-Pot resolution for fine work. Follows the view: "
      "Channel fines the four channel pots, Meter fines the meter pots." },
    { "uf1_extender",
      "UF1. Makes the UF1 the ninth strip of the UF8's bank instead of an "
      "independent channel. Releases a Focus Set pin, the two are "
      "mutually exclusive." },
    { "uf1_extender_side",
      "UF1. Whether the extender sits to the left or the right of the "
      "UF8's eight." },
    { "uf1_presets",
      "UF1. Opens the preset browser for whichever plug-in the current "
      "view resolves. Reads SSL's own library from disk, not REAPER's "
      "preset list." },
    { "uf1_time_display_step",
      "UF1. Steps the big display's time format: Time (minutes:seconds), "
      "then Bars, then Samples. Independent of the units REAPER's own ruler "
      "and transport are set to. The field flashes the name of the format "
      "it just moved to." },
    { "uf1_five_to_eight",
      "UF1. DAW mode: swaps between tracks 1-4 and 5-8 of the window. "
      "Sends mode: pages the window of four sends." },
    { "uf1_sends_receives_toggle",
      "UF1. Flips the routing view between the track's SENDS and its "
      "RECEIVES. The window jumps back to the first group. Also on Shift "
      "plus 5-8." },
    { "uf1_bank_step",
      "UF1. Moves the selection eight tracks at a time. Param sign picks "
      "the direction." },
    { "uf1_page_step",
      "UF1. Steps the soft-key BANK in DAW mode, the plug-in's parameter "
      "PAGE in Plugin mode, and the meter's page in Meter view. Param "
      "sign picks the direction." },
    { "uf1_dyn_bank_page",
      "UF1. Pages WITHIN the active dynamic soft-key bank (the FX list, "
      "the colours). Does nothing on a static or single-page bank, "
      "deliberately, rather than moving something else." },
    { "uf1_view_cycle",
      "UF1. Steps through the four modes in order: Plugin, DAW, Meter, "
      "Sends." },
    { "ssl_strip_mode_toggle",
      "UF8/UC1. The fader drives the SSL channel strip's own Fader Level "
      "instead of REAPER's track volume." },
    { "ssl_strip_mode_toggle_with_gui",
      "UF8/UC1. As SSL Strip Mode, and opens the strip's plug-in window "
      "with it." },
    { "uf1_strip_mode_toggle",
      "UF1. The fader drives the SSL channel strip's own Out-Gain instead "
      "of REAPER's track volume." },
    { "uf1_strip_mode_toggle_with_gui",
      "UF1. As UF1 Strip Mode, and opens the strip's plug-in window with "
      "it." },
    { "uf8_plugin_mode_toggle",
      "UF8. All eight strips become the controls of ONE learned plug-in "
      "instead of eight tracks. Needs a plug-in that qualifies, or the "
      "mode drops straight back out." },
    { "uf8_plugin_mode_toggle_with_gui",
      "UF8. As UF8 Plug-in Mode, and pops that plug-in's window on entry, "
      "closing it on exit." },
    { "uc1_outgain_fader_toggle",
      "UC1. Switches the Out-Gain knob between the mapped strip parameter "
      "and REAPER's track volume. Works on tracks with no channel strip "
      "at all." },
    { "restart",
      "Re-opens the USB devices without the trip through REAPER's "
      "Preferences. This restarts the DEVICES, not the extension: new "
      "code still needs a REAPER restart." },
    { "mixer_toggle",
      "Opens or closes the Rea-Sixty Settings window." },
    { "home",
      "Clears every routing override at once and returns faders and "
      "V-Pots to plain track volume and pan. Also drops the active Quick "
      "on this layer." },
    { "selection_mode_norm",
      "Back to NORM: SEL selects, V-Pots pan. Always sets it, never "
      "toggles, so it is a safe panic button." },
    { "selection_mode_rec",
      "SEL arms the track for recording and its LED shows the arm state. "
      "Firing it again returns to NORM." },
    { "selection_mode_rec_mon",
      "SEL arms the track AND flips input monitoring. Firing it again "
      "returns to NORM." },
    { "selection_mode_auto",
      "SEL colours by the track's automation mode and the V-Pot scrolls "
      "through the modes. Leaving the mode reverts tracks the Selection "
      "Set had armed." },
    { "selection_mode_instance",
      "Each strip's V-Pot walks EVERY FX on that strip's track. Push "
      "toggles the active one's window. Called FX Cycle; the internal "
      "name says instance for older config files." },
    { "selection_mode_instance_cycle",
      "Each strip's V-Pot walks only the SSL-mapped or learned instances "
      "on that track, so Strip Mode and Plug-in Mode follow along. Does "
      "nothing on a track with fewer than two." },
    { "selection_mode_dynamount",
      "The enabled robotic mic stands take over N strips: fader drives "
      "distance or left-right, V-Pot nudges rotation. The rest stay "
      "ordinary tracks." },
    { "encoder_mode_dispatch",
      "Runs whatever the Channel Encoder's current mode says. This is the "
      "action the encoder itself carries; the modes below choose what it "
      "does." },
    { "encoder_nav",
      "Channel Encoder selects the previous or next track. The home "
      "position." },
    { "encoder_nudge",
      "Channel Encoder nudges the playhead. The step size follows "
      "REAPER's own Nudge setting." },
    { "encoder_focus",
      "Channel Encoder acts as a mouse wheel under the cursor, so it "
      "scrolls whatever you are pointing at." },
    { "encoder_markers",
      "Channel Encoder jumps to the previous or next marker." },
    { "encoder_bank_by_1",
      "Channel Encoder banks the surface one strip at a time instead of "
      "eight." },
    { "encoder_last_param",
      "Channel Encoder drives the parameter you last touched, wherever it "
      "lives." },
    { "encoder_instance",
      "Channel Encoder walks the SSL-mapped or learned instances on the "
      "focused track." },
    { "encoder_fx_cycle",
      "Channel Encoder walks EVERY FX on the focused track, with no "
      "instance filter." },
    { "encoder_fx_scroll_all",
      "As FX Cycle, but at the end of a track's chain it carries on into "
      "the next track's." },
    { "encoder_instance_scroll_all",
      "As Instance Cycle, but at the end of a track's chain it carries on "
      "into the next track's." },
    { "encoder_fx_move",
      "Channel Encoder moves the active FX up or down inside the focused "
      "track's chain. Stops at the ends." },
    { "encoder_cs_cycle",
      "Channel Encoder steps the active Channel Strip through your "
      "favourites, carrying shared values across each swap." },
    { "encoder_bc_cycle",
      "Channel Encoder steps the active Bus Compressor through your "
      "favourites." },
    { "encoder_fav_cycle",
      "Channel Encoder steps through favourites of whichever class you "
      "last worked on, Channel Strip or Bus Compressor." },
    { "encoder_selset_cycle",
      "Channel Encoder walks the Selection Sets that have something in "
      "them, off included." },
    { "select_relative",
      "Encoder action. Selects the previous or next track." },
    { "track_scroll",
      "Encoder action. Scrolls the track view without changing the "
      "selection." },
    { "track_select_range",
      "Encoder action. Extends the selection to the neighbouring track, "
      "the way Shift with the arrow keys does." },
    { "playhead_nudge",
      "Encoder action. Nudges the playhead by REAPER's own Nudge setting." },
    { "mouse_scroll",
      "Encoder action. Sends a mouse wheel to whatever sits under the "
      "cursor." },
    { "instance_cycle",
      "Encoder action. Walks the instances of the FOCUSED CLASS on the "
      "current track." },
    { "fx_cycle",
      "Encoder action. Walks EVERY FX on the focused track, not just the "
      "focused class." },
    { "fx_scroll_all",
      "Encoder action, across tracks: at the end of one chain it "
      "continues into the next track's." },
    { "instance_scroll_all",
      "Encoder action, across tracks: at the end of one chain it "
      "continues into the next track's instances." },
    { "fx_move",
      "Encoder action. Moves the active FX up or down inside its track's "
      "chain." },
    { "cs_cycle",
      "Encoder action. Steps the active Channel Strip through your "
      "favourites." },
    { "bc_cycle",
      "Encoder action. Steps the active Bus Compressor through your "
      "favourites." },
    { "fav_cycle",
      "Encoder action. Steps through favourites of whichever class you "
      "last worked on." },
    { "selset_cycle",
      "Encoder action. Walks the Selection Sets that have something in "
      "them. Leaves the Channel Encoder Mode free for something else." },
    { "temp_selset_scroll",
      "Encoder action. Scrolls through the Focus Set's members." },
    { "bc_track_scroll",
      "Encoder action, UC1. Moves the Bus Compressor's anchor track "
      "without changing the selection." },
    { "bc_track_scroll_select",
      "Encoder action, UC1. Moves the Bus Compressor's anchor track AND "
      "selects it." },
    { "instance_next",
      "Next instance of the focused class on the current track. Wraps "
      "around." },
    { "instance_prev",
      "Previous instance of the focused class on the current track. Wraps "
      "around." },
    { "plugin_bypass",
      "Bypasses or un-bypasses the ACTIVE FX, the one the cycle cursor "
      "points at. The LED is lit while it is bypassed." },
    { "plugin_offline",
      "Takes the ACTIVE FX offline or back online. Offline frees its CPU "
      "and its state entirely." },
    { "plugin_preset_next",
      "Next preset on the ACTIVE FX." },
    { "plugin_preset_prev",
      "Previous preset on the ACTIVE FX." },
    { "plugin_preset_cycle",
      "Encoder action. Scrolls the ACTIVE FX's presets." },
    { "plugin_move_up",
      "Moves the ACTIVE FX one visible slot up its track's chain. Slides "
      "into empty slots and reorders past real ones; the cursor follows "
      "so the next press still targets it." },
    { "plugin_move_down",
      "Moves the ACTIVE FX one visible slot down its track's chain. The "
      "cursor follows the plug-in." },
    { "show_focused_plugin_gui",
      "Opens or closes the floating window of the focused instance on the "
      "focused track." },
    { "show_fx_chain",
      "Opens or closes the focused track's FX chain window." },
    { "close_all_fx_guis",
      "Closes every floating FX window and every chain window in the "
      "project. The tidy-up after a long session of opening things." },
    { "domain_cs",
      "Points the surfaces at the CHANNEL STRIP on the focused track and "
      "latches the soft-keys to it." },
    { "domain_bc",
      "Points the surfaces at the BUS COMPRESSOR on the focused track and "
      "latches the soft-keys to it." },
    { "cs_copy_own_toggle",
      "Whether switching a Channel Strip favourite carries the current "
      "values across or restores that favourite's own saved settings. Lit "
      "means own settings." },
    { "bc_copy_own_toggle",
      "Whether switching a Bus Compressor favourite carries the current "
      "values across or restores its own saved settings. Lit means own "
      "settings." },
    { "fav_copy_own_toggle",
      "The same copy-or-own switch, for whichever class you last worked "
      "on." },
    { "quick_learn",
      "Sweeps the whole PROJECT for plug-ins that can be mapped "
      "automatically." },
    { "quick_learn_track",
      "Sweeps the FOCUSED TRACK for plug-ins that can be mapped "
      "automatically." },
    { "touch_to_learn_toggle",
      "Arms learning without the HUD: touch a control on the surface, it "
      "blinks, then wiggle a plug-in parameter to bind the two. Stays "
      "armed for the next control until you switch it off." },
    { "learn_hud_toggle",
      "Shows or hides the Learn HUD, the on-screen map of the focused "
      "plug-in's assignments." },
    { "sticky_pot_get_next",
      "Arms the pin: the next plug-in parameter you touch becomes that "
      "track's V-Pot pin. Pressing the V-Pot while armed clears it "
      "instead." },
    { "sticky_pot_toggle",
      "Suspends or resumes every pinned parameter at once. The pins "
      "themselves are kept." },
    { "selset_recall",
      "Recalls Selection Set `param` (1 to 8). Firing the set that is "
      "already active switches the filter off." },
    { "selset_save",
      "Snapshots the current REAPER track selection into slot `param` (1 "
      "to 8)." },
    { "temp_selset_add",
      "Adds the selected tracks to the Focus Set. Members stick to the "
      "leftmost strips; nothing is hidden." },
    { "temp_selset_remove",
      "Removes the selected tracks from the Focus Set." },
    { "temp_selset_toggle_selected",
      "Adds the selected tracks to the Focus Set, or removes them if they "
      "are already in it." },
    { "temp_selset_set_from_selection",
      "Replaces the whole Focus Set with the current selection." },
    { "temp_selset_clear",
      "Empties the Focus Set." },
    { "temp_selset_recall",
      "Pins the Focus Set, or releases it. While pinned its members hold "
      "the leftmost strips." },
    { "temp_selset_pin_focused",
      "Puts the focused track into the Focus Set and pins it in one "
      "press." },
    { "temp_selset_pin_uf1_channel",
      "Puts the channel the UF1 is showing into the Focus Set and pins "
      "it. The UF1's SOFT key carries this by default." },
    { "focus_scope_cycle",
      "Steps where the Focus Set pin applies: Both surfaces, UF1 only, "
      "UF8 only." },
    { "focus_scope_both",
      "The Focus Set pin applies to both surfaces." },
    { "focus_scope_uf1",
      "The Focus Set pin applies to the UF1 only." },
    { "focus_scope_uf8",
      "The Focus Set pin applies to the UF8 only." },
    { "param_group_remove_all",
      "Takes the selected tracks out of every parameter group." },
    { "multi_select_as_temp_group_toggle",
      "While on, selecting several tracks behaves like a temporary "
      "parameter group without setting one up." },
    { "selection_clear_all",
      "Deselects every track in the project." },
    { "tracks_arm_all",
      "Arms every track for recording, or unarms them all if they already "
      "are. REAPER's own action only arms." },
    { "automation_zero_all",
      "Puts every track's automation mode back to Trim/Read." },
    { "folder_mode",
      "The surface shows folder parents only. Long-pressing SEL on a "
      "parent spills its children." },
    { "show_only_selected",
      "The surface shows only the selected tracks." },
    { "tcp_follows_selection_toggle",
      "Whether REAPER's track panel scrolls to follow what you select on "
      "the surface." },
    { "surface_mirror_tcp",
      "The surfaces show what the TRACK panel shows, hidden tracks "
      "included." },
    { "surface_mirror_mcp",
      "The surfaces show what the MIXER shows." },
    { "bank_left",
      "Banks the surface left by a full window. In UF8 Plug-in Mode it "
      "steps the fader bank instead, for plug-ins with more than eight "
      "controls." },
    { "bank_right",
      "Banks the surface right by a full window. In UF8 Plug-in Mode it "
      "steps the fader bank instead." },
    { "bank_by_1_left",
      "Banks the surface left by ONE strip. In a routing view it pages "
      "one send instead." },
    { "bank_by_1_right",
      "Banks the surface right by ONE strip. In a routing view it pages "
      "one send instead." },
    { "page_left",
      "Previous soft-key bank." },
    { "page_right",
      "Next soft-key bank." },
    { "softkey_bank_select",
      "Picks a soft-key bank outright: param 0 is V-POT, 1 to 5 are the "
      "numbered banks. Also clears the Pan override." },
    { "ssl_softkey",
      "Fires the SSL soft-key at position `param` (0 to 7) in whatever "
      "bank is showing, so the row changes meaning as you step banks." },
    { "master_pin_strip1",
      "UF8. Parks the Master bus on physical strip 1. Firing it again "
      "releases it." },
    { "master_pin_strip8",
      "UF8. Parks the Master bus on physical strip 8. Firing it again "
      "releases it." },
    { "send_this",
      "UF8/UC1. The eight strips leave the track bank and become the "
      "focused track's first eight SENDS. Param 0 puts them on the "
      "faders, 1 on the V-Pots. Nothing on a one-strip surface." },
    { "recv_this",
      "UF8/UC1. The eight strips leave the track bank and become the "
      "focused track's first eight RECEIVES. Param 0 puts them on the "
      "faders, 1 on the V-Pots. Nothing on a one-strip surface." },
    { "marker_overlay_toggle",
      "Turns the UF8's strips into a jump panel for markers AND regions." },
    { "marker_overlay_markers_only_toggle",
      "As Nav Mode, showing markers only, with no drilling into regions." },
    { "marker_overlay_regions_only_toggle",
      "As Nav Mode, showing regions only, with no drilling in." },
    { "focused_panel_toggle",
      "Shows or hides the frameless focused-track panel over the Arrange "
      "view." },
    { "mode_banner_toggle",
      "Whether a banner flashes on screen when the Selection or Encoder "
      "mode changes." },
    { "brightness_leds_up",
      "Button LEDs one step brighter, on every connected surface." },
    { "brightness_leds_down",
      "Button LEDs one step dimmer." },
    { "brightness_lcds_up",
      "Displays one step brighter." },
    { "brightness_lcds_down",
      "Displays one step dimmer." },
    { "brightness_both_up",
      "LEDs and displays together, one step brighter." },
    { "brightness_both_down",
      "LEDs and displays together, one step dimmer." },
    { "mod_shift",
      "Holds the Shift modifier while pressed, so other keys fire their "
      "Shift assignment. Double-clicking latches it on; the next press "
      "releases it. Param 1 makes it a plain toggle." },
    { "mod_cmd",
      "Holds the Cmd modifier while pressed. Param 1 makes it a toggle "
      "instead." },
    { "mod_ctrl",
      "Holds the Ctrl modifier while pressed. Param 1 makes it a toggle "
      "instead." },
    { "jog_mode_cycle",
      "UF1. Steps the jog wheel through its six modes." },
    { "jog_mode_playhead",
      "UF1. The jog wheel moves the playhead." },
    { "jog_mode_scrub",
      "UF1. The jog wheel scrubs the audio." },
    { "jog_mode_items",
      "UF1. The jog wheel works on media items." },
    { "jog_mode_envelope",
      "UF1. The jog wheel works on envelope points." },
    { "jog_mode_razor",
      "UF1. The jog wheel works on razor selections." },
    { "jog_mode_fades",
      "UF1. The jog wheel sets fade length. Which fade is decided by the "
      "edge you aimed at and what lies next to it." },
    { "jog_razor_whole",
      "Razor mode: aim at the whole razor area rather than one edge." },
    { "jog_razor_left",
      "Razor mode: aim at the left edge, which is then what the wheel "
      "moves." },
    { "jog_razor_right",
      "Razor mode: aim at the right edge." },
    { "jog_razor_top",
      "Razor mode: aim at the top edge." },
    { "jog_razor_bottom",
      "Razor mode: aim at the bottom edge." },
    { "jog_env_point_prev",
      "Envelope mode: move to the previous point." },
    { "jog_env_point_next",
      "Envelope mode: move to the next point." },
    { "jog_env_point_prev_add",
      "Envelope mode: previous point, adding it to the selection." },
    { "jog_env_point_next_add",
      "Envelope mode: next point, adding it to the selection." },
    { "jog_env_lane_up",
      "Envelope mode: move to the lane above." },
    { "jog_env_lane_down",
      "Envelope mode: move to the lane below." },
    { "jog_env_target_toggle",
      "Envelope mode: whether the wheel edits the selected points or "
      "moves the playhead." },
    { "jog_item_prev",
      "Items mode: move to the previous item." },
    { "jog_item_next",
      "Items mode: move to the next item." },
    { "jog_item_prev_add",
      "Items mode: previous item, adding it to the selection." },
    { "jog_item_next_add",
      "Items mode: next item, adding it to the selection." },
    { "jog_item_track_up",
      "Items mode: move to the item on the track above." },
    { "jog_item_track_down",
      "Items mode: move to the item on the track below." },
    { "jog_item_track_up_add",
      "Items mode: item on the track above, adding it to the selection." },
    { "jog_item_track_down_add",
      "Items mode: item on the track below, adding it to the selection." },
    { "jog_fade_left",
      "Fades mode: aim at the fade-in, or step to the previous item when "
      "the cross is set to walk." },
    { "jog_fade_right",
      "Fades mode: aim at the fade-out, or step to the next item when the "
      "cross is set to walk." },
    { "jog_fade_up",
      "Fades mode: next fade shape, or the item above when the cross is "
      "set to walk." },
    { "jog_fade_down",
      "Fades mode: previous fade shape, or the item below when the cross "
      "is set to walk." },
    { "jog_fade_left_add",
      "Fades mode: as fade-in / previous item, adding to the selection." },
    { "jog_fade_right_add",
      "Fades mode: as fade-out / next item, adding to the selection." },
    { "jog_fade_up_add",
      "Fades mode: as next shape / item above, adding to the selection." },
    { "jog_fade_down_add",
      "Fades mode: as previous shape / item below, adding to the "
      "selection." },
    { "jog_fade_nav_toggle",
      "Fades mode: whether the nav cross aims at fades or walks between "
      "items." },
    { "jog_fade_follow_toggle",
      "Fades mode: whether the view follows the fade you are working on. "
      "Switching it off restores the view you had." },
    { "jog_zoom_selection",
      "Zooms to the current selection, and back out on a second press." },
    { "jog_content_drag",
      "Hold to drag the content under the razor or the held item with the "
      "wheel. Needs Hold behaviour on the key, which the factory binding "
      "sets." },
    { "jog_nav_left",
      "The collective nav-cross action from before the cross became "
      "bindable per mode. Kept so older configs keep working; bind the "
      "per-mode actions instead." },
    { "jog_nav_right",
      "The collective nav-cross action from before the cross became "
      "bindable per mode. Kept for older configs." },
    { "jog_nav_up",
      "The collective nav-cross action from before the cross became "
      "bindable per mode. Kept for older configs." },
    { "jog_nav_down",
      "The collective nav-cross action from before the cross became "
      "bindable per mode. Kept for older configs." },
    { "jog_nav_center",
      "The collective nav-cross centre from before the cross became "
      "bindable per mode. Kept for older configs." },
    { "zoom_up",
      "Zoom in vertically." },
    { "zoom_down",
      "Zoom out vertically." },
    { "zoom_left",
      "Zoom out horizontally." },
    { "zoom_right",
      "Zoom in horizontally." },
    { "zoom_center",
      "Zoom to fit the whole project." },
    { "fx_param_inc",
      "Steps one learned FX-Learn slot up on the focused track's plug-in. "
      "The slot, step size and wrap are set in the picker; the slot's own "
      "range and curve apply." },
    { "fx_param_dec",
      "Steps one learned FX-Learn slot down. Same settings as step up, "
      "opposite direction." },
    { "__reaper_action__",
      "Internal. Carries a plain REAPER action id; the picker offers "
      "those through the Native list instead." },
};

// Name FAMILIES, matched on the prefix after the exact table misses. Order
// matters where one prefix contains another: the longer, more specific entry
// (auto_off_global) has to come before the shorter one (auto_off), which is why
// this is an ordered list and not a map.
static const BuiltinDoc kBuiltinDocPrefixes[] = {
    { "send_all_",
      "UF8/UC1. All eight strips show the SAME send number, one per "
      "banked track, instead of the tracks themselves. Param 0 puts them "
      "on the faders, 1 on the V-Pots." },
    { "recv_all_",
      "UF8/UC1. All eight strips show the SAME receive number, one per "
      "banked track. Param 0 puts them on the faders, 1 on the V-Pots." },
    { "switch_cs_",
      "Replaces the active Channel Strip with this favourite, on every "
      "selected track, or on the focused one if nothing is selected." },
    { "copy_cs_",
      "Inserts this Channel Strip favourite BELOW the active one with the "
      "same settings and bypasses the original, so you can A/B them." },
    { "switch_bc_",
      "Replaces the active Bus Compressor with this favourite, on every "
      "selected track, or on the focused one if nothing is selected." },
    { "copy_bc_",
      "Inserts this Bus Compressor favourite BELOW the active one with "
      "the same settings and bypasses the original." },
    { "switch_fav_",
      "Replaces the active plug-in with this favourite, using whichever "
      "class you last worked on. Lit when that favourite is already the "
      "one on the track." },
    { "copy_fav_",
      "Inserts this favourite below the active plug-in with the same "
      "settings and bypasses the original, using whichever class you last "
      "worked on." },
    { "param_group_add_",
      "Adds every selected track to this parameter group." },
    { "param_group_clear_",
      "Strips every track in the project of membership in this parameter "
      "group." },
    { "param_group_toggle_",
      "Switches this parameter group on or off. The LED is lit while it "
      "is active." },
    { "layer_select_",
      "Switches the surface to this binding layer." },
    { "softkey_bank_",
      "Picks this soft-key bank directly. Bank 0 is the V-POT row." },
    { "ssl_bank_",
      "Focuses one fixed parameter of this SSL bank, whatever the current "
      "PAGE bank is. The picker names the parameter." },
    { "auto_off_global",
      "Forces EVERY track to automation Off, overriding their own modes." },
    { "auto_read_global",
      "Forces every track to automation Read." },
    { "auto_write_global",
      "Forces every track to automation Write." },
    { "auto_trim_global",
      "Forces every track to automation Trim." },
    { "auto_latch_global",
      "Forces every track to automation Latch." },
    { "auto_latch_prv_global",
      "Forces every track to automation Latch Preview." },
    { "auto_touch_global",
      "Forces every track to automation Touch." },
    { "auto_off",
      "Sets the selected tracks to automation Off." },
    { "auto_read",
      "Sets the selected tracks to automation Read." },
    { "auto_write",
      "Sets the selected tracks to automation Write." },
    { "auto_trim",
      "Sets the selected tracks to automation Trim." },
    { "auto_latch_prv",
      "Sets the selected tracks to automation Latch Preview." },
    { "auto_latch",
      "Sets the selected tracks to automation Latch. The LED also reports "
      "Latch Preview, which shares the lamp." },
    { "auto_touch",
      "Sets the selected tracks to automation Touch." },
    { "uf1_view_",
      "Puts the UF1 straight into that mode. Bindable from any surface, "
      "so one key per mode beats cycling through all four." },
};

const char* builtinDescription(const std::string& n)
{
    for (const auto& d : kBuiltinDocs)
        if (n == d.name) return d.text;
    for (const auto& d : kBuiltinDocPrefixes)
        if (n.rfind(d.name, 0) == 0) return d.text;
    return "";
}

const std::vector<const char*>& builtinCategoryOrder()
{
    static const std::vector<const char*> kCats = {
        "Favourites", "Cycle Actions", "Selection Modes", "Encoder Modes",
        "Jog Modes", "Jog Actions",
        "Hardware Modes", "Plug-in", "Layer", "Soft-Key Bank", "SSL",
        "Bank / Page", "Automation", "Zoom", "Sends / Receives",
        "Selection Sets", "Parameter Groups", "Tracks", "Master",
        "Brightness", "Modifiers", "FX Param", "Sticky Pot",
    };
    return kCats;
}

// bit0 = UF8, bit1 = UC1, bit2 = UF1. Universal builtins → all three.
//
// FALLBACK ONLY. This decides from the id's prefix plus the hand-maintained
// exception blocks below, so a device-specific builtin whose name carries no
// prefix falls through to "universal" without a word. New builtins set
// BuiltinDescriptor::deviceMask at their registration site instead; that value
// wins in builtinShownForId. Do not grow the exception list.
uint8_t builtinDeviceMask(const std::string& n)
{
    // The UF1's four HARDWARE MODES are a UF1 property you set from ANYWHERE —
    // that is the whole reason they were made bindable (2026-08-11: "these make
    // each one bindable to any button on any surface"). The uf1_ prefix below
    // would have scoped them to the UF1's own picker, where they are the one
    // place you do NOT need them: MODE + soft-key already does it there. Frank
    // went looking for "UF1 METER" on a UF8 top soft-key and found nothing.
    // (The jog modes dodged this by accident — they are named jog_mode_*.)
    if (n.rfind("uf1_view_", 0) == 0) return 0b111;
    if (n.rfind("uf1_", 0) == 0) return 0b100;   // UF1-only (uf1_flip/master/uf1_encoder_*/…)
    if (n.rfind("uf8_", 0) == 0) return 0b001;   // UF8-only (uf8_plugin_mode_*)
    if (n.rfind("uc1_", 0) == 0) return 0b010;   // UC1-only (uc1_outgain_fader_toggle)
    // UF8/UC1-semantics builtins that write SHARED state the UF1 does NOT read,
    // so they are a pure no-op on the UF1 — hide them from the UF1 picker, keep
    // them on UF8 + UC1 (same "not UF1" scope as the 8-strip SSL set below). The
    // uf1_ check above already claimed the UF1 counterparts (uf1_encoder_* /
    // uf1_flip / uf1_strip_mode), so this only catches the UF8/UC1 originals:
    //   · encoder_* / encoder_mode_dispatch → g_encoderMode (UF1 = g_uf1EncoderMode)
    //   · flip                              → g_flip         (UF1 = g_uf1Flip)
    //   · ssl_strip_mode_*                  → g_pluginFaderMode (UF1 = g_uf1StripMode)
    if (n.rfind("encoder_", 0) == 0
     || n == "flip"
     || n.rfind("ssl_strip_mode_", 0) == 0) return 0b011;   // UF8 + UC1, not UF1
    // 8-strip / per-strip SSL builtins — meaningless on the single-strip UF1
    // (the blueprint's UF1 drop-list). Kept on UF8 + UC1, hidden on UF1 only.
    if (n.rfind("send_all_", 0) == 0 || n.rfind("recv_all_", 0) == 0
     || n == "ssl_softkey" || n.rfind("ssl_bank_", 0) == 0
     || n.rfind("softkey_bank_", 0) == 0 || n.rfind("master_pin_", 0) == 0)
        return 0b011;                            // UF8 + UC1, not UF1
    return 0b111;                                // universal
}

int builtinDeviceForId(ButtonId id)
{
    if (id >= ButtonId::Uf1VpotAbovePush && id <= ButtonId::Uf1Jog)   return 2;
    if (id >= ButtonId::Uc1Encoder1     && id <= ButtonId::Uc1Btn360) return 1;
    return 0;
}

bool builtinShownForId(const std::string& n, ButtonId id)
{
    // An explicit mask on the descriptor wins over the name-prefix table.
    // 0 means the registration site said nothing, so fall back. See the
    // BuiltinDescriptor::deviceMask comment for why the table alone is not
    // enough.
    uint8_t mask = 0;
    auto it = g_builtins.find(n);
    if (it != g_builtins.end()) mask = it->second.deviceMask;
    if (mask == 0) mask = builtinDeviceMask(n);
    return (mask & (1u << builtinDeviceForId(id))) != 0;
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
    if (idx >= kLastFiredModSize) return Modifier::Plain;
    const auto v = g_lastFiredMod[idx].load(std::memory_order_relaxed);
    const uint8_t mod = v & 0x7F;  // strip long-press bit
    if (mod >= kModifierCount) return Modifier::Plain;
    return static_cast<Modifier>(mod);
}

bool lastFiredWasLongPress(ButtonId id)
{
    const auto idx = static_cast<size_t>(id);
    if (idx >= kLastFiredModSize) return false;
    return (g_lastFiredMod[idx].load(std::memory_order_relaxed) & 0x80) != 0;
}

} // namespace uf8::bindings
