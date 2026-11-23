#include "OrcProfile.h"

#include "JsonTree.h"
#include "OrcConfig.h"
#include "OrcFiles.h"
#include "OrcHandover.h"
#include "OrcModes.h"
#include "PhonesServer.h"
#include "RmeManager.h"
#include "Sha256.h"

#include <cstring>
#include <ctime>

namespace orc::profile {

namespace {

namespace rme = reasixty::rme;
namespace ph  = orc::phones;

// Copied as they are. rme.json and moreme.json are merged (mergeRme, mergeMoreMe).
constexpr const char* kPlain[]  = { "orc.json", "moreme-performers.json" };
// ORC's mode values that belong to a profile, by key (OrcModes.h).
constexpr const char* kModes[]  = { "dynamount_presets", "dynamount_fill" };
// The names a MoreMe logo can have (PhonesServer.cpp, importLogo).
constexpr const char* kLogos[]  = { "moreme-logo.png", "moreme-logo.jpg", "moreme-logo.svg" };
constexpr const char* kStaged   = "profile-import.orcprofile";

std::string dir() { return orc::supportDir(); }

std::string jsonText(const std::string& s)
{
    std::string o = "\"";
    for (const char ch : s) {
        if (ch == '"' || ch == '\\') { o += '\\'; o += ch; }
        else if (static_cast<unsigned char>(ch) >= 0x20) o += ch;
    }
    return o + "\"";
}

const File* find(const std::vector<File>& files, const std::string& name)
{
    for (const auto& f : files)
        if (f.name == name) return &f;
    return nullptr;
}

bool isLogoName(const std::string& n)
{
    for (const char* l : kLogos)
        if (n == l) return true;
    return false;
}

std::string timeText(const char* fmt)
{
    const std::time_t now = std::time(nullptr);
    char b[32];
    std::strftime(b, sizeof(b), fmt, std::localtime(&now));
    return b;
}

// Next to `path`, then over it: a file half written never stands in its place.
bool writeOver(const std::string& path, const std::string& data)
{
    const std::string tmp = path + ".import";
    if (!orc_handover::writeFile(tmp, data)) return false;
    if (orc_files::replaceFile(tmp, path)) return true;
    orc_files::removeFile(tmp);
    return false;
}

std::string g_lastApplied;

} // namespace

// ── pure ──────────────────────────────────────────────────────────────────────

std::string base64Decode(const std::string& in, bool& ok)
{
    auto val = [](unsigned char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    out.reserve(in.size() * 3 / 4);
    unsigned acc = 0;
    int bits = 0;
    ok = true;
    for (std::size_t i = 0; i < in.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        if (c == '=') break;
        if (c == '\n' || c == '\r' || c == ' ') continue;
        const int v = val(c);
        if (v < 0) { ok = false; return {}; }
        acc = (acc << 6) | static_cast<unsigned>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((acc >> bits) & 0xFF);
        }
    }
    return out;
}

std::string pack(const std::vector<File>& files, const Info& info)
{
    std::string o = "{\n  \"format\": \"orc-profile\",\n  \"version\": " + std::to_string(kVersion)
                  + ",\n  \"orc\": " + jsonText(info.orc)
                  + ",\n  \"created\": " + jsonText(info.created)
                  + ",\n  \"files\": {";
    for (std::size_t i = 0; i < files.size(); ++i)
        o += std::string(i ? "," : "") + "\n    " + jsonText(files[i].name) + ": \""
           + reasixty::base64Encode(files[i].data.data(), files[i].data.size()) + "\"";
    return o + "\n  }\n}\n";
}

bool unpack(const std::string& text, std::vector<File>& files, Info& info, std::string& err)
{
    files.clear();
    info = Info{};
    wdl_json_parser p;
    const wdl_json_element* root = p.parse(text.c_str(), static_cast<int>(text.size()));
    JsonTreeGuard guard{p, root};
    const char* format = root && root->is_object() ? root->get_string_by_name("format") : nullptr;
    if (!format || std::strcmp(format, "orc-profile") != 0) { err = "Not an ORC profile."; return false; }
    if (const char* v = root->get_string_by_name("version", true)) info.version = std::atoi(v);
    if (info.version < 1 || info.version > kVersion) {
        err = "This profile needs a newer ORC.";
        return false;
    }
    if (const char* v = root->get_string_by_name("orc")) info.orc = v;
    if (const char* v = root->get_string_by_name("created")) info.created = v;
    const wdl_json_element* fs = root->get_item_by_name("files");
    if (!fs || !fs->is_object()) { err = "The profile is damaged."; return false; }
    for (int i = 0; ; ++i) {
        const wdl_json_element* e = fs->enum_item(i);
        const char* name = fs->enum_item_name(i);
        if (!e || !name) break;
        const char* v = e->get_string_value();
        bool ok = v != nullptr;
        File f{ name, ok ? base64Decode(v, ok) : std::string() };
        if (!ok) { files.clear(); err = "The profile is damaged."; return false; }
        files.push_back(std::move(f));
    }
    if (!find(files, "orc.json") || !find(files, "rme.json")) {
        files.clear();
        err = "The profile is incomplete.";
        return false;
    }
    err.clear();
    return true;
}

std::string mergeRme(const std::string& local, const std::string& fromProfile)
{
    rme::Config here;
    rme::configFromJson(local, here);
    rme::Config c = here;
    rme::configFromJson(fromProfile, c);
    c.enabled  = here.enabled;
    c.host     = here.host;
    c.sendPort = here.sendPort;
    c.recvPort = here.recvPort;
    return rme::configToJson(c);
}

std::string mergeMoreMe(const std::string& local, const std::string& fromProfile)
{
    ph::Settings here;
    ph::parseSettings(local, here);
    ph::Settings theirs;
    ph::parseSettings(fromProfile, theirs);
    ph::Settings s = here;
    s.studio = theirs.studio;
    s.logo   = isLogoName(theirs.logo) ? theirs.logo : std::string();
    return ph::settingsJson(s);
}

// ── ORC's folder ──────────────────────────────────────────────────────────────

std::string current()
{
    const std::string d = dir();
    std::vector<File> files;
    auto add = [&](const std::string& name) {
        std::string data = orc_handover::readFile(d + "/" + name);
        if (!data.empty()) files.push_back({ name, std::move(data) });
    };
    add("orc.json");
    add("rme.json");
    add("moreme.json");
    for (const char* n : kPlain)
        if (std::strcmp(n, "orc.json") != 0) add(n);
    for (const char* k : kModes) {
        const std::string v = orc::modes::read(k);
        if (!v.empty()) files.push_back({ std::string("modes/") + k, v });
    }
    ph::Settings s;
    if (ph::loadSettings(s) && isLogoName(s.logo)) {
        std::string data = orc_handover::readFile(d + "/" + s.logo);
        if (!data.empty()) files.push_back({ "logo/" + s.logo, std::move(data) });
    }
    return pack(files, Info{ kVersion, orc::version(), timeText("%Y-%m-%d %H:%M") });
}

bool exportTo(const std::string& path, std::string& err)
{
    // The link's last edit first, so it is in the file.
    orc::saveRmeConfigIfDirty();
    const std::string text = current();
    if (text.find("\"orc.json\"") == std::string::npos) {
        err = "Nothing to export yet.";
        return false;
    }
    if (!orc_handover::writeFile(path, text)) {
        err = "Cannot write " + path;
        return false;
    }
    err.clear();
    return true;
}

bool check(const std::string& path, Info& info, std::string& err)
{
    const std::string text = orc_handover::readFile(path);
    if (text.empty()) { err = "Cannot read the file."; return false; }
    std::vector<File> files;
    return unpack(text, files, info, err);
}

bool stage(const std::string& path, std::string& err)
{
    Info info;
    if (!check(path, info, err)) return false;
    if (!orc_handover::writeFile(dir() + "/" + kStaged, orc_handover::readFile(path))) {
        err = "Cannot write to " + dir();
        return false;
    }
    err.clear();
    return true;
}

std::string applyStaged()
{
    const std::string d = dir();
    const std::string staged = d + "/" + kStaged;
    const std::string text = orc_handover::readFile(staged);
    if (text.empty()) return {};
    std::vector<File> files;
    Info info;
    std::string err;
    if (!unpack(text, files, info, err)) {
        orc_files::removeFile(staged);
        return g_lastApplied = "Profile not imported: " + err;
    }

    // The files as they are, first.
    const std::string backups = d + "/profiles";
    orc_files::makeDir(backups);
    const std::string backup = "before-import-" + timeText("%Y%m%d-%H%M%S") + "." + kExtension;
    if (!orc_handover::writeFile(backups + "/" + backup, current())) {
        orc_files::removeFile(staged);
        return g_lastApplied = "Profile not imported: cannot write to " + backups;
    }

    bool ok = true;
    // What the profile carries replaces what is here; what it does not carry
    // goes back to empty, so a profile without performers leaves none behind.
    for (const char* n : kPlain) {
        const File* f = find(files, n);
        if (f) ok &= writeOver(d + "/" + n, f->data);
        else   orc_files::removeFile(d + "/" + n);
    }
    {
        const File* f = find(files, "rme.json");
        ok &= writeOver(d + "/rme.json", mergeRme(orc_handover::readFile(d + "/rme.json"), f->data));
    }
    {
        const File* f = find(files, "moreme.json");
        ok &= writeOver(d + "/moreme.json",
                        mergeMoreMe(orc_handover::readFile(d + "/moreme.json"), f ? f->data : std::string()));
    }
    // A fresh stamp, so the values also win over Rea-Sixty's at the next handover
    // (ModeSync: the newer value wins).
    for (const char* k : kModes) {
        const File* f = find(files, std::string("modes/") + k);
        orc::modes::write(k, f ? f->data : std::string());
    }
    for (const auto& f : files) {
        if (f.name.rfind("logo/", 0) != 0) continue;
        const std::string n = f.name.substr(5);
        if (!isLogoName(n)) continue;
        for (const char* old : kLogos)
            if (n != old) orc_files::removeFile(d + "/" + old);
        ok &= writeOver(d + "/" + n, f.data);
    }
    orc_files::removeFile(staged);
    g_lastApplied = ok ? "Profile imported. The settings before it: profiles/" + backup
                       : "Profile imported in part. The settings before it: profiles/" + backup;
    return g_lastApplied;
}

const std::string& lastApplied() { return g_lastApplied; }

} // namespace orc::profile
