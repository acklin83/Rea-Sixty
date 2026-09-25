//
// KeyMacroSend — the half of the key macros that needs somewhere to type.
//
// Split out of KeyMacro.cpp so that parsing a chord does not drag the REAPER SDK
// with it: ORC reads the same bindings file and has to understand "cmd+s" even
// though it has no window to send it to. Rea-Sixty links this; ORC does not, and
// its bindings host simply leaves sendKeyChord unset.
//
#include "KeyMacro.h"

#include "reaper_plugin.h"            // pulls in swell.h (HWND/SendMessage/VK_*)
#include "reaper_plugin_functions.h"  // GetMainHwnd

namespace keymacro {

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
