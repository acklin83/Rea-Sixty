// orc/Phones: ORC MoreMe's mixer logic (docs/orc-phones-plan.md, stage 1).
// The rules are stoerme's (~/Documents/dev/stoerme/bridge.js); the PIN part is
// its test/pin.test.js, case for case.

#include "Phones.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

namespace rme = reasixty::rme;
using namespace orc::phones;

static void feed(rme::State& st, const std::string& a, float v)
{
    rme::Message m;
    m.address = a;
    m.args.push_back(rme::Arg::fromFloat(v));
    rme::ingest(st, m);
}
static void feedName(rme::State& st, const std::string& a, const std::string& s)
{
    rme::Message m;
    m.address = a;
    m.args.push_back(rme::Arg::fromString(s));
    rme::ingest(st, m);
}

// What Manager::send does with each value: fold it into the remote's state.
static void sendAll(rme::State& st, const std::vector<Osc>& osc)
{
    for (const auto& o : osc) rme::ingest(st, rme::localEcho(o.address, o.value));
}

static bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

// Everything sent to one client since the last call, joined.
static std::string to(std::vector<std::pair<int, std::string>>& out, int id)
{
    std::string s;
    for (const auto& [who, json] : out)
        if (who == id) s += json + "\n";
    return s;
}

// The rig of these tests: Phones 1 on output 8 ("Drums"), Phones 2 on output 10,
// inputs 1 and 2 and playback 0 visible on the remote.
static rme::State rig()
{
    rme::State st;
    feed(st, "/controlroom/phones1", 8);
    feed(st, "/controlroom/phones2", 10);
    feedName(st, "/output/8/name", "Drums");
    feed(st, "/output/8/volume", -10.0f);
    feedName(st, "/input/1/name", "Kick");
    feed(st, "/input/1/color", 4);
    feedName(st, "/input/2/name", "Snare");
    feed(st, "/mix/in/1/8/fader", -6.0f);
    feed(st, "/mix/in/2/8/fader", 0.0f);
    feed(st, "/mix/pb/0/8/fader", -20.0f);
    feed(st, "/mix/in/1/10/fader", -12.0f);
    return st;
}
static rme::Manager::Visible vis(std::uint64_t round, std::vector<int> in, std::vector<int> pb)
{
    rme::Manager::Visible v;
    v.round = round;
    v.in = std::move(in);
    v.pb = std::move(pb);
    return v;
}

