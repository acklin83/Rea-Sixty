#!/usr/bin/env python3
"""
Builds rea-sixty-ssl-preview.pdf — a project explainer + user manual
preview for SSL.

Rewritten from scratch 2026-05-13. Every factual claim in this
document was cross-checked against origin/main source. Notes inline
where the supporting evidence lives.

Regenerate with:
    python3 docs/outreach/build_ssl_preview_pdf.py
"""

from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT, TA_JUSTIFY
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import cm, mm
from reportlab.platypus import (
    BaseDocTemplate,
    Frame,
    NextPageTemplate,
    PageBreak,
    PageTemplate,
    Paragraph,
    Spacer,
    Table,
    TableStyle,
)


OUT = Path(__file__).parent / "rea-sixty-ssl-preview.pdf"

INK = colors.HexColor("#111419")
MUTED = colors.HexColor("#5a6270")
ACCENT = colors.HexColor("#009FD5")
RULE = colors.HexColor("#d6dadf")
PANEL = colors.HexColor("#f4f6f8")

base = getSampleStyleSheet()
H1 = ParagraphStyle("H1", parent=base["Heading1"],
                    fontName="Helvetica-Bold", fontSize=22, leading=26,
                    textColor=INK, spaceBefore=0, spaceAfter=10)
H2 = ParagraphStyle("H2", parent=base["Heading2"],
                    fontName="Helvetica-Bold", fontSize=14, leading=18,
                    textColor=INK, spaceBefore=14, spaceAfter=6)
H3 = ParagraphStyle("H3", parent=base["Heading3"],
                    fontName="Helvetica-Bold", fontSize=11, leading=14,
                    textColor=INK, spaceBefore=10, spaceAfter=3)
BODY = ParagraphStyle("Body", parent=base["BodyText"],
                      fontName="Helvetica", fontSize=10, leading=14,
                      textColor=INK, alignment=TA_JUSTIFY, spaceAfter=6)
BODY_L = ParagraphStyle("BodyL", parent=BODY, alignment=TA_LEFT)
LEAD = ParagraphStyle("Lead", parent=BODY, fontSize=11, leading=15, spaceAfter=10)
MONO = ParagraphStyle("Mono", parent=BODY, fontName="Courier", fontSize=9,
                      leading=12, textColor=INK, alignment=TA_LEFT, spaceAfter=6)
BULLET = ParagraphStyle("Bullet", parent=BODY, leftIndent=14, bulletIndent=2,
                        spaceAfter=2, alignment=TA_LEFT)
EYEBROW = ParagraphStyle("Eyebrow", parent=BODY, fontSize=8.5, leading=11,
                         textColor=ACCENT, alignment=TA_LEFT,
                         fontName="Helvetica-Bold", spaceAfter=2)


def _page_chrome(canvas, doc):
    canvas.saveState()
    w, h = A4
    canvas.setStrokeColor(RULE)
    canvas.setLineWidth(0.4)
    canvas.line(2.0 * cm, h - 1.6 * cm, w - 2.0 * cm, h - 1.6 * cm)
    canvas.setFont("Helvetica", 8)
    canvas.setFillColor(MUTED)
    canvas.drawString(2.0 * cm, h - 1.35 * cm, "Rea-Sixty  ·  SSL preview")
    canvas.drawRightString(w - 2.0 * cm, h - 1.35 * cm,
                           "Open-source REAPER extension for SSL UF8 / UC1")
    canvas.line(2.0 * cm, 1.6 * cm, w - 2.0 * cm, 1.6 * cm)
    canvas.setFont("Helvetica", 8)
    canvas.setFillColor(MUTED)
    canvas.drawString(2.0 * cm, 1.2 * cm,
                      "github.com/acklin83/reaper-uf8  ·  MIT-licensed  ·  Not affiliated with Solid State Logic Ltd.")
    canvas.drawRightString(w - 2.0 * cm, 1.2 * cm, f"Page {doc.page}")
    canvas.restoreState()


def _cover(canvas, doc):
    canvas.saveState()
    w, h = A4
    canvas.setFillColor(ACCENT)
    canvas.rect(0, h - 1.0 * cm, w, 1.0 * cm, fill=1, stroke=0)
    canvas.setStrokeColor(RULE)
    canvas.setLineWidth(0.4)
    canvas.line(2.0 * cm, 1.6 * cm, w - 2.0 * cm, 1.6 * cm)
    canvas.setFont("Helvetica", 8)
    canvas.setFillColor(MUTED)
    canvas.drawString(2.0 * cm, 1.2 * cm,
                      "Document prepared as a courtesy preview for Solid State Logic.")
    canvas.drawRightString(w - 2.0 * cm, 1.2 * cm,
                           "Not for redistribution outside SSL.")
    canvas.restoreState()


def build_doc():
    doc = BaseDocTemplate(
        str(OUT),
        pagesize=A4,
        leftMargin=2.0 * cm, rightMargin=2.0 * cm,
        topMargin=2.2 * cm, bottomMargin=2.0 * cm,
        title="Rea-Sixty — SSL Preview",
        author="Rea-Sixty project (acklin83/reaper-uf8)",
        subject="Project explainer and user-manual preview for Solid State Logic",
    )
    frame = Frame(doc.leftMargin, doc.bottomMargin,
                  doc.width, doc.height, id="body")
    doc.addPageTemplates([
        PageTemplate(id="cover", frames=frame, onPage=_cover),
        PageTemplate(id="body",  frames=frame, onPage=_page_chrome),
    ])
    return doc


def p(text, style=BODY):
    return Paragraph(text, style)


def code(text):
    safe = (text.replace("&", "&amp;")
                .replace("<", "&lt;")
                .replace(">", "&gt;"))
    safe = safe.replace("\n", "<br/>")
    return Paragraph(
        f'<font face="Courier" size="9">{safe}</font>',
        ParagraphStyle("c", parent=MONO, backColor=PANEL,
                       borderPadding=6, leftIndent=0,
                       spaceBefore=2, spaceAfter=10),
    )


