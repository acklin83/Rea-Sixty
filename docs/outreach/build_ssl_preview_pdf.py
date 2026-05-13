#!/usr/bin/env python3
"""
Builds rea-sixty-ssl-preview.pdf — a project explainer + user manual
preview document intended to share with SSL.

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
ACCENT = colors.HexColor("#009FD5")  # REAPER default blue, also a UF8 palette entry
RULE = colors.HexColor("#d6dadf")
PANEL = colors.HexColor("#f4f6f8")

base = getSampleStyleSheet()

H1 = ParagraphStyle(
    "H1", parent=base["Heading1"],
    fontName="Helvetica-Bold", fontSize=22, leading=26,
    textColor=INK, spaceBefore=0, spaceAfter=10,
)
H2 = ParagraphStyle(
    "H2", parent=base["Heading2"],
    fontName="Helvetica-Bold", fontSize=14, leading=18,
    textColor=INK, spaceBefore=14, spaceAfter=6,
)
H3 = ParagraphStyle(
    "H3", parent=base["Heading3"],
    fontName="Helvetica-Bold", fontSize=11, leading=14,
    textColor=INK, spaceBefore=10, spaceAfter=3,
)
BODY = ParagraphStyle(
    "Body", parent=base["BodyText"],
    fontName="Helvetica", fontSize=10, leading=14,
    textColor=INK, alignment=TA_JUSTIFY, spaceAfter=6,
)
BODY_L = ParagraphStyle(
    "BodyL", parent=BODY, alignment=TA_LEFT,
)
SMALL = ParagraphStyle(
    "Small", parent=BODY, fontSize=8.5, leading=11, textColor=MUTED,
)
LEAD = ParagraphStyle(
    "Lead", parent=BODY, fontSize=11, leading=15, spaceAfter=10,
)
MONO = ParagraphStyle(
    "Mono", parent=BODY, fontName="Courier", fontSize=9, leading=12,
    textColor=INK, alignment=TA_LEFT, spaceAfter=6,
)
BULLET = ParagraphStyle(
    "Bullet", parent=BODY, leftIndent=14, bulletIndent=2,
    spaceAfter=2, alignment=TA_LEFT,
)
EYEBROW = ParagraphStyle(
    "Eyebrow", parent=BODY, fontSize=8.5, leading=11,
    textColor=ACCENT, alignment=TA_LEFT,
    fontName="Helvetica-Bold", spaceAfter=2,
)


# ---------- page frame -----------------------------------------------------

def _page_chrome(canvas, doc):
    canvas.saveState()
    w, h = A4
    # header rule
    canvas.setStrokeColor(RULE)
    canvas.setLineWidth(0.4)
    canvas.line(2.0 * cm, h - 1.6 * cm, w - 2.0 * cm, h - 1.6 * cm)
    # header text
    canvas.setFont("Helvetica", 8)
    canvas.setFillColor(MUTED)
    canvas.drawString(2.0 * cm, h - 1.35 * cm,
                      "Rea-Sixty  ·  SSL preview")
    canvas.drawRightString(w - 2.0 * cm, h - 1.35 * cm,
                           "Independent open-source REAPER extension for UF8 / UC1")
    # footer
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
    # accent bar
    canvas.setFillColor(ACCENT)
    canvas.rect(0, h - 1.0 * cm, w, 1.0 * cm, fill=1, stroke=0)
    # bottom note
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
    frame = Frame(
        doc.leftMargin, doc.bottomMargin,
        doc.width, doc.height, id="body",
    )
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
    # escape minimal HTML entities
    safe = (text.replace("&", "&amp;")
                .replace("<", "&lt;")
                .replace(">", "&gt;"))
    safe = safe.replace("\n", "<br/>")
    return Paragraph(f'<font face="Courier" size="9">{safe}</font>',
                     ParagraphStyle("c", parent=MONO, backColor=PANEL,
                                    borderPadding=6, leftIndent=0,
                                    spaceBefore=2, spaceAfter=10))


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


# ---------- content --------------------------------------------------------

def cover_page():
    s = []
    s.append(Spacer(1, 5.5 * cm))
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
          "project is, how it works, what it does today, and what it asks "
          "of SSL — nothing more.", BODY),
    ], bg=PANEL))
    s.append(Spacer(1, 1.5 * cm))
    s.append(kv_table([
        ("Project", "Rea-Sixty (repository: <i>reaper-uf8</i>)"),
        ("Type", "REAPER extension &mdash; C++, csurf_inst, libusb"),
        ("Status", "Working, near feature-complete on macOS (Phase 1 + Phase 2)"),
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
        ("1.", "Executive summary"),
        ("2.", "Why this exists"),
        ("3.", "How it works"),
        ("4.", "What works today"),
        ("5.", "Legal posture"),
        ("6.", "Asks of SSL"),
        ("PART 2", "USER MANUAL (preview)"),
        ("7.", "Installation"),
        ("8.", "First run &mdash; what to expect"),
        ("9.", "Day-to-day operation"),
        ("10.","UF8 button &amp; LED reference"),
        ("11.","UC1 reference"),
        ("12.","Bindings &amp; Learn Mode"),
        ("13.","Plug-in Mixer window &amp; Settings"),
        ("14.","Troubleshooting"),
        ("15.","Uninstall"),
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


def part_one_project():
    s = []

    # 1. Executive summary
    s.append(p("1. Executive summary", H2))
    s.append(p(
        "Rea-Sixty is an independent open-source REAPER extension that "
        "drives the SSL UF8 and UC1 directly from REAPER. It opens the "
        "controllers' vendor-USB interface, replays the host-side init "
        "sequence, and emits the same Plug-in-Mixer-mode frames that SSL "
        "360&deg; produces &mdash; reading track state from REAPER's C API "
        "(name, colour, fader, pan, sends, automation, focused FX, gain "
        "reduction). The result: REAPER users get the colour-aware "
        "scribble-strip experience natively, without an SSL VST3 on every "
        "track, and the SSL Bus Compressor's GR meter on the UC1 follows "
        "audio-driven gain reduction on the focused track.", LEAD))
    s.append(p(
        "The extension is a single shared library (<font face='Courier'>"
        "reaper_uf8.dylib</font> on macOS), MIT-licensed. It does not "
        "redistribute, decompile, or reproduce any SSL software, plug-in, "
        "firmware, or trademark; all interoperability is established by "
        "passive USB observation of legally purchased SSL hardware running "
        "legally licensed SSL 360&deg;.", BODY))

    # 2. Why
    s.append(p("2. Why this exists", H2))
    s.append(p(
        "The SSL UF8's scribble strips can render DAW track colours, but "
        "only when the controller is in Plug-in Mixer Mode &mdash; which in "
        "turn requires an SSL VST3 (Channel Strip 2 / 4K B / E / G, 360 "
        "Link, or Bus Compressor) on every track that should appear. For "
        "100+ track REAPER sessions this is impractical, and it is the most "
        "frequent feature request we hear from REAPER users with UF8s.", BODY))
    s.append(p(
        "MCU and HUI cannot carry colour at all, so any &ldquo;just talk "
        "MCU&rdquo; workaround is dead on arrival for the colour use case. "
        "And SSL 360&deg; holds the UF8's vendor-USB interface with an "
        "exclusive claim, so a quiet side-car next to 360&deg; is not "
        "possible either. The only technically honest path was to "
        "re-implement 360&deg;'s host-side responsibilities for REAPER "
        "specifically &mdash; which is exactly what Rea-Sixty does.", BODY))

    # 3. How
    s.append(p("3. How it works", H2))
    s.append(p(
        "Rea-Sixty registers with REAPER as a <font face='Courier'>"
        "csurf_inst</font> (<font face='Courier'>"
        "IReaperControlSurface</font>). On load it enumerates SSL devices "
        "via libusb (VID <font face='Courier'>0x31E9</font>, PIDs "
        "<font face='Courier'>0x0021</font> UF8 / <font face='Courier'>"
        "0x0023</font> UC1), claims the vendor interface, and replays the "
        "init sequence we observed 360&deg; sending at startup. It then "
        "runs the keepalive cycle the controllers require to stay in "
        "Plug-in Mixer mode, and pushes frames driven by REAPER state on a "
        "single timer.", BODY))
    s.append(p(
        "Inputs from the controller (button events, fader motion + touch, "
        "V-Pot deltas, UC1 knob deltas) are parsed from vendor-USB packets "
        "and dispatched through the REAPER C API "
        "(<font face='Courier'>CSurf_OnVolumeChange</font>, "
        "<font face='Courier'>CSurf_OnMuteChange</font>, "
        "<font face='Courier'>Main_OnCommand</font>, "
        "<font face='Courier'>TrackFX_SetParam</font>, &hellip;). No "
        "virtual MIDI port, no Control Surface Integrator, no MCU bridge.", BODY))
    s.append(panel([
        p("Topology", ParagraphStyle("ph", parent=H3, spaceBefore=0)),
        code("REAPER  <->  Rea-Sixty extension (csurf_inst)  <->  UF8 / UC1 (vendor-USB, libusb)"),
        p("Specifically <i>not</i>:", BODY),
        code("REAPER  <->  CSI  <->  virtual MCU MIDI  <->  bridge  <->  UF8"),
    ]))

    # The decoded surface
    s.append(p("Decoded protocol surface", H3))
    s.append(p("The following are implemented and verified on the hardware:", BODY))
    for b in [
        "Frame format and checksum on both endpoints (<font face='Courier'>FF &lt;cmd&gt; &lt;len&gt; &lt;data&gt; &lt;chk&gt;</font>).",
        "UF8 vendor init sequence + Plug-in-Mixer keepalive pair (13&nbsp;B / 64&nbsp;B cycling, ~150&nbsp;ms).",
        "Per-strip TFT colour command and the 16-entry palette (12 of 16 indices mapped; remainder snap nearest-RGB).",
        "Per-strip SEL / MUTE / SOLO LED frames; transport LEDs; 12-segment metering frame.",
        "Display zones: scribble text, parameter label, value line, channel-strip type, O/PdB readout, channel-number, color bar.",
        "UF8 inbound button-ID map (per-strip + globals, full table on page 9).",
        "UC1 inbound button + knob ID map; outbound LED frames per cell; GR meter (16-bit BE dB&times;10), VU bands, display zones.",
    ]:
        s.append(Paragraph(f"&bull;&nbsp;&nbsp;{b}", BULLET))

    return s


def part_one_status():
    s = []
    # 4. What works today
    s.append(p("4. What works today", H2))
    s.append(p(
        "Status as of the document date. macOS is the lead platform; "
        "Windows and Linux ports are tracked but not yet shipped.", BODY))

    rows = [
        ["UF8 standalone replacement",        "Shipping",   "Phase 1 complete &mdash; no SSL 360&deg; required for UF8."],
        ["DAW-layer scribble strip colours",  "Shipping",   "Polled from <font face='Courier'>GetTrackColor()</font>; pushed on bank shifts. Not offered by SSL 360&deg;."],
        ["Full 16-bit fader resolution",      "Shipping",   "Direct <font face='Courier'>CSurf_OnVolumeChange</font>; no MCU 14-bit lossy stage."],
        ["UC1 parameter mirror (focused FX)", "Shipping",   "Knobs follow Channel Strip 2 / Bus Comp 2 on the focused track; values mirrored on the UC1 displays."],
        ["UC1 GR display (audio-driven)",     "Shipping",   "Driven by <font face='Courier'>TrackFX_GetNamedConfigParm(\"GainReduction_dB\", &hellip;)</font> &mdash; the PreSonus VST3 GR extension that REAPER exposes for the SSL plug-ins."],
        ["Bindings (per-strip / transport / global / soft-keys / Learn)", "Shipping", "12 user soft-key banks + the 6 SSL stock banks; modifier combos."],
        ["Generic FX-parameter learn",        "Shipping",   "GUID-keyed; survives FX-slot reorders. Works for any VST/JS/AU param."],
        ["On-screen Plug-in Mixer + Settings", "Shipping",  "Vendored Dear ImGui in a dockable SWELL window; picks up the user's REAPER theme."],
        ["Folder mode + selection sets + send/receive layers", "Landing", "Phase 2.5 in progress."],
        ["Windows / Linux ports",             "Planned",    "Phase 4. Capture workflow already runs on Windows."],
    ]
    s.append(grid_table(
        ["Capability", "State", "Notes"],
        rows,
        col_widths=[5.5 * cm, 2.2 * cm, 9.3 * cm],
    ))

    s.append(p("Known limitations", H3))
    for b in [
        "SSL 360&deg; must be quit before REAPER starts &mdash; the exclusive USB claim is unchanged.",
        "Some palette indices (likely greys) still snap to nearest match instead of being decoded.",
        "Coexistence with 360&deg; is not pursued; a complete replacement is the explicit goal.",
        "Hardware behaviour under unforeseen frames is not part of SSL's documented public API; we ship with a clear &ldquo;use at your own risk &mdash; may void warranty&rdquo; notice (see Legal).",
    ]:
        s.append(Paragraph(f"&bull;&nbsp;&nbsp;{b}", BULLET))

    # 5. Legal
    s.append(p("5. Legal posture", H2))
    s.append(p(
        "We took care to be on solid legal footing before publishing.", BODY))
    s.append(panel([
        p("Trademarks", H3),
        p("&ldquo;SSL&rdquo;, &ldquo;Solid State Logic&rdquo;, &ldquo;SSL "
          "360&deg;&rdquo;, &ldquo;UF8&rdquo;, and &ldquo;UC1&rdquo; are "
          "trademarks of Solid State Logic Ltd. They are used in the "
          "project documentation solely to identify the hardware and "
          "software this extension interoperates with (nominative fair "
          "use). The project is not affiliated with, endorsed by, or "
          "sponsored by SSL.", BODY),
        p("Interoperability basis", H3),
        p("Developed via independent passive observation of the USB wire "
          "protocol between legally purchased SSL UF8 / UC1 hardware and "
          "legally licensed SSL 360&deg; software, for the sole purpose of "
          "achieving interoperability with REAPER. No SSL code, firmware, "
          "binaries, or proprietary creative content is decompiled, "
          "reproduced, or redistributed. Cited authorities: EU Software "
          "Directive 2009/24/EC Art. 6 (interoperability exception); "
          "&sect;69e UrhG (Germany); 17 USC &sect;1201(f) (US "
          "interoperability exception).", BODY),
        p("No warranty", H3),
        p("The project ships under MIT with an explicit &ldquo;use at your "
          "own risk&rdquo; notice that running third-party firmware-level "
          "communication with SSL hardware may void the user's warranty.", BODY),
    ]))

    # 6. Asks
    s.append(p("6. Asks of SSL", H2))
    s.append(p(
        "We would much rather collaborate than work in parallel. "
        "Concretely:", BODY))
    s.append(panel([
        p("1. &mdash; Any objection to this being a public, open-source "
          "project? We would rather hear it now than after the fact, and "
          "we are open to changing how the project is shaped.", BODY),
        p("2. &mdash; Would SSL be willing to share protocol "
          "documentation, under NDA if needed? Even partial reference "
          "would save us substantial capture-and-decode effort and produce "
          "a more robust result for shared customers.", BODY),
        p("3. &mdash; If this could grow into something more formal &mdash; "
          "a documented partner extension, a listing alongside SSL's "
          "supported integrations &mdash; we'd be glad to talk.", BODY),
    ]))
    s.append(p(
        "We did this because we love the hardware and were curious what "
        "was possible. Collaboration is the preferred outcome.", BODY))
    return s


def part_two_manual_intro():
    s = []
    s.append(PageBreak())
    s.append(p("USER MANUAL (preview)", EYEBROW))
    s.append(p("Rea-Sixty for REAPER", H1))
    s.append(p(
        "The following sections are the user-facing manual that ships with "
        "the extension. Included here as a preview so SSL can see exactly "
        "what end-users are told to do.", LEAD))
    return s


def part_two_install():
    s = []
    s.append(p("7. Installation", H2))

    s.append(p("Prerequisites", H3))
    for b in [
        "REAPER (any recent version, 6.x / 7.x).",
        "An SSL UF8, UC1, or both, connected via USB.",
        "macOS: Homebrew &mdash; <font face='Courier'>brew install libusb cmake pkg-config</font>.",
        "<b>SSL 360&deg; must be quit before REAPER starts.</b> 360&deg; holds the controller's vendor interface exclusively; Rea-Sixty cannot open it while 360&deg; is running.",
    ]:
        s.append(Paragraph(f"&bull;&nbsp;&nbsp;{b}", BULLET))

    s.append(p("Build (macOS)", H3))
    s.append(code(
        "git clone https://github.com/acklin83/reaper-uf8.git\n"
        "cd reaper-uf8/extension\n"
        "cmake -B build -G \"Unix Makefiles\"\n"
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
    return s


def part_two_first_run():
    s = []
    s.append(p("8. First run &mdash; what to expect", H2))
    for b in [
        "On load, nothing is visible inside REAPER itself. The extension is a control-surface plug-in; there is no menu entry to open.",
        "The UF8 wakes from the &ldquo;Awaiting Connection to SSL 360&deg; Software&rdquo; idle screen and enters Plug-in Mixer Mode.",
        "Scribble strips show the first eight tracks: track number, name, fader dB readout, V-Pot ring (Pan by default), and the DAW track colour bar.",
        "The 12-segment meter on each strip follows REAPER's track meter.",
        "If a UC1 is connected, its knobs follow the focused track's SSL Channel Strip 2 / Bus Compressor 2 (whichever is present); the GR meter follows the plug-in's gain reduction.",
        "If something fails, REAPER's Console (View &rarr; Console) shows <font face='Courier'>reaper_uf8: &lt;reason&gt;</font>. The most common reason is <font face='Courier'>SSL360Core owns the device</font>.",
    ]:
        s.append(Paragraph(f"&bull;&nbsp;&nbsp;{b}", BULLET))

    s.append(p("9. Day-to-day operation", H2))
    s.append(p("The control surface behaves the way a UF8/UC1 owner already expects, with a few REAPER-native specifics:", BODY))

    s.append(grid_table(
        ["Control", "Default behaviour"],
        [
            ["Fader",                    "REAPER track volume, full 16-bit. Touch is detected; automation latches honour REAPER's automation modes."],
            ["V-Pot rotate",             "Pan by default. <i>Flip</i> swaps with the fader. <i>Plugin</i> targets the focused FX param."],
            ["V-Pot push",               "Reset target to default (pan = 0; param = default value)."],
            ["SOLO / CUT / SEL",         "Solo / Mute / Select on the strip's track. SEL behaves as &ldquo;exclusive select&rdquo; on a single press (no MCU double-press needed)."],
            ["BANK &lt; / &gt;",         "Shift visible 8 by 8 tracks. CHANNEL &lt; / &gt; shifts by 1."],
            ["LAYER 1 / 2 / 3",          "Cycle the user-defined layer (DAW / FX / Sends, configurable)."],
            ["QUICK 1 / 2 / 3",          "Plug-in Mixer assignments: 1 = Channel Strip, 2 = Bus Comp, 3 = I/O meter toggle (matches the SSL stock layout)."],
            ["Soft keys (above strips)", "Per-strip parameter-page key in PM mode; bind to any REAPER action via Learn Mode."],
            ["Transport",                "Standard REAPER Play / Stop / Record / Rewind / Fast-forward."],
            ["CHANNEL encoder",          "Standard = bank &plusmn;1; NAV = scrub; NUDGE; FOCUS = mouse-wheel emulation. Press-and-hold = Cursor-Transport mode."],
            ["360&deg; key",             "Opens the on-screen Plug-in Mixer + Settings window (Rea-Sixty's, not SSL 360&deg;'s)."],
            ["UC1 knobs",                "Drive the focused track's Channel Strip / Bus Compressor in real time. <i>Fine</i> halves the step size."],
            ["UC1 GR meter",             "Audio-driven gain reduction for the focused track's SSL compressor."],
        ],
        col_widths=[3.8 * cm, 13.2 * cm],
    ))
    return s


def part_two_button_ref():
    s = []
    s.append(p("10. UF8 button &amp; LED reference", H2))
    s.append(p(
        "The IDs below are the values the UF8 emits on its vendor-USB IN "
        "endpoint &mdash; documented here so users writing their own "
        "ReaScript automations can identify which physical control fired. "
        "Per-strip indices are <font face='Courier'>N = 0..7</font>, left "
        "to right.", BODY))

    s.append(p("Per-strip buttons", H3))
    s.append(grid_table(
        ["Button", "Formula", "Strip&nbsp;0", "Strip&nbsp;7"],
        [
            ["V-Pot push",                          "0x08 + N",       "0x08", "0x0F"],
            ["Top soft-key (above scribble)",       "0x18 + N",       "0x18", "0x1F"],
            ["SOLO",                                "0x20 + 3&middot;N", "0x20", "0x35"],
            ["CUT (Mute)",                          "0x21 + 3&middot;N", "0x21", "0x36"],
            ["SEL (Select)",                        "0x22 + 3&middot;N", "0x22", "0x37"],
        ],
        col_widths=[6.5 * cm, 4.0 * cm, 3.25 * cm, 3.25 * cm],
    ))

    s.append(p("Global buttons (selection)", H3))
    s.append(grid_table(
        ["ID", "Name", "ID", "Name"],
        [
            ["0x40", "Layer 1",            "0x41", "Layer 2"],
            ["0x42", "Layer 3",            "0x43", "Quick 1 (Channel Strip)"],
            ["0x44", "Quick 2 (Bus Comp)", "0x45", "Quick 3 (I/O meter)"],
            ["0x46", "360&deg; / Settings","0x50", "Plugin"],
            ["0x51", "Channel",            "0x52", "Page &larr;"],
            ["0x53", "Page &rarr;",        "0x54", "Flip"],
            ["0x58", "Automation Off",     "0x59", "Read"],
            ["0x5A", "Write",              "0x5B", "Trim"],
            ["0x5C", "Latch",              "0x5D", "Touch"],
            ["0x6E", "PAN",                "0x6F", "Fine / Shift"],
            ["0x70", "Norm / Clear",       "0x71", "Rec / All"],
            ["0x72", "Auto / Zero",        "0x76", "Channel encoder push"],
            ["0x78", "Bank &larr;",        "0x79", "Bank &rarr;"],
        ],
        col_widths=[2.0 * cm, 6.5 * cm, 2.0 * cm, 6.5 * cm],
    ))

    s.append(p("Outbound LED / colour", H3))
    s.append(grid_table(
        ["Function", "Frame"],
        [
            ["Per-strip TFT colour",               "<font face='Courier'>FF 66 09 18 &lt;8 palette indices&gt; &lt;chk&gt;</font>"],
            ["Per-strip SEL / MUTE / SOLO LED",    "<font face='Courier'>FF 3B 03 &lt;id&gt; 00 &lt;state&gt; &lt;chk&gt;</font>"],
            ["12-segment track meter",             "<font face='Courier'>FF 38 04 &hellip; / FF 39 04 &hellip;</font>"],
            ["Layer / page (also PM keepalive)",   "<font face='Courier'>FF 1B 01 &lt;XX&gt; &lt;chk&gt;</font>"],
        ],
        col_widths=[6.5 * cm, 10.5 * cm],
    ))
    return s


def part_two_uc1():
    s = []
    s.append(p("11. UC1 reference", H2))
    s.append(p(
        "UC1 button IDs are stable across plug-in contexts (Channel Strip "
        "vs Bus Comp). Knob IDs in the dedicated EQ / Dynamics zone always "
        "map to their named function. The top-centre V-Pot row is "
        "soft-mapped per focused plug-in.", BODY))

    s.append(p("Buttons", H3))
    s.append(grid_table(
        ["ID", "Button", "ID", "Button"],
        [
            ["0x08", "HF Bell",           "0x09", "EQ Type"],
            ["0x0A", "EQ In",             "0x0B", "LF Bell"],
            ["0x0C", "Bus Comp IN",       "0x14", "Fast Attack (Comp)"],
            ["0x15", "Peak",              "0x16", "Dyn In"],
            ["0x17", "Expand",            "0x18", "Fast Attack (Gate)"],
            ["0x19", "Polarity",          "0x1A", "S/C Listen"],
            ["0x1B", "Solo Clear",        "0x1C", "Solo"],
            ["0x1D", "Cut",               "0x1E", "Channel IN"],
            ["0x1F", "Fine",              "",     ""],
        ],
        col_widths=[2.0 * cm, 6.5 * cm, 2.0 * cm, 6.5 * cm],
    ))

    s.append(p("Channel-strip knobs (always CS params)", H3))
    s.append(grid_table(
        ["ID", "Knob", "ID", "Knob"],
        [
            ["0x00", "LPF freq",          "0x07", "LMF Gain"],
            ["0x01", "HPF freq",          "0x08", "LMF Frequency"],
            ["0x02", "HF Gain",           "0x09", "LMF Q"],
            ["0x03", "HF Frequency",      "0x0A", "LF Frequency"],
            ["0x04", "HMF Gain",          "0x0B", "LF Gain"],
            ["0x05", "HMF Frequency",     "0x17", "Gate Release"],
            ["0x06", "HMF Q",             "0x18", "Gate Hold"],
            ["0x19", "Gate Threshold",    "0x1A", "Gate Range"],
            ["0x1B", "Comp Release",      "0x1C", "Comp Threshold"],
            ["0x1D", "Comp Ratio",        "",     ""],
        ],
        col_widths=[2.0 * cm, 6.5 * cm, 2.0 * cm, 6.5 * cm],
    ))

    s.append(p("Outbound (selected)", H3))
    s.append(grid_table(
        ["Function", "Frame"],
        [
            ["GR meter",                 "<font face='Courier'>FF 5B 02 &lt;BE-16 dB&times;10&gt; &lt;chk&gt;</font>"],
            ["VU meter",                 "<font face='Courier'>FF 13 04 01 &lt;level&gt; 01 &lt;in/out&gt;</font>"],
            ["Per-button LED",           "<font face='Courier'>FF 13 04 &lt;bank&gt; &lt;cell&gt; 01 &lt;state&gt;</font>"],
            ["Display zone text",        "<font face='Courier'>FF 66 &lt;len&gt; &lt;zone&gt; &lt;ascii&hellip;&gt; &lt;chk&gt;</font>"],
            ["Keepalive (required)",     "<font face='Courier'>FF 1B 01 &lt;counter&gt; &lt;chk&gt;</font>  (~1 Hz)"],
        ],
        col_widths=[5.5 * cm, 11.5 * cm],
    ))
    return s


def part_two_bindings():
    s = []
    s.append(p("12. Bindings &amp; Learn Mode", H2))
    s.append(p(
        "Mappings live in a single JSON file. The on-screen Settings "
        "screen and the Learn Mode workflow both edit it; the extension "
        "watches it on disk and reloads on change &mdash; no REAPER "
        "restart needed.", BODY))

    s.append(p("Config file location", H3))
    s.append(grid_table(
        ["OS", "Path"],
        [
            ["macOS",   "<font face='Courier'>~/Library/Application Support/REAPER/rea_sixty/bindings.json</font>"],
            ["Windows", "<font face='Courier'>%APPDATA%\\REAPER\\rea_sixty\\bindings.json</font>"],
            ["Linux",   "<font face='Courier'>~/.config/REAPER/rea_sixty/bindings.json</font>"],
        ],
        col_widths=[2.5 * cm, 14.5 * cm],
    ))

    s.append(p("Binding types", H3))
    for b in [
        "<b>reaper_action</b> &mdash; dispatches a REAPER action (numeric ID or named SWS / ReaPack command).",
        "<b>builtin</b> &mdash; surface behaviour (bank navigation, layer cycle, V-Pot mode, flip, mute/solo clear, &hellip;).",
        "<b>track_target</b> &mdash; per-strip standard target (select, mute, solo, rec_arm, phase, monitor, fx_bypass, automation_mode).",
        "<b>fx_param</b> &mdash; bind a V-Pot or soft-key to an FX parameter, GUID-keyed so FX-slot reorders don't break the mapping.",
    ]:
        s.append(Paragraph(f"&bull;&nbsp;&nbsp;{b}", BULLET))

    s.append(p("Learn Mode", H3))
    for b in [
        "Open the Settings screen, click <b>Learn</b>.",
        "Press the UF8 / UC1 control you want to bind. The extension reports the captured control back to the UI.",
        "Trigger the REAPER action (or move the FX parameter) you want to bind to it.",
        "The binding is written to <font face='Courier'>bindings.json</font> and the extension reloads it.",
        "Right-click a learned slot for <b>Fill sequential (right)</b> &mdash; auto-fills the strips to the right with consecutive parameters of the same FX.",
        "ESC at any time cancels Learn Mode.",
    ]:
        s.append(Paragraph(f"&bull;&nbsp;&nbsp;{b}", BULLET))

    s.append(p("13. Plug-in Mixer window &amp; Settings", H2))
    s.append(p(
        "The on-screen Plug-in Mixer mirrors the eight currently-banked "
        "tracks &mdash; track name, colour, fader, pan, focused FX "
        "parameter &mdash; in a dockable window inside REAPER. It uses a "
        "vendored Dear ImGui inside a SWELL host window, and picks up the "
        "user's REAPER theme automatically (light or dark).", BODY))
    s.append(p(
        "The Settings screen lives in the same window. It exposes "
        "controller brightness, sleep timeout, default V-Pot mode per "
        "layer, the soft-key bank editor, and the Learn-Mode workflow. "
        "All settings persist to the same JSON file the extension reads "
        "at startup.", BODY))
    return s


def part_two_trouble():
    s = []
    s.append(p("14. Troubleshooting", H2))
    s.append(grid_table(
        ["Symptom", "Cause / fix"],
        [
            ["UF8 stays on the &ldquo;Awaiting Connection to SSL 360&deg;&rdquo; screen.",
             "SSL 360&deg; is still running. Quit it, then restart REAPER."],
            ["REAPER Console: <font face='Courier'>SSL360Core owns the device</font>.",
             "Same as above &mdash; the vendor interface is exclusively claimed."],
            ["Scribble strips light up but stay blank.",
             "Init replayed but Plug-in Mixer keepalive lost. Check the Console for libusb timeout errors and reseat the USB cable."],
            ["Track colour on the strip is &ldquo;close but not exact&rdquo;.",
             "REAPER colour is being snapped to the nearest 360&deg; palette entry. Some palette indices are still unmapped; the snap is by design."],
            ["UC1 GR meter sits at zero with audio playing.",
             "The focused track has no SSL Bus Comp / Channel Strip, or the plug-in's GR readout extension is unavailable in this REAPER build."],
            ["Fader feels less responsive than under SSL 360&deg;.",
             "Should not happen &mdash; we use full 16-bit. If it does, please open a GitHub issue with a session description."],
            ["UF8 LCDs show no colour bar despite seeing the strips.",
             "The colour command sits in PM mode only. Confirm the layer is not set to Plug-in Mixer Off."],
        ],
        col_widths=[6.5 * cm, 10.5 * cm],
    ))
    s.append(p("Anything not on the list goes to the issue tracker:", BODY))
    s.append(p('<font face="Courier" color="#009FD5">github.com/acklin83/reaper-uf8/issues</font>', BODY_L))

    s.append(p("15. Uninstall", H2))
    s.append(p("Remove the symlink (or copy) from REAPER's UserPlugins folder and restart REAPER:", BODY))
    s.append(code(
        "rm ~/Library/Application\\ Support/REAPER/UserPlugins/reaper_uf8.dylib"
    ))
    s.append(p(
        "The bindings JSON is left in place so re-installing later "
        "preserves the user's mappings. Delete the <font face='Courier'>"
        "rea_sixty</font> folder under REAPER's resource directory to "
        "remove it as well.", BODY))

    s.append(Spacer(1, 1.0 * cm))
    s.append(panel([
        p("End of preview", ParagraphStyle("end", parent=H3, spaceBefore=0)),
        p("This document is a snapshot for SSL of the project as it stands "
          "today. The living source of truth is the GitHub repository: "
          "<font face='Courier' color='#009FD5'>github.com/acklin83/reaper-uf8</font>.", BODY),
        p("Questions, objections, or an opening to collaborate &mdash; all "
          "very welcome. See section 6 for the asks; the outreach email "
          "draft is in <font face='Courier'>docs/outreach/</font> in the "
          "repository.", BODY),
    ]))
    return s


def main():
    doc = build_doc()
    story = []
    story += cover_page()
    story += toc_page()
    story += part_one_project()
    story += part_one_status()
    story += part_two_manual_intro()
    story += part_two_install()
    story += part_two_first_run()
    story += part_two_button_ref()
    story += part_two_uc1()
    story += part_two_bindings()
    story += part_two_trouble()
    doc.build(story)
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
