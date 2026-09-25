//
// The first test of UF1 painting code in this project.
//
// It exists because the painter used to be unreachable: it read file-scope
// globals and wrote straight into the device, so the only way to find out what
// it drew was to plug a UF1 in and look. Every display bug this year was found
// that way, by Frank, at the desk. paint() takes a View and writes into a Sink,
// so the frames can be read here instead.
//
// What it pins is the dedup, and that is deliberate. The V-Pot row once had two
// painters with a cache each; they agreed until they did not, and leaving a mode
// left the other mode's labels on the glass because the second painter believed
// they were already there (repaired 2026-09-19). A cache is only correct if an
// unchanged view sends nothing and a changed one sends exactly its own element.
//

#include <cassert>
#include <cstdio>
#include <vector>

#include "UF1Protocol.h"
#include "Uf1Spread.h"

namespace {

struct Recorder {
    std::vector<std::vector<std::uint8_t>> frames;
    uf1spread::Sink sink()
    {
        return [this](std::vector<std::uint8_t> f) { frames.push_back(std::move(f)); };
    }
    void clear() { frames.clear(); }
    // The element address sits in the frame the way buildScreen puts it there;
    // the test only needs to know whether a given element was written at all.
    bool wrote(std::uint16_t addr) const
    {
        for (const auto& f : frames)
            for (std::size_t i = 0; i + 1 < f.size(); ++i)
                if (static_cast<std::uint16_t>((f[i] << 8) | f[i + 1]) == addr) return true;
        return false;
    }
};

uf1spread::View sampleView()
{
    uf1spread::View v;
    v.header   = "INPUT   Main";
    v.timecode = "Voc 1";
    v.pots[0]  = { "Phones1", "-6.0dB", 0.75, false, false };
    v.pots[1]  = { "Phones2", "-9.0dB", 0.60, false, false };
    v.pots[2]  = { "Main",    "0.0dB",  0.85, false, false };
    v.pots[3]  = { "",        "",       0.0,  false, true  };
    v.chName   = "Voc 1";
    v.chDb     = "-6.0";
    v.chNumber = "1";
    v.palette  = 0x0C;
    v.chActive = true;
    v.faderPos = 20000;
    return v;
}

int failures = 0;
void check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
}

} // namespace