int main()
{
    // ── pin.test.js ──────────────────────────────────────────────────────────
    {   // no PIN set: the mixer stays open
        const PinChecker c("");
        EXPECT(!c.required());
        EXPECT(c.ok(""));
    }
    {   // the right PIN passes, anything else does not
        const PinChecker c("482913");
        EXPECT(c.required());
        EXPECT(c.ok("482913"));
        EXPECT(!c.ok("482914"));
        EXPECT(!c.ok(""));
        EXPECT(!c.ok("4829130"));
    }
    {   // five wrong PINs lock the client for ten minutes
        FailRecord rec;
        const FailRecord* p = nullptr;
        const long long t0 = 1000000;
        for (int i = 1; i < kMaxFails; ++i) {
            rec = afterFail(p, t0 + i); p = &rec;
            EXPECT(lockLeft(p, t0 + i) == 0);
        }
        rec = afterFail(p, t0 + 10);
        EXPECT(lockLeft(&rec, t0 + 10) == kLockMs);
        EXPECT(lockLeft(&rec, t0 + 10 + kLockMs) == 0);   // the lock runs out
    }
    {   // wrong PINs spread wider than the window do not add up
        FailRecord rec;
        const FailRecord* p = nullptr;
        long long t = 0;
        for (int i = 0; i < 20; ++i) {
            t += kWindowMs + 1;
            rec = afterFail(p, t); p = &rec;
            EXPECT(lockLeft(p, t) == 0);
        }
    }
    {   // X-Real-IP counts only from a proxy inside a private network
        EXPECT(clientKey("::ffff:192.168.177.3", "85.1.2.3") == "85.1.2.3");   // NAS proxy
        EXPECT(clientKey("85.9.9.9", "1.1.1.1") == "85.9.9.9");               // forged from outside
        EXPECT(clientKey("192.168.177.50", "") == "192.168.177.50");          // direct LAN client
        // X-Forwarded-For when there is no X-Real-IP: its last entry, the one
        // the proxy wrote; only from a private peer (Frank 30.10.2026).
        EXPECT(clientKey("127.0.0.1", "", "6.6.6.6, 7.7.7.7, 85.1.2.3") == "85.1.2.3");     // tunnel on the Mac
        EXPECT(clientKey("192.168.177.3", "", "85.1.2.3") == "85.1.2.3");
        EXPECT(clientKey("85.9.9.9", "", "1.1.1.1") == "85.9.9.9");                 // forged from outside
        EXPECT(clientKey("192.168.177.3", "85.1.2.3", "9.9.9.9") == "85.1.2.3");   // X-Real-IP wins
        EXPECT(isPrivate("100.109.232.65"));                                  // tailnet
        EXPECT(!isPrivate("172.32.0.1"));
        EXPECT(isPrivate("172.16.0.1") && isPrivate("172.31.255.1"));
        EXPECT(!isPrivate("100.128.0.1") && isPrivate("100.64.0.1"));
        EXPECT(isPrivate("::1") && isPrivate("127.0.0.1") && isPrivate("10.1.2.3"));
        EXPECT(!isPrivate("1.10.0.1") && !isPrivate("192.169.0.1"));
    }
    EXPECT(colourHex(4) == "#e53935");
    EXPECT(colourHex(1).empty() && colourHex(0).empty() && colourHex(-1).empty());

    // ── the PIN gate ─────────────────────────────────────────────────────────
    {
        Mixer mx("1234");
        const rme::State st = rig();
        mx.sync(st, vis(1, { 1, 2 }, { 0 }), true);
        const int a = mx.open("192.168.1.20");
        mx.message(a, R"({"t":"hello","pin":""})", 0);
        auto out = mx.takeOut();
        EXPECT(to(out, a) == "{\"t\":\"pin\",\"wrong\":0,\"wait\":0}\n");
        // Outside, nothing else is answered.
        mx.message(a, R"({"t":"select","mix":"phone1"})", 0);
        EXPECT(mx.takeOut().empty());
        EXPECT(mx.clients() == 0);

        mx.message(a, R"({"t":"hello","pin":"9999"})", 0);
        out = mx.takeOut();
        EXPECT(has(to(out, a), "\"wrong\":1"));
        mx.message(a, R"({"t":"hello","pin":"1234"})", 0);
        out = mx.takeOut();
        EXPECT(has(to(out, a), "\"t\":\"config\""));
        EXPECT(has(to(out, a), "\"tmSeen\":true"));
        EXPECT(mx.clients() == 1);

        // Five wrong ones from another address lock it; then even the right
        // PIN waits, and says how long.
        const int b = mx.open("192.168.1.21");
        for (int i = 0; i < kMaxFails; ++i) mx.message(b, R"({"t":"hello","pin":"0000"})", 1000 + i);
        out = mx.takeOut();
        EXPECT(has(to(out, b), "\"wrong\":0,\"wait\":600"));
        mx.message(b, R"({"t":"hello","pin":"1234"})", 2000);
        out = mx.takeOut();
        EXPECT(has(to(out, b), "\"t\":\"pin\"") && !has(to(out, b), "config"));
        mx.message(b, R"({"t":"hello","pin":"1234"})", 2000 + kLockMs);
        out = mx.takeOut();
        EXPECT(has(to(out, b), "\"t\":\"config\""));

        // A new PIN sends everybody back to the PIN page.
        mx.setPin("5678");
        out = mx.takeOut();
        EXPECT(has(to(out, a), "\"t\":\"pin\"") && has(to(out, b), "\"t\":\"pin\""));
        EXPECT(mx.clients() == 0);
    }

    // ── headphones, the lock, the takeover ───────────────────────────────────
    {
        Mixer mx;   // no PIN: stoerme's open mixer, so the rest needs no hello dance
        rme::State st = rig();
        mx.sync(st, vis(1, { 1, 2 }, { 0 }), true);
        const int a = mx.open("a"), b = mx.open("b");
        mx.message(a, R"({"t":"hello"})", 0);
        auto out = mx.takeOut();
        const std::string cfg = to(out, a);
        EXPECT(has(cfg, R"({"id":"phone1","phone":1,"bus":8,"label":"Drums","color":"#0099FF","locked":false})"));
        EXPECT(has(cfg, R"("label":"Phones 2")"));   // output 10 has no name
        EXPECT(!has(cfg, "phone3"));                 // not assigned

        mx.message(a, R"({"t":"select","mix":"phone1"})", 0);
        out = mx.takeOut();
        const std::string snap = to(out, a);
        EXPECT(has(snap, "\"t\":\"snapshot\",\"mix\":\"phone1\""));
        EXPECT(has(snap, R"({"id":"in:1","kind":"in","n":1,"label":"Kick","max":6,"color":"#e53935"})"));
        EXPECT(has(snap, R"({"id":"in:2","kind":"in","n":2,"label":"Snare","max":6,"color":"#3fcf6a"})"));
        EXPECT(has(snap, R"({"id":"pb:0","kind":"pb","n":0,"label":"PB 0","max":0,"color":"#f0c000"})"));
        EXPECT(has(snap, R"("in:1":{"db":-6,"pan":0,"mute":0,"solo":0})"));
        EXPECT(has(snap, R"("master":{"db":-10,"mute":0})"));
        EXPECT(mx.takeRefresh());
        EXPECT(!mx.takeRefresh());
        // b now sees phone1 as taken.
        EXPECT(has(to(out, b), R"("id":"phone1","phone":1,"bus":8,"label":"Drums","color":"#0099FF","locked":true)"));

        mx.message(b, R"({"t":"select","mix":"phone1"})", 0);
        out = mx.takeOut();
        EXPECT(to(out, b) == "{\"t\":\"denied\",\"mix\":\"phone1\"}\n");
        EXPECT(to(out, a).empty());

        mx.message(b, R"({"t":"select","mix":"phone1","force":true})", 0);
        out = mx.takeOut();
        EXPECT(has(to(out, a), "{\"t\":\"released\",\"mix\":\"phone1\"}"));
        EXPECT(has(to(out, b), "\"t\":\"snapshot\""));
        EXPECT(mx.held() == 1);

        // The holder leaves: free at once.
        mx.close(b);
        out = mx.takeOut();
        EXPECT(has(to(out, a), R"("id":"phone1","phone":1,"bus":8,"label":"Drums","color":"#0099FF","locked":false)"));
        EXPECT(mx.held() == 0);
    }

    // ── values both ways, quiet rounds, the mute ─────────────────────────────
    {
        Mixer mx;
        rme::State st = rig();
        const auto v1 = vis(1, { 1, 2 }, { 0 });
        mx.sync(st, v1, true);
        const int a = mx.open("a"), c = mx.open("c");
        mx.message(a, R"({"t":"select","mix":"phone1"})", 0);
        mx.message(c, R"({"t":"select","mix":"phone2"})", 0);
        mx.takeOut();
        mx.takeOsc();

        // The page moves a fader: TotalMix gets the node, everybody on that
        // headphone the value, nobody on another one.
        mx.message(a, R"({"t":"set","mix":"phone1","src":"in:2","ctrl":"fader","val":-3.5,"end":false})", 0);
        auto osc = mx.takeOsc();
        EXPECT(osc.size() == 1 && osc[0].address == "/mix/in/2/8/fader" && osc[0].value == -3.5f);
        auto out = mx.takeOut();
        EXPECT(to(out, a) == "{\"t\":\"update\",\"mix\":\"phone1\",\"src\":\"in:2\",\"db\":-3.5}\n");
        EXPECT(to(out, c).empty());
        // Clamped to the input's +6.
        mx.message(a, R"({"t":"set","mix":"phone1","src":"in:2","ctrl":"fader","val":40})", 0);
        osc = mx.takeOsc();
        EXPECT(osc.size() == 1 && osc[0].value == 6.0f);
        mx.takeOut();

        // The remote's state has the same value (Manager::send folds it in): a
        // round that repeats it says nothing.
        feed(st, "/mix/in/2/8/fader", 6.0f);
        mx.sync(st, v1, true);
        EXPECT(mx.takeOut().empty());

        // TotalMix moves one: that, and only that.
        feed(st, "/mix/in/1/8/fader", -9.0f);
        mx.sync(st, v1, true);
        out = mx.takeOut();
        EXPECT(to(out, a) == "{\"t\":\"update\",\"mix\":\"phone1\",\"src\":\"in:1\",\"db\":-9}\n");
        EXPECT(to(out, c).empty());

        // Mute: the fader rides to -inf, the phones keep the level.
        mx.message(a, R"({"t":"mute","mix":"phone1","src":"in:1"})", 0);
        osc = mx.takeOsc();
        EXPECT(osc.size() == 1 && osc[0].address == "/mix/in/1/8/fader" && osc[0].value == -300.0f);
        out = mx.takeOut();
        EXPECT(to(out, a) == "{\"t\":\"update\",\"mix\":\"phone1\",\"src\":\"in:1\",\"mute\":1}\n");
        feed(st, "/mix/in/1/8/fader", -300.0f);   // what the remote now holds
        mx.sync(st, v1, true);
        EXPECT(mx.takeOut().empty());
        // The remote reconnects (its state starts empty) and TotalMix reports
        // the -inf again: still muted, still the remembered level.
        mx.sync(rme::State{}, v1, true);
        mx.takeOut();
        mx.sync(st, v1, true);
        {
            auto back = mx.takeOut();
            const std::string s1 = to(back, a);
            EXPECT(!has(s1, "\"src\":\"in:1\""));
            EXPECT(has(s1, R"("in:1":{"db":-9,"pan":0,"mute":1,"solo":0})"));
        }
        // A fresh snapshot shows the remembered level, muted.
        mx.message(a, R"({"t":"select","mix":"phone1"})", 0);
        out = mx.takeOut();
        EXPECT(has(to(out, a), R"("in:1":{"db":-9,"pan":0,"mute":1,"solo":0})"));
        // Unmute: back to it.
        mx.message(a, R"({"t":"mute","mix":"phone1","src":"in:1"})", 0);
        osc = mx.takeOsc();
        EXPECT(osc.size() == 1 && osc[0].value == -9.0f);
        out = mx.takeOut();
        EXPECT(to(out, a) == "{\"t\":\"update\",\"mix\":\"phone1\",\"src\":\"in:1\",\"mute\":0}\n");
        feed(st, "/mix/in/1/8/fader", -9.0f);
        mx.sync(st, v1, true);
        EXPECT(mx.takeOut().empty());

        // Muted, and then moved in TotalMix: unmuted, at the new level.
        mx.message(a, R"({"t":"mute","mix":"phone1","src":"in:1"})", 0);
        mx.takeOsc(); mx.takeOut();
        feed(st, "/mix/in/1/8/fader", -300.0f);
        mx.sync(st, v1, true);
        EXPECT(mx.takeOut().empty());
        feed(st, "/mix/in/1/8/fader", -2.0f);
        mx.sync(st, v1, true);
        out = mx.takeOut();
        EXPECT(to(out, a) == "{\"t\":\"update\",\"mix\":\"phone1\",\"src\":\"in:1\",\"db\":-2,\"mute\":0}\n");

        // Muted, and then dragged on the page: unmuted, and the update says so.
        mx.message(a, R"({"t":"mute","mix":"phone1","src":"in:1"})", 0);
        mx.takeOsc(); mx.takeOut();
        mx.message(a, R"({"t":"set","mix":"phone1","src":"in:1","ctrl":"fader","val":-1})", 0);
        out = mx.takeOut();
        EXPECT(to(out, a) == "{\"t\":\"update\",\"mix\":\"phone1\",\"src\":\"in:1\",\"db\":-1,\"mute\":0}\n");
        sendAll(st, mx.takeOsc());

        // Pan, solo, master.
        mx.message(a, R"({"t":"set","mix":"phone1","src":"pb:0","ctrl":"pan","val":-2})", 0);
        osc = mx.takeOsc();
        sendAll(st, osc);
        EXPECT(osc.size() == 1 && osc[0].address == "/mix/pb/0/8/balpan" && osc[0].value == -1.0f);
        mx.message(a, R"({"t":"solo","mix":"phone1","src":"pb:0"})", 0);
        osc = mx.takeOsc();
        sendAll(st, osc);
        EXPECT(osc.size() == 1 && osc[0].address == "/mix/pb/0/8/solo" && osc[0].value == 1.0f);
        mx.message(a, R"({"t":"master","mix":"phone1","ctrl":"vol","val":3})", 0);
        osc = mx.takeOsc();
        sendAll(st, osc);
        EXPECT(osc.size() == 1 && osc[0].address == "/output/8/volume" && osc[0].value == 0.0f);
        mx.message(a, R"({"t":"master","mix":"phone1","ctrl":"mute"})", 0);
        osc = mx.takeOsc();
        sendAll(st, osc);
        EXPECT(osc.size() == 1 && osc[0].address == "/output/8/mute" && osc[0].value == 1.0f);
        out = mx.takeOut();
        EXPECT(has(to(out, a), "{\"t\":\"update\",\"mix\":\"phone1\",\"src\":\"pb:0\",\"pan\":-1}"));
        EXPECT(has(to(out, a), "{\"t\":\"update\",\"mix\":\"phone1\",\"src\":\"pb:0\",\"solo\":1}"));
        EXPECT(has(to(out, a), "{\"t\":\"master\",\"mix\":\"phone1\",\"db\":0}"));
        EXPECT(has(to(out, a), "{\"t\":\"master\",\"mix\":\"phone1\",\"mute\":1}"));
        // A source that is not "in:" or "pb:" is not an address.
        mx.message(a, R"({"t":"set","mix":"phone1","src":"out:1","ctrl":"fader","val":0})", 0);
        mx.message(a, R"({"t":"set","mix":"phone7","src":"in:1","ctrl":"fader","val":0})", 0);
        mx.message(a, "not json", 0);
        EXPECT(mx.takeOsc().empty());

        // TotalMix moves the master of Phones 2: only c hears it.
        feed(st, "/output/10/volume", -4.0f);
        mx.sync(st, v1, true);
        out = mx.takeOut();
        EXPECT(to(out, c) == "{\"t\":\"master\",\"mix\":\"phone2\",\"db\":-4}\n");
        EXPECT(to(out, a).empty());
    }

    // ── visibility per round ─────────────────────────────────────────────────
    {
        Mixer mx;
        rme::State st = rig();
        mx.sync(st, vis(1, { 1, 2 }, { 0 }), true);
        const int a = mx.open("a");
        mx.message(a, R"({"t":"select","mix":"phone1"})", 0);
        mx.takeOut();
        // The same round again, and a new round with the same set: quiet.
        mx.sync(st, vis(1, { 1, 2 }, { 0 }), true);
        mx.sync(st, vis(2, { 1, 2 }, { 0 }), true);
        EXPECT(mx.takeOut().empty());
        // Input 2 hidden in TotalMix: the next round drops it, and the phone
        // gets a new snapshot without it.
        mx.sync(st, vis(3, { 1 }, { 0 }), true);
        auto out = mx.takeOut();
        const std::string s = to(out, a);
        EXPECT(has(s, "\"t\":\"snapshot\""));
        EXPECT(has(s, "\"in:1\"") && !has(s, "\"in:2\""));
        // A colour changed in TotalMix: new snapshot too.
        feed(st, "/input/1/color", 6);
        mx.sync(st, vis(3, { 1 }, { 0 }), true);
        out = mx.takeOut();
        EXPECT(has(to(out, a), "\"color\":\"#43a047\""));
        // A name that is not UTF-8 (Latin-1 "ö") reaches the page as UTF-8.
        feedName(st, "/input/1/name", "Kl\xF6tz");
        mx.sync(st, vis(3, { 1 }, { 0 }), true);
        out = mx.takeOut();
        EXPECT(has(to(out, a), "\"label\":\"Kl\xC3\xB6tz\""));
    }

    // ── TotalMix not there yet ───────────────────────────────────────────────
    {
        Mixer mx;
        mx.sync(rme::State{}, rme::Manager::Visible{}, false);
        const int a = mx.open("a");
        mx.message(a, R"({"t":"hello"})", 0);
        auto out = mx.takeOut();
        EXPECT(to(out, a) == "{\"t\":\"config\",\"version\":\"1\",\"tmSeen\":false,\"mixes\":[]}\n");
        mx.message(a, R"({"t":"select","mix":"phone1"})", 0);
        EXPECT(mx.takeOut().empty());
    }

    if (g_fail) { std::printf("test_orc_phones: %d failure(s)\n", g_fail); return 1; }
    std::printf("test_orc_phones: ok\n");
    return 0;
}
