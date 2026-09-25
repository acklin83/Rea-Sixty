#include "RmeFace.h"

#include "RmeManager.h"
#include "RmeState.h"
#include "RmeStrip.h"
#include "RmeUf1.h"
#include "TrackName.h"
#include "UF1Protocol.h"
#include "Uf1EqCurve.h"
#include "Uf1Text.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace reasixty::rme::face {

namespace rmeu = reasixty::rme::uf1;
namespace rmes = reasixty::rme::strip;

namespace {

bool modeMenu(const Host& h)      { return h.modeMenuOpen && h.modeMenuOpen(); }
bool faderTouched(const Host& h)  { return h.faderTouched && h.faderTouched(); }
bool faderHasPos(const Host& h)   { return h.faderHasPos && h.faderHasPos(); }
std::uint16_t faderPos(const Host& h) { return h.faderPos ? h.faderPos() : 0; }
int  bankNow(const Host& h)       { return h.bankNow ? h.bankNow() : 0; }
int  bankCount(const Host& h)     { return std::max(1, h.bankCount ? h.bankCount() : 1); }

int vpotSlot(const input::State& in, int pot)
{
    return pot + 4 * std::clamp(in.vpotBank.load(), 0, Config::kVpotBanks - 1);
}

void emitVpotRow(const Host& h, const Out& o, Cache&, const uf1spread::VpotRow& row, bool force)
{
    (void)o;
    if (h.emitVpotRow) h.emitVpotRow(row, force);
}

std::string dbText(double db)
{
    if (db <= -99.0) return "-inf";
    return formatDbReadout(std::pow(10.0, db / 20.0));
}

static void paintSmall(Cache& cc, const Out& o,
                       const std::string& name, const std::string& db,
                       const std::string& line, int chNo, int palette,
                       int barPos, bool barCentre, const std::string& chSoft,
                       const std::string& barText, bool force)
{
    if (force) {
        // Einmal alles leer, auch die LEDs und den Readout-Balken, die dieser
        // Maler sonst nicht anfasst. Derselbe Helfer wie fuer eine leere Spur.
        uf1spread::blankChannelZone(o.send);
        cc.sName.clear(); cc.sDb.clear(); cc.sLine.clear(); cc.sNo = INT_MIN; cc.sPal = INT_MIN;
        cc.sBar = INT_MIN; cc.sChSoft = "\x01";   // never a real label: forces the write
        cc.sBarText = "\x01";
        // Der Balken-Stil, wie im Kanalmaler: 0x01 = Zeiger (die Pan-Optik). Der
        // Init laesst ihn auf 0x03 = aus, dann zeichnet keine Position etwas.
        const uint8_t pointer = 0x01;
        o.send(::uf1::buildScreen(::uf1::scr::kBarStyle,
                                         std::span<const uint8_t>(&pointer, 1)));
    }
    // ⇨ DIE BESCHRIFTUNG DES SOFT-KEYS UEBER DEM KANAL (0x0004, ein Index). Im
    // Side-Car ist die Taste Stereo/Mono; vorher stand hier weiter, was REAPER
    // zuletzt hineingeschrieben hatte (Frank 22.09.: "zeigt der Soft-Key ueber
    // dem Kanal im Label Mono Stereo?"). Derselbe Rahmen wie im Kanalmaler.
    if (force || chSoft != cc.sChSoft) {
        cc.sChSoft = chSoft;
        std::vector<uint8_t> p;
        p.push_back(0x00);
        p.insert(p.end(), chSoft.begin(), chSoft.end());
        o.send(::uf1::buildScreen(::uf1::scr::kChSoftKey, p));
    }
    // Pan-Balken: Position 0..100, Mittelmarke 0x80 genau in der Mitte, wie der
    // REAPER-Pfad. barPos < 0 = leer.
    {
        const int key = (barPos < 0) ? -1 : barPos * 2 + (barCentre ? 1 : 0);
        if (force || key != cc.sBar) {
            cc.sBar = key;
            const uint8_t pb[2] = { static_cast<uint8_t>(barPos < 0 ? 0 : barPos),
                                    static_cast<uint8_t>(barPos >= 0 && barCentre ? 0x80 : 0x00) };
            o.send(::uf1::buildScreen(::uf1::scr::kVPotReadoutBar, pb));
        }
    }
    auto text = [&o](uint16_t addr, const std::string& t) {
        const std::string folded = utf8ToLatin1(t);
        std::vector<uint8_t> p;
        p.reserve(folded.size() + 1);
        p.push_back(0x00);
        p.insert(p.end(), folded.begin(), folded.end());
        o.send(::uf1::buildScreen(addr, p));
    };
    const std::string nm = abbreviateTrackName_(name, kUf1TrackNameChars, -1,
                                                /*foldLatin1*/ false);
    if (force || nm != cc.sName) { cc.sName = nm; text(::uf1::scr::kTrackName, nm); }
    if (force || db != cc.sDb) {
        cc.sDb = db;
        std::vector<uint8_t> p;
        p.push_back(0x00);
        for (size_t k = 0; k < 6; ++k)
            p.push_back(k < db.size() ? static_cast<uint8_t>(db[k]) : 0x00);
        p.push_back(db.empty() ? 0x20 : 'd');
        p.push_back(db.empty() ? 0x20 : 'B');
        o.send(::uf1::buildScreen(::uf1::scr::kOutputDb, p));
    }
    if (force || line != cc.sLine) { cc.sLine = line; text(::uf1::scr::kValueLine, line); }
    // ⇨ DER TEXT IM FARBBALKEN (0x0017). Bis 22.09. schrieb ihn hier niemand, also
    // stand der Plug-in-Name der REAPER-Spur darin (Frank 22.09.). Jetzt der
    // Submix, 12 Zeichen breit. ⛔ Nie leer senden: ueber dieselbe Zelle rastet der
    // Kanalmaler die Ebene ein, ein leerer Text laesst sie fallen; "nichts" sind
    // 12 Leerzeichen wie bei einer leeren Spur. Beim Verlassen schreibt der
    // Kanalmaler den Plug-in-Namen neu (uf1HandOverScreen_ zaehlt g_uf1Gen hoch).
    {
        std::string bt = utf8ToLatin1(barText);
        if (bt.size() > 12) bt = abbreviateTrackName_(bt, 12, -1, /*foldLatin1*/ false);
        if (bt.empty()) bt.assign(12, ' ');
        if (force || bt != cc.sBarText) {
            cc.sBarText = bt;
            std::vector<uint8_t> p;
            p.push_back(0x00);
            p.insert(p.end(), bt.begin(), bt.end());
            o.send(::uf1::buildScreen(::uf1::scr::kCsType, p));
        }
    }
    if (force || chNo != cc.sNo) {
        cc.sNo = chNo;
        text(::uf1::scr::kChNumber, chNo > 0 ? std::to_string(chNo) : std::string());
    }
    if (force || palette != cc.sPal) {
        cc.sPal = palette;
        // kChActive ist das "Kanal belegt"-Flag der Firmware und GATET den
        // Farbbalken; ohne es malt die Farbe nichts.
        const uint8_t act = palette > 0 ? 0x01 : 0x00;
        const uint8_t pal = static_cast<uint8_t>(palette > 0 ? palette : 0);
        o.send(::uf1::buildScreen(::uf1::scr::kChActive,
                                         std::span<const uint8_t>(&act, 1)));
        o.send(::uf1::buildScreen(::uf1::scr::kColourBar,
                                         std::span<const uint8_t>(&pal, 1)));
    }
}

} // namespace