def panel(flowables, bg=PANEL, pad=8):
    inner = Table([[f] for f in flowables], colWidths=[17.0 * cm - 2 * pad])
    inner.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), bg),
        ("LEFTPADDING", (0, 0), (-1, -1), pad),
        ("RIGHTPADDING", (0, 0), (-1, -1), pad),
        ("TOPPADDING", (0, 0), (-1, -1), pad),
        ("BOTTOMPADDING", (0, 0), (-1, -1), pad),
        ("BOX", (0, 0), (-1, -1), 0.4, RULE),
    ]))
    return inner


def kv_table(rows, col_widths=None):
    if col_widths is None:
        col_widths = [4.0 * cm, 13.0 * cm]
    data = [[p(k, ParagraphStyle("kvk", parent=BODY, fontName="Helvetica-Bold")),
             p(v, BODY_L)] for k, v in rows]
    t = Table(data, colWidths=col_widths)
    t.setStyle(TableStyle([
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LINEBELOW", (0, 0), (-1, -2), 0.25, RULE),
        ("TOPPADDING", (0, 0), (-1, -1), 5),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
    ]))
    return t


def grid_table(headers, rows, col_widths):
    head = [[p(h, ParagraphStyle("th", parent=BODY,
                                 fontName="Helvetica-Bold",
                                 textColor=colors.white)) for h in headers]]
    body = [[p(c, BODY_L) for c in r] for r in rows]
    t = Table(head + body, colWidths=col_widths, repeatRows=1)
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), INK),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, PANEL]),
        ("LINEBELOW", (0, 0), (-1, -1), 0.25, RULE),
        ("LEFTPADDING", (0, 0), (-1, -1), 6),
        ("RIGHTPADDING", (0, 0), (-1, -1), 6),
        ("TOPPADDING", (0, 0), (-1, -1), 5),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
    ]))
    return t


# ---------------------------------------------------------------------------
# Cover
# ---------------------------------------------------------------------------

def cover_page():
    # Verified: README.md "Rea-Sixty" public name and "reaper-uf8"
    # repository name; Bindings.h kUserBankCount = 12; SSL device PIDs
    # in UF8Device.cpp / UC1Protocol.h.
    s = []
    s.append(Spacer(1, 5.0 * cm))
    s.append(p("Project preview &amp; user manual",
               ParagraphStyle("eyebrow_cover", parent=EYEBROW, fontSize=10)))
    s.append(p("Rea-Sixty",
               ParagraphStyle("title", parent=H1,
                              fontSize=44, leading=48, spaceAfter=4)))
    s.append(p("An open-source REAPER extension for the SSL UF8 and UC1.",
               ParagraphStyle("subtitle", parent=BODY,
                              fontSize=14, leading=18, textColor=MUTED,
                              spaceAfter=20)))
    s.append(Spacer(1, 1.0 * cm))
    s.append(panel([
        p("Prepared for Solid State Logic as a courtesy preview ahead of "
          "any formal correspondence. Aim: transparency. What the project "
          "is, how it works, what ships today, what it asks of SSL.", BODY),
    ]))
    s.append(Spacer(1, 1.2 * cm))
    s.append(kv_table([
        ("Project",  "Rea-Sixty (repository: <i>reaper-uf8</i>)"),
        ("Type",     "REAPER extension &mdash; C++, csurf_inst, libusb, vendored Dear ImGui (via ReaImGui)"),
        ("Hardware", "SSL UF8 (VID&nbsp;0x31E9 / PID&nbsp;0x0021), SSL UC1 (PID&nbsp;0x0023)"),
        ("Platform", "macOS (tested). Windows and Linux are on the roadmap, not yet built."),
        ("Source",   '<font color="#009FD5">github.com/acklin83/reaper-uf8</font>'),
        ("License",  "MIT &mdash; original code only; no SSL material is decompiled, reproduced, or redistributed."),
        ("Document date", "2026-05-13"),
    ]))
    s.append(NextPageTemplate("body"))
    s.append(PageBreak())
    return s


# ---------------------------------------------------------------------------
# Contents
# ---------------------------------------------------------------------------

def toc_page():
    s = []
    s.append(p("Contents", H1))
    s.append(Spacer(1, 4 * mm))
    items = [
        ("PART 1", "PROJECT"),
        ("1.",  "What it is, in one paragraph"),
        ("2.",  "Why the project exists"),
        ("3.",  "How it works"),
        ("4.",  "What ships today"),
        ("5.",  "What is still under construction"),
        ("6.",  "Legal posture"),
        ("7.",  "Asks of SSL"),
        ("PART 2", "USER MANUAL (preview)"),
        ("8.",  "Install"),
        ("9.",  "First start"),
        ("10.", "Buttons, faders, V-Pots"),
        ("11.", "Layers, modifiers, the Channel encoder"),
        ("12.", "Surface filters: Folder Mode, Show Only Selected, Selection Sets"),
        ("13.", "Send / Receive routing"),
        ("14.", "SSL Strip Mode and Instance Cycle"),
        ("15.", "FX Learn"),
        ("16.", "Bindings file &amp; CSI import"),
        ("17.", "Troubleshooting"),
        ("18.", "Uninstall"),
    ]
    rows = []
    for n, name in items:
        if n.startswith("PART"):
            rows.append([
                p(n, ParagraphStyle("toc_part", parent=BODY,
                                    fontName="Helvetica-Bold",
                                    textColor=ACCENT, fontSize=9)),
                p(name, ParagraphStyle("toc_sec", parent=BODY,
                                       fontName="Helvetica-Bold",
                                       textColor=ACCENT)),
            ])
            continue
        rows.append([
            p(n, ParagraphStyle("toc_n", parent=BODY,
                                fontName="Helvetica-Bold")),
            p(name, BODY_L),
        ])
    t = Table(rows, colWidths=[2.0 * cm, 15.0 * cm])
    t.setStyle(TableStyle([
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
        ("TOPPADDING", (0, 0), (-1, -1), 5),
    ]))
    s.append(t)
    s.append(PageBreak())
    return s


# ---------------------------------------------------------------------------
# Part 1
# ---------------------------------------------------------------------------

