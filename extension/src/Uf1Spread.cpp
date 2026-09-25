#include "Uf1Spread.h"
#include <cstring>
#include "Uf1Text.h"
#include "TrackName.h"

#include <algorithm>
#include <cmath>

#include "UF1Protocol.h"

namespace uf1spread {
namespace {

// A text element carries its index in the first byte and the characters after
// it, which is how SSL empties one too: the index alone and nothing behind it.
std::vector<std::uint8_t> indexedText(std::uint8_t idx, const std::string& s)
{
    std::vector<std::uint8_t> p;
    p.reserve(1 + s.size());
    p.push_back(idx);
    p.insert(p.end(), s.begin(), s.end());
    return p;
}

std::vector<std::uint8_t> plainText(const std::string& s)
{
    std::vector<std::uint8_t> p;
    p.reserve(1 + s.size());
    p.push_back(0x00);
    p.insert(p.end(), s.begin(), s.end());
    return p;
}

// Bar position and style, byte for byte as the surface has always been given
// them: a signed cell for the position and 0x80 beside it, style 0x03 empty,
// 0x08 bipolar, 0x01 unipolar.
void barBytes(const Pot& p, std::array<std::uint8_t, 8>& bars,
              std::array<std::uint8_t, 4>& styles, std::size_t i)
{
    const int pos = p.bipolar
        ? std::clamp(static_cast<int>(std::lround((p.norm - 0.5) * 200.0)), -100, 100)
        : std::clamp(static_cast<int>(std::lround(p.norm * 100.0)), 0, 100);
    bars[i * 2]     = static_cast<std::uint8_t>(static_cast<std::int8_t>(pos));
    bars[i * 2 + 1] = 0x80;
    styles[i]       = p.empty ? 0x03 : (p.bipolar ? 0x08 : 0x01);
}

} // namespace

int paintVpotRow(const VpotRow& row, VpotCache& cache, const Sink& out)
{
    const bool all = !cache.valid;
    int n = 0;
    auto send = [&](std::vector<std::uint8_t> f) { out(std::move(f)); ++n; };

    // Leaving Layout 1 clears the per-pot text once, the index byte alone and
    // nothing behind it, the way SSL empties one. Otherwise a name survives into
    // a layout that might draw it.
    if (row.layout1 || cache.namesShown) {
        for (std::uint8_t i = 0; i < 4; ++i) {
            const std::string nm = row.layout1 ? row.names[i] : std::string();
            if (!all && cache.namesShown == row.layout1 && nm == cache.names[i]) continue;
            cache.names[i] = nm;
            send(uf1::buildScreen(uf1::scr::kVpotNumber, indexedText(i, nm)));
        }
        cache.namesShown = row.layout1;
    }
    for (std::uint8_t i = 0; i < 4; ++i) {
        if (!all && cache.valid && row.line[i] == cache.line[i]) continue;
        cache.line[i] = row.line[i];
        send(uf1::buildScreen(uf1::scr::kFocusedParam, indexedText(i, row.line[i])));
    }
    if (all || row.bars != cache.bars) {
        cache.bars = row.bars;
        send(uf1::buildScreen(uf1::scr::kVpotBars, row.bars));
    }
    // Its own gate: a page whose positions happen to repeat still gets its
    // styles corrected.
    if (all || row.styles != cache.styles) {
        cache.styles = row.styles;
        send(uf1::buildScreen(uf1::scr::kVpotStyle, row.styles));
    }
    cache.valid = true;
    return n;
}

int paint(const View& v, Cache& cache, const Sink& out)
{
    const bool all = !cache.valid;
    const View& was = cache.shown;
    int n = 0;
    auto send = [&](std::vector<std::uint8_t> f) { out(std::move(f)); ++n; };

    if (all || v.header != was.header)
        send(uf1::buildScreen(uf1::scr::kHeaderRow, plainText(v.header)));
    // ⛔ 0x0119 TAKES A SEGMENT MASK PER CELL, NOT TEXT. Plain text here came out
    // as a row of "8." on ORC's first run (25.09.). seg7Payload spells it.
    if (all || v.timecode != was.timecode)
        send(uf1::buildScreen(uf1::scr::kTimecode, uf1::seg7Payload(v.timecode)));

    for (std::uint8_t i = 0; i < 4; ++i) {
        if (all || v.pots[i].name != was.pots[i].name)
            send(uf1::buildScreen(uf1::scr::kVpotNumber, indexedText(i, v.pots[i].name)));
        if (all || v.pots[i].line != was.pots[i].line)
            send(uf1::buildScreen(uf1::scr::kFocusedParam, indexedText(i, v.pots[i].line)));
    }

    // The row's bars and styles are one frame each for all four pots, so they
    // are compared as a whole. Two independent gates, not one: a page whose
    // positions happen to repeat still gets its styles corrected.
    std::array<std::uint8_t, 8> bars{}, wasBars{};
    std::array<std::uint8_t, 4> styles{}, wasStyles{};
    for (std::size_t i = 0; i < 4; ++i) {
        barBytes(v.pots[i], bars, styles, i);
        barBytes(was.pots[i], wasBars, wasStyles, i);
    }
    if (all || bars != wasBars)
        send(uf1::buildScreen(uf1::scr::kVpotBars, bars));
    if (all || styles != wasStyles)
        send(uf1::buildScreen(uf1::scr::kVpotStyle, styles));

    if (all || v.chName != was.chName)
        send(uf1::buildScreen(uf1::scr::kTrackName, plainText(v.chName)));
    if (all || v.chDb != was.chDb)
        send(uf1::buildScreen(uf1::scr::kOutputDb, plainText(v.chDb)));
    if (all || v.chNumber != was.chNumber)
        send(uf1::buildScreen(uf1::scr::kChNumber, plainText(v.chNumber)));
    if (all || v.palette != was.palette) {
        const std::uint8_t pal = static_cast<std::uint8_t>(v.palette);
        send(uf1::buildScreen(uf1::scr::kColourBar, std::span<const std::uint8_t>(&pal, 1)));
    }
    if (all || v.chActive != was.chActive) {
        const std::uint8_t on = v.chActive ? 0x01 : 0x00;
        send(uf1::buildScreen(uf1::scr::kChActive, std::span<const std::uint8_t>(&on, 1)));
    }

    // -1 means "the fader is not ours to move", so the motor is left alone
    // rather than driven to zero.
    if (v.faderPos >= 0 && (all || v.faderPos != was.faderPos))
        send(uf1::buildMotorPosition(static_cast<std::uint16_t>(v.faderPos)));

    cache.shown = v;
    cache.valid = true;
    return n;
}

} // namespace uf1spread

