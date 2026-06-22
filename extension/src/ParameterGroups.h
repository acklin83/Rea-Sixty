#pragma once
//
// ParameterGroups — multi-track parameter-sync feature ("8 persistent
// groups + temp-group from selection"). Inspired by Selection Sets but
// dedicated to fanning out hardware-originated parameter writes onto
// every member of any active group.
//
// Membership: per-track 8-bit mask stored in REAPER's
// P_EXT:reasixty:pg_mask (survives save/load natively, no chunk patch).
// Bit N (0..7) set ⇔ track is a member of group N.
//
// Group meta (name + active flag) + the global "multi-select as temp
// group" toggle persist to <REAPER_RESOURCE>/rea_sixty/parameter_groups.json.
//
// Phase 1 scope: only PluginMap-recognised plug-ins broadcast (SSL CS /
// BC built-in maps + user FX-Learn maps). Unmapped third-party plug-ins
// are silent-skipped; FX-name-match broadcast is a follow-up phase.
//

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "FocusedParam.h"           // uf8::Domain

// Must match reaper_plugin.h's `class MediaTrack;` forward declaration
// or MSVC name-mangles function pointers with this type differently
// from the SDK's, producing LNK2019 with `class MediaTrack *` vs
// `struct MediaTrack *` mismatches.
class MediaTrack;

namespace uf8 {

struct UserPluginMap;                // fwd from UserPluginCatalog.h

namespace param_groups {

constexpr int kSlotCount = 8;

struct SlotMeta {
    std::string name;
    bool        active = false;
};

struct State {
    std::array<SlotMeta, kSlotCount> slots{};
    bool                             multiSelectAsTempGroup = true;
};

State& state();

// Per-track bitmask accessors (read/write P_EXT). bit N = group N member.
uint8_t getMaskForTrack(MediaTrack* tr);
void    setMaskForTrack(MediaTrack* tr, uint8_t mask);

// Add every currently-selected track to group `slotIdx`. No-op when
// slotIdx is out of range.
void addSelectedToGroup(int slotIdx);

// Remove every currently-selected track from all groups.
void removeSelectedFromAllGroups();

// Wipe membership of group `slotIdx` across every track in the project.
void clearGroupMembership(int slotIdx);

// Toggle the `active` flag (persists via save()).
void toggleGroupActive(int slotIdx);
bool isGroupActive(int slotIdx);

// Multi-select fallback (Settings checkbox).
bool multiSelectAsTempGroup();
void setMultiSelectAsTempGroup(bool on);

// Returns the set of member tracks (excluding `leader`) that should
// mirror a write originated on `leader`:
//   - Any persistent group active: union of all members of every active
//     group (silently dedup; leader excluded).
//   - Else, if multiSelectAsTempGroup is on and leader is part of a
//     selection of ≥2 tracks: the other selected tracks.
//   - Else: empty.
std::vector<MediaTrack*> resolveBroadcastTargets(MediaTrack* leader);

// ---- Broadcast helpers ----------------------------------------------------
//
// Call AFTER the leader's TrackFX_SetParamNormalized / SetMediaTrackInfo_*
// completes. The helper is a no-op when resolveBroadcastTargets returns
// empty.

// SSL CS / BC built-in PluginMap path. Members resolved via
// lookupPluginOnTrack(domain) + findSlotByLinkIdx(slotLinkIdx). Member
// silently skipped when no matching plug-in / slot is present.
void broadcastBuiltinSlot(MediaTrack* leader,
                          Domain domain,
                          int slotLinkIdx,
                          double normValue);

// User-FX-Learn (UserPluginMap) path. Members must host the SAME user
// map (substring match against `leaderMap->match` on FX name). vst3Param
// is taken 1:1 because matching FX share the VST3 param layout.
void broadcastUserParam(MediaTrack* leader,
                        const UserPluginMap* leaderMap,
                        int vst3Param,
                        double normValue);

// Track attribute (e.g. "B_PHASE"). Writes the same numeric `value` to
// every target track via SetMediaTrackInfo_Value.
void broadcastTrackBool(MediaTrack* leader,
                        const char* attrName,
                        double value);

// Solo / Mute fan-out. Targets get the leader's *absolute* new state via
// CSurf_OnSoloChange / CSurf_OnMuteChange (not -1 toggle) so a mixed
// selection lands at a uniform state. Call after the leader's own
// CSurf_OnSoloChange(-1) / CSurf_OnMuteChange(-1) has settled and the
// new state has been read back.
void broadcastSoloMute(MediaTrack* leader, bool isSolo, int absoluteValue);

// Track volume is intentionally NOT broadcast: REAPER gangs the faders of
// multiple selected tracks natively (relative, offset-preserving). A
// fan-out here only collapsed a mixed-level selection onto the leader's
// absolute level (Frank 2026-06-09). Groups carry mute/solo + CS/BC only.

// True when the broadcast helpers are currently executing (member
// writes in flight). The REAPER CSURF_EXT_SETFXPARAM hook checks this
// to avoid re-broadcasting our own writes when they round-trip back
// through REAPER's param-change callback.
bool inBroadcast();

// Raise / lower the broadcast-suppression depth around a batch of programmatic
// FX-param writes (e.g. CS-Switch value transfer) so the CSURF_EXT_SETFXPARAM
// hook doesn't fan those writes out to group members. Balanced push/pop.
void pushBroadcastSuppress();
void popBroadcastSuppress();

// Wall-clock milliseconds since a broadcast finished writing members.
// chaseLastTouchedFx uses this to suppress UC1 focus jumps while
// broadcast member writes are still propagating through REAPER's
// last-touched tracking. Returns INT64_MAX when no broadcast has run.
int64_t millisSinceLastBroadcast();

// Persistence (JSON sidecar). Safe to call multiple times.
void load();
void save();

// Absolute path where parameter_groups.json is persisted. Exposed
// for the setup-bundle module's direct file I/O.
std::string configPath();

} // namespace param_groups
} // namespace uf8
