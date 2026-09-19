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
#include "RmeOsc.h"
#include "RmeState.h"

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

    // ── colour index 0 means hidden, and it is an INDEX, not RGB ────────────
    feed("/input/0/color", 0.0f);
    feed("/input/1/color", 5.0f);
    check(st.inputs[0].colour == 0, "colour 0 is the hidden marker");
    check(st.inputs[1].colour == 5, "colour is a palette index");

    // ── levels only arrive with Send Peak Level on; empty is the normal state
    check(st.levelOut.empty(), "no levels until the mixer is told to send them");
    feed("/level/out/8", -6.5f);
    check(st.levelOut.count(8) == 1 && st.levelOut[8] < -6.4, "a level lands on its bus");

    if (g_fail == 0) std::printf("test_rme_osc: all checks passed\n");
    return g_fail == 0 ? 0 : 1;
}
