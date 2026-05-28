#include "UC1Surface.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "reaper_plugin_functions.h"
#include "Bindings.h"  // dispatch / dispatchEncoder for Uc1Encoder2
#include "FocusedParam.h"  // uf8::setFocus — project UC1 knob turns onto the broadcast UF8 strip
#include "GrCalibration.h" // uf8::applyGrCalibration + kBcVuBpDb / kLedsBpDb
#include "MarkerOverlay.h"  // Phase 2.8b: Encoder 2 intercept for Nav Mode cursor
#include "NavDispatch.h"    // shared push-action dispatcher (UC1 + UF8)
#include "Palette.h"  // uf8::quantize for UC1 focused-track colour
#include "TrackName.h"  // abbreviateTrackName_ (Smart Abbreviate / Truncate)

// Defined in main.cpp — marks the UF8 Nav overlay decoration cache
// dirty so the next tick re-pushes after a cursor move or view
// change originating on the UC1 side (Phase 2.8b).
extern "C" void reasixty_markNavOverlayDirty();

// Diag (Frank 2026-05-19): mirror of main.cpp's diagSetParamLog_ so
// UC1 V-Pot writes get the same four-outcome log line as UF8 SSL
// Strip Mode fader writes.
extern void diagSetParamLog_(const char* site, MediaTrack* tr, int fx,
                             int param, double n, bool setRet,
                             double after);

// Shift-Fine mode check (defined in main.cpp). Returns true when the
// Settings toggle is on AND Shift is held (keyboard or UF8 hardware).
bool reasixty_shiftFineActive();

// Platform-normalised track-colour reader (defined in main.cpp).
// GetTrackColor returns 0xBBGGRR on Windows, 0xRRGGBB on macOS/Linux.
// This helper swaps R<->B on Windows so callers see a uniform
// 0xRRGGBB encoding.
extern uint32_t trackColorRgb(MediaTrack* tr);

// Phase 2.8c — user prefs read from this TU on the input thread. All
// return atomic-loaded values; C-linkage to keep the symbols stable.
extern "C" int  reasixty_navUc1Takeover();
extern "C" int  reasixty_navUc1Push();
extern "C" int  reasixty_navUc1PushShift();
extern "C" int  reasixty_navUc1LongPress();
// Per-surface Nav Mode (2026-05-28). 0 = Mirror UF8 (legacy), 1 = UC1
// Regions, 2 = UC1 Markers. Independent UC1 cursor is mutated via the
// delta helper below; clamp + render happen in pushUc1NavCarousel.
int reasixty_navUc1Mode();
extern "C" int reasixty_navUc1CursorDelta(int delta);
#include "ParameterGroups.h"  // multi-track param sync on UC1-originated writes
#include "PluginMap.h" // uf8::lookupPluginOnTrack + slotIdxForVst3Param
#include "UserPluginCatalog.h"  // user_plugins::lookupOwnedSlot + UserLinkSlot curve helpers

// Defined in main.cpp — scroll REAPER's MCP so the just-selected track
// is visible (and, on UF8, rebank the 8-strip window around it). Shared
// with UF8's SEL/CHANNEL-encoder paths so UC1 encoders feel identical.
void reasixty_followSelectedInMixer(MediaTrack* tr);
MediaTrack* reasixty_stepVisibleTrack(MediaTrack* cur, int step);
// Generic "scroll tracks" — same handler the `track_scroll` builtin
// invokes. Used here as the dispatchEncoder fallback for Uc1Encoder1
// so bindings.json files saved before this encoder became bindable
// still scroll tracks (factory behaviour).
void reasixty_applyTrackScroll(int step);
int reasixty_stripInstanceActiveFx(MediaTrack* tr);
std::string reasixty_fxCycleDisplayName(MediaTrack* tr, int fxIdx);
void reasixty_toggleMixerWindow();
bool reasixty_grAnyFx();   // GR-source toggle (Settings → Device)
// Settings → Modes → FX/Instance Cycle — controls-routing bitmask. Bit 2
// = UC1 Encoder 1 (CHANNEL), bit 3 = UC1 Encoder 2 (BC). When set AND
// SelectionMode is Instance / InstanceCycle, the encoder rotation drives
// applyInstanceCycle_ / applyFxCycle_ on the focused track instead of
// its normal behaviour (track-scroll for Enc1, bindings dispatch for
// Enc2). Mirror of the bit constants in main.cpp.
int  reasixty_cycleControlMask();
bool reasixty_dispatchSelModeCycle(int step);
constexpr int kCycleCtrlUc1Enc1 = 0x04;
constexpr int kCycleCtrlUc1Enc2 = 0x08;
// Per-tick device calibration (Settings → Device → Calibrate BC/CS).
// section: 0=BC VU (6 ticks 0/4/8/12/16/20 dB), 1=CS LEDs (5 ticks
// 3/6/10/14/20 dB). Active test: -1 normal, 0..5 = force BC tick,
// 100..104 = force CS tick.
int    reasixty_uc1CalCount(int section);
double reasixty_uc1CalTickDb(int section, int idx);
double reasixty_uc1CalGet(int section, int idx);
double reasixty_uc1CalEffective(int section, int idx);
int    reasixty_uc1CalActiveTest();
// View-active-plugin window follower — see definition in main.cpp.
void reasixty_followFocusedGuiToFx(MediaTrack* tr, int fxIdx);
// Touched-FX reveal — see definition in main.cpp. `domainInt` matches
// the underlying uf8::Domain enum's int representation.
void reasixty_pushTouchedFxReveal(void* tr, int fxIdx, int domainInt);
bool reasixty_touchedFxRevealActive();
void* reasixty_touchedFxRevealTrack();
const char* reasixty_touchedFxRevealLabel();
// Per-FX user-rename — empty string if the user hasn't renamed.
std::string reasixty_fxUserRename(void* tr, int fxIdx);
// Folder Mode reveal: when a UC1 knob writes a param on `tr`, the UF8
// strip displaying that track should briefly show the real value
// instead of the "Folder" placeholder. No-op outside folder mode.
void reasixty_bumpFolderReveal(MediaTrack* tr);

// REC + RME (TotalReaper) bridges for UC1 — defined in main.cpp.
// Surface calls these from handleKnob_ / handleButton_ on the main
// thread (UC1Surface::poll drains its queue there, so REAPER track
// APIs are safe). All return true when the action consumed the input
// (caller suppresses fallback). `which`: 0 = Enc2 Push, 1 = Cut,
// 2 = Solo. signedStep for the encoder bridges is already in physical
// detents — pass stepFromAccumulator's output directly.
bool reasixty_dispatchUc1RecRmeButton(int which, MediaTrack* tr);
bool reasixty_dispatchUc1RecRmeGain(MediaTrack* tr, int signedStep);
bool reasixty_dispatchUc1RecRmeInputChan(MediaTrack* tr, int signedStep);
// LED-mirror lookup for Cut/Solo. Returns -1 when REC+RME isn't
// governing the button (caller falls back to B_MUTE / I_SOLO);
// 0/1 = mirrored P_EXT toggle state. `which`: 1 = Cut, 2 = Solo.
int  reasixty_recUc1ButtonMirroredState(int which, MediaTrack* tr);
// Predicate: would dispatchUc1RecRmeButton fire? Used to swallow the
// release edge on Uc1Encoder2Push so bindings::dispatch never sees
// an unpaired release. `which`: 0 = Enc2 Push, 1 = Cut, 2 = Solo,
// 3 = Polarity.
bool reasixty_recRmeUc1ButtonAssigned(int which);
// Build the REC+RME readout (UF8 V-Pot value-line format) for the
// focused track. Returns false when not applicable; UC1 then renders
// the regular focused-param readout in its place.
bool reasixty_recUc1ReadoutText(MediaTrack* tr,
                                std::string* outLabel,
                                std::string* outValue);
// True when SelectionMode is Rec or RecMon — used to switch the UC1
// Fine button to momentary (hold-to-Shift) instead of toggle.
bool reasixty_inRecOrRecMonMode();

