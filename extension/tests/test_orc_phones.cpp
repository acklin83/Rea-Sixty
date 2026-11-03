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

    // ── the meters (Frank 30.10.2026) ────────────────────────────────────────
    // Per source its own peak, the headphone output as "m", L and R apart on a
    // stereo channel; half-dB steps, -60 dB and below as off; only changes.
    {
        Mixer mx;
        rme::State st = rig();
        feed(st, "/output/8/stereo", 1);
        feed(st, "/input/2/stereo", 1);
        feed(st, "/level/out/8", -12.3f);
        feed(st, "/level/out/9", -14.0f);
        feed(st, "/level/in/1", -20.2f);
        feed(st, "/level/in/2", -90.0f);   // below the floor
        feed(st, "/level/in/3", -6.0f);    // the right half of input 2
        mx.sync(st, vis(1, { 1, 2 }, { 0 }), true);
        const int a = mx.open("a"), b = mx.open("b");
        mx.message(a, R"({"t":"hello"})", 0);
        mx.message(b, R"({"t":"hello"})", 0);
        mx.message(a, R"({"t":"select","mix":"phone1"})", 0);
        mx.takeOut();
        mx.meters();
        auto out = mx.takeOut();
        const std::string lv = to(out, a);
        EXPECT(has(lv, R"("t":"levels","mix":"phone1")"));
        EXPECT(has(lv, R"("m":[-12.5,-14])"));          // stereo output, L and R
        EXPECT(has(lv, R"("in:1":[-20])"));             // mono, rounded to half a dB
        EXPECT(has(lv, R"("in:2":[-60,-6])"));          // stereo source, L off
        EXPECT(has(lv, R"("pb:0":[-60])"));             // nothing reported: off
        EXPECT(to(out, b).empty());                     // no headphone, no meters

        mx.meters();                                    // nothing changed
        out = mx.takeOut();
        EXPECT(to(out, a).empty());

        feed(st, "/level/in/1", -20.1f);                // same half-dB step
        feed(st, "/level/out/9", -3.0f);
        mx.sync(st, vis(1, { 1, 2 }, { 0 }), true);
        mx.takeOut();
        mx.meters();
        out = mx.takeOut();
        EXPECT(has(to(out, a), R"("lv":{"m":[-12.5,-3]})"));   // only the change

        mx.message(a, R"({"t":"meters","on":false})", 0);
        feed(st, "/level/in/1", -1.0f);
        mx.sync(st, vis(1, { 1, 2 }, { 0 }), true);
        mx.takeOut();
        mx.meters();
        out = mx.takeOut();
        EXPECT(to(out, a).empty());                     // switched off on the phone

        mx.message(a, R"({"t":"meters","on":true})", 0);
        mx.meters();
        out = mx.takeOut();
        EXPECT(has(to(out, a), R"("in:1":[-1])") && has(to(out, a), R"("m":)"));   // all once
    }

    // ── the studio's own names (Frank 30.10.2026) ────────────────────────────
    // Own name first, then TotalMix' name, then "Phones N"; pushed at once.
    {
        Mixer mx;
        rme::State st = rig();
        mx.sync(st, vis(1, { 1, 2 }, { 0 }), true);
        const int a = mx.open("a");
        mx.message(a, R"({"t":"hello"})", 0);
        mx.takeOut();
        mx.setNames({ "", "Bass", "", "" });
        auto out = mx.takeOut();
        const std::string cfg = to(out, a);
        EXPECT(has(cfg, R"("label":"Drums")"));    // no own name: TotalMix' name
        EXPECT(has(cfg, R"("label":"Bass")"));     // own name over "Phones 2"
        EXPECT(!has(cfg, R"("label":"Phones 2")"));
        mx.setNames({ "Voc", "", "", "" });
        out = mx.takeOut();
        EXPECT(has(to(out, a), R"("label":"Voc")") && has(to(out, a), R"("label":"Phones 2")"));
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
        EXPECT(to(out, a) == "{\"t\":\"config\",\"version\":\"1\",\"tmSeen\":false,\"link\":\"ok\",\"mixes\":[]}\n");
        mx.message(a, R"({"t":"select","mix":"phone1"})", 0);
        EXPECT(mx.takeOut().empty());
    }

    // ── the performers (docs/moreme-performers-plan.md) ─────────────────────────
    {
        namespace ph = orc::phones;
        // The file: every field there and back, quotes and umlauts whole.
        ph::Book b;
        int n = 0;
        auto token = [&] { return std::string("tok") + static_cast<char>('a' + n++) + "xyz1"; };
        EXPECT(ph::applyDesk(b, R"({"t":"add"})", token));
        EXPECT(ph::applyDesk(b, R"({"t":"add"})", token));
        EXPECT(b.performers.size() == 2 && b.performers[0].output == "phones1" && b.performers[1].output == "phones2");
        EXPECT(b.performers[0].chanEq == 0 && !b.performers[0].fxSend && !b.performers[0].hide);   // locked from the factory
        const std::string id = b.performers[0].id;
        EXPECT(ph::applyDesk(b, R"({"t":"update","id":")" + id + R"J(","performer":{"id":"evilxx","name":"Zo\"é","role":"Lead","output":"phones1","colour":"#112233","useLayout":false,"faders":[{"src":"in:1","name":"My Kick","colour":"#ff0000"},{"src":"in:1","name":"twice"},{"src":"bad","name":"x"},{"src":"pb:0","name":"","colour":"url(x)"}],"master":false,"masterEq":true,"chanEq":2,"chanDyn":9,"fxSend":true,"hide":true}})J", token));
        ph::Book back;
        EXPECT(ph::parseBook(ph::bookJson(b), back));
        const ph::Performer& p = back.performers[0];
        EXPECT(p.id == id);                       // an update never moves the link
        EXPECT(p.name == "Zo\"\xC3\xA9" && p.role == "Lead" && p.colour == "#112233");
        EXPECT(!p.useLayout && p.faders.size() == 2 && p.faders[0].name == "My Kick" && p.faders[1].src == "pb:0");
        EXPECT(p.faders[1].colour.empty());       // only a colour gets into a style
        EXPECT(!p.master && p.masterEq && p.chanEq == 2 && p.chanDyn == 2 && p.fxSend && p.hide);
        // Snapshots: save, change, load.
        EXPECT(!ph::applyDesk(b, R"({"t":"snapSave"})", token));   // no name yet
        EXPECT(ph::applyDesk(b, R"({"t":"snapSave","name":"Band"})", token) && b.current == "Band");
        EXPECT(ph::applyDesk(b, R"({"t":"remove","id":")" + id + R"("})", token) && b.performers.size() == 1);
        EXPECT(ph::applyDesk(b, R"({"t":"snapLoad","name":"Band"})", token) && b.performers.size() == 2 && b.performers[0].id == id);
        EXPECT(ph::applyDesk(b, R"({"t":"snapNew"})", token) && b.performers.empty() && b.current.empty());
        EXPECT(ph::applyDesk(b, R"({"t":"snapDelete","name":"Band"})", token) && b.snapshots.empty());
        EXPECT(!ph::applyDesk(b, "not json", token) && !ph::applyDesk(b, R"({"t":"remove","id":"nobody"})", token));
        EXPECT(ph::validToken("abcd1234") && !ph::validToken("abc") && !ph::validToken("../etc") && !ph::validToken("ABCD"));

        // ⛔ Studio Control: this Mac, directly, under the loopback's name.
        EXPECT(ph::deskAllowed("127.0.0.1", "", "", "127.0.0.1:8088", "http://127.0.0.1:8088"));
        EXPECT(ph::deskAllowed("127.0.0.1", "", "", "localhost:8088", ""));
        EXPECT(!ph::deskAllowed("192.168.1.5", "", "", "192.168.1.2:8088", ""));                // a phone on the network
        EXPECT(!ph::deskAllowed("8.8.8.8", "", "", "127.0.0.1:8088", ""));                      // from outside
        EXPECT(!ph::deskAllowed("127.0.0.1", "8.8.8.8", "", "127.0.0.1", ""));                  // a proxy on this Mac
        EXPECT(!ph::deskAllowed("127.0.0.1", "", "8.8.8.8", "127.0.0.1", ""));
        EXPECT(!ph::deskAllowed("127.0.0.1", "", "", "phones.example.com", ""));                // a proxy without headers
        EXPECT(!ph::deskAllowed("127.0.0.1", "", "", "127.0.0.1:8088", "http://evil.example.com")); // another page

        // The mixer: a link shows its performer, and only what they were given.
        rme::State st = rig();
        for (const char* o : { "/output/8/color", "/output/10/color" }) feed(st, o, 1);
        feedName(st, "/output/10/name", "Bass");
        feed(st, "/input/1/eq/enable", 1);
        feed(st, "/input/1/eq/band1gain", 2);
        feed(st, "/input/1/eq/band1freq", 100);
        feed(st, "/input/1/eq/band1q", 1);
        feed(st, "/input/1/eq/band1type", 0);
        feed(st, "/input/1/fxsend", -30);
        feed(st, "/output/8/eq/enable", 1);
        Mixer mx("1234");
        mx.sync(st, vis(1, {1, 2}, {0}), true);
        ph::Performer drums;
        drums.id = "drumsaaa"; drums.name = "Sam"; drums.role = "Drums"; drums.output = "phones1";
        drums.useLayout = false;
        drums.faders = { { "in:2", "Sn", "#00ff00" }, { "in:1", "", "" }, { "in:7", "hidden", "" } };
        ph::Performer bass;
        bass.id = "bassaaaa"; bass.name = "Alex"; bass.output = "phones2"; bass.master = false;
        mx.setPerformers({ drums, bass });
        mx.takeOut();

        // A phone on the link: its performer, its faders in its order.
        const int a = mx.open("10.0.0.5");
        mx.message(a, R"({"t":"hello","pin":"1234","performer":"drumsaaa"})", 0);
        auto out = mx.takeOut();
        std::string s = to(out, a);
        EXPECT(has(s, R"("id":"perf:drumsaaa")") && has(s, R"("role":"Drums")") && !has(s, "bassaaaa") && has(s, R"("link":"ok")"));
        mx.message(a, R"({"t":"select","mix":"perf:bassaaaa"})", 0);    // someone else's: no
        out = mx.takeOut();
        EXPECT(!has(to(out, a), "snapshot"));
        mx.message(a, R"({"t":"select","mix":"perf:drumsaaa"})", 0);
        out = mx.takeOut();
        s = to(out, a);
        EXPECT(has(s, "\"t\":\"snapshot\"") && has(s, R"("label":"Sn")") && has(s, R"("color":"#00ff00")") && has(s, R"("label":"Kick")"));
        EXPECT(s.find(R"("id":"in:2")") < s.find(R"("id":"in:1")"));    // the order set on the desk
        EXPECT(!has(s, "in:7") && !has(s, R"("id":"pb:0")"));           // hidden, and not on the list
        mx.takeOsc();

        // ⛔ What isn't theirs does nothing, whatever the page sends.
        mx.message(a, R"({"t":"set","mix":"perf:drumsaaa","src":"pb:0","ctrl":"fader","val":0})", 0);
        EXPECT(mx.takeOsc().empty());
        mx.message(a, R"({"t":"set","mix":"perf:bassaaaa","src":"in:1","ctrl":"fader","val":0})", 0);
        EXPECT(mx.takeOsc().empty());
        mx.message(a, R"({"t":"strip","mix":"perf:drumsaaa","target":"in:1","param":"b1gain","val":3})", 0);
        EXPECT(mx.takeOsc().empty());                                   // EQ locked from the factory
        mx.message(a, R"({"t":"strip","mix":"perf:drumsaaa","target":"in:1","param":"fxsend","val":-3})", 0);
        EXPECT(mx.takeOsc().empty());                                   // so is the FX send
        // Their own fader: yes, on their own output.
        mx.message(a, R"({"t":"set","mix":"perf:drumsaaa","src":"in:1","ctrl":"fader","val":-3})", 0);
        auto osc = mx.takeOsc();
        EXPECT(osc.size() == 1 && osc[0].address == "/mix/in/1/8/fader");
        mx.takeOut();

        // Simple opens the gains and not the rest; Advanced the rest.
        drums.chanEq = 1;
        drums.fxSend = true;
        mx.setPerformers({ drums, bass });
        out = mx.takeOut();
        EXPECT(has(to(out, a), R"("eq":1)"));
        mx.message(a, R"({"t":"strip","mix":"perf:drumsaaa","target":"in:1","param":"b1gain","val":99})", 0);
        osc = mx.takeOsc();
        EXPECT(osc.size() == 1 && osc[0].address == "/input/1/eq/band1gain" && osc[0].value == 20.0f);   // clamped
        mx.message(a, R"({"t":"strip","mix":"perf:drumsaaa","target":"in:1","param":"b1freq","val":500})", 0);
        EXPECT(mx.takeOsc().empty());
        mx.message(a, R"({"t":"strip","mix":"perf:drumsaaa","target":"in:1","param":"fxsend","val":-3})", 0);
        osc = mx.takeOsc();
        EXPECT(osc.size() == 1 && osc[0].address == "/input/1/fxsend");
        mx.message(a, R"({"t":"strip","mix":"perf:drumsaaa","target":"in:2","param":"b1gain","val":3})", 0);
        EXPECT(mx.takeOsc().empty());                                   // TotalMix reported no EQ on it
        mx.message(a, R"({"t":"strip","mix":"perf:drumsaaa","target":"in:1","param":"48v","val":1})", 0);
        EXPECT(mx.takeOsc().empty());                                   // never on the list
        // The page with the curve, from the graph's own maths.
        mx.takeOut();
        mx.message(a, R"({"t":"stripOpen","mix":"perf:drumsaaa","target":"in:1"})", 0);
        out = mx.takeOut();
        s = to(out, a);
        EXPECT(has(s, "\"t\":\"strip\"") && has(s, "\"curve\":[") && has(s, "\"b1gain\":{\"v\":"));
        EXPECT(!has(s, "\"b1freq\""));                                  // Simple doesn't get it
        mx.takeOsc();
        // Advanced: the shapes, by the catalogue's names. Band 3's filters are the
        // other way round from band 1's (measured at the mixer 25.09.2026).
        feed(st, "/input/1/eq/band3type", 2);
        mx.sync(st, vis(1, {1, 2}, {0}), true);
        drums.chanEq = 2;
        mx.setPerformers({ drums, bass });
        mx.takeOut();
        mx.message(a, R"({"t":"stripOpen","mix":"perf:drumsaaa","target":"in:1"})", 0);
        out = mx.takeOut();
        s = to(out, a);
        EXPECT(has(s, R"("names":["Bell","Shelf","HiPass","LoPass"])"));
        EXPECT(has(s, R"("names":["Bell","Shelf","LoPass","HiPass"])"));
        mx.takeOsc();
        mx.message(a, R"({"t":"strip","mix":"perf:drumsaaa","target":"in:1","param":"b1type","val":1})", 0);
        osc = mx.takeOsc();
        EXPECT(osc.size() == 1 && osc[0].address == "/input/1/eq/band1type" && osc[0].value == 1.0f);
        mx.message(a, R"({"t":"strip","mix":"perf:drumsaaa","target":"in:1","param":"b1type","val":9})", 0);
        osc = mx.takeOsc();
        EXPECT(osc.size() == 1 && osc[0].value == 3.0f);               // the last entry, no further
        drums.chanEq = 1;
        mx.setPerformers({ drums, bass });
        mx.takeOut();
        mx.message(a, R"({"t":"strip","mix":"perf:drumsaaa","target":"in:1","param":"b1type","val":1})", 0);
        EXPECT(mx.takeOsc().empty());                                  // Simple: not the shape
        mx.takeOut();

        // The own output: no fader unless it was given.
        const int c = mx.open("10.0.0.6");
        mx.message(c, R"({"t":"hello","pin":"1234","performer":"bassaaaa"})", 0);
        mx.message(c, R"({"t":"select","mix":"perf:bassaaaa"})", 0);
        out = mx.takeOut();
        EXPECT(has(to(out, c), R"("shown":0)"));
        mx.takeOsc();
        mx.message(c, R"({"t":"master","mix":"perf:bassaaaa","ctrl":"vol","val":0})", 0);
        EXPECT(mx.takeOsc().empty());

        // A link that's gone says so, and a list when there's no link.
        const int e = mx.open("10.0.0.7");
        mx.message(e, R"({"t":"hello","pin":"1234","performer":"nobodyxx"})", 0);
        out = mx.takeOut();
        EXPECT(has(to(out, e), R"("link":"unknown")"));
        const int f = mx.open("10.0.0.8");
        mx.message(f, R"({"t":"hello","pin":"1234"})", 0);
        out = mx.takeOut();
        s = to(out, f);
        EXPECT(has(s, "perf:drumsaaa") && has(s, "perf:bassaaaa") && !has(s, "phone1"));
        // Taking the performer away ends it at once.
        mx.setPerformers({ bass });
        out = mx.takeOut();
        EXPECT(has(to(out, a), R"("link":"unknown")"));
        // No performers: Phones 1-4 as ever.
        mx.setPerformers({});
        out = mx.takeOut();
        EXPECT(has(to(out, f), "phone1"));
    }

    if (g_fail) { std::printf("test_orc_phones: %d failure(s)\n", g_fail); return 1; }
    std::printf("test_orc_phones: ok\n");
    return 0;
}
