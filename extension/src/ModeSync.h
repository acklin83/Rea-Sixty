#pragma once
//
// ⇨ HUE, DYNAMOUNT AND OBS KEPT IN STEP BETWEEN ORC AND REA-SIXTY (Frank
// 29.10.2026: "können wir hue, dyna synchron halten"). Each program keeps its
// own copy (ORC its files, Rea-Sixty REAPER's ExtState), so each works alone,
// whatever is installed. They meet at the handover, the one moment the other
// program takes the surfaces: ORC puts its values into handover.released,
// Rea-Sixty its own into handover.returned, and the receiver takes every value
// that is newer on the other side.
//
// "Newer" is a time stamp per value (ModeSettings::kImportKeys), set when the
// value changes in its program, whoever changed it (a settings page, the
// bridge pairing, a stored preset): the Stamper compares what it saw last.
// A value nobody has changed since this came in has no stamp; when neither side
// has one, ORC's wins (Frank 29.10.2026: "beim ersten Mal gewinnt ORC").
//
// The lines ride below the receipt's own (OrcHandover.h): "M\t<key>\t<stamp>\t<value>".
// A value holds no newline (ExtState cuts there); tabs it may hold.
// Test: tests/test_mode_sync.cpp.

#include "ModeSettings.h"

#include <chrono>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace modesync {

using Ms = long long;   // ms since the epoch, the wall clock both programs share

inline Ms nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

struct Value {
    std::string key;
    Ms          stamp = 0;   // 0 = never changed since stamps came in
    std::string value;
};

inline std::string format(const std::vector<Value>& vs)
{
    std::string out;
    for (const auto& v : vs) {
        if (v.value.find('\n') != std::string::npos) continue;
        out += "M\t" + v.key + "\t" + std::to_string(v.stamp) + "\t" + v.value + "\n";
    }
    return out;
}

// Every "M\t" line of a handover file; other lines are not ours.
inline std::vector<Value> parse(const std::string& text)
{
    std::vector<Value> out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("M\t", 0) != 0) continue;
        const std::size_t a = line.find('\t', 2);
        if (a == std::string::npos) continue;
        const std::size_t b = line.find('\t', a + 1);
        if (b == std::string::npos) continue;
        Value v;
        v.key   = line.substr(2, a - 2);
        v.stamp = std::atoll(line.substr(a + 1, b - a - 1).c_str());
        v.value = line.substr(b + 1);
        if (!v.key.empty()) out.push_back(v);
    }
    return out;
}

// Does the incoming value replace mine? Newer wins; with no stamp on either
// side ORC's does. An empty value never wipes one that is there.
inline bool takeIncoming(Ms mine, Ms theirs, const std::string& incoming, bool incomingIsOrc)
{
    if (incoming.empty()) return false;
    if (theirs > mine) return true;
    return theirs == 0 && mine == 0 && incomingIsOrc;
}

using Get      = std::function<std::string(const char* key)>;
using GetStamp = std::function<Ms(const char* key)>;
using SetStamp = std::function<void(const char* key, Ms stamp)>;

// The values worth sending: every key that has one, with its stamp.
inline std::vector<Value> collect(const Get& get, const GetStamp& stamp)
{
    std::vector<Value> out;
    for (const char* key : modesettings::kImportKeys) {
        Value v{ key, stamp(key), get(key) };
        if (!v.value.empty()) out.push_back(v);
    }
    return out;
}

// Stamps a value when it changes. prime() takes what is there now as seen
// without stamping it: at start, and after a handover brought values in (they
// carry their own stamp).
struct Stamper {
    std::map<std::string, std::string> seen;
    bool primed = false;

    void prime(const Get& get)
    {
        for (const char* key : modesettings::kImportKeys) seen[key] = get(key);
        primed = true;
    }
    void tick(const Get& get, const SetStamp& set, Ms now)
    {
        if (!primed) { prime(get); return; }
        for (const char* key : modesettings::kImportKeys) {
            std::string v = get(key);
            auto& last = seen[key];
            if (v == last) continue;
            last = std::move(v);
            set(key, now);
        }
    }
};

} // namespace modesync
