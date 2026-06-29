#include "KeyMacro.h"

#include "reaper_plugin.h"            // pulls in swell.h (HWND/SendMessage/VK_*)
#include "reaper_plugin_functions.h"  // GetMainHwnd

#if defined(_WIN32)
#  include <windows.h>
#endif

#include <cctype>
#include <cstring>
#include <vector>

namespace keymacro {
namespace {

struct NamedKey { const char* name; int vk; };

// Non-alphanumeric keys with a stable neutral name. Letters (a-z) and digits
// (0-9) are handled programmatically in parse/format, not listed here.
const NamedKey kNamedKeys[] = {
    {"tab", VK_TAB},        {"enter", VK_RETURN}, {"return", VK_RETURN},
    {"esc", VK_ESCAPE},     {"escape", VK_ESCAPE},
    {"space", VK_SPACE},    {"backspace", VK_BACK}, {"delete", VK_DELETE},
    {"del", VK_DELETE},     {"insert", VK_INSERT}, {"ins", VK_INSERT},
    {"home", VK_HOME},      {"end", VK_END},
    {"pageup", VK_PRIOR},   {"pagedown", VK_NEXT},
    {"left", VK_LEFT},      {"right", VK_RIGHT},
    {"up", VK_UP},          {"down", VK_DOWN},
    {"f1", VK_F1},   {"f2", VK_F2},   {"f3", VK_F3},   {"f4", VK_F4},
    {"f5", VK_F5},   {"f6", VK_F6},   {"f7", VK_F7},   {"f8", VK_F8},
    {"f9", VK_F9},   {"f10", VK_F10}, {"f11", VK_F11}, {"f12", VK_F12},
    {"f13", VK_F13}, {"f14", VK_F14}, {"f15", VK_F15}, {"f16", VK_F16},
    {"f17", VK_F17}, {"f18", VK_F18}, {"f19", VK_F19}, {"f20", VK_F20},
};

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Resolve a single non-modifier token to a vkey. 0 if unknown.
int tokenToVk(const std::string& tokLower) {
    if (tokLower.size() == 1) {
        const unsigned char c = static_cast<unsigned char>(tokLower[0]);
        if (c >= 'a' && c <= 'z') return 'A' + (c - 'a');
        if (c >= '0' && c <= '9') return c;   // VK '0'..'9' == ASCII
    }
    for (const auto& k : kNamedKeys)
        if (tokLower == k.name) return k.vk;
    return 0;
}

// Resolve a modifier token to its Mod flag. 0 if not a modifier.
uint32_t tokenToMod(const std::string& tokLower) {
    if (tokLower == "shift") return ModShift;
    if (tokLower == "ctrl" || tokLower == "control") return ModCtrl;
    if (tokLower == "alt" || tokLower == "opt" || tokLower == "option") return ModAlt;
    if (tokLower == "cmd" || tokLower == "command" || tokLower == "win" ||
        tokLower == "super" || tokLower == "meta")
        return ModCmd;
    return 0;
}

} // namespace

bool parseChord(const std::string& s, KeyChord& out) {
    out = KeyChord{};
    std::string cur;
    bool haveKey = false;
    auto flush = [&]() -> bool {
        if (cur.empty()) return true;
        const std::string low = toLower(cur);
        cur.clear();
        if (const uint32_t m = tokenToMod(low)) { out.mods |= m; return true; }
        const int vk = tokenToVk(low);
        if (vk == 0) return false;          // unknown token
        if (haveKey) return false;          // more than one main key
        out.vk = vk; haveKey = true;
        return true;
    };
    for (const char ch : s) {
        if (ch == '+' || ch == ' ' || ch == '\t') {
            if (!flush()) return false;
        } else {
            cur.push_back(ch);
        }
    }
    if (!flush()) return false;
    return out.valid();
}

std::string keyName(int vk) {
    if (vk >= 'A' && vk <= 'Z') return std::string(1, static_cast<char>('a' + (vk - 'A')));
    if (vk >= '0' && vk <= '9') return std::string(1, static_cast<char>(vk));
    for (const auto& k : kNamedKeys)
        if (vk == k.vk) return k.name;   // first match wins (canonical name)
    return {};
}

std::string formatChord(const KeyChord& c) {
    if (!c.valid()) return {};
    std::string out;
    auto add = [&](const char* tok) {
        if (!out.empty()) out += " + ";
        out += tok;
    };
    if (c.mods & ModCtrl)  add("ctrl");
    if (c.mods & ModAlt)   add("alt");
    if (c.mods & ModShift) add("shift");
    if (c.mods & ModCmd)   add("cmd");
    const std::string k = keyName(c.vk);
    if (!k.empty()) add(k.c_str());
    return out;
}

bool isModifierVk(int vk) {
    switch (vk) {
        case VK_SHIFT: case VK_CONTROL: case VK_MENU:
        case VK_LWIN: case VK_RWIN:
            return true;
#if defined(_WIN32)
        // Windows also reports the left/right modifier variants.
        case VK_LSHIFT: case VK_RSHIFT:
        case VK_LCONTROL: case VK_RCONTROL:
        case VK_LMENU: case VK_RMENU:
            return true;
#endif
        default:
            return false;
    }
}

void sendChordToReaper(const KeyChord& c) {
    if (!c.valid() || !GetMainHwnd || !kbd_translateAccelerator || !SectionFromUniqueID)
        return;
    HWND h = GetMainHwnd();
    if (!h) return;

    // Feed REAPER's own accelerator processor: it looks up the key bound to
    // this chord and fires the action. Modifiers ride in MSG.lParam as the
    // accelerator FVIRT flags (SDK: IS_MSG_VIRTKEY(msg) == lParam & FVIRTKEY,
    // reaper_plugin.h:1547); wParam is the virtual key. No SWELL window calls,
    // no GetAsyncKeyState dependency.
    //
    // SWELL's modifier-flag mapping differs by platform and Ctrl must NOT be
    // conflated with Cmd on macOS (Frank 2026-06-29):
    //   macOS:   Cmd (⌘) -> FCONTROL, physical Control (⌃) -> FLWIN
    //   Win/Lin: Ctrl     -> FCONTROL, Win/Super            -> FLWIN
    // So "cmd+s" fires Save (⌘S) on mac while "ctrl+s" stays a distinct ⌃S.
    LPARAM flags = FVIRTKEY;
#if defined(__APPLE__)
    if (c.mods & ModCmd)   flags |= FCONTROL;   // ⌘
    if (c.mods & ModCtrl)  flags |= FLWIN;      // physical ⌃ (distinct from ⌘)
#else
    if (c.mods & ModCtrl)  flags |= FCONTROL;
  #ifdef FLWIN
    if (c.mods & ModCmd)   flags |= FLWIN;      // Win / Super key
  #endif
#endif
    if (c.mods & ModAlt)   flags |= FALT;
    if (c.mods & ModShift) flags |= FSHIFT;

    MSG msg{};
    msg.hwnd    = h;
    msg.message = WM_KEYDOWN;
    msg.wParam  = static_cast<WPARAM>(c.vk);
    msg.lParam  = flags;

    KbdSectionInfo* mainSection = SectionFromUniqueID(0);  // 0 = main actions
    kbd_translateAccelerator(h, &msg, mainSection);
}

} // namespace keymacro
