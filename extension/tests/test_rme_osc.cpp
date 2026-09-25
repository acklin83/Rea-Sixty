// Unit tests for the RME Global OSC codec and the TotalMix state cache.
//
// The bytes and values here are not invented: they come from a live dump taken
// on 2026-09-19 from TotalMix FX 2.10 alpha 8 on OSC Remote 1 (UDP 7001 in,
// 7002 back), answering /sendall with 3595 addresses. What is pinned:
//
//   · a round trip through the encoder, because a wrong pad byte is silent
//   · BUNDLES, because every TotalMix packet is one and a decoder without them
//     sees nothing at all
//   · the three-valued snapshot echo (0 off / 2 active / 3 changed)
//   · ⛔ that a ROLE pointing at a channel this remote cannot see is a STATE,
//     which is the one thing on Frank's rig that would otherwise read as a bug
//
#include "RmeManager.h"
#include "RmeNames.h"
#include "RmeOsc.h"
#include "RmeState.h"
#include "RmeUf1.h"
#include "Uf1EqCurve.h"
#include "RmeStrip.h"
#include "RmeUf1.h"

#include <algorithm>
#include <cmath>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace reasixty::rme;

static int g_fail = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}

static std::vector<std::uint8_t> bundleOf(
    const std::vector<std::vector<std::uint8_t>>& msgs)
{
    std::vector<std::uint8_t> b;
    const char* hdr = "#bundle";
    b.insert(b.end(), hdr, hdr + 8);            // includes the NUL
    for (int i = 0; i < 8; ++i) b.push_back(0); // time tag
    for (const auto& m : msgs) {
        const std::uint32_t n = static_cast<std::uint32_t>(m.size());
        b.push_back(static_cast<std::uint8_t>((n >> 24) & 0xFF));
        b.push_back(static_cast<std::uint8_t>((n >> 16) & 0xFF));
        b.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFF));
        b.push_back(static_cast<std::uint8_t>(n & 0xFF));
        b.insert(b.end(), m.begin(), m.end());
    }
    return b;
}