def part_project():
    s = []

    # 1.
    s.append(p("1. What it is, in one paragraph", H2))
    # Verified: README.md status section; architecture-decision.md;
    # main.cpp registerSurface() = csurf_inst path.
    s.append(p(
        "Rea-Sixty is an independent open-source REAPER extension that "
        "drives the SSL UF8 and UC1 directly. It claims the controllers' "
        "vendor-USB interface over libusb, replays the host-side "
        "initialisation sequence we observed SSL 360&deg; emit, and "
        "runs the keepalive cycle that keeps the Plug-in Mixer rendering "
        "path alive. Everything the user sees on the hardware &mdash; "
        "track name, colour, fader, pan, sends, receives, automation "
        "state, focused-FX parameters, gain reduction &mdash; is read "
        "from REAPER's C API through a "
        "<font face='Courier'>csurf_inst</font>.", LEAD))

    # 2.
    s.append(p("2. Why the project exists", H2))
    # Verified: SSL UF8 manual sections cited in docs/uf8-manual-reference.md;
    # docs/architecture-decision.md "Why" list.
    s.append(p(
        "The UF8's scribble strips can render DAW track colours, but "
        "only when the controller is in Plug-in Mixer Mode, which "
        "requires an SSL VST3 plug-in on every track that should appear. "
        "For sessions of 100+ tracks this is impractical. MCU and HUI "
        "carry no colour at all, so a &ldquo;just talk MCU&rdquo; "
        "workaround is impossible for the colour use case.", BODY))
    s.append(p(
        "SSL 360&deg; also holds the UF8's vendor-USB interface with an "
        "exclusive claim, so coexisting next to it is not possible. The "
        "only path was to take over 360&deg;'s host-side responsibilities "
        "for REAPER specifically &mdash; which is what Rea-Sixty does.", BODY))

    # 3.
    s.append(p("3. How it works", H2))
    # Verified: UF8Device.cpp (libusb open, init replay, keepalive at
    # 150 ms in kPmKeepaliveInterval), UC1Device.cpp (kKeepaliveInterval
    # = 150 ms), main.cpp Surface::Run() loop.
    s.append(p(
        "On load, the extension enumerates SSL devices over libusb, "
        "claims their vendor interface, replays the captured init "
        "sequence, and starts a host thread that emits the Plug-in "
        "Mixer keepalive every 150&nbsp;ms (both UF8 and UC1). "
        "Inbound vendor-USB frames are parsed into press / release / "
        "fader / V-Pot / knob events and dispatched through the REAPER "
        "C API. Outbound state is rendered to the controllers as "
        "scribble text, colour bars, LED frames, fader motor positions, "
        "gain-reduction LED ladders and VU meters.", BODY))
    s.append(panel([
        p("Topology", ParagraphStyle("ph", parent=H3, spaceBefore=0)),
        code("REAPER  <->  Rea-Sixty (csurf_inst)  <->  UF8 / UC1 (vendor-USB, libusb)"),
    ]))

    s.append(p("Decoded protocol surface", H3))
    # All verified by direct grep / read of Protocol.cpp / UC1Protocol.cpp /
    # UF8Device.cpp / Palette.cpp / UC1Surface.cpp.
    for b in [
        "UF8 + UC1 frame format and checksum "
        "(<font face='Courier'>FF &lt;cmd&gt; &lt;len&gt; &lt;data&gt; &lt;chk&gt;</font>).",
        "UF8 init sequence and the 13&nbsp;B / 64&nbsp;B Plug-in-Mixer keepalive pair at 150&nbsp;ms.",
        "UC1 init / keepalive (also 150&nbsp;ms).",
        "Per-strip TFT colour command and the UF8's 16-entry palette. 11 indices have known RGB values; 5 are off / unmapped. Off-palette REAPER colours are matched by chromaticity to the closest known entry.",
        "All Plug-in Mixer scribble zones: track name, parameter label, value line, channel-strip type, O/PdB fader readout, channel number, colour bar.",
        "Per-strip SEL / MUTE / SOLO LED frames; transport LEDs; 12-segment metering frame.",
        "UF8 inbound button-ID map (per-strip + globals); fader 16-bit position and touch.",
        "UC1 inbound button + knob IDs (stable across plug-in contexts); GR meter (16-bit BE dB&times;10); VU bars; display zones.",
    ]:
        s.append(Paragraph(f"&bull;&nbsp;&nbsp;{b}", BULLET))
    return s


