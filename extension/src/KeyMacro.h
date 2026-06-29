#pragma once
//
// Keyboard-macro synthesis for the Bindings "Keyboard macro" action type.
//
// A keyboard macro is a chain of key chords (each an ActionStep of
// ActionType::Keyboard whose `action` holds the chord string and `wait_ms`
// the delay before the next entry). Each chord is delivered IN-PROCESS to
// REAPER's main window via SWELL/Win32 window messages — never OS-global key
// injection, so it can't leak into OS window management. REAPER is already the
// host and focused app, so the keys land in REAPER. Frank 2026-06-29.
//
// The chord string is a neutral, platform-stable token list joined by '+'
// (spaces tolerated): e.g. "cmd+shift+t", "ctrl+alt+f4", "tab". Modifier
// tokens: shift, ctrl, alt (opt/option), cmd (win/super/meta — same flag).
//
#include <cstdint>
#include <string>
#include <vector>

namespace keymacro {

enum Mod : uint32_t {
    ModShift = 1u << 0,
    ModCtrl  = 1u << 1,   // physical Control
    ModAlt   = 1u << 2,   // Alt / Option
    ModCmd   = 1u << 3,   // Cmd (macOS) / Win (Windows) — "primary" modifier
};

struct KeyChord {
    uint32_t mods = 0;
    int      vk   = 0;    // SWELL/Win32 virtual-key code; 0 = no main key
    bool valid() const { return vk != 0; }
};

// Parse a neutral chord string into a KeyChord. Case-insensitive, '+' or
// whitespace separated. Returns false if the (single) main-key token is
// unknown or absent — a modifiers-only chord is not a valid macro entry.
bool parseChord(const std::string& s, KeyChord& out);

// Canonical neutral string for a chord (modifier order ctrl, alt, shift, cmd
// then the key name), used for storage + display. Empty if !valid().
std::string formatChord(const KeyChord& c);

// Map a single virtual-key to its neutral key name ("t", "f4", "tab", ...).
// Empty for unknown / modifier keys.
std::string keyName(int vk);

// True if `vk` is a modifier virtual-key (Shift/Ctrl/Alt/Cmd/Win).
bool isModifierVk(int vk);

// Deliver one chord to REAPER's main window. MAIN-THREAD ONLY (posts SWELL
// window messages + reads GetMainHwnd). No-op if the chord is invalid or the
// main window is unavailable.
void sendChordToReaper(const KeyChord& c);

// ---- Macro (sequence of chords with per-entry delays) ----------------------
// A keyboard macro is self-contained in a single binding action string: a list
// of entries, each a pre-delay (ms, applied before the chord fires) and a
// chord. Serialised as  "<delayMs>|<chord>;<delayMs>|<chord>;…"  (entries split
// by ';', delay and chord by '|'). A plain chord with no '|' parses as one
// entry with delay 0 — backward-compatible with single-chord values.
struct MacroEntry {
    int         delayMs = 0;   // wait this long BEFORE firing the chord
    std::string chord;         // RAW chord text (parsed at dispatch via parseChord)
};

// Parse a macro string into entries. The chord is kept as its raw text so the
// editor can round-trip half-typed input losslessly; parse it with parseChord
// when firing (skip entries whose chord doesn't parse).
std::vector<MacroEntry> parseMacro(const std::string& s);

// Serialise entries back to the canonical macro string.
std::string formatMacro(const std::vector<MacroEntry>& entries);

} // namespace keymacro
