#include "PluginChunkPatch.h"

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "reaper_plugin_functions.h"

namespace uf8 {
namespace {

// ---- base64 ----------------------------------------------------------

constexpr char kB64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

constexpr int8_t b64DecodeValue(char c) {
    if (c >= 'A' && c <= 'Z') return static_cast<int8_t>(c - 'A');
    if (c >= 'a' && c <= 'z') return static_cast<int8_t>(c - 'a' + 26);
    if (c >= '0' && c <= '9') return static_cast<int8_t>(c - '0' + 52);
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::string b64Decode(std::string_view s) {
    std::string out;
    out.reserve((s.size() * 3) / 4);
    uint32_t buf = 0;
    int bits = 0;
    for (char c : s) {
        if (c == '=' || c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
        const int8_t v = b64DecodeValue(c);
        if (v < 0) continue;
        buf = (buf << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buf >> bits) & 0xFF));
            buf &= (1u << bits) - 1u;
        }
    }
    return out;
}

std::string b64Encode(std::string_view s) {
    std::string out;
    out.reserve(((s.size() + 2) / 3) * 4);
    uint32_t buf = 0;
    int bits = 0;
    for (char c : s) {
        buf = (buf << 8) | static_cast<uint8_t>(c);
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out.push_back(kB64Alphabet[(buf >> bits) & 0x3F]);
            buf &= (1u << bits) - 1u;
        }
    }
    if (bits > 0) {
        out.push_back(kB64Alphabet[(buf << (6 - bits)) & 0x3F]);
    }
    while (out.size() % 4) out.push_back('=');
    return out;
}

// ---- XML payload patches --------------------------------------------

using PatchFn = bool (*)(std::string& xml);

// Flip <PARAM_NON_AUTO id="HighQuality" value="X.0"/> in the active A/B
// slot. Active slot determined by StateASelected attribute. No-op (false)
// if the plug-in's XML schema doesn't have HighQuality (BC2 doesn't —
// BC2 exposes oversampling as a normal automatable param instead).
bool patchHighQuality(std::string& xml) {
    constexpr std::string_view kSasMarker = "StateASelected=\"";
    const size_t sasPos = xml.find(kSasMarker);
    if (sasPos == std::string::npos) return false;
    const size_t activeIdx = sasPos + kSasMarker.size();
    if (activeIdx >= xml.size()) return false;
    const char active = xml[activeIdx];
    if (active != '0' && active != '1') return false;
    const char slotTag = (active == '1') ? 'A' : 'B';

    // Slot opens with "<A " or "<A>" / "<B " or "<B>"; reject false
    // matches like <AbsoluteSomething>.
    const std::string slotOpen = std::string("<") + slotTag;
    const size_t slotStart = xml.find(slotOpen, activeIdx);
    if (slotStart == std::string::npos) return false;
    const char afterTag = (slotStart + 2 < xml.size())
        ? xml[slotStart + 2] : '\0';
    if (afterTag != ' ' && afterTag != '>') return false;
    const size_t slotBodyStart = xml.find('>', slotStart);
    if (slotBodyStart == std::string::npos) return false;
    const std::string slotClose = std::string("</") + slotTag + ">";
    const size_t slotEnd = xml.find(slotClose, slotBodyStart);
    if (slotEnd == std::string::npos) return false;

    constexpr std::string_view kHqPattern =
        "<PARAM_NON_AUTO id=\"HighQuality\" value=\"";
    const size_t hqPos = xml.find(kHqPattern, slotBodyStart);
    if (hqPos == std::string::npos || hqPos > slotEnd) return false;
    const size_t valStart = hqPos + kHqPattern.size();
    const size_t valEnd = xml.find('"', valStart);
    if (valEnd == std::string::npos) return false;

    const std::string cur = xml.substr(valStart, valEnd - valStart);
    const std::string nxt = (cur == "1.0") ? "0.0" : "1.0";
    if (cur.size() != nxt.size()) return false;
    xml.replace(valStart, cur.size(), nxt);
    return true;
}

