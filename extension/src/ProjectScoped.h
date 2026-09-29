#pragma once
//
// ProjectScoped — state that lives in the project file, one copy per open tab.
//
// ⇨ WHY (forum, timothys_monster, 29.09.2026; Frank: "Should be session aware,
// of course"): a Selection Set saved in project A was gone after a detour
// through a new tab B. Every projectconfig hook kept ONE set of globals for all
// open projects: B's begin-load emptied them, the switch back to A loaded
// nothing, and saving A then wrote the empty state into A's .rpp as well.
//
// The rest of the code keeps reading its globals (the "live" state), which
// always belong to the active tab, the `owner`. Every other open tab has a
// parked copy here. `swapLive` exchanges the live globals with a State, so the
// hooks' own bodies stay as they are:
//   - with(p, body): a load or save of project p (GetCurrentProjectInLoadSave)
//     runs `body` on p's state: the live one if p is the owner, else p's parked
//     copy, swapped in for the duration.
//   - switchTo(p):   the active tab changed; the live state is parked under the
//     old owner (if that tab is still open) and p's parked copy goes live.
//   - prune():       closed tabs lose their copy, so a new project that gets a
//     closed one's address starts empty.
//
// A State{} must equal what the hook's begin-load resets to: it is what a tab
// that never loaded anything (a new empty project) shows.
//
// Pure and header-only, no REAPER: the key is any comparable type, "is this
// project still open" comes from the caller, so tests drive it with ints.
// Main thread only, like the projectconfig callbacks and onTimer.
//
#include <map>
#include <utility>

namespace reasixty {

template <class Key, class State>
class ProjectScoped {
public:
    using SwapLive = void (*)(State&);

    explicit ProjectScoped(SwapLive swapLive) : swapLive_(swapLive) {}

    template <class Body>
    void with(Key p, Body&& body)
    {
        if (hasOwner_ && p == owner_) { body(); return; }
        State& st = parked_[p];
        swapLive_(st);
        body();
        swapLive_(st);
    }

    // Returns true when the live state changed hands.
    template <class IsOpen>
    bool switchTo(Key p, IsOpen&& isOpen)
    {
        if (hasOwner_ && p == owner_) return false;
        State incoming{};
        auto it = parked_.find(p);
        if (it != parked_.end()) {
            incoming = std::move(it->second);
            parked_.erase(it);
        }
        swapLive_(incoming);   // live = p's state, incoming = the old owner's
        if (hasOwner_ && isOpen(owner_)) parked_[owner_] = std::move(incoming);
        owner_ = p;
        hasOwner_ = true;
        prune(isOpen);
        return true;
    }

    template <class IsOpen>
    void prune(IsOpen&& isOpen)
    {
        for (auto it = parked_.begin(); it != parked_.end();)
            it = isOpen(it->first) ? std::next(it) : parked_.erase(it);
    }

    std::size_t parkedCount() const { return parked_.size(); }

private:
    SwapLive swapLive_;
    std::map<Key, State> parked_;
    Key owner_{};
    bool hasOwner_ = false;
};

}  // namespace reasixty
