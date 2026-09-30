#pragma once
//
// VcaSpill: which tracks a VCA lead spills onto the surface, and how the chain
// of spilled leads moves when SEL is long-pressed. Pure logic; main.cpp reads
// REAPER's group bits and owns the state, tests/test_vca_spill.cpp a table.
//
// REAPER (User Guide 7.81, 5.16/5.18): a track is a VCA lead of every group in
// its VOLUME_VCA_LEAD bits and follows every group in its VOLUME_VCA_FOLLOW
// bits. Leads chain (lead in one group, follower in another) and followers may
// sit in several VCAs at once. 128 groups since REAPER 7.23.
//
// The chain, outermost lead first (Frank 30.09.2026, plan in
// .local-docs/vca-spill-plan.md). Long SEL on:
//   - a lead while nothing is spilled        spill it
//   - a member of the deepest lead that is itself a lead   one level deeper
//   - the outermost lead                      leave the spill
//   - the deepest lead (not the outermost)    back one level
//   - a lead in between                       back to that level
//   - any other lead                          spill it instead (new chain)
//   - anything else                           nothing
//
#include <algorithm>
#include <cstdint>
#include <vector>

namespace vca {

struct Groups {
    uint32_t w[4] = {0, 0, 0, 0};    // groups 1..32, 33..64, 65..96, 97..128
    bool any() const { return (w[0] | w[1] | w[2] | w[3]) != 0; }
    bool meets(const Groups& o) const
    {
        return ((w[0] & o.w[0]) | (w[1] & o.w[1]) | (w[2] & o.w[2]) | (w[3] & o.w[3])) != 0;
    }
    bool has(int group1to128) const
    {
        if (group1to128 < 1 || group1to128 > 128) return false;
        const int i = group1to128 - 1;
        return (w[i / 32] >> (i % 32)) & 1u;
    }
};

// `t` follows `lead` through at least one of lead's VCA groups.
template <class T, class LeadOf, class FollowOf>
bool follows(T t, T lead, LeadOf leadOf, FollowOf followOf)
{
    return t != lead && followOf(t).meets(leadOf(lead));
}

// Followers of `lead`, in the order of `all` (track order), lead excluded.
template <class T, class LeadOf, class FollowOf>
std::vector<T> members(const std::vector<T>& all, T lead, LeadOf leadOf, FollowOf followOf)
{
    std::vector<T> out;
    for (T t : all)
        if (follows(t, lead, leadOf, followOf)) out.push_back(t);
    return out;
}

// Leads that follow no other lead: what VCA Mode shows. A chained lead (lead in
// one group, follower of another lead) is reached by spilling its parent.
template <class T, class LeadOf, class FollowOf>
std::vector<T> topLeads(const std::vector<T>& all, LeadOf leadOf, FollowOf followOf)
{
    std::vector<T> leads;
    for (T t : all)
        if (leadOf(t).any()) leads.push_back(t);
    std::vector<T> out;
    for (T t : leads) {
        bool underAnother = false;
        for (T l : leads)
            if (follows(t, l, leadOf, followOf)) { underAnother = true; break; }
        if (!underAnother) out.push_back(t);
    }
    return out;
}

enum class Step { None, Enter, Deeper, Back, Jump, Exit };

// Long SEL (or the "spill selected" action) on `t`. `isLead`: t leads a VCA
// group. `followsDeepest`: t follows the deepest spilled lead.
template <class T>
Step press(std::vector<T>& chain, T t, bool isLead, bool followsDeepest)
{
    const auto it = std::find(chain.begin(), chain.end(), t);
    if (it != chain.end()) {
        const size_t i = static_cast<size_t>(it - chain.begin());
        if (i == 0) { chain.clear(); return Step::Exit; }
        if (i + 1 == chain.size()) { chain.pop_back(); return Step::Back; }
        chain.resize(i + 1);
        return Step::Jump;
    }
    if (!isLead) return Step::None;
    if (!chain.empty() && followsDeepest) { chain.push_back(t); return Step::Deeper; }
    chain.assign(1, t);
    return Step::Enter;
}

// Cut the chain at the first lead that no longer exists or no longer leads.
// Returns true when something was cut.
template <class T, class Valid>
bool prune(std::vector<T>& chain, Valid valid)
{
    for (size_t i = 0; i < chain.size(); ++i)
        if (!valid(chain[i])) { chain.resize(i); return true; }
    return false;
}

} // namespace vca
