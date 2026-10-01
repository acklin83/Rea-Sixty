#pragma once
//
// SelsetBank: the "Selection Sets" dynamic soft-key bank (Frank 01.10.2026, plan
// docs/selset-dynamic-bank-plan.md). Key N is Selection Set slot N, on the UF8's eight
// top keys and on the UF1's four (two pages). Pure logic; main.cpp feeds it the slot
// and runs the op through the existing selset paths, tests/test_selset_bank.cpp a table.
//
// Recall is the surface FILTER, as selset_recall has always been; selecting the tracks
// in REAPER is its own gesture. Destructive gestures never sit on the one you trigger
// by accident: a long press only saves into an EMPTY slot.
//
#include <string>

namespace selbank {

struct Slot {
    bool        populated = false;   // name, tracks, or a group binding
    bool        group     = false;   // Group slot (follows a REAPER track group)
    int         groupIdx  = 0;       // 1..128 when group
    bool        active    = false;   // the recalled slot (filter on)
    std::string name;
};

struct Key {
    std::string label;
    bool        present = false;     // false = dark key
    int         led     = 0;         // 0 off, 1 dim, 2 on (DynSlotInfo)
};

// slot1to8 only names an unnamed slot ("Set 3").
inline Key key(const Slot& s, int slot1to8)
{
    Key k;
    if (!s.populated) return k;
    k.present = true;
    k.led     = s.active ? 2 : 1;
    if (!s.name.empty())  k.label = s.name;
    else if (s.group)     k.label = "Grp " + std::to_string(s.groupIdx);
    else                  k.label = "Set " + std::to_string(slot1to8);
    return k;
}

enum class Op { None, Recall, Save, Select, Clear };

// Dynamic-bank gesture codes: 0 Plain, 1 Shift, 2 Cmd, 3 Ctrl, 4 long press.
inline Op op(int gesture, bool populated)
{
    switch (gesture) {
        case 0: return populated ? Op::Recall : Op::None;
        case 4: return populated ? Op::None : Op::Save;   // never overwrite by holding
        case 1: return Op::Save;                          // Shift overwrites
        case 2: return populated ? Op::Select : Op::None;
        case 3: return populated ? Op::Clear : Op::None;
        default: return Op::None;
    }
}

} // namespace selbank