def part_what_ships():
    s = []
    s.append(p("4. What ships today", H2))
    s.append(p(
        "Status as of the document date. macOS only. Every entry below "
        "was verified directly in the source on origin/main &mdash; not "
        "inferred from plans or roadmap.", BODY))

    # Each row's claim is followed by a code reference in the source comment.
    rows = [
        # Verified: README.md, architecture-decision.md.
        ["UF8 standalone replacement",
         "csurf_inst with native init / keepalive / scribble / colour / LED / meter / fader motor / button routing. No SSL 360&deg; required; no MCU bridge."],
        # Verified: ColorSync.{cpp,h}, Palette.cpp chromaticity matching.
        ["DAW-layer scribble-strip colours",
         "Polled from <font face='Courier'>GetTrackColor()</font> and pushed on bank shift / theme change. Chromaticity-quantising palette match for off-palette colours."],
        # Verified: main.cpp CSurf_OnVolumeChange + fader rendering at
        # 16-bit precision (vs MCU pitch-bend's 14-bit).
        ["16-bit fader resolution",
         "Direct <font face='Courier'>CSurf_OnVolumeChange</font>; no MCU 14-bit lossy stage."],
        # Verified: UC1Surface.cpp parameter mirror logic and PluginMap.cpp
        # tables for CS2 / BC2 / 4K B / 4K E / 4K G / Link.
        ["UC1 parameter mirror",
         "Physical knobs drive the focused track's SSL Channel Strip 2 / Bus Compressor 2 / 4K&nbsp;B / 4K&nbsp;E / 4K&nbsp;G / 360 Link in real time. Values mirrored back to the UC1 displays."],
        # Verified: UC1Surface.cpp TrackFX_GetNamedConfigParm
        # "GainReduction_dB" lookup; piecewise dB ladder 3/6/10/14/20 with
        # 5 LEDs × 6 sub-steps = 30 sub-steps over 20 dB.
        ["GR meter (UC1 + UF8)",
         "Audio-driven, read via the PreSonus VST3 "
         "<font face='Courier'>GainReduction_dB</font> named-config-parm. "
         "Same 30-sub-step index drives both the UC1 LED ladder and the "
         "UF8 GR byte, so the two hardware meters stay synchronised. "
         "Piecewise dB matches the SSL plug-in's 0–3–6–10–14–20&nbsp;dB scale."],
        # Verified: main.cpp ssl_strip_mode_toggle + ssl_strip_mode_toggle_with_gui;
        # both end up flipping g_pluginFaderMode.
        ["SSL Strip Mode",
         "<font face='Courier'>ssl_strip_mode_toggle</font> (factory default: Shift+Plugin). "
         "Fader and V-Pot leave DAW track volume / pan and follow the focused SSL plug-in's "
         "output level / pan instead. With-GUI variant additionally opens / closes the "
         "plug-in's REAPER GUI."],
        # Verified: main.cpp uf8_plugin_mode_toggle.
        ["UF8 Plug-in Deep Edit",
         "<font face='Courier'>uf8_plugin_mode_toggle</font> &mdash; mutually exclusive with SSL Strip Mode; for deep editing inside a single plug-in instance."],
        # Verified: main.cpp instance_cycle + encoder_instance builtins,
        # EncoderMode::Instance enum.
        ["Instance cycle",
         "Channel encoder in Instance mode (or "
         "<font face='Courier'>instance_cycle</font> on any bound button) walks plug-in instances within the focused domain (Channel Strip or Bus Comp) on the selected track. Picks up UF8-only user-mapped plug-ins too."],
        # Verified: main.cpp send/receive route state, getTrackSendName /
        # GetTrackReceiveName usage; AllTracksN + ThisTrack states for
        # both V-Pot and Fader, both Send and Receive (4 independent pairs).
        ["Send / Receive layers",
         "AllTracks-N (every strip shows send/receive index N for its track) and ThisTrack (8 strips show the focused track's first 8 sends/receives). Independent state for V-Pot vs Fader &times; Send vs Receive. LEDs, colours, names and mute follow the routed entity."],
        # Verified: main.cpp g_folderMode atomic + folder_mode builtin +
        # I_FOLDERDEPTH parents-only filter + g_spilledParent long-press spill;
        # kSelLongPressMs = 500 ms.
        ["Folder Mode",
         "Bindable surface filter: only top-level / depth-0 tracks fill the 8 strips. Long-press SEL (500&nbsp;ms) on a folder-start track spills its children onto the strips to the right; long-press again to collapse."],
        # Verified: main.cpp g_showOnlySelected; selset_recall builtin
        # accepting param 1..8; g_selsetActive.
        ["Show Only Selected + Selection Sets",
         "<font face='Courier'>show_only_selected</font> filters strips to live-selected tracks. 8 named slots (Selection Sets) recall via <font face='Courier'>selset_recall</font> param&nbsp;1..8 &mdash; GUID-keyed so reorders / deletes don't corrupt them."],
        # Verified: Bindings.cpp/h Behavior types (Momentary, Toggle, …),
        # multi-step chain support in Bindings.h (PendingChain), Learn
        # mode in SettingsScreen.cpp.
        ["Bindings + Learn",
         "Per-strip, transport, global and soft-keys. Modifier-aware dispatch (Shift / Fine, Norm, Rec, Auto). Multi-action chains with optional inter-step delays. Per-binding label, colour, brightness. Learn mode in the Settings screen."],
        # Verified: Bindings.h kUserBankCount = 12, kUserBankSlots = 8;
        # Bindings.cpp kBankIds[6] = V-POT + Bank1..Bank5 SSL stock banks.
        ["12 user Soft-Key Banks + 6 SSL stock banks",
         "Bindings.h <font face='Courier'>kUserBankCount&nbsp;=&nbsp;12</font>, 8 slots per bank. The 6 SSL stock banks (V-POT, Bank&nbsp;1..5) are addressable via <font face='Courier'>softkey_bank_select</font> with param&nbsp;0..5."],
        # Verified: UserPluginCatalog.cpp/.h + SettingsScreen.cpp Fx-Learn
        # tab (drawFxLearn) + fillSequentialUf8_ + "Fill sequential (right)"
        # menu item.
        ["FX Learn for any VST / JS / AU",
         "GUID-keyed bindings survive FX-slot reorders. Param snapshot persisted, so a learned slot stays editable when no live FX instance is present. &ldquo;Fill sequential (right)&rdquo; on the right-click menu auto-fills consecutive parameters into the cells to the right of the current slot."],
        # Verified: VirtualNotch.h/.cpp; usage in main.cpp and UC1Surface.cpp.
        ["Virtual notch",
         "Magnet behaviour at 0&nbsp;dB / centre for EQ gains and pan: rotation snaps to neutral when crossing or entering the zone, then continues normally so 0.1&nbsp;dB nudges still work."],
        # Verified: SettingsScreen.cpp drawDevice / drawBindings /
        # drawSoftKeyBanks / drawFxLearn / drawModes / drawSelectionSets /
        # drawAbout; SettingsScreen.cpp drawCsiImportSection.
        ["Settings screen (in-REAPER window)",
         "Dockable ImGui window with left-rail navigation. Sections: Device, Bindings, Soft-Key Banks, FX Learn, Modes, Selection Sets, About. CSI Import preview in the Bindings section for users migrating from CSI."],
        # Verified: ThemeBridge.{cpp,h}; pushAll/popAll bracket the window.
        ["REAPER-theme follow",
         "ThemeBridge polls REAPER's colour theme and re-tints the ImGui window live (light / dark switch follows immediately)."],
    ]
    s.append(grid_table(
        ["Capability", "Notes"],
        rows,
        col_widths=[5.5 * cm, 11.5 * cm],
    ))
    return s


def part_construction():
    s = []
    s.append(p("5. What is still under construction", H2))
    s.append(p(
        "Two areas are honest about not being done:", BODY))
    # Verified: MixerLayout.cpp is 33 lines, body of draw() prints
    # "Plugin Mixer — scaffold (Phase 2.6b fills this in)" and lists
    # planned sections as comments only.
    s.append(panel([
        p("Plug-in Mixer pane", H3),
        p("The in-REAPER window has a left-rail entry called "
          "&ldquo;Mixer&rdquo;. The rest of the rail (Device, Bindings, "
          "Soft-Key Banks, FX Learn, Modes, Selection Sets, About) is "
          "real and shipping. The Mixer pane itself is currently a "
          "scaffold: the window opens to it, but the contents are a "
          "placeholder reminding the developer what still needs to be "
          "drawn (channel-strip columns per track, Bus-Compressor rack, "
          "audio meters, fader / pan widgets). Source: "
          "<font face='Courier'>extension/src/MixerLayout.cpp</font>.", BODY),
    ]))
    s.append(panel([
        p("Windows and Linux ports", H3),
        p("Capture and analysis tooling already runs on Windows; the "
          "Windows / Linux native build of the extension is on the "
          "roadmap and has not started.", BODY),
    ]))
    return s