namespace uf1spread {

int paintStrip(const StripView& v, StripCache& cache, const Sink& out)
{
    const bool all = !cache.valid;
    const StripView& was = cache.shown;
    int n = 0;
    auto send = [&](std::vector<std::uint8_t> f) { out(std::move(f)); ++n; };
    auto byte = [&](std::uint16_t addr, std::uint8_t b) {
        send(uf1::buildScreen(addr, std::span<const std::uint8_t>(&b, 1)));
    };

    if (all || v.name != was.name)
        send(uf1::buildScreen(uf1::scr::kTrackName, plainText(v.name)));
    if (all || v.db != was.db)
        send(uf1::buildScreen(uf1::scr::kOutputDb, plainText(v.db)));
    if (all || v.line != was.line)
        send(uf1::buildScreen(uf1::scr::kValueLine, plainText(v.line)));
    if (all || v.number != was.number)
        send(uf1::buildScreen(uf1::scr::kChNumber, plainText(v.number)));
    if (all || v.csType != was.csType)
        send(uf1::buildScreen(uf1::scr::kCsType, plainText(v.csType)));
    if (all || v.chSoft != was.chSoft)
        send(uf1::buildScreen(uf1::scr::kChSoftKey, plainText(v.chSoft)));
    if (all || v.palette != was.palette)
        byte(uf1::scr::kColourBar, static_cast<std::uint8_t>(v.palette));
    if (all || v.active != was.active)
        byte(uf1::scr::kChActive, v.active ? 0x01 : 0x00);

    if (all || v.barPos != was.barPos || v.barCentre != was.barCentre) {
        const std::array<std::uint8_t, 2> bar{
            static_cast<std::uint8_t>(static_cast<std::int8_t>(
                std::clamp(v.barPos, -100, 100))), 0x80 };
        send(uf1::buildScreen(uf1::scr::kVPotReadoutBar, bar));
        byte(uf1::scr::kBarStyle, v.barCentre ? 0x08 : 0x01);
    }

    // The curve is compared as drawn, not as modelled: render first, then ask
    // whether the picture moved. Two frames go out, the refresh byte with the
    // 251st point and then the 249 columns, which is the order the device wants.
    std::array<std::uint8_t, 251> col{};
    std::uint8_t tail = 0;
    uf1eq::render(v.eq, col, tail);
    if (all || col != cache.col || tail != cache.tail) {
        const std::array<std::uint8_t, 2> refresh{ 0x01, tail };
        send(uf1::buildScreen(uf1::scr::kGraphic, refresh));
        send(uf1::buildScreen(uf1::scr::kGraphic,
                              std::span<const std::uint8_t>(col.data(), col.size())));
        cache.col  = col;
        cache.tail = tail;
    }

    cache.shown = v;
    cache.valid = true;
    return n;
}

int enterLayout(std::uint8_t layout, const Sink& out)
{
    int n = 0;
    auto send = [&](std::vector<std::uint8_t> f) { out(std::move(f)); ++n; };
    const std::uint8_t via[2] = { 0x00, 0x01 };
    send(uf1::buildScreen(0x0100, via));
    send(uf1::buildScreen(0x0100, via));
    const std::uint8_t to[2] = { layout, 0x00 };
    send(uf1::buildScreen(0x0100, to));
    // The layer's chrome, as the probe sends it after the selector.
    const std::uint8_t c0102 = 0x00, c0110 = 0x0f, c011a = 0x02;
    send(uf1::buildScreen(0x0102, std::span<const std::uint8_t>(&c0102, 1)));
    send(uf1::buildScreen(0x0110, std::span<const std::uint8_t>(&c0110, 1)));
    send(uf1::buildScreen(0x011a, std::span<const std::uint8_t>(&c011a, 1)));
    // ⛔ 0x0118 = DIE FREIGABE DER SOFT-KEY-HERVORHEBUNG IN LAYOUT 1, ein Byte
    // pro Key. Ohne sie macht das Hervorhebungsbit (0x0102) den Namen des
    // eingeschalteten Keys LEER statt ihn hervorzuheben. Am Geraet gemessen
    // 22.09. (Frank, Sonde): 01 00 00 00 gibt nur Key 1 frei, 00 01 00 00
    // nur Key 2, 01 01 01 01 alle vier, jeweils sauber an und aus. Nur NACH
    // dem Ebenenwechsel: ohne frischen Wechsel blieb die Hervorhebung hängen.
    // Layout 3 braucht es nicht (dort geht 0x0102 allein); zurueck auf den
    // Init-Wert 00 beim Verlassen (hier fuer Layout 3, uf1HandOverScreen_
    // fuer das Ende des Side-Cars).
    const std::uint8_t hl = (layout == kLayoutOverview) ? 0x01 : 0x00;
    const std::uint8_t c0118[4] = { hl, hl, hl, hl };
    send(uf1::buildScreen(0x0118, c0118));
    return n;
}

// Compose one cell's text. The 9-char label zone + 10-char value zone lives in
// uf1ValueLine; the Latin-1 fold belongs at the emit because a plug-in's param
// name is arbitrary UTF-8 and the panel is one byte per glyph.
void vpotCell(VpotRow& row, int i, const std::string& label,
                         const std::string& value)
{
    row.line[static_cast<size_t>(i)] = utf8ToLatin1(uf1ValueLine(label, value));
}

// Compose one cell's bar + style. Both rules, in one place:
//
// ⛔ A CENTRE BAR CARRIES A SIGNED DEVIATION, NOT AN ABSOLUTE POSITION. Decoded
// 2026-08-17 from cap72 by pairing every style-0x08 pot with its own dB readout:
// 0 dB → 0, full boost → 100 (0x64), full cut → 156 (0x9c = −100 in two's
// complement). Sending the plain 0..100 position put every gain at three
// quarters of the bar. Unipolar bars are unaffected — there 0..100 IS the
// position.
//
// The odd byte is BRIGHTNESS, uniformly 0x80. It was once wired to `bipolar`,
// which made V-Pot 1 read dimmer than its neighbours on any page whose first
// slot is not a gain (Frank 2026-08-17). It is not polarity.
//
// And the STYLE follows the UF8: unipolar draws a travelling LINE (0x01),
// bipolar a fill from the centre (0x08), an empty slot nothing (0x03). SSL's
// own "fill from the left" (0x02) made frequencies fill instead of point.
// Layout 1: name and value apart, the fill-from-left style 0x02 (0x03 = text
// only, for an empty pot). Measured 21.09.: 0x02, 0x03 and 0x04 render there.
void vpotCellL1(VpotRow& row, int i, const std::string& name,
                           const std::string& value, double norm, bool empty,
                uint8_t style)
{
    const size_t k = static_cast<size_t>(i);
    row.layout1 = true;
    row.names[k] = utf8ToLatin1(name).substr(0, 8);
    row.line[k]  = utf8ToLatin1(value).substr(0, 14);
    const int pos = std::clamp(static_cast<int>(std::lround(norm * 100.0)), 0, 100);
    row.bars[k * 2]     = empty ? 0 : static_cast<uint8_t>(pos);
    row.bars[k * 2 + 1] = 0x80;
    row.styles[k] = empty ? 0x03 : style;
}

void vpotBar(VpotRow& row, int i, double norm,
                        bool bipolar, bool empty)
{
    const int pos = bipolar
        ? std::clamp(static_cast<int>(std::lround((norm - 0.5) * 200.0)), -100, 100)
        : std::clamp(static_cast<int>(std::lround(norm * 100.0)),            0, 100);
    row.bars[static_cast<size_t>(i) * 2]     =
        static_cast<uint8_t>(static_cast<int8_t>(pos));
    row.bars[static_cast<size_t>(i) * 2 + 1] = 0x80;
    row.styles[static_cast<size_t>(i)] = empty ? 0x03 : (bipolar ? 0x08 : 0x01);
}

// ⇨ DIE FRAMEFOLGE DES GRAPHEN, EINMAL. Kurzer Rahmen "01 <letzter Punkt>",
// dann der volle mit "00 01" und den Spalten (cap73, siehe unten). Seit dem
// RME-Side-Car (21.09.) malen zwei Quellen denselben Graphen, REAPER/SSL und
// TotalMix, und die Folge ist so empfindlich, dass es sie nur hier gibt.
// `col` muss die Kopfbytes 0x00 0x01 schon tragen.
void eqFrames(const std::array<std::uint8_t, 251>& col, std::uint8_t tail, const Sink& out)
{
    const std::array<std::uint8_t, 2> eqRefresh{0x01, tail};
    out(uf1::buildScreen(0x0122, eqRefresh));
    out(uf1::buildScreen(0x0122,
        std::span<const std::uint8_t>(col.data(), col.size())));
}

void blankChannelZone(const Sink& out)
{
    const std::uint8_t none = 0x00;
    auto zoneText = [&](uint16_t addr) {
        // 0x00-prefixed text cell with no glyphs after the prefix.
        out(uf1::buildScreen(addr, std::span<const std::uint8_t>(&none, 1)));
    };
    zoneText(uf1::scr::kTrackName);
    zoneText(uf1::scr::kOutputDb);
    zoneText(uf1::scr::kValueLine);
    zoneText(uf1::scr::kChNumber);
    const std::uint8_t bar[] = { 0x00, 0x00 };
    out(uf1::buildScreen(uf1::scr::kVPotReadoutBar, bar));
    // 0x0006 is the firmware's OWN "channel populated" flag (it gates the colour
    // bar), so an empty slot is a state the device already knows how to show.
    out(uf1::buildScreen(uf1::scr::kChActive,
                                     std::span<const std::uint8_t>(&none, 1)));
    out(uf1::buildColourRgb(uf1::led::kSel, 0x000000u));   // unselected → dark
    out(uf1::buildLedLevel (uf1::led::kSel, uf1::led::kFf39Lit));
    out(uf1::buildLedPrimary(uf1::led::kSolo, uf1::led::kDimSolo));
    out(uf1::buildLedLevel  (uf1::led::kSolo, uf1::led::kDimSolo));
    out(uf1::buildLedPrimary(uf1::led::kCut,  uf1::led::kDimCut));
    out(uf1::buildLedLevel  (uf1::led::kCut,  uf1::led::kDimCut));
}

// Channel-view large-LCD header (0x011c, 8 x 25 bytes): "REAPER" + "1/8" +
// "OFF" template, cap77. Shared by the channel painter and the idle cycle.
extern const std::uint8_t kPluginHeader[200] = {
    0x52,0x45,0x41,0x50,0x45,0x52,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0x31,0x2f,0x38,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0x4f,0x46,0x46,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0 };

std::array<std::uint8_t, 200> pageHeader(int cur, int total, bool fine)
{
    std::array<std::uint8_t, 200> h{};
    std::memcpy(h.data(), kPluginHeader, sizeof(kPluginHeader));
    h[75] = static_cast<std::uint8_t>('0' + std::clamp(cur,   1, 9));
    h[77] = static_cast<std::uint8_t>('0' + std::clamp(total, 1, 9));
    // Field 4 (bytes 100-102) = the Quick-Key-2 "Fine Ctrl" state readout. The
    // cap77 template hardcodes "OFF"; drive it from the host's fine state so the header
    // tracks the toggle live (Frank 2026-08-04: was stuck "OFF" in every
    // channel mode). ON = "ON" + clear the 3rd byte so no stray 'F' remains.
    if (fine) { h[100] = 'O'; h[101] = 'N'; h[102] = 0x00; }
    // Cells 1, 2 and 5-7 are left at zero here: in Channel-Strip mode the firmware
    // bound only 0, 3 (SOFT KEYS page) and 4 (FINE CTRL) to screen regions. Proven
    // on hardware 2026-08-10 by writing markers into the other five — nothing
    // rendered, while the marker in cell 0 of the SAME frame did (firmware before
    // SSL 360 2.1.12). ⇨ On 2.1.12 cells 1 and 2 ARE drawn: they are rows 2 and 3
    // of the encoder list under the CHANNEL label, cell 0 is row 1, shown while
    // 0x011e = 0x1f (cap132). See uf1SetEncoderList_.
    return h;
}

void fillModeList(std::array<std::uint8_t, 200>& h,
                             int* vis, int n, int cap, int cur,
                             const char* (*nameOf)(int))
{
    int ci = -1;
    for (int i = 0; i < n; ++i) if (vis[i] == cur) { ci = i; break; }
    if (ci < 0) {                          // live mode hidden: it still leads
        if (n < cap) { vis[n] = cur; ci = n++; }
        else ci = 0;
    }
    auto put = [&](int cell, const char* s) {
        const size_t len = std::strlen(s);
        for (size_t k = 0; k < 25; ++k)
            h[static_cast<std::size_t>(cell) * 25 + k] = (k < len) ? static_cast<std::uint8_t>(s[k]) : 0;
    };
    for (int r = 0; r < 3; ++r)
        put(r, (r < n) ? nameOf(vis[(ci + r) % n]) : "");
}

} // namespace uf1spread
