#include "MixerLayout.h"
#include "ThemeAssets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>

#include "reaper_plugin_functions.h"
#include "reaper_imgui_functions.h"

namespace uf8 {

namespace {

// Strip dimensions — tuned to fit ~14 strips in the 1600 px default
// window width with horizontal scrolling for more. Mirrors SSL 360°'s
// channel-strip proportions (tall + narrow).
constexpr double kStripW         = 96.0;
constexpr double kStripGap       = 4.0;
constexpr double kColorBarH      = 6.0;
constexpr double kHeaderH        = 40.0;
constexpr double kFaderColumnH   = 360.0;
constexpr double kFaderW         = 28.0;
constexpr double kMeterW         = 18.0;
constexpr double kFaderGap       = 6.0;   // between fader and meter
constexpr double kBtnRowH        = 22.0;

// REAPER COLORREF (0x00BBGGRR, R at byte 0) → ImGui RGBA (0xRRGGBBAA).
inline int rgbaFromReaperColor(int reaperColor, int alpha = 0xFF)
{
    const int r =  reaperColor        & 0xFF;
    const int g = (reaperColor >>  8) & 0xFF;
    const int b = (reaperColor >> 16) & 0xFF;
    return (r << 24) | (g << 16) | (b << 8) | (alpha & 0xFF);
}

// REAPER's UI fader uses a perceptual taper: slider 0..1000-ish maps to
// dB via SLIDER2DB / DB2SLIDER. Volume in REAPER is stored linearly
// (vol=1.0 == 0 dB), so for visual fader position we want slider/maxSlider.
// kMaxFaderDb = +12 matches REAPER's default Track Max Volume preference.
constexpr double kMaxFaderDb = 12.0;
constexpr double kMinFaderDb = -60.0;

// Convert linear volume (REAPER native unit) → normalised fader position
// (0.0 = bottom, 1.0 = top, where top corresponds to kMaxFaderDb).
double volToFaderPos(double linVol)
{
    if (linVol <= 0.0) return 0.0;
    const double db = 20.0 * std::log10(linVol);
    const double sl = DB2SLIDER(db);
    const double slTop = DB2SLIDER(kMaxFaderDb);
    if (slTop <= 0.0) return 0.0;
    return std::clamp(sl / slTop, 0.0, 1.0);
}

// Inverse — for fader interaction. pos: 0..1, returns linear volume.
double faderPosToVol(double pos)
{
    pos = std::clamp(pos, 0.0, 1.0);
    const double slTop = DB2SLIDER(kMaxFaderDb);
    const double sl    = pos * slTop;
    const double db    = SLIDER2DB(sl);
    return std::pow(10.0, db / 20.0);
}

// Linear peak → dBFS. Used for both numeric readouts and meter scaling.
double peakToDb(double peak)
{
    if (peak <= 1e-9) return -120.0;
    return 20.0 * std::log10(peak);
}

// dB → vertical fill fraction for the meter (0..1, 0 = bottom).
// Maps the same range as the fader so peak = top corresponds to peak
// near +12 dB — close enough to REAPER's MCP meter visually.
double dbToMeterFrac(double db)
{
    constexpr double kMeterTopDb    = 6.0;
    constexpr double kMeterBottomDb = -60.0;
    return std::clamp(
        (db - kMeterBottomDb) / (kMeterTopDb - kMeterBottomDb),
        0.0, 1.0);
}

// Draw a single track strip starting at the current cursor position.
// Pulls all dynamic state straight from the REAPER API each frame —
// no caching, no diffing. Cheap on the scale of dozens of tracks.
void drawStrip_(ImGui_Context* ctx, MediaTrack* tr, int trackIdx)
{
    if (!tr) return;

    // -- PushID so identical widget labels across strips don't collide.
    char idBuf[24];
    std::snprintf(idBuf, sizeof(idBuf), "strip%d", trackIdx);
    ImGui_PushID(ctx, idBuf);

    double childW = kStripW;
    double childH = kColorBarH + kHeaderH + kFaderColumnH + kBtnRowH * 3 + 24;
    int childFlags = ImGui_ChildFlags_Borders;
    if (ImGui_BeginChild(ctx, idBuf, &childW, &childH, &childFlags,
                        /*window_flags*/ nullptr)) {

        ImGui_DrawList* dl = ImGui_GetWindowDrawList(ctx);

        // ------------------------------------------------------------------
        // Track-colour bar — top stripe filled with GetTrackColor.
        // ------------------------------------------------------------------
        {
            double sx = 0, sy = 0;
            ImGui_GetCursorScreenPos(ctx, &sx, &sy);
            const int rawColor = GetTrackColor(tr);
            int rgba;
            if ((rawColor & 0x01000000) && (rawColor & 0x00FFFFFF)) {
                rgba = rgbaFromReaperColor(rawColor);
            } else {
                rgba = 0x404448FF;  // dim grey for tracks with no custom colour
            }
            if (dl) {
                ImGui_DrawList_AddRectFilled(
                    dl, sx, sy, sx + kStripW - 8, sy + kColorBarH,
                    rgba, /*rounding*/ nullptr, /*flags*/ nullptr);
            }
            ImGui_Dummy(ctx, kStripW - 8, kColorBarH);
        }

        // ------------------------------------------------------------------
        // Header — track number + name. Truncated to fit strip width.
        // ------------------------------------------------------------------
        {
            char name[256] = {};
            (void)GetSetMediaTrackInfo_String(
                tr, "P_NAME", name, /*setNewValue*/ false);
            const int trackNumber = (int)GetMediaTrackInfo_Value(
                tr, "IP_TRACKNUMBER");
            char hdr1[32];
            std::snprintf(hdr1, sizeof(hdr1), "%d", trackNumber);
            ImGui_Text(ctx, hdr1);
            // Truncate display name to ~14 chars to fit the strip width.
            char nameDisp[24];
            if (name[0]) {
                std::snprintf(nameDisp, sizeof(nameDisp), "%.14s", name);
            } else {
                std::snprintf(nameDisp, sizeof(nameDisp), "(no name)");
            }
            ImGui_Text(ctx, nameDisp);
        }

        ImGui_Separator(ctx);

        // ------------------------------------------------------------------
        // Volume readout (current dB).
        // ------------------------------------------------------------------
        double linVol = 1.0, pan = 0.0;
        GetTrackUIVolPan(tr, &linVol, &pan);
        const double curDb = (linVol <= 0.0) ? -120.0 : 20.0 * std::log10(linVol);
        {
            char vbuf[24];
            if (curDb <= -100.0)
                std::snprintf(vbuf, sizeof(vbuf), "-inf");
            else
                std::snprintf(vbuf, sizeof(vbuf), "%+.1f dB", curDb);
            ImGui_Text(ctx, vbuf);
        }

        // ------------------------------------------------------------------
        // Fader + meter region — invisible button captures drag, draw-list
        // primitives render the visual. Using direct draw rather than
        // ImGui_SliderDouble because we want a vertical fader with a
        // theme-aware sprite handle (Phase C+).
        // ------------------------------------------------------------------
        {
            double sx = 0, sy = 0;
            ImGui_GetCursorScreenPos(ctx, &sx, &sy);

            const double regionW = kFaderW + kFaderGap + kMeterW;
            const double regionH = kFaderColumnH;

            const double faderX0 = sx;
            const double faderX1 = sx + kFaderW;
            const double faderY0 = sy;
            const double faderY1 = sy + regionH;
            const double meterX0 = faderX1 + kFaderGap;
            const double meterX1 = meterX0 + kMeterW;

            // ----- Backgrounds first (drawn behind interactive surface) ---
            if (dl) {
                ImGui_DrawList_AddRectFilled(
                    dl, faderX0, faderY0, faderX1, faderY1,
                    0x202530FF, /*rounding*/ nullptr, /*flags*/ nullptr);
                ImGui_DrawList_AddRectFilled(
                    dl, meterX0, faderY0, meterX1, faderY1,
                    0x181C22FF, /*rounding*/ nullptr, /*flags*/ nullptr);
            }

            // ----- Peak meter columns -------------------------------------
            const int nch = (int)GetMediaTrackInfo_Value(tr, "I_NCHAN");
            const int useCh = std::clamp(nch, 1, 2);
            const double colW = (useCh == 1) ? kMeterW : kMeterW * 0.5;
            for (int ch = 0; ch < useCh; ++ch) {
                const double peak = Track_GetPeakInfo(tr, ch);
                const double db   = peakToDb(peak);
                const double frac = dbToMeterFrac(db);
                const double colX0 = meterX0 + ch * colW;
                const double colX1 = colX0 + colW - (useCh > 1 ? 1.0 : 0.0);
                const double fillY = faderY1 - frac * regionH;
                int meterColor;
                if (db >= 0.0)       meterColor = 0xE03030FF;  // red above 0 dBFS
                else if (db >= -6.0) meterColor = 0xE0C040FF;  // yellow -6..0
                else                 meterColor = 0x40C040FF;  // green default
                if (dl && frac > 0.001) {
                    ImGui_DrawList_AddRectFilled(
                        dl, colX0, fillY, colX1, faderY1,
                        meterColor, /*rounding*/ nullptr, /*flags*/ nullptr);
                }
            }

            // ----- Hit region: invisible button over the fader column -----
            ImGui_SetCursorScreenPos(ctx, faderX0, faderY0);
            (void)ImGui_InvisibleButton(ctx, "fader_drag",
                                        kFaderW, regionH,
                                        /*flagsInOptional*/ nullptr);
            const bool active = ImGui_IsItemActive(ctx);
            const bool hovered = ImGui_IsItemHovered(ctx,
                                                    /*flagsInOptional*/ nullptr);

            if (active) {
                double mx = 0, my = 0;
                ImGui_GetMousePos(ctx, &mx, &my);
                const double newPos = std::clamp(
                    (faderY1 - my) / regionH, 0.0, 1.0);
                const double newVol = faderPosToVol(newPos);
                if (std::fabs(newVol - linVol) > 1e-6) {
                    CSurf_OnVolumeChange(tr, newVol, /*relative*/ false);
                }
            }
            if (hovered && ImGui_IsMouseDoubleClicked(ctx, /*button*/ 0)) {
                CSurf_OnVolumeChange(tr, 1.0, /*relative*/ false);
            }

            // ----- Handle on top of everything ----------------------------
            const double pos = volToFaderPos(linVol);
            const double handleY = faderY1 - pos * regionH;
            constexpr double kHandleH = 12.0;
            if (dl) {
                ImGui_DrawList_AddRectFilled(
                    dl, faderX0 - 2, handleY - kHandleH * 0.5,
                    faderX1 + 2, handleY + kHandleH * 0.5,
                    0xD0D4DAFF, /*rounding*/ nullptr, /*flags*/ nullptr);
            }

            // Cursor: place at bottom of region for the next widget row.
            ImGui_SetCursorScreenPos(ctx, sx, sy + regionH);
            (void)regionW;  // silence unused (reserved for future meter slot)
        }

        ImGui_Separator(ctx);

        // ------------------------------------------------------------------
        // Pan readout (no edit handle yet — Phase C polish).
        // ------------------------------------------------------------------
        {
            char pbuf[24];
            if (std::fabs(pan) < 0.005)
                std::snprintf(pbuf, sizeof(pbuf), "C");
            else if (pan < 0.0)
                std::snprintf(pbuf, sizeof(pbuf), "L%d", (int)std::round(-pan * 100));
            else
                std::snprintf(pbuf, sizeof(pbuf), "R%d", (int)std::round( pan * 100));
            ImGui_Text(ctx, pbuf);
        }

        // ------------------------------------------------------------------
        // Solo / Mute / Arm buttons.
        // ------------------------------------------------------------------
        {
            const bool isSolo = GetMediaTrackInfo_Value(tr, "I_SOLO")  != 0.0;
            const bool isMute = GetMediaTrackInfo_Value(tr, "B_MUTE")  != 0.0;
            const bool isArm  = GetMediaTrackInfo_Value(tr, "I_RECARM") != 0.0;

            double bw = kStripW - 16;
            double bh = kBtnRowH;

            // Solo button — coloured backdrop when active.
            const int oldBtnColIdx = ImGui_Col_Button;
            int pushed = 0;
            auto pushIf = [&](bool on, int rgba) {
                if (on) {
                    ImGui_PushStyleColor(ctx, oldBtnColIdx, rgba);
                    ++pushed;
                }
            };
            pushIf(isSolo, 0xE0C040FF);   // yellow when soloed
            if (ImGui_Button(ctx, "Solo", &bw, &bh)) {
                CSurf_OnSoloChange(tr, /*toggle*/ -1);
            }
            if (pushed > 0) { ImGui_PopStyleColor(ctx, &pushed); pushed = 0; }

            pushIf(isMute, 0xE05050FF);   // red when muted
            if (ImGui_Button(ctx, "Mute", &bw, &bh)) {
                CSurf_OnMuteChange(tr, /*toggle*/ -1);
            }
            if (pushed > 0) { ImGui_PopStyleColor(ctx, &pushed); pushed = 0; }

            pushIf(isArm,  0xE03030FF);   // red when armed
            if (ImGui_Button(ctx, "Arm", &bw, &bh)) {
                CSurf_OnRecArmChange(tr, /*toggle*/ -1);
            }
            if (pushed > 0) { ImGui_PopStyleColor(ctx, &pushed); pushed = 0; }
        }
    }
    ImGui_EndChild(ctx);

    ImGui_PopID(ctx);
}

} // namespace

void MixerLayout::draw(ImGui_Context* ctx)
{
    // Drive the theme asset cache so PNG handles are valid this frame.
    theme_assets::tick(ctx);

    // Horizontal-scrolling region that hosts the strip row.
    int childFlags  = ImGui_ChildFlags_None;
    int windowFlags = ImGui_WindowFlags_HorizontalScrollbar;
    if (ImGui_BeginChild(ctx, "mixer_strips",
                         /*size_w*/ nullptr, /*size_h*/ nullptr,
                         &childFlags, &windowFlags)) {
        const int n = CountTracks(nullptr);
        for (int i = 0; i < n; ++i) {
            MediaTrack* tr = GetTrack(nullptr, i);
            if (!tr) continue;
            if (i > 0) ImGui_SameLine(ctx,
                                      /*offset_from_start_x*/ nullptr,
                                      /*spacing*/ nullptr);
            drawStrip_(ctx, tr, i);
        }

        if (n == 0) {
            ImGui_Text(ctx, "(no tracks in project)");
        }
    }
    ImGui_EndChild(ctx);
}

} // namespace uf8