namespace uc1 {

namespace {

// Read whether the plug-in is effectively bypassed, honouring the
// per-binding bypassInverted flag. SSL-stock plug-ins expose a "Bypass"
// param (1 = bypassed); user plug-ins like bx_townhouse expose "Comp In"
// (1 = active). The IN button + LED render paths used to assume the
// stock semantic everywhere, which left Townhouse's IN LED inverted and
// the BC-bypass meter cascade silencing on the wrong edge. Helper here
// keeps the read consistent across the 7+ sites that need it.
inline bool readPluginBypass_(MediaTrack* tr, const PluginBindings* m, int fxIdx)
{
    if (!tr || !m || m->bypassParam == kParamNone) return false;
    const double v = TrackFX_GetParamNormalized(tr, fxIdx, m->bypassParam);
    return m->bypassInverted ? (v < 0.5) : (v > 0.5);
}

// EXTENDED FUNCTIONS list — params NOT on the main soft-key bank that
// SSL360 exposes via the BACK button drill-down (manual p.19). Entry
// labels are tuned to fit the 14-char slot width SSL360 uses (decoded
// from uc1_37 scroll capture).
//
// slotId resolves against PluginMap::slots so the same list works
// across CS variants (CS 2 / 4K E / 4K G / 4K B). Entries whose slotId
// isn't present on the current plug-in are silently skipped (TBD —
// initial scroll is full list regardless of plug-in availability).
struct ExtFuncsEntry {
    const char* slotId;
    const char* shortLabel;   // <= 14 chars for the 3-slot triple
    const char* longLabel;    // header text
};
constexpr ExtFuncsEntry kExtFuncs[] = {
    { "AutoMakeup",   "AUTO MKP",  "Auto Makeup"     },
    { "WidthFreq",    "WIDTH FQ",  "Width Frequency" },
    { "WidthMode",    "WIDTH MD",  "Width Mode"      },
    { "FiltersIn",    "FILT IN",   "Filters In"      },
    { "OutputTrim",   "OUT TRIM",  "Output Trim"     },
    { "Width",        "WIDTH",     "Width"           },
    { "Pan",          "PAN",       "Pan"             },
    { "Pre",          "PRE",       "Pre"             },
    { "MicDrive",     "MIC DRV",   "Mic / Drive"     },
    { "CompMix",      "COMP MIX",  "Comp Mix"        },
};
constexpr int kExtFuncsCount = sizeof(kExtFuncs) / sizeof(kExtFuncs[0]);

// Label for the zone-0x03/0x05 readout based on which knob was touched.
// Kept short here — the surface clips/pads to 22 chars before sending.
// Matches the labels SSL 360° pushes in our captures (uc1_04 etc.).
const char* labelForKnob(uint8_t knobId, bool busCompContext)
{
    if (busCompContext) {
        switch (knobId) {
            case knob::kBCThreshold: return "Threshold";
            case knob::kBCMakeup:    return "Makeup";
            case knob::kBCAttack:    return "Attack";
            case knob::kBCRelease:   return "Release";
            case knob::kBCRatio:     return "Ratio";
            case knob::kBCMix:       return "Mix";
            case knob::kBCScHpf:     return "S/C HPF";
        }
    }
    switch (knobId) {
        // CS dedicated pots
        case knob::kCSLowPass:        return "Low Pass";
        case knob::kCSHighPass:       return "High Pass";
        case knob::kCSHfGain:         return "HF Gain";
        case knob::kCSHfFreq:         return "HF Frequency";
        case knob::kCSHmfGain:        return "HMF Gain";
        case knob::kCSHmfFreq:        return "HMF Frequency";
        case knob::kCSHmfQ:           return "HMF Q";
        case knob::kCSLmfGain:        return "LMF Gain";
        case knob::kCSLmfFreq:        return "LMF Frequency";
        case knob::kCSLmfQ:           return "LMF Q";
        case knob::kCSLfFreq:         return "LF Frequency";
        case knob::kCSLfGain:         return "LF Gain";
        // CS right-side
        case knob::kCSGateRelease:    return "Release";    // (Gate)
        case knob::kCSGateHold:       return "Hold";
        case knob::kCSGateThreshold:  return "Threshold";  // (Gate)
        case knob::kCSGateRange:      return "Range";
        case knob::kCSCompRelease:    return "Release";    // (Dyn)
        case knob::kCSCompThreshold:  return "Threshold";  // (Dyn)
        case knob::kCSCompRatio:      return "Ratio";      // (Dyn)
        // Repurposed V-Pots
        case knob::kCSInputTrim:      return "In Trim";
        case knob::kCSFaderLevel:     return "Fader Level";
    }
    return "";
}

// Build a zone-0x03/0x05 readout matching SSL 360°'s captured format:
//   [label][padding to 16 chars][value]
//
// Total length = 16 + value.size() (22 for 6-char values like "12.1dB",
// 23 for 7-char values like "-10.0dB" or "102.5Hz"). UC1's value-zone
// LCD slots start at position 16; right-justifying into a fixed 22-char
// field shifted 7-char values one left of that anchor, causing the
// split "1    02.5Hz" / "-        10.0dB" rendering on hardware.
std::string formatReadout(std::string_view label, std::string_view value)
{
    constexpr size_t kLabelPad = 16;
    std::string out;
    out.reserve(kLabelPad + value.size());

    // std::min is macro-shadowed by WDL/swell — wrap to suppress expansion.
    const size_t lmax = (std::min)(label.size(), kLabelPad);
    out.append(label.data(), lmax);
    if (lmax < kLabelPad) {
        out.append(kLabelPad - lmax, ' ');
    }
    out.append(value.data(), value.size());
    return out;
}

// Which LED cell belongs to this button? Returns {0,0} if there's no
// dedicated LED (Fine, Polarity without display, etc.).
led::Cell cellForButton(uint8_t buttonId)
{
    switch (buttonId) {
        case button::kHfBell:      return led::kHfBell;
        case button::kEqType:      return led::kEqType;
        case button::kEqIn:        return led::kEqIn;
        case button::kLfBell:      return led::kLfBell;
        case button::kBusCompIn:   return led::kBusCompIn;
        case button::kFastAttComp: return led::kFastAttComp;
        case button::kPeak:        return led::kPeak;
        case button::kDynIn:       return led::kDynIn;
        case button::kExpand:      return led::kExpand;
        case button::kFastAttGate: return led::kFastAttGate;
        case button::kPolarity:    return led::kPolarity;
        case button::kScListen:    return led::kScListen;
        case button::kSolo:        return led::kSolo;
        case button::kSoloClear:   return led::kSoloClear;
        case button::kCut:         return led::kCut;
        case button::kChannelIn:   return led::kChannelIn;
        case button::kFine:        return led::kFine;
    }
    return {0, 0};
}

} // namespace

UC1Surface::UC1Surface()
{
    lastButtonLed_.fill(-1);  // -1 = unknown, force first push per button
}

void UC1Surface::attach(UC1Device& device)
{
    device_ = &device;
    device_->setKnobHandler([this](const KnobEvent& ev) {
        std::lock_guard<std::mutex> lk(queueMu_);
        knobQueue_.push_back(ev);
    });
    device_->setButtonHandler([this](const ButtonEvent& ev) {
        std::lock_guard<std::mutex> lk(queueMu_);
        buttonQueue_.push_back(ev);
    });
}

void UC1Surface::setFocusedTrack(void* track)
{
    if (focusedTrack_ == track) return;
    focusedTrack_ = track;
    // Skip GR pushes for a short window so the BC mechanical needle
    // doesn't twitch toward the outgoing track's last-read value
    // before the new track's GR settles in REAPER. ~250 ms is below
    // human-perceptible meter latency for typical compressor envelopes
    // but above the cross-tick jitter window.
    grSettleUntil_ = std::chrono::steady_clock::now()
                   + std::chrono::milliseconds(250);
    // Invalidate the ring-cell cache so refresh()'s eager ring loops
    // re-write every cell on the new focus, not just the ones whose
    // target state differs from our last-known state. Without this, a
    // cell that the firmware lit (init flood, previous session, etc.)
    // but our cache thinks is OFF stays stuck until the user rotates
    // the dot through it — manifests as a "hanging" LED in EQ rings.
    ringCellCache_.clear();
    lastButtonLed_.fill(-1);  // re-push every button LED on the new focus
    // Drop the BC-bypass cache too. The next pollBcBypassState_ tick on
    // the new track will refresh the backlight to match its bypass state
    // but won't fire the FF 5C cosmetic — focus change is not a press.
    lastBcBypassed_ = -1;
    // Drop back to MAIN on focus change — any in-progress menu (EXT_FUNCS
    // / ROUTING / PRESETS) referenced the previous track's plug-in
    // context, no point keeping it.
    mode_ = Uc1Mode::Main;
    refresh();
}

void UC1Surface::setBcAnchorTrack(void* track)
{
    if (track && !ValidatePtr2(nullptr, track, "MediaTrack*")) return;
    if (track == bcAnchorTrack_) return;
    // Match the BC-encoder code path: scroll-overlay flag + GR settle
    // so the BC carousel triple repaints (header + prev/curr/next track
    // names) and the GR needle doesn't briefly show the outgoing
    // anchor's reduction value. Without the overlay, V-Pot-driven anchor
    // changes update the BC display state but the user-visible carousel
    // still shows the previous anchor.
    bcScrollOverlayActive_ = true;
    // 3 s window matches Frank's expectation — long enough to read the
    // prev/curr/next BC track names before pushFocusedParamReadout_ is
    // allowed to repaint zone 0x05 again. 1.5 s (the SSL360 reference
    // value) was too short in practice; a fresh poll-tick value-poll
    // could re-render the BC param the moment the overlay expired and
    // visually destroy the carousel.
    bcScrollOverlayUntil_ =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
    bcAnchorTrack_ = track;
    grSettleUntil_ = std::chrono::steady_clock::now()
                   + std::chrono::milliseconds(250);
    // Drop the BC mechanical needle to 0 immediately so the outgoing
    // anchor's last GR value doesn't briefly read on the new anchor's
    // meter. UC1Device's 50 Hz FF 5B stream resumes from the new
    // anchor's pushGainReduction value once the settle window ends.
    if (device_) {
        device_->send(buildZeroGr());
        // Release the CS readout slot — the BC carousel claims the
        // central upper LCD area in sub=0x02 layout, but a sticky
        // zone 0x03 ("HF Gain  +3.0dB" etc. from the last CS knob
        // edit) keeps painting on top of the carousel until the
        // firmware is told to drop the slot. Symmetric with the
        // Encoder 1 path which invalidates zone 0x03 on every CS
        // scroll. Carousel redraw signal (0x0F) matches the same
        // path so the firmware repaints the new BC triple cleanly.
        if (!lastZone03Text_.empty()) {
            lastZone03Text_.clear();
            device_->send(buildDisplayInvalidate(zone::kChannelStripReadout));
        }
        device_->send(buildDisplayInvalidate(0x0F));
    }
    invalidateCache();
    // Carousel + BC label + header repaint must run NOW — without
    // refresh() the new bcAnchor only takes visual effect after the
    // next external setFocusedTrack or refresh trigger. Encoder 2's
    // own handler runs refresh() after setBcAnchorTrack; chase-driven
    // anchor changes (UF8 V-Pot on a BC param) need the same so the
    // BC carousel + track name update immediately, mirroring how a CS
    // knob edit drives the CS carousel via setFocusedTrack → refresh.
    // Frank 2026-05-12: "wenn BC parameter auf UF8 gedreht werden:
    // wie bei CS parameter sofort carousel mit kanal updaten".
    refresh();
}

void UC1Surface::applyBcTrackScrollImpl_(int step, bool selectAlso)
{
    // Shared BC-track step + re-anchor. selectAlso=true additionally
    // pulls REAPER selection + UF8 bank to the new anchor (Frank
    // 2026-05-27 "scroll and select" variant). Default false preserves
    // the classic Encoder-2 contract: carousel moves, selection stays.
    if (step == 0) return;
    const int n = CountTracks(nullptr);
    if (n <= 0) return;

    int curIdx = -1;
    if (void* anchor = effectiveBcTrack_()) {
        curIdx = static_cast<int>(GetMediaTrackInfo_Value(
            static_cast<MediaTrack*>(anchor), "IP_TRACKNUMBER")) - 1;
    } else if (focusedTrack_) {
        curIdx = static_cast<int>(GetMediaTrackInfo_Value(
            static_cast<MediaTrack*>(focusedTrack_), "IP_TRACKNUMBER")) - 1;
    }

    const bool forward = step > 0;
    const int stepsAbs = forward ? step : -step;
    int probe = curIdx;
    int found = -1;
    for (int k = 0; k < stepsAbs; ++k) {
        int next = -1;
        if (forward) {
            for (int i = probe + 1; i < n; ++i) {
                auto b = lookupBindingsOnTrack(GetTrack(nullptr, i));
                if (b.busCompMap) { next = i; break; }
            }
        } else {
            for (int i = probe - 1; i >= 0; --i) {
                auto b = lookupBindingsOnTrack(GetTrack(nullptr, i));
                if (b.busCompMap) { next = i; break; }
            }
        }
        if (next < 0) break;  // no more BC tracks in this direction
        found = next;
        probe = next;
    }
    if (found < 0) return;
    MediaTrack* tr = GetTrack(nullptr, found);
    if (!tr) return;
    setBcAnchorTrack(tr);
    if (selectAlso) {
        SetOnlyTrackSelected(tr);
        reasixty_followSelectedInMixer(tr);
    }
    // Encoder 2 BC-scroll is the user expressing "show me BC" — flip
    // focus so resolveActiveFx_ / Toggle Focused UI / etc. target the
    // BC instance instead of the channel's CS / last-cursor FX. Without
    // this, the LCD shows BC's name (via bcBindings_.busCompMap) but
    // fp.domain stays at ChannelStrip and the push action opens the CS
    // (or nothing) on the focused track. Frank 2026-05-22.
    uf8::setFocus({uf8::Domain::BusComp, 0});
    refresh();
}

void UC1Surface::applyBcTrackScroll(int step)
{
    applyBcTrackScrollImpl_(step, /*selectAlso=*/false);
}

void UC1Surface::applyBcTrackScrollAndSelect(int step)
{
    applyBcTrackScrollImpl_(step, /*selectAlso=*/true);
}

void UC1Surface::showInstanceCarousel(const std::string& prev,
                                     const std::string& curr,
                                     const std::string& next,
                                     const std::string& header)
{
    if (!device_) return;
    // Mutually exclusive with the BC-scroll overlay — clear it before
    // we claim the same layout slot so the header doesn't flicker
    // between "BUS COMP 2" and the instance header for a frame.
    bcScrollOverlayActive_ = false;
    // CS-scroll overlay (Encoder 1) suppresses zone 0x03; we want
    // both zones suppressed while the instance carousel holds the
    // upper LCD, so leave csScrollOverlayActive_ alone (it'll either
    // expire naturally or be unset on its own timeout).
    instanceCarouselActive_ = true;
    instanceCarouselUntil_  = std::chrono::steady_clock::now()
                            + std::chrono::milliseconds(3000);
    instanceCarouselHeader_ = header;
    instanceCarouselTripleFrame_ =
        buildTrackNameTripleLarge(prev, curr, next);
    // Match BC-scroll overlay's frame order: invalidate old text
    // zones, claim sub=0x02 layout, header, triple, redraw signal.
    if (!lastZone03Text_.empty()) {
        lastZone03Text_.clear();
        device_->send(buildDisplayInvalidate(zone::kChannelStripReadout));
    }
    if (!lastZone05Text_.empty()) {
        lastZone05Text_.clear();
        device_->send(buildDisplayInvalidate(zone::kBusCompReadout));
    }
    device_->send(buildCentralMode(CentralMode::Main, 0x02));
    device_->send(buildLcdHeader(header));
    device_->send(std::vector<uint8_t>(instanceCarouselTripleFrame_));
    lastLargeTripleFrame_ = instanceCarouselTripleFrame_;
    device_->send(buildDisplayInvalidate(0x0F));
}

void UC1Surface::showNavCarousel(const std::string& prev,
                                 const std::string& curr,
                                 const std::string& next,
                                 const std::string& header,
                                 uint8_t paletteIdx)
{
    if (!device_) return;

    // First entry: claim the LCD layout slot and clear any competing
    // overlays. Subsequent calls only send the deltas (header / triple
    // / palette) when the cached args change.
    const bool firstEntry = !navCarouselActive_;
    if (firstEntry) {
        bcScrollOverlayActive_   = false;
        instanceCarouselActive_  = false;
        // CS-scroll only suppresses zone 0x03 — leave it to expire on
        // its own timer; the Nav carousel doesn't care about zone 0x03.
        if (!lastZone03Text_.empty()) {
            lastZone03Text_.clear();
            device_->send(buildDisplayInvalidate(zone::kChannelStripReadout));
        }
        if (!lastZone05Text_.empty()) {
            lastZone05Text_.clear();
            device_->send(buildDisplayInvalidate(zone::kBusCompReadout));
        }
        device_->send(buildCentralMode(CentralMode::Main, 0x02));
        navCarouselActive_  = true;
        // Force the cached strings to mismatch so we send everything
        // on this first tick regardless of prior content.
        navCarouselHeader_.clear();
        navCarouselPrev_.clear();
        navCarouselCurr_.clear();
        navCarouselNext_.clear();
        navCarouselPalette_ = 0xFF;
    }

    if (header != navCarouselHeader_) {
        navCarouselHeader_ = header;
        device_->send(buildLcdHeader(header));
    }

    if (prev != navCarouselPrev_
        || curr != navCarouselCurr_
        || next != navCarouselNext_)
    {
        navCarouselPrev_ = prev;
        navCarouselCurr_ = curr;
        navCarouselNext_ = next;
        auto frame = buildTrackNameTripleLarge(prev, curr, next);
        device_->send(std::vector<uint8_t>(frame));
        lastLargeTripleFrame_ = std::move(frame);
        device_->send(buildDisplayInvalidate(0x0F));
    }

    if (paletteIdx != navCarouselPalette_) {
        navCarouselPalette_ = paletteIdx;
        device_->send(buildFocusedColour(paletteIdx));
    }
}

void UC1Surface::setMode(Uc1Mode m)
{
    if (mode_ == m) return;
    mode_ = m;
    // Reset PRESETS sub-mode whenever we enter (or leave) Presets so
    // the user always lands on the CS/BC selector first.
    if (m == Uc1Mode::Presets) {
        presetsSub_ = PresetsSubMode::Selector;
    }
    if (!device_) return;
    // LCD top-banner — all six bytes confirmed against uc1_37+38.
    const CentralMode banner =
        m == Uc1Mode::Main      ? CentralMode::Main
      : m == Uc1Mode::ExtFuncs  ? CentralMode::ExtFuncs
      : m == Uc1Mode::Routing   ? CentralMode::Routing
      : m == Uc1Mode::Presets   ? CentralMode::Presets
                                : CentralMode::Transport;
    device_->send(buildCentralMode(banner));
    // BC mode dot: lit only in MAIN. Single bank=0x02 frame.
    device_->send(buildBcModeDot(m == Uc1Mode::Main));
    // Routing + Presets dots: dual-bank (brightness + selection).
    for (auto& f : buildMenuDot(kCellRoutingDot, m == Uc1Mode::Routing)) {
        device_->send(f);
    }
    for (auto& f : buildMenuDot(kCellPresetsDot, m == Uc1Mode::Presets)) {
        device_->send(f);
    }
    // Render menu-mode subscreen content immediately on entry so the
    // user sees something rather than a blank LCD waiting for their
    // first encoder click.
    if (m == Uc1Mode::Presets) renderPresetsSubscreen_();
    if (m == Uc1Mode::ExtFuncs) {
        extFuncsIdx_ = 0;
        extFuncsActive_ = false;  // always start in list mode
        renderExtFuncsSubscreen_();
    }
    // Returning to MAIN re-runs refresh() so the carousel + central
    // label repaint over the menu-mode LCD layout. refresh() is gated
    // to MAIN-only (added when EXT_FUNCS leaked BC carousel), so we
    // must call it explicitly here on the upward transition.
    if (m == Uc1Mode::Main) refresh();
}

void UC1Surface::hideNavCarousel()
{
    if (!navCarouselActive_) return;
    navCarouselActive_ = false;
    navCarouselPrev_.clear();
    navCarouselCurr_.clear();
    navCarouselNext_.clear();
    navCarouselHeader_.clear();
    navCarouselPalette_ = 0xFF;
    if (!device_ || mode_ != Uc1Mode::Main) return;
    // Match the instance-carousel revert: reset sub=0x00 then refresh()
    // so the central-label branch picks the regular MAIN/CS 2/BC 2
    // path and the LARGE triple repopulates from BC carousel data.
    device_->send(buildCentralMode(CentralMode::Main, 0x00));
    refresh();
}

void UC1Surface::invalidateCache()
{
    ringCellCache_.clear();
    lastZone05Text_.clear();  // force the next pushFocusedParamReadout_ to send
    lastZone03Text_.clear();
    lastButtonLed_.fill(-1);  // re-push every button LED on the next poll
    lastBcBypassed_ = -1;     // re-push BC backlight (without phantom cosmetic)
}

int UC1Surface::poll()
{
    std::deque<KnobEvent>   knobs;
    std::deque<ButtonEvent> buttons;
    {
        std::lock_guard<std::mutex> lk(queueMu_);
        knobs.swap(knobQueue_);
        buttons.swap(buttonQueue_);
    }

    int handled = 0;
    for (const auto& e : knobs)   { handleKnob_(e);   ++handled; }
    for (const auto& e : buttons) { handleButton_(e); ++handled; }

    // BC-scroll overlay revert. uc1_41 capture: SSL360 reverts the
    // central LCD from "BC scroll" sub-mode (banner 0x01 sub=0x02 +
    // header "BUS COMP 2") to plain MAIN (sub=0x00 + buildCentralLabel
    // "MAIN"/"CS 2"/etc.) ~1.5s after the last detent. Triple keeps its
    // BC content. Clear the flag and run refresh() so its central-label
    // branch picks the regular path now that the overlay is off.
    if (bcScrollOverlayActive_
        && std::chrono::steady_clock::now() >= bcScrollOverlayUntil_
        && device_
        && mode_ == Uc1Mode::Main)
    {
        bcScrollOverlayActive_ = false;
        device_->send(buildCentralMode(CentralMode::Main, 0x00));
        refresh();
    }

    // CS-scroll overlay revert — symmetric to bcScrollOverlay. Once the
    // 3 s window elapses with no further Encoder 1 activity, drop the
    // suppression so the next CS knob edit can repaint zone 0x03.
    if (csScrollOverlayActive_
        && std::chrono::steady_clock::now() >= csScrollOverlayUntil_
        && device_
        && mode_ == Uc1Mode::Main)
    {
        csScrollOverlayActive_ = false;
    }

    // Instance / FX cycle carousel revert. Same shape as the BC-scroll
    // revert: reset sub=0x00 then refresh() so the central-label branch
    // picks the regular MAIN/CS 2/BC 2 label and the LARGE triple
    // repopulates from the BC carousel data.
    if (instanceCarouselActive_
        && std::chrono::steady_clock::now() >= instanceCarouselUntil_
        && device_
        && mode_ == Uc1Mode::Main)
    {
        instanceCarouselActive_ = false;
        device_->send(buildCentralMode(CentralMode::Main, 0x00));
        refresh();
    }

    // Per-tick value poll. Catches every cause of focused-param change:
    //   - UF8 Page <-/-> shifted slotIdx (text changes)
    //   - UF8 V-Pot rotation on the focused track (value changes)
    //   - Plugin-GUI mouse edit (value changes)
    //   - REAPER automation moving the param under us (value changes)
    // Internal dedup against lastZone05Text_ skips the USB write when
    // nothing changed, so the cost when idle is just two REAPER API
    // calls + a string compare.
    pushFocusedParamReadout_();

    // Mirror plugin/track state to button LEDs and knob rings every
    // tick. Both have internal dedup, so the cost when idle is just
    // a few REAPER API reads + cache compares — no USB traffic. This
    // is what makes plugin-GUI edits, automation, and preset loads
    // reflect on the surface without waiting for a UC1 input event.
    pollButtonLeds_();
    pollKnobRings_();
    pollBcBypassState_();
    pollGainReduction_();

    return handled;
}

double UC1Surface::clickToDelta_(int8_t delta) const
{
    // Each encoder click = ~1/64 of a full param sweep. Fine mode = 1/4
    // of that (= 1/256 per click, effectively 256 clicks to traverse
    // the full normalized range). 1/64 at default roughly matches SSL
    // 360°'s perceived feel on the Bus Comp Threshold (40 dB range
    // covered in ~200 encoder clicks = 0.2 dB/click vs. our 0.625 dB
    // per click — slightly snappier but responsive).
    constexpr double kStepPerClick = 1.0 / 64.0;
    double d = delta * kStepPerClick;
    if (fineMode_.load(std::memory_order_relaxed)
        || reasixty_shiftFineActive()) d *= 0.25;
    return d;
}

void UC1Surface::handleKnob_(const KnobEvent& ev)
{
    // Diag-first: log every knob event before any suppression, so
    // unmapped IDs (Attack TBD, any knob we haven't attributed) show
    // up in the console. Per-ID budget of 3 keeps volume sane.
    static int kPerIdRemaining[0x20] = { 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                                         3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 };
    const bool logThis = (ev.id < 0x20 && kPerIdRemaining[ev.id] > 0);
    if (logThis) --kPerIdRemaining[ev.id];

    // Helper for fine-tick encoders (CHANNEL, BC): accumulate deltas
    // across events so N ticks = 1 physical click. Direction change
    // OR a >100 ms gap since the last event clears residual ticks,
    // so occasional short-tick clicks don't silently accumulate drift.
    auto stepFromAccumulator = [&ev](int& acc, auto& lastT, int ticksPerStep) -> int {
        auto now = std::chrono::steady_clock::now();
        if (lastT.time_since_epoch().count() != 0
            && now - lastT > std::chrono::milliseconds(100)) {
            acc = 0;
        }
        lastT = now;
        if ((ev.delta > 0 && acc < 0) || (ev.delta < 0 && acc > 0)) acc = 0;
        acc += ev.delta;
        int step = acc / ticksPerStep;
        acc -= step * ticksPerStep;
        return step;
    };

    // CHANNEL encoder — scroll through ALL REAPER tracks. ~4 ticks/click.
    if (ev.id == knob::kChannelEncoder) {
        static int acc = 0;
        static std::chrono::steady_clock::time_point lastT{};
        // ticksPerStep=3 (was 4) — encoder fires 3 OR 4 ticks per detent
        // inconsistently; with 4 the 3-tick clicks got eaten by the 100 ms
        // GAP-RESET, costing Frank "3-4 clicks per track step" (diagnosed
        // 2026-05-25 via /tmp/rea_sixty_encoder.log). 3 catches both: 3-tick
        // clicks fire one step cleanly; 4-tick clicks fire one step + leave
        // 1 residual which the next gap-reset wipes.
        int step = stepFromAccumulator(acc, lastT, 3);
        if (step == 0) { ++stats_.knobEventsHandled; return; }
        // SEL Mode override — when bit 2 (UC1 Encoder 1) is ticked in
        // Settings → Modes → FX/Instance Cycle AND SelectionMode is
        // Instance / InstanceCycle, hijack the encoder away from track
        // scroll and drive the cycle on the focused track. Skip every
        // track-scroll side effect (overlay flags, redraw signals,
        // setFocusedTrack) — applyFxCycle_ / applyInstanceCycle_ handle
        // their own invalidation.
        if ((reasixty_cycleControlMask() & kCycleCtrlUc1Enc1) != 0
            && reasixty_dispatchSelModeCycle(step))
        {
            ++stats_.knobEventsHandled;
            return;
        }
        // Per uc1_47 capture every Encoder 1 detent ends with FF 66 01 0F
        // (zone 0x0F invalidate). Without it the CS-carousel small-
        // triple scroll doesn't visually animate on the UC1 LCD —
        // refresh() builds the new triple but the firmware appears to
        // need this redraw signal to actually advance the carousel.
        // Step through the visible-track list (not the raw project
        // list) so hidden / collapsed-folder tracks are skipped —
        // mirrors the UF8 surface filter. Frank 2026-05-22.
        // Validate focusedTrack_ here in addition to poll() — an Enc1
        // turn between a track delete and the next poll tick would
        // otherwise pass a stale pointer to stepVisibleTrack, which
        // jumps to first/last instead of stepping. Same "above the
        // deleted one" fallback as the poll-side branch.
        if (focusedTrack_
            && !ValidatePtr2(nullptr, focusedTrack_, "MediaTrack*")) {
            focusedTrack_ = nullptr;
            const int total = CountTracks(nullptr);
            MediaTrack* fallback = nullptr;
            if (total > 0) {
                int idx = lastFocusedTrackIdx_ - 1;
                if (idx < 0) idx = 0;
                if (idx >= total) idx = total - 1;
                fallback = GetTrack(nullptr, idx);
            }
            if (fallback) setFocusedTrack(fallback);
            lastFocusedTrackIdx_ = -1;
        }
        // Bindings-routed dispatch (Uc1Encoder1). Falls back to the
        // factory `track_scroll` handler when no binding exists so
        // older bindings.json files saved before this encoder became
        // bindable still scroll tracks. Default Plain binding wires
        // Uc1Encoder1 → track_scroll, making the bindings path the
        // normal path. The CS-readout overlay + carousel redraw below
        // fire unconditionally — they're hardware-UI side effects of
        // ANY Encoder 1 rotation, independent of the binding's action.
        if (!uf8::bindings::dispatchEncoder(
                uf8::bindings::ButtonId::Uc1Encoder1, step)) {
            reasixty_applyTrackScroll(step);
        }
        if (device_) {
            // Hide the CS readout (zone 0x03) so the channel-name
            // carousel takes its space. User FR 2026-05-05: "Wenn an
            // Encoder 1 gedreht wird, soll die obere parameter-zeile
            // ausgeblendet werden". Set the overlay flag so subsequent
            // pushFocusedParamReadout_ ticks skip zone 0x03 until the
            // user actually touches a CS knob (or 3s elapse). Mirror
            // of bcScrollOverlay for the CS side.
            csScrollOverlayActive_ = true;
            csScrollOverlayUntil_  = std::chrono::steady_clock::now()
                                   + std::chrono::milliseconds(3000);
            if (!lastZone03Text_.empty()) {
                lastZone03Text_.clear();
                device_->send(buildDisplayInvalidate(zone::kChannelStripReadout));
            }
            // Carousel scroll redraw signal — uc1_47 every Encoder 1
            // detent ends with this 5-byte invalidate.
            device_->send(buildDisplayInvalidate(0x0F));
        }
        if (logThis) {
            MediaTrack* landedTr = static_cast<MediaTrack*>(focusedTrack_);
            const int landed = landedTr
                ? static_cast<int>(GetMediaTrackInfo_Value(
                      landedTr, "IP_TRACKNUMBER"))
                : 0;
            char line[96];
            snprintf(line, sizeof(line),
                "UC1 CHANNEL delta=%d step=%d → track %d\n",
                (int)ev.delta, step, landed);
        }
        ++stats_.knobEventsHandled;
        return;
    }

    // BC encoder — context-dependent on UC1 Mode (User Guide p.18-21):
    //   * MAIN     → jump to next/prev BC-bearing track (BC anchor scroll).
    //   * PRESETS  → live-preview navigate the focused CS plug-in's
    //                preset list one preset per detent.
    //   * ROUTING  → cycle the SSL routing-order chunk attribute (A3, TBD).
    //   * EXT_FUNCS→ scroll/adjust the Extended Functions menu (Phase B).
    //   * TRANSPORT→ scrub the playhead (A2, TBD — needs encoder-push id).
    if (ev.id == knob::kBcEncoder && mode_ == Uc1Mode::ExtFuncs) {
        static int extAcc = 0;
        static std::chrono::steady_clock::time_point extLastT{};
        const int step = stepFromAccumulator(extAcc, extLastT, 3);
        if (step == 0) { ++stats_.knobEventsHandled; return; }
        if (!extFuncsActive_) {
            // Inactive (list) mode — rotate cursor with wrap.
            extFuncsIdx_ += step;
            while (extFuncsIdx_ < 0)               extFuncsIdx_ += kExtFuncsCount;
            while (extFuncsIdx_ >= kExtFuncsCount) extFuncsIdx_ -= kExtFuncsCount;
            renderExtFuncsSubscreen_();
            ++stats_.knobEventsHandled;
            return;
        }
        // Active (adjust) mode — change the focused item's param value.
        if (!focusedTrack_) {
            ++stats_.knobEventsHandled;
            return;
        }
        auto match = uf8::lookupPluginOnTrack(focusedTrack_,
                                              uf8::Domain::ChannelStrip);
        if (!match.map) {
            ++stats_.knobEventsHandled;
            return;
        }
        const auto& cur = kExtFuncs[extFuncsIdx_];
        for (const auto& s : match.map->slots) {
            if (!s.id || std::strcmp(s.id, cur.slotId) != 0) continue;
            if (s.vst3Param < 0) break;
            const double curN = TrackFX_GetParamNormalized(
                static_cast<MediaTrack*>(focusedTrack_),
                match.fxIndex, s.vst3Param);
            // Per-param step sizing. Toggles flip on a single detent;
            // stepped enums advance one step per detent; continuous
            // params use 1%/detent (0.1% in fine mode). REAPER returns
            // step values in NORMALIZED [0..1] units via the
            // GetParameterStepSizes API.
            double pStep = 0.0, pSmallStep = 0.0, pLargeStep = 0.0;
            bool isToggle = false;
            const bool haveStepInfo = TrackFX_GetParameterStepSizes(
                static_cast<MediaTrack*>(focusedTrack_),
                match.fxIndex, s.vst3Param,
                &pStep, &pSmallStep, &pLargeStep, &isToggle);
            double next = curN;
            // Look up usl up front so both the stepped + continuous
            // branches can read sens / range without re-querying.
            char fxBuf[256] = {0};
            TrackFX_GetFXName(
                static_cast<MediaTrack*>(focusedTrack_),
                match.fxIndex, fxBuf, sizeof(fxBuf));
            const uf8::UserLinkSlot* usl =
                uf8::user_plugins::lookupOwnedSlot(fxBuf, s.linkIdx);
            if (haveStepInfo && isToggle) {
                // 1 detent (in either direction) flips the toggle.
                next = (curN >= 0.5) ? 0.0 : 1.0;
            } else if (haveStepInfo && pStep > 0.0) {
                // Stepped enum — input `step` is already pre-accumulated
                // upstream (3 raw detents → 1 step via extAcc, see
                // line 782). At sens=1.0 we keep the legacy "1 step per
                // detent" feel; sens>1 fires multiple steps per detent,
                // sens<1 uses a residual accumulator so a slow turn
                // still advances eventually. Range snaps to the step
                // grid.
                const bool fineSt = fineMode_.load(std::memory_order_relaxed)
                                   || reasixty_shiftFineActive();
                float sens = usl ? usl->sensitivity : 1.0f;
                if (sens < 0.1f) sens = 0.1f;
                else if (sens > 8.0f) sens = 8.0f;
                if (fineSt) sens *= 0.25f;
                static float s_accExt[0x40]{};
                static std::chrono::steady_clock::time_point s_lastExt[0x40]{};
                auto& acc   = s_accExt[s.linkIdx & 0x3F];
                auto& lastT = s_lastExt[s.linkIdx & 0x3F];
                const auto now = std::chrono::steady_clock::now();
                if (lastT.time_since_epoch().count() != 0
                    && now - lastT > std::chrono::milliseconds(150)) {
                    acc = 0.0f;
                }
                lastT = now;
                if ((step > 0 && acc < 0.0f)
                    || (step < 0 && acc > 0.0f)) acc = 0.0f;
                acc += static_cast<float>(step) * sens;
                const int logical = static_cast<int>(acc);
                acc -= static_cast<float>(logical);
                if (logical == 0) {
                    ++stats_.knobEventsHandled;
                    return;
                }
                const float pStepF = static_cast<float>(pStep);
                const double minN = static_cast<double>(uf8::snapToStep(
                    usl ? usl->rangeMin : 0.0f, pStepF));
                const double maxN = static_cast<double>(uf8::snapToStep(
                    usl ? usl->rangeMax : 1.0f, pStepF));
                next = curN + logical * pStep;
                if (next < minN) next = minN;
                if (next > maxN) next = maxN;
            } else {
                // Continuous — 1% per detent, 0.1% in fine mode. When the
                // bound plug-in is a user-learned map with an explicit
                // UserLinkSlot for this slot, apply per-slot knob travel
                // (sensitivity + range/curve) so the EXT_FUNCS encoder
                // and a UF8 V-Pot bound to the same slot stay in sync.
                const bool fine = fineMode_.load(std::memory_order_relaxed)
                                 || reasixty_shiftFineActive();
                const double scale = fine ? 0.001 : 0.01;
                double delta = step * scale;
                if (usl) {
                    delta *= static_cast<double>(usl->sensitivity);
                    double t = static_cast<double>(
                        uf8::inverseCurve(*usl, static_cast<float>(curN)));
                    t += delta;
                    if (t < 0.0) t = 0.0;
                    if (t > 1.0) t = 1.0;
                    next = static_cast<double>(
                        uf8::applyCurve(*usl, static_cast<float>(t)));
                } else {
                    next = curN + delta;
                }
            }
            if (next < 0.0) next = 0.0;
            if (next > 1.0) next = 1.0;
            TrackFX_SetParamNormalized(
                static_cast<MediaTrack*>(focusedTrack_),
                match.fxIndex, s.vst3Param, next);
            uf8::param_groups::broadcastBuiltinSlot(
                static_cast<MediaTrack*>(focusedTrack_),
                uf8::Domain::ChannelStrip, s.linkIdx, next);
            reasixty_bumpFolderReveal(
                static_cast<MediaTrack*>(focusedTrack_));
            break;
        }
        renderExtFuncsSubscreen_();
        ++stats_.knobEventsHandled;
        return;
    }
    if (ev.id == knob::kBcEncoder && mode_ == Uc1Mode::Transport) {
        // Sec-Encoder rotation in TRANSPORT scrubs the playhead. ~0.5s
        // per detent (musical "feels-right" baseline; can tune later).
        // Fine modifier shrinks to 0.05s/detent for fine-grained edits.
        // Pure SetEditCurPos — no MCU/HUI dependency.
        static int xportAcc = 0;
        static std::chrono::steady_clock::time_point xportLastT{};
        const int step = stepFromAccumulator(xportAcc, xportLastT, 3);
        if (step == 0) { ++stats_.knobEventsHandled; return; }
        const bool fine2 = fineMode_.load(std::memory_order_relaxed)
                          || reasixty_shiftFineActive();
        const double secsPerDetent = fine2 ? 0.05 : 0.5;
        const double curPos = GetCursorPosition();
        double newPos = curPos + step * secsPerDetent;
        if (newPos < 0.0) newPos = 0.0;
        SetEditCurPos(newPos, true /*moveview*/, false /*seekplay*/);
        ++stats_.knobEventsHandled;
        return;
    }
    if (ev.id == knob::kBcEncoder && mode_ == Uc1Mode::Routing) {
        static int routingAcc = 0;
        static std::chrono::steady_clock::time_point routingLastT{};
        const int step = stepFromAccumulator(routingAcc, routingLastT, 3);
        if (step == 0) { ++stats_.knobEventsHandled; return; }
        if (!focusedTrack_) {
            ++stats_.knobEventsHandled;
            return;
        }
        auto match = uf8::lookupPluginOnTrack(focusedTrack_,
                                              uf8::Domain::ChannelStrip);
        if (!match.map) {
            ++stats_.knobEventsHandled;
            return;
        }
        // Routing-preset table — 10 main 4-tuples (FiltIn, FiltSC, EqSC,
        // DynPreEq) the SSL CS plug-in's routing GUI cycles through
        // (decoded from user's 2026-05-01 dump-routing-flags sequence
        // covering manual Orders 1..9 + a 10th non-manual reachable
        // combo; manual Order 10 "EQ → Dyn → Filters" is unreachable
        // in this plug-in's routing UI). ExtSC adds the b-variant for
        // each preset (20 total cycle positions = idx 0..19).
        struct Preset { uint8_t filtIn, filtSC, eqSC, dynPreEq; };
        static constexpr Preset kPresets[10] = {
            { 1, 0, 0, 0 },  // Order 1:  F → E → D
            { 0, 0, 0, 0 },  // Order 2:  E → F → D (Filters off main)
            { 0, 0, 0, 1 },  // Order 3:  D → E → F (Filters off main)
            { 1, 0, 0, 1 },  // Order 4:  F → D → E
            { 1, 1, 0, 1 },  // Order 5:  F → D → E (Filt → S/C)
            { 1, 0, 1, 0 },  // Order 6:  F → E → D (EQ → S/C)
            { 1, 1, 0, 0 },  // Order 7:  F → E → D (Filt → S/C)
            { 1, 1, 1, 0 },  // Order 8:  E → F → D (Filt+EQ → S/C)
            { 0, 0, 1, 0 },  // Order 9:  E → F → D (EQ → S/C)
            { 0, 1, 1, 1 },  // Order 10: alt (D → E + Filt+EQ S/C)
        };
        // Read current flags to find which preset position we're at —
        // gives the user a stable starting point if they entered the
        // routing GUI at any combo, including dialed-by-hand. -1 if
        // current combo isn't one of the 10 presets (jump to 0).
        auto readFlag = [&](const char* slotId) -> int {
            for (const auto& s : match.map->slots) {
                if (s.id && std::strcmp(s.id, slotId) == 0 && s.vst3Param >= 0) {
                    return TrackFX_GetParamNormalized(
                        static_cast<MediaTrack*>(focusedTrack_),
                        match.fxIndex, s.vst3Param) >= 0.5 ? 1 : 0;
                }
            }
            return 0;
        };
        const int curFiltIn   = readFlag("FiltersToInput");
        const int curFiltSC   = readFlag("FiltersToSC");
        const int curEqSC     = readFlag("EqToSC");
        const int curDynPreEq = readFlag("DynamicsPreEq");
        const int curExtSC    = readFlag("ExternalSC");
        int curPresetIdx = -1;
        for (int i = 0; i < 10; ++i) {
            if (kPresets[i].filtIn   == curFiltIn   &&
                kPresets[i].filtSC   == curFiltSC   &&
                kPresets[i].eqSC     == curEqSC     &&
                kPresets[i].dynPreEq == curDynPreEq) {
                curPresetIdx = i;
                break;
            }
        }
        // Combined index 0..19: even = main preset, odd = b-variant.
        // Cycle wraps (no clamp) — matches SSL360's continuous scroll.
        int combinedIdx = (curPresetIdx >= 0)
            ? curPresetIdx * 2 + curExtSC
            : 0;
        int newCombined = combinedIdx + step;
        while (newCombined < 0)   newCombined += 20;
        while (newCombined >= 20) newCombined -= 20;
        const int newPresetIdx = newCombined / 2;
        const int newExtSC     = newCombined % 2;
        const Preset& p = kPresets[newPresetIdx];
        // Apply the preset.
        auto setFlag = [&](const char* slotId, int v) {
            for (const auto& s : match.map->slots) {
                if (s.id && std::strcmp(s.id, slotId) == 0 && s.vst3Param >= 0) {
                    const double nv = v ? 1.0 : 0.0;
                    TrackFX_SetParamNormalized(
                        static_cast<MediaTrack*>(focusedTrack_),
                        match.fxIndex, s.vst3Param, nv);
                    uf8::param_groups::broadcastBuiltinSlot(
                        static_cast<MediaTrack*>(focusedTrack_),
                        uf8::Domain::ChannelStrip, s.linkIdx, nv);
                    return;
                }
            }
        };
        setFlag("FiltersToInput", p.filtIn);
        setFlag("FiltersToSC",    p.filtSC);
        setFlag("EqToSC",         p.eqSC);
        setFlag("DynamicsPreEq",  p.dynPreEq);
        setFlag("ExternalSC",     newExtSC);
        reasixty_bumpFolderReveal(static_cast<MediaTrack*>(focusedTrack_));
        // LCD routing-order indicator: byte 0x01..0x0A for main, 0x80
        // OR'd in for b-variants.
        const uint8_t orderByte = static_cast<uint8_t>(
            (newPresetIdx + 1) | (newExtSC ? 0x80 : 0x00));
        if (device_) {
            device_->send(buildRoutingOrderIndicator(orderByte));
        }
        ++stats_.knobEventsHandled;
        return;
    }
    if (ev.id == knob::kBcEncoder && mode_ == Uc1Mode::Presets) {
        static int presetAcc = 0;
        static std::chrono::steady_clock::time_point presetLastT{};
        const int step = stepFromAccumulator(presetAcc, presetLastT, 3);
        if (step == 0) { ++stats_.knobEventsHandled; return; }
        if (presetsSub_ == PresetsSubMode::Selector) {
            // Toggle CS/BC selection on every rotation step (only two
            // options, so the magnitude of step is irrelevant — flip).
            presetsSelectCs_ = !presetsSelectCs_;
            renderPresetsSubscreen_();
            ++stats_.knobEventsHandled;
            return;
        }
        // Browse subscreen — live-preview navigate the focused-domain
        // plug-in's preset list. NavigatePresets loads each preset on
        // the way (REAPER's only API for sequential preset traversal).
        if (!focusedTrack_) {
            ++stats_.knobEventsHandled;
            return;
        }
        auto match = uf8::lookupPluginOnTrack(
            focusedTrack_,
            presetsSelectCs_ ? uf8::Domain::ChannelStrip
                             : uf8::Domain::BusComp);
        if (!match.map) {
            ++stats_.knobEventsHandled;
            return;
        }
        const int dir = step > 0 ? 1 : -1;
        const int abs = step > 0 ? step : -step;
        for (int k = 0; k < abs; ++k) {
            TrackFX_NavigatePresets(static_cast<MediaTrack*>(focusedTrack_),
                                    match.fxIndex, dir);
        }
        renderPresetsSubscreen_();
        ++stats_.knobEventsHandled;
        return;
    }

    // BC encoder — jump to the next/prev track (relative to the
    // current BC anchor) that has a plugin targeted by the Bus-Comp
    // section. "Next" = first BC track whose project index is
    // greater than the anchor's; "prev" = first BC track whose
    // project index is smaller. Anchor seeds from the effective BC
    // track so subsequent scrolls advance from where the display
    // currently sits, not from the CS focus.
    //
    // Only active in MAIN mode — in ROUTING / EXT_FUNCS / TRANSPORT
    // the rotation is reserved for menu navigation (cycling routing
    // orders, scrolling param list, scrubbing playhead). Without this
    // gate, entering any menu from the UC1 would still scroll BC
    // tracks under the user's hand.
    if (ev.id == knob::kBcEncoder && mode_ != Uc1Mode::Main) {
        // Consume the event silently — mode-specific handlers (Presets
        // branch above; Routing/ExtFuncs/Transport TBD) own the
        // dispatch in their own modes. Fall-through would clobber the
        // BC track focus on every detent.
        ++stats_.knobEventsHandled;
        return;
    }
    if (ev.id == knob::kBcEncoder) {
        static int acc = 0;
        static std::chrono::steady_clock::time_point lastT{};
        // ticksPerStep=3 — matches Encoder 1 (Frank 2026-05-26 reported
        // Enc2 occasionally skipping values at threshold 2: a 3-tick
        // detent landed step=1 with residual=1, the next 1-tick burst
        // pushed acc to 2 → step=1 fired again, advancing two BC tracks
        // for one physical click. Threshold 3 catches both 3-tick
        // detents and 2-tick burst pairs without double-firing; the
        // earlier "2-tick clicks get eaten" worry didn't reproduce here
        // because the gap-reset already absorbs lone shorts.
        int step = stepFromAccumulator(acc, lastT, 3);
        if (step == 0) { ++stats_.knobEventsHandled; return; }
        // Phase 2.8b — Nav Mode cursor scroll. When the overlay is
        // active, Encoder 2 walks one marker/region per detent regardless
        // of SEL Mode or bindings dispatch. moveCursor sets the cursor
        // pin so auto-follow won't fight back. g_navOverlayDirty triggers
        // a UF8 re-paint on the next tick; the UC1 carousel is repushed
        // every tick via pushUc1NavCarousel so no explicit signal needed.
        if (uf8::nav::Overlay::instance().active()
            && reasixty_navUc1Takeover())
        {
            // Independent UC1 modes (Regions / Markers) use a UC1-local
            // cursor so UF8's overlay cursor stays untouched. Mirror
            // mode (== 0) walks the shared Overlay cursor as before.
            if (reasixty_navUc1Mode() != 0) {
                reasixty_navUc1CursorDelta(step);
            } else {
                uf8::nav::Overlay::instance().moveCursor(step);
            }
            reasixty_markNavOverlayDirty();
            ++stats_.knobEventsHandled;
            return;
        }
        // SEL Mode override — when bit 3 (UC1 Encoder 2) is ticked in
        // Settings → Modes → FX/Instance Cycle AND SelectionMode is
        // Instance / InstanceCycle, hijack the encoder away from its
        // bindings (fx_cycle / bc_track_scroll / etc.) and drive the
        // cycle on the focused track. Bindings dispatch resumes the
        // moment SEL Mode leaves.
        if ((reasixty_cycleControlMask() & kCycleCtrlUc1Enc2) != 0
            && reasixty_dispatchSelModeCycle(step))
        {
            ++stats_.knobEventsHandled;
            return;
        }
        // REC + RME (TotalReaper) override — when active, Encoder 2
        // rotation steps preamp gain ±1 dB on the focused track, or
        // changes the input channel when Shift is held. Mirrors the
        // UF8 V-Pot logic at main.cpp ~7846. Bridges resolve to false
        // when RME is off / SelectionMode isn't Rec, so the normal
        // bindings dispatch path runs in every other case.
        if (focusedTrack_) {
            auto* trMedia = static_cast<MediaTrack*>(focusedTrack_);
            // UC1 has no Shift button — Fine doubles as Shift here so
            // users without keyboard-Shift enabled can still reach the
            // input-channel cycle (already true elsewhere via
            // reasixty_shiftFineActive).
            const bool shiftMod =
                uf8::bindings::modifierHeld(uf8::bindings::Modifier::Shift)
                || fineMode_.load(std::memory_order_relaxed);
            if (shiftMod) {
                if (reasixty_dispatchUc1RecRmeInputChan(trMedia, step)) {
                    ++stats_.knobEventsHandled;
                    return;
                }
            } else {
                if (reasixty_dispatchUc1RecRmeGain(trMedia, step)) {
                    ++stats_.knobEventsHandled;
                    return;
                }
            }
        }
        // Bindings-routed dispatch (Uc1Encoder2). Falls back to the
        // legacy BC-track-scroll when no binding exists (older
        // bindings.json files saved before this surface became
        // bindable) so behaviour is preserved across upgrades.
        if (!uf8::bindings::dispatchEncoder(
                uf8::bindings::ButtonId::Uc1Encoder2, step)) {
            applyBcTrackScroll(step);
        }
        ++stats_.knobEventsHandled;
        return;
    }

    if (!focusedTrack_) {
        if (logThis) {
            char line[96];
            snprintf(line, sizeof(line),
                "UC1 knob 0x%02x delta=%d  (no focused track)\n",
                ev.id, (int)ev.delta);
        }
        ++stats_.knobEventsSuppressed;
        return;
    }

    auto csBindings = lookupBindingsOnTrack(focusedTrack_);
    // BC writes target the BC anchor track (independent of focusedTrack_)
    // so the BC section stays pinned regardless of the user's CS focus.
    void* bcAnchorRaw = effectiveBcTrack_();
    UC1Bindings bcBindings = bcAnchorRaw
        ? ((bcAnchorRaw == focusedTrack_) ? csBindings
                                          : lookupBindingsOnTrack(bcAnchorRaw))
        : UC1Bindings{};
    if (!bcBindings.busCompMap && !csBindings.channelMap) {
        if (logThis) {
            char line[96];
            snprintf(line, sizeof(line),
                "UC1 knob 0x%02x delta=%d  (no BC anchor + no CS on focus)\n",
                ev.id, (int)ev.delta);
        }
        ++stats_.knobEventsSuppressed;
        return;
    }

    // Pick Bus Comp when available and the knob is a V-Pot; otherwise
    // fall back to Channel Strip. The readout zone is unified to 0x05
    // (kBusCompReadout) regardless of which family the knob writes to —
    // see pushFocusedParamReadout_ for the rationale.
    const PluginBindings* map = nullptr;
    int fxIdx = -1;
    bool busCompContext = false;
    void* writeTrackRaw = focusedTrack_;

    const ControlDomain domain = classifyKnob(ev.id);
    if (domain == ControlDomain::BusComp && bcBindings.busCompMap) {
        map           = bcBindings.busCompMap;
        fxIdx         = bcBindings.busCompFxIdx;
        busCompContext = true;
        writeTrackRaw = bcAnchorRaw;
    } else if (csBindings.channelMap) {
        map   = csBindings.channelMap;
        fxIdx = csBindings.channelFxIdx;
    } else if (bcBindings.busCompMap) {
        // No CS plugin — even a CS-area knob won't have anywhere to go.
        // Fall through as suppressed.
    }

    if (!map) {
        if (logThis) {
            char line[96];
            snprintf(line, sizeof(line),
                "UC1 knob 0x%02x delta=%d  (no matching domain binding)\n",
                ev.id, (int)ev.delta);
        }
        ++stats_.knobEventsSuppressed; return;
    }

    const int vst3Param = map->knobParam[ev.id];
    if (vst3Param == kParamNone) {
        if (logThis) {
            char line[128];
            snprintf(line, sizeof(line),
                "UC1 knob 0x%02x delta=%d  unmapped in %s  (add to UC1PluginMap)\n",
                ev.id, (int)ev.delta, map->shortName);
        }
        ++stats_.knobEventsSuppressed;
        return;
    }

    MediaTrack* tr = static_cast<MediaTrack*>(writeTrackRaw);
    double cur = TrackFX_GetParamNormalized(tr, fxIdx, vst3Param);

    // User has started editing a param — the scroll-overlay flags were
    // set so the carousel could hold the LCD after an Encoder 1/2 spin,
    // but a knob turn means scrolling is over. Without this, an Encoder
    // 2 scroll (3 s overlay) followed by an immediate UC1 BC knob edit
    // left pushFocusedParamReadout_'s overlay gate suppressing the
    // zone 0x05 readout for up to 3 s — Frank 2026-05-12: "UC1 BC
    // controls werden nicht berücksichtigt" when BC anchor != selected.
    // Clear both overlays so the next pushFocusedParamReadout_ (below)
    // is allowed through and the carousel-blocked CS zone reopens too.
    if (bcScrollOverlayActive_ || csScrollOverlayActive_) {
        const bool wasBc = bcScrollOverlayActive_;
        bcScrollOverlayActive_ = false;
        csScrollOverlayActive_ = false;
        // Drop the carousel banner if we were holding BC scroll — the
        // user is back to a regular param edit on the BC anchor and
        // expects the standard MAIN-mode central layout (matches the
        // overlay-expiry path in poll()).
        if (wasBc && device_ && mode_ == Uc1Mode::Main) {
            device_->send(buildCentralMode(CentralMode::Main, 0x00));
            refresh();
        }
    }

    // EQ-gain knobs use a finer per-click step than the rest of the
    // surface. With the global 1/64 the dB-per-click varies by band
    // because different SSL EQ ranges map differently into the same
    // normalized sweep — wider-range bands felt ~1 dB/click, narrower
    // bands ~0.5 dB. Halving for the four gains gives a uniformly
    // tighter feel and makes the post-snap nudge close to 0.25 dB.
    const bool isEqGain = (ev.id == knob::kCSHfGain
                        || ev.id == knob::kCSHmfGain
                        || ev.id == knob::kCSLmfGain
                        || ev.id == knob::kCSLfGain);

    // Per-param step sizing. Mirrors the EXT_FUNCS encoder handler
    // above. Discrete-stepped params (e.g. bx_townhouse Buss Comp
    // Ratio, 5 steps → norm step = 0.25) need 1 detent = 1 step;
    // without this the 1/64 click-delta meant ~16 clicks per stop.
    // Toggles flip on a single detent. Continuous params keep the
    // existing 1/64 + EQ-gain magnet path.
    double pStep = 0.0, pSmall = 0.0, pLarge = 0.0;
    bool   isToggle = false;
    const bool haveStepInfo = TrackFX_GetParameterStepSizes(
        tr, fxIdx, vst3Param, &pStep, &pSmall, &pLarge, &isToggle);

    // Hoist the user-slot lookup so both the stepped + continuous
    // branches can honour per-binding sensitivity / range. Built-in
    // SSL CS/BC params have no UserLinkSlot — usl stays null and the
    // defaults (sens=1, range 0..1) preserve byte-identical behaviour.
    char fxBuf[256] = {0};
    TrackFX_GetFXName(tr, fxIdx, fxBuf, sizeof(fxBuf));
    const uf8::Domain dom = busCompContext
        ? uf8::Domain::BusComp
        : uf8::Domain::ChannelStrip;
    int linkIdx = -1;
    {
        auto mm = uf8::lookupPluginOnTrack(tr, dom);
        if (mm.map && mm.fxIndex == fxIdx) {
            linkIdx = uf8::slotIdxForVst3Param(*mm.map, vst3Param);
        }
    }
    const uf8::UserLinkSlot* usl = (linkIdx >= 0)
        ? uf8::user_plugins::lookupOwnedSlot(fxBuf, linkIdx)
        : nullptr;

    double next;
    if (haveStepInfo && isToggle) {
        // Any detent flips the toggle. Sign of delta is irrelevant.
        next = (cur >= 0.5) ? 0.0 : 1.0;
    } else if (haveStepInfo && pStep > 0.0) {
        // Discrete-stepped — accumulate raw detents into logical steps
        // so a fast hardware scroll doesn't slam through 8 stops. The
        // detents-per-step threshold derives from user sensitivity:
        // sens=1 → 2 detents/step (legacy baseline); sens=2 → 1
        // detent/step; sens=0.5 → 4 detents/step; sens≥4 fires multiple
        // steps per detent via the fractional accumulator. Range snaps
        // to the step grid when usl carries a custom rangeMin/Max.
        static float s_stepAcc[0x20]{};
        static std::chrono::steady_clock::time_point s_stepLastT[0x20]{};
        auto& acc   = s_stepAcc[ev.id & 0x1F];
        auto& lastT = s_stepLastT[ev.id & 0x1F];
        const auto now = std::chrono::steady_clock::now();
        if (lastT.time_since_epoch().count() != 0
            && now - lastT > std::chrono::milliseconds(150)) {
            acc = 0.0f;
        }
        lastT = now;
        const int signedDet = ev.delta * (map->inverted[ev.id] ? -1 : 1);
        float sens = usl ? usl->sensitivity : 1.0f;
        // Shift-fine: quarter the effective speed (more detents per
        // step). Honours both the surface's own fine toggle and the
        // global keyboard-Shift fine modifier — matches the continuous
        // branch in this same function and the UF8 V-Pot paths.
        const bool fineSt = fineMode_.load(std::memory_order_relaxed)
                          || reasixty_shiftFineActive();
        if (fineSt) sens *= 0.25f;
        const auto r = uf8::tickStepped(acc, signedDet, sens);
        acc = r.newAccum;
        if (r.logicalSteps == 0) {
            ++stats_.knobEventsHandled;
            return;
        }
        const float pStepF = static_cast<float>(pStep);
        const double minN = static_cast<double>(uf8::snapToStep(
            usl ? usl->rangeMin : 0.0f, pStepF));
        const double maxN = static_cast<double>(uf8::snapToStep(
            usl ? usl->rangeMax : 1.0f, pStepF));
        next = cur + r.logicalSteps * pStep;
        if (next < minN) next = minN;
        if (next > maxN) next = maxN;
    } else {
        // Continuous — clickToDelta_ + EQ-gain magnet, plus per-slot
        // knob travel (range/curve/sensitivity) when the bound plug-in
        // is a user-learned map with an explicit UserLinkSlot. Built-in
        // SSL CS/BC slots have no UserLinkSlot → keep the legacy linear
        // + EQ-gain-magnet path so default behaviour is byte-identical.
        // Mirrors the UF8 V-Pot branch in main.cpp so a UC1 knob and a
        // UF8 V-Pot bound to the same param share scaling.

        double delta = clickToDelta_(ev.delta);
        if (isEqGain) delta *= 0.5;
        delta *= (map->inverted[ev.id] ? -1.0 : 1.0);
        if (usl) {
            // User customised travel — skip the EQ-gain virtual notch
            // (it presumes SSL EQ topology) and apply sensitivity +
            // range/curve via t-space around the current value.
            delta *= static_cast<double>(usl->sensitivity);
            double t = static_cast<double>(
                uf8::inverseCurve(*usl, static_cast<float>(cur)));
            t += delta;
            if (t < 0.0) t = 0.0;
            if (t > 1.0) t = 1.0;
            next = static_cast<double>(
                uf8::applyCurve(*usl, static_cast<float>(t)));
        } else {
            next = isEqGain
                ? uf8::applyVirtualNotch(cur, delta, /*center*/0.5,
                                         /*zone*/0.015, 0.0, 1.0)
                : std::clamp(cur + delta, 0.0, 1.0);
        }
    }
    const bool uc1SetOk = TrackFX_SetParamNormalized(tr, fxIdx, vst3Param, next);
    const double uc1After = TrackFX_GetParamNormalized(tr, fxIdx, vst3Param);
    diagSetParamLog_("uc1/knob", tr, fxIdx, vst3Param, next, uc1SetOk, uc1After);
    reasixty_bumpFolderReveal(tr);
    // Touched-FX reveal (3 s) — the strip + UC1 LCD show whatever
    // plug-in this knob just wrote to, regardless of the active mode
    // (Instance Selection, SSL Strip Mode, focused-domain default).
    // Frank 2026-05-15: "Touched-FX gewinnt mit 3 s reveal".
    reasixty_pushTouchedFxReveal(
        writeTrackRaw, fxIdx,
        static_cast<int>(busCompContext
            ? uf8::Domain::BusComp
            : uf8::Domain::ChannelStrip));
    // "View active plugin" follow is intentionally NOT called here
    // anymore — the inline TrackFX_Show pair was disrupting the
    // subsequent focus-projection block (UF8 stopped switching its
    // colour-bar plug-in label on cross-domain touches). The same
    // intent is now implemented in the timer-tick path via
    // reasixty_followFocusedGuiToFx + a request flag set below, so
    // the GUI swap happens after handleKnob_ has finished updating
    // focus state. Frank 2026-05-15.

    // Project the focused-param onto UF8: turning a UC1 knob makes the
    // touched parameter the new focused param across the bank. We look
    // up UF8's PluginMap (separate types from uc1::PluginBindings even
    // though both describe the same on-track plug-in) for the same
    // domain, then find the slot whose vst3Param matches what UC1 just
    // wrote. If no slot maps (e.g. UC1 knob writes a param UF8's slot
    // list doesn't expose) we leave the focus untouched.
    //
    // Discriminator is busCompContext (= "we actually used the BC map"),
    // NOT the raw `domain` from classifyKnob(): a BC knob on a track
    // without BC2 falls through to the CS map (line 357 above) and the
    // resulting write is a CS-domain operation — so the focus must
    // mirror CS, otherwise we'd write Domain::BusComp + a CS-derived
    // slotIdx and trigger a render against the wrong plug-in family.
    const auto uf8Domain = busCompContext
        ? uf8::Domain::BusComp
        : uf8::Domain::ChannelStrip;
    // Look up the UF8 plugin on whichever track the write actually went
    // to — for BC knobs that's bcAnchor, not focusedTrack_. Without this
    // a BC knob on a CS-only focused track would skip the focus push.
    auto uf8Match = uf8::lookupPluginOnTrack(writeTrackRaw, uf8Domain);
    bool focusedParamRendered = false;
    if (uf8Match.map) {
        const int slotIdx = uf8::slotIdxForVst3Param(*uf8Match.map, vst3Param);
        if (slotIdx >= 0) {
            uf8::param_groups::broadcastBuiltinSlot(
                static_cast<MediaTrack*>(writeTrackRaw),
                uf8Domain, slotIdx, next);
            // Cross-domain knob touch flips the focused-param domain.
            // The central LCD's plug-in shortName label is computed off
            // this domain in refresh(), so a BC↔CS shift on the same
            // track has to force a refresh — neither setFocusedTrack
            // nor setBcAnchorTrack will fire here (same track stays
            // focused / anchored), so the LCD would keep showing the
            // outgoing domain's plug-in name. Mirrors the pattern from
            // the CHANNEL encoder handler above (which forces CS focus
            // + refresh()). Frank 2026-05-15: "EQ am UC1 bewegt, UC1
            // bleibt auf Townhouse statt 4K E".
            const bool domainShifted =
                (uf8::getFocusedParam().domain != uf8Domain);
            uf8::setFocus({uf8Domain, slotIdx});
            // Unified readout: same code path as poll-tick value polling
            // and Page <-/-> external focus changes. Dedup cache inside
            // pushFocusedParamReadout_ ensures we don't double-push when
            // the next poll() tick runs immediately after this.
            pushFocusedParamReadout_();
            if (domainShifted) refresh();
            focusedParamRendered = true;
        }
    }
    if (!focusedParamRendered) {
        // Knob wrote a vst3Param outside UF8's slot list (or the track
        // has no UF8-recognised plugin for this domain). Fall back to a
        // direct per-knob readout so the user still sees their edit;
        // skips the dedup cache (next poll-tick will re-render the
        // focused param's text, overwriting this transient).
        pushKnobReadout_(ev.id, tr, fxIdx, vst3Param,
                         zone::kBusCompReadout,
                         labelForKnob(ev.id, busCompContext));
    }
    // "View active plugin" follow — queue a deferred swap so the
    // floating FX window switches to the touched FX on the next timer
    // tick (handled by the drainer in onTimer). Fired AFTER the focus
    // projection block above so a same-tick swap can't disrupt the
    // focus state we just set. No-op when no focused-GUI window is
    // currently open or it's already on this FX.
    reasixty_followFocusedGuiToFx(tr, fxIdx);

    // Pass the visual position (flipped when the pot is inverted) so
    // the LED ring goes CW when the pot goes CW — independent of which
    // way the VST3 param value moves.
    const double visual = map->inverted[ev.id] ? (1.0 - next) : next;
    pushKnobRing_(ev.id, visual);

    // (Old defensive 7-seg repaint after FaderLevel/BCMix/BCRelease ring
    // moves removed — uc1_31/32 confirmed those rings actually live on
    // byte5=0x01 while the 7-seg writes on byte5=0x00, so the cell
    // numbers collide but the LEDs do not.)

    if (logThis) {
        char pname[64] = {0};
        TrackFX_GetParamName(tr, fxIdx, vst3Param, pname, sizeof(pname));
        char line[192];
        snprintf(line, sizeof(line),
            "UC1 knob 0x%02x '%s' plug=%s inv=%d delta=%d → param %d '%s' val=%.3f\n",
            ev.id, labelForKnob(ev.id, busCompContext),
            map->shortName, map->inverted[ev.id] ? 1 : 0,
            (int)ev.delta, vst3Param, pname, next);
    }

    ++stats_.knobEventsHandled;
}

void UC1Surface::handleButton_(const ButtonEvent& ev)
{
    // Fine is a latching toggle — press flips the mode, release is a
    // no-op. LED reflects the latched state. User preference
    // 2026-04-23: momentary "hold to fine-tune" was awkward for long
    // parameter sweeps; toggle lets the user engage Fine once and
    // adjust as many knobs as they want.
    if (ev.id == button::kFine) {
        // In REC / RecMon: Fine acts as a momentary modifier (hold to
        // engage; release to drop). Doubles as the Shift-equivalent for
        // the Enc2 input-channel toggle while tracking. Readout text
        // is suppressed in this mode so the readout zone can keep its
        // REC+RME content (48V/Pd/Ph + gain) instead of flashing
        // "Fine On/Off".
        if (reasixty_inRecOrRecMonMode()) {
            fineMode_.store(ev.pressed, std::memory_order_relaxed);
            pushButtonLed_(ev.id, ev.pressed);
            if (ev.pressed) ++stats_.buttonEventsHandled;
            return;
        }
        if (ev.pressed) {
            const bool next = !fineMode_.load(std::memory_order_relaxed);
            fineMode_.store(next, std::memory_order_relaxed);
            pushButtonLed_(ev.id, next);
            pushButtonReadout_(ev.id, "Fine", next ? "On" : "Off",
                               zone::kChannelStripReadout);
        }
        ++stats_.buttonEventsHandled;
        return;
    }

    // Central Control Panel — Back / Confirm / Routing / Presets / 360° /
    // Magnifier. These cycle the UC1's MAIN/EXT_FUNCS/ROUTING/PRESETS/
    // TRANSPORT mode (UC1 User Guide p.18-21). All operate on the press
    // edge; no LEDs (the buttons themselves are unlit on the panel).
    //
    // Phase A1: state machine + mode toggling only. ROUTING/PRESETS/
    // TRANSPORT bodies (chunk patch / preset nav / transport actions)
    // and the LCD top-label rendering land in subsequent commits.
    // setMode() is a member function (UC1Surface::setMode) — the local
    // lambda used to live here; hoisted as part of Phase 2.8b so the
    // Nav arbitration loop can force Main on overlay entry. All call
    // sites below now invoke the member directly.
    if (ev.id == button::kBack) {
        if (ev.pressed) {
            switch (mode_) {
                case Uc1Mode::Main:      setMode(Uc1Mode::ExtFuncs); break;
                case Uc1Mode::ExtFuncs:  setMode(Uc1Mode::Main);     break;
                case Uc1Mode::Routing:   setMode(Uc1Mode::Main);     break;
                case Uc1Mode::Presets:
                    // BACK in Browse → up to Selector. BACK in
                    // Selector → exit to MAIN.
                    if (presetsSub_ == PresetsSubMode::Browse) {
                        presetsSub_ = PresetsSubMode::Selector;
                        // Re-send banner 0x03 (we switched to 0x02 in
                        // Browse) + selector content.
                        if (device_) {
                            device_->send(buildCentralMode(CentralMode::Presets));
                        }
                        renderPresetsSubscreen_();
                    } else {
                        setMode(Uc1Mode::Main);
                    }
                    break;
                case Uc1Mode::Transport:
                    // BACK in TRANSPORT = Stop. REAPER action 1016.
                    Main_OnCommand(1016, 0);
                    break;
            }
            ++stats_.buttonEventsHandled;
        }
        return;
    }
    if (ev.id == button::kConfirm) {
        if (ev.pressed) {
            switch (mode_) {
                case Uc1Mode::Main:      /* nop */ break;
                case Uc1Mode::ExtFuncs:  /* B: confirm selection */ break;
                case Uc1Mode::Routing:   /* nop */ break;
                case Uc1Mode::Presets:
                    // Live-preview model: every encoder detent already
                    // loaded the preset. Confirm = "I'll keep this one,
                    // back to MAIN."
                    if (focusedTrack_) {
                        auto match = uf8::lookupPluginOnTrack(
                            focusedTrack_, uf8::Domain::ChannelStrip);
                        if (!match.map) {
                            match = uf8::lookupPluginOnTrack(
                                focusedTrack_, uf8::Domain::BusComp);
                        }
                        if (match.map) {
                            char name[128] = {};
                            TrackFX_GetPreset(
                                static_cast<MediaTrack*>(focusedTrack_),
                                match.fxIndex, name, sizeof(name));
                            char line[160];
                            snprintf(line, sizeof(line),
                                "UC1 Preset confirmed: %s\n",
                                name[0] ? name : "<no name>");
                        }
                    }
                    setMode(Uc1Mode::Main);
                    break;
                case Uc1Mode::Transport:
                    // CONFIRM in TRANSPORT = Play. REAPER action 1007.
                    Main_OnCommand(1007, 0);
                    break;
            }
            ++stats_.buttonEventsHandled;
        }
        return;
    }
    if (ev.id == button::kSecEncPush) {
        // Phase 2.8b — Nav Mode push gesture. While overlay-active in
        // MAIN, this button commits a cursor jump (or back/drill via
        // shift / long-press). Intercepted BEFORE the bindings
        // dispatch below so the user's Uc1Encoder2Push binding
        // (default show_focused_plugin_gui) doesn't double-fire.
        // Skipped entirely when the user has turned off the UC1
        // takeover preference (Phase 2.8c).
        if (mode_ == Uc1Mode::Main
            && uf8::nav::Overlay::instance().active()
            && reasixty_navUc1Takeover())
        {
            using namespace std::chrono;
            if (ev.pressed) {
                navEnc2PressTime_ = steady_clock::now();
                navEnc2Pressed_   = true;
                ++stats_.buttonEventsHandled;
                return;
            }
            // Release — decide action based on held duration + shift state.
            if (!navEnc2Pressed_) {
                // Stray release (press was swallowed somewhere else).
                ++stats_.buttonEventsHandled;
                return;
            }
            navEnc2Pressed_ = false;
            const auto held = duration_cast<milliseconds>(
                steady_clock::now() - navEnc2PressTime_).count();
            const bool isLong  = held > 500;
            const bool isShift = (uf8::bindings::currentModifierSnapshot()
                                  == uf8::bindings::Modifier::Shift);

            // Unified action enum (see NavDispatch.h). Resolve the gesture
            // → action via Settings, dispatch via the shared free
            // function so the UF8 channel-encoder push at main.cpp:6720
            // can share the same logic.
            const int plainAct = reasixty_navUc1Push();
            const int shiftAct = reasixty_navUc1PushShift();
            const int longAct  = reasixty_navUc1LongPress();
            const int act = isLong ? longAct : (isShift ? shiftAct : plainAct);
            uf8::nav::dispatchPushActionUc1(act);

            ++stats_.buttonEventsHandled;
            return;
        }
        // MAIN mode: dispatch via Bindings. Default Plain binding is
        // `show_focused_plugin_gui` (Frank 2026-05-14 "das könnte
        // show plugin gui werden"), but the user can swap in any
        // builtin via Settings → Bindings → UC1 Encoder 2 Push. Also
        // dispatch the release edge so Hold-behaviour bindings work.
        // Non-MAIN modes still own the encoder push for menu navigation
        // (Manual p.20-21: Presets confirm, ExtFuncs toggle, Transport
        // exit) — those branches stay hardcoded so the menu UX matches
        // SSL360 regardless of user binding.
        if (mode_ == Uc1Mode::Main) {
            // REC + RME override — runs the user-assigned TotalReaper
            // action on press; swallows the matching release so the
            // regular Uc1Encoder2Push binding never sees half of a
            // press/release pair (would leak an unpaired release into
            // any Hold-behaviour binding).
            if (reasixty_recRmeUc1ButtonAssigned(0) && focusedTrack_) {
                if (ev.pressed) {
                    reasixty_dispatchUc1RecRmeButton(
                        0, static_cast<MediaTrack*>(focusedTrack_));
                    ++stats_.buttonEventsHandled;
                }
                return;
            }
            const bool handled = uf8::bindings::dispatch(
                uf8::bindings::ButtonId::Uc1Encoder2Push, ev.pressed);
            if (!handled && ev.pressed) {
                // Legacy fallback: bindings.json saved before Uc1Encoder2Push
                // existed has no entry for this button. Preserve SSL's
                // factory MAIN→TRANSPORT toggle so the user can still
                // reach transport mode without re-saving their config.
                setMode(Uc1Mode::Transport);
            }
            if (ev.pressed) ++stats_.buttonEventsHandled;
            return;
        }
        if (ev.pressed) {
            switch (mode_) {
                case Uc1Mode::Main:      /* handled above */            break;
                case Uc1Mode::Transport: setMode(Uc1Mode::Main);        break;
                case Uc1Mode::Presets:
                    if (presetsSub_ == PresetsSubMode::Selector) {
                        presetsSub_ = PresetsSubMode::Browse;
                        renderPresetsSubscreen_();
                    } else {
                        // In Browse — preset is already live-loaded by
                        // rotation. Push acts as "confirm + exit"
                        // (same as Confirm button in Presets).
                        setMode(Uc1Mode::Main);
                    }
                    break;
                case Uc1Mode::ExtFuncs:
                    // Toggle list / active (= adjust). Renders the
                    // value frame regardless — list-mode also shows
                    // the current value, active-mode just makes the
                    // encoder rotate the value instead of scrolling.
                    // buildMenuCommit on push toggle highlights the
                    // active-state visual (yellow value / green name
                    // per uc1_37↔uc1_39 diff). NOT sent per scroll
                    // step — SSL360 only emits it on the push toggle.
                    extFuncsActive_ = !extFuncsActive_;
                    if (device_) device_->send(buildMenuCommit(extFuncsActive_));
                    renderExtFuncsSubscreen_();
                    break;
                case Uc1Mode::Routing:   /* nop */ break;
            }
            ++stats_.buttonEventsHandled;
        }
        return;
    }
    if (ev.id == button::kRouting) {
        if (ev.pressed) {
            setMode(mode_ == Uc1Mode::Routing ? Uc1Mode::Main
                                              : Uc1Mode::Routing);
            ++stats_.buttonEventsHandled;
        }
        return;
    }
    if (ev.id == button::kPresets) {
        if (ev.pressed) {
            setMode(mode_ == Uc1Mode::Presets ? Uc1Mode::Main
                                              : Uc1Mode::Presets);
            ++stats_.buttonEventsHandled;
        }
        return;
    }
    if (ev.id == button::k360) {
        // Dispatch via Bindings on press AND release so Hold-behaviour
        // bindings get their release edge. Factory default = the
        // `mixer_toggle` builtin (SSL360's "open GUI" semantically maps
        // to opening our Mixer/Settings window since we ARE the SSL360
        // replacement); user can rebind on the UC1 tab.
        uf8::bindings::dispatch(
            uf8::bindings::ButtonId::Uc1Btn360, ev.pressed);
        if (ev.pressed) ++stats_.buttonEventsHandled;
        return;
    }
    if (ev.id == button::kMagnifier) {
        // Dispatch via Bindings on both press AND release so Hold-
        // behaviour bindings fire their release edge. No factory
        // default — user binds via Settings → Bindings → UC1.
        // Hardware LED cell is not yet decoded; LedOverride still
        // lights the on-screen mockup so the user gets visual
        // feedback in the editor.
        uf8::bindings::dispatch(
            uf8::bindings::ButtonId::Uc1Magnifier, ev.pressed);
        if (ev.pressed) ++stats_.buttonEventsHandled;
        return;
    }

    // Track-level buttons: act on the press edge; release is a no-op.
    // Solo/Cut target the focused track; Solo Clear is a global unsolo.
    // We push the LED inline (pushButtonLed_, same path Fine uses) to
    // avoid depending on REAPER's SetSurface* callback firing back on
    // the initiating surface. One-shot diag so we can see in the
    // console what state each press computed.
    auto anySolo = []() -> bool {
        const int n = CountTracks(nullptr);
        for (int i = 0; i < n; ++i) {
            if (GetMediaTrackInfo_Value(GetTrack(nullptr, i), "I_SOLO") > 0.5) return true;
        }
        return false;
    };
    static int kDiagSoloCut = 12;
    if (ev.id == button::kSolo) {
        if (ev.pressed && focusedTrack_) {
            MediaTrack* tr = static_cast<MediaTrack*>(focusedTrack_);
            // REC + RME override — dispatch the user-assigned
            // TotalReaper action instead of toggling REAPER solo. LED
            // refresh runs through pollButtonLeds_, which mirrors the
            // P_EXT state under the same gate.
            if (reasixty_dispatchUc1RecRmeButton(2, tr)) {
                ++stats_.buttonEventsHandled;
                return;
            }
            CSurf_OnSoloChange(tr, -1);
            const bool on = GetMediaTrackInfo_Value(tr, "I_SOLO") > 0.5;
            uf8::param_groups::broadcastSoloMute(tr, true, on ? 1 : 0);
            pushButtonLed_(button::kSolo, on);
            pushButtonLed_(button::kSoloClear, anySolo());
            pushButtonReadout_(button::kSolo, "Solo", on ? "On" : "Off",
                               zone::kChannelStripReadout);
            if (kDiagSoloCut > 0) {
                --kDiagSoloCut;
                char line[80];
                snprintf(line, sizeof(line),
                    "UC1 Solo press → solo=%d anySolo=%d\n", on, anySolo());
            }
            ++stats_.buttonEventsHandled;
        }
        return;
    }
    if (ev.id == button::kCut) {
        if (ev.pressed && focusedTrack_) {
            MediaTrack* tr = static_cast<MediaTrack*>(focusedTrack_);
            // REC + RME override — see kSolo branch above. LED tracks
            // the assigned toggle's P_EXT mirror.
            if (reasixty_dispatchUc1RecRmeButton(1, tr)) {
                ++stats_.buttonEventsHandled;
                return;
            }
            CSurf_OnMuteChange(tr, -1);
            const bool on = GetMediaTrackInfo_Value(tr, "B_MUTE") > 0.5;
            uf8::param_groups::broadcastSoloMute(tr, false, on ? 1 : 0);
            pushButtonLed_(button::kCut, on);
            pushButtonReadout_(button::kCut, "Cut", on ? "On" : "Off",
                               zone::kChannelStripReadout);
            if (kDiagSoloCut > 0) {
                --kDiagSoloCut;
                char line[80];
                snprintf(line, sizeof(line),
                    "UC1 Cut press → mute=%d\n", on);
            }
            ++stats_.buttonEventsHandled;
        }
        return;
    }
    if (ev.id == button::kSoloClear) {
        if (ev.pressed) {
            // REAPER action 40340 = "Track: Unsolo all tracks".
            Main_OnCommand(40340, 0);
            pushButtonLed_(button::kSoloClear, anySolo());
            pushButtonReadout_(button::kSoloClear, "Solo Clear",
                               anySolo() ? "On" : "Off",
                               zone::kChannelStripReadout);
            // Every strip's solo LED could have just been turned off,
            // but since we only light Solo for the focused track here,
            // a single refresh on the focused one is enough.
            if (focusedTrack_) {
                const bool on = GetMediaTrackInfo_Value(
                    static_cast<MediaTrack*>(focusedTrack_), "I_SOLO") > 0.5;
                pushButtonLed_(button::kSolo, on);
            }
            if (kDiagSoloCut > 0) {
                --kDiagSoloCut;
                char line[80];
                snprintf(line, sizeof(line),
                    "UC1 SoloClear press → anySolo=%d\n", anySolo());
            }
            ++stats_.buttonEventsHandled;
        }
        return;
    }

    if (!focusedTrack_ || !ev.pressed) {
        // Only act on the press edge — UC1 sends a release right after.
        if (ev.pressed) ++stats_.buttonEventsSuppressed;
        return;
    }

    auto bindings = lookupBindingsOnTrack(focusedTrack_);
    MediaTrack* tr = static_cast<MediaTrack*>(focusedTrack_);
    // BC button targets are routed to the BC anchor (independent of CS
    // focus). For most buttons the CS path stays on focusedTrack_; only
    // the BusCompIn case below needs the anchor.
    void* bcAnchorRaw = effectiveBcTrack_();
    UC1Bindings bcBindings = bcAnchorRaw
        ? ((bcAnchorRaw == focusedTrack_) ? bindings
                                          : lookupBindingsOnTrack(bcAnchorRaw))
        : UC1Bindings{};
    MediaTrack* bcTr = static_cast<MediaTrack*>(bcAnchorRaw);

    // Polarity — toggle REAPER's per-track phase-invert (B_PHASE),
    // not a plugin param. cap17/18 noted this button produces no
    // FF 22 event on at least some firmwares; if it does fire, we
    // want it routed to REAPER track state. LED mirrors B_PHASE
    // regardless (refresh() picks it up on track changes).
    if (ev.id == button::kPolarity) {
        // REC + RME override — dispatch the user-assigned TotalReaper
        // action (default: Phase Invert) on press; swallow release so
        // it doesn't double-toggle.
        if (reasixty_recRmeUc1ButtonAssigned(3)) {
            if (ev.pressed) {
                reasixty_dispatchUc1RecRmeButton(3, tr);
                ++stats_.buttonEventsHandled;
            }
            return;
        }
        const bool cur = GetMediaTrackInfo_Value(tr, "B_PHASE") > 0.5;
        const double phaseNext = cur ? 0.0 : 1.0;
        SetMediaTrackInfo_Value(tr, "B_PHASE", phaseNext);
        uf8::param_groups::broadcastTrackBool(tr, "B_PHASE", phaseNext);
        pushButtonLed_(ev.id, !cur);
        pushButtonReadout_(ev.id, "Polarity", !cur ? "In" : "Out",
                           zone::kChannelStripReadout);
        ++stats_.buttonEventsHandled;
        return;
    }

    // Channel IN — two modes:
    //   * SSL Channel Strip on track: toggle the plugin's internal
    //     "Channel In" switch (found by VST3 param name). This mirrors
    //     the plugin's own IN button, not the global bypass.
    //   * No SSL plugin: fall back to bypassing the first track FX.
    // The toggle itself flips between 0 and 1; the "active" state shown
    // on the LED / readout honours bypassInverted (SSL stock: 1=off;
    // bx_townhouse "Comp In": 1=on).
    auto toggleBypassParam = [&](MediaTrack* targetTr, const PluginBindings* m,
                                 int fxIdx, uf8::Domain domain,
                                 const char* labelLong, int readoutZone) {
        if (!targetTr || !m || m->bypassParam == kParamNone) return false;
        const double cur = TrackFX_GetParamNormalized(targetTr, fxIdx, m->bypassParam);
        const double next = (cur > 0.5) ? 0.0 : 1.0;
        TrackFX_SetParamNormalized(targetTr, fxIdx, m->bypassParam, next);
        // Multi-track sync: translate vst3Param → UF8 link-slot idx and
        // broadcast via the SSL CS/BC PluginMap on each member.
        if (auto uf8Match = uf8::lookupPluginOnTrack(targetTr, domain);
            uf8Match.map)
        {
            const int slotIdx = uf8::slotIdxForVst3Param(
                *uf8Match.map, m->bypassParam);
            if (slotIdx >= 0) {
                uf8::param_groups::broadcastBuiltinSlot(
                    targetTr, domain, slotIdx, next);
            }
        }
        reasixty_bumpFolderReveal(targetTr);
        const bool inActive = m->bypassInverted ? (next >= 0.5) : (next < 0.5);
        pushButtonLed_(ev.id, inActive);
        pushButtonReadout_(ev.id, labelLong,
                           inActive ? "In" : "Out", readoutZone);
        return true;
    };

    if (ev.id == button::kChannelIn) {
        // No SSL CS plug-in on the focused track → no-op. Previous code
        // fell back to toggling the FIRST track FX's bypass, which
        // hijacked unrelated plug-ins (user FR 2026-05-05: must not do
        // that). The Channel-IN button only acts on a recognised CS
        // plug-in's own Bypass param.
        toggleBypassParam(tr, bindings.channelMap, bindings.channelFxIdx,
                          uf8::Domain::ChannelStrip,
                          "Channel Strip", zone::kChannelStripReadout);
        ++stats_.buttonEventsHandled;
        return;
    }
    if (ev.id == button::kBusCompIn) {
        // BC IN targets the BC anchor track, not the CS-focused track.
        toggleBypassParam(bcTr, bcBindings.busCompMap, bcBindings.busCompFxIdx,
                          uf8::Domain::BusComp,
                          "Bus Comp", zone::kBusCompReadout);
        ++stats_.buttonEventsHandled;
        return;
    }

    // Plugin-param toggles (EQ In, Dyn In, Fast Attack, etc.). These
    // live on the Channel Strip plugin.
    if (!bindings.channelMap) { ++stats_.buttonEventsSuppressed; return; }
    const int vst3Param = bindings.channelMap->buttonParam[ev.id];
    if (vst3Param == kParamNone) { ++stats_.buttonEventsSuppressed; return; }

    const double cur = TrackFX_GetParamNormalized(tr, bindings.channelFxIdx, vst3Param);

    // EQ Type on 4K E is a 3-state "EQ Colour" cycle (Brown → Black →
    // Orange → Brown). 4K G's EQ Colour param exposes three normalised
    // positions but only two distinct values (Pink at 0.0, Black at
    // 0.5 / 1.0) — user-confirmed 2026-04-30 that 4K G should behave
    // as a 2-way toggle, matching the UF8 V-Pot path. CS 2's EQ Type
    // and 4K B (no EQ Type) are binary or absent.
    const bool is3StateEqColour = (ev.id == button::kEqType)
        && std::strcmp(bindings.channelMap->shortName, "4K E") == 0;

    double next;
    if (is3StateEqColour) {
        int step = static_cast<int>(cur * 2.0 + 0.5);
        if (step < 0) step = 0;
        if (step > 2) step = 2;
        step = (step + 1) % 3;
        next = step * 0.5;
    } else {
        next = (cur < 0.5) ? 1.0 : 0.0;
    }
    TrackFX_SetParamNormalized(tr, bindings.channelFxIdx, vst3Param, next);
    reasixty_bumpFolderReveal(tr);
    pushButtonLed_(ev.id, next >= 0.5);

    // Project the toggled param onto UF8 so all 8 V-Pots show + control
    // it across the bank — same broadcast model the UC1 knob path uses.
    // Looks up the UF8 PluginMap (separate from uc1::PluginBindings),
    // finds the slot whose vst3Param matches, and sets the focus. UC1
    // zone 0x05 picks this up via pushFocusedParamReadout_ on the next
    // poll tick (and we call it inline so the user sees the new focus
    // immediately rather than after one tick of latency).
    auto uf8Match = uf8::lookupPluginOnTrack(focusedTrack_,
                                             uf8::Domain::ChannelStrip);
    if (uf8Match.map) {
        const int slotIdx = uf8::slotIdxForVst3Param(*uf8Match.map, vst3Param);
        if (slotIdx >= 0) {
            uf8::param_groups::broadcastBuiltinSlot(
                static_cast<MediaTrack*>(focusedTrack_),
                uf8::Domain::ChannelStrip, slotIdx, next);
            uf8::setFocus({uf8::Domain::ChannelStrip, slotIdx});
            pushFocusedParamReadout_();
        }
    }

    // Push the post-toggle readout. Most CS buttons are binary
    // In/Out toggles; EQ Type is the exception (cycles colours/bell
    // shapes). Use the plugin's formatted string when it produces
    // something more descriptive than "0"/"1", otherwise fall back
    // to "In"/"Out".
    auto labelFor = [](uint8_t id) -> const char* {
        switch (id) {
            case button::kHfBell:      return "HF Bell";
            case button::kEqType:      return "EQ Type";
            case button::kEqIn:        return "EQ";
            case button::kLfBell:      return "LF Bell";
            case button::kFastAttComp: return "Fast Attack";
            case button::kPeak:        return "Peak";
            case button::kDynIn:       return "DYN";
            case button::kExpand:      return "Expander";
            case button::kFastAttGate: return "Fast Attack";
            case button::kScListen:    return "S/C Listen";
        }
        return "";
    };
    char fmtBuf[64] = {0};
    std::string valueText;
    if (TrackFX_FormatParamValueNormalized(tr, bindings.channelFxIdx,
                                           vst3Param, next,
                                           fmtBuf, sizeof(fmtBuf))
        && fmtBuf[0])
    {
        valueText = fmtBuf;
        // Plugins commonly format binary params as "0"/"1"; those
        // read better as "Out"/"In" on the LCD.
        if (valueText == "0") valueText = "Out";
        else if (valueText == "1") valueText = "In";
    } else {
        valueText = (next > 0.5) ? "In" : "Out";
    }
    // S/C Listen is an "On/Off" toggle, not "In/Out".
    if (ev.id == button::kScListen) {
        valueText = (next > 0.5) ? "On" : "Off";
    }
    pushButtonReadout_(ev.id, labelFor(ev.id), valueText,
                       zone::kChannelStripReadout);
    ++stats_.buttonEventsHandled;
}

namespace {

// Strip a single space between the numeric part and the unit suffix.
// REAPER's TrackFX_FormatParamValueNormalized returns "12.1 dB",
// "102.5 Hz", "50.0 %", "0.12 s" — SSL 360°'s zone 0x05 format is the
// same without the separator space ("12.1dB", "102.5Hz"). Also folds
// the UTF-8 infinity glyph (E2 88 9E) to ASCII "INF" before any unit
// processing — Comp Ratio at max returns "∞:1", Output Gain at min
// returns "-∞ dB"; without this the Latin-1 LCD renders the UTF-8
// bytes as garbage like "-â**".
std::string compactUnit(std::string_view s)
{
    std::string r{s};
    // ∞ → INF (UTF-8 → ASCII fold).
    for (size_t p = 0; p + 2 < r.size(); ) {
        if (static_cast<unsigned char>(r[p])     == 0xE2 &&
            static_cast<unsigned char>(r[p + 1]) == 0x88 &&
            static_cast<unsigned char>(r[p + 2]) == 0x9E) {
            r.replace(p, 3, "INF");
            p += 3;
        } else {
            ++p;
        }
    }
    static constexpr std::string_view units[] = {
        " dB", " Hz", " kHz", " ms", " s", " %", " :1",
    };
    for (auto u : units) {
        auto p = r.rfind(u);
        if (p == std::string::npos) continue;
        // Only strip the separator space if the char immediately
        // before it is digit/decimal — i.e. a numeric value. For
        // "OFF Hz" the char before the space is 'F', not numeric, so
        // we leave the whole string alone (display will show "OFF").
        if (p == 0) break;
        const char prev = r[p - 1];
        const bool isNumeric = (prev >= '0' && prev <= '9') || prev == '.';
        if (!isNumeric) break;
        r.erase(p, 1);
        break;
    }
    return r;
}

// For non-numeric values like "OFF", "AUTO", "N/A" — drop the unit
// token entirely. SSL 360° shows just "OFF" without " Hz" in that
// case (uc1_07 captured "S/C HPF         OFF Hz" but the OFF plays
// cleaner without the trailing unit).
std::string stripUnitIfNonNumeric(std::string_view s)
{
    static constexpr std::string_view units[] = {
        " dB", " Hz", " kHz", " ms", " s", " %", " :1",
    };
    for (auto u : units) {
        auto p = s.rfind(u);
        if (p == std::string::npos) continue;
        if (p == 0) return std::string{s};
        const char prev = s[p - 1];
        const bool isNumeric = (prev >= '0' && prev <= '9') || prev == '.';
        if (!isNumeric) return std::string{s.substr(0, p)};
    }
    return std::string{s};
}

// Right-pad / left-pad a value string to a fixed width. UC1's numeric
// LCD has physically-spaced digit slots with a gap between the
// leftmost sign/overflow slot and the main digit column. SSL 360°
// sends values in a fixed position so chars always land in expected
// slots; anything shorter gets leading spaces, anything longer
// "overflows" into the sign slot (bad — shows e.g. "1    02.5Hz"
// instead of "102.5Hz"). Forcing 7 chars matches the captured format
// for the widest Bus Comp values ("-20.0dB", "-10.0dB", "102.5Hz").
std::string padValueFixed(std::string_view s, size_t width = 7)
{
    if (s.size() >= width) return std::string{s};
    std::string r(width - s.size(), ' ');
    r.append(s);
    return r;
}

} // namespace

void* UC1Surface::effectiveBcTrack_() const
{
    // Validate that bcAnchorTrack_ still references a live track.
    // REAPER reuses MediaTrack* pointers across project edits, but a
    // deleted track returns IP_TRACKNUMBER < 0; ValidatePtr2 is the
    // canonical "is this still a track?" check.
    if (bcAnchorTrack_ && ValidatePtr2(nullptr, bcAnchorTrack_, "MediaTrack*")) {
        return bcAnchorTrack_;
    }
    // Lazy fallback: first BC-bearing track in the project. Means the
    // BC carousel shows something useful even before the user touches
    // the BC encoder.
    const int n = CountTracks(nullptr);
    for (int i = 0; i < n; ++i) {
        MediaTrack* t = GetTrack(nullptr, i);
        if (lookupBindingsOnTrack(t).busCompMap) return t;
    }
    return nullptr;
}

void UC1Surface::pushButtonReadout_(uint8_t /*buttonId*/, std::string_view label,
                                    std::string_view value, uint8_t zone)
{
    if (!device_) return;
    auto readout = formatReadout(label, value);
    device_->send(buildDisplayText(zone, readout, readout.size()));
}

void UC1Surface::pushFocusedParamReadout_()
{
    if (!device_) return;
    // Only Main mode owns the CS/BC readout zones (0x03/0x05). Menu
    // modes (EXT_FUNCS / PRESETS / ROUTING / TRANSPORT) repurpose the
    // central LCD area and have their own value-field render paths
    // (e.g. renderExtFuncsSubscreen_ writes the live value via
    // buildLcdValue / FF 66 <len> 0E). Without this guard, a chase
    // tick during an EXT_FUNCS adjust kept overwriting the subscreen
    // with the regular readout text — the user saw their value
    // jumping into the wrong LCD region instead of landing in the
    // EXT_FUNCS parameter field.
    if (mode_ != Uc1Mode::Main) return;

    // REC + RME override: when the surface is in Rec/RecMon and RME is
    // on, the CS readout zone shows TotalMix preamp state (48V/Pd/Ph
    // flags + gain dB) for the focused track instead of the focused
    // plug-in param. Mirrors the UF8 V-Pot value line.
    if (focusedTrack_
        && ValidatePtr2(nullptr, focusedTrack_, "MediaTrack*"))
    {
        auto* trMedia = static_cast<MediaTrack*>(focusedTrack_);
        std::string rmeLabel, rmeValue;
        if (reasixty_recUc1ReadoutText(trMedia, &rmeLabel, &rmeValue)) {
            auto readout = formatReadout(rmeLabel, rmeValue);
            if (!csScrollOverlayActive_ && !bcScrollOverlayActive_
                && !instanceCarouselActive_ && !navCarouselActive_
                && readout != lastZone03Text_)
            {
                lastZone03Text_ = readout;
                device_->send(buildReadoutPrecursor(0x00));
                if (!lastLargeTripleFrame_.empty()) {
                    device_->send(std::vector<uint8_t>(lastLargeTripleFrame_));
                }
                device_->send(buildDisplayText(
                    zone::kChannelStripReadout, readout, readout.size()));
                lastZone03Edit_ = std::chrono::steady_clock::now();
            }
            // Owned the CS readout this tick — fall through so BC
            // domain still renders its own focused-param value line.
            // CS-domain focused params are intentionally suppressed
            // while REC+RME owns the zone.
            const auto focused2 = uf8::getFocusedParam();
            if (focused2.domain != uf8::Domain::BusComp) return;
        }
    }

    const auto focused = uf8::getFocusedParam();
    if (focused.domain == uf8::Domain::None) return;

    // Plug-in lookup track: prefer the touched-FX track (the one
    // chase last resolved a Link slot on), else fall back to UC1's
    // UC1 always renders the SELECTED track. A V-Pot edit on a non-selected
    // track (Frank 2026-05-07) was previously hijacking UC1's readout and
    // colour bar via g_focusedFxTrack; the Settings toggle "Track selection
    // follows parameter change" handles that case explicitly by re-selecting
    // the manipulated track, so UC1 here just mirrors focusedTrack_.
    //
    // ValidatePtr2 guards against stale pointers across project switch /
    // track delete (crash 2026-05-05 17:18, PC tag pattern of a freed
    // MediaTrack — lookupPluginOnTrack → TrackFX_GetCount segfaulted).
    // CS focus reads from the selected track; BC focus reads from the
    // BC anchor (Encoder 2's target). Earlier this used focusedTrack_
    // for BOTH domains, so a BC knob edit (which writes to the BC
    // anchor) left the readout pinned to focusedTrack_'s BC plug-in
    // value — same text every tick, dedup-skipped, LCD frozen.
    // Captured via Frank 2026-05-12: "UC1 BC Parameter change wird
    // nicht mehr geupdated".
    void* lookupTrack = (focused.domain == uf8::Domain::BusComp)
        ? effectiveBcTrack_()
        : focusedTrack_;
    if (lookupTrack && !ValidatePtr2(nullptr, lookupTrack, "MediaTrack*")) {
        lookupTrack = nullptr;
    }
    if (!lookupTrack) return;

    auto match = uf8::lookupPluginOnTrack(lookupTrack, focused.domain);
    if (!match.map) return;
    const uf8::LinkSlot* slotPtr = uf8::findSlotByLinkIdx(*match.map,
                                                          focused.slotIdx);
    if (!slotPtr) return;

    const auto& slot = *slotPtr;
    // Suppress the readout when focus has landed on the plug-in's
    // Bypass slot. By convention every PluginMap has Bypass at
    // linkIdx 0; the "default focus after an Instance Cycle / cursor
    // focus / show_focused_plugin_gui" paths in main.cpp use
    // setFocus({domain, 0}) which resolves here as Bypass and made
    // the LCD show "Bypass OFF" whenever the user hadn't yet touched
    // a knob since opening / cycling the plug-in (Frank 2026-05-19).
    // Bypass state is already visible on the CS IN / BC IN button LED;
    // dropping the LCD line removes the false-positive idle display.
    if (slot.linkIdx == 0) return;
    MediaTrack* tr = static_cast<MediaTrack*>(lookupTrack);
    char formatted[64] = {};
    const double cur = TrackFX_GetParamNormalized(tr, match.fxIndex,
                                                  slot.vst3Param);
    TrackFX_FormatParamValueNormalized(tr, match.fxIndex, slot.vst3Param,
                                       cur, formatted, sizeof(formatted));
    std::string value = stripUnitIfNonNumeric(compactUnit(formatted));
    auto readout = formatReadout(slot.name, value);

    // Per-domain LCD zone routing. CS → zone 0x03 (Channel Strip
    // readout area), BC → zone 0x05 (Bus Comp readout area). Per
    // captures uc1_04 + uc1_15 SSL 360° drives them domain-correctly.
    const uint8_t zoneByte = (focused.domain == uf8::Domain::BusComp)
                           ? zone::kBusCompReadout
                           : zone::kChannelStripReadout;
    // Encoder-scroll overlay gates. While the CS / BC channel-encoder
    // scroll overlay is active, suppress the matching readout zone so
    // the carousel keeps the LCD area to itself. BC scroll also gates
    // the CS zone because sub=0x02 layout hands the upper LCD area to
    // the BC carousel — a stale CS readout there paints on top
    // (Frank 2026-05-12 "CS parameter ist immer noch dort").
    if (zoneByte == zone::kChannelStripReadout
        && (csScrollOverlayActive_ || bcScrollOverlayActive_
            || instanceCarouselActive_ || navCarouselActive_)) return;
    if (zoneByte == zone::kBusCompReadout
        && (bcScrollOverlayActive_ || instanceCarouselActive_
            || navCarouselActive_)) return;
    std::string& cache = (zoneByte == zone::kBusCompReadout)
                       ? lastZone05Text_ : lastZone03Text_;
    if (readout == cache) return;
    cache = readout;

    // SSL 360° readout-update burst (uc1_04 / uc1_15 / uc1_47):
    // precursor (flag=0x00) → LARGE triple → text. uc1_47 capture
    // confirms flag=0x00 for both CS-knob and BC-knob value updates;
    // flag=0x02 is only emitted during Encoder-2 (BC anchor scroll)
    // detents and is handled separately in the encoder path.
    device_->send(buildReadoutPrecursor(0x00));
    if (!lastLargeTripleFrame_.empty()) {
        device_->send(std::vector<uint8_t>(lastLargeTripleFrame_));
    }
    device_->send(buildDisplayText(zoneByte, readout, readout.size()));

    // Stamp this zone's edit time. poll()'s timeout check uses it to
    // fire the SSL 360° "release LCD layout slot" invalidate ~3s after
    // the last value-change (matches uc1_46 capture's t=44 burst).
    auto& editTime = (zoneByte == zone::kBusCompReadout)
                   ? lastZone05Edit_ : lastZone03Edit_;
    editTime = std::chrono::steady_clock::now();

    // Release the OPPOSITE readout zone immediately on a domain switch
    // (CS-edit ↔ BC-edit) so two parameter lines don't sit on the LCD
    // at once. Use the SSL 360° invalidate frame, not blank-text — the
    // invalidate releases the layout slot, blank text leaves it active.
    const uint8_t otherZone = (zoneByte == zone::kBusCompReadout)
                            ? zone::kChannelStripReadout : zone::kBusCompReadout;
    std::string& otherCache = (zoneByte == zone::kBusCompReadout)
                            ? lastZone03Text_ : lastZone05Text_;
    if (!otherCache.empty()) {
        otherCache.clear();
        device_->send(buildDisplayInvalidate(otherZone));
    }

    // Colour bar tracks the SELECTED track via refresh()'s focusedTrack_
    // path — no override here. The previous "follow last touched FX" logic
    // bled non-selected V-Pot edits into UC1's display; the Settings option
    // "Track selection follows parameter change" handles the auto-select
    // case so this surface always mirrors selection.
}

void UC1Surface::renderExtFuncsSubscreen_()
{
    if (!device_ || mode_ != Uc1Mode::ExtFuncs) return;
    // Wrap idx into bounds — defensive against drift.
    int idx = extFuncsIdx_;
    if (idx < 0) idx = 0;
    if (idx >= kExtFuncsCount) idx = kExtFuncsCount - 1;
    extFuncsIdx_ = idx;
    const auto& cur = kExtFuncs[idx];
    const auto& prev = kExtFuncs[(idx - 1 + kExtFuncsCount) % kExtFuncsCount];
    const auto& next = kExtFuncs[(idx + 1) % kExtFuncsCount];
    // SSL360 re-sends the banner at the start of every scroll-step
    // frame burst (uc1_37). Acts as a redraw signal — without it
    // the firmware sometimes ignores the trailing indicator frames.
    device_->send(buildCentralMode(CentralMode::ExtFuncs));
    device_->send(buildLcdHeader(cur.longLabel));
    device_->send(buildTrackNameTripleLarge(prev.shortLabel,
        cur.shortLabel, next.shortLabel));
    // Read the current item's parameter value via PluginMap slot
    // lookup → TrackFX_FormatParamValueNormalized. Display in the
    // value field (FF 66 <len> 0E). Skip silently if no plug-in /
    // entry isn't on the focused plug-in.
    //
    // buildLcdRoundIndicator(v) is sent even though the yellow arc
    // doesn't visibly paint — empirically the firmware uses it as a
    // commit/repaint trigger for the bottom LCD region. Without it,
    // the value text caches and stops updating on subsequent encoder
    // detents (only the very first value-frame paints).
    if (focusedTrack_) {
        auto match = uf8::lookupPluginOnTrack(focusedTrack_,
                                              uf8::Domain::ChannelStrip);
        if (match.map) {
            for (const auto& s : match.map->slots) {
                if (s.id && std::strcmp(s.id, cur.slotId) == 0
                         && s.vst3Param >= 0) {
                    char buf[64] = {};
                    const double v = TrackFX_GetParamNormalized(
                        static_cast<MediaTrack*>(focusedTrack_),
                        match.fxIndex, s.vst3Param);
                    TrackFX_FormatParamValueNormalized(
                        static_cast<MediaTrack*>(focusedTrack_),
                        match.fxIndex, s.vst3Param, v, buf, sizeof(buf));
                    std::string val{buf};
                    while (!val.empty() && val.front() == ' ') val.erase(0, 1);
                    const size_t sp = val.find(' ');
                    if (sp != std::string::npos) val.erase(sp);
                    if (val.size() > 8) val.resize(8);
                    device_->send(buildLcdValue(val));
                    device_->send(buildLcdUnit(""));
                    device_->send(buildLcdRoundIndicator(v));
                    break;
                }
            }
        }
    }
    // Commit (FF 66 02 09 <flag>) is NOT sent per scroll step —
    // SSL360 only emits it on push toggles to flip the green-name
    // active flag (uc1_37 vs uc1_39 difference). The Sec-Encoder
    // push handler in handleButton_ owns the commit.
}

void UC1Surface::renderPresetsSubscreen_()
{
    if (!device_ || mode_ != Uc1Mode::Presets) return;
    if (presetsSub_ == PresetsSubMode::Selector) {
        // Selector subscreen — 4 frames per uc1_38 t=0.6:
        //   header "PRESETS" + 3-slot triple + commit.
        device_->send(buildLcdHeader("PRESETS"));
        if (presetsSelectCs_) {
            device_->send(buildTrackNameTripleLarge("",
                "CHANNEL STRIP", "BUS COMP"));
        } else {
            device_->send(buildTrackNameTripleLarge("",
                "BUS COMP", "CHANNEL STRIP"));
        }
        device_->send(buildMenuCommit());
        return;
    }
    // Browse subscreen — 6 frames per uc1_38 t=8.477:
    //   banner 0x02 + main header (domain) + 5-slot list + sub-header
    //   "PRESETS" + indicator 0x08 + commit 0x09.
    device_->send(buildCentralMode(CentralMode::PresetsSub));
    // Manual writes the domain name with a trailing space ("CHANNEL
    // STRIP " / "BUS COMP ") in capture — match exactly so firmware
    // recognises the layout.
    device_->send(buildLcdHeader(presetsSelectCs_
        ? "CHANNEL STRIP " : "BUS COMP "));
    auto sendListBody = [&](std::string_view prev2, std::string_view prev1,
                            std::string_view curr, std::string_view next1,
                            std::string_view next2) {
        device_->send(buildPresetListScroll(prev2, prev1, curr, next1, next2));
        device_->send(buildLcdSubHeader("PRESETS"));
        device_->send(buildMenuIndicator08());
        device_->send(buildMenuCommit());
    };
    if (!focusedTrack_) {
        sendListBody("", "", "<no track>", "", "");
        return;
    }
    auto match = uf8::lookupPluginOnTrack(
        focusedTrack_,
        presetsSelectCs_ ? uf8::Domain::ChannelStrip
                         : uf8::Domain::BusComp);
    if (!match.map) {
        sendListBody("", "", "<no plug-in>", "", "");
        return;
    }
    char name[128] = {};
    TrackFX_GetPreset(static_cast<MediaTrack*>(focusedTrack_),
                      match.fxIndex, name, sizeof(name));
    int numPresets = 0;
    const int curIdx = TrackFX_GetPresetIndex(
        static_cast<MediaTrack*>(focusedTrack_), match.fxIndex, &numPresets);
    // REAPER's preset API doesn't expose names without navigating, so
    // adjacent slots show "..." when more presets exist in that
    // direction. Visually less rich than SSL360's full prev/next
    // names but functional and avoids a destructive walk-and-restore.
    const std::string prev = (curIdx > 0) ? "..." : "";
    const std::string next = (curIdx + 1 < numPresets) ? "..." : "";
    sendListBody("", prev,
        name[0] ? std::string{name} : std::string{"<no name>"},
        next, "");
}

void UC1Surface::pushKnobReadout_(uint8_t knobId, void* trackRaw, int fxIdx,
                                  int vst3Param, uint8_t zone,
                                  std::string_view label)
{
    if (!device_) return;

    MediaTrack* tr = static_cast<MediaTrack*>(trackRaw);
    char formatted[64];
    formatted[0] = '\0';
    const double cur = TrackFX_GetParamNormalized(tr, fxIdx, vst3Param);
    TrackFX_FormatParamValueNormalized(tr, fxIdx, vst3Param, cur,
                                       formatted, sizeof(formatted));
    // UC1 expects compact "X.XdB" / "X.XHz" / "X%". Non-numeric values
    // ("OFF", "AUTO", "N/A") drop the unit entirely. No padding — the
    // readout builder anchors the value at position 16 regardless of
    // length, which is what SSL 360° does in captures.
    std::string value = stripUnitIfNonNumeric(compactUnit(formatted));

    auto readout = formatReadout(label, value);

    // Diag — log the final 22-char string for the first N events per
    // knob ID. Use '·' placeholder for space so we can count columns
    // exactly. Helps diagnose value-alignment problems in zone 0x05/03.
    static int kReadoutDebugRemaining[0x20] = {
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    };
    if (knobId < 0x20 && kReadoutDebugRemaining[knobId] > 0) {
        --kReadoutDebugRemaining[knobId];
        std::string vis = readout;
        for (auto& c : vis) if (c == ' ') c = '.';
        char line[64];
        snprintf(line, sizeof(line),
            "UC1 push zone=0x%02x raw='%s' → '%s'\n",
            zone, formatted, vis.c_str());
    }

    // Variable-width readout — 22 for 6-char values, 23 for 7-char.
    // UC1 accepts either; what matters is that value digits land at
    // position 16 onwards.
    device_->send(buildDisplayText(zone, readout, readout.size()));
}

namespace {
// Pot LED ring cell maps. ALL UC1 pot rings render as a single moving
// LED dot — the "Kerbe" of the analog knob. No fill/trace, regardless
// of the underlying parameter (gain, frequency, threshold, etc.). This
// is what SSL 360° does natively for the FREQ + Q knobs and what we
// want for every pot.
//
// Cell map source: uc1_15_knob_channelstrip_sweep.pcapng (full CS+Dyn
// sweep, 22 knobs), dual_40_bc_pots.pcapng + uc1_04..07 (BC pots).
// Per-knob attribution by correlating EP 0x81 IN knob events
// (`FF 24 02 <id> <delta>`) with EP 0x02 OUT LED writes, midpoint-
// bucketed by knob-id transitions.
// Two render modes for the LED ring:
//   Position — single bright dot at the LED nearest the value (default).
//   Gradient — single dot on bank 0x01, with bank 0x02 brightness fading
//              over 3 steps {0xFF, 0x4C, 0x19} to neighbouring cells.
//              Used by the bipolar pots whose physical LEDs visibly fade
//              between positions: all 4 EQ Gains, Comp Threshold, Gate
//              Threshold, BC Threshold/Makeup/Mix, Input/Output Gain,
//              Comp Ratio. Decoded from uc1_28/29/31/32 captures
//              showing bank-0x02 states {0x00, 0x19, 0x4C, 0xFF}.
enum class RingMode : uint8_t { Position = 0, Gradient = 1 };
// Most rings use a uniform byte5 (the section "role" byte) for every
// cell. A few have a quirk LED whose byte5 differs — e.g. BC Release
// LED 7/7 lives at cell 0x00 byte5=0x01 (decoded from uc1_34 sweep)
// because cell 0x00 byte5=0x00 is taken by the hundreds 7-seg
// segment 'a'. `b5Override` is an optional parallel array — when
// non-null, b5Override[i] supersedes the knob-default byte5 for
// cell i. Keep it null for rings without quirks.
struct RingDef {
    const uint8_t* cells;
    int            nCells;
    RingMode       mode = RingMode::Position;
    const uint8_t* b5Override = nullptr;
};

// ---- EQ section (12 knobs) ----
// All rings contiguous 11 cells. Earlier guess that the centre cell of
// each Gain pot was a separately-driven 0-dB indicator was wrong — the
// user reported the 12 o'clock LED missing on all four Gains after we
// excluded those cells. The capture happened to miss writes to those
// cells (sweep timing artefact), but they ARE part of the ring.
constexpr uint8_t kLpfCells[]    = {0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F};
constexpr uint8_t kHpfCells[]    = {0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94};
constexpr uint8_t kHfGainCells[] = {0x7E,0x7F,0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88};
constexpr uint8_t kHfFreqCells[] = {0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x7B,0x7C,0x7D};
constexpr uint8_t kHmfGainCells[]= {0x68,0x69,0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,0x70,0x71,0x72};
constexpr uint8_t kHmfFreqCells[]= {0x5D,0x5E,0x5F,0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67};
constexpr uint8_t kHmfQCells[]   = {0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0x5B,0x5C};
constexpr uint8_t kLmfGainCells[]= {0x45,0x46,0x47,0x48,0x49,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F};
constexpr uint8_t kLmfFreqCells[]= {0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,0x40,0x41,0x42,0x43,0x44};
constexpr uint8_t kLmfQCells[]   = {0x2F,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39};
constexpr uint8_t kLfFreqCells[] = {0x24,0x25,0x26,0x27,0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x2E};
constexpr uint8_t kLfGainCells[] = {0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x20,0x21,0x22};

// ---- Channel Strip I/O (Input Trim + Fader Level / Output Gain) ----
// uc1_32: Input Trim — 11 contiguous cells, 3-step brightness, byte5=0x00.
constexpr uint8_t kInputTrimCells[]  = {0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA};
// Fader Level / Output Gain — knob 0x16, byte5=0x01. Confirmed in
// Output Gain / Fader Level — full 11-LED ring per user 2026-05-01.
// First-revision map (0x0C..0x16) was off-by-one: 0x0C belongs to
// BC Mix LED 11/11 (BC Mix's high-address neighbour, byte5=0x01).
// Real Output Gain range is 0x0D..0x17. The 7-seg digit cells at
// 0x10..0x16 are byte5=0x00 — independent address space, no collision.
constexpr uint8_t kFaderLevelCells[] = {0x0D,0x0E,0x0F,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17};

// ---- Dyn / Gate section (7 knobs) ----
constexpr uint8_t kGateReleaseCells[]   = {0x7C,0x7D,0x7E,0x7F,0x80,0x81,0x82,0x83,0x84,0x85,0x86};
constexpr uint8_t kGateHoldCells[]      = {0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91};
// 11-LED ring (user 2026-05-01) — earlier 10-cell map missed the CCW LED.
constexpr uint8_t kGateThresholdCells[] = {0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x7B};
// Gate Range = 11-cell ring 0x66..0x70. The 5 cells BELOW the ring
// (0x61..0x65) are the **Gate GR meter** — the right-hand of the two
// 5-LED GR strips on the Channel Strip Dynamics section (Comp GR is
// the left strip at 0x5C..0x60). Earlier guess that the ring was
// 0x61..0x6B was wrong — the dot bled into the Gate GR strip at
// CCW positions because we were addressing GR cells as ring cells.
constexpr uint8_t kGateRangeCells[]     = {0x66,0x67,0x68,0x69,0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,0x70};
constexpr uint8_t kCompReleaseCells[]   = {0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A};
// 11-LED rings (user 2026-05-01) — earlier 10-cell maps missed the CCW LED.
constexpr uint8_t kCompThresholdCells[] = {0x45,0x46,0x47,0x48,0x49,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F};
constexpr uint8_t kCompRatioCells[]     = {0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,0x40,0x41,0x42,0x43,0x44};

// ---- Bus Comp section (7 knobs) ----
// Cell maps confirmed in uc1_31 (2026-04-26). All BC pots use byte5=0x00
// EXCEPT BC Mix which uses byte5=0x01 (a quirk — Mix shares the address
// space with Dyn/Gate rings rather than the rest of BC).
constexpr uint8_t kBcRatioCells[]    = {0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC};
constexpr uint8_t kBcScHpfCells[]    = {0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5};
constexpr uint8_t kBcAttackCells[]   = {0xDD,0xDE,0xDF,0xE0,0xE1,0xE2,0xE3};
// BC Release: 7 LEDs. First 6 at byte5=0x00 (0xFA..0xFF). LED 7/7
// (Auto position, full-CW) lives at cell 0x00 byte5=0x01 — quirk
// decoded from uc1_34_bc_release_sweep at t=4.08s. Cell 0x00 at
// byte5=0x00 is the hundreds 7-seg segment 'a', so SSL360 routes
// the wrap LED through the byte5=0x01 address space instead.
constexpr uint8_t kBcReleaseCells[]      = {0xFA,0xFB,0xFC,0xFD,0xFE,0xFF,0x00};
constexpr uint8_t kBcReleaseB5Override[] = {0x00,0x00,0x00,0x00,0x00,0x00,0x01};
// BC Threshold: 11 LEDs (user 2026-05-01) — extend CW past previous 10-cell map.
constexpr uint8_t kBcThresholdCells[]= {0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE};
// BC Makeup: 11 LEDs (user 2026-05-01) — extend CCW past previous 10-cell map.
constexpr uint8_t kBcMakeupCells[]   = {0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9};
// BC Mix — byte5=0x01, 11 LEDs (user 2026-05-01); previous 9-cell map
// missed both the CCW and CW LEDs. 0x0C is BC Mix LED 11/11 (Output
// Gain map starts at 0x0D right above it).
constexpr uint8_t kBcMixCells[]      = {0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C};

const RingDef* ringFor(uint8_t knobId)
{
    constexpr auto P = RingMode::Position;
    constexpr auto G = RingMode::Gradient;
    static const RingDef kLpf       {kLpfCells,        11, P};
    static const RingDef kHpf       {kHpfCells,        11, P};
    static const RingDef kHfGain    {kHfGainCells,     11, G};
    static const RingDef kHfFreq    {kHfFreqCells,     11, P};
    static const RingDef kHmfGain   {kHmfGainCells,    11, G};
    static const RingDef kHmfFreq   {kHmfFreqCells,    11, P};
    static const RingDef kHmfQ      {kHmfQCells,       11, P};
    static const RingDef kLmfGain   {kLmfGainCells,    11, G};
    static const RingDef kLmfFreq   {kLmfFreqCells,    11, P};
    static const RingDef kLmfQ      {kLmfQCells,       11, P};
    static const RingDef kLfFreq    {kLfFreqCells,     11, P};
    static const RingDef kLfGain    {kLfGainCells,     11, G};
    static const RingDef kInputTrim {kInputTrimCells,  11, G};
    static const RingDef kFaderLevel{kFaderLevelCells, 11, G};
    static const RingDef kGateRelease  {kGateReleaseCells,   11, P};
    static const RingDef kGateHold     {kGateHoldCells,      11, P};
    static const RingDef kGateThr      {kGateThresholdCells, 11, G};
    static const RingDef kGateRange    {kGateRangeCells,     11, P};
    static const RingDef kCompRelease  {kCompReleaseCells,   11, P};
    static const RingDef kCompThr      {kCompThresholdCells, 11, G};
    static const RingDef kCompRatio    {kCompRatioCells,     11, G};
    static const RingDef kBcRatio   {kBcRatioCells,     7, P};
    static const RingDef kBcScHpf   {kBcScHpfCells,    11, P};
    static const RingDef kBcAttack  {kBcAttackCells,    7, P};
    static const RingDef kBcRelease {kBcReleaseCells,   7, P, kBcReleaseB5Override};
    static const RingDef kBcThr     {kBcThresholdCells,11, G};
    static const RingDef kBcMakeup  {kBcMakeupCells,   11, G};
    static const RingDef kBcMix     {kBcMixCells,      11, G};

    switch (knobId) {
        // EQ
        case knob::kCSLowPass:    return &kLpf;
        case knob::kCSHighPass:   return &kHpf;
        case knob::kCSHfGain:     return &kHfGain;
        case knob::kCSHfFreq:     return &kHfFreq;
        case knob::kCSHmfGain:    return &kHmfGain;
        case knob::kCSHmfFreq:    return &kHmfFreq;
        case knob::kCSHmfQ:       return &kHmfQ;
        case knob::kCSLmfGain:    return &kLmfGain;
        case knob::kCSLmfFreq:    return &kLmfFreq;
        case knob::kCSLmfQ:       return &kLmfQ;
        case knob::kCSLfFreq:     return &kLfFreq;
        case knob::kCSLfGain:     return &kLfGain;
        // Channel Strip I/O
        case knob::kCSInputTrim:      return &kInputTrim;
        case knob::kCSFaderLevel:     return &kFaderLevel;
        // Dyn / Gate
        case knob::kCSGateRelease:    return &kGateRelease;
        case knob::kCSGateHold:       return &kGateHold;
        case knob::kCSGateThreshold:  return &kGateThr;
        case knob::kCSGateRange:      return &kGateRange;
        case knob::kCSCompRelease:    return &kCompRelease;
        case knob::kCSCompThreshold:  return &kCompThr;
        case knob::kCSCompRatio:      return &kCompRatio;
        // Bus Comp
        case knob::kBCRatio:          return &kBcRatio;
        case knob::kBCScHpf:          return &kBcScHpf;
        case knob::kBCAttack:         return &kBcAttack;
        case knob::kBCRelease:        return &kBcRelease;
        case knob::kBCThreshold:      return &kBcThr;
        case knob::kBCMakeup:         return &kBcMakeup;
        case knob::kBCMix:            return &kBcMix;
    }
    return nullptr;
}
} // namespace

UC1Surface::CascadeState UC1Surface::computeCascade_(
    void* csRaw, const UC1Bindings& csBindings,
    void* bcRaw, const UC1Bindings& bcBindings)
{
    CascadeState s{};
    MediaTrack* csTr = static_cast<MediaTrack*>(csRaw);
    MediaTrack* bcTr = static_cast<MediaTrack*>(bcRaw);
    auto readBypass = [](MediaTrack* t, const PluginBindings* m, int fxIdx) -> bool {
        return readPluginBypass_(t, m, fxIdx);
    };
    s.csBypassed = readBypass(csTr, csBindings.channelMap, csBindings.channelFxIdx);
    s.bcBypassed = readBypass(bcTr, bcBindings.busCompMap, bcBindings.busCompFxIdx);
    if (csTr && csBindings.channelMap) {
        const int eqInP = csBindings.channelMap->buttonParam[button::kEqIn];
        if (eqInP != kParamNone) {
            s.eqOff = TrackFX_GetParamNormalized(
                csTr, csBindings.channelFxIdx, eqInP) < 0.5;
        }
        const int dynP = csBindings.channelMap->buttonParam[button::kDynIn];
        if (dynP != kParamNone) {
            s.dynOff = TrackFX_GetParamNormalized(
                csTr, csBindings.channelFxIdx, dynP) < 0.5;
        }
    }
    return s;
}

bool UC1Surface::buttonCascadeDim_(uint8_t btn, const CascadeState& s) const
{
    // Bypass-toggle buttons display their own state — never cascade-dim.
    if (btn == button::kEqIn       || btn == button::kDynIn
     || btn == button::kChannelIn  || btn == button::kBusCompIn) return false;
    // Track / surface buttons aren't plug-in controls — exempt.
    if (btn == button::kSolo       || btn == button::kCut
     || btn == button::kSoloClear  || btn == button::kPolarity
     || btn == button::kFine) return false;
    // EQ subsection: HF/LF Bell + EQ Type
    if (btn == button::kHfBell || btn == button::kEqType
     || btn == button::kLfBell) {
        return s.csBypassed || s.eqOff;
    }
    // DYN subsection: Comp / Gate fast-attack + Peak + Expand + ScListen
    if (btn == button::kFastAttComp || btn == button::kPeak
     || btn == button::kExpand      || btn == button::kFastAttGate
     || btn == button::kScListen) {
        return s.csBypassed || s.dynOff;
    }
    return false;
}

bool UC1Surface::knobCascadeDim_(uint8_t knobId, const CascadeState& s) const
{
    using namespace knob;
    // BC pots (0x0E..0x14: Ratio/ScHpf/Attack/Release/Threshold/Makeup/Mix)
    if (knobId >= kBCRatio && knobId <= kBCMix) return s.bcBypassed;
    // EQ knobs (0x00..0x0B: LP/HP + 4 EQ bands × 3 params)
    if (knobId <= kCSLfGain) return s.csBypassed || s.eqOff;
    // DYN knobs (0x17..0x1D: Gate Release..Comp Ratio)
    if (knobId >= kCSGateRelease && knobId <= kCSCompRatio) {
        return s.csBypassed || s.dynOff;
    }
    // Channel knobs that are still CS plug-in params: Input Trim, Fader.
    if (knobId == kCSInputTrim || knobId == kCSFaderLevel) return s.csBypassed;
    return false;
}

void UC1Surface::pushKnobRing_(uint8_t knobId, double normalized, bool dim)
{
    if (!device_) return;
    const RingDef* def = ringFor(knobId);
    if (!def) return;  // knob not yet mapped — no LED update

    if (normalized < 0.0) normalized = 0.0;
    if (normalized > 1.0) normalized = 1.0;

    // Per-knob state cache so we only push cells that changed.
    // Each entry packs sel | (brightness << 8) so we dedup BOTH
    // banks together. 0xFFFF = sentinel "unset", forces a write
    // since neither sel=0/1 nor any real brightness can match.
    auto& last = ringCellCache_[knobId];
    if (static_cast<int>(last.size()) != def->nCells) {
        last.assign(def->nCells, 0xFFFF);
    }

    // Dim mode for the section-bypass cascade: replace 0xFF brightness
    // with 0x33 so the dot is visibly half-bright. Gradient sub-steps
    // (0x19, 0x4C) stay as-is — they're already faded.
    const uint8_t brFull = dim ? 0x33 : 0xFF;

    int idx = 0;
    std::vector<uint8_t> target(def->nCells, 0);
    std::vector<uint8_t> brTarget(def->nCells, 0);

    if (def->mode == RingMode::Gradient) {
        // x3 resolution: each LED-to-LED transition has 3 visual
        // sub-steps. The current LED stays FULL while the next LED
        // (in the direction of rotation) fades up through 0x19 →
        // 0x4C → 0xFF, then the dot snaps to that LED and the cycle
        // repeats. 11 LEDs × 3 sub-steps = 31 distinct visual
        // positions.
        const int subMax = (def->nCells - 1) * 3;
        int k = static_cast<int>(normalized * subMax + 0.5);
        if (k < 0) k = 0;
        if (k > subMax) k = subMax;
        const int mainCell = k / 3;
        const int frac     = k % 3;
        idx = mainCell;
        target[mainCell] = 1;
        brTarget[mainCell] = brFull;
        if (frac > 0 && mainCell + 1 < def->nCells) {
            // 1 → 0x19, 2 → 0x4C
            brTarget[mainCell + 1] = (frac == 1) ? 0x19 : 0x4C;
        }
    } else {
        // Position-only: continuous f rounded to nearest LED.
        const double f = normalized * (def->nCells - 1);
        idx = static_cast<int>(f + 0.5);
        if (idx < 0) idx = 0;
        if (idx >= def->nCells) idx = def->nCells - 1;
        target[idx] = 1;
        brTarget[idx] = brFull;
    }

    // Dual-bank encoding per cell. Bank 0x01 = selection 0/1, bank 0x02
    // = brightness 0/FF.
    //
    // **byte5 (the "role" byte) is section-dependent**, decoded from
    // SSL360 captures uc1_28 + uc1_29 + dual_40:
    //   - EQ pots (knobs 0x00..0x0B) → byte5 = 0x00
    //   - Input Trim (0x0C) → byte5 = 0x00
    //   - BC pots (knobs 0x0E..0x13) → byte5 = 0x00
    //   - BC Mix (knob 0x14) → byte5 = 0x01 (quirk — Mix is in the
    //     same address space as Dyn/Gate, not the rest of BC)
    //   - Fader Level / Output Gain (knob 0x16) → byte5 = 0x01
    //   - Dyn/Gate pots (knobs 0x17..0x1D) → byte5 = 0x01
    // The two byte5 values address distinct LED groups on bank 0x01/0x02
    // — they are NOT the same physical LEDs, even when cell numbers
    // overlap. Writing the wrong byte5 hits a non-displayed register.
    const uint8_t b5Default = (knobId == knob::kBCMix
                        || knobId == knob::kCSFaderLevel
                        || (knobId >= 0x17 && knobId <= 0x1D)) ? 0x01 : 0x00;
    auto make = [](uint8_t bank, uint8_t cell, uint8_t b5, uint8_t state) {
        std::vector<uint8_t> f;
        f.reserve(8);
        f.push_back(0xFF);
        f.push_back(0x13);
        f.push_back(0x04);
        f.push_back(bank);
        f.push_back(cell);
        f.push_back(b5);
        f.push_back(state);
        uint32_t sum = 0;
        for (size_t k = 1; k < f.size(); ++k) sum += f[k];
        f.push_back(static_cast<uint8_t>(sum & 0xFF));
        return f;
    };
    // Dedup both selection AND brightness via the packed cache key.
    // Cells unchanged across both banks skip both writes — a 22-frame
    // Gradient-mode push collapses to ~2-4 frames per knob tick once
    // the dot has moved one cell.
    for (int i = 0; i < def->nCells; ++i) {
        const uint8_t selState = target[i] ? 0x01 : 0x00;
        const uint16_t want = static_cast<uint16_t>(selState)
                            | (static_cast<uint16_t>(brTarget[i]) << 8);
        if (last[i] == want) continue;
        last[i] = want;
        const uint8_t cell = def->cells[i];
        // Per-cell byte5 override for quirks (e.g. BC Release LED 7/7).
        const uint8_t b5 = def->b5Override ? def->b5Override[i] : b5Default;
        device_->send(make(0x01, cell, b5, selState));
        device_->send(make(0x02, cell, b5, brTarget[i]));
    }
}

void UC1Surface::clearKnobRing_(uint8_t knobId)
{
    // Force every cell of this knob's ring to fully dark — sel=0x00,
    // brightness=0x00 on both banks. Used when the focused track has no
    // CS plug-in (or BC anchor lost) so EQ/DYN/BC sections don't leave
    // stale dots glowing on the previous track's value.
    if (!device_) return;
    const RingDef* def = ringFor(knobId);
    if (!def) return;
    auto& last = ringCellCache_[knobId];
    if (static_cast<int>(last.size()) != def->nCells) {
        last.assign(def->nCells, 0xFFFF);
    }
    const uint8_t b5Default = (knobId == knob::kBCMix
                        || knobId == knob::kCSFaderLevel
                        || (knobId >= 0x17 && knobId <= 0x1D)) ? 0x01 : 0x00;
    auto make = [](uint8_t bank, uint8_t cell, uint8_t b5, uint8_t state) {
        std::vector<uint8_t> f;
        f.reserve(8);
        f.push_back(0xFF); f.push_back(0x13); f.push_back(0x04);
        f.push_back(bank); f.push_back(cell);  f.push_back(b5);
        f.push_back(state);
        uint32_t sum = 0;
        for (size_t k = 1; k < f.size(); ++k) sum += f[k];
        f.push_back(static_cast<uint8_t>(sum & 0xFF));
        return f;
    };
    for (int i = 0; i < def->nCells; ++i) {
        constexpr uint16_t want = 0x0000;  // sel=0, brightness=0
        if (last[i] == want) continue;
        last[i] = want;
        const uint8_t cell = def->cells[i];
        const uint8_t b5 = def->b5Override ? def->b5Override[i] : b5Default;
        device_->send(make(0x01, cell, b5, 0x00));
        device_->send(make(0x02, cell, b5, 0x00));
    }
}

void UC1Surface::pushButtonLed_(uint8_t buttonId, LedState state)
{
    if (!device_) return;
    auto cell = cellForButton(buttonId);
    if (cell.bank == 0 && cell.cell == 0) return;  // unmapped

    const bool on = (state != LedState::Off);
    const bool dim = (state == LedState::Dim);

    // Dedup packs the tri-state: 0 = off, 1 = on-bright, 2 = on-dim.
    // pollButtonLeds_ runs every tick so this fires constantly with
    // unchanged values; skip the USB write when nothing changed.
    // invalidateCache() and setFocusedTrack() clear lastButtonLed_ so
    // refreshes re-push.
    if (buttonId < lastButtonLed_.size()) {
        const int8_t want = (state == LedState::Off) ? 0
                          : (state == LedState::Dim) ? 2 : 1;
        if (lastButtonLed_[buttonId] == want) return;
        lastButtonLed_[buttonId] = want;
    }

    auto buildRaw = [](uint8_t bank, uint8_t c, uint8_t b5, uint8_t state) {
        std::vector<uint8_t> f{0xFF, 0x13, 0x04, bank, c, b5, state};
        uint8_t cks = 0;
        for (size_t i = 1; i < f.size(); ++i) cks += f[i];
        f.push_back(cks);
        return f;
    };

    // Right-side Dyn/Gate/SC plugin-param buttons (FastAttComp, Peak,
    // DynIn, Expand, FastAttGate, ScListen) need TWO frames on
    // byte5=0x01 — the dyn-section LED address space:
    //   bank 0x01 = selection bit (0x01 on / 0x00 off)
    //   bank 0x02 = brightness   (0xFF on / 0x00 off)
    // Same dual-bank scheme pushKnobRing_ uses, but with byte5=0x01
    // so the writes don't bleed into EQ-ring rendering (byte5=0x00).
    // Single bank=0x02 frame alone leaves the selection bit clear and
    // the LED stays dark.
    const bool isDynButton =
        buttonId == button::kFastAttComp || buttonId == button::kPeak      ||
        buttonId == button::kDynIn       || buttonId == button::kExpand    ||
        buttonId == button::kFastAttGate || buttonId == button::kScListen;
    if (isDynButton) {
        const uint8_t bri = !on ? 0x00 : (dim ? 0x33 : 0xFF);
        device_->send(buildRaw(0x01, cell.cell, 0x01, on ? 0x01 : 0x00));
        device_->send(buildRaw(0x02, cell.cell, 0x01, bri));
        return;
    }

    // Left-side EQ-section plugin-param buttons (HfBell, EqType, EqIn,
    // LfBell) live in the EQ-section LED address space — byte5=0x00.
    // Cap21/cap22 show SSL360 sending a single bank=0x02 byte5=0x00
    // frame per press, but on our extension that single frame doesn't
    // light the LED (same gap as the dyn buttons before adding the
    // bank=0x01 selection-bit companion). Send the dual-bank pair on
    // byte5=0x00. The cells (0x23, 0x50, 0x51, 0x89) sit in gaps
    // between adjacent EQ rings, so the byte5=0x00 writes don't bleed
    // into any ring rendering.
    const bool isEqButton =
        buttonId == button::kHfBell || buttonId == button::kEqType ||
        buttonId == button::kEqIn   || buttonId == button::kLfBell;
    if (isEqButton) {
        const uint8_t bri = !on ? 0x00 : (dim ? 0x33 : 0xFF);
        device_->send(buildRaw(0x01, cell.cell, 0x00, on ? 0x01 : 0x00));
        device_->send(buildRaw(0x02, cell.cell, 0x00, bri));
        return;
    }

    // Other buttons (Fine, central-section track buttons with Solo/Cut
    // overrides, Solo Clear) keep the single-frame buildLedWrite path
    // — those were empirically verified working with one frame.
    //   - Solo Clear uses bank 0x01 + state 0x01 on / 0x00 off (cap17).
    //   - Solo/Cut/Polarity/ChannelIn/BusCompIn: fan-out test confirmed
    //     the LED lights with bank 0x01 + state 0x01.
    uint8_t bank = cell.bank;
    uint8_t stateOn = led::kStateOn;
    if (buttonId == button::kSoloClear) {
        stateOn = 0x01;
    } else if (buttonId == button::kSolo      ||
               buttonId == button::kCut       ||
               buttonId == button::kPolarity  ||
               buttonId == button::kChannelIn ||
               buttonId == button::kBusCompIn)
    {
        bank = 0x01;
        stateOn = 0x01;
    }
    // Dim only meaningful for the kStateOn=0xFF path (the Solo/Cut/etc.
    // 0x01-coded buttons have no firmware-supported dim state); dim
    // requests on those collapse to plain on.
    uint8_t stateByte;
    if (!on)        stateByte = led::kStateOff;
    else if (dim && stateOn == led::kStateOn) stateByte = led::kStateDim;
    else            stateByte = stateOn;

    device_->send(buildLedWrite(bank, cell.cell, stateByte));
}

void UC1Surface::pollButtonLeds_()
{
    if (!device_) return;

    MediaTrack* tr = static_cast<MediaTrack*>(focusedTrack_);
    UC1Bindings bindings = tr ? lookupBindingsOnTrack(tr) : UC1Bindings{};
    // BC button LED state reads from the BC anchor (BC section is pinned
    // independent of CS focus).
    void* bcAnchorRaw = effectiveBcTrack_();
    MediaTrack* bcTr = static_cast<MediaTrack*>(bcAnchorRaw);
    UC1Bindings bcBindings = bcAnchorRaw
        ? ((bcAnchorRaw == focusedTrack_) ? bindings
                                          : lookupBindingsOnTrack(bcAnchorRaw))
        : UC1Bindings{};
    const CascadeState cascade = computeCascade_(tr, bindings, bcTr, bcBindings);

    auto ledForParam = [&](const PluginBindings* map, int fxIdx, uint8_t btnId) {
        if (!map || !tr) return false;
        const int p = map->buttonParam[btnId];
        if (p == kParamNone) return false;
        return TrackFX_GetParamNormalized(tr, fxIdx, p) >= 0.5;
    };
    auto stateFor = [&](uint8_t btn, bool on) -> LedState {
        if (!on) return LedState::Off;
        return buttonCascadeDim_(btn, cascade) ? LedState::Dim : LedState::On;
    };

    for (uint8_t btn = 0; btn < 0x20; ++btn) {
        const auto cell = cellForButton(btn);
        if (cell.bank == 0 && cell.cell == 0) continue;

        bool on = false;
        switch (classifyButton(btn)) {
            case ControlDomain::BusComp: {
                // BC IN LED reflects the plug-in's Bypass param: lit when
                // bypass < 0.5 (plug-in active / IN). Falls back to
                // TrackFX_Enabled if a track has BC2 but no bypassParam
                // is registered (shouldn't happen — kept defensively).
                bool bcOn = false;
                if (bcBindings.busCompMap && bcTr) {
                    if (bcBindings.busCompMap->bypassParam != kParamNone) {
                        bcOn = !readPluginBypass_(bcTr,
                            bcBindings.busCompMap, bcBindings.busCompFxIdx);
                    } else {
                        bcOn = TrackFX_GetEnabled(bcTr, bcBindings.busCompFxIdx);
                    }
                }
                pushButtonLed_(btn, bcOn);
                continue;
            }
            case ControlDomain::ChannelStrip:
                if (btn == button::kChannelIn) {
                    bool cin = false;
                    if (bindings.channelMap && tr) {
                        if (bindings.channelMap->bypassParam != kParamNone) {
                            cin = !readPluginBypass_(tr,
                                bindings.channelMap, bindings.channelFxIdx);
                        } else {
                            cin = TrackFX_GetEnabled(tr, bindings.channelFxIdx);
                        }
                    } else if (tr && TrackFX_GetCount(tr) > 0) {
                        cin = TrackFX_GetEnabled(tr, 0);
                    }
                    pushButtonLed_(btn, cin);
                    continue;
                }
                on = ledForParam(bindings.channelMap, bindings.channelFxIdx, btn);
                break;
        }

        if (btn == button::kFine) on = fineMode_.load(std::memory_order_relaxed);

        if (btn == button::kSolo) {
            const int mirrored = reasixty_recUc1ButtonMirroredState(2, tr);
            const bool ledOn = (mirrored >= 0)
                ? (mirrored == 1)
                : (tr && GetMediaTrackInfo_Value(tr, "I_SOLO") > 0.5);
            pushButtonLed_(btn, ledOn);
            continue;
        }
        if (btn == button::kCut) {
            const int mirrored = reasixty_recUc1ButtonMirroredState(1, tr);
            const bool ledOn = (mirrored >= 0)
                ? (mirrored == 1)
                : (tr && GetMediaTrackInfo_Value(tr, "B_MUTE") > 0.5);
            pushButtonLed_(btn, ledOn);
            continue;
        }
        if (btn == button::kPolarity) {
            const int mirrored = reasixty_recUc1ButtonMirroredState(3, tr);
            const bool ledOn = (mirrored >= 0)
                ? (mirrored == 1)
                : (tr && GetMediaTrackInfo_Value(tr, "B_PHASE") > 0.5);
            pushButtonLed_(btn, ledOn);
            continue;
        }
        if (btn == button::kSoloClear) {
            bool anySolo = false;
            const int nTr = CountTracks(nullptr);
            for (int i = 0; i < nTr; ++i) {
                if (GetMediaTrackInfo_Value(GetTrack(nullptr, i), "I_SOLO") > 0.5) {
                    anySolo = true;
                    break;
                }
            }
            pushButtonLed_(btn, anySolo);
            continue;
        }

        pushButtonLed_(btn, stateFor(btn, on));
    }
}

void UC1Surface::pollBcBypassState_()
{
    if (!device_) return;
    // BC lives on its own track (typically a bus), independent of the
    // currently-focused track. Use the BC anchor — same source used by
    // the BC carousel + display — so the backlight + cosmetic fire from
    // BC presses regardless of where focus is.
    MediaTrack* tr = static_cast<MediaTrack*>(effectiveBcTrack_());
    if (!tr) {
        lastBcBypassed_ = -1;
        return;
    }
    UC1Bindings b = lookupBindingsOnTrack(tr);
    if (!b.busCompMap || b.busCompMap->bypassParam == kParamNone) {
        lastBcBypassed_ = -1;
        return;
    }
    const bool bypassed = readPluginBypass_(tr, b.busCompMap, b.busCompFxIdx);
    const int8_t cur = bypassed ? 1 : 0;
    if (cur == lastBcBypassed_) return;

    // BC mechanical-VU backlight (cap43): binary on/off, FF when enabled
    // / 00 when bypassed. Lives outside the 0x33 dim cascade — has its
    // own cell.
    device_->send(buildLedWrite(0x02, 0x01, bypassed ? 0x00 : 0xFF));

    // FF 5C cosmetic needle-pose (cap45): one frame per real transition.
    // Skip on the unknown→known boot path so we don't fire a phantom
    // "you just toggled" pose at every focus change to a track that's
    // simply already-bypassed or already-enabled.
    if (lastBcBypassed_ != -1) {
        device_->send(buildBcBypassPose(/*entering=*/bypassed));
    }

    lastBcBypassed_ = cur;
}

void UC1Surface::pollGainReduction_()
{
    if (!device_) return;
    // Settle gate: skip pushing GR within the brief window after a focus
    // / anchor change so the mechanical needle doesn't twitch toward a
    // stale or transient reading. The needle naturally holds its last
    // commanded position; pollGainReduction is called every tick, so
    // skipping for ~250 ms just delays the first new-track push.
    if (std::chrono::steady_clock::now() < grSettleUntil_) return;
    // PreSonus VST3 GR-meter standard, exposed by REAPER as a named
    // config parm. Returns a string with the dB value (e.g. "12.345").
    // Documented as "ReaComp + other supported compressors" — SSL Native
    // BC2 / CS2 expose the same readback (verified by user; SSL360
    // itself uses the same host-side mechanism to drive the UC1's
    // mechanical needle in MCU mode). One read per FX present.
    // Read order: when the user-mapped plug-in carries an explicit GR
    // param (Settings → FX Learn → Metering picker), read the raw
    // parameter value via TrackFX_GetParam — that's what most VST3
    // plug-ins expose for meter outputs (often the value displayed in
    // their own UI). FormattedParamValue is unreliable for meter params
    // that lack a string formatter. Otherwise fall back to the PreSonus
    // GainReduction_dB named-config-parm, which built-in SSL CS2/BC2
    // implement. Sign is normalised to |value| so pushGainReduction's
    // clamp-positive contract holds; offsetDb shifts the raw reading
    // before abs(), letting the user calibrate against plug-ins whose
    // meter reads e.g. negative-going dB.
    // `cal` + `bp` + `nBp` describe a per-renderer breakpoint table (BC
    // VU motor uses 0/4/8/12/16/20 dB ticks; DYN GR LEDs + UF8 GR row
    // use the SSL plug-in's 3/6/10/14/20 dB segment boundaries). When
    // `cal` is nullptr the function is identity-calibrated — built-in
    // plug-ins and the fallback-walk path go through this branch.
    // Sign is normalised via |abs|; the bottom breakpoint (cal[0])
    // absorbs any residual baseline drift.
    //
    // Read strategy (Frank 2026-05-15, Brainworx SSL 9000J): when the
    // user designated an explicit GR param via the FX Learn picker we
    // ask REAPER for the plug-in's FORMATTED value ("3.45 dB"), NOT
    // the raw param value. The raw param is often in a normalised or
    // proprietary scale (Brainworx returns 0..1 where 1=20 dB GR);
    // treating that as dB makes the meter wildly inaccurate. The
    // formatted string is what the plug-in's own UI displays, so it
    // matches what the user sees. We atof the leading number and let
    // anything trailing (" dB", " %", anything else) drop. PreSonus
    // GainReduction_dB fallback is unchanged — it always returns dB.
    auto readGr = [](MediaTrack* tr, int fxIdx, int grParam,
                     const double* cal, const double* bp, int nBp) -> float {
        if (!tr || fxIdx < 0) return 0.0f;
        double v = 0.0;
        if (grParam >= 0) {
            char fbuf[64] = {0};
            if (TrackFX_GetFormattedParamValue(tr, fxIdx, grParam,
                                               fbuf, sizeof(fbuf))
                && fbuf[0])
            {
                v = std::atof(fbuf);
            } else {
                double mn = 0.0, mx = 0.0;
                v = TrackFX_GetParam(tr, fxIdx, grParam, &mn, &mx);
            }
        } else {
            char buf[64] = {0};
            if (!TrackFX_GetNamedConfigParm(tr, fxIdx, "GainReduction_dB",
                                            buf, sizeof(buf))) {
                return 0.0f;
            }
            v = std::atof(buf);
        }
        if (v < 0) v = -v;
        if (cal) v = uf8::applyGrCalibration(v, bp, cal, nBp);
        return static_cast<float>(v);
    };

    // BC: drives the mechanical analog needle. Source is the BC-anchor
    // track (independent of focus — same as pollBcBypassState_).
    float bcGr = 0.0f;
    MediaTrack* bcTr = static_cast<MediaTrack*>(effectiveBcTrack_());
    if (bcTr) {
        UC1Bindings b = lookupBindingsOnTrack(bcTr);
        if (b.busCompMap) {
            bcGr = readGr(bcTr, b.busCompFxIdx, b.busCompGrParam,
                          b.busCompGrBcVuCal, uf8::kBcVuBpDb, uf8::kBcVuBpCount);
        }
    }

    // CS Comp GR: drives the 5-LED Comp strip. Source is the focused
    // track's CS plug-in's combined GainReduction_dB (Comp + Gate
    // contributions blended; in practice the Gate's contribution shows
    // up as a Range-sized spike when gating, so the Comp strip is the
    // closest thing we have to "comp activity" without separate readout).
    //
    // When the track has no SSL CS / user-mapped CS plug-in, Frank
    // 2026-05-06: fall back to ANY FX on the track that exposes the
    // PreSonus GainReduction_dB named-config-parm. ReaComp, FabFilter
    // Pro-C2, Waves SSL G, etc. all implement it. The first hit wins;
    // walk the chain so the meter still does something useful when
    // the user is mixing with a non-SSL compressor.
    float csCompGr = 0.0f;
    MediaTrack* csTr = static_cast<MediaTrack*>(focusedTrack_);
    if (csTr) {
        UC1Bindings b = lookupBindingsOnTrack(csTr);
        if (b.channelMap) {
            csCompGr = readGr(csTr, b.channelFxIdx, b.channelGrParam,
                              b.channelGrLedsCal, uf8::kLedsBpDb, uf8::kLedsBpCount);
        } else if (::reasixty_grAnyFx()) {
            const int fxCount = TrackFX_GetCount(csTr);
            for (int fx = 0; fx < fxCount; ++fx) {
                char buf[64] = {0};
                if (!TrackFX_GetNamedConfigParm(csTr, fx, "GainReduction_dB",
                                                buf, sizeof(buf))) {
                    continue;
                }
                double v = std::atof(buf);
                if (v < 0) v = -v;
                csCompGr = static_cast<float>(v);
                break;
            }
        }
    }

    // CS Gate GR: TODO. SSL CS2 doesn't expose a Gate-only readout via
    // GainReduction_dB; the user's hardware shows Gate GR independently
    // (it lit up alongside the Range knob during dual_35 capture work).
    // Until we find the right data source (separate parmname? Range param
    // value? real-time signal vs Gate-Threshold?), drive at 0 so the
    // strip stays dark — better than mirroring Comp GR onto it.
    float csGateGr = 0.0f;

    // Device-level per-tick calibration (Settings → Device → Calibrate).
    // Applied AFTER per-plugin FX-Learn cal — this is a hardware-trim,
    // independent of which plug-in produced the reading. Both renderers
    // get their own cal table because the BC VU motor and the DYN LED
    // strip have unrelated physical response curves.
    //
    // Calibration test mode override: when the user is in Settings →
    // Device Calibrate, the live GR feed is bypassed and the matching
    // tick's calibrated value is sent. -1 (default) = normal poll.
    const int testTick = ::reasixty_uc1CalActiveTest();
    if (testTick >= 0 && testTick < 6) {
        bcGr      = static_cast<float>(::reasixty_uc1CalTickDb(0, testTick)
                                       + ::reasixty_uc1CalEffective(0, testTick));
        csCompGr  = 0.0f;
        csGateGr  = 0.0f;
    } else if (testTick >= 100 && testTick < 105) {
        const int idx = testTick - 100;
        csCompGr  = static_cast<float>(::reasixty_uc1CalTickDb(1, idx)
                                       + ::reasixty_uc1CalEffective(1, idx));
        bcGr      = 0.0f;
        csGateGr  = 0.0f;
    } else {
        // Normal path: apply device-level cal piecewise. The BC table
        // anchors at 0/4/8/12/16/20 dB; the LED table at 3/6/10/14/20.
        // Effective = factory baseline + user delta — see main.cpp.
        double bcCal[6], csCal[5];
        for (int i = 0; i < 6; ++i) bcCal[i] = ::reasixty_uc1CalEffective(0, i);
        for (int i = 0; i < 5; ++i) csCal[i] = ::reasixty_uc1CalEffective(1, i);
        bcGr     = static_cast<float>(uf8::applyGrCalibration(
                       bcGr,     uf8::kBcVuBpDb, bcCal, 6));
        csCompGr = static_cast<float>(uf8::applyGrCalibration(
                       csCompGr, uf8::kLedsBpDb, csCal, 5));
        csGateGr = static_cast<float>(uf8::applyGrCalibration(
                       csGateGr, uf8::kLedsBpDb, csCal, 5));
    }

    pushGainReduction(bcGr, csCompGr, csGateGr);
}

void UC1Surface::pollKnobRings_()
{
    if (!device_) return;
    MediaTrack* tr = static_cast<MediaTrack*>(focusedTrack_);
    UC1Bindings bindings = tr ? lookupBindingsOnTrack(tr) : UC1Bindings{};
    void* bcAnchorRaw = effectiveBcTrack_();
    MediaTrack* bcTr = static_cast<MediaTrack*>(bcAnchorRaw);
    UC1Bindings bcBindings = bcAnchorRaw
        ? ((bcAnchorRaw == focusedTrack_) ? bindings
                                          : lookupBindingsOnTrack(bcAnchorRaw))
        : UC1Bindings{};
    if (!tr && !bcTr) return;
    const CascadeState cascade = computeCascade_(tr, bindings, bcTr, bcBindings);

    auto pushOne = [&](MediaTrack* t, uint8_t knobId,
                       const PluginBindings* m, int fxIdx) {
        if (!t || !m) return;
        const int vst3Param = m->knobParam[knobId];
        if (vst3Param == kParamNone) return;
        const double v = TrackFX_GetParamNormalized(t, fxIdx, vst3Param);
        const double visual = m->inverted[knobId] ? (1.0 - v) : v;
        pushKnobRing_(knobId, visual, knobCascadeDim_(knobId, cascade));
    };

    if (tr && bindings.channelMap) {
        for (uint8_t knobId = 0; knobId < 0x20; ++knobId) {
            pushOne(tr, knobId, bindings.channelMap, bindings.channelFxIdx);
        }
    }
    if (bcTr && bcBindings.busCompMap) {
        for (uint8_t knobId = 0; knobId < 0x20; ++knobId) {
            pushOne(bcTr, knobId, bcBindings.busCompMap, bcBindings.busCompFxIdx);
        }
    }
}

void UC1Surface::refresh()
{
    if (!device_) return;
    // Menu modes (PRESETS / ROUTING / EXT_FUNCS / TRANSPORT) own the
    // LCD content. refresh() repaints the MAIN-mode carousel + central
    // label which would clobber the menu rendering on track focus
    // changes. Skip everything when not in MAIN — pollKnobRings_ /
    // pollButtonLeds_ continue to mirror plug-in state to LEDs every
    // tick regardless, so we don't need refresh() for that.
    if (mode_ != Uc1Mode::Main) return;

    // Project-load guard. REAPER fires SetSurfaceSolo / SetSurfaceMute
    // mid-project-load (TrackList_AdjustWindows → UpdateAllExternalSurfaces),
    // which lands here while our cached focusedTrack_ still points at
    // the previous project's freed MediaTrack. Reading P_NAME off the
    // stale pointer crashed inside _platform_strlen — captured 21:51.
    // ValidatePtr2 with proj=nullptr (current project) returns false
    // for tracks that no longer belong to the active project; clear
    // our cache in that case so subsequent ticks recover cleanly.
    if (focusedTrack_ &&
        !ValidatePtr2(nullptr, focusedTrack_, "MediaTrack*")) {
        // Track was deleted under us. Drop the dangling pointer FIRST so
        // setFocusedTrack's "same pointer" early-exit doesn't skip the
        // refresh, then snap focus to the track that was DIRECTLY ABOVE
        // the deleted one. lastFocusedTrackIdx_ was cached on the
        // previous tick while the pointer was still valid; tracks below
        // shift down by one on delete, so the track now at
        // (oldIdx - 1) is the one we want — or, when the deleted track
        // was already #0, the new #0. Falls back to project track 0
        // when no cached index is available (cold-start race). Without
        // this the UC1 carousel stayed stuck on the deleted track and
        // the next Enc 1 turn jumped to first/last via stepVisibleTrack
        // on a stale pointer (Frank 2026-05-26).
        focusedTrack_ = nullptr;
        const int total = CountTracks(nullptr);
        MediaTrack* fallback = nullptr;
        if (total > 0) {
            int idx = lastFocusedTrackIdx_ - 1;
            if (idx < 0) idx = 0;
            if (idx >= total) idx = total - 1;
            fallback = GetTrack(nullptr, idx);
        }
        if (fallback) setFocusedTrack(fallback);
        lastFocusedTrackIdx_ = -1;
    } else if (focusedTrack_) {
        // Cache index while pointer is known good. IP_TRACKNUMBER is
        // 1-based with 0 = master; convert to zero-based project index.
        const int tn = static_cast<int>(GetMediaTrackInfo_Value(
            static_cast<MediaTrack*>(focusedTrack_), "IP_TRACKNUMBER"));
        if (tn > 0) lastFocusedTrackIdx_ = tn - 1;
    }

    auto bindings = focusedTrack_ ? lookupBindingsOnTrack(focusedTrack_) : UC1Bindings{};
    // BC bindings live on the BC anchor (independent of CS focus).
    // effectiveBcTrack_ also caches a pointer that may have been
    // invalidated by a project load — same ValidatePtr2 guard.
    void* bcAnchorRaw_ = effectiveBcTrack_();
    if (bcAnchorRaw_ &&
        !ValidatePtr2(nullptr, bcAnchorRaw_, "MediaTrack*")) {
        bcAnchorRaw_ = nullptr;
    }
    MediaTrack* bcTr_ = static_cast<MediaTrack*>(bcAnchorRaw_);
    UC1Bindings bcBindings_ = bcAnchorRaw_
        ? ((bcAnchorRaw_ == focusedTrack_) ? bindings
                                           : lookupBindingsOnTrack(bcAnchorRaw_))
        : UC1Bindings{};

    // Track name push — zones 0x02 (CS slot) and 0x04 (BC slot). Per
    // SSL UC1 User Guide p.17 each slot shows the DAW track name of
    // the track that has THAT specific plugin inserted. If a plugin
    // isn't present on the focused track, its slot reads "No Plug-ins".
    // Empty string → device keeps the last state; we explicitly reset
    // the slot to all-zeros when empty so the stale track name from a
    // previous focus doesn't linger.
    auto resolveTrackName = [&]() -> std::string {
        if (!focusedTrack_) return {};
        MediaTrack* tr = static_cast<MediaTrack*>(focusedTrack_);
        char nameBuf[128] = {0};
        if (GetSetMediaTrackInfo_String(tr, "P_NAME", nameBuf, false)
            && nameBuf[0] != '\0')
        {
            return nameBuf;
        }
        int idx = static_cast<int>(GetMediaTrackInfo_Value(tr, "IP_TRACKNUMBER"));
        char fallback[32];
        snprintf(fallback, sizeof(fallback), "Track %d", idx);
        return fallback;
    };

    // CS slot always shows the focused track's name — Rea-Sixty uses
    // the Channel Strip display as the general "current track" view,
    // whether or not an SSL Channel Strip 2 plugin is on the track.
    // BC slot uses the BC anchor (independent of CS focus, persists
    // across CHANNEL-encoder scrolling). Falls back gracefully when
    // no BC track exists in the project.
    std::string csName = resolveTrackName();
    // Re-validate the BC anchor here too — effectiveBcTrack_() can
    // hand back a cached pointer that was invalidated by a project
    // load between the earlier guard at the top of refresh() and now.
    void* bcAnchor = effectiveBcTrack_();
    if (bcAnchor && !ValidatePtr2(nullptr, bcAnchor, "MediaTrack*")) {
        bcAnchor = nullptr;
    }
    std::string bcName;
    if (bcAnchor) {
        MediaTrack* bcTr = static_cast<MediaTrack*>(bcAnchor);
        char nameBuf[128] = {0};
        if (GetSetMediaTrackInfo_String(bcTr, "P_NAME", nameBuf, false)
            && nameBuf[0])
        {
            bcName = nameBuf;
        } else {
            int idx = static_cast<int>(GetMediaTrackInfo_Value(bcTr, "IP_TRACKNUMBER"));
            char fallback[32];
            snprintf(fallback, sizeof(fallback), "Track %d", idx);
            bcName = fallback;
        }
    }

    // Diag — first 8 refreshes only. Routed through a file log instead
    // of ShowConsoleMsg: we already lost one session to a PC=0 fault
    // on a ShowConsoleMsg call from REAPER's surface-callback path
    // (see learnings.md), and refresh() is on that same path.
    {
        static int kDiagRemaining = 8;
        if (kDiagRemaining > 0) {
            --kDiagRemaining;
            if (FILE* f = std::fopen("/tmp/rea_sixty_uc1_refresh.log", "a")) {
                std::fprintf(f, "UC1 refresh  cs='%s' bc='%s'\n",
                             csName.c_str(), bcName.c_str());
                std::fclose(f);
            }
        }
    }

    // Track-name carousel — 3 slots [prev, current, next].
    // REAPER track index is 0-based; focused track is the middle slot.
    // Empty slot strings leave the slot's zero-pad intact so edge cases
    // (first/last track) don't show stale names.
    auto nameOfIdx = [](int idx) -> std::string {
        const int n = CountTracks(nullptr);
        if (idx < 0 || idx >= n) return "";
        MediaTrack* t = GetTrack(nullptr, idx);
        char buf[128] = {0};
        if (GetSetMediaTrackInfo_String(t, "P_NAME", buf, false) && buf[0]) return buf;
        char fallback[16];
        snprintf(fallback, sizeof(fallback), "Trk %d", idx + 1);
        return fallback;
    };
    // BC carousel: 3-slot [prev, curr, next] across BC-bearing tracks
    // ONLY (skips non-BC tracks). Mirrors the CS carousel's UX but
    // anchored on the BC focus. Curr is the BC anchor; prev/next are
    // the BC-bearing tracks immediately before/after it in project
    // order. Slots are empty when no BC track exists in that
    // direction.
    std::vector<int> bcIndices;
    {
        const int n = CountTracks(nullptr);
        bcIndices.reserve(n);
        for (int i = 0; i < n; ++i) {
            MediaTrack* t = GetTrack(nullptr, i);
            if (lookupBindingsOnTrack(t).busCompMap) bcIndices.push_back(i);
        }
    }
    int bcAnchorProjIdx = -1;
    if (bcAnchor) {
        bcAnchorProjIdx = static_cast<int>(GetMediaTrackInfo_Value(
            static_cast<MediaTrack*>(bcAnchor), "IP_TRACKNUMBER")) - 1;
    }
    int bcRank = -1;
    for (size_t i = 0; i < bcIndices.size(); ++i) {
        if (bcIndices[i] == bcAnchorProjIdx) { bcRank = static_cast<int>(i); break; }
    }
    auto bcNameAtRank = [&](int rank) -> std::string {
        if (rank < 0 || rank >= static_cast<int>(bcIndices.size())) return "";
        return nameOfIdx(bcIndices[rank]);
    };
    int curIdx = -1;
    if (focusedTrack_) {
        curIdx = static_cast<int>(GetMediaTrackInfo_Value(
            static_cast<MediaTrack*>(focusedTrack_), "IP_TRACKNUMBER")) - 1;
    }
    // UC1 CS carousel slot fits 12 chars; UC1 BC carousel slot fits 14
    // (per hardware testing 2026-05-25). abbreviateTrackName_ honours the
    // global Smart Abbreviate / Truncate toggle from Settings.
    const std::string prevName = abbreviateTrackName_(nameOfIdx(curIdx - 1), 12);
    const std::string currName = abbreviateTrackName_(nameOfIdx(curIdx),     12);
    const std::string nextName = abbreviateTrackName_(nameOfIdx(curIdx + 1), 12);
    // Build triples but defer sending — uc1_47 Encoder 1 burst at
    // t=1.4309s shows SSL 360°'s order is layout-enable → central-label
    // → LARGE triple → SMALL triple → palette → invalidate(0x0F). The
    // sub-mode byte in `FF 66 03 00 01 <sub>` drives a layout state
    // machine: 0x01 = CS-with-carousel visible, 0x00 = neutral (no
    // carousel). Sending triples BEFORE the layout-enable, then sending
    // sub=0x00 from steady-state code, was emptying the carousel slots
    // every refresh.
    auto smallTriple = buildTrackNameTripleSmall(prevName, currName, nextName);
    lastSmallTripleFrame_ = smallTriple;
    auto largeTriple = buildTrackNameTripleLarge(
        abbreviateTrackName_(bcNameAtRank(bcRank - 1), 14),
        abbreviateTrackName_(bcNameAtRank(bcRank),     14),
        abbreviateTrackName_(bcNameAtRank(bcRank + 1), 14));
    lastLargeTripleFrame_ = largeTriple;

    // 7-segment push moved to the end of refresh() — see below. Several
    // knob ring cell maps (FaderLevel, BC Mix, BC Release) overlap the
    // 7-seg ones/tens/hundreds cells on bank 0x01, so the 7-seg has to
    // be the LAST writer in this function for focus-change to land
    // legibly. Pushing it here would just be overwritten by the eager
    // ring loops.

    // Central label — 4-char plugin-type tag shown in the UC1 central
    // LCD. "MAIN" when no SSL plugin is focused, otherwise the plugin's
    // shortName ("CS 2", "BC 2", "4K E" …). Also drives the
    // colour-bar-enable flag that gates the coloured top-stripe
    // rendering: 0x01 when plugin context exists, 0x00 for MAIN.
    // Plugin presence: CS bound to focused track, BC bound to anchor.
    //
    // BC-scroll overlay (uc1_41 2026-05-01): during the ~1.5s overlay
    // window we replace the central label with "BUS COMP 2" — same
    // FF 66 …01… frame slot, so without this branch the buildCentralLabel
    // call below silently overwrites the overlay header set by the BC
    // encoder handler. We also need the overlay's banner sub=0x02
    // pushed here because refresh() may run AFTER the encoder handler
    // (via setFocusedTrack's internal refresh) and reset whatever
    // banner state was last written.
    // Colour-bar enable: any plug-in context — SSL Instance (CS or BC)
    // OR a fall-through-target FX on the focused track (cursor's
    // default-to-FX[0]). Mirrors the CS-branch fallback below so the
    // bar's coloured stripe lights up for non-Instance plug-ins too,
    // not just SSL Instances. Frank 2026-05-22.
    const bool haveActiveFx = focusedTrack_
        && reasixty_stripInstanceActiveFx(
               static_cast<MediaTrack*>(focusedTrack_)) >= 0;
    const bool havePlugin = bindings.channelMap || bcBindings_.busCompMap
        || haveActiveFx;
    device_->send(buildColourBarEnable(havePlugin));

    // Release stale readout zones for any domain the new focused track
    // doesn't host. Without this, navigating to a CS-less track left
    // the last CS readout text ("Bypass Off" etc.) ghosting in zone
    // 0x03; same for BC. Send the SSL 360° invalidate frame
    // (FF 66 01 <zone>) and clear the cache.
    if (!bindings.channelMap && !lastZone03Text_.empty()) {
        lastZone03Text_.clear();
        device_->send(buildDisplayInvalidate(zone::kChannelStripReadout));
    }
    if (!bcBindings_.busCompMap && !lastZone05Text_.empty()) {
        lastZone05Text_.clear();
        device_->send(buildDisplayInvalidate(zone::kBusCompReadout));
    }
    if (instanceCarouselActive_) {
        // Instance / FX cycle carousel — claim the same layout slot
        // as the BC-scroll overlay but with the caller-supplied
        // header (e.g. track name) and our own LARGE triple. The
        // triple was already sent inside showInstanceCarousel; we
        // refresh the central mode + header here so a refresh()
        // triggered while the overlay is active (e.g. from an FX
        // GUI sync) doesn't drop sub=0x02.
        device_->send(buildCentralMode(CentralMode::Main, 0x02));
        device_->send(buildLcdHeader(instanceCarouselHeader_));
        if (!instanceCarouselTripleFrame_.empty()) {
            device_->send(std::vector<uint8_t>(instanceCarouselTripleFrame_));
            lastLargeTripleFrame_ = instanceCarouselTripleFrame_;
        }
    } else if (bcScrollOverlayActive_) {
        device_->send(buildCentralMode(CentralMode::Main, 0x02));
        device_->send(buildLcdHeader("BUS COMP 2"));
    } else {
        // Central label resolution — strict domain mapping. The user
        // controls which domain the LCD shows by picking the matching
        // encoder/Quick button:
        //   * Channel encoder + Q1 → CS focus → CS label or MAIN.
        //   * BC encoder      + Q2 → BC focus → BC label or MAIN.
        // No cross-domain fallback: if you're in CS focus and the
        // focused track has no CS, the LCD reads "MAIN" — it does NOT
        // fall through to the anchor's BC name (which would be visible
        // even after the user moved to a different track via the
        // channel encoder, defeating "moved to channel X" feedback).
        const auto fp = uf8::getFocusedParam();
        const bool wantBc = (fp.domain == uf8::Domain::BusComp);

        // baseLabel-as-string lets us swap in a user-rename (variable-
        // length) over the const-char* PluginBindings shortName. Empty
        // string falls back to "MAIN" at the strncpy below.
        std::string baseLabel;
        void*       instanceTrack = nullptr;
        bool        useBc        = false;
        // Touched-FX reveal (3 s) wins over the focused-domain default,
        // mirroring the UF8 csType priority in pushZonesForVisibleSlots.
        // Only fires when the touched FX is on this UC1's focused
        // track — same-track-only is the natural scope since UC1 only
        // renders one plug-in label at a time. Frank 2026-05-15.
        const bool revealActive = reasixty_touchedFxRevealActive()
            && reasixty_touchedFxRevealTrack() == focusedTrack_;
        if (revealActive) {
            baseLabel    = reasixty_touchedFxRevealLabel();
            instanceTrack = focusedTrack_;
            useBc        = false;
        } else if (wantBc) {
            if (bcBindings_.busCompMap) {
                // User-rename of the BC instance wins over the shortName
                // fallback. Frank 2026-05-20: SSL 360 Link instances
                // should surface their user-given name, not the family-
                // level "L-BC" abbreviation.
                if (bcTr_) {
                    baseLabel = reasixty_fxUserRename(
                        bcTr_, bcBindings_.busCompFxIdx);
                }
                if (baseLabel.empty()) {
                    baseLabel = bcBindings_.busCompMap->shortName;
                }
                instanceTrack = effectiveBcTrack_();
                useBc        = true;
            }
            // else: "MAIN" — no BC on anchor, don't fall back to CS.
        } else {
            if (bindings.channelMap) {
                // Same rename-precedence rule as the BC branch above.
                if (focusedTrack_) {
                    baseLabel = reasixty_fxUserRename(
                        focusedTrack_, bindings.channelFxIdx);
                }
                if (baseLabel.empty()) {
                    baseLabel = bindings.channelMap->shortName;
                }
                instanceTrack = focusedTrack_;
                useBc        = false;
            } else if (focusedTrack_) {
                // No CS on focused track — fall through to the
                // channel's active FX (cursor default-to-FX[0]) so a
                // non-Instance plug-in (UF8-learned or unmapped) still
                // gets named on the LCD. Mirrors the UF8 colour-bar
                // fallback in main.cpp pushZonesForVisibleSlots so
                // "was angezeigt wird = was beim Push aufgeht" holds.
                // BC-focus stays strict (above branch) — the BC anchor
                // is a narrow concept that should not surface non-BC
                // plug-ins. Frank 2026-05-22.
                MediaTrack* fT =
                    static_cast<MediaTrack*>(focusedTrack_);
                const int fxIdx = reasixty_stripInstanceActiveFx(fT);
                if (fxIdx >= 0) {
                    baseLabel     = reasixty_fxCycleDisplayName(fT, fxIdx);
                    instanceTrack = focusedTrack_;
                    useBc         = false;
                }
            }
        }
        // Zero plug-ins on this track → leave the central label blank;
        // the empty LCD field is a clearer "nothing here" cue than a
        // literal label. Frank 2026-05-25.

        // Central label width — buildCentralLabel accepts up to 8 chars
        // (Frank 2026-05-09 widening probe). The A/B/C multi-instance
        // letter suffix was dropped 2026-05-09 — Frank wants the raw
        // displayShort regardless of how many instances are on the
        // track (instance cycling is conveyed via the encoder action,
        // not the LCD label).
        constexpr int kCentralLabelW = 7;
        char labelBuf[kCentralLabelW + 1] = {0};
        std::strncpy(labelBuf, baseLabel.c_str(), kCentralLabelW);
        const int total = !instanceTrack ? 0
            : useBc ? bcInstanceCount(instanceTrack)
                    : csInstanceCount(instanceTrack);
        const int idx = (total > 1 && instanceTrack)
            ? (useBc ? bcInstanceIndex(instanceTrack)
                     : csInstanceIndex(instanceTrack))
            : 0;
        device_->send(buildCentralLabel(labelBuf));

        // Longer plug-in name in the LCD header zone (the same one the
        // BC-scroll overlay uses for "BUS COMP 2"). Uses the PluginMap's
        // `match` string — typically the descriptive name ("Bus
        // Compressor 2", "Channel Strip 2", or the user's match string
        // for learned plug-ins like "bx_townhouse Buss Compressor").
        // With multi-instance, append " A"/" B"/etc. Trims to 16 chars.
        // Sub-mode 0x02 makes the header visible (default 0x00 hides
        // it); applied only when there's a name to show, falls back to
        // 0x00 otherwise so the regular Main layout returns.
        // Resolve via uf8::lookupPluginOnTrack so we get the uf8::PluginMap
        // (which carries `displayLong` — the UPPERCASE abbreviated name
        // like "BUS COMP 2" / "CHANNEL STRIP 2"). Falls back to the
        // built-in `match` string when displayLong is null (covers user-
        // mapped plug-ins until they get a Settings-side displayLong
        // editor). uc1::PluginBindings doesn't carry displayLong, so we
        // can't use bcBindings_/bindings directly here.
        std::string longName;
        void* longTrack = wantBc ? effectiveBcTrack_() : focusedTrack_;
        if (longTrack) {
            const auto domain = wantBc
                ? uf8::Domain::BusComp : uf8::Domain::ChannelStrip;
            auto pm = uf8::lookupPluginOnTrack(longTrack, domain);
            if (pm.map) {
                // User-rename precedence — see Frank 2026-05-20. If the
                // user has explicitly renamed the FX instance, that wins
                // over both displayLong and match.
                longName = reasixty_fxUserRename(longTrack, pm.fxIndex);
                if (longName.empty()) {
                    if (pm.map->displayLong && *pm.map->displayLong) {
                        longName = pm.map->displayLong;
                    } else if (pm.map->match && *pm.map->match) {
                        longName = pm.map->match;
                    }
                }
                // No multi-instance letter suffix on the LCD header
                // either — same rule as the central label (Frank
                // 2026-05-09).
                if (longName.size() > 16) longName.resize(16);
            }
        }
        // No buildLcdHeader(longName) here: SSL 360° in CS mode only
        // sends the 4-char buildCentralLabel via FF 66 05 01; the longer
        // "BUS COMP 2"/"CHANNEL STRIP 2" header writes to the same zone
        // 0x01 and would overwrite the area the SMALL triple (CS
        // carousel) occupies, hiding the carousel entirely.
        // No buildCentralMode(Main, 0x00) here either: that frame is
        // FF 66 03 00 01 00 — the sub=0x00 variant of the same
        // FF 66 03 00 01 X frame buildColourBarEnable already wrote
        // earlier as sub=0x01. Sending sub=0x00 from refresh()'s
        // steady-state path was the carousel-killer (uc1_47 t=1.4309s
        // confirms SSL fires sub=0x01 during Encoder-1 detents and
        // only drops to sub=0x00 ~3 s after scroll stops, not on every
        // tick).
        (void)longName;
    }

    // Carousel triple sends, in SSL 360°'s order: LARGE then SMALL.
    // Must come AFTER buildColourBarEnable + buildCentralLabel above
    // — uc1_47 capture order is layout-enable → label → LARGE → SMALL.
    // Skip the LARGE triple when the instance/FX carousel is active —
    // showInstanceCarousel + the instanceCarouselActive_ branch above
    // already pushed the plug-in-name triple into the same LCD slot.
    // Without this gate, refresh()'s BC-track-name triple clobbers the
    // carousel's plug-in names mid-scroll. Frank 2026-05-22.
    if (!instanceCarouselActive_) {
        device_->send(std::vector<uint8_t>(largeTriple));
    }
    device_->send(std::vector<uint8_t>(smallTriple));

    // Focused-track colour bar — single palette byte. Uses the same
    // quantizer as UF8's color-bar (uf8::quantize on the track's
    // 0xRRGGBB colour). When no plugin is loaded the bar is inactive
    // anyway (colour-bar-enable=0), but we still push a palette=0x00
    // to clear stale state.
    {
        uint8_t palette = 0x00;
        if (focusedTrack_) {
            MediaTrack* t = static_cast<MediaTrack*>(focusedTrack_);
            const uint32_t rgb = trackColorRgb(t);
            palette = (rgb == 0) ? 0x00 : uf8::quantize(rgb);
        }
        device_->send(buildFocusedColour(palette));
    }

    // Push each button's LED to mirror its current plugin-param state.
    // We walk the full button-ID range and ask the appropriate binding
    // for each one; kParamNone or no binding → LED off.
    MediaTrack* tr = static_cast<MediaTrack*>(focusedTrack_);
    const CascadeState cascade = computeCascade_(tr, bindings, bcTr_, bcBindings_);
    auto ledForParam = [&](const PluginBindings* map, int fxIdx, uint8_t btnId) {
        if (!map || !tr) return false;
        const int p = map->buttonParam[btnId];
        if (p == kParamNone) return false;
        const double v = TrackFX_GetParamNormalized(tr, fxIdx, p);
        return v >= 0.5;
    };
    auto stateFor = [&](uint8_t btn, bool on) -> LedState {
        if (!on) return LedState::Off;
        return buttonCascadeDim_(btn, cascade) ? LedState::Dim : LedState::On;
    };

    for (uint8_t btn = 0; btn < 0x20; ++btn) {
        const auto cell = cellForButton(btn);
        if (cell.bank == 0 && cell.cell == 0) continue;  // not an LED

        bool on = false;
        switch (classifyButton(btn)) {
            case ControlDomain::BusComp: {
                // BC IN LED reads from BC anchor (BC section pinned).
                bool bcOn = false;
                if (bcBindings_.busCompMap && bcTr_) {
                    if (bcBindings_.busCompMap->bypassParam != kParamNone) {
                        bcOn = !readPluginBypass_(bcTr_,
                            bcBindings_.busCompMap, bcBindings_.busCompFxIdx);
                    } else {
                        bcOn = TrackFX_GetEnabled(bcTr_, bcBindings_.busCompFxIdx);
                    }
                }
                pushButtonLed_(btn, bcOn);
                continue;
            }
            case ControlDomain::ChannelStrip:
                if (btn == button::kChannelIn) {
                    // ChannelIn LED — same logic, reads plug-in Bypass.
                    bool cin = false;
                    if (bindings.channelMap && tr) {
                        if (bindings.channelMap->bypassParam != kParamNone) {
                            cin = !readPluginBypass_(tr,
                                bindings.channelMap, bindings.channelFxIdx);
                        } else {
                            cin = TrackFX_GetEnabled(tr, bindings.channelFxIdx);
                        }
                    } else if (tr && TrackFX_GetCount(tr) > 0) {
                        cin = TrackFX_GetEnabled(tr, 0);
                    }
                    pushButtonLed_(btn, cin);
                    continue;
                } else {
                    on = ledForParam(bindings.channelMap,
                                     bindings.channelFxIdx, btn);
                }
                break;
        }

        // Fine tracks the surface's own modifier state, not a plugin param.
        if (btn == button::kFine) on = fineMode_.load(std::memory_order_relaxed);

        // Track-state LEDs: Solo/Cut/Polarity mirror the focused
        // track; Solo Clear lights when any track in the project is
        // soloed. Route through pushButtonLed_ so the per-button
        // state-encoding overrides (bank/state mappings for
        // Solo/Cut/SoloClear) apply — a direct buildLedWrite here
        // would use the wrong bank and leave stale LEDs lit after
        // track switches.
        if (btn == button::kSolo) {
            const int mirrored = reasixty_recUc1ButtonMirroredState(2, tr);
            const bool ledOn = (mirrored >= 0)
                ? (mirrored == 1)
                : (tr && GetMediaTrackInfo_Value(tr, "I_SOLO") > 0.5);
            pushButtonLed_(btn, ledOn);
            continue;
        }
        if (btn == button::kCut) {
            const int mirrored = reasixty_recUc1ButtonMirroredState(1, tr);
            const bool ledOn = (mirrored >= 0)
                ? (mirrored == 1)
                : (tr && GetMediaTrackInfo_Value(tr, "B_MUTE") > 0.5);
            pushButtonLed_(btn, ledOn);
            continue;
        }
        if (btn == button::kPolarity) {
            const int mirrored = reasixty_recUc1ButtonMirroredState(3, tr);
            const bool ledOn = (mirrored >= 0)
                ? (mirrored == 1)
                : (tr && GetMediaTrackInfo_Value(tr, "B_PHASE") > 0.5);
            pushButtonLed_(btn, ledOn);
            continue;
        }
        if (btn == button::kSoloClear) {
            bool anySolo = false;
            const int nTr = CountTracks(nullptr);
            for (int i = 0; i < nTr; ++i) {
                if (GetMediaTrackInfo_Value(GetTrack(nullptr, i), "I_SOLO") > 0.5) {
                    anySolo = true;
                    break;
                }
            }
            pushButtonLed_(btn, anySolo);
            continue;
        }

        // Same dim cascade as pollButtonLeds_ — route through the helper
        // so the dual-bank dim path applies; this fall-through case is
        // for buttons not in the dyn/eq groups but still plug-in params.
        pushButtonLed_(btn, stateFor(btn, on));
    }

    // BC GR zero-out moved into setBcAnchorTrack — only stale on anchor
    // change, not on every CS-focus refresh. Firing here on every refresh
    // pulsed the needle toward 0 each track-select event (UC1Device's
    // 50 Hz FF 5B stream then pulled it back to the live GR), producing
    // a visible wiggle on the BC mechanical meter even when the BC
    // anchor was unchanged. Frank 2026-05-07.

    // Push every mapped knob's LED ring immediately on focus change so
    // the rings reflect the current plugin state without waiting for
    // the user to actually move a knob. Reads the normalized VST3
    // value for each knob that has a ring mapping defined.
    //
    // No more knob exclusions: byte5 routing in pushKnobRing_ now
    // separates EQ/BC (byte5=0x00) from Dyn/Gate + BC Mix + Fader Level
    // (byte5=0x01), so cell numbers can collide with the 7-seg cells
    // without sharing physical LEDs.
    // CS-domain knob rings (EQ + DYN + Input Trim + Fader Level). Push
    // values when the focused track carries a CS plug-in (native, 360°
    // Link, or learned via UC1PluginMap); otherwise clear every CS ring
    // fully dark — Frank 2026-05-07: "wenn kein CS Plugin auf selected
    // channel sollen die LEDs von EQ und DYN Section komplett aus sein".
    for (uint8_t knobId = 0; knobId < 0x20; ++knobId) {
        if (classifyKnob(knobId) != ControlDomain::ChannelStrip) continue;
        if (tr && bindings.channelMap) {
            const int vst3Param = bindings.channelMap->knobParam[knobId];
            if (vst3Param == kParamNone) {
                clearKnobRing_(knobId);
                continue;
            }
            const double v = TrackFX_GetParamNormalized(
                tr, bindings.channelFxIdx, vst3Param);
            const double visual =
                bindings.channelMap->inverted[knobId] ? (1.0 - v) : v;
            pushKnobRing_(knobId, visual, knobCascadeDim_(knobId, cascade));
        } else {
            clearKnobRing_(knobId);
        }
    }
    // BC knob rings read from the BC anchor track (pinned independent
    // of CS focus). No anchor → clear every BC ring.
    for (uint8_t knobId = 0; knobId < 0x20; ++knobId) {
        if (classifyKnob(knobId) != ControlDomain::BusComp) continue;
        if (bcTr_ && bcBindings_.busCompMap) {
            const int vst3Param = bcBindings_.busCompMap->knobParam[knobId];
            if (vst3Param == kParamNone) {
                clearKnobRing_(knobId);
                continue;
            }
            const double v = TrackFX_GetParamNormalized(
                bcTr_, bcBindings_.busCompFxIdx, vst3Param);
            const double visual =
                bcBindings_.busCompMap->inverted[knobId] ? (1.0 - v) : v;
            pushKnobRing_(knobId, visual, knobCascadeDim_(knobId, cascade));
        } else {
            clearKnobRing_(knobId);
        }
    }

    // 7-segment position indicator — show the REAPER track number
    // (1-based) on the central red display. Push order doesn't matter
    // any more for safety (rings on byte5=0x01 won't touch this
    // address space), but kept here at the end for clarity.
    if (focusedTrack_) {
        int idx = static_cast<int>(GetMediaTrackInfo_Value(
            static_cast<MediaTrack*>(focusedTrack_), "IP_TRACKNUMBER"));
        if (idx < 0) idx = 0;
        for (const auto& frame : buildSevenSeg(static_cast<unsigned int>(idx))) {
            device_->send(frame);
        }
    }

    // Focused-param readout for the new track. The focus value itself
    // hasn't changed (track-switch leaves slotIdx + domain alone), but
    // the displayed value depends on the track's plug-in instance, so
    // recompute + push (dedup cache handles the text-equal case).
    pushFocusedParamReadout_();
}

void UC1Surface::pushGainReduction(float bcGrDb, float csCompGrDb, float csGateGrDb)
{
    if (!device_) return;
    // Bus Comp meter (FF 5B 02) — UC1Device streams this at 50 Hz on
    // its own; just update the cached value with the BC-side dB.
    device_->setGainReduction(bcGrDb);

    // Channel-Strip Dynamics GR LEDs.
    //   Comp GR strip: byte5=0x01, cells 0x5C..0x60  (5 LEDs)
    //   Gate GR strip: byte5=0x01, cells 0x61..0x65  (5 LEDs)
    //
    // Each cell needs a PAIRED write:
    //   bank=0x01 state=0x01 → mark cell as "active in GR group" (selection)
    //   bank=0x02 state=<brightness> → set brightness within the group
    // Without the bank=0x01 selection bit, the bank=0x02 brightness write
    // falls through to a different LED bank (lights the first-CCW LED of
    // Comp Release / Gate Range / Dyn In / Gate Hold). Same pair-write
    // rule as the bank=0x01-required status-register LEDs.
    //
    // Brightness: 6 visible steps per LED — {0x03, 0x19, 0x2D, 0x54,
    // 0x99, 0xFF}, captured from dual_35_cs_gr_ramp + uc1_36_gate_gr.
    // 5 LEDs × 6 sub-steps = 30 sub-steps over 20 dB Vollausschlag →
    // 0.667 dB per sub-step. Same sub-step index drives both the UC1
    // strip here AND the UF8 byte (computed in main.cpp), so the two
    // hardware meters stay synchronised at every dB level (Frank
    // 2026-05-07: capture verified UF8 byte 0x05 ↔ UC1 LED 0 full,
    // 0x18 ↔ LED 4 sub 3 — the byte mapping in main.cpp is now
    // derived from this 30-step sub-step rather than redundantly
    // computed from dB).
    static const uint8_t kLevels[6] = {0x03, 0x19, 0x2D, 0x54, 0x99, 0xFF};
    constexpr int   kSubsPerLed = 6;
    constexpr int   kSubsTotal  = 5 * kSubsPerLed;     // 30
    constexpr float kDbPerSub   = 20.0f / kSubsTotal;  // 0.667 dB

    // Piecewise dB → sub-step matching the SSL Native plug-in's GR
    // meter scale. Plug-in segments are at 3 / 6 / 10 / 14 / 20 dB —
    // so each LED covers a non-uniform dB range. Each LED still has
    // 6 sub-steps; the dB-per-sub varies per band.
    //   LED 0  0..3 dB    sub 0..6    (0.5 dB/sub)
    //   LED 1  3..6 dB    sub 6..12   (0.5 dB/sub)
    //   LED 2  6..10 dB   sub 12..18  (0.667 dB/sub)
    //   LED 3  10..14 dB  sub 18..24  (0.667 dB/sub)
    //   LED 4  14..20 dB  sub 24..30  (1.0 dB/sub)
    // At exactly 6 dB sub == 12 → LED 1 full + LED 2 entering, which
    // matches the plug-in's "6 dB full, 10 dB first step" Frank
    // 2026-05-07. Identical formula in main.cpp drives the UF8 byte.
    auto subStepFromDb = [&](float dB) -> int {
        if (dB <= 0) return 0;
        double s;
        if      (dB <=  3.0) s =       (dB        ) * (6.0 / 3.0);
        else if (dB <=  6.0) s =  6.0 + (dB -  3.0) * (6.0 / 3.0);
        else if (dB <= 10.0) s = 12.0 + (dB -  6.0) * (6.0 / 4.0);
        else if (dB <= 14.0) s = 18.0 + (dB - 10.0) * (6.0 / 4.0);
        else if (dB <= 20.0) s = 24.0 + (dB - 14.0) * (6.0 / 6.0);
        else                  s = static_cast<double>(kSubsTotal);
        int r = static_cast<int>(std::lround(s));
        if (r < 0)            r = 0;
        if (r > kSubsTotal)   r = kSubsTotal;
        return r;
    };
    auto stripTargets = [&](int subStep, uint8_t (&out)[5]) {
        if (subStep < 0) subStep = 0;
        if (subStep >= kSubsTotal) {
            for (auto& o : out) o = 0xFF;
            return;
        }
        const int active = subStep / kSubsPerLed;        // 0..4
        const int sub    = subStep % kSubsPerLed;        // 0..5
        for (int i = 0; i < 5; ++i) out[i] = 0;
        for (int i = 0; i < active; ++i) out[i] = 0xFF;
        out[active] = (subStep == 0) ? 0x00 : kLevels[sub];
    };

    // Match SSL360's exact pattern from dual_35 — counted across the
    // whole capture: bank=0x01 fires EXACTLY twice per cell (once at
    // activation, once at deactivation), never on brightness updates.
    // Repeated bank=0x01 state=0x01 emissions on every change destabilise
    // neighbour rings (Comp Release / Gate Range / Dyn In / Gate Hold
    // first-CCW LEDs flicker — user-observed 2026-04-28). Track selection
    // separately from brightness; emit bank=0x01 only on selection edge.
    //
    // Activation order matches dual_35:
    //   1. bank=0x02 cell <new> state=0x00   (preset to dark)
    //   2. bank=0x01 cell <new> state=0x01   (activate)
    //   3. bank=0x02 cell <previous> state=0xFF (lock previous to full)
    //   then ramping bank=0x02 brightness for the active edge.
    auto pushStrip = [&](uint8_t baseCell, const uint8_t (&target)[5],
                         uint8_t (&briCache)[5], uint8_t (&selCache)[5]) {
        bool anyChanged = false;
        for (int i = 0; i < 5; ++i) {
            if (target[i] != briCache[i]) { anyChanged = true; break; }
        }
        if (!anyChanged) return;

        // 1. Emit bank=0x01 ONLY for cells whose activation state changed.
        //    Activation: preset bank=0x02 to 0 first, then bank=0x01 to 1.
        //    Deactivation: just bank=0x01 to 0.
        for (int i = 0; i < 5; ++i) {
            const uint8_t cell = static_cast<uint8_t>(baseCell + i);
            const uint8_t newSel = target[i] ? 0x01 : 0x00;
            if (newSel != selCache[i]) {
                if (newSel == 0x01) {
                    device_->send(buildLedWrite(0x02, cell, 0x00));
                    device_->send(buildLedWrite(0x01, cell, 0x01));
                } else {
                    device_->send(buildLedWrite(0x01, cell, 0x00));
                }
                selCache[i] = newSel;
            }
        }
        // 2. Emit bank=0x02 brightness for every cell whose brightness
        //    changed AND every active cell that needs re-asserting after
        //    a sibling's activation. Simpler: re-emit brightness for all
        //    active cells on any change (matches SSL360's "lock previous
        //    to 0xFF" behaviour for free).
        for (int i = 0; i < 5; ++i) {
            const uint8_t cell = static_cast<uint8_t>(baseCell + i);
            if (target[i] != 0 || briCache[i] != target[i]) {
                device_->send(buildLedWrite(0x02, cell, target[i]));
            }
            briCache[i] = target[i];
        }
    };

    static uint8_t lastCompBri[5] = {0xFE, 0xFE, 0xFE, 0xFE, 0xFE};
    static uint8_t lastGateBri[5] = {0xFE, 0xFE, 0xFE, 0xFE, 0xFE};
    static uint8_t lastCompSel[5] = {0, 0, 0, 0, 0};
    static uint8_t lastGateSel[5] = {0, 0, 0, 0, 0};
    uint8_t compTarget[5];
    uint8_t gateTarget[5];
    stripTargets(subStepFromDb(csCompGrDb), compTarget);
    stripTargets(subStepFromDb(csGateGrDb), gateTarget);

    pushStrip(0x5C, compTarget, lastCompBri, lastCompSel);
    pushStrip(0x61, gateTarget, lastGateBri, lastGateSel);
}

void UC1Surface::pushVu(uint8_t meter, uint8_t level)
{
    if (!device_) return;
    device_->send(buildVuMeter(meter, level));
}

void UC1Surface::pushCsVu(float inputL, float inputR,
                          float outputL, float outputR)
{
    if (!device_) return;

    // BC-bypass cascade: when the focused track's BC plug-in is
    // bypassed, silence both meters.
    if (focusedTrack_) {
        UC1Bindings b = lookupBindingsOnTrack(focusedTrack_);
        if (b.busCompMap && b.busCompMap->bypassParam != kParamNone) {
            if (readPluginBypass_(
                    static_cast<MediaTrack*>(focusedTrack_),
                    b.busCompMap, b.busCompFxIdx)) {
                inputL = inputR = outputL = outputR = -120.f;
            }
        }
    }

    // Cell map decoded 2026-04-28 from `uc1_13_vu_meters.pcapng`.
    // Each meter is 16 LEDs tall × 2 cells per LED (L+R interleaved):
    //   LED i → L cell = base + 2*i, R cell = base + 2*i + 1
    //   Input  meter: byte5=0x00, base 0xA0  (cells 0xA0..0xBF, 32 total)
    //   Output meter: byte5=0x01, base 0x18  (cells 0x18..0x37, 32 total)
    // bank=0x01, state=0x01 lit / 0x00 off (binary, no brightness).
    //
    // SSL UC1 CS I/O meter LED scale, bottom-to-top, per user spec:
    //   -60, -50, -40, -30, -27, -24, -21, -18, -15, -12, -9, -6, -3, -2, -1, 0 dBFS
    // 16 LEDs, LED 0 = -60 dBFS, LED 15 = 0 dBFS (clip).
    constexpr int kNleds = 16;
    static constexpr float kThreshold[kNleds] = {
        -60.f, -50.f, -40.f, -30.f, -27.f, -24.f, -21.f, -18.f,
        -15.f, -12.f,  -9.f,  -6.f,  -3.f,  -2.f,  -1.f,   0.f,
    };

    constexpr uint8_t kInputBase  = 0xA0;  // byte5=0x00
    constexpr uint8_t kOutputBase = 0x18;  // byte5=0x01

    // Custom frame builder — byte5 is per-meter, can't use buildLedWrite
    // (which hardcodes byte5=0x01).
    auto sendVu = [&](uint8_t cell, uint8_t byte5, uint8_t state) {
        std::vector<uint8_t> f;
        f.reserve(8);
        f.push_back(0xFF);
        f.push_back(0x13);
        f.push_back(0x04);
        f.push_back(0x01);          // bank
        f.push_back(cell);
        f.push_back(byte5);
        f.push_back(state);
        uint32_t sum = 0;
        for (size_t k = 1; k < f.size(); ++k) sum += f[k];
        f.push_back(static_cast<uint8_t>(sum & 0xFF));
        device_->send(std::move(f));
    };

    auto pushMeter = [&](uint8_t base, uint8_t byte5,
                         float dbL, float dbR,
                         uint8_t (&lastL)[kNleds],
                         uint8_t (&lastR)[kNleds]) {
        // Each LED i lights independently per channel when that channel's
        // dB exceeds the LED's threshold. LED 0 (-60 dBFS) lights as
        // soon as audio is present.
        for (int i = 0; i < kNleds; ++i) {
            const uint8_t targetL = (dbL >= kThreshold[i]) ? 0x01 : 0x00;
            const uint8_t targetR = (dbR >= kThreshold[i]) ? 0x01 : 0x00;
            const uint8_t cellL = static_cast<uint8_t>(base + 2 * i);
            const uint8_t cellR = static_cast<uint8_t>(base + 2 * i + 1);
            if (lastL[i] != targetL) {
                lastL[i] = targetL;
                sendVu(cellL, byte5, targetL);
            }
            if (lastR[i] != targetR) {
                lastR[i] = targetR;
                sendVu(cellR, byte5, targetR);
            }
        }
    };

    static uint8_t lastInL[kNleds],  lastInR[kNleds];
    static uint8_t lastOutL[kNleds], lastOutR[kNleds];
    static bool initOnce = false;
    if (!initOnce) {
        for (int i = 0; i < kNleds; ++i) {
            lastInL[i] = lastInR[i] = lastOutL[i] = lastOutR[i] = 0xFE;
        }
        initOnce = true;
    }
    pushMeter(kInputBase,  0x00, inputL,  inputR,  lastInL,  lastInR);
    pushMeter(kOutputBase, 0x01, outputL, outputR, lastOutL, lastOutR);
}

} // namespace uc1