def part_legal_asks():
    s = []
    s.append(p("6. Legal posture", H2))
    # Verified: README.md "Legal & Safety" section verbatim; LICENSE = MIT;
    # docs/interop-rationale.md.
    s.append(panel([
        p("Trademarks", H3),
        p("&ldquo;SSL&rdquo;, &ldquo;Solid State Logic&rdquo;, &ldquo;SSL "
          "360&deg;&rdquo;, &ldquo;UF8&rdquo;, and &ldquo;UC1&rdquo; are "
          "trademarks of Solid State Logic Ltd. They appear in the "
          "project documentation solely to identify the hardware and "
          "software this extension interoperates with (nominative fair "
          "use). The project is not affiliated with, endorsed by, or "
          "sponsored by SSL.", BODY),
        p("Interoperability basis", H3),
        p("Developed via independent passive observation of the USB wire "
          "protocol between legally purchased SSL UF8 / UC1 hardware and "
          "legally licensed SSL 360&deg; software, for the sole purpose "
          "of interoperating with REAPER. No SSL code, firmware, "
          "binaries, or proprietary creative content is decompiled, "
          "reproduced, or redistributed. Cited authorities: EU Software "
          "Directive 2009/24/EC Art.&nbsp;6 (interoperability exception); "
          "&sect;69e UrhG (Germany); 17 USC &sect;1201(f) (US "
          "interoperability exception).", BODY),
        p("No warranty", H3),
        p("MIT licence, with an explicit notice in the README that "
          "running third-party firmware-level communication with SSL "
          "hardware may void the user's warranty.", BODY),
    ]))

    s.append(p("7. Asks of SSL", H2))
    s.append(p("Three asks, in plain language:", BODY))
    s.append(panel([
        p("1. &mdash; Do you object to this being a public, "
          "open-source project? We'd rather hear it now than after the "
          "fact, and we're open to changing how the project is shaped.", BODY),
        p("2. &mdash; Would SSL be willing to share protocol "
          "documentation, under NDA if needed? Even partial reference "
          "would save us substantial capture-and-decode effort.", BODY),
        p("3. &mdash; If this could grow into something more formal "
          "(documented partner extension, listing alongside SSL's "
          "supported integrations), we'd be glad to talk.", BODY),
    ]))
    s.append(p(
        "Collaboration is the preferred outcome.", BODY))
    return s


# ---------------------------------------------------------------------------
# Part 2 — Manual
# ---------------------------------------------------------------------------

def manual_intro():
    s = []
    s.append(PageBreak())
    s.append(p("USER MANUAL (preview)", EYEBROW))
    s.append(p("Rea-Sixty for REAPER", H1))
    s.append(p(
        "The user-facing manual that ships with the extension. Included "
        "so SSL can see what end-users are told to do.", LEAD))
    return s


def manual_install():
    s = []
    s.append(p("8. Install", H2))

    s.append(p("Prerequisites", H3))
    for b in [
        "REAPER (any recent version).",
        "An SSL UF8, UC1, or both, connected by USB.",
        "macOS &mdash; Homebrew: <font face='Courier'>brew install libusb cmake pkg-config</font>.",
        "<b>SSL 360&deg; must be quit before REAPER starts.</b> 360&deg; holds the controller's vendor interface exclusively; Rea-Sixty cannot open it while 360&deg; is running.",
    ]:
        s.append(Paragraph(f"&bull;&nbsp;&nbsp;{b}", BULLET))

    s.append(p("Build (macOS)", H3))
    s.append(code(
        "git clone https://github.com/acklin83/reaper-uf8.git\n"
        "cd reaper-uf8/extension\n"
        "cmake -B build\n"
        "cmake --build build -j$(sysctl -n hw.ncpu)"
    ))

    s.append(p("Install into REAPER", H3))
    s.append(p("Symlink (preferred during development &mdash; rebuilds are live):", BODY))
    s.append(code(
        'ln -sf "$PWD/build/reaper_uf8.dylib" \\\n'
        '       ~/Library/Application\\ Support/REAPER/UserPlugins/reaper_uf8.dylib'
    ))
    s.append(p("Or copy the dylib into the same folder. Restart REAPER.", BODY))

    s.append(p("Windows / Linux", H3))
    s.append(p(
        "Not yet. The capture and analysis tools run on Windows today; "
        "the extension itself is macOS-only at this time.", BODY))
    return s


def manual_first_start():
    s = []
    s.append(p("9. First start", H2))
    # Verified: behavior matches code paths described in docs/install-macos.md
    # plus current Surface::Run() startup. "Awaiting Connection to SSL
    # 360°" is the UF8's idle screen text (uf8-manual-reference.md).
    for b in [
        "On load nothing visible appears inside REAPER itself; the extension is a control-surface plug-in with no menu entry.",
        "The UF8 leaves its &ldquo;Awaiting Connection to SSL 360&deg; Software&rdquo; idle screen and enters Plug-in Mixer Mode.",
        "Scribble strips show the first eight tracks: track number, name, fader dB readout, V-Pot ring (Pan by default), and the DAW track-colour bar.",
        "The 12-segment meter on each strip follows REAPER's track meter.",
        "If a UC1 is connected, its knobs follow the focused track's SSL Channel Strip&nbsp;2 / Bus Compressor&nbsp;2 / 4K&nbsp;B / 4K&nbsp;E / 4K&nbsp;G. The GR LEDs follow the plug-in's gain reduction.",
        "Triggering the REAPER action <i>Rea-Sixty: Toggle Plugin Mixer Window</i> opens the on-screen window (left-rail navigation: Mixer / Device / Bindings / Soft-Key Banks / FX Learn / Modes / Selection Sets / About).",
        "If nothing happens, open REAPER's Console (View &rarr; Console). Rea-Sixty logs failures as <font face='Courier'>reaper_uf8: &lt;reason&gt;</font>. The most common reason is <font face='Courier'>SSL360Core owns the device</font> &mdash; quit SSL 360&deg; and restart REAPER.",
    ]:
        s.append(Paragraph(f"&bull;&nbsp;&nbsp;{b}", BULLET))
    return s


