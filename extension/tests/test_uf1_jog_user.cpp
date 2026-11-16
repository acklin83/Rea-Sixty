// The UF1's own jog modes (docs/uf1-user-jog-modes-plan.md): the turn
// arithmetic (JogUserTurn.h) against REAPER's own relmode-1 encoder, and the
// bindings engine for real: the ids, the nav cross of the four new modes, the
// v58 upgrade, the slot settings through the file.

#include "Bindings.h"
#include "BindingsHost.h"
#include "JogUserTurn.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace bnd = uf8::bindings;
using bnd::ButtonId;

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

static std::string g_dir;
static const int P = static_cast<int>(bnd::Modifier::Plain);

// REAPER's encoder, as reaper_csurf/csurf_osc.cpp has it (encode_relmode1_extended),
// so relmode1 is checked against the thing it imitates, not against itself.
static void reaperRel1(double d, int* val, int* valhw)
{
    int val7bit = 0, z = 0;
    if (d < 0.0) {
        const double w = std::floor(d);
        if (w >= -64.0) { val7bit = (int)w; z = (int)((d - w) * 256.0 + 0.5); }
        else val7bit = -64;
    } else if (d > 0.0) {
        const double w = std::ceil(d);
        if (w <= 63.0) { val7bit = (int)w; z = (int)((w - d) * 256.0 + 0.5); }
        else val7bit = 63;
    }
    *val = val7bit & 0x7f;
    *valhw = -1 - (z < 255 ? z : 255);
}

static void setVersion(int v)
{
    const std::string path = g_dir + "/bindings.json";
    std::ifstream in(path);
    std::stringstream ss; ss << in.rdbuf();
    std::string js = ss.str();
    const size_t k = js.find("\"version\"");
    EXPECT(k != std::string::npos);
    if (k == std::string::npos) return;
    size_t a = js.find(':', k) + 1;
    while (a < js.size() && js[a] == ' ') ++a;
    size_t b = a;
    while (b < js.size() && js[b] >= '0' && js[b] <= '9') ++b;
    js.replace(a, b - a, std::to_string(v));
    std::ofstream(path) << js;
}

static void testArithmetic()
{
    // Divider 1: every detent fires, with its sign.
    double acc = 0.0;
    EXPECT(jogusr::divide(acc, 1, 1.0) == 1);
    EXPECT(jogusr::divide(acc, -3, 1.0) == -3);
    // Divider 3: five detents = one firing, two left over; one more = a second.
    acc = 0.0;
    EXPECT(jogusr::divide(acc, 5, 3.0) == 1);
    EXPECT(jogusr::divide(acc, 1, 3.0) == 1);
    EXPECT(acc == 0.0);
    // Three single detents at 1/3 each add up to exactly one.
    acc = 0.0;
    EXPECT(jogusr::divide(acc, 1, 3.0) == 0);
    EXPECT(jogusr::divide(acc, 1, 3.0) == 0);
    EXPECT(jogusr::divide(acc, 1, 3.0) == 1);
    // A reversal drops the remainder: two right then one left fires nothing.
    acc = 0.0;
    EXPECT(jogusr::divide(acc, 2, 3.0) == 0);
    EXPECT(jogusr::divide(acc, -1, 3.0) == 0);
    EXPECT(jogusr::divide(acc, -2, 3.0) == -1);
    // A divider below 1 counts as 1.
    acc = 0.0;
    EXPECT(jogusr::divide(acc, 2, 0.0) == 2);

    // relmode 1 for whole steps is REAPER's encoding.
    for (int st : { 1, -1, 7, -7, 63, -64 }) {
        int v = 0, hw = 0;
        reaperRel1(static_cast<double>(st), &v, &hw);
        const jogusr::Rel1 r = jogusr::relmode1(st);
        EXPECT(r.val == v);
        EXPECT(r.valhw == hw);
    }
    EXPECT(jogusr::relmode1(-1).val == 127);
    EXPECT(jogusr::relmode1(1).val == 1);
    // Out of one call's range it clamps, as REAPER's does.
    EXPECT(jogusr::relmode1(-70).val == (-64 & 0x7F));
    EXPECT(jogusr::relmode1(100).val == 63);
    // splitRel carries the rest in more calls of the same sign.
    int parts[8];
    EXPECT(jogusr::splitRel(-70, parts, 8) == 2);
    EXPECT(parts[0] == -64 && parts[1] == -6);
    EXPECT(jogusr::splitRel(5, parts, 8) == 1 && parts[0] == 5);
    EXPECT(jogusr::splitRel(0, parts, 8) == 0);
}