// Flip the StateASelected attribute (single-digit "0" / "1"). Works on
// every SSL plug-in with an A/B compare (CS-family + BC).
bool patchStateASelected(std::string& xml) {
    constexpr std::string_view kSasMarker = "StateASelected=\"";
    const size_t sasPos = xml.find(kSasMarker);
    if (sasPos == std::string::npos) return false;
    const size_t idx = sasPos + kSasMarker.size();
    if (idx >= xml.size()) return false;
    const char cur = xml[idx];
    if (cur != '0' && cur != '1') return false;
    xml[idx] = (cur == '1') ? '0' : '1';
    return true;
}

// ---- chunk walker ---------------------------------------------------

// In-place mutation of a single VST block within `chunk`, where the
// block boundaries are [headStart .. blockEnd) (blockEnd points to the
// first char of the closing "\n>\n"). Decodes per-line, attempts the
// patch, re-encodes preserving original line byte boundaries, and
// substitutes the new body. Returns true if the patch was applied
// (chunk size unchanged on success).
bool patchVstBlock(std::string& chunk,
                   size_t headStart,
                   size_t headEnd,
                   size_t bodyEnd,
                   PatchFn patch)
{
    std::vector<size_t> lineByteLens;
    std::string bin;
    {
        size_t pos = headEnd + 1;
        while (pos < bodyEnd) {
            size_t eol = chunk.find('\n', pos);
            if (eol == std::string::npos || eol > bodyEnd) eol = bodyEnd;
            std::string_view line(chunk.data() + pos, eol - pos);
            bool hasContent = false;
            for (char c : line) {
                if (c != ' ' && c != '\t' && c != '\r') { hasContent = true; break; }
            }
            if (hasContent) {
                std::string decoded = b64Decode(line);
                lineByteLens.push_back(decoded.size());
                bin += decoded;
            }
            pos = eol + 1;
        }
    }

    constexpr std::string_view kXmlStartMarker = "<?xml";
    constexpr std::string_view kXmlEndMarker   = "</SSL_PLUGIN_STATE>";
    const size_t xs = bin.find(kXmlStartMarker);
    if (xs == std::string::npos) return false;
    size_t xe = bin.find(kXmlEndMarker, xs);
    if (xe == std::string::npos) return false;
    xe += kXmlEndMarker.size();

    std::string xml = bin.substr(xs, xe - xs);
    const size_t origLen = xml.size();
    if (!patch(xml)) return false;
    if (xml.size() != origLen) return false;

    std::string newBin = bin.substr(0, xs) + xml + bin.substr(xe);

    std::string newBody;
    newBody.reserve(bodyEnd - headEnd);
    size_t cursor = 0;
    for (size_t n : lineByteLens) {
        if (!newBody.empty()) newBody.push_back('\n');
        newBody += b64Encode(std::string_view(newBin.data() + cursor, n));
        cursor += n;
    }

    // Substitute body in place. Old body was [headEnd+1 .. bodyEnd).
    chunk.replace(headEnd + 1, bodyEnd - (headEnd + 1), newBody);
    return true;
}

// Find the next <VST "VST3: …"> block whose plug-in is an SSL CS-family or BC —
// i.e. its VST3 name starts with "SSL " (Channel Strip 2 / 4K G / 360 Link / Bus
// Compressor, all "VST3: SSL …") OR "4K " (the 4K B / 4K E channel strips, whose
// VST3 names are literally "VST3: 4K B" / "VST3: 4K E" with NO "SSL" prefix).
// Returns the head '<' offset, or npos. FIX (Frank HW 2026-07-29): the old single
// "<VST \"VST3: SSL " marker silently SKIPPED 4K B/E, so HQ Mode / A/B no-op'd on
// them (they lack "SSL" in the name) — on every surface, not just UF1.
size_t nextSslVstHead(const std::string& chunk, size_t from) {
    constexpr std::string_view kVstPrefix = "<VST \"VST3: ";
    for (size_t p = from; ; ) {
        const size_t h = chunk.find(kVstPrefix, p);
        if (h == std::string::npos) return std::string::npos;
        const size_t nameStart = h + kVstPrefix.size();
        if (chunk.compare(nameStart, 4, "SSL ") == 0 ||
            chunk.compare(nameStart, 3, "4K ")  == 0)
            return h;
        p = nameStart;   // not an SSL/4K block — keep looking
    }
}

