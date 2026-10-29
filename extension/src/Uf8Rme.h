#pragma once
//
// TotalMix on the UF8: the side-car's rules (docs/orc-uf8-plan.md, sections 2.1,
// 2.4, 2.6). Which channels sit on the eight strips, what each strip shows, and
// what a fader, a V-Pot or a key writes. No device and no REAPER: TotalMix'
// state in, uf8strips::View and OSC writes out. ORC runs it today; Rea-Sixty's
// UF8 side-car will run the same file (plan stage 7).
//
// ⛔ THE SELECTION IS THE UF1's. Row, chosen channel and submix live in
// reasixty::rme::input::State, one per program, and the UF1 side-car reads the
// very same object; this file only adds what the UF8 needs on top: which eight
// of the row are on the strips, and the channel view. So SEL on the UF8 is the
// channel the UF1 shows, and the window follows a channel the UF1 picks.
//
// Every rule that exists for the UF1 is called, not repeated: RmeUf1 (names,
// levels, mute, solo, pan, the fader law), RmeStrip (the channel view's pages and
// parameters), RmeInput (row, submix, window, select).
// Test: tests/test_uf8_rme.cpp.

#include "RmeInput.h"
#include "RmeManager.h"
#include "RmeState.h"
#include "RmeStrip.h"
#include "RmeUf1.h"
#include "PushQuiet.h"
#include "Uf8Strips.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace reasixty::rme::uf8side {

namespace rmeu = reasixty::rme::uf1;
namespace rmes = reasixty::rme::strip;
namespace rmei = reasixty::rme::input;

constexpr int kStrips = 8;
using Writes = rmes::Writes;

// The UF8's own part. One per program, owned by the caller; atomics because the
// input arrives on the device thread and the painter runs on another.
struct Side {
    // First visible channel on strip 1, an index into rmeu::visibleChannels of
    // that row. Each row keeps its own.
    std::atomic<int>  first[rmeu::kRowCount] = { {0}, {0}, {0}, {0} };
    // The selection the window last followed, per row: a new one moves it.
    std::atomic<int>  followed[rmeu::kRowCount] = { {-2}, {-2}, {-2}, {-2} };
    // The channel view (PLUG-IN): one STRIP page of the selected channel on the
    // eight V-Pots, the soft keys pick the page.
    std::atomic<bool> chanView{false};
    std::atomic<int>  chanPage{-1};      // rmes::View id, -1 = the first
    std::atomic<int>  chanPageGroup{0};  // which eight pages the soft keys show
    std::atomic<double> potAccum[kStrips] = {};
    PushQuiet<kStrips> pushQuiet;
    // ⇨ MAIN ON STRIP 8 WHILE IT MOVES (Config::uf8ShowMainOnChange): the
    // CHANNEL encoder's target and until when it stands there, on a steady
    // clock in ms. Strip 8's fader, V-Pot and CUT act on it meanwhile, and each
    // of them keeps it there.
    std::atomic<std::int64_t> mainUntilMs{0};
    std::atomic<int>          mainRow{0};
    std::atomic<int>          mainCh{-1};

    Side() = default;
    Side(const Side&) = delete;
    Side& operator=(const Side&) = delete;
};

// ── which channels ───────────────────────────────────────────────────────────

// The channel on each strip, -1 = empty. Reads the window, does not move it.
std::array<int, kStrips> channels(const State& st, const rmei::State& in, const Side& u);

// Move the window so the selected channel is on a strip, if the selection
// changed since the last call (a channel picked on the UF1 or by SEL). Called
// once per paint.
void follow(const State& st, const rmei::State& in, Side& u);

// Bank < > (8) and Page < > (1): the window, clamped at the ends.
void shiftWindow(const State& st, const rmei::State& in, Side& u, int by);

// ── what the strips show ─────────────────────────────────────────────────────

struct Frame {
    std::array<uf8strips::View, kStrips> strips;
    std::array<int, kStrips>    colour{};     // UF8 palette index for the bar, 0 = off
    std::array<std::uint8_t, 16> vu{};         // buildVuMeter levels, in/out per strip
    std::array<double, kStrips> faderLin{};   // 0..1 where the fader belongs
};

// `softKeys` are the eight soft-key cells of the overview (label, lamp 0/1/2,
// rgb), filled by the caller from its RME soft-key bank; in the channel view
// they are the pages instead.
struct SoftCell { std::string label; int lamp = 0; std::uint32_t rgb = 0xFFFFFF; };

Frame frame(const State& st, const rmei::State& in, const Side& u, const Config& cfg,
            const std::array<SoftCell, kStrips>& softKeys);

// ── input ────────────────────────────────────────────────────────────────────

// A fader moved under the hand to `lin` (0..1). Nothing for an empty strip.
Writes fader(const State& st, const rmei::State& in, const Side& u, int strip, double lin);

// V-Pot turned. Overview: Input row = preamp gain, other rows = pan into the
// submix (width on FX). Channel view: V-Pot 1-4 the page's parameters, 5-8 the
// page's switches (turning does nothing there; a push switches). `scale` is the
// fine factor (1.0 normal).
Writes vpot(const State& st, const rmei::State& in, Side& u, const Config& cfg,
            int strip, int delta, double scale, std::int64_t nowMs);

// V-Pot pushed: back to the default (gain 0 dB, pan centre, width 1, a page's
// parameter to its default), or a page switch toggled.
Writes vpotPush(const State& st, const rmei::State& in, Side& u, const Config& cfg,
                int strip, std::int64_t nowMs);

// SEL / CUT / SOLO of a strip.
void   sel(const State& st, rmei::State& in, const Side& u, int strip);
Writes cut(const State& st, const rmei::State& in, const Side& u, int strip);
Writes solo(const State& st, const rmei::State& in, const Side& u, int strip);

// PLUG-IN: the channel view on or off (only with a selected channel).
void toggleChannelView(const State& st, const rmei::State& in, Side& u);
// A soft key in the channel view: the page on that key. Page < > in the channel
// view: the next eight pages, when there are more than eight.
void pickPage(const State& st, const rmei::State& in, Side& u, const Config& cfg, int key);
void stepPageGroup(const State& st, const rmei::State& in, Side& u, const Config& cfg, int dir);

// ⇨ SEND/PLUG-IN 1-8 PICK THE SUBMIX (Frank 29.10.2026), in the control room's
// order: 1-4 Phones 1-4, 5 Main B, 6 Main (rmeu::controlRoomOutputs), 7-8 empty.
// A role without an output leaves its key dark rather than letting the others
// move up, as on the UF1's pots. The lamp: 2 the submix in play, 1 another
// output, 0 none.
int  submixKeyLamp(const State& st, const rmei::State& in, int key);
void pickSubmix(const State& st, rmei::State& in, const Config& cfg, int key);

// The channel encoder: Main volume, as the UF1's jog (cfg.jogTarget, jogStepDb).
// With cfg.uf8ShowMainOnChange the target stands on strip 8 for kMainShowMs.
constexpr int          kMainStrip  = 7;
constexpr std::int64_t kMainShowMs = 2000;
Writes mainVolume(const State& st, const rmei::State& in, Side& u, const Config& cfg,
                  int detents, double scale);
// True while strip 8 shows the CHANNEL encoder's target.
bool mainShowing(const Side& u);

} // namespace reasixty::rme::uf8side