static void testIds()
{
    // The nav cross of the four new modes: own ids, named, and back.
    static const ButtonId kNav[] = { ButtonId::Uf1NavUp, ButtonId::Uf1NavLeft,
                                     ButtonId::Uf1NavCentre, ButtonId::Uf1NavRight,
                                     ButtonId::Uf1NavDown };
    for (const ButtonId base : kNav)
        for (int m = 0; m < bnd::kUf1JogModeCountForNav; ++m) {
            const ButtonId id = bnd::perModeNavId(base, m);
            EXPECT(id != base);
            ButtonId b = ButtonId::None; int mm = -1;
            EXPECT(bnd::splitPerModeNavId(id, &b, &mm));
            EXPECT(b == base && mm == m);
            EXPECT(*bnd::toName(id) != 0);
            EXPECT(bnd::fromName(bnd::toName(id)) == id);
            EXPECT(id < ButtonId::Uf1Jog);   // a UF1 control
        }
    EXPECT(bnd::kUf1JogModeCountForNav
           == bnd::kUf1JogFirstUserMode + bnd::kUf1JogUserModes);
    // The turn ids: two per slot, named, UF1 controls, not nav ids.
    for (int s = 0; s < bnd::kUf1JogUserModes; ++s)
        for (bool right : { false, true }) {
            const ButtonId id = bnd::uf1JogUserTurnId(s, right);
            int ss = -1; bool rr = !right;
            EXPECT(bnd::isUf1JogTurnId(id, &ss, &rr));
            EXPECT(ss == s && rr == right);
            EXPECT(*bnd::toName(id) != 0);
            EXPECT(bnd::fromName(bnd::toName(id)) == id);
            EXPECT(id < ButtonId::Uf1Jog);
            EXPECT(!bnd::splitPerModeNavId(id, nullptr, nullptr));
        }
    EXPECT(bnd::uf1JogUserTurnId(-1, true) == ButtonId::None);
    EXPECT(bnd::uf1JogUserTurnId(bnd::kUf1JogUserModes, false) == ButtonId::None);
    EXPECT(!bnd::isUf1JogTurnId(ButtonId::Uf1Jog));
    EXPECT(!bnd::isUf1JogTurnId(bnd::perModeNavId(ButtonId::Uf1NavDown, 10)));
}

static void testCrossAndUpgrade()
{
    // Factory: the cross of a user mode does what the physical key does.
    for (int u = 0; u < bnd::kUf1JogUserModes; ++u) {
        const int m = bnd::kUf1JogFirstUserMode + u;
        EXPECT(bnd::getBinding(0, bnd::perModeNavId(ButtonId::Uf1NavUp, m)).shortPress[P].action
               == bnd::getBinding(0, ButtonId::Uf1NavUp).shortPress[P].action);
        EXPECT(bnd::getBinding(0, bnd::perModeNavId(ButtonId::Uf1NavCentre, m)).shortPress[P].action
               == bnd::getBinding(0, ButtonId::Uf1NavCentre).shortPress[P].action);
    }
    // v57 file: no user-mode ids. The upgrade seeds them from the key, and a
    // user mode's own binding survives it.
    for (const ButtonId base : { ButtonId::Uf1NavUp, ButtonId::Uf1NavLeft,
                                 ButtonId::Uf1NavCentre, ButtonId::Uf1NavRight,
                                 ButtonId::Uf1NavDown })
        for (int u = 0; u < bnd::kUf1JogUserModes; ++u)
            bnd::clearBinding(0, bnd::perModeNavId(base, bnd::kUf1JogFirstUserMode + u));
    bnd::Binding own = bnd::getBinding(0, ButtonId::Uf1NavLeft);
    own.shortPress[P].type   = bnd::ActionType::Builtin;
    own.shortPress[P].action = "mod_shift";
    bnd::setBinding(0, bnd::perModeNavId(ButtonId::Uf1NavLeft, bnd::kUf1JogFirstUserMode + 2), own);
    bnd::save();
    setVersion(57);
    bnd::load();
    EXPECT(bnd::getBinding(0, bnd::perModeNavId(ButtonId::Uf1NavUp, bnd::kUf1JogFirstUserMode))
               .shortPress[P].action
           == bnd::getBinding(0, ButtonId::Uf1NavUp).shortPress[P].action);
    EXPECT(bnd::getBinding(0, bnd::perModeNavId(ButtonId::Uf1NavLeft, bnd::kUf1JogFirstUserMode + 2))
               .shortPress[P].action == "mod_shift");
    // The built-in modes' crosses are untouched by the new block: Lanes still
    // has its own.
    EXPECT(bnd::getBinding(0, ButtonId::Uf1NavUpLanes).shortPress[P].action == "lane_prev");
}