int main()
{
    // ── encode / decode round trip ──────────────────────────────────────────
    {
        const auto wire = encode("/mix/in/38/6/fader",
                                 { Arg::fromFloat(-12.5f) });
        check(wire.size() % 4 == 0, "an encoded message is 4-byte aligned");
        std::vector<Message> got;
        const std::size_t n = forEachMessage(wire, [&](const Message& m) { got.push_back(m); });
        check(n == 1 && got.size() == 1, "a bare message decodes");
        check(got[0].address == "/mix/in/38/6/fader", "address survives");
        check(got[0].args.size() == 1 && got[0].args[0].number() < -12.4
              && got[0].args[0].number() > -12.6, "float survives");
    }
    {
        // An address whose length is an exact multiple of 4 needs a FULL pad
        // word, not zero bytes. This is the classic off-by-one in OSC encoders.
        const auto wire = encode("/dim", { Arg::fromFloat(1.0f) });
        std::vector<Message> got;
        forEachMessage(wire, [&](const Message& m) { got.push_back(m); });
        check(got.size() == 1 && got[0].address == "/dim",
              "a 4-byte address keeps its terminator");
    }
    {
        const auto wire = encode("/status/device", { Arg::fromString("Fireface UFX+") });
        std::vector<Message> got;
        forEachMessage(wire, [&](const Message& m) { got.push_back(m); });
        check(got.size() == 1 && got[0].args.size() == 1
              && got[0].args[0].type == Arg::Type::String
              && got[0].args[0].s == "Fireface UFX+", "string argument survives");
    }

    // ── bundles, which is how TotalMix actually speaks ──────────────────────
    {
        const auto pkt = bundleOf({
            encodeFloat("/controlroom/dim", 1.0f),
            encodeFloat("/controlroom/mainmono", 0.0f),
            encode("/output/8/name", { Arg::fromString("Phones 1") }),
        });
        std::vector<Message> got;
        const std::size_t n = forEachMessage(pkt, [&](const Message& m) { got.push_back(m); });
        check(n == 3 && got.size() == 3, "a bundle yields every message in it");
        check(got[2].address == "/output/8/name", "…in order");
    }
    {
        const auto inner = bundleOf({ encodeFloat("/controlroom/talkback", 1.0f) });
        const auto outer = bundleOf({ inner, encodeFloat("/globalmute", 1.0f) });
        std::size_t n = 0;
        forEachMessage(outer, [&](const Message&) { ++n; });
        check(n == 2, "a bundle inside a bundle is walked too");
    }
    {
        const std::vector<std::uint8_t> junk{ 0x01, 0x02, 0x03 };
        check(forEachMessage(junk, [](const Message&) {}) == 0,
              "garbage yields nothing and does not crash");
    }

    // ── the state cache, with Frank's own values ────────────────────────────
    State st;
    auto feed = [&](const std::string& addr, float v) {
        Message m; m.address = addr; m.args.push_back(Arg::fromFloat(v));
        ingest(st, m);
    };
    auto feedStr = [&](const std::string& addr, const std::string& v) {
        Message m; m.address = addr; m.args.push_back(Arg::fromString(v));
        ingest(st, m);
    };

    feed("/controlroom/mainout", 0.0f);
    feed("/controlroom/mainoutb", 6.0f);
    feed("/controlroom/phones1", 8.0f);
    feed("/controlroom/phones2", 10.0f);
    feed("/controlroom/phones3", 2.0f);     // hidden on this remote
    feed("/controlroom/phones4", -1.0f);    // unassigned
    feed("/controlroom/dimreduction", -20.0f);
    feed("/controlroom/dim", 0.0f);
    feed("/controlroom/linkab", 1.0f);
    feedStr("/output/0/name", "Main");
    feedStr("/output/6/name", "Speaker B");
    feedStr("/output/8/name", "Phones 1");
    feedStr("/output/10/name", "Phones 2");
    feed("/output/8/volume", -16.23f);

    check(st.mainOut == 0 && st.mainOutB == 6, "the main roles land");
    check(st.dimReduction < -19.9 && st.dimReduction > -20.1, "dim reduction is dB");
    check(st.linkAB, "link A/B is a flag");
    check(st.outputForRole(st.phones[0]) != nullptr
          && st.outputForRole(st.phones[0])->name == "Phones 1",
          "phones 1 resolves to its channel");
    check(st.outputForRole(st.phones[0])->volume < -16.2, "…with its volume");

    // ⛔ The two "no channel" answers are DIFFERENT and the surface has to tell
    // them apart: phones 4 was never assigned, phones 3 is assigned to a channel
    // this remote is not allowed to see.
    check(!State::roleAssigned(st.phones[3]), "phones 4 is unassigned");
    check(!st.roleHidden(st.phones[3]), "…which is not the same as hidden");
    check(State::roleAssigned(st.phones[2]), "phones 3 IS assigned");
    check(st.roleHidden(st.phones[2]), "…but points at a channel we never see");

    // ── ⛔ A RIGHT HALF IS NOT A CHANNEL ────────────────────────────────────
    // Per-side parameters (phase, delay, gain, every room-EQ band) are
    // addressed on index + 1. A first pass that marked a channel seen on ANY
    // message counted 94 outputs on Frank's rig where TotalMix shows 5, and the
    // role resolver would have handed one of those phantoms back as visible.
    feed("/output/9/phase", 0.0f);
    check(st.outputs.count(9) == 1, "the right half is still tracked");
    check(!st.outputs[9].seen, "…but it is NOT a strip: no name, no identity");
    check(st.outputForRole(9) == nullptr, "…and a role can never resolve to it");
    {
        std::size_t strips = 0;
        for (const auto& kv : st.outputs) if (kv.second.seen) ++strips;
        check(strips == 4, "only the named outputs count as channels");
    }

    // ── snapshots come back three-valued ────────────────────────────────────
    feed("/snapshot/load/1", 0.0f);
    feed("/snapshot/load/5", 3.0f);
    feed("/snapshot/load/6", 2.0f);
    check(st.snapshot[0] == SnapshotState::Off, "slot 1 is off");
    check(st.snapshot[4] == SnapshotState::Changed, "slot 5 is CHANGED, not on");
    check(st.snapshot[5] == SnapshotState::Active, "slot 6 is active");
    // Our own load (value 1, folded in by Manager::send): that slot is the
    // active one at once, every other is off, a changed one included.
    feed("/snapshot/load/3", 1.0f);
    check(st.snapshot[2] == SnapshotState::Active, "a slot we load is active at once");
    check(st.snapshot[4] == SnapshotState::Off && st.snapshot[5] == SnapshotState::Off,
          "...and the others, changed or active, are off");
    // Layouts have no answer, so the state holds the one we sent, 1-based on the wire.
    check(st.lastLayout == -1, "no layout until one is sent");
    feed("/layout/load/4", 1.0f);
    check(st.lastLayout == 3, "the layout last sent, 0-based");

    // ── colour index 0 means hidden, and it is an INDEX, not RGB ────────────
    feed("/input/0/color", 0.0f);
    feed("/input/1/color", 5.0f);
    check(st.inputs[0].colour == 0, "colour 0 is the hidden marker");
    check(st.inputs[1].colour == 5, "colour is a palette index");

    // ── levels only arrive with Send Peak Level on; empty is the normal state
    check(st.levelOut.empty(), "no levels until the mixer is told to send them");
    feed("/level/out/8", -6.5f);
    check(st.levelOut.count(8) == 1 && st.levelOut[8] < -6.4, "a level lands on its bus");

    // ── the fader curve, measured 2026-09-21 on /mix/pb/0/10 ────────────────
    auto near = [](double a, double b, double tol) { return std::fabs(a - b) <= tol; };
    check(faderlinToDb(0.0) == kDbOff, "faderlin 0 is OFF");
    check(near(faderlinToDb(0.3), -27.584, 0.001), "0.3 = -27.584 dB (outputs and nodes)");
    check(near(faderlinToDb(0.5), -12.1254, 0.001), "0.5 = -12.13 dB");
    check(near(faderlinToDb(1.0), 6.0, 0.001), "1.0 = +6 dB, playbacks included");
    {
        // The 21 points measured on Frank's rig before RME's formula was at
        // hand (/mix/pb/0/10, 2026-09-21). The formula has to hit every one.
        const double measured[21] = {
            kDbOff,   -57.5783, -50.6309, -44.1578, -38.1589, -32.6343, -27.584,
            -23.0079, -18.9061, -15.2786, -12.1254, -9.44641, -7.24171, -5.48824,
            -3.84706, -2.20588, -0.564707, 1.07647, 2.71764, 4.35882, 6.0 };
        bool all = faderlinToDb(0.0) == kDbOff;
        for (int i = 1; i <= 20; ++i)
            if (!near(faderlinToDb(i * 0.05), measured[i], 0.01)) all = false;
        check(all, "RME's formula lands on all 21 measured points");
    }
    check(dbToFaderlin(kDbOff) == 0.0, "OFF maps back to 0");
    check(dbToFaderlin(20.0) == 1.0, "above +6 clamps to the top");
    {
        bool ok = true, mono = true;
        double prev = -1e9;
        for (int i = 1; i <= 100; ++i) {
            const double x  = i / 100.0;
            const double db = faderlinToDb(x);
            if (db <= prev) mono = false;
            prev = db;
            if (!near(dbToFaderlin(db), x, 1e-6)) ok = false;
        }
        check(mono, "the curve only ever rises");
        check(ok, "faderlin -> dB -> faderlin comes back, both directions");
    }

    // ── ⛔ TotalMix does not echo our own writes, so we fold them in ourselves
    {
        const Message e = localEcho("/output/10/faderlin", 0.3f);
        check(e.address == "/output/10/volume", "an output's faderlin echoes as volume");
        check(!e.args.empty() && near(e.args[0].number(), -27.584, 0.01),
              "…in dB, on TotalMix' curve");
        const Message n = localEcho("/mix/in/0/10/faderlin", 0.3f);
        check(n.address == "/mix/in/0/10/fader", "a node's faderlin echoes as fader");
        const Message d = localEcho("/controlroom/dim", 1.0f);
        check(d.address == "/controlroom/dim" && !d.args.empty()
              && d.args[0].number() == 1.0, "anything else echoes as itself");
        State s2;
        ingest(s2, localEcho("/output/10/faderlin", 0.3f));
        check(near(s2.outputs[10].volume, -27.584, 0.01), "and the state takes the echo");
    }

    // ── rme.json ─────────────────────────────────────────────────────────────
    {
        Config c;
        check(!c.enabled && c.sendPort == 7005 && c.recvPort == 7006,
              "defaults: off, Remote 3 (7005 out, 7006 back)");
        c.enabled = true; c.host = "192.168.177.83"; c.sendPort = 7007; c.recvPort = 7008;
        Config r;
        check(configFromJson(configToJson(c), r), "what we write, we read");
        check(r.enabled && r.host == c.host && r.sendPort == 7007 && r.recvPort == 7008,
              "round trip keeps every field");
        Config keep; keep.host = "10.0.0.1";
        check(!configFromJson("not json", keep) && keep.host == "10.0.0.1",
              "a broken file changes nothing");
        Config extra;
        check(configFromJson("{\"future\": {\"a\": 1}, \"connection\": {\"send\": 7005}}", extra)
              && extra.sendPort == 7005, "unknown fields are not an error");
        Config bad;
        configFromJson("{\"connection\": {\"send\": 99999, \"receive\": 0}}", bad);
        check(bad.sendPort == 7005 && bad.recvPort == 7006, "impossible ports fall back");
    }

    // ── rme.json carries the whole side-car layout ──────────────────────────
    {
        Config c;
        check(c.vpots[0].target == "phones1" && c.vpots[3].target == "phones4"
              && c.vpots[0].push == "submix" && c.jogTarget == "main",
              "defaults: Phones 1-4 on the pots, push picks the submix, Main on the jog");
        // A v1 file wrote the old default "select" for every pot. Frank: a push
        // on Phones 1-4 must not move the fader. v1 could not mean it on purpose.
        Config v1;
        configFromJson("{\"version\": 1, \"vpots\": [{\"target\": \"phones1\", "
                       "\"push\": \"select\"}, {\"push\": \"mute\"}]}", v1);
        check(v1.vpots[0].push == "submix" && v1.vpots[1].push == "mute",
              "v1 'select' becomes 'submix', anything else stays");
        Config v2;
        configFromJson("{\"version\": 2, \"vpots\": [{\"push\": \"select\"}]}", v2);
        check(v2.vpots[0].push == "select", "from v2 on, 'select' is a choice and stays");
        c.vpots[2].target = "input:30"; c.vpots[2].push = "mute";
        c.jogTarget = "phones2"; c.jogStepDb = 1.0; c.colourMap[4] = 0x09;
        Config r;
        check(configFromJson(configToJson(c), r), "layout round trip parses");
        check(r.vpots[2].target == "input:30" && r.vpots[2].push == "mute"
              && r.jogTarget == "phones2" && r.jogStepDb == 1.0 && r.colourMap[4] == 0x09,
              "…and keeps every pot, the jog and the colours");
        // Two banks (Frank 21.09.): Phones 1-4, then Main A / Main B and two empty.
        check(c.vpots[4].target == "main" && c.vpots[5].target == "mainB"
              && c.vpots[6].target.empty() && c.vpots[7].target.empty(),
              "bank 2 defaults: Main A, Main B, two empty pots");
        check(r.vpots[6].target.empty() && r.vpots[5].target == "mainB",
              "an empty pot survives the round trip as empty");
        // A v2 file has four pots: they stay bank 1, bank 2 keeps its defaults.
        Config v2b;
        configFromJson("{\"version\": 2, \"vpots\": [{\"target\": \"output:4\"}, {}, {}, {}]}", v2b);
        check(v2b.vpots[0].target == "output:4" && v2b.vpots[4].target == "main"
              && v2b.vpots[5].target == "mainB", "a v2 file fills bank 1 only");
        Config a, b = a;
        b.vpots[1].target = "output:4";
        check(a.sameConnection(b), "moving a pot is not a reconnect");
        b.recvPort = 7010;
        check(!a.sameConnection(b), "moving a port is");
    }

    // ── the UF1 side-car's view of TotalMix (RmeUf1) ────────────────────────
    {
        namespace u = reasixty::rme::uf1;
        State s3;
        auto put = [&](const char* addr, float v) {
            Message m; m.address = addr; m.args.push_back(Arg::fromFloat(v)); ingest(s3, m);
        };
        auto name = [&](const char* addr, const char* n) {
            Message m; m.address = addr; m.args.push_back(Arg::fromString(n)); ingest(s3, m);
        };
        name("/output/0/name", "Main");      put("/output/0/color", 1);  put("/output/0/volume", -20.0f);
        name("/output/8/name", "Phones 1");  put("/output/8/color", 1);  put("/output/8/volume", -16.0f);
        name("/output/10/name", "Phones 2"); put("/output/10/color", 1);
        name("/input/0/name", "Voc 1");      put("/input/0/color", 8);
        name("/input/2/name", "Git");        put("/input/2/color", 0);   // hidden
        name("/input/6/name", "Bass");       put("/input/6/color", 5);
        name("/playback/0/name", "AN 1/2");  put("/playback/0/color", 1);
        put("/controlroom/mainout", 0); put("/controlroom/phones1", 8);
        put("/controlroom/phones2", 10); put("/controlroom/phones3", 2);
        put("/mix/in/0/10/fader", -3.7f);
        put("/mix/pb/0/10/fader", -4.1f);

        const auto ins = u::visibleChannels(s3, u::Row::Input);
        check(ins.size() == 2 && ins[0] == 0 && ins[1] == 6,
              "a row walks the VISIBLE channels only (colour 0 skipped)");
        check(u::stepChannel(ins, 0, 1) == 6 && u::stepChannel(ins, 6, 1) == 6
              && u::stepChannel(ins, 6, -5) == 0, "stepping clamps at both ends");
        check(u::stepChannel(ins, 2, 1) == 6 && u::stepChannel(ins, 2, -1) == 0,
              "from a channel that vanished, one detent still moves");

        const auto p1 = u::resolveTarget(s3, "phones1");
        check(p1.row == u::Row::Output && p1.ch == 8 && p1.visible, "phones1 is the role");
        const auto p3 = u::resolveTarget(s3, "phones3");
        check(p3.assigned && !p3.visible, "a role on a channel this remote cannot see");
        const auto p4 = u::resolveTarget(s3, "phones4");
        check(!p4.assigned, "a role TotalMix never named");
        const auto fx = u::resolveTarget(s3, "input:6");
        check(fx.row == u::Row::Input && fx.ch == 6 && fx.visible, "a fixed channel");

        check(u::effectiveSubmix(s3, 10) == 10, "a visible output is the submix");
        check(u::effectiveSubmix(s3, 4) == 0, "an unknown one falls back to Main");

        bool known = false;
        check(u::levelDb(s3, u::Row::Input, 0, 10, known) < -3.6 && known,
              "an input's level is its node in the submix");
        u::levelDb(s3, u::Row::Input, 6, 10, known);
        check(!known, "a node TotalMix never sent is unknown, not off");
        check(u::levelAddress(u::Row::Input, 0, 10, true) == "/mix/in/0/10/faderlin"
              && u::levelAddress(u::Row::Playback, 0, 10, false) == "/mix/pb/0/10/fader"
              && u::levelAddress(u::Row::Output, 8, 0, true) == "/output/8/faderlin",
              "level addresses");

        // EQ: off is flat; on gives three bands; the low cut takes its slope.
        check(!u::eqModel(s3, u::Row::Output, 8).on, "no EQ reported = flat");
        put("/output/8/eq/enable", 1); put("/output/8/eq/band1type", 1);
        put("/output/8/eq/band3type", 1); put("/output/8/eq/band1gain", 3);
        put("/output/8/lowcut/enable", 1); put("/output/8/lowcut/freq", 80);
        put("/output/8/lowcut/slope", 3);
        const auto em = u::eqModel(s3, u::Row::Output, 8);
        check(em.on && em.bands.size() == 4, "EQ on: three bands and the low cut");
        check(em.bands[0].kind == uf1eq::Band::Kind::LowShelf
              && em.bands[2].kind == uf1eq::Band::Kind::HighShelf,
              "Shelve is low on band 1 and high on band 3");
        check(em.bands[3].kind == uf1eq::Band::Kind::HighPass && em.bands[3].order == 4
              && em.bands[3].freq == 80.0, "slope index 3 draws 24 dB/oct");
        put("/playback/0/eq/enable", 1);
        check(!u::eqModel(s3, u::Row::Playback, 0).on, "a playback never has an EQ");

        name("/output/2/name", "MADI 49/50"); put("/output/2/color", 1);
        check(u::displayName(s3, u::Row::Output, 2) == "Phones 3",
              "an output with a role is shown by the role, not its strip name");
        check(u::displayName(s3, u::Row::Output, 0) == "Main", "Main by role");
        check(u::displayName(s3, u::Row::Input, 6) == "Bass", "an input by its name");
        put("/input/6/stereo", 1);
        check(s3.inputs[6].stereo && !s3.inputs[0].stereo, "the stereo flag lands per strip");
        check(u::nudgeDb(kDbOff, 1, 0.5) == -64.5, "off climbs to TotalMix' floor first");
        check(u::nudgeDb(kDbOff, 3, 0.5) == -63.5, "and steps on from there");
        check(u::nudgeDb(-60.0, -1, 0.5) == -60.5 && u::nudgeDb(5.8, 2, 0.5) == 6.0,
              "nudges in steps, clamps at +6");
        // TotalMix stops at -64.5 on every channel (Frank 25.09.): turning on
        // down stays there, it never goes off.
        check(u::nudgeDb(-64.3, -1, 0.5) == -64.5, "clamps at -64.5");
        check(u::nudgeDb(-64.5, -10, 0.5) == -64.5, "and stays there");
        check(u::nudgeDb(kDbOff, -1, 0.5) == kDbOff, "off turned down stays off");

        // SOLO sits on the routing, CUT on the strip (side-car, 21.09.).
        check(u::muteAddress(u::Row::Input, 6) == "/input/6/mute"
              && u::muteAddress(u::Row::Playback, 0) == "/playback/0/mute"
              && u::muteAddress(u::Row::Output, 8) == "/output/8/mute",
              "mute is per strip, in all three rows");
        check(u::soloAddress(u::Row::Input, 6, 10) == "/mix/in/6/10/solo"
              && u::soloAddress(u::Row::Playback, 0, 10) == "/mix/pb/0/10/solo",
              "solo goes into the submix");
        check(u::soloAddress(u::Row::Output, 8, 10).empty()
              && u::soloAddress(u::Row::Input, 6, -1).empty(),
              "no solo on an output, none without a submix");
        check(!u::soloed(s3, u::Row::Input, 6, 10), "solo is off until TotalMix says so");
        put("/mix/in/6/10/solo", 1);
        check(u::soloed(s3, u::Row::Input, 6, 10) && !u::soloed(s3, u::Row::Input, 6, 8),
              "solo lands per node, not per strip");
        check(u::panAddress(u::Row::Input, 6, 10) == "/mix/in/6/10/balpan"
              && u::panAddress(u::Row::Output, 8, 10) == "/output/8/balpan"
              && u::panAddress(u::Row::Input, 6, -1).empty(),
              "pan: an input pans into the submix, an output pans itself");
        bool pk = true;
        u::panValue(s3, u::Row::Input, 6, 10, pk);
        check(!pk, "pan unknown until TotalMix says so");
        put("/mix/in/6/10/balpan", -0.5f);
        check(u::panValue(s3, u::Row::Input, 6, 10, pk) == -0.5 && pk,
              "a node's pan lands per submix");
        put("/output/8/balpan", 0.25f);
        check(u::panValue(s3, u::Row::Output, 8, -1, pk) == 0.25 && pk,
              "an output's pan is its own strip leaf");
        put("/mix/in/6/10/solo", 0);
        check(!u::soloed(s3, u::Row::Input, 6, 10), "and turns off again");
    }

    // ── the channel view (RmeStrip), 21.09. ─────────────────────────────────
    {
        namespace sp = reasixty::rme::strip;
        using Row = reasixty::rme::uf1::Row;
        State s;
        auto put = [&](const char* addr, float v) {
            Message m; m.address = addr; m.args.push_back(Arg::fromFloat(v)); ingest(s, m);
        };
        auto name = [&](const char* addr, const char* n) {
            Message m; m.address = addr; m.args.push_back(Arg::fromString(n)); ingest(s, m);
        };
        // Input 8 is a mono mic channel, input 30 a stereo pair (31 = right half).
        name("/input/8/name", "Kick"); put("/input/8/gain", 30); put("/input/8/48v", 1);
        put("/input/8/phase", 0); put("/input/8/eq/band1gain", 2);
        put("/input/8/dynamics/compthres", -20);
        name("/input/30/name", "Keys"); put("/input/30/stereo", 1);
        put("/input/30/gain", 10); put("/input/30/width", 1); put("/input/30/msproc", 0);
        put("/input/30/phase", 0); put("/input/31/phase", 1);

        check(s.inputs[8].leaf("gain") && *s.inputs[8].leaf("gain") == 30.0,
              "every numeric leaf lands in the channel");
        check(!s.inputs[8].leaf("name"), "a name is not a leaf");
        State copy = s;
        put("/input/8/gain", 31);
        check(*copy.inputs[8].leaf("gain") == 30.0 && *s.inputs[8].leaf("gain") == 31.0,
              "a snapshot keeps its values when the live state moves on");

        const sp::Param* width  = sp::find("width");
        const sp::Param* phaseR = sp::find("phaseR");
        const sp::Param* gain   = sp::find("gain");
        check(width && phaseR && gain && !sp::find("nope") && !sp::find(""),
              "catalogue finds known ids, and only those");
        check(!sp::available(s, Row::Input, 8, *width) && sp::available(s, Row::Input, 30, *width),
              "width only on a stereo strip");
        check(!sp::available(s, Row::Input, 8, *phaseR) && sp::available(s, Row::Input, 30, *phaseR),
              "Phase R only on a stereo strip");
        double v = 0;
        check(sp::value(s, Row::Input, 30, *phaseR, v) && v == 1.0,
              "Phase R reads the right half, index + 1");
        check(sp::label(s, Row::Input, 8, *sp::find("phase")) == "Phase"
              && sp::label(s, Row::Input, 30, *sp::find("phase")) == "Phase L",
              "a mono strip's phase has no L");

        const auto pages = reasixty::rme::defaultStripPages();
        const auto mono = sp::availablePages(s, Row::Input, 8, pages);
        std::vector<std::string> names;
        for (int i : mono) names.push_back(pages[static_cast<size_t>(i)].name);
        check(names == std::vector<std::string>{ "Input", "EQ 1", "Dyn" },
              "pages follow what TotalMix reported: Input, EQ 1, Dyn");
        const auto st2 = sp::availablePages(s, Row::Input, 30, pages);
        check(std::find(st2.begin(), st2.end(), 1) != st2.end(),
              "a stereo strip with M/S gets Input 2");
        check(sp::pageShowsGraph(pages[3]) && sp::pageShowsGraph(pages[2])
              && !sp::pageShowsGraph(pages[0]) && !sp::pageShowsGraph(pages[6]),
              "EQ and Low Cut pages draw the graph, the rest do not");
        check(!sp::pageAllowsRow(pages[0], Row::Output) && sp::pageAllowsRow(pages[0], Row::Playback)
              && sp::pageAllowsRow(pages[9], Row::Output) && !sp::pageAllowsRow(pages[9], Row::Input),
              "Input pages are in/pb, the Output page is out");

        auto w = sp::nudge(s, Row::Input, 8, *gain, 3);
        check(w.size() == 1 && w[0].first == "/input/8/gain" && w[0].second == 34.0f,
              "gain moves in whole dB on a mono strip");
        w = sp::nudge(s, Row::Input, 30, *gain, -2);
        check(w.size() == 2 && w[0].first == "/input/30/gain" && w[1].first == "/input/31/gain"
              && w[1].second == 8.0f, "gain goes to both halves of a stereo pair");
        w = sp::nudge(s, Row::Input, 8, *gain, -100);
        check(w.size() == 1 && w[0].second == 0.0f, "clamped at the bottom");
        w = sp::press(s, Row::Input, 30, *phaseR);
        check(w.size() == 1 && w[0].first == "/input/31/phase" && w[0].second == 0.0f,
              "Phase R toggles the right half only");
        w = sp::press(s, Row::Input, 8, *sp::find("48v"));
        check(w.size() == 1 && w[0].second == 0.0f, "a key toggles");
        put("/input/8/eq/band1type", 3);
        w = sp::press(s, Row::Input, 8, *sp::find("b1type"));
        check(w.size() == 1 && w[0].second == 0.0f, "a list key wraps round");
        put("/input/8/eq/band1freq", 1000);
        w = sp::nudge(s, Row::Input, 8, *sp::find("b1freq"), 12);
        check(w.size() == 1 && std::fabs(w[0].second - 2000.0f) < 0.5f,
              "twelve detents are one octave");
        check(sp::nudge(s, Row::Input, 8, *width, 1).empty(), "nothing to nudge where it is absent");
        put("/input/8/eq/band1gain", 2);
        w = sp::nudge(s, Row::Input, 8, *sp::find("b1gain"), 1, 0.25);
        check(w.size() == 1 && std::fabs(w[0].second - 2.125f) < 1e-4f,
              "Fine quarters a continuous step");
        put("/input/8/eq/band1type", 0);
        w = sp::nudge(s, Row::Input, 8, *sp::find("b1type"), 1, 0.25);
        check(w.size() == 1 && w[0].second == 1.0f, "Fine leaves a list step whole");
        check(sp::stepsWhole(*sp::find("b1type")) && sp::stepsWhole(*sp::find("crossfeed"))
              && !sp::stepsWhole(*sp::find("b1gain")), "lists and numbers step per detent");
        put("/input/30/width", 0.0f);
        w = sp::nudge(s, Row::Input, 30, *width, -100);
        check(w.size() == 1 && w[0].second == -1.0f, "width reaches -1 (swapped)");

        check(sp::format(*sp::find("b1freq"), Row::Input, 1200) == "1.20 kHz"
              && sp::format(*sp::find("compratio"), Row::Input, 4) == "4.0:1"
              && sp::format(*sp::find("balpan"), Row::Output, -0.5) == "L50"
              && sp::format(*sp::find("reflevel"), Row::Output, 1) == "+4 dBu"
              && sp::format(*sp::find("reflevel"), Row::Input, 1) == "LoGain",
              "values read as TotalMix shows them");
        check(sp::sendChanAddress(Row::Playback, 4) == "/sendchan/playback/4", "sendchan");
        // V-Pot push = neutral (22.09.): 0 dB where the range holds it, width 1,
        // pan centre; nothing where no default is belegt.
        put("/input/8/eq/band1gain", 4.5f);
        w = sp::resetWrites(s, Row::Input, 8, *sp::find("b1gain"));
        check(w.size() == 1 && w[0].first == "/input/8/eq/band1gain" && w[0].second == 0.0f,
              "push resets an EQ gain to 0 dB");
        w = sp::resetWrites(s, Row::Input, 30, *gain);
        check(w.size() == 2 && w[0].second == 0.0f, "preamp gain to 0 dB, both halves");
        put("/input/30/width", 0.3f);
        w = sp::resetWrites(s, Row::Input, 30, *width);
        check(w.size() == 1 && w[0].second == 1.0f, "width back to stereo");
        put("/input/8/eq/band1freq", 250); put("/input/8/eq/band1type", 0);
        w = sp::resetWrites(s, Row::Input, 8, *sp::find("b1freq"));
        check(w.size() == 1 && w[0].second == 80.0f, "band 1 back to 80 Hz");
        w = sp::resetWrites(s, Row::Input, 8, *sp::find("b1type"));
        check(w.size() == 1 && w[0].second == 1.0f, "band 1 back to Shelf");
        w = sp::resetWrites(s, Row::Input, 8, *sp::find("compthres"));
        check(w.size() == 1 && w[0].second == -30.0f, "Frank's threshold wins over 0 dB");

        // rme.json carries the pages, and a v3 file without them keeps the factory set.
        Config c;
        c.stripPages[0].pots[3] = "fxsend";
        c.stripPages.push_back({ "Mine", "out", { "delay", "", "", "" }, { "", "", "", "" } });
        Config r;
        check(configFromJson(configToJson(c), r) && r.stripPages.size() == c.stripPages.size()
              && r.stripPages[0].pots[3] == "fxsend" && r.stripPages.back().name == "Mine"
              && r.stripPages.back().rows == "out" && r.stripPages.back().pots[1].empty(),
              "strip pages survive the round trip, empty slots included");
        Config v3;
        configFromJson("{\"version\": 3}", v3);
        check(v3.stripPages.size() == pages.size(), "a file without pages keeps the factory set");
    }


    // ── snapshot and layout names (TotalMix' state file, not OSC) ────────────
    // Lines 70-86 of Frank's last.FirefaceUFX+23802132.xml, 2026-09-22, bytes
    // as they are on disk: the file is LATIN-1, so "\xF6" is one byte there.
    {
        const std::string xml =
            "\t\t\t<val e=\"SnapshotName 0\" v=\"Mix\"/>\n"
            "\t\t\t<val e=\"LayoutName 0\" v=\"All\"/>\n"
            "\t\t\t<val e=\"SnapshotName 3\" v=\"Uncr\xF6wned\"/>\n"
            "\t\t\t<val e=\"LayoutName 3\" v=\"Tracking\"/>\n"
            "\t\t\t<val e=\"SnapshotName 6\" v=\"Drums &amp; Bass\"/>\n"
            "\t\t\t<val e=\"SnapshotName 7\" v=\"\"/>\n"
            "\t\t\t<val e=\"LastLayoutPrest\" v=\"3\"/>\n";
        Names n;
        check(parseNames(xml, n), "a state file with names parses");
        check(n.snapshot[0] == "Mix" && n.layout[3] == "Tracking", "names by slot, 0-based");
        check(n.snapshot[3] == "Uncr\xC3\xB6wned", "Latin-1 byte F6 arrives as UTF-8 C3 B6");
        check(n.snapshot[6] == "Drums & Bass", "XML entities unescaped");
        check(snapshotName(n, 7) == "Snapshot 8" && layoutName(n, 1) == "Layout 2",
              "an empty or missing name falls back to the 1-based number");
        Names u;
        parseNames("<val e=\"SnapshotName 1\" v=\"Gr\xC3\xBCn\"/>", u);
        check(u.snapshot[1] == "Gr\xC3\xBCn", "a value that already is UTF-8 is left alone");
        Names none;
        check(!parseNames("<General/>", none), "no names, no parse");
    }

    if (g_fail == 0) std::printf("test_rme_osc: all checks passed\n");
        // ⛔ Band 1 and band 3 do not share indices 2 and 3. Frank measured both at
    // the mixer on 2026-09-25 after the surface offered a high-pass and set a
    // low-pass: index 2 is "the pass filter of this band", and the bands sit at
    // opposite ends. The note this replaced said "both the same", which had read
    // band 1 and assumed band 3 mirrored it, and the same wrong order had been
    // copied into two sources and three documents.
    {
        using K = uf1eq::Band::Kind;
        auto kindAt = [](reasixty::rme::uf1::Row row, int type) {
            reasixty::rme::State st;
            auto& m = (row == reasixty::rme::uf1::Row::Output) ? st.outputs : st.inputs;
            auto& ch = m[0];
            ch.name = "ch";
            ch.seen = true;
            ch.eq.seen = true;
            ch.eq.on   = true;
            ch.eq.type1 = type;
            ch.eq.type3 = type;
            const auto mod = reasixty::rme::uf1::eqModel(st, row, 0);
            return mod;
        };
        const auto r = reasixty::rme::uf1::Row::Output;
        const auto t2 = kindAt(r, 2);
        const auto t3 = kindAt(r, 3);
        check(t2.bands.size() >= 3, "eq: three bands modelled");
        if (t2.bands.size() >= 3) {
            check(t2.bands[0].kind == K::HighPass, "band 1 type 2 is a high-pass");
            check(t2.bands[2].kind == K::LowPass,  "band 3 type 2 is a LOW-pass");
        }
        if (t3.bands.size() >= 3) {
            check(t3.bands[0].kind == K::LowPass,  "band 1 type 3 is a low-pass");
            check(t3.bands[2].kind == K::HighPass, "band 3 type 3 is a HIGH-pass");
        }
    }

return g_fail == 0 ? 0 : 1;
}
