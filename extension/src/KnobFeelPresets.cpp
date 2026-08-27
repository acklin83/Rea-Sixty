#include "KnobFeelPresets.h"

#include <cstdlib>
#include <cstring>
#include <ostream>
#include <sstream>

#include "reaper_plugin_functions.h"   // GetExtState / SetExtState
#include "WDL/jsonparse.h"
#include "JsonTree.h"

namespace uf8 {
namespace feel_presets {
namespace {

constexpr const char* kExtSection = "rea_sixty";
constexpr const char* kExtKey     = "knob_feel_presets";

std::array<KnobFeel, kCount> g_cache;
bool g_loaded = false;

const char* polName_(VPotPolarity p)
{
    return p == VPotPolarity::Bipolar ? "bipolar" : "unipolar";
}

void appendEscaped_(std::ostream& os, const std::string& s)
{
    os << '"';
    for (char c : s) {
        if (c == '"' || c == '\\') os << '\\' << c;
        else if (c == '\n')        os << "\\n";
        else                       os << c;
    }
    os << '"';
}

std::string serialize_()
{
    std::ostringstream os;
    os << "{ \"presets\": [";
    for (int i = 0; i < kCount; ++i) {
        const KnobFeel& f = g_cache[i];
        if (i) os << ",";
        os << " { \"used\": " << (f.used ? "true" : "false");
        if (f.used) {
            os << ", \"name\": ";        appendEscaped_(os, f.name);
            os << ", \"inverted\": "    << (f.inverted ? "true" : "false");
            os << ", \"rangeMin\": "    << f.rangeMin;
            os << ", \"rangeMax\": "    << f.rangeMax;
            os << ", \"sensitivity\": " << f.sensitivity;
            os << ", \"polarity\": \""  << polName_(f.polarity) << "\"";
            os << ", \"defaultNorm\": " << f.defaultNorm;
            if (!f.curvePoints.empty()) {
                os << ", \"curvePoints\": [";
                bool first = true;
                for (const auto& pt : f.curvePoints) {
                    if (!first) os << ",";
                    first = false;
                    os << " [" << pt.first << ", " << pt.second << "]";
                }
                os << " ]";
            }
        }
        os << " }";
    }
    os << " ] }";
    return os.str();
}

bool readBool_(wdl_json_element* obj, const char* key, bool& out)
{
    if (!obj) return false;
    auto* v = obj->get_item_by_name(key);
    if (!v) return false;
    if (auto* s = v->get_string_value(true)) {
        out = (std::strcmp(s, "true") == 0 || std::strcmp(s, "1") == 0);
        return true;
    }
    return false;
}

void readFloat_(wdl_json_element* obj, const char* key, float& out)
{
    if (!obj) return;
    if (auto* v = obj->get_item_by_name(key))
        if (auto* s = v->get_string_value(true)) out = (float)std::atof(s);
}

void load_()
{
    g_loaded = true;
    for (auto& f : g_cache) f = KnobFeel{};

    const char* raw = GetExtState ? GetExtState(kExtSection, kExtKey) : nullptr;
    if (!raw || !raw[0]) return;

    std::string json = raw;
    wdl_json_parser p;
    wdl_json_element* root = p.parse(json.c_str(), static_cast<int>(json.size()));
    JsonTreeGuard rootGuard{p, root};
    if (!root || !root->is_object()) return;
    auto* arr = root->get_item_by_name("presets");
    if (!arr || !arr->is_array() || !arr->m_array) return;

    const int n = arr->m_array->GetSize();
    for (int i = 0; i < n && i < kCount; ++i) {
        wdl_json_element* po = arr->enum_item(i);
        if (!po || !po->is_object()) continue;
        KnobFeel f;
        readBool_(po, "used", f.used);
        if (!f.used) { g_cache[i] = f; continue; }
        if (auto* v = po->get_item_by_name("name"))
            if (auto* s = v->get_string_value()) f.name = s;
        readBool_(po, "inverted", f.inverted);
        readFloat_(po, "rangeMin", f.rangeMin);
        readFloat_(po, "rangeMax", f.rangeMax);
        readFloat_(po, "sensitivity", f.sensitivity);
        if (auto* v = po->get_item_by_name("defaultNorm"))
            if (auto* s = v->get_string_value(true)) f.defaultNorm = std::atof(s);
        if (auto* v = po->get_item_by_name("polarity"))
            if (auto* s = v->get_string_value())
                f.polarity = (std::strcmp(s, "bipolar") == 0)
                    ? VPotPolarity::Bipolar : VPotPolarity::Unipolar;
        if (auto* cpa = po->get_item_by_name("curvePoints");
            cpa && cpa->is_array() && cpa->m_array)
        {
            const int pn = cpa->m_array->GetSize();
            f.curvePoints.reserve(pn);
            for (int pi = 0; pi < pn; ++pi) {
                wdl_json_element* pe = cpa->enum_item(pi);
                if (!pe || !pe->is_array() || !pe->m_array) continue;
                if (pe->m_array->GetSize() < 2) continue;
                auto* xe = pe->enum_item(0);
                auto* ye = pe->enum_item(1);
                if (!xe || !ye) continue;
                const char* xs = xe->get_string_value(true);
                const char* ys = ye->get_string_value(true);
                if (!xs || !ys) continue;
                f.curvePoints.emplace_back((float)std::atof(xs),
                                           (float)std::atof(ys));
            }
        }
        g_cache[i] = std::move(f);
    }
}

void persist_()
{
    if (SetExtState)
        SetExtState(kExtSection, kExtKey, serialize_().c_str(), true);
}

} // namespace

const std::array<KnobFeel, kCount>& get()
{
    if (!g_loaded) load_();
    return g_cache;
}

void set(int slot, const KnobFeel& f)
{
    if (slot < 0 || slot >= kCount) return;
    if (!g_loaded) load_();
    g_cache[slot] = f;
    g_cache[slot].used = true;
    persist_();
}

void clear(int slot)
{
    if (slot < 0 || slot >= kCount) return;
    if (!g_loaded) load_();
    g_cache[slot] = KnobFeel{};
    persist_();
}

} // namespace feel_presets
} // namespace uf8