static void testSlotSettings()
{
    // Fresh: four empty slots, Left / right, divider 1.
    for (int s = 0; s < bnd::kUf1JogUserModes; ++s) {
        const bnd::Uf1JogUserMode m = bnd::getUf1JogUser(s);
        EXPECT(m.name.empty() && !m.relative && m.divider == 1);
    }
    bnd::Uf1JogUserMode m;
    m.name = "TakesTakesTakes";   // longer than the UF1 can show
    m.relative = true;
    m.divider  = 500;
    bnd::setUf1JogUser(1, m);
    EXPECT(bnd::getUf1JogUser(1).name == "TakesTak");
    EXPECT(bnd::getUf1JogUser(1).divider == bnd::kUf1JogUserDividerMax);
    m.name = "Zoom"; m.divider = 3;
    bnd::setUf1JogUser(3, m);
    bnd::save();
    bnd::load();
    EXPECT(bnd::getUf1JogUser(1).name == "TakesTak");
    EXPECT(bnd::getUf1JogUser(1).relative);
    EXPECT(bnd::getUf1JogUser(3).name == "Zoom");
    EXPECT(bnd::getUf1JogUser(3).divider == 3);
    EXPECT(bnd::getUf1JogUser(0).name.empty());
    EXPECT(bnd::getUf1JogUser(-1).name.empty());

    // A turn binding: resolved on Plain, nothing without one.
    bnd::ActionSlot sl; bool own = false;
    EXPECT(!bnd::resolveTurnSlot(bnd::uf1JogUserTurnId(0, true), sl, own));
    bnd::Binding b;
    b.shortPress[P].type   = bnd::ActionType::Builtin;
    b.shortPress[P].action = "mod_shift";
    bnd::setBinding(0, bnd::uf1JogUserTurnId(0, true), b);
    EXPECT(bnd::resolveTurnSlot(bnd::uf1JogUserTurnId(0, true), sl, own));
    EXPECT(own && sl.action == "mod_shift");
    // Not a turn id: refused, whatever it is bound to.
    EXPECT(!bnd::resolveTurnSlot(ButtonId::Uf1NavUp, sl, own));
    // The binding survives the file.
    bnd::save();
    bnd::load();
    EXPECT(bnd::getBinding(0, bnd::uf1JogUserTurnId(0, true)).shortPress[P].action == "mod_shift");
}

int main()
{
    char tmpl[] = "/tmp/rs_uf1joguserXXXXXX";
    g_dir = mkdtemp(tmpl);
    bnd::Host h;
    h.configDir  = [] { return g_dir; };
    h.configFile = [] { return std::string("bindings.json"); };
    bnd::setHost(std::move(h));
    bnd::registerModifierBuiltins(nullptr);
    bnd::load();

    testArithmetic();
    testIds();
    testCrossAndUpgrade();
    testSlotSettings();

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("uf1_jog_user: ok\n");
    return 0;
}