// Walk all SSL CS-family / BC <VST> blocks in `chunk`, apply `patch` to each,
// return count of successful patches. Mutates `chunk` in place.
int forEachSslVstBlock(std::string& chunk, PatchFn patch) {
    int patched = 0;
    size_t searchFrom = 0;
    while (true) {
        const size_t headStart = nextSslVstHead(chunk, searchFrom);
        if (headStart == std::string::npos) break;
        const size_t headEnd = chunk.find('\n', headStart);
        if (headEnd == std::string::npos) break;
        const size_t bodyEnd = chunk.find("\n>\n", headEnd);
        if (bodyEnd == std::string::npos) break;

        const size_t origBodyLen = bodyEnd - (headEnd + 1);
        if (patchVstBlock(chunk, headStart, headEnd, bodyEnd, patch)) {
            ++patched;
        }
        // patchVstBlock preserves base-64 line content but the new body
        // length may differ (different line counts → different newline
        // count). Recompute the offset for the next iteration from the
        // current replaced block by walking past its (possibly new)
        // close marker.
        const size_t nextStart = chunk.find("\n>\n", headEnd);
        if (nextStart == std::string::npos) break;
        searchFrom = nextStart + 3;
        (void)origBodyLen;
    }
    return patched;
}

// ---- public driver --------------------------------------------------

int applyToAllSsl(MediaTrack* tr, PatchFn patch) {
    if (!tr) return 0;
    constexpr int kChunkBufSize = 1 << 20;  // 1 MB — heaviest chunks I've
                                            // seen are < 16 KB.
    std::vector<char> buf(kChunkBufSize, 0);
    if (!GetTrackStateChunk(tr, buf.data(), kChunkBufSize, false)) return 0;

    std::string chunk(buf.data());
    const int patched = forEachSslVstBlock(chunk, patch);
    if (patched == 0) return 0;

    return SetTrackStateChunk(tr, chunk.c_str(), false) ? patched : 0;
}

}  // namespace

int togglePluginHQ(MediaTrack* tr) { return applyToAllSsl(tr, patchHighQuality); }
int togglePluginAB(MediaTrack* tr) { return applyToAllSsl(tr, patchStateASelected); }

namespace {

// Decode the first <VST "VST3: SSL ..."> block's binary blob into the
// XML payload. Returns empty string on any failure.
std::string firstSslXml(const std::string& chunk) {
    const size_t headStart = nextSslVstHead(chunk, 0);
    if (headStart == std::string::npos) return {};
    const size_t headEnd = chunk.find('\n', headStart);
    if (headEnd == std::string::npos) return {};
    const size_t bodyEnd = chunk.find("\n>\n", headEnd);
    if (bodyEnd == std::string::npos) return {};

    std::string bin;
    {
        size_t pos = headEnd + 1;
        while (pos < bodyEnd) {
            size_t eol = chunk.find('\n', pos);
            if (eol == std::string::npos || eol > bodyEnd) eol = bodyEnd;
            std::string_view line(chunk.data() + pos, eol - pos);
            bool hasContent = false;
            for (char c : line) {
                if (c != ' ' && c != '\t' && c != '\r') { hasContent = true; break; }
            }
            if (hasContent) bin += b64Decode(line);
            pos = eol + 1;
        }
    }
    constexpr std::string_view kXmlStartMarker = "<?xml";
    constexpr std::string_view kXmlEndMarker   = "</SSL_PLUGIN_STATE>";
    const size_t xs = bin.find(kXmlStartMarker);
    if (xs == std::string::npos) return {};
    size_t xe = bin.find(kXmlEndMarker, xs);
    if (xe == std::string::npos) return {};
    xe += kXmlEndMarker.size();
    return bin.substr(xs, xe - xs);
}

}  // namespace

