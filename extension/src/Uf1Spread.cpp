#include "Uf1Spread.h"

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

int paint(const View& v, Cache& cache, const Sink& out)
{
    const bool all = !cache.valid;
    const View& was = cache.shown;
    int n = 0;
    auto send = [&](std::vector<std::uint8_t> f) { out(std::move(f)); ++n; };

    if (all || v.header != was.header)
        send(uf1::buildScreen(uf1::scr::kHeaderRow, plainText(v.header)));
    if (all || v.timecode != was.timecode)
        send(uf1::buildScreen(uf1::scr::kTimecode, plainText(v.timecode)));

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

} // namespace uf1spread
