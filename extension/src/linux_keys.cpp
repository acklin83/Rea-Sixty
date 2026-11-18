// Linux keyboard modifiers for the surfaces: SOLO/CUT modes, Settings,
// Behaviour, Keyboard, Alt-drag snap-back, Shift for fine, the FX Learn layers.
// Until 18.11.2026 the host*Held_ readers in main.cpp returned false on Linux
// ("TBD" since May), so all of that did nothing there while the Settings
// options were still on show (Frank: "sollte doch auch gehen!").
//
// WHY THE X SERVER. REAPER on Linux draws through GDK on X11 (XWayland under a
// Wayland session), and SWELL's own GetAsyncKeyState answers VK_SHIFT /
// VK_CONTROL / VK_MENU from the pointer query's modifier mask. This file asks
// the same question over a connection of its own.
// ⚠ Under Wayland the X server sees the keys only while an X client, i.e. a
// REAPER window, has the keyboard focus.
//
// WHY dlopen AND NOT -lX11. Same as linux_http.cpp: no hard dependency in
// reaper_rea-sixty.so and no X11 headers at build time (the CI container has
// none). The three prototypes below are libX11's (include/X11/Xlib.h, read
// 18.11.2026); XID/Window is unsigned long and Bool is int on the client side.
//
// Main thread only: the connection is ours alone and never shared.

#if defined(__linux__)

#include "LinuxKeyMods.h"

#include <dlfcn.h>

#include <chrono>

namespace linuxkeys {
namespace {

struct XDisplay;                       // opaque
using XWindow = unsigned long;
using XOpenDisplayFn        = XDisplay* (*)(const char*);
using XDefaultRootWindowFn  = XWindow (*)(XDisplay*);
using XQueryPointerFn       = int (*)(XDisplay*, XWindow, XWindow*, XWindow*,
                                      int*, int*, int*, int*, unsigned int*);

struct X {
    bool                 tried = false;
    XDisplay*            dpy   = nullptr;
    XWindow              root  = 0;
    XQueryPointerFn      query = nullptr;
};

X& x()
{
    static X s;
    if (s.tried) return s;
    s.tried = true;   // one attempt: no X server at REAPER start stays none
    static const char* kNames[] = { "libX11.so.6", "libX11.so" };
    void* lib = nullptr;
    for (const char* n : kNames)
        if ((lib = ::dlopen(n, RTLD_LAZY | RTLD_LOCAL)) != nullptr) break;
    if (!lib) return s;
    auto open  = reinterpret_cast<XOpenDisplayFn>(::dlsym(lib, "XOpenDisplay"));
    auto root  = reinterpret_cast<XDefaultRootWindowFn>(::dlsym(lib, "XDefaultRootWindow"));
    auto query = reinterpret_cast<XQueryPointerFn>(::dlsym(lib, "XQueryPointer"));
    if (!open || !root || !query) return s;
    XDisplay* d = open(nullptr);   // $DISPLAY
    if (!d) return s;
    s.dpy   = d;
    s.root  = root(d);
    s.query = query;
    return s;
}

}  // namespace

Mods held()
{
    // Several readers ask within one tick (Shift, Ctrl, Alt, the FX layer, the
    // solo mode); one round trip to the X server per few milliseconds is enough.
    static std::chrono::steady_clock::time_point s_at{};
    static Mods s_last{};
    const auto now = std::chrono::steady_clock::now();
    if (now - s_at < std::chrono::milliseconds(4)) return s_last;
    s_at = now;
    X& xs = x();
    if (!xs.dpy || !xs.query) { s_last = Mods{}; return s_last; }
    XWindow rootRet = 0, childRet = 0;
    int rx = 0, ry = 0, wx = 0, wy = 0;
    unsigned int mask = 0;
    // The mask is filled even when the pointer is on another screen (the
    // return value only says which screen), so it is used either way.
    xs.query(xs.dpy, xs.root, &rootRet, &childRet, &rx, &ry, &wx, &wy, &mask);
    s_last = decode(mask);
    return s_last;
}

}  // namespace linuxkeys

#endif  // __linux__