// ── RME-Side-Car: der Maler ──────────────────────────────────────────────────
// Die UF1 als Monitor-Controller fuer TotalMix. Layout 1 (Farbbalken, Pot-Namen)
// fuer die Uebersicht und jede STRIP-Seite ohne Graph, Layout 3 fuer EQ und Low
// Cut (Plan 6a, Frank 21.09.; bis dahin stand hier "Layout 3" fuer alles).
// Plan: docs/uf1-spread-plan.md.
//
//   V-Pot 1-4     vier einstellbare Ziele (Standard Phones 1-4), Name und dB
//   Fader         der gewaehlte Kanal, ueber faderlin
//   EQ-Graph      Kanal-EQ des gewaehlten Kanals, nie Room EQ
//   kleine Anz.   Name, dB, Reihe, Nummer, Farbe des gewaehlten Kanals
//
// ⛔ DIE CHECKLISTE ([[uf1-screen-owning-mode-checklist]]), Zelle fuer Zelle:
// Ausgang = Host::modeMenuOverlay. Kleine Anzeige = paintSmall. V-Pot-Reihe =
// Host::emitVpotRow. Graph = uf1spread::eqFrames. Fader = hier.
// Soft-Keys, LEDs, Hervorhebung: beim Wirt (Host::sideCarSoftKeys,
// Host::stripSoftKeys, Host::buttonLeds).
void paint(Cache& cc, input::State& in, const Host& h, const Out& o, bool force)
{

    auto& rm = rme::manager();
    const rme::Config cfg = rm.config();
    const rme::State  st  = rm.snapshot();
    const bool   linked = (rm.link() == rme::LinkState::Online) || st.ingested > 0;

    const auto row = static_cast<rmeu::Row>(std::clamp(in.row.load(), 0, 2));
    const int  sel = linked ? input::selected(in, st, row) : -1;
    const int  sub = rmeu::effectiveSubmix(st, in.submix.load());
    bool known = false;
    const double selDb = (sel >= 0) ? rmeu::levelDb(st, row, sel, sub, known)
                                    : rme::kDbOff;

    // ── STRIP und die Ebene ─────────────────────────────────────────────────
    // ⇨ PLAN 6a (Frank 21.09.): EQ- und Low-Cut-Seiten auf Layout 3 mit dem
    // Graphen, alles andere, auch die Uebersicht, auf Layout 1 mit den vier
    // Farbbalken. Der Graph existiert nur in Layout 3, also heisst "kein
    // Graph" die Ebene wechseln, nicht den Graphen ausblenden.
    // ⛔ ZWEISTUFIG, wie SSL in cap141: zweimal {00,01}, dann das Ziel. Direkt
    // umgeschaltet blieb Layout 2 in der Sonde leer.
    const bool strip = in.strip.load() && sel >= 0;
    if (in.strip.load() && sel < 0) in.strip.store(false);   // Kanal weg
    const int  page  = strip ? input::stripPage(in, st, row, sel, cfg) : -1;
    const rme::StripPage* pg = page >= 0 ? &cfg.stripPages[static_cast<size_t>(page)] : nullptr;
    const uint8_t wantLayout = (pg && rmes::pageShowsGraph(*pg)) ? uf1spread::kLayoutGraph
                                                            : uf1spread::kLayoutOverview;
    const bool relayout = force || wantLayout != cc.sLayout;
    if (relayout) {
        cc.sLayout = wantLayout;
        // ⇨ Die Eintrittsfolge liegt seit 25.09.2026 in Uf1Spread::enterLayout,
        // Byte fuer Byte wie sie hier stand, damit ORC sie mitfaehrt statt sie
        // abzuschreiben. Die Begruendungen (zweistufig wie cap141, 0x0118 als
        // Freigabe der Soft-Key-Hervorhebung in Layout 1) stehen dort.
        uf1spread::enterLayout(wantLayout, o.send);
    }
    // Alles auf dem grossen Schirm neu, wenn die Ebene gewechselt hat. Die
    // kleine Anzeige ist ein eigenes Display und bleibt bei `force`.
    const bool big = force || relayout;

    // Beim Oeffnen und bei jedem Kanalwechsel in STRIP alle Werte dieses einen
    // Kanals holen (RMEs /sendchan, plan 5a), statt auf /sendall zu warten.
    // Bei einem Stereopaar auch die rechte Haelfte: dort liegt Phase R.
    {
        if (strip && (static_cast<int>(row) != cc.sAskRow || sel != cc.sAskCh)) {
            cc.sAskRow = static_cast<int>(row); cc.sAskCh = sel;
            rm.send(rmes::sendChanAddress(row, sel), 1.0f);
            const rme::Channel* c = rmeu::channelOf(st, row, sel);
            if (c && c->stereo) rm.send(rmes::sendChanAddress(row, sel + 1), 1.0f);
        } else if (!strip) {
            cc.sAskRow = -1; cc.sAskCh = -1;
        }
    }

    // ── Fader ───────────────────────────────────────────────────────────────
    // Touch-Entprellung wie Item Volume. faderlin 0..1 ist dieselbe Stellung
    // wie der Fader in TotalMix; die Ruecklesung kommt in dB (RmeState.h).
    const auto nowT = std::chrono::steady_clock::now();
    if (faderTouched(h)) cc.sLastTouch = nowT;
    const bool touched = faderTouched(h)
        || (nowT - cc.sLastTouch < std::chrono::milliseconds(150));
    const bool writable = sel >= 0 && (row == rmeu::Row::Output || sub >= 0);
    if (writable && touched && faderHasPos(h)) {
        const uint16_t pos = faderPos(h);
        if (pos != cc.sSentPos) {
            cc.sSentPos = pos;
            // ⛔ DIE ENDZONEN, WIE UEBERALL AM UF1-FADER. Die Hardware meldet am
            // Anschlag nie ganz 0, und roh geteilt kam der Fader nie unter
            // -64.x dB, also nie auf -inf (Frank 21.09.). uf1PosToNorm_ rastet
            // die unteren und oberen 64 Schritte auf 0 / 1 ein.
            const double lin = ::uf1::faderPosToNorm(pos);
            rm.send(rmeu::levelAddress(row, sel, sub, /*faderlin*/ true),
                    static_cast<float>(lin));
        }
        cc.sMotorPos = pos;
    } else {
        cc.sSentPos = 0xFFFF;
        const double lin = (sel >= 0 && known) ? rme::dbToFaderlin(selDb) : 0.0;
        const uint16_t want = static_cast<uint16_t>(std::lround(lin * ::uf1::kFaderMax));
        if (force || want != cc.sMotorPos || sel != cc.sMotorCh || static_cast<int>(row) != cc.sMotorRow) {
            cc.sMotorPos = want; cc.sMotorCh = sel; cc.sMotorRow = static_cast<int>(row);
            o.sendPriority(::uf1::buildMotorEnable(true));
            o.send(::uf1::buildMotorPosition(want));
        }
    }

    // ── kleine Anzeige ──────────────────────────────────────────────────────
    // Name, dB und darunter PAN (Wertzeile + Zeiger), wie im REAPER-Modus. Das
    // Submix-Ziel steht im Farbbalken, und es BLEIBT IN STRIP STEHEN (Frank
    // 22.09.: "verschwindet im strip mode, sollte bleiben"): der Fader schreibt
    // dort in denselben Submix wie in der Uebersicht, also gilt die Zeile weiter.
    // Ausgang: OUTPUT, ohne TotalMix: RME.
    {
        std::string name = "RME", db, line;
        std::string barText = !linked ? std::string("RME")
                            : row == rmeu::Row::Output ? std::string("OUTPUT")
                            : [&] {
                                  const std::string on =
                                      rmeu::displayName(st, rmeu::Row::Output, sub);
                                  return "> " + (on.empty() ? std::string("--") : on);
                              }();
        int no = 0, pal = 0, barPos = -1;
        bool barCentre = false;
        std::string chSoft;
        if (!linked) {
            line = "no TotalMix";
        } else if (sel < 0) {
            line = std::string(rmeu::rowName(row)) + " empty";
        } else {
            const rme::Channel* c = rmeu::channelOf(st, row, sel);
            name = rmeu::displayName(st, row, sel);
            db   = known ? dbText(selDb) : std::string();
            no   = sel + 1;
            if (c && c->colour >= 0 && c->colour < 9) pal = cfg.colourMap[c->colour];
            // Der Zustand, den der Soft-Key ueber dem Kanal umschaltet.
            chSoft = (c && c->stereo) ? "STEREO" : "MONO";
            bool pk = false;
            const double pan = rmeu::panValue(st, row, sel, sub, pk);
            if (pk) {
                line      = composeValueLine("Pan", formatPanReadout(pan));
                barPos    = std::clamp(static_cast<int>(std::lround((pan + 1.0) * 50.0)), 0, 100);
                barCentre = (pan == 0.0);
            } else {
                line = composeValueLine("Pan", "");
            }
        }
        paintSmall(cc, o, name, db, line, no, pal, barPos, barCentre, chSoft, barText, force);
    }

    // ── V-Pot-Reihe ─────────────────────────────────────────────────────────
    // Uebersicht und STRIP-Seiten ohne Graph: Layout 1, Name und Wert getrennt.
    // EQ/Low Cut: Layout 3, die gewohnte Zeile.
    // Die Farbe ueber jedem Pot (0x012b, nur Layout 1): TotalMix-Farbe seines
    // Kanals, in STRIP die des Fader-Kanals auf allen vier.
    std::array<uint8_t, 4> bars4{};
    auto palOf = [&](rmeu::Row r, int ch) -> uint8_t {
        const rme::Channel* c = rmeu::channelOf(st, r, ch);
        return (c && c->colour >= 0 && c->colour < 9)
            ? static_cast<uint8_t>(cfg.colourMap[c->colour]) : 0;
    };
    if (strip) {
        uf1spread::VpotRow vr;
        const uint8_t pal = palOf(row, sel);
        for (int i = 0; i < 4; ++i) {
            const rmes::Param* p = input::stripParam(cfg, page, false, i);
            double v = 0.0;
            const bool have = p && rmes::available(st, row, sel, *p)
                           && rmes::value(st, row, sel, *p, v);
            bars4[static_cast<size_t>(i)] = have ? pal : 0;
            const std::string nm = have ? rmes::label(st, row, sel, *p) : std::string();
            const std::string tx = have ? rmes::format(*p, row, v) : std::string();
            const double nrm = have ? rmes::norm(*p, row, v) : 0.0;
            if (wantLayout == 0x01) {
                // Pan zeichnete in Layout 1 einen Balken von links. Die Linie
                // (0x01) und die Mittelfuellung (0x08) blenden dort die Reihe aus
                // (gemessen 21.09.); 0x04 ist die wandernde Marke, am Geraet
                // bestaetigt (Frank 22.09.: "Marke wandert bei pan im strip").
                const bool panLike = have && p->kind == rmes::Kind::Pan;
                uf1spread::vpotCellL1(vr, i, nm, tx, nrm, !have, panLike ? uint8_t{0x04} : uint8_t{0x02});
            } else {
                uf1spread::vpotCell(vr, i, nm, tx);
                // dB um null (EQ-Gain) von der Mitte aus, alles andere als Linie.
                const bool bip = have && p->kind == rmes::Kind::Db && p->lo < 0.0 && p->hi > 0.0;
                uf1spread::vpotBar(vr, i, nrm, bip, !have);
            }
        }
        emitVpotRow(h, o, cc, vr, big);
    } else {
        uf1spread::VpotRow vr;
        for (int i = 0; i < 4; ++i) {
            if (!linked) {
                uf1spread::vpotCellL1(vr, i, i == 0 ? "RME" : "", i == 0 ? "no TotalMix" : "", 0.0, true);
                continue;
            }
            const std::string& spec = cfg.vpots[vpotSlot(in, i)].target;
            // Ein leerer Platz ist leer, kein "--": "--" heisst "Rolle ohne Ausgang".
            if (spec.empty()) {
                uf1spread::vpotCellL1(vr, i, "", "", 0.0, true);
                continue;
            }
            const rmeu::Target t = rmeu::resolveTarget(st, spec);
            if (!t.assigned || !t.visible) {
                uf1spread::vpotCellL1(vr, i, t.assigned ? "hidden" : "--", "", 0.0, true);
                continue;
            }
            bool k = false;
            const double d = rmeu::levelDb(st, t.row, t.ch, sub, k);
            const std::string label = rmeu::displayName(st, t.row, t.ch);
            // ⇨ EINE AUSWAHL, EINE MARKE (Frank 22.09., Punkt 7). Vorher trug der
            // Pot auf dem Fader einen Stern und der Submix einen Pfeil; seit ein
            // Pot-Druck bei einem Ausgang auf dem Fader den Fader mitnimmt, sind
            // beide dasselbe. Die Marke ist der Farbbalken ueber dem Pot: weiss
            // ueber dem Submix, dunkel ueber den anderen. Keine Zeichen im Namen.
            // 0x01 = weiss in der UF1-Palette (colourMap-Vorgabe fuer TotalMix
            // "weiss", am Geraet am 22.09. weiss gesehen).
            bars4[static_cast<size_t>(i)] =
                (t.row == rmeu::Row::Output && t.ch == sub) ? 0x01 : 0x00;
            uf1spread::vpotCellL1(vr, i, label, k ? dbText(d) + " dB" : std::string(),
                           k ? rme::dbToFaderlin(d) : 0.0, /*empty*/ !k);
        }
        emitVpotRow(h, o, cc, vr, big);
    }
    // Die Farbbalken, nur in Layout 1 (in Layout 3 zeichnet 0x012b nichts).
    if (wantLayout == 0x01) {
        if (big || bars4 != cc.sBars4) {
            cc.sBars4 = bars4;
            o.send(::uf1::buildScreen(::uf1::scr::kColourBars4, bars4));
        }
    }

    // ── EQ-Graph: Kanal-EQ des Fader-Kanals ─────────────────────────────────
    {
        if (big) cc.sHave = false;
        const uf1eq::Model m = (sel >= 0) ? rmeu::eqModel(st, row, sel) : uf1eq::Model{};
        std::array<uint8_t, 251> col{};
        uint8_t tail = 0x64;
        uf1eq::render(m, col, tail);
        col[0] = 0x00;
        col[1] = 0x01;
        // Nur in Layout 3: dort lebt der Graph, in Layout 1 gibt es ihn nicht.
        if (wantLayout == 0x03 && (!cc.sHave || col != cc.sCol || tail != cc.sTail)) {
            cc.sCol = col; cc.sTail = tail; cc.sHave = true;
            uf1spread::eqFrames(col, tail, o.send);
        }
    }

    // Soft-Keys: in STRIP die Schalter der Seite, sonst die Side-Car-Bank.
    // Beide gehen durch denselben Emitter, dessen Cache den Wechsel traegt.
    // ⛔ DAS MODE-MENUE SCHREIBT SEINE NAMEN DIREKT, am Emitter vorbei. Wer die
    // Keys besitzt, schreibt beim Loslassen seine eigenen zurueck, sonst bleiben
    // PLUGIN / DAW / METER / SENDS stehen (Frank 22.09., in STRIP gesehen).
    const bool menuNow = modeMenu(h);
    const bool menuClosed = cc.sMenuWas && !menuNow;
    cc.sMenuWas = menuNow;
    if (strip) {
        std::array<uf1spread::SkCell, 4> cells{};
        for (int i = 0; i < 4; ++i) {
            // ⛔ JEDER KEY BEKOMMT EINE BESCHRIFTUNG, AUCH EINE LEERE. Der Emitter
            // schreibt nur Zellen mit haveLabel; ein leerer STRIP-Key liess
            // stehen, was die Uebersicht dort hatte (Frank 22.09.).
            uf1spread::SkCell& sk = cells[static_cast<size_t>(i)];
            sk.haveLabel = true;
            const rmes::Param* p = input::stripParam(cfg, page, true, i);
            double v = 0.0;
            if (!p || !rmes::available(st, row, sel, *p) || !rmes::value(st, row, sel, *p, v))
                continue;
            // Ein Listen-Key traegt seinen Wert im Namen ("Type 1 Bell"), ein
            // Schalter zeigt ihn an der Lampe.
            sk.label = rmes::label(st, row, sel, *p);
            if (p->kind == rmes::Kind::List) sk.label += " " + rmes::format(*p, row, v);
            sk.on = (p->kind == rmes::Kind::Toggle) ? (v >= 0.5) : true;
        }
        // Bei gehaltenem MODE mit menuOpen, damit die Hervorhebung weggeht
        // (siehe uf1PaintSideCarSoftKeys_).
        if (h.stripSoftKeys) h.stripSoftKeys(cells, !menuNow && (big || menuClosed), false,
                           /*menuOpen*/ menuNow);
    } else {
        if (h.sideCarSoftKeys) h.sideCarSoftKeys(big);
    }

    // ── SOLO, CUT, SEL: der Fader-Kanal in TotalMix ─────────────────────────
    // Dieselben Bytes wie im Kanalmaler (uf1PaintChannelStrip_): SOLO/CUT hell
    // oder gedimmt, SEL weiss oder dunkel. SEL leuchtet, wenn der Ausgang auf
    // dem Fader der Submix ist; auf Eingang und Playback tut SEL nichts und ist
    // dunkel. SOLO gibt es nur im Routing, auf einem Ausgang also nie.
    {
        const rme::Channel* c = (sel >= 0) ? rmeu::channelOf(st, row, sel) : nullptr;
        const bool muted  = c && c->mute;
        const bool soloOn = sel >= 0 && rmeu::soloed(st, row, sel, sub);
        const bool selOn  = sel >= 0 && row == rmeu::Row::Output && sel == sub;
        const int packed = (muted ? 1 : 0) | (soloOn ? 2 : 0) | (selOn ? 4 : 0);
        if (force || packed != cc.sPacked) {
            cc.sPacked = packed;
            auto led = [&](uint8_t id, bool on, uint8_t litPrim, uint8_t dim) {
                if (force) o.send(::uf1::buildLed(id, true));
                o.send(::uf1::buildLedPrimary(id, on ? litPrim : dim));
                o.send(::uf1::buildLedLevel(id, on ? ::uf1::led::kFf39Lit : dim));
            };
            led(::uf1::led::kSolo, soloOn, ::uf1::led::kPrimSoloLit, ::uf1::led::kDimSolo);
            led(::uf1::led::kCut,  muted,  ::uf1::led::kPrimCutLit,  ::uf1::led::kDimCut);
            if (force) o.send(::uf1::buildLed(::uf1::led::kSel, true));
            o.send(::uf1::buildColourRgb(::uf1::led::kSel, selOn ? 0xFFFFFFu : 0x000000u));
            o.send(::uf1::buildLedLevel(::uf1::led::kSel, ::uf1::led::kFf39Lit));
        }
    }
    // Die uebrigen Tasten: Transport und SHIFT wie REAPER, der Rest dunkel.
    // 5-8 leuchtet auf Bank 2, ueber dieselbe Verfuegbarkeit wie die Spurgruppe.
    // < > hell, solange es in diese Richtung weitergeht: in STRIP die Seiten,
    // sonst die Side-Car-Baenke.
    bool arrowL = false, arrowR = false;
    if (strip) {
        const auto pages = rmes::availablePages(st, row, sel, cfg.stripPages);
        const auto it = std::find(pages.begin(), pages.end(), page);
        if (it != pages.end()) {
            arrowL = it != pages.begin();
            arrowR = (it + 1) != pages.end();
        }
    } else {
        const int nb = bankCount(h);
        arrowL = bankNow(h) > 0;
        arrowR = bankNow(h) < nb - 1;
    }
    const rme::Channel* selCh = (sel >= 0) ? rmeu::channelOf(st, row, sel) : nullptr;
    // Bank ◄ ► blaettern im Side-Car nichts mehr; 5-8 leuchtet auf der zweiten
    // Haelfte der Soft-Key-Bank (Frank 26.09.: "snapshots mit 5-8").
    if (h.buttonLeds) h.buttonLeds(force, uf1spread::BtnAvail{ arrowL, arrowR, false, false,
                                            !strip && in.skHalf.load() == 1,
                                            selCh && selCh->stereo });

    // ── Zeitfeld: immer der Jog-Kanal (Main) in dB ──────────────────────────
    // ⛔ KEINE REAPER-ZEIT IM SIDE-CAR (Frank 21.09.: "immer im time display
    // anzeigen"). Vorher stand hier REAPERs Uhr, Main nur als Einblendung beim
    // Drehen und dauerhaft in Kopfzelle 4 unter FINE CTRL. Das Zeitfeld gehoert
    // jetzt dem Side-Car wie der Rest der Flaeche; nur eine Einblendung (Bankname)
    // darf es kurz haben, danach steht Main wieder da.
    const rmeu::Target jogT = rmeu::resolveTarget(st, cfg.jogTarget);
    bool jogKnown = false;
    const double jogDb = jogT.visible
        ? rmeu::levelDb(st, jogT.row, jogT.ch, sub, jogKnown) : rme::kDbOff;
    {
        std::array<uint8_t, 11> tc{};
        const std::string flash = h.tcFlash ? h.tcFlash() : std::string();
        if (!flash.empty())
            ::uf1::encodeSeg7Text(flash.c_str(), tc.data());
        else if (jogKnown)
            ::uf1::encodeSeg7Text(dbText(jogDb).c_str(), tc.data());
        if (big || tc != cc.sTc) {
            cc.sTc = tc;
            o.send(::uf1::buildScreen(::uf1::scr::kTimecode, tc));
        }
    }

    // ── Zyklus fuer den Pacer: Kopfzeile und Pegel ──────────────────────────
    // ⛔ OHNE DAS SENDET DER PACER WEITER DEN LETZTEN REAPER-STAND. Kopfzeile und
    // Pegel am Fader stecken im Schnappschuss, den sonst nur uf1PaintChannel_
    // baut, und der laeuft im Side-Car nicht (Frank 21.09.: Kopfzeile zeigte
    // REAPER, der Pegel kam von der Spur).
    {
        // ⚠ DIE REIHENLISTE NUR IN LAYOUT 3. Ihre Zustandsbytes (0110/011a/011e,
        // dazu 011d im Pacer) sind dort gemessen; in Layout 1 flackerte beim
        // Halten von MODE die oberste Reihe (Frank 22.09.), und das ist der
        // einzige Unterschied zu Layout 3, den wir dabei schicken. VERMUTUNG,
        // nicht gemessen. Layout 1 hat ohnehin nur CELL1/CELL2, drei Zeilen passen
        // nicht: dort steht die gewaehlte Reihe in CELL1.
        const bool menuHeld = modeMenu(h);
        const bool listOpen = menuHeld && wantLayout == 0x03;
        // Die drei Zustandsbytes der Liste, einmal pro Flanke, wie im Kanalmaler.
        if (big || listOpen != cc.sListOpen) {
            cc.sListOpen = listOpen;
            const uint8_t s0110 = listOpen ? 0x07 : 0x0f;
            const uint8_t s011a = listOpen ? 0x03 : 0x02;
            const uint8_t s011e = listOpen ? 0x1f : 0x19;
            o.send(::uf1::buildScreen(0x0110, std::span<const uint8_t>(&s0110, 1)));
            o.send(::uf1::buildScreen(0x011a, std::span<const uint8_t>(&s011a, 1)));
            o.send(::uf1::buildScreen(0x011e, std::span<const uint8_t>(&s011e, 1)));
        }

        auto hdr = uf1spread::pageHeader(bankNow(h) + 1, bankCount(h), /*fine*/ false);
        auto putCell = [&](int cell, const std::string& t) {
            for (size_t k = 0; k < 25; ++k)
                hdr[static_cast<size_t>(cell) * 25 + k] =
                    (k < t.size()) ? static_cast<uint8_t>(t[k]) : 0;
        };
        if (listOpen) {
            // MODE + Encoder: die drei Reihen, wie die Encoder-Liste.
            int vis[3] = { 0, 1, 2 };
            uf1spread::fillModeList(hdr, vis, 3, 3, static_cast<int>(row),
                             [](int r) { return rmeu::rowName(static_cast<rmeu::Row>(r)); });
        } else if (menuHeld) {
            putCell(0, rmeu::rowName(row));
        } else if (strip) {
            // STRIP: die Seite in CELL1, "3/8" in CELL2 und in der Zelle, die
            // Layout 3 unter SOFT KEYS zeigt. Layout 1 zeigt nur CELL1 und CELL2.
            const auto pages = rmes::availablePages(st, row, sel, cfg.stripPages);
            if (!pg) {
                putCell(0, "NO SETTINGS");
            } else {
                const int at = static_cast<int>(std::find(pages.begin(), pages.end(), page)
                                                - pages.begin());
                putCell(0, utf8ToLatin1(pg->name));
                const std::string n = std::to_string(at + 1) + "/" + std::to_string(pages.size());
                putCell(1, n);
                putCell(3, n);
            }
        } else if (!linked) {
            putCell(0, "RME");
        } else {
            // Nur die Reihe; der Submix steht seit 22.09. im Farbbalken der
            // kleinen Anzeige, der hat mehr Platz (Frank: "Nur die Reihe").
            putCell(0, rmeu::rowName(row));
        }
        // Uebersicht: die Side-Car-Bank auch in CELL2, denn Layout 1 zeigt die
        // Zelle unter SOFT KEYS (3) nicht.
        if (!menuHeld && !strip) {
            putCell(1, std::to_string(bankNow(h) + 1) + "/" + std::to_string(bankCount(h)));
        }
        // Zelle 4 (unter FINE CTRL) leer: Main steht im Zeitfeld, und REAPERs
        // Fine-Zustand, den uf1PageHeader_ dort einträgt, gilt hier nicht.
        putCell(4, std::string());

        std::vector<std::vector<uint8_t>> meters, tail;
        // Pegel des gewaehlten Kanals aus TotalMix (/level/<bus>/<n> und n+1),
        // Peak in dB, dieselbe Umrechnung wie der REAPER-Pfad. Kommt nur, wenn
        // in TotalMix fuer dieses Remote "Send Peak Level" an ist; sonst 0.
        uint8_t lvL = 0, lvR = 0;
        if (sel >= 0) {
            const std::map<int, double>& lv = row == rmeu::Row::Output ? st.levelOut
                                            : row == rmeu::Row::Input  ? st.levelIn
                                                                       : st.levelPb;
            const auto a = lv.find(sel);
            if (a != lv.end()) lvL = dbToVuByte_(a->second);
            lvR = lvL;
            // Rechts nur bei einem Stereo-Kanal von n + 1: bei Mono ist n + 1 der
            // naechste Kanal, nicht die andere Seite.
            const rme::Channel* sc = rmeu::channelOf(st, row, sel);
            if (sc && sc->stereo)
                if (const auto bR = lv.find(sel + 1); bR != lv.end())
                    lvR = dbToVuByte_(bR->second);
        }
        const uint8_t k0009[] = { lvL, lvR, 0x00, 0x00 };
        const uint8_t z4[]    = { 0x00, 0x00, 0x00, 0x00 };
        const uint8_t z1      = 0x00;
        meters.push_back(::uf1::buildScreen(0x0009, k0009));
        meters.push_back(::uf1::buildScreen(0x000a, z4));
        meters.push_back(::uf1::buildScreen(0x0015, std::span<const uint8_t>(&z1, 1)));
        meters.push_back(::uf1::buildScreen(0x0016, std::span<const uint8_t>(&z1, 1)));
        tail.push_back(::uf1::buildScreen(::uf1::scr::kHeaderRow,
            std::span<const uint8_t>(hdr.data(), hdr.size())));
        const uint8_t hl = listOpen ? 0x19 : 0x00;
        tail.push_back(::uf1::buildScreen(0x011d, std::span<const uint8_t>(&hl, 1)));
        // ⇨ THE HOST PACES THESE. In the extension they ride the meter stream's
        // cycle (uf1CyclePacerLoop_); ORC has no stream and sends them itself.
        if (h.publishCycle) h.publishCycle(std::move(meters), std::move(tail));
    }

    // Der Ausgang. Ohne ihn ist das Side-Car eine Falle.
    if (h.modeMenuOverlay) h.modeMenuOverlay(force);
}

} // namespace reasixty::rme::face