def manual_controls():
    s = []
    s.append(p("10. Buttons, faders, V-Pots", H2))
    s.append(p(
        "Factory defaults. Every entry below is a registered binding "
        "and can be re-bound in the Bindings section of the Settings "
        "screen.", BODY))
    # All entries verified against Bindings.cpp factory bindings.
    s.append(grid_table(
        ["Control", "Default behaviour"],
        [
            ["Fader",
             "REAPER track volume, 16-bit. Touch detected for automation latches."],
            ["V-Pot rotate",
             "Pan by default. Flip swaps the V-Pot's target with the fader's. In SSL Strip Mode the V-Pot redirects to the focused SSL plug-in's pan."],
            ["V-Pot push",
             "Reset target to default (pan&nbsp;=&nbsp;0; FX param = plug-in default)."],
            ["SOLO / CUT / SEL",
             "Solo / Mute / Select on the strip's track. Single-press SEL is exclusive select."],
            ["BANK &lt; / BANK &gt;",
             "Shift visible 8 by 8 (within whatever the current surface filter shows)."],
            ["PAGE &lt; / PAGE &gt;",
             "Soft-key bank page navigation (V-POT / Bank&nbsp;1..5)."],
            ["LAYER 1 / 2 / 3",
             "Three bindable layers (Layer&nbsp;2 and 3 start empty by factory default)."],
            ["QUICK 1 / QUICK 2",
             "Q1 focuses the Channel-Strip domain; Q2 focuses the Bus-Comp domain. Q3 unbound by default."],
            ["Transport",
             "REAPER Play / Stop / Record / Rewind / Fast-forward."],
            ["360&deg; key",
             "Opens / closes Rea-Sixty's on-screen window (the same dockable ImGui window the toggle action targets)."],
            ["UC1 knobs",
             "Drive the focused track's Channel Strip / Bus Compressor in real time. The dedicated EQ / dynamics knobs are stably mapped; the top-centre V-Pot row is soft-mapped per focused plug-in variant."],
        ],
        col_widths=[3.8 * cm, 13.2 * cm],
    ))
    return s


def manual_layers_encoder():
    s = []
    s.append(p("11. Layers, modifiers, the Channel encoder", H2))

    s.append(p("Layers", H3))
    # Verified: Bindings.h Layer1/2/3; main.cpp layer_select_1..3 builtins.
    for b in [
        "Three layers (Layer&nbsp;1, 2, 3). Each layer holds its own set of soft-key bindings; per-strip, transport and global bindings are layer-independent.",
        "Layer&nbsp;1 ships with sensible defaults. Layers&nbsp;2 and 3 start empty so users can build from scratch.",
        "<font face='Courier'>layer_select_1/2/3</font> jumps directly; <font face='Courier'>layer_select</font> with a param cycles.",
    ]:
        s.append(Paragraph(f"&bull;&nbsp;&nbsp;{b}", BULLET))

    s.append(p("Modifiers", H3))
    # Verified: Bindings.cpp modifier matching & dispatch; main.cpp
    # mod_shift / mod_cmd / mod_ctrl builtins.
    for b in [
        "Shift / Fine and the SSL Norm / Rec / Auto row are dispatched as modifier flags. Each binding can match plain, +Shift, +Cmd, +Ctrl etc.; the matcher prefers the most specific binding present.",
        "On a button release the binding fires under whatever modifier was held at press time, so a modifier released early still produces the &ldquo;intended&rdquo; release event.",
        "LED preview: dim when the modifier-specific binding is inactive, bright when held and active. Per-binding colour overrides take precedence over the stock SSL colour.",
    ]:
        s.append(Paragraph(f"&bull;&nbsp;&nbsp;{b}", BULLET))

    s.append(p("Channel encoder modes", H3))
    # Verified: main.cpp enum EncoderMode {Nav, Nudge, Focus, Instance};
    # encoder_nav/nudge/focus/instance builtins; encoder_mode_dispatch
    # switch statement; default = Nav (track select).
    s.append(grid_table(
        ["Mode", "Behaviour"],
        [
            ["<b>Nav</b> (default)", "Track select &mdash; rotate to move the selection."],
            ["<b>Nudge</b>",         "Nudges the playhead by the REAPER nudge amount."],
            ["<b>Focus</b>",         "Mouse-scroll-wheel emulation at the cursor."],
            ["<b>Instance</b>",      "Cycles plug-in instances within the focused domain on the selected track."],
        ],
        col_widths=[3.5 * cm, 13.5 * cm],
    ))
    s.append(p(
        "The encoder push (channel-encoder press) resets the mode to "
        "Nav. Bindings <font face='Courier'>encoder_nav</font>, "
        "<font face='Courier'>encoder_nudge</font>, "
        "<font face='Courier'>encoder_focus</font>, "
        "<font face='Courier'>encoder_instance</font> set the mode "
        "directly from any button.", BODY))
    return s


def manual_filters():
    s = []
    s.append(p("12. Surface filters: Folder Mode, Show Only Selected, Selection Sets", H2))
    s.append(p(
        "These three are surface filters &mdash; they change which "
        "tracks fill the 8 strips without changing the project. They "
        "stack with the bank / channel buttons.", BODY))

    s.append(p("Folder Mode", H3))
    # Verified: main.cpp g_folderMode + folder_mode builtin +
    # I_FOLDERDEPTH parents-only filter + g_spilledParent +
    # kSelLongPressMs = 500.
    for b in [
        "Bind any button to <font face='Courier'>folder_mode</font>. Toggle on: only folder-start tracks (I_FOLDERDEPTH&nbsp;&gt;=&nbsp;1) and depth-0 children fill the strips.",
        "Long-press SEL on a folder-start track (500&nbsp;ms) spills that folder's children onto the strips immediately to its right. Long-press again to collapse.",
        "REAPER's MCP folder-collapse state is left alone &mdash; Folder Mode is a Rea-Sixty-only surface filter, the TCP view does not change.",
        "Persists across REAPER restarts via the <font face='Courier'>ReaSixty/folderMode</font> ExtState.",
    ]:
        s.append(Paragraph(f"&bull;&nbsp;&nbsp;{b}", BULLET))

    s.append(p("Show Only Selected", H3))
    # Verified: g_showOnlySelected + show_only_selected builtin.
    s.append(p(
        "Bindable filter: only currently-selected tracks fill the "
        "strips. Useful when you want to focus a few specific tracks "
        "without committing to a saved set.", BODY))

    s.append(p("Selection Sets", H3))
    # Verified: g_selsetActive, selset_recall builtin with param 1..8.
    s.append(p(
        "Eight saved slots, recalled via "
        "<font face='Courier'>selset_recall</font> with param&nbsp;1..8. "
        "Slots are GUID-keyed (track GUIDs, not indices) so reorders and "
        "deletes don't corrupt them. The Selection Sets tab of the "
        "Settings screen manages save / recall / clear.", BODY))
    return s