void readPluginToggleStates(MediaTrack* tr, int& ab, int& hq) {
    ab = -1;
    hq = -1;
    if (!tr) return;
    constexpr int kChunkBufSize = 1 << 20;
    std::vector<char> buf(kChunkBufSize, 0);
    if (!GetTrackStateChunk(tr, buf.data(), kChunkBufSize, false)) return;
    std::string chunk(buf.data());
    const std::string xml = firstSslXml(chunk);
    if (xml.empty()) return;

    // A/B
    {
        constexpr std::string_view k = "StateASelected=\"";
        const size_t p = xml.find(k);
        if (p != std::string::npos && p + k.size() < xml.size()) {
            const char c = xml[p + k.size()];
            if (c == '0' || c == '1') ab = (c - '0');
        }
    }

    // HQ — read the active slot's HighQuality
    if (ab >= 0) {
        const char slotTag = (ab == 1) ? 'A' : 'B';
        const std::string slotOpen = std::string("<") + slotTag;
        const size_t slotStart = xml.find(slotOpen);
        if (slotStart != std::string::npos) {
            const char afterTag = (slotStart + 2 < xml.size()) ? xml[slotStart + 2] : '\0';
            if (afterTag == ' ' || afterTag == '>') {
                const size_t slotBodyStart = xml.find('>', slotStart);
                const std::string slotClose = std::string("</") + slotTag + ">";
                const size_t slotEnd = xml.find(slotClose, slotBodyStart);
                if (slotBodyStart != std::string::npos && slotEnd != std::string::npos) {
                    constexpr std::string_view kHq =
                        "<PARAM_NON_AUTO id=\"HighQuality\" value=\"";
                    const size_t hp = xml.find(kHq, slotBodyStart);
                    if (hp != std::string::npos && hp < slotEnd) {
                        const size_t valStart = hp + kHq.size();
                        const size_t valEnd = xml.find('"', valStart);
                        if (valEnd != std::string::npos) {
                            const std::string v = xml.substr(valStart, valEnd - valStart);
                            // string compare avoids strtod locale woes
                            hq = (v == "1.0" || v == "1") ? 1 : 0;
                        }
                    } else {
                        hq = 0;  // no HighQuality element (BC2 etc.) → off
                    }
                }
            }
        }
    }
}


// ── Native preset load: the plug-in's own state, not just its parameters ───
// Setting the host parameters gets the VALUES right and the plug-in still shows
// "that preset, modified" — because what SSL calls the loaded preset is not a
// function of the values at all, it is an ATTRIBUTE in the state:
//   <A LastLoadedPreset="/Library/…/Presets/MeterPro/K-20.xml">
// (Frank 2026-08-20: "er soll doch im plugin auch das preset laden! jetzt ist es
// einfach das geladene preset mit änderungen!")
//
// So write it where SSL keeps it. REAPER hands the instance's chunk over
// verbatim through TrackFX_Get/SetNamedConfigParm("vst_chunk") — which is also
// the ONLY route that survives a Meter Pro: its chunk is ~840 KB (the loudness
// history rides along), and GetTrackStateChunk would have to carry the whole
// track and would truncate.
//
// ⚠ THIS EDIT CHANGES THE LENGTH, and the container counts the bytes FOUR
// times, in two endiannesses. The layout below is what a real Meter Pro chunk
// actually contains (837217 bytes, read back out of REAPER on 2026-08-20 — the
// first attempt at this was modelled on the <VST> block in the project file
// instead, which is a DIFFERENT framing, and it refused every chunk):
//     0    uint32 LE   = blob - 16          REAPER's own count
//     4    uint32 LE   = 1
//     8    "VstW" + three big-endian words
//     24   "CcnK"                            VST2-style container, optional
//     28   uint32 BE   = blob - 40           byteSize
//     32   "FBCh" + version + fxID + fxVersion + numPrograms + future[128]
//     156+ uint32 BE   = blob - 192          chunkSize   (at CcnK + 156)
//     184  the plug-in's own state … "VC2!" + uint32 LE = XML length … the XML
// The "VC2!" length is the one constant across every SSL plug-in (verified on
// 113 blocks); the rest is checked at load time, never assumed: each count has
// to read as "the blob minus a small header" or this writes NOTHING and returns
// 0, and the caller falls back to setting the host parameters. A preset with
// the right values beats a wrecked instance.

