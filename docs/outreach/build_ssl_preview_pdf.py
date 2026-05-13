#!/usr/bin/env python3
"""
Builds rea-sixty-ssl-preview.pdf — a project explainer + user manual
preview document intended to share with SSL.

Rewritten 2026-05-13 against origin/main. Replaces the earlier draft;
all content reflects what actually ships today.

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

# ---------- styles ---------------------------------------------------------

INK = colors.HexColor("#111419")
MUTED = colors.HexColor("#5a6270")
ACCENT = colors.HexColor("#009FD5")   # REAPER default blue, also UF8 palette 0x05
OK = colors.HexColor("#1f8a4c")
WAIT = colors.HexColor("#b87a00")
RULE = colors.HexColor("#d6dadf")
PANEL = colors.HexColor("#f4f6f8")
DEEP = colors.HexColor("#0d2436")

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
SMALL = ParagraphStyle("Small", parent=BODY, fontSize=8.5, leading=11, textColor=MUTED)
LEAD = ParagraphStyle("Lead", parent=BODY, fontSize=11, leading=15, spaceAfter=10)
MONO = ParagraphStyle("Mono", parent=BODY, fontName="Courier", fontSize=9,
                      leading=12, textColor=INK, alignment=TA_LEFT, spaceAfter=6)
BULLET = ParagraphStyle("Bullet", parent=BODY, leftIndent=14, bulletIndent=2,
                        spaceAfter=2, alignment=TA_LEFT)
EYEBROW = ParagraphStyle("Eyebrow", parent=BODY, fontSize=8.5, leading=11,
                         textColor=ACCENT, alignment=TA_LEFT,
                         fontName="Helvetica-Bold", spaceAfter=2)


# ---------- page chrome ----------------------------------------------------

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
                           "Independent open-source REAPER extension for UF8 / UC1")
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


# ---------- helpers --------------------------------------------------------

def p(text, style=BODY):
    return Paragraph(text, style)


def bullets(items, style=BULLET):
    return [Paragraph(f"&bull;&nbsp;&nbsp;{t}", style) for t in items]


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


def badge(text, color=OK):
    return Paragraph(
        f'<font face="Helvetica-Bold" size="8" color="white">'
        f'&nbsp;{text}&nbsp;</font>',
        ParagraphStyle("badge", parent=BODY, backColor=color,
                       alignment=TA_LEFT, leading=11,
                       borderPadding=2, leftIndent=0))


# ---------- content --------------------------------------------------------

def cover_page():
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
          "any formal correspondence. The aim is transparency: what the "
          "project is, how it works, what ships today, and what it asks "
          "of SSL &mdash; nothing more.", BODY),
    ], bg=PANEL))
    s.append(Spacer(1, 1.2 * cm))
    s.append(kv_table([
        ("Project", "Rea-Sixty (repository: <i>reaper-uf8</i>)"),
        ("Type", "REAPER extension &mdash; C++, csurf_inst, libusb, Dear ImGui"),
        ("Status",
         "macOS: feature-complete for Phase 1 &amp; 2; Plug-in Mixer + "
         "Settings shipped (Phase 2.6 / 2.7); FX-Learn production-ready."),
        ("Hardware", "SSL UF8 (PID 0x0021) and SSL UC1 (PID 0x0023)"),
        ("Source", '<font color="#009FD5">github.com/acklin83/reaper-uf8</font>'),
        ("License", "MIT &mdash; original code only, no SSL material redistributed"),
        ("Document date", "2026-05-13"),
    ]))
    s.append(NextPageTemplate("body"))
    s.append(PageBreak())
    return s


def toc_page():
    s = []
    s.append(p("Contents", H1))
    s.append(Spacer(1, 4 * mm))
    items = [
        ("PART 1", "PROJECT"),
        ("1.",  "Executive summary"),
        ("2.",  "Why this exists"),
        ("3.",  "How it works"),
        ("4.",  "Feature matrix"),
        ("5.",  "Legal posture"),
        ("6.",  "Asks of SSL"),
        ("PART 2", "USER MANUAL (preview)"),
        ("7.",  "Installation"),
        ("8.",  "First run &mdash; what to expect"),
        ("9.",  "Day-to-day operation"),
        ("10.", "Layers, modifiers, and the Channel encoder"),
        ("11.", "SSL Strip Mode and Instance Cycle"),
        ("12.", "FX-Learn and the Plug-in Mixer window"),
        ("13.", "Settings screen"),
        ("14.", "Bindings file &amp; CSI import"),
        ("15.", "Troubleshooting"),
        ("16.", "Uninstall"),
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


# ----- Part 1: Project -----

def part_project():
    s = []

    # 1. Executive summary
    s.append(p("1. Executive summary", H2))
    s.append(p(
        "Rea-Sixty is an independent open-source REAPER extension that "
        "drives the SSL UF8 and UC1 directly. It opens the controllers' "
        "vendor-USB interface, replays the host-side init sequence we "
        "observed SSL 360&deg; sending, and runs the keepalive cycle the "
        "Plug-in Mixer rendering path requires. From there everything "
        "&mdash; track name, colour, fader, pan, sends, automation, "
        "focused FX parameters, audio-driven gain reduction &mdash; "
        "comes from REAPER's C API.", LEAD))
    s.append(p(
        "On macOS the project is feature-complete for its first two "
        "phases. Phase&nbsp;1 (UF8 standalone replacement) and "
        "Phase&nbsp;2 (UC1 parameter mirror + GR pipeline) ship today. "
        "Phase&nbsp;2.6 (an in-REAPER Plug-in Mixer window) and "
        "Phase&nbsp;2.7 (an in-app Settings screen) are also shipping "
        "&mdash; both render in a vendored Dear ImGui inside a dockable "
        "SWELL window and follow the user's REAPER theme.", BODY))
    s.append(p(
        "The extension ships as a single shared library "
        "(<font face='Courier'>reaper_uf8.dylib</font> on macOS) under "
        "the MIT licence. It does not redistribute, decompile, or "
        "reproduce SSL software, plug-in, firmware, or trademarks. All "
        "interoperability rests on passive USB observation of legally "
        "purchased SSL hardware running legally licensed SSL 360&deg;.", BODY))

    # 2. Why
    s.append(p("2. Why this exists", H2))
    s.append(p(
        "The UF8's scribble strips can render DAW track colours, but "
        "only when the controller is in Plug-in Mixer Mode, which in turn "
        "requires an SSL VST3 (Channel Strip 2 / 4K B / E / G, 360 Link, "
        "or Bus Compressor) on every track that should appear. For "
        "100+ track REAPER sessions this is impractical, and it is the "
        "single most common complaint we hear from REAPER users with a "
        "UF8 on the desk. MCU and HUI carry no colour at all, so any "
        "&ldquo;just talk MCU&rdquo; workaround is dead on arrival for "
        "the colour use case.", BODY))
    s.append(p(
        "SSL 360&deg; also holds the UF8's vendor-USB interface with an "
        "exclusive claim, so coexisting next to it is not possible. The "
        "only honest path was to re-implement 360&deg;'s host-side "
        "responsibilities for REAPER specifically &mdash; which is what "
        "Rea-Sixty does.", BODY))

    # 3. How
    s.append(p("3. How it works", H2))
    s.append(p(
        "Rea-Sixty registers with REAPER as a "
        "<font face='Courier'>csurf_inst</font>. On load it enumerates "
        "SSL devices over libusb (VID&nbsp;0x31E9; PID&nbsp;0x0021 UF8, "
        "PID&nbsp;0x0023 UC1), claims the vendor interface, and replays "
        "the init sequence. From there a single ~30&nbsp;Hz timer drives "
        "everything: inbound vendor-USB frames are parsed into press / "
        "release / fader / V-Pot / knob events and dispatched through "
        "the REAPER C API; outbound state is pushed to the controllers "
        "as scribble text, colour bars, LED frames, fader motor, GR and "
        "VU meters.", BODY))
    s.append(panel([
        p("Topology", ParagraphStyle("ph", parent=H3, spaceBefore=0)),
        code("REAPER  <->  Rea-Sixty extension (csurf_inst)  <->  UF8 / UC1 (vendor-USB, libusb)"),
        p("Specifically <i>not</i>:", BODY),
        code("REAPER  <->  CSI  <->  virtual MCU MIDI  <->  bridge  <->  UF8"),
    ]))

    s.append(p("Decoded protocol surface", H3))
    for b in [
        "UF8 + UC1 frame format and checksum "
        "(<font face='Courier'>FF &lt;cmd&gt; &lt;len&gt; &lt;data&gt; &lt;chk&gt;</font>).",
        "UF8 vendor init sequence plus the 13&nbsp;B / 64&nbsp;B Plug-in-Mixer keepalive pair (~150&nbsp;ms cadence).",
        "Per-strip TFT colour command, the 16-entry palette, and a chromaticity-quantising matcher for arbitrary REAPER colours.",
        "All Plug-in Mixer scribble zones: track name, parameter label, value line, channel-strip type, O/PdB fader readout, channel number, colour bar.",
        "Per-strip SEL / MUTE / SOLO LED frames; transport LEDs; 12-segment metering frame.",
        "UF8 inbound button-ID map (per-strip + globals); fader 16-bit position + touch.",
        "UC1 inbound button + knob IDs (stable across plug-in contexts); GR meter "
        "(16-bit BE dB&times;10, ladder calibrated to the SSL plug-in's native 0–3–6–10–14–20&nbsp;dB scale); VU bars; display zones.",
        "UF8 + UC1 keepalive cadence (loss = device times out / reverts to idle screen).",
    ]:
        s.append(Paragraph(f"&bull;&nbsp;&nbsp;{b}", BULLET))

    s.append(p("Key components in the source tree", H3))
    s.append(grid_table(
        ["Component", "Owns"],
        [
            ["<font face='Courier'>Protocol</font>, <font face='Courier'>UC1Protocol</font>", "Frame parser / builder; checksum; init bytes; keepalive."],
            ["<font face='Courier'>UF8Device</font>, <font face='Courier'>UC1Device</font>", "libusb I/O; per-device init replay; outbound push pipeline."],
            ["<font face='Courier'>ColorSync</font>",          "Polls <font face='Courier'>GetTrackColor()</font>; per-bank colour push; chromaticity-aware palette match."],
            ["<font face='Courier'>UC1Surface</font>, <font face='Courier'>PluginMap</font>, <font face='Courier'>UserPluginCatalog</font>", "Focused-FX parameter mirror; built-in SSL plug-in tables and runtime user maps."],
            ["<font face='Courier'>FocusedParam</font>",        "Last-touched / focused FX tracking; instance bookkeeping."],
            ["<font face='Courier'>Bindings</font>",            "Press/release dispatch, modifier-aware lookup, multi-action chains, Learn mode."],
            ["<font face='Courier'>MixerWindow</font>, <font face='Courier'>MixerLayout</font>", "Dockable Dear ImGui Plug-in Mixer (8 channel-strip columns + Bus-Comp rack)."],
            ["<font face='Courier'>SettingsScreen</font>",      "Tabbed Settings UI inside MixerWindow."],
            ["<font face='Courier'>ThemeBridge</font>",         "Polls REAPER theme; live-updates ImGui colours."],
            ["<font face='Courier'>VirtualNotch</font>",        "EQ-gain soft-touch (magnet-only behaviour for 0 dB)."],
        ],
        col_widths=[5.5 * cm, 11.5 * cm],
    ))
    return s


def part_feature_matrix():
    s = []
    s.append(p("4. Feature matrix", H2))
    s.append(p(
        "Status as of the document date. All entries are macOS; "
        "Windows and Linux are tracked but not yet shipped.", BODY))

    rows = [
        ["UF8 standalone replacement (Phase&nbsp;1)",
         "Shipping",
         "No SSL 360&deg; required for UF8; init, keepalive, scribble, colour, LEDs, meters, fader motor, buttons all native."],
        ["DAW-layer scribble-strip colours",
         "Shipping",
         "Polled from REAPER on bank shift / theme change. Chromaticity-quantising palette match (incl. dark-colour HSV polar matching)."],
        ["Full 16-bit fader resolution",
         "Shipping",
         "Direct <font face='Courier'>CSurf_OnVolumeChange</font>; no MCU 14-bit lossy stage."],
        ["UC1 parameter mirror (Phase&nbsp;2)",
         "Shipping",
         "Knobs follow the focused track's SSL Channel Strip&nbsp;2 / Bus Compressor&nbsp;2 / 4K&nbsp;B&nbsp;/&nbsp;E&nbsp;/&nbsp;G in real time. Values mirrored back to UC1 displays."],
        ["UC1 + UF8 GR meter (audio-driven)",
         "Shipping",
         "Driven by <font face='Courier'>TrackFX_GetNamedConfigParm(\"GainReduction_dB\", &hellip;)</font>. UC1 and UF8 share a 30-sub-step ladder calibrated to the SSL plug-in's 3 / 6 / 10 / 14 / 20&nbsp;dB scale."],
        ["SSL Strip Mode",
         "Shipping",
         "Shift+Plugin toggles fader / V-Pot from DAW track to the focused SSL plug-in's output level / pan. Variant name (CS2 / 4K&nbsp;B / &hellip;) shown in the scribble strip."],
        ["Instance cycle",
         "Shipping",
         "Channel encoder + modifier walks instances within a domain (Channel Strip / Bus Comp). Includes UF8-only user-mapped plug-ins."],
        ["Bindings + Learn mode",
         "Shipping",
         "Per-strip, transport, global, soft-keys per layer. Modifier combos; multi-action chains; per-binding label / colour / brightness overrides; release-time fallback to press-time binding."],
        ["12 user Soft-Key Banks + 6 SSL stock banks",
         "Shipping",
         "Editable in the Settings screen. Bank tabs and slot headers use stable IDs so ImGui input fields keep focus across keystrokes."],
        ["FX-Learn (any VST / JS / AU)",
         "Shipping",
         "GUID-keyed bindings survive FX-slot reorders. Param-snapshot persistence so a learned slot stays editable without the live FX. &ldquo;Fill sequential&rdquo; / &ldquo;Fill sequential (right)&rdquo; for batch assignment. UF8-only or UC1-shared mode flag per entry."],
        ["EXT_FUNCS V-Pot push builtins",
         "Shipping",
         "Auto-Makeup, Width Mode, Width Freq, Filters In &mdash; built-in extensions for UC1 Comp / Gate page that SSL 360&deg; reaches via a hidden right-click."],
        ["Virtual notch (EQ gains, pan)",
         "Shipping",
         "Magnet-only soft-touch around 0&nbsp;dB / centre; passes through on continued motion."],
        ["Plug-in Mixer window (Phase&nbsp;2.6)",
         "Shipping",
         "Dockable Dear ImGui window, 8 channel-strip columns + Bus-Comp rack. Reads track colours, focused-FX params, audio meters live."],
        ["Settings screen (Phase&nbsp;2.7)",
         "Shipping",
         "Tabbed UI: Device, Bindings, Soft-Key Banks, FX-Learn, Modes, Selection Sets, About. CSI-import preview for migration."],
        ["Selection Sets (8 slots)",
         "Partial",
         "UI shell and data model present (GUID-keyed, project-local). End-to-end recall ergonomics still under iteration."],
        ["Folder Mode (Phase&nbsp;2.5a)",
         "Backlog",
         "Designed; not yet implemented."],
        ["Send / Receive layers",
         "Partial / Backlog",
         "UF8 Sends focus-variant is in; full Sends / Receives layer flow still on the backlog."],
        ["Windows / Linux ports (Phase&nbsp;4)",
         "Planned",
         "Capture workflow already runs on Windows. Native build not started."],
    ]
    s.append(grid_table(
        ["Capability", "State", "Notes"],
        rows,
        col_widths=[5.5 * cm, 2.0 * cm, 9.5 * cm],
    ))
    return s


def part_legal_asks():
    s = []
    s.append(p("5. Legal posture", H2))
    s.append(p("The project was published only after deliberately establishing each of the points below.", BODY))
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
          "of achieving interoperability with REAPER. No SSL code, "
          "firmware, binaries, or proprietary creative content is "
          "decompiled, reproduced, or redistributed. Cited authorities: "
          "EU Software Directive 2009/24/EC Art.&nbsp;6 (interoperability "
          "exception); &sect;69e UrhG (Germany); 17 USC &sect;1201(f) "
          "(US interoperability exception).", BODY),
        p("No warranty", H3),
        p("MIT licence, with an explicit &ldquo;use at your own "
          "risk&rdquo; notice in the README that running third-party "
          "firmware-level communication with SSL hardware may void the "
          "user's warranty with SSL.", BODY),
    ]))

    s.append(p("6. Asks of SSL", H2))
    s.append(p("We would much rather collaborate than work in parallel. Concretely:", BODY))
    s.append(panel([
        p("1. &mdash; Any objection to this being a public, open-source "
          "project? We would rather hear it now than after the fact, and "
          "we are open to changing how the project is shaped.", BODY),
        p("2. &mdash; Would SSL be willing to share protocol "
          "documentation, under NDA if needed? Even partial reference "
          "would save us substantial capture-and-decode effort and "
          "produce a more robust result for shared customers.", BODY),
        p("3. &mdash; If this could grow into something more formal "
          "&mdash; a documented partner extension, a listing alongside "
          "SSL's supported integrations &mdash; we'd be glad to talk.", BODY),
    ]))
    s.append(p(
        "We did this because we love the hardware and were curious what "
        "was possible. Collaboration is the preferred outcome.", BODY))
    return s


# ----- Part 2: Manual -----

def manual_intro():
    s = []
    s.append(PageBreak())
    s.append(p("USER MANUAL (preview)", EYEBROW))
    s.append(p("Rea-Sixty for REAPER", H1))
    s.append(p(
        "The following sections are the user-facing manual that ships "
        "with the extension. Included here so SSL can see exactly what "
        "end-users are told to do.", LEAD))
    return s


def manual_install():
    s = []
    s.append(p("7. Installation", H2))

    s.append(p("Prerequisites", H3))
    for b in [
        "REAPER 6.x or 7.x.",
        "An SSL UF8, UC1, or both, connected via USB.",
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
    s.append(p("Or copy:", BODY))
    s.append(code(
        'cp build/reaper_uf8.dylib \\\n'
        '   ~/Library/Application\\ Support/REAPER/UserPlugins/'
    ))
    s.append(p("Restart REAPER.", BODY))
    s.append(p(
        "<b>Windows / Linux:</b> not shipping yet (Phase&nbsp;4). The "
        "capture and analysis tooling runs on Windows today, but the "
        "extension itself is macOS-only at this time.", BODY))
    return s


def manual_first_run():
    s = []
    s.append(p("8. First run — what to expect", H2))
    for b in [
        "On load, nothing is visible inside REAPER itself. The extension is a control-surface plug-in; there is no menu entry to open.",
        "The UF8 wakes from the &ldquo;Awaiting Connection to SSL 360&deg; Software&rdquo; idle screen and enters Plug-in Mixer Mode.",
        "Scribble strips show the first eight tracks: track number, name, fader dB readout, V-Pot ring (Pan by default), and the DAW track-colour bar.",
        "The 12-segment meter on each strip follows REAPER's track meter.",
        "If a UC1 is connected, its knobs follow the focused track's SSL Channel Strip / Bus Compressor variant (Channel Strip&nbsp;2 / Bus Comp&nbsp;2 / 4K&nbsp;B / 4K&nbsp;E / 4K&nbsp;G). The GR meter follows the plug-in's gain reduction.",
        "Triggering the action <font face='Courier'>Rea-Sixty: Toggle Plug-in Mixer Window</font> opens the on-screen mixer + settings window. Dock it anywhere in REAPER's layout.",
        "If something fails, REAPER's Console (View &rarr; Console) shows <font face='Courier'>reaper_uf8: &lt;reason&gt;</font>. The most common reason is <font face='Courier'>SSL360Core owns the device</font> &mdash; see Troubleshooting.",
    ]:
        s.append(Paragraph(f"&bull;&nbsp;&nbsp;{b}", BULLET))

    s.append(p("9. Day-to-day operation", H2))
    s.append(p("The controllers behave the way their owners already expect, with a few REAPER-native specifics:", BODY))
    s.append(grid_table(
        ["Control", "Default behaviour"],
        [
            ["Fader",
             "REAPER track volume, full 16-bit. Touch is detected; automation latches honour REAPER's automation mode."],
            ["V-Pot rotate",
             "Pan by default. Flip swaps with the fader. In SSL Strip Mode the V-Pot retargets the focused plug-in's pan."],
            ["V-Pot push",
             "Reset target to default (pan = 0; param = the plug-in's default value)."],
            ["SOLO / CUT / SEL",
             "Solo / Mute / Select on the strip's track. Single-press SEL is &ldquo;exclusive select&rdquo; (no MCU double-press)."],
            ["BANK &lt; / &gt;",
             "Shift visible 8 by 8 tracks. CHANNEL &lt; / &gt; shifts by 1."],
            ["LAYER 1 / 2 / 3",
             "Cycle the user-defined layer (e.g. DAW / FX / Sends; configurable)."],
            ["QUICK 1 / 2 / 3",
             "Plug-in Mixer assignments: 1&nbsp;=&nbsp;Channel Strip, 2&nbsp;=&nbsp;Bus Comp, 3&nbsp;=&nbsp;I/O meter toggle (matches the SSL stock layout)."],
            ["Soft keys (above strips)",
             "Per-strip parameter-page key in PM mode; bind to any REAPER action via Learn Mode."],
            ["Transport",
             "REAPER Play / Stop / Record / Rewind / Fast-forward."],
            ["360&deg; key",
             "Opens Rea-Sixty's on-screen Plug-in Mixer + Settings window (not SSL 360&deg;)."],
            ["UC1 knobs",
             "Drive the focused track's Channel Strip / Bus Compressor in real time. <i>Fine</i> halves the step size."],
            ["UC1 GR meter",
             "Audio-driven gain reduction for the focused track's SSL compressor."],
        ],
        col_widths=[3.8 * cm, 13.2 * cm],
    ))
    return s


def manual_layers():
    s = []
    s.append(p("10. Layers, modifiers, and the Channel encoder", H2))
    s.append(p(
        "Three building blocks shape what every UF8 button does at any "
        "moment: the active layer, the held modifier, and the Channel "
        "encoder mode.", BODY))

    s.append(p("Layers", H3))
    for b in [
        "Layers select the meaning of soft-keys, V-Pot rotation, and any binding marked as layer-aware.",
        "Stock layers ship with sensible defaults; user layers are added in the Settings &rarr; Bindings tab.",
        "Per-strip controls (Select / Mute / Solo / Rec-Arm) and the Transport row stay layer-independent.",
    ]:
        s.append(Paragraph(f"&bull;&nbsp;&nbsp;{b}", BULLET))

    s.append(p("Modifiers", H3))
    for b in [
        "<b>Shift</b> (Fine) and the SSL stock Norm / Rec / Auto modifier row are dispatched as press/hold/release flags.",
        "Each binding can match plain, Shift, Shift+Modifier, etc. The matcher prefers the most specific binding present.",
        "On release, the binding fires under the modifier that was held at press time &mdash; so a modifier released early still produces the &ldquo;intended&rdquo; release event.",
        "LEDs preview the result: dim when the modifier-specific binding is inactive, bright when held active. Per-binding colour overrides win over the stock SSL colour.",
    ]:
        s.append(Paragraph(f"&bull;&nbsp;&nbsp;{b}", BULLET))

    s.append(p("Channel encoder modes", H3))
    s.append(grid_table(
        ["Mode", "Behaviour"],
        [
            ["<b>Standard</b>",       "Bank &plusmn;1 track."],
            ["<b>NAV</b>",            "Transport scrub."],
            ["<b>NUDGE</b>",          "Nudge selection / edit-cursor by the REAPER nudge amount."],
            ["<b>FOCUS</b>",          "Mouse-wheel emulation at the cursor."],
            ["<b>Instance cycle</b>", "With the appropriate modifier held, walks plug-in instances on the focused track within the current domain (Channel Strip / Bus Comp). Picks up UF8-only user-mapped plug-ins as well."],
            ["<b>Press-and-hold</b>", "Toggles Cursor-Transport mode &mdash; the cursor / mode keys become Stop / Play / Rewind / Fast-forward / Record."],
        ],
        col_widths=[3.5 * cm, 13.5 * cm],
    ))
    return s


def manual_strip_mode():
    s = []
    s.append(p("11. SSL Strip Mode and Instance Cycle", H2))

    s.append(p("SSL Strip Mode", H3))
    s.append(p(
        "<b>Shift + Plugin</b> toggles SSL Strip Mode. In this mode the "
        "UF8 fader and V-Pot leave DAW-track volume / pan behind and "
        "track the focused SSL plug-in instead: the fader drives the "
        "plug-in's output level, the V-Pot drives the plug-in's pan, "
        "and the scribble strip shows the variant name "
        "(<font face='Courier'>CS2</font>, <font face='Courier'>4K&nbsp;B</font>, "
        "<font face='Courier'>4K&nbsp;E</font>, <font face='Courier'>4K&nbsp;G</font>, "
        "<font face='Courier'>Bus Comp</font>) in the track row.", BODY))
    s.append(p(
        "Strip Mode also rewires the modifier row to the plug-in's "
        "&ldquo;hidden&rdquo; controls (Auto-Makeup, Filters In, Width "
        "Mode, Width Frequency) &mdash; the parameters SSL 360&deg; "
        "exposes via right-click menus on the UC1 are first-class V-Pot "
        "pushes here.", BODY))

    s.append(p("Instance cycle", H3))
    for b in [
        "Holding the appropriate modifier and turning the Channel encoder walks instances of the focused-domain plug-in on the selected track.",
        "Channel-Strip domain: cycles through CS2 / 4K B / 4K E / 4K G / 360-Link variants present on the track.",
        "Bus-Comp domain: cycles through every Bus Comp instance on the track.",
        "User-mapped UF8-only plug-ins participate &mdash; the encoder picks them up alongside the stock SSL ones.",
        "The current anchor track is preserved when the BC carousel chases a focus change &mdash; you don't lose the instance you were editing because focus shifted.",
    ]:
        s.append(Paragraph(f"&bull;&nbsp;&nbsp;{b}", BULLET))
    return s


def manual_fx_learn_mixer():
    s = []
    s.append(p("12. FX-Learn and the Plug-in Mixer window", H2))

    s.append(p("Plug-in Mixer window (Phase 2.6)", H3))
    s.append(p(
        "Triggering the REAPER action <font face='Courier'>Rea-Sixty: "
        "Toggle Plug-in Mixer Window</font> opens a dockable Dear ImGui "
        "window that mirrors the eight currently-banked tracks &mdash; "
        "track name, colour, fader, pan, focused-FX parameter &mdash; "
        "alongside a Bus-Comp rack. The window reads REAPER's theme via "
        "the SDK and re-tints itself in real time when you switch "
        "between light and dark themes.", BODY))

    s.append(p("FX-Learn (Phase 2.5d)", H3))
    s.append(p(
        "FX-Learn is the user-programmable Plug-in Mixer for "
        "third-party plug-ins. Open <b>Settings &rarr; FX-Learn</b>:", BODY))
    for b in [
        "Pick an FX on the focused track from the catalogue list; the schematic UI shows a UF8 or UC1 mockup with bindable cells.",
        "Arm a cell &rarr; touch a parameter in the plug-in window &rarr; the binding is recorded with the plug-in's GUID and the parameter index. GUID-keyed means FX-slot reorders never break it.",
        "<b>Fill sequential</b> / <b>Fill sequential (right)</b> on a learned slot's right-click menu auto-assigns consecutive parameters of the same plug-in to the cells to its right &mdash; useful for plug-ins whose params come in a sensible order.",
        "Each entry has a UF8-only / shared mode flag, label, colour, and brightness override.",
        "Parameter values are formatted via <font face='Courier'>TrackFX_GetFormattedParamValue</font> &mdash; the plug-in's own formatter (&ldquo;−3.5 dB&rdquo;, &ldquo;42 %&rdquo;), no per-plug-in formatter code needed.",
        "Snapshot persistence means a learned slot is editable even when no live FX instance is present &mdash; the schematic remembers the last seen state.",
    ]:
        s.append(Paragraph(f"&bull;&nbsp;&nbsp;{b}", BULLET))
    return s


def manual_settings():
    s = []
    s.append(p("13. Settings screen", H2))
    s.append(p(
        "The Settings screen lives inside the same window as the "
        "Plug-in Mixer (Phase&nbsp;2.7). Left-rail navigation, ImGui "
        "tabs; everything writes through to the bindings JSON file the "
        "extension watches at runtime.", BODY))
    s.append(grid_table(
        ["Tab", "What it does"],
        [
            ["<b>Device</b>",
             "LED brightness, scribble brightness, meter ballistic, SEL-follows-colour toggle, list of connected devices."],
            ["<b>Bindings</b>",
             "Per-button mapper for Per-strip / Transport / Global / Soft-keys. Learn workflow. CSI-import preview for users migrating from a CSI zone file."],
            ["<b>Soft-Key Banks</b>",
             "Editor for 12 user banks plus the 6 SSL stock banks (CS&nbsp;6 + BC&nbsp;2). Per-slot label, colour, and brightness; stable ImGui IDs keep the cursor inside the input fields across keystrokes."],
            ["<b>FX-Learn</b>",
             "User-plug-in catalogue editor. UC1 / UF8 / shared mockups. Param-snapshot rendering and persistence."],
            ["<b>Modes</b>",
             "Folder Mode, Selection Sets, Send / Receive layer toggles. Folder Mode and full Receives are still on the backlog at the time of this document."],
            ["<b>Selection Sets</b>",
             "Eight project-local slots, GUID-keyed so reorders and deletes don't corrupt them."],
            ["<b>About</b>",
             "Version, build hash, log file links, links to the GitHub issues tracker."],
        ],
        col_widths=[3.5 * cm, 13.5 * cm],
    ))
    return s


def manual_bindings_file():
    s = []
    s.append(p("14. Bindings file &amp; CSI import", H2))
    s.append(p(
        "Bindings live in a single JSON file. The Settings screen and "
        "the Learn-Mode workflow both edit it; the extension watches "
        "its mtime and reloads on change &mdash; no REAPER restart "
        "needed. Schema-version bumps trigger a migration on load; the "
        "previous version is archived as "
        "<font face='Courier'>bindings.json.v&lt;N&gt;.bak</font>.", BODY))

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
    for b in [
        "<b>reaper_action</b> &mdash; dispatches a REAPER action (numeric ID or named SWS / ReaPack / user-macro).",
        "<b>builtin</b> &mdash; surface behaviour (bank nav, layer cycle, V-Pot mode, flip, mute/solo clear, instance cycle, Strip-Mode toggle, &hellip;).",
        "<b>track_target</b> &mdash; per-strip standard target (select, mute, solo, rec_arm, phase, monitor, fx_bypass, automation_mode).",
        "<b>fx_param</b> &mdash; bind a V-Pot or soft-key to an FX parameter; GUID-keyed.",
        "<b>chain</b> &mdash; multi-action chain: a single press fires several bindings in order. Useful for &ldquo;solo this track + scroll to it + open its FX&rdquo; macros.",
    ]:
        s.append(Paragraph(f"&bull;&nbsp;&nbsp;{b}", BULLET))

    s.append(p("CSI import", H3))
    s.append(p(
        "Users migrating from a CSI-based setup can drop their CSI zone "
        "file into the import dialog. The Bindings tab shows a preview "
        "table of how each CSI line will land in Rea-Sixty's "
        "<font face='Courier'>bindings.json</font>; the user can accept "
        "or hand-edit before commit. Defensive logging guards "
        "malformed zone files.", BODY))
    return s


def manual_trouble_uninstall():
    s = []
    s.append(p("15. Troubleshooting", H2))
    s.append(grid_table(
        ["Symptom", "Cause / fix"],
        [
            ["UF8 stays on the &ldquo;Awaiting Connection to SSL 360&deg;&rdquo; screen.",
             "SSL 360&deg; is still running. Quit it, then restart REAPER."],
            ["REAPER Console: <font face='Courier'>SSL360Core owns the device</font>.",
             "Same root cause &mdash; the vendor interface is exclusively claimed."],
            ["Scribble strips light up but stay blank.",
             "Init replayed but the Plug-in-Mixer keepalive was lost. Check the Console for libusb timeout errors, reseat the USB cable, restart REAPER."],
            ["Track colour on the strip is &ldquo;close but not exact&rdquo;.",
             "The REAPER colour is being snapped to the nearest 360&deg; palette entry. The matcher prefers chromaticity over RGB distance, but the palette is still 16 entries deep."],
            ["UC1 GR meter sits at zero with audio playing.",
             "The focused track has no SSL Bus Comp / Channel Strip with the PreSonus GR extension, or the focus has slipped to a different track. Re-focus and verify in <b>Settings &rarr; Device</b>."],
            ["Shift / Fine appears &ldquo;stuck&rdquo;.",
             "Fixed in 2026-05-09 build. If you still see it, please report with the steps that produced it."],
            ["ImGui input fields lose focus after every keystroke.",
             "Fixed for Soft-Key Banks in 2026-05-13. If you see it elsewhere, please report &mdash; it's almost certainly the same ImGui-ID hashing bug we've been hunting."],
            ["FX-Learn binding disappears after rearranging FX slots.",
             "Should not happen &mdash; bindings are GUID-keyed. If it does, the FX was removed and re-added with a fresh GUID; re-learn the slot."],
        ],
        col_widths=[6.5 * cm, 10.5 * cm],
    ))
    s.append(p("Anything not on the list goes to the issue tracker:", BODY))
    s.append(p('<font face="Courier" color="#009FD5">github.com/acklin83/reaper-uf8/issues</font>', BODY_L))

    s.append(p("16. Uninstall", H2))
    s.append(p("Remove the dylib (or symlink) from REAPER's UserPlugins folder and restart REAPER:", BODY))
    s.append(code(
        "rm ~/Library/Application\\ Support/REAPER/UserPlugins/reaper_uf8.dylib"
    ))
    s.append(p(
        "The <font face='Courier'>rea_sixty</font> folder under "
        "REAPER's resource directory is left in place so re-installing "
        "preserves the user's bindings, soft-key banks, and FX-Learn "
        "catalogue. Delete that folder to remove every trace.", BODY))

    s.append(Spacer(1, 0.7 * cm))
    s.append(panel([
        p("End of preview", ParagraphStyle("end", parent=H3, spaceBefore=0)),
        p("This document is a snapshot of the project for SSL as of "
          "2026-05-13. The living source of truth is the GitHub "
          "repository: <font face='Courier' color='#009FD5'>"
          "github.com/acklin83/reaper-uf8</font>.", BODY),
        p("Questions, objections, or an opening to collaborate &mdash; "
          "all very welcome. See section&nbsp;6 for the asks; the "
          "outreach email draft is in <font face='Courier'>"
          "docs/outreach/</font> in the repository.", BODY),
    ]))
    return s


def main():
    doc = build_doc()
    story = []
    story += cover_page()
    story += toc_page()
    story += part_project()
    story += part_feature_matrix()
    story += part_legal_asks()
    story += manual_intro()
    story += manual_install()
    story += manual_first_run()
    story += manual_layers()
    story += manual_strip_mode()
    story += manual_fx_learn_mixer()
    story += manual_settings()
    story += manual_bindings_file()
    story += manual_trouble_uninstall()
    doc.build(story)
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