def manual_sendrecv():
    s = []
    s.append(p("13. Send / Receive routing", H2))
    # Verified: main.cpp Send/Receive routing comment block + send_all_N /
    # recv_all_N / send_this / recv_this builtins. Four independent state
    # pairs (V-Pot vs Fader × Send vs Receive).
    s.append(p(
        "Rea-Sixty has four independent routing states &mdash; V-Pot vs "
        "Fader, each &times; Send vs Receive. They behave consistently:", BODY))
    for b in [
        "<b>AllTracks-N</b> (param 1..8) &mdash; every visible strip shows send (or receive) index <i>N</i> for its own track. Default factory binding: the Send/Plugin button row above the V-Pots.",
        "<b>ThisTrack</b> &mdash; the 8 strips show the focused track's first 8 sends (or receives). Bound to <font face='Courier'>send_this</font> / <font face='Courier'>recv_this</font>.",
        "Within a domain (e.g. V-Pots showing sends) AllTracks-N and ThisTrack are mutually exclusive.",
        "Send and Receive on the same physical output (V-Pot vs Fader) are mutually exclusive &mdash; AllTracksReceive3 on the V-Pots cancels any active Send mode on the V-Pots.",
        "V-Pot and Fader stay independent: V-Pots can show Sends while Faders still drive track volume.",
        "LEDs, colours, names, and CUT/Mute all follow the routed entity rather than the host track while a routing mode is active.",
    ]:
        s.append(Paragraph(f"&bull;&nbsp;&nbsp;{b}", BULLET))
    return s


def manual_strip_mode():
    s = []
    s.append(p("14. SSL Strip Mode and Instance Cycle", H2))

    s.append(p("SSL Strip Mode", H3))
    # Verified: main.cpp ssl_strip_mode_toggle / ssl_strip_mode_toggle_with_gui
    # builtins; mutual-exclusion with uf8_plugin_mode_toggle.
    s.append(p(
        "Two builtins flip <font face='Courier'>g_pluginFaderMode</font>:", BODY))
    for b in [
        "<font face='Courier'>ssl_strip_mode_toggle</font> &mdash; UF8 fader and V-Pot leave DAW track volume / pan and follow the focused SSL plug-in's output level / pan instead.",
        "<font face='Courier'>ssl_strip_mode_toggle_with_gui</font> &mdash; same, plus opens / closes the focused plug-in's REAPER GUI on the main thread. Factory-bound to Shift+Plugin.",
        "Mutually exclusive with <font face='Courier'>uf8_plugin_mode_toggle</font> (UF8 Plug-in Deep Edit). Turning one on forces the other off.",
        "Persists across restarts via the <font face='Courier'>ReaSixty/pluginFaderMode</font> ExtState.",
    ]:
        s.append(Paragraph(f"&bull;&nbsp;&nbsp;{b}", BULLET))

    s.append(p("Instance Cycle", H3))
    # Verified: instance_cycle / instance_next / instance_prev builtins;
    # applyInstanceCycle_ helper; per-domain CS / BC carousel; user-mapped
    # plug-ins participate.
    for b in [
        "Channel encoder set to Instance mode (or any button bound to <font face='Courier'>instance_cycle</font>) walks plug-in instances on the focused track within the active domain.",
        "Channel-Strip domain: cycles through CS2 / 4K&nbsp;B / 4K&nbsp;E / 4K&nbsp;G / 360 Link variants present on the track.",
        "Bus-Comp domain: cycles through every Bus Comp instance on the track.",
        "User-mapped UF8-only plug-ins (entered via FX Learn) participate in the cycle alongside the stock SSL ones.",
    ]:
        s.append(Paragraph(f"&bull;&nbsp;&nbsp;{b}", BULLET))
    return s


def manual_fx_learn():
    s = []
    s.append(p("15. FX Learn", H2))
    # Verified: SettingsScreen.cpp drawFxLearn tab; UserPluginCatalog;
    # paramSnapshot persistence; fillSequentialUf8_ + "Fill sequential
    # (right)" right-click menu item.
    s.append(p(
        "FX Learn lets any VST / JS / AU plug-in be controlled from the "
        "UF8 (and selectively from the UC1's soft-mapped knob row). "
        "Open <b>Settings &rarr; FX Learn</b>:", BODY))
    for b in [
        "Add an FX from the catalogue (or pick one from the focused track). A schematic UF8 mockup appears with bindable cells.",
        "Arm a cell, then touch the parameter inside the plug-in's REAPER window. The binding records the plug-in's GUID and the parameter index; FX-slot reorders don't break it.",
        "<b>Fill sequential (right)</b>: right-click a learned cell whose parameter name contains a digit (e.g. &ldquo;Band 1 Freq&rdquo;). The remaining cells to the right auto-fill with the next consecutive parameters of the same plug-in.",
        "Each entry carries a label, colour, brightness, and a UF8-only / UC1-shared mode flag.",
        "Param values are formatted via <font face='Courier'>TrackFX_GetFormattedParamValue</font> &mdash; the plug-in's own formatter is used (&ldquo;&minus;3.5&nbsp;dB&rdquo;, &ldquo;42&nbsp;%&rdquo;); no per-plug-in formatter code in Rea-Sixty.",
        "Param snapshots are persisted so a learned slot stays editable when the live FX instance isn't present.",
    ]:
        s.append(Paragraph(f"&bull;&nbsp;&nbsp;{b}", BULLET))
    return s