namespace {

uint32_t rdU32(const std::string& b, size_t o) {
    return  (uint32_t)(uint8_t)b[o]
         | ((uint32_t)(uint8_t)b[o + 1] << 8)
         | ((uint32_t)(uint8_t)b[o + 2] << 16)
         | ((uint32_t)(uint8_t)b[o + 3] << 24);
}

// The container mixes endianness: REAPER's own counts are little-endian, the
// VST2-style FBCh header inside it is big-endian, as that format has always been.
uint32_t rdU32BE(const std::string& b, size_t o) {
    return  (uint32_t)(uint8_t)b[o + 3]
         | ((uint32_t)(uint8_t)b[o + 2] << 8)
         | ((uint32_t)(uint8_t)b[o + 1] << 16)
         | ((uint32_t)(uint8_t)b[o]     << 24);
}

void wrU32BE(std::string& b, size_t o, uint32_t v) {
    b[o + 3] = (char)( v        & 0xFF);
    b[o + 2] = (char)((v >> 8)  & 0xFF);
    b[o + 1] = (char)((v >> 16) & 0xFF);
    b[o]     = (char)((v >> 24) & 0xFF);
}

void wrU32(std::string& b, size_t o, uint32_t v) {
    b[o]     = (char)( v        & 0xFF);
    b[o + 1] = (char)((v >> 8)  & 0xFF);
    b[o + 2] = (char)((v >> 16) & 0xFF);
    b[o + 3] = (char)((v >> 24) & 0xFF);
}

std::string xmlAttrEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '&':  o += "&amp;";  break;
            case '<':  o += "&lt;";   break;
            case '>':  o += "&gt;";   break;
            case '"':  o += "&quot;"; break;
            default:   o.push_back(c);
        }
    }
    return o;
}

struct LenField { size_t off; bool bigEndian; };

// Substitute every value the preset names into the ACTIVE A/B slot of `xml`, and
// point that slot's LastLoadedPreset at the file. Returns how many values were
// substituted. Only ids the instance ALREADY carries are touched: the plug-in's
// own identity lives in the same state (HostTrackIndex, SessionDataId, UniqueId)
// and no preset may overwrite it.
int applyPresetToStateXml(std::string& xml,
                          const std::string& presetXml,
                          const std::string& presetPath)
{
    // Active slot — the same StateASelected the HQ / A-B patches above read.
    char tag = 'A';
    const size_t sas = xml.find("StateASelected=\"");
    if (sas != std::string::npos && sas + 16 < xml.size())
        tag = (xml[sas + 16] == '0') ? 'B' : 'A';

    const std::string open = std::string("<") + tag;
    size_t slotOpen = std::string::npos;
    for (size_t p = 0; ; ) {
        const size_t h = xml.find(open, p);
        if (h == std::string::npos) break;
        const char after = (h + 2 < xml.size()) ? xml[h + 2] : '\0';
        if (after == ' ' || after == '>') { slotOpen = h; break; }
        p = h + 2;
    }
    size_t regionStart = 0, regionEnd = xml.size();
    if (slotOpen != std::string::npos) {
        const size_t gt = xml.find('>', slotOpen);
        const std::string close = std::string("</") + tag + ">";
        const size_t e = (gt == std::string::npos)
                       ? std::string::npos : xml.find(close, gt);
        if (gt != std::string::npos && e != std::string::npos) {
            regionStart = gt + 1; regionEnd = e;
        } else {
            slotOpen = std::string::npos;   // no A/B slots → whole state
        }
    }

    const size_t ps = xml.find("<PROCESSOR_STATE>", regionStart);
    if (ps == std::string::npos || ps > regionEnd) return 0;
    const size_t pe = xml.find("</PROCESSOR_STATE>", ps);
    if (pe == std::string::npos || pe > regionEnd) return 0;
    std::string body = xml.substr(ps, pe - ps);

    int applied = 0;
    for (size_t p = 0; (p = presetXml.find("<PARAM", p)) != std::string::npos; ) {
        const size_t end = presetXml.find("/>", p);
        if (end == std::string::npos) break;
        const std::string t = presetXml.substr(p, end - p);
        p = end + 2;
        const size_t ip = t.find("id=\""), vp = t.find("value=\"");
        if (ip == std::string::npos || vp == std::string::npos) continue;
        const size_t ie = t.find('"', ip + 4), ve = t.find('"', vp + 7);
        if (ie == std::string::npos || ve == std::string::npos) continue;
        // id="X" with the closing quote, so no id can be another's prefix.
        const std::string key = "id=\"" + t.substr(ip + 4, ie - (ip + 4)) + "\"";
        const size_t at = body.find(key);
        if (at == std::string::npos) continue;   // this variant has no such id
        const size_t vs = body.find("value=\"", at);
        if (vs == std::string::npos) continue;
        const size_t tagEnd = body.find("/>", at);
        if (tagEnd != std::string::npos && tagEnd < vs) continue;  // other tag
        const size_t vsEnd = body.find('"', vs + 7);
        if (vsEnd == std::string::npos) continue;
        body.replace(vs + 7, vsEnd - (vs + 7), t.substr(vp + 7, ve - (vp + 7)));
        ++applied;
    }
    if (applied == 0) return 0;
    xml.replace(ps, pe - ps, body);   // slotOpen sits BEFORE ps → still valid

    // The attribute the plug-in SHOWS. Everything above only makes the values
    // agree with it.
    if (slotOpen != std::string::npos) {
        const size_t gt = xml.find('>', slotOpen);
        const std::string esc = xmlAttrEscape(presetPath);
        const size_t at = xml.find("LastLoadedPreset=\"", slotOpen);
        if (at != std::string::npos && gt != std::string::npos && at < gt) {
            const size_t e = xml.find('"', at + 18);
            if (e != std::string::npos && e < gt)
                xml.replace(at + 18, e - (at + 18), esc);
        } else {
            xml.insert(slotOpen + 2, " LastLoadedPreset=\"" + esc + "\"");
        }
    }
    return applied;
}

