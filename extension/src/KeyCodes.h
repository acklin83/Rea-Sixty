#pragma once
//
// KeyCodes — the virtual-key values the macro parser names, without swell.h.
//
// The parser is pure: a chord string in, a KeyChord out. Only SENDING a chord
// needs a host to send it to. But the table of named keys used to reach its
// numbers through reaper_plugin.h, so parsing a chord dragged the whole REAPER
// SDK behind it and ORC could not read a bindings file that mentioned F5.
//
// These are the Windows virtual-key codes, fixed since Win32 and what SWELL maps
// onto on macOS and Linux as well. Written out rather than included so that a
// program with no REAPER can still parse a macro; the names are kept beside the
// values so the table stays readable against Microsoft's list.
//
// ⛔ Only define what is not already there: inside the extension swell.h has
// defined these long before this header is reached, and redefining them is a
// hard error rather than a warning.
//

#ifndef VK_TAB
#define VK_TAB     0x09
#define VK_RETURN  0x0D
#define VK_ESCAPE  0x1B
#define VK_SPACE   0x20
#define VK_BACK    0x08
#define VK_DELETE  0x2E
#define VK_INSERT  0x2D
#define VK_HOME    0x24
#define VK_END     0x23
#define VK_PRIOR   0x21
#define VK_NEXT    0x22
#define VK_LEFT    0x25
#define VK_RIGHT   0x27
#define VK_UP      0x26
#define VK_DOWN    0x28
#define VK_F1      0x70
#define VK_F2      0x71
#define VK_F3      0x72
#define VK_F4      0x73
#define VK_F5      0x74
#define VK_F6      0x75
#define VK_F7      0x76
#define VK_F8      0x77
#define VK_F9      0x78
#define VK_F10     0x79
#define VK_F11     0x7A
#define VK_F12     0x7B
#define VK_F13     0x7C
#define VK_F14     0x7D
#define VK_F15     0x7E
#define VK_F16     0x7F
#define VK_F17     0x80
#define VK_F18     0x81
#define VK_F19     0x82
#define VK_F20     0x83
// The modifier keys themselves, which isModifierVk tests against.
#define VK_SHIFT   0x10
#define VK_CONTROL 0x11
#define VK_MENU    0x12
#define VK_LWIN    0x5B
#define VK_RWIN    0x5C
#endif  // VK_TAB