int main()
{
    // A cold cache draws everything.
    {
        Recorder rec;
        uf1spread::Cache c;
        const int n = uf1spread::paint(sampleView(), c, rec.sink());
        check(n > 0, "a cold cache writes something");
        check(static_cast<std::size_t>(n) == rec.frames.size(), "the count matches the sink");
        check(rec.wrote(uf1::scr::kHeaderRow),   "header written");
        check(rec.wrote(uf1::scr::kVpotBars),    "bars written");
        check(rec.wrote(uf1::scr::kVpotStyle),   "styles written");
        check(rec.wrote(uf1::scr::kTrackName),   "channel name written");
        check(rec.wrote(uf1::scr::kColourBar),   "colour bar written");
        check(rec.wrote(uf1::scr::kChActive),    "populated flag written");
    }

    // ⛔ The property the old painter could not prove about itself: painting the
    // same picture twice puts nothing more on the wire.
    {
        Recorder rec;
        uf1spread::Cache c;
        uf1spread::paint(sampleView(), c, rec.sink());
        rec.clear();
        const int n = uf1spread::paint(sampleView(), c, rec.sink());
        check(n == 0, "an unchanged view writes nothing");
        check(rec.frames.empty(), "and hands the sink nothing");
    }

    // One pot's text moves: its own two fields go, the row's bars and styles do
    // not, because neither changed.
    {
        Recorder rec;
        uf1spread::Cache c;
        uf1spread::paint(sampleView(), c, rec.sink());
        rec.clear();
        auto v = sampleView();
        v.pots[1].line = "-12.0dB";
        uf1spread::paint(v, c, rec.sink());
        check(rec.wrote(uf1::scr::kFocusedParam), "the changed pot's value line goes");
        check(!rec.wrote(uf1::scr::kVpotBars),    "the bars stay put");
        check(!rec.wrote(uf1::scr::kVpotStyle),   "the styles stay put");
        check(!rec.wrote(uf1::scr::kHeaderRow),   "the header stays put");
    }

    // A pot's position moves: the bars go, the styles do not. Two gates, not
    // one — a page whose positions repeat still needs its styles corrected, and
    // the other way round.
    {
        Recorder rec;
        uf1spread::Cache c;
        uf1spread::paint(sampleView(), c, rec.sink());
        rec.clear();
        auto v = sampleView();
        v.pots[0].norm = 0.25;
        uf1spread::paint(v, c, rec.sink());
        check(rec.wrote(uf1::scr::kVpotBars),  "the bars follow the position");
        check(!rec.wrote(uf1::scr::kVpotStyle), "the styles do not");
    }

    // Bipolar and empty are style, not position.
    {
        Recorder rec;
        uf1spread::Cache c;
        uf1spread::paint(sampleView(), c, rec.sink());
        rec.clear();
        auto v = sampleView();
        v.pots[2].bipolar = true;
        uf1spread::paint(v, c, rec.sink());
        check(rec.wrote(uf1::scr::kVpotStyle), "bipolar changes the style");
    }

    // A fader we do not own is not driven anywhere.
    {
        Recorder rec;
        uf1spread::Cache c;
        auto v = sampleView();
        v.faderPos = -1;
        uf1spread::paint(v, c, rec.sink());
        bool motor = false;
        for (const auto& f : rec.frames)
            if (f.size() >= 2 && f[0] == 0xFF && f[1] == 0x60) motor = true;
        check(!motor, "faderPos -1 leaves the motor alone");
    }

    // ── STRIP ────────────────────────────────────────────────────────────────
    auto strip = [] {
        uf1spread::StripView v;
        v.name = "Voc 1"; v.db = "-6.0"; v.line = "GAIN  +12.0dB";
        v.number = "1"; v.csType = "PREAMP"; v.chSoft = "MONO";
        v.palette = 0x0C; v.active = true; v.barPos = 40;
        v.eq.on = true;
        v.eq.bands.push_back({ uf1eq::Band::Kind::Bell, 1000.0, 3.0, 1.0, 2 });
        return v;
    };

    {
        Recorder rec;
        uf1spread::StripCache c;
        const int n = uf1spread::paintStrip(strip(), c, rec.sink());
        check(n > 0, "STRIP: a cold cache writes something");
        check(rec.wrote(uf1::scr::kTrackName), "STRIP: name written");
        check(rec.wrote(uf1::scr::kValueLine), "STRIP: value line written");
        check(rec.wrote(uf1::scr::kCsType),    "STRIP: type cell written");
        check(rec.wrote(uf1::scr::kGraphic),   "STRIP: the curve is drawn");
    }

    {
        Recorder rec;
        uf1spread::StripCache c;
        uf1spread::paintStrip(strip(), c, rec.sink());
        rec.clear();
        const int n = uf1spread::paintStrip(strip(), c, rec.sink());
        check(n == 0, "STRIP: an unchanged view writes nothing");
    }

    // The value line moves under the hand while the curve stands still. The
    // graph is two frames of 251 bytes; redrawing it for a text change is the
    // kind of waste that made the display drop to lazy render-on-idle.
    {
        Recorder rec;
        uf1spread::StripCache c;
        uf1spread::paintStrip(strip(), c, rec.sink());
        rec.clear();
        auto v = strip();
        v.line = "GAIN  +13.0dB";
        uf1spread::paintStrip(v, c, rec.sink());
        check(rec.wrote(uf1::scr::kValueLine), "STRIP: the value line follows");
        check(!rec.wrote(uf1::scr::kGraphic),  "STRIP: the curve is left alone");
    }

    // A band moves: the curve is redrawn.
    {
        Recorder rec;
        uf1spread::StripCache c;
        uf1spread::paintStrip(strip(), c, rec.sink());
        rec.clear();
        auto v = strip();
        v.eq.bands[0].gainDb = 6.0;
        uf1spread::paintStrip(v, c, rec.sink());
        check(rec.wrote(uf1::scr::kGraphic), "STRIP: the curve follows a band");
    }

    // ⛔ EQ off is a statement. Switching it off has to reach the glass, or the
    // last curve stands there claiming an EQ that is out.
    {
        Recorder rec;
        uf1spread::StripCache c;
        uf1spread::paintStrip(strip(), c, rec.sink());
        rec.clear();
        auto v = strip();
        v.eq.on = false;
        uf1spread::paintStrip(v, c, rec.sink());
        check(rec.wrote(uf1::scr::kGraphic), "STRIP: EQ off redraws the graph flat");
    }

    if (failures == 0) std::printf("test_orc_paint: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