// Rewrite ONE decoded chunk in place: preset values into the active slot,
// LastLoadedPreset onto it, and the three byte counts fixed up. Returns the
// number of values written, 0 when the blob is not in the shape described above
// (and then `bin` is untouched). Pure — no REAPER API, so the test can drive it.
int patchSslChunkWithPreset(std::string& bin,
                            const std::string& presetPath,
                            const std::string& presetXml,
                            char* diagOut = nullptr, int diagOutSz = 0)
{
    auto diag = [&](const char* fmt, ...) {
        if (!diagOut || diagOutSz <= 0) return;
        va_list ap; va_start(ap, fmt);
        vsnprintf(diagOut, (size_t)diagOutSz, fmt, ap);
        va_end(ap);
    };
    if (bin.size() < 160) { diag("blob %zu bytes", bin.size()); return 0; }
    const size_t xs = bin.find("<?xml");
    size_t xe = bin.find("</SSL_PLUGIN_STATE>");
    if (xs == std::string::npos || xe == std::string::npos || xs < 8) {
        diag("blob %zu, head %02x%02x%02x%02x, xml at %ld, state end %ld",
             bin.size(), (unsigned char)bin[0], (unsigned char)bin[1],
             (unsigned char)bin[2], (unsigned char)bin[3],
             (long)(xs == std::string::npos ? -1 : (long)xs),
             (long)(xe == std::string::npos ? -1 : (long)xe));
        return 0;
    }
    xe += 19;
    if (bin.compare(xs - 8, 4, "VC2!") != 0) {
        diag("blob %zu, xml at %zu, no VC2! (%02x%02x%02x%02x)", bin.size(), xs,
             (unsigned char)bin[xs-8], (unsigned char)bin[xs-7],
             (unsigned char)bin[xs-6], (unsigned char)bin[xs-5]);
        return 0;
    }
    if (rdU32(bin, xs - 4) != (uint32_t)(xe - xs)) {
        diag("blob %zu, VC2! len %u vs xml %zu", bin.size(),
             rdU32(bin, xs - 4), xe - xs);
        return 0;
    }

    // Every byte count that has to move with the edit, each one CHECKED before
    // it is believed: a length here always reads as "the whole blob, minus a
    // small header". A field that does not is not a length, and then this
    // refuses rather than writing over something it has not understood.
    auto isLen = [&](size_t off, bool be) {
        if (off + 4 > bin.size()) return false;
        const uint32_t v = be ? rdU32BE(bin, off) : rdU32(bin, off);
        return v <= bin.size() && v + 256 >= bin.size()
            && v > (uint32_t)(xe - xs);
    };
    std::vector<LenField> lens;
    if (!isLen(0, false)) {
        diag("blob %zu, xml %zu at %zu, no leading length (head %08x %08x %08x)",
             bin.size(), xe - xs, xs,
             rdU32(bin, 0), rdU32(bin, 4), rdU32(bin, 8));
        return 0;
    }
    lens.push_back({0, false});
    // The VST2-style container REAPER wraps a VST3 state in — present on the
    // Meter, absent on the smaller plug-ins, so it is optional but, once found,
    // must be intact: 'CcnK' + byteSize + 'FBCh' + … + chunkSize at +156.
    const size_t ck = bin.find("CcnK");
    if (ck != std::string::npos && ck < 256) {
        if (bin.compare(ck + 8, 4, "FBCh") != 0 ||
            !isLen(ck + 4, true) || !isLen(ck + 156, true)) {
            diag("blob %zu, CcnK at %zu not the expected shape "
                 "(byteSize %u, chunkSize %u)", bin.size(), ck,
                 ck + 8 <= bin.size() ? rdU32BE(bin, ck + 4) : 0,
                 ck + 160 <= bin.size() ? rdU32BE(bin, ck + 156) : 0);
            return 0;
        }
        lens.push_back({ck + 4,   true});
        lens.push_back({ck + 156, true});
    }

    std::string xml = bin.substr(xs, xe - xs);
    const size_t wasLen = xml.size();
    const int applied = applyPresetToStateXml(xml, presetXml, presetPath);
    if (applied <= 0) { diag("no id of the preset is in the instance"); return 0; }
    const long delta = (long)xml.size() - (long)wasLen;

    std::string out = bin.substr(0, xs) + xml + bin.substr(xe);
    wrU32(out, xs - 4, (uint32_t)xml.size());
    for (const LenField& f : lens) {
        if (f.bigEndian) wrU32BE(out, f.off, (uint32_t)((long)rdU32BE(out, f.off) + delta));
        else             wrU32  (out, f.off, (uint32_t)((long)rdU32  (out, f.off) + delta));
    }
    bin.swap(out);
    return applied;
}

}  // namespace

