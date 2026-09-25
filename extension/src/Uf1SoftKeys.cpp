#include "Uf1SoftKeys.h"

#include "UF1Protocol.h"
#include "Uf1Text.h"

#include <algorithm>
#include <span>
#include <vector>

namespace uf1sk {

namespace {
// The four display soft keys' LED ids (cap106, HW-confirmed): btn id - 0x18.
constexpr std::uint8_t kSoftKeyLedId[4] = { 0x01, 0x02, 0x03, 0x04 };
} // namespace

void keyColourNibbles(uint32_t rgb, bool bright,
                                 uint8_t& g4, uint8_t& r4, uint8_t& b4)
{
    const uint8_t r = static_cast<uint8_t>((rgb >> 16) & 0xFF);
    const uint8_t g = static_cast<uint8_t>((rgb >> 8)  & 0xFF);
    const uint8_t b = static_cast<uint8_t>( rgb        & 0xFF);
    int rn = uf1::quantiseChannel(r);
    int gn = uf1::quantiseChannel(g);
    int bn = uf1::quantiseChannel(b);
    if (!bright) {
        auto rest = [](int n) { return n > 0 ? 1 : 0; };
        rn = rest(rn); gn = rest(gn); bn = rest(bn);
    }
    g4 = static_cast<uint8_t>(gn);
    r4 = static_cast<uint8_t>(rn);
    b4 = static_cast<uint8_t>(bn);
}

uint32_t bindingLedColour(const uf8::bindings::Binding& bd,
                                     const uf8::bindings::ActionSlot& colSlot,
                                     bool on)
{
    uint8_t rgb3[3];
    uf8::bindings::Brightness bri;
    if (on) uf8::bindings::effectiveLedActive  (bd, colSlot, rgb3, bri);
    else    uf8::bindings::effectiveLedInactive(bd, colSlot, rgb3, bri);
    const uint32_t rgb = (uint32_t(rgb3[0]) << 16)
                       | (uint32_t(rgb3[1]) << 8)
                       |  static_cast<uint32_t>(rgb3[2]);
    // ⇨ OFF MEANS OFF. The editor offers Off / Dim / Bright for every UF1 key,
    // and until 2026-08-22 this read only "Bright?" — so Off rendered as Dim and
    // the radio did nothing (the UF8 has honoured it via brightnessToState_ since
    // 2026-05-06). Black here also drives the FF39 byte at the send sites: a lamp
    // with no colour is never "lit".
    if (bri == uf8::bindings::Brightness::Off) return 0u;
    if (bri == uf8::bindings::Brightness::Bright) return rgb;
    return (((((rgb >> 16) & 0xFF) / 4) << 16)      // dim = a quarter per channel
          | ((((rgb >> 8)  & 0xFF) / 4) << 8)
          |  (( rgb        & 0xFF) / 4));
}

uf1spread::SkCell staticBankCell(int bankNo, int i)
{
    std::string label;
    bool haveLabel = false, on = false;
    bool keyHasColour = false, keyColBright = false;
    uint32_t keyColRgb = 0;
    // Label from the bank slot (its own label, else the built-in's
    // display name); blank = unassigned. LED follows the bound
    // action's engaged state exactly like the UF8 soft-keys —
    // `bindingHasActiveSlot_` is the same resolver (built-in state
    // + REAPER GetToggleCommandState2, across all modifier slots),
    // so bright = engaged, dim = idle. The LED COLOUR is driven from
    // the binding too (see the LED block below).
    const uf8::bindings::Binding dawSlot = uf8::bindings::getUf1SoftBankSlot(bankNo, i);
    const int mIdx =
        static_cast<int>(uf8::bindings::bankModifierSnapshot());
    // The name follows the held modifier; the rule lives in Bindings.cpp
    // (uf1SoftBankKeyLabel) so ORC shows the same word for the same key.
    label = uf8::bindings::uf1SoftBankKeyLabel(bankNo, i);
    haveLabel = true;
    on = uf8::bindings::bindingHasActiveSlotForSet(dawSlot, mIdx);
    // LED colour from the binding (active vs inactive colour + brightness),
    // like the UF8 soft-keys — Frank assigns these in Settings → Bindings →
    // UF1 and HW-confirmed they render. An unassigned slot (blank label) goes
    // dark unless ledShowWhenEmpty. Drives the FF38 GRB path in the LED block.
    keyHasColour = true;
    if (label.empty() && !dawSlot.ledShowWhenEmpty) {
        keyColRgb = 0; keyColBright = false;   // empty → dark
    } else {
        // ⇨ THE SET'S OWN COLOUR, IF IT HAS ONE.
        // A modifier set is a full bank, so it carries its own LED
        // override (ActionSlot::led) just like an ordinary button's
        // modifier slot does. Reading dawSlot.color outright meant
        // the Shift set could never look different from Plain
        // (Frank 2026-08-18). effectiveLed* falls back to the key's
        // colour when the set has no override, so a set that never
        // set one paints exactly as before.
        uint8_t c[3];
        uf8::bindings::Brightness bri;
        const auto& lsp = dawSlot.shortPress[mIdx];
        if (on) uf8::bindings::effectiveLedActive  (dawSlot, lsp, c, bri);
        else    uf8::bindings::effectiveLedInactive(dawSlot, lsp, c, bri);
        keyColBright = (bri == uf8::bindings::Brightness::Bright);
        keyColRgb = (uint32_t(c[0]) << 16) | (uint32_t(c[1]) << 8)
                  | static_cast<uint32_t>(c[2]);
        // Off is a third state, not "not bright" — same fix as
        // uf1BindingLedColour_ (2026-08-22). The radio exists in the
        // slot editor, so it has to mean something here.
        if (bri == uf8::bindings::Brightness::Off) keyColRgb = 0;
    }
    uf1spread::SkCell c;
    c.label = label; c.haveLabel = haveLabel; c.on = on;
    c.hasColour = keyHasColour; c.colRgb = keyColRgb; c.colBright = keyColBright;
    return c;
}

void emitRow(const std::array<uf1spread::SkCell, 4>& cells,
             bool force, bool ledsBorrowed, bool menuOpen,
             RowCache& cc, const uf1spread::Sink& out)
{

    uint8_t skHighlight = 0;
    for (int i = 0; i < 4; ++i) {
        const uf1spread::SkCell& c = cells[static_cast<size_t>(i)];
        // Label (0x0104, <idx> + text) — SSL strip only; change-detected.
        // Latin-1 first, then abbreviated to the 13-char field: uf1SoftKeyText
        // in Uf1Text.cpp, shared with ORC, where the reasons are written out.
        const std::string label = uf1SoftKeyText(c.label);
        if (c.haveLabel && (force || label != cc.sSkLabel[i])) {
            cc.sSkLabel[i] = label;
            std::vector<uint8_t> pb;
            pb.reserve(1 + label.size());
            pb.push_back(uint8_t(i));
            pb.insert(pb.end(), label.begin(), label.end());
            out(uf1::buildScreen(uf1::scr::kSoftKeyLabel, pb));
        }
        // Soft-key LED. IDs 0x01-0x04 = btn − 0x18 (cap106, HW-CONFIRMED). Scheme =
        // the UF8 FF38(+FF39, +FF3B enable) one. SOFT-KEY 1 (id 0x01) is FF38-ONLY
        // (cap106: sending FF39 to 0x01 sticks the rectangle). Two render paths:
        //  • keyHasColour (a DAW static-bank binding colour, or a dynamic bank's
        //    class colour) → the FF38 GRB frame, saturated and level-set by
        //    uf1KeyColourNibbles_ (see there: dimming in 8-bit space destroyed
        //    the hue), PLUS the FF39 state byte on keys 2-4. Frank HW-confirmed
        //    the display soft-key colour renders (2026-07-30). The earlier
        //    "revert to state-only" was WRONG: its bug was DROPPING the FF39
        //    keys 2-4 need — here we send BOTH, so the colour shows and keys
        //    2-4 stay lit.
        //  • otherwise (Plugin/CS, Sends, learned) → the HW-verified state-only bytes
        //    (buildLedPrimary FF38 + buildLedLevel FF39, key-1-only rule), unchanged.
        uint8_t kg4 = 0, kr4 = 0, kb4 = 0;
        if (c.hasColour)
            keyColourNibbles(c.colRgb, c.colBright, kg4, kr4, kb4);
        // Change key carries the NIBBLES, not the source rgb: two colours that
        // quantise the same must not re-send, and the same colour at two levels
        // must (the old key hashed the 8-bit value and got both wrong by luck).
        const int ledState = c.hasColour
            ? ((1 << 30) | (c.on ? (1 << 24) : 0)
               | (int(kg4) << 8) | (int(kr4) << 4) | int(kb4))
            : (c.on ? 1 : 0);
        // While the BC-GR meter or the MODE menu owns these four LEDs, poison
        // the cache instead of sending: they paint them later this same tick,
        // so a send here is pure churn — and the poisoned entry is what makes
        // every LED re-send the moment they hand them back.
        //
        // ⚠ `modeMenu` joined this on 2026-08-11. Pressing MODE flips
        // grOwnsSk OFF (the BC meter stands down while the menu is up), which
        // un-parked this block on the very tick the menu paints — so all four
        // LEDs got the channel state and then the menu's, two frame sets apart
        // in one tick. That is a visible flash, and key 1 shows it worst
        // because it is FF38-only and swings lit <-> dim on its own send
        // (Frank: "kurzer flicker auf LED soft-key 1"). Same rule as the placeholder-then-real
        // trap in [[uf1-mode-edge-must-not-relayout]]: ONE writer per tick.
        if (ledsBorrowed || menuOpen) { cc.sSkLed[i] = INT_MIN; }
        else if (force || ledState != cc.sSkLed[i]) {
            cc.sSkLed[i] = ledState;
            const uint8_t id = kSoftKeyLedId[i];   // 0x01..0x04
            if (force) out(uf1::buildLed(id, true));  // FF3B enable
            if (c.hasColour) {
                out(uf1::buildColour(id, kg4, kr4, kb4));        // FF38 colour
                // ⇨ FF39 STAYS LIT AND THE NIBBLES CARRY THE LEVEL — the SEL
                // LED's rule (uf1PaintChannelStrip_, kFf39Lit + a black rgb for
                // "off"), adopted here because the old dim FF39 stacked on top
                // of an already-dimmed colour and took the second bite out of
                // exactly the hue this key exists to show. A key with nothing to
                // show gets rgb 0 above, which is dark on its own.
                if (i != 0)
                out(uf1::buildLedLevel(id, uf1::led::kFf39Lit));
            } else if (i == 0) {
                // FF38-only (Frank HW 2026-07-29). Same lit byte as keys 2-4 —
                // it was 0xf0 here, which is pure green (uf1::led::kPrimSoftKey*).
                out(uf1::buildLedPrimary(id,
                c.on ? uf1::led::kPrimSoftKeyLit : uf1::led::kPrimSoftKeyDim));
            } else {
                out(uf1::buildLedPrimary(id,
                c.on ? uf1::led::kPrimSoftKeyLit : uf1::led::kPrimSoftKeyDim));
                out(uf1::buildLedLevel(id,   c.on ? 0x00 : 0x11)); // FF39
            }
        }
        if (c.on) skHighlight |= static_cast<uint8_t>(1u << i);
    }

    // ⇨ 0x0102 — the on-screen highlight, one bit per key, bit0 = SK1.
    // HW-decoded 2026-08-17 (Frank walked 1/2/4/8/9/10/11/12/15 and every
    // combination landed exactly where the bits say). Two corpus readings had
    // failed before that, both because this element and the labels are
    // change-gated on the wire, so pairing them after the fact compares values
    // from different moments.
    //
    // ★ This is a SECOND channel, and that is the point (Frank's idea): the four
    // soft-key LEDs have three owners, and when the Bus-Comp GR meter takes them
    // the keys' own on/off state used to vanish for as long as the meter ran.
    // The highlight is not part of that arbitration, so an engaged toggle stays
    // visible underneath the metering. Sent unconditionally for the same reason
    // — deliberately NOT gated on grOwnsSk.
    // ⇨ …with ONE exception: the MODE-hold menu borrows all four keys, and it
    // overrode their labels and their LEDs but never this third channel — so a
    // dynamic bank's selected key kept its bar sitting under "PLUGIN / DAW /
    // METER / SENDS" (Frank 2026-08-26). The row's own selection stands down
    // while the menu is open. Nothing has to put it back: menuEdge folds into
    // `changed`, so the release tick re-sends the real mask through this gate.
    {
        const uint8_t hi = menuOpen ? uint8_t{0} : skHighlight;
        if (force || hi != cc.sSkHi) {
            cc.sSkHi = hi;
            out(uf1::buildScreen(0x0102, std::span<const uint8_t>(&hi, 1)));
        }
    }
}

} // namespace uf1sk