def manual_bindings_file():
    s = []
    s.append(p("16. Bindings file &amp; CSI import", H2))
    # Verified: SettingsScreen.cpp drawBindings + drawCsiImportSection;
    # bindings.json read/write at ~/.../REAPER/rea_sixty/bindings.json.
    s.append(p(
        "Bindings are stored in a JSON file under REAPER's resource "
        "directory. The Settings screen edits this file; Learn-Mode "
        "edits it; the extension watches the file's mtime and reloads "
        "on change with no REAPER restart needed.", BODY))

    s.append(p("Location", H3))
    s.append(grid_table(
        ["OS", "Path"],
        [
            ["macOS",
             "<font face='Courier'>~/Library/Application Support/REAPER/rea_sixty/bindings.json</font>"],
            ["Windows (future)",
             "<font face='Courier'>%APPDATA%\\REAPER\\rea_sixty\\bindings.json</font>"],
            ["Linux (future)",
             "<font face='Courier'>~/.config/REAPER/rea_sixty/bindings.json</font>"],
        ],
        col_widths=[3.5 * cm, 13.5 * cm],
    ))

    s.append(p("Binding types", H3))
    # Verified: Bindings.h Type enum / DescBuilder + main.cpp builtin
    # registrations (65 total).
    for b in [
        "<b>reaper_action</b> &mdash; dispatches a REAPER action (numeric ID or named SWS / ReaPack / user-macro).",
        "<b>builtin</b> &mdash; surface behaviour: bank nav, layer / V-Pot mode, flip, folder/show-only-selected, send/receive routing, instance cycle, SSL Strip Mode toggle, modifier flags, encoder modes, brightness, etc. (65 builtins total).",
        "<b>track_target</b> &mdash; per-strip standard target (select, mute, solo, rec_arm, &hellip;).",
        "<b>fx_param</b> &mdash; bind a V-Pot or soft-key to an FX parameter; GUID-keyed.",
        "Multi-action chains: any slot can carry up to N steps with optional inter-step delays; single-step chains run synchronously, multi-step ones are queued and drained from the main-thread timer.",
    ]:
        s.append(Paragraph(f"&bull;&nbsp;&nbsp;{b}", BULLET))

    s.append(p("CSI import", H3))
    # Verified: SettingsScreen.cpp drawCsiImportSection reading Home.zon
    # via CsiImport.cpp.
    s.append(p(
        "Users migrating from a CSI surface configuration can point "
        "Rea-Sixty at the CSI Surface directory (defaults to "
        "<font face='Courier'>~/Library/Application&nbsp;Support/REAPER/CSI/Surfaces/SSLUF8</font>). "
        "The Bindings section reads CSI's <font face='Courier'>Home.zon</font> "
        "and previews the resulting bindings before they're committed.", BODY))
    return s


def manual_trouble_uninstall():
    s = []
    s.append(p("17. Troubleshooting", H2))
    s.append(grid_table(
        ["Symptom", "Likely cause / fix"],
        [
            ["UF8 stays on &ldquo;Awaiting Connection to SSL 360&deg; Software&rdquo;.",
             "SSL 360&deg; is still running. Quit it, restart REAPER."],
            ["Console: <font face='Courier'>SSL360Core owns the device</font>.",
             "Same cause &mdash; the vendor interface is exclusively claimed."],
            ["Scribble strips light up but stay blank.",
             "Init replayed but the 150&nbsp;ms Plug-in-Mixer keepalive isn't reaching the device. Check the Console for libusb errors, reseat the USB cable, restart REAPER."],
            ["Track colour on the strip is &ldquo;close but not exact&rdquo;.",
             "REAPER's colour is being matched by chromaticity to the closest of the UF8's 11 known palette entries (16 total, 5 unmapped). The match is deliberately conservative."],
            ["UC1 GR LEDs sit at zero with audio playing.",
             "The focused track has no SSL Channel Strip / Bus Comp on it, or its plug-in does not expose the PreSonus <font face='Courier'>GainReduction_dB</font> named-config-parm. Re-focus the track in question."],
            ["The Mixer pane in the on-screen window shows a &ldquo;scaffold&rdquo; message.",
             "It is a scaffold today &mdash; the channel-strip + Bus-Comp rendering is on the roadmap. Settings (Device, Bindings, Soft-Key Banks, FX Learn, Modes, Selection Sets, About) all work in the same window."],
        ],
        col_widths=[6.5 * cm, 10.5 * cm],
    ))
    s.append(p("Issues that aren't on the list:", BODY))
    s.append(p('<font face="Courier" color="#009FD5">github.com/acklin83/reaper-uf8/issues</font>', BODY_L))

    s.append(p("18. Uninstall", H2))
    s.append(p("Delete the dylib (or its symlink) and restart REAPER:", BODY))
    s.append(code(
        "rm ~/Library/Application\\ Support/REAPER/UserPlugins/reaper_uf8.dylib"
    ))
    s.append(p(
        "User data &mdash; bindings, soft-key banks, FX-Learn catalogue "
        "&mdash; lives in "
        "<font face='Courier'>~/Library/Application Support/REAPER/rea_sixty/</font>. "
        "Removing the dylib leaves that folder intact. Delete the "
        "folder by hand to wipe every Rea-Sixty trace.", BODY))

    s.append(Spacer(1, 0.6 * cm))
    s.append(panel([
        p("End of preview", ParagraphStyle("end", parent=H3, spaceBefore=0)),
        p("Source of truth: "
          "<font face='Courier' color='#009FD5'>"
          "github.com/acklin83/reaper-uf8</font>. "
          "Questions, objections, or an opening to collaborate are "
          "welcome &mdash; see section&nbsp;7.", BODY),
    ]))
    return s


def main():
    doc = build_doc()
    story = []
    story += cover_page()
    story += toc_page()
    story += part_project()
    story += part_what_ships()
    story += part_construction()
    story += part_legal_asks()
    story += manual_intro()
    story += manual_install()
    story += manual_first_start()
    story += manual_controls()
    story += manual_layers_encoder()
    story += manual_filters()
    story += manual_sendrecv()
    story += manual_strip_mode()
    story += manual_fx_learn()
    story += manual_bindings_file()
    story += manual_trouble_uninstall()
    doc.build(story)
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