// Which preset the instance currently reports as loaded — the same attribute
// loadSslPresetIntoInstance writes, read back. `out` gets the NAME (the file's
// base name without ".xml"), which is what the surface lists. false = no chunk,
// no attribute, or a plug-in that keeps none.
bool sslLoadedPresetName(MediaTrack* tr, int fx, char* out, int outSz)
{
    if (!out || outSz <= 0) return false;
    out[0] = 0;
    if (!tr || !TrackFX_GetNamedConfigParm) return false;

    std::string b64;
    for (size_t cap = 1u << 20; cap <= (1u << 26); cap <<= 2) {
        std::vector<char> buf(cap, 0);
        if (TrackFX_GetNamedConfigParm(tr, fx, "vst_chunk", buf.data(), (int)cap)
            && buf[0]) {
            std::string got(buf.data());
            if (got.size() + 64 < cap) { b64.swap(got); break; }
        }
    }
    if (b64.empty()) return false;
    const std::string bin = b64Decode(b64);

    // The ACTIVE slot's attribute — B's is a different preset, and reading the
    // wrong one would put the browser on an entry the user is not hearing.
    const size_t xs = bin.find("<?xml");
    if (xs == std::string::npos) return false;
    char tag = 'A';
    const size_t sas = bin.find("StateASelected=\"", xs);
    if (sas != std::string::npos && sas + 16 < bin.size())
        tag = (bin[sas + 16] == '0') ? 'B' : 'A';
    const std::string open = std::string("<") + tag;
    size_t slot = std::string::npos;
    for (size_t p = xs; ; ) {
        const size_t h = bin.find(open, p);
        if (h == std::string::npos) break;
        const char after = (h + 2 < bin.size()) ? bin[h + 2] : '\0';
        if (after == ' ' || after == '>') { slot = h; break; }
        p = h + 2;
    }
    if (slot == std::string::npos) return false;
    const size_t gt = bin.find('>', slot);
    const size_t at = bin.find("LastLoadedPreset=\"", slot);
    if (at == std::string::npos || gt == std::string::npos || at > gt) return false;
    const size_t e = bin.find('"', at + 18);
    if (e == std::string::npos || e > gt) return false;

    std::string path = bin.substr(at + 18, e - (at + 18));
    // Undo the attribute escaping, then keep the file's base name without the
    // extension: that is the string the preset list is built from.
    for (const auto& r : {std::pair<const char*, const char*>{"&amp;", "&"},
                          {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}})
        for (size_t p = 0; (p = path.find(r.first, p)) != std::string::npos; )
            path.replace(p, strlen(r.first), r.second);
    const size_t slash = path.find_last_of("/\\");
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    if (name.size() > 4) {
        const std::string ext = name.substr(name.size() - 4);
        if (ext == ".xml" || ext == ".XML") name.resize(name.size() - 4);
    }
    if (name.empty() || (int)name.size() >= outSz) return false;
    memcpy(out, name.c_str(), name.size() + 1);
    return true;
}

int loadSslPresetIntoInstance(MediaTrack* tr, int fx,
                              const char* presetPath, const char* presetXml,
                              char* diagOut, int diagOutSz)
{
    // Every early return says WHY into diagOut. A chunk we decline looks exactly
    // like a chunk we never read, and the caller's fallback hides both.
    auto diag = [&](const char* fmt, ...) {
        if (!diagOut || diagOutSz <= 0) return;
        va_list ap; va_start(ap, fmt);
        vsnprintf(diagOut, (size_t)diagOutSz, fmt, ap);
        va_end(ap);
    };
    diag("no attempt");
    if (!tr || !presetPath || !*presetPath || !presetXml || !*presetXml) {
        diag("bad args");
        return 0;
    }
    if (!TrackFX_GetNamedConfigParm || !TrackFX_SetNamedConfigParm) {
        diag("no NamedConfigParm in this REAPER");
        return 0;
    }

    // Grown, not guessed: a truncated read looks exactly like a plug-in whose
    // chunk we don't understand, and that would silently downgrade every load.
    bool readOk = false;
    auto readChunk = [&]() {
        std::string out;
        for (size_t cap = 1u << 20; cap <= (1u << 26); cap <<= 2) {
            std::vector<char> buf(cap, 0);
            if (TrackFX_GetNamedConfigParm(tr, fx, "vst_chunk", buf.data(),
                                           (int)cap) && buf[0]) {
                readOk = true;
                std::string got(buf.data());
                if (got.size() + 64 < cap) { out.swap(got); break; }
            }
        }
        return out;
    };
    const std::string b64 = readChunk();
    if (b64.empty()) {
        diag(readOk ? "vst_chunk larger than 64 MB" : "vst_chunk read failed");
        return 0;
    }

    std::string bin = b64Decode(b64);
    const int applied = patchSslChunkWithPreset(bin, presetPath, presetXml,
                                                diagOut, diagOutSz);
    if (applied <= 0) return 0;

    if (!TrackFX_SetNamedConfigParm(tr, fx, "vst_chunk", b64Encode(bin).c_str())) {
        diag("vst_chunk write refused (%d values, %zu bytes)", applied, bin.size());
        return 0;
    }

    // Read back and look for the attribute we just wrote. A write that REAPER
    // accepts but the plug-in drops would otherwise be indistinguishable from a
    // load — which is exactly the failure this whole function exists to end.
    // Nothing to show for it → put the ORIGINAL back.
    {
        const std::string back = readChunk();
        const std::string want = "LastLoadedPreset=\"" + xmlAttrEscape(presetPath);
        if (back.empty() || b64Decode(back).find(want) == std::string::npos) {
            TrackFX_SetNamedConfigParm(tr, fx, "vst_chunk", b64.c_str());
            diag("plug-in did not keep the chunk, original restored");
            return 0;
        }
    }
    return applied;
}

}  // namespace uf8
