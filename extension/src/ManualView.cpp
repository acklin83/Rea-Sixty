#include "ManualView.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// ReaImGui bindings — declarations only; REAIMGUIAPI_IMPLEMENT lives in
// MixerWindow.cpp (one TU materialises the lazy-resolved function storage).
#include "reaper_imgui_functions.h"

// Embedded manual: kUserManualBytes / kUserManualSize in uf8::setup_bundle,
// baked from docs/user-manual.md at build time.
#include "user_manual.h"

// Opens a URL in the user's browser (defined in main.cpp). Used for inline
// [text](url) links.
void reasixty_openUrl(const char* url);

namespace uf8 {
namespace {

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------

enum InlineStyle : uint8_t { S_PLAIN = 0, S_BOLD = 1, S_CODE = 2, S_LINK = 3 };

// One word of inline text plus its style. The renderer lays these out with
// manual word-wrapping so bold / code / link runs can mix fonts on a flowing,
// wrapping line (ImGui has no rich-text primitive). spaceBefore records
// whether whitespace preceded this word in the source — a style boundary with
// no intervening space (e.g. `foo**bar**`) yields spaceBefore=false so the
// words butt together.
struct Tok {
    std::string text;
    std::string href;        // S_LINK only
    uint8_t     style = S_PLAIN;
    bool        spaceBefore = false;
};

enum BlockType : uint8_t {
    B_H1, B_H2, B_H3, B_PARA, B_BULLET, B_NUM, B_TABLE, B_RULE, B_SPACER
};

struct Block {
    uint8_t                 type = B_PARA;
    int                     level = 0;       // bullet/numbered indent depth
    std::string             marker;          // "•" or "1." for list items
    std::vector<Tok>        inl;             // text blocks
    std::vector<std::vector<std::vector<Tok>>> rows; // table: rows→cols→tokens
};

struct SectionRef { std::string title; int blockIdx = 0; };
struct Chapter {
    std::string             title;
    int                     blockStart = 0;
    int                     blockEnd   = 0;
    std::vector<SectionRef> sections;
};

struct Model {
    std::vector<Block>   blocks;
    std::vector<Chapter> chapters;
    std::string          version;   // from frontmatter `date:`
    bool                 built = false;
};

// ---------------------------------------------------------------------------
// Inline parsing
// ---------------------------------------------------------------------------

// Split a styled span into per-word tokens. The first word inherits
// `leadingSpace`; subsequent words always have a preceding space.
void appendWords(std::vector<Tok>& out, const std::string& txt,
                 uint8_t style, const std::string& href, bool leadingSpace)
{
    size_t i = 0, n = txt.size();
    bool first = true;
    while (i < n) {
        if (txt[i] == ' ' || txt[i] == '\t') { i++; continue; }
        size_t j = i;
        while (j < n && txt[j] != ' ' && txt[j] != '\t') j++;
        Tok t;
        t.text = txt.substr(i, j - i);
        t.style = style;
        t.href = href;
        t.spaceBefore = first ? leadingSpace : true;
        out.push_back(std::move(t));
        first = false;
        i = j;
    }
}

std::vector<Tok> parseInline(const std::string& s)
{
    std::vector<Tok> out;
    std::string cur;
    bool spaceSeen = false;
    auto flushPlain = [&]() {
        if (!cur.empty()) {
            Tok t; t.text = cur; t.style = S_PLAIN; t.spaceBefore = spaceSeen;
            out.push_back(std::move(t));
            cur.clear();
            spaceSeen = false;
        }
    };
    size_t i = 0, n = s.size();
    while (i < n) {
        char c = s[i];
        // `code span`
        if (c == '`') {
            size_t end = s.find('`', i + 1);
            if (end != std::string::npos) {
                flushPlain();
                appendWords(out, s.substr(i + 1, end - (i + 1)),
                            S_CODE, "", spaceSeen);
                spaceSeen = false;
                i = end + 1;
                continue;
            }
        }
        // **bold**
        if (c == '*' && i + 1 < n && s[i + 1] == '*') {
            size_t end = s.find("**", i + 2);
            if (end != std::string::npos) {
                flushPlain();
                appendWords(out, s.substr(i + 2, end - (i + 2)),
                            S_BOLD, "", spaceSeen);
                spaceSeen = false;
                i = end + 2;
                continue;
            }
        }
        // [text](url)
        if (c == '[') {
            size_t close = s.find(']', i + 1);
            if (close != std::string::npos && close + 1 < n
                && s[close + 1] == '(') {
                size_t paren = s.find(')', close + 2);
                if (paren != std::string::npos) {
                    flushPlain();
                    appendWords(out, s.substr(i + 1, close - (i + 1)), S_LINK,
                                s.substr(close + 2, paren - (close + 2)),
                                spaceSeen);
                    spaceSeen = false;
                    i = paren + 1;
                    continue;
                }
            }
        }
        // <https://…> autolink
        if (c == '<') {
            size_t close = s.find('>', i + 1);
            if (close != std::string::npos) {
                std::string inner = s.substr(i + 1, close - (i + 1));
                if (inner.rfind("http", 0) == 0) {
                    flushPlain();
                    appendWords(out, inner, S_LINK, inner, spaceSeen);
                    spaceSeen = false;
                    i = close + 1;
                    continue;
                }
            }
        }
        if (c == ' ' || c == '\t') {
            flushPlain();
            spaceSeen = true;
            i++;
            continue;
        }
        cur.push_back(c);
        i++;
    }
    flushPlain();
    return out;
}

std::string plainText(const std::vector<Tok>& toks)
{
    std::string s;
    for (const Tok& t : toks) {
        if (t.spaceBefore && !s.empty()) s.push_back(' ');
        s += t.text;
    }
    return s;
}

// ---------------------------------------------------------------------------
// Block parsing
// ---------------------------------------------------------------------------

std::string rtrim(const std::string& s)
{
    size_t e = s.size();
    while (e > 0 && (s[e - 1] == '\r' || s[e - 1] == ' ' || s[e - 1] == '\t'))
        e--;
    return s.substr(0, e);
}
std::string trim(const std::string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) b++;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r'))
        e--;
    return s.substr(b, e - b);
}

bool isTableSeparator(const std::string& line)
{
    bool sawDash = false;
    for (char c : line) {
        if (c == '-') sawDash = true;
        else if (c != '|' && c != ':' && c != ' ' && c != '\t') return false;
    }
    return sawDash;
}

std::vector<std::vector<Tok>> parseTableRow(const std::string& line)
{
    // Drop the leading/trailing pipe, split on the rest.
    std::vector<std::vector<Tok>> cells;
    std::string body = trim(line);
    size_t start = (!body.empty() && body[0] == '|') ? 1 : 0;
    size_t end = body.size();
    if (end > start && body[end - 1] == '|') end--;
    std::string seg;
    for (size_t i = start; i < end; ++i) {
        if (body[i] == '|') {
            cells.push_back(parseInline(trim(seg)));
            seg.clear();
        } else {
            seg.push_back(body[i]);
        }
    }
    cells.push_back(parseInline(trim(seg)));
    return cells;
}

void buildModel(Model& m)
{
    const std::string raw(
        reinterpret_cast<const char*>(uf8::setup_bundle::kUserManualBytes),
        uf8::setup_bundle::kUserManualSize);

    // Split into lines.
    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : raw) {
            if (c == '\n') { lines.push_back(rtrim(cur)); cur.clear(); }
            else cur.push_back(c);
        }
        if (!cur.empty()) lines.push_back(rtrim(cur));
    }

    // Strip YAML frontmatter (--- … ---), grabbing the version from `date:`.
    size_t i = 0;
    if (!lines.empty() && lines[0] == "---") {
        size_t j = 1;
        while (j < lines.size() && lines[j] != "---") {
            const std::string& fl = lines[j];
            if (fl.rfind("date:", 0) == 0) m.version = trim(fl.substr(5));
            j++;
        }
        i = (j < lines.size()) ? j + 1 : j;
    }

    std::string para;
    auto flushPara = [&]() {
        if (!para.empty()) {
            Block b; b.type = B_PARA; b.inl = parseInline(para);
            m.blocks.push_back(std::move(b));
            para.clear();
        }
    };

    for (; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (line.empty()) { flushPara(); continue; }

        // LaTeX / pandoc directives (e.g. \newpage) are PDF-only — drop them
        // so they don't render as literal text in the chapters.
        if (line[0] == '\\') continue;

        // Headings
        if (line.rfind("### ", 0) == 0) {
            flushPara();
            Block b; b.type = B_H3; b.inl = parseInline(trim(line.substr(4)));
            m.blocks.push_back(std::move(b));
            continue;
        }
        if (line.rfind("## ", 0) == 0) {
            flushPara();
            Block b; b.type = B_H2; b.inl = parseInline(trim(line.substr(3)));
            m.blocks.push_back(std::move(b));
            continue;
        }
        if (line.rfind("# ", 0) == 0) {
            flushPara();
            Block b; b.type = B_H1; b.inl = parseInline(trim(line.substr(2)));
            m.blocks.push_back(std::move(b));
            continue;
        }
        // Horizontal rule
        if (line == "---" || line == "***" || line == "___") {
            flushPara();
            Block b; b.type = B_RULE;
            m.blocks.push_back(std::move(b));
            continue;
        }
        // Table — gather consecutive pipe rows.
        if (!line.empty() && trim(line)[0] == '|') {
            flushPara();
            Block b; b.type = B_TABLE;
            size_t k = i;
            while (k < lines.size() && !lines[k].empty()
                   && trim(lines[k])[0] == '|') {
                if (!isTableSeparator(lines[k]))
                    b.rows.push_back(parseTableRow(lines[k]));
                k++;
            }
            i = k - 1;  // outer loop ++ lands on the line after the table
            if (!b.rows.empty()) m.blocks.push_back(std::move(b));
            continue;
        }
        // Indent / list detection
        size_t indent = 0;
        while (indent < line.size() && line[indent] == ' ') indent++;
        const std::string body = line.substr(indent);
        // Bullet
        if (body.size() >= 2 && (body[0] == '-' || body[0] == '*')
            && body[1] == ' ') {
            flushPara();
            Block b; b.type = B_BULLET;
            b.level = static_cast<int>(indent / 2);
            b.marker = "\xE2\x80\xA2";  // •
            b.inl = parseInline(trim(body.substr(2)));
            m.blocks.push_back(std::move(b));
            continue;
        }
        // Numbered: digits '.' ' '
        {
            size_t d = 0;
            while (d < body.size() && body[d] >= '0' && body[d] <= '9') d++;
            if (d > 0 && d + 1 < body.size() && body[d] == '.'
                && body[d + 1] == ' ') {
                flushPara();
                Block b; b.type = B_NUM;
                b.level = static_cast<int>(indent / 2);
                b.marker = body.substr(0, d + 1);
                b.inl = parseInline(trim(body.substr(d + 2)));
                m.blocks.push_back(std::move(b));
                continue;
            }
        }
        // Blockquote — strip the marker, treat as paragraph text.
        if (body.rfind("> ", 0) == 0) {
            if (!para.empty()) para.push_back(' ');
            para += trim(body.substr(2));
            continue;
        }
        // Plain paragraph text — soft-join with a space.
        if (!para.empty()) para.push_back(' ');
        para += trim(line);
    }
    flushPara();

    // Build chapter / section navigation from H1 / H2.
    for (int bi = 0; bi < static_cast<int>(m.blocks.size()); ++bi) {
        const Block& b = m.blocks[bi];
        if (b.type == B_H1) {
            if (!m.chapters.empty()) m.chapters.back().blockEnd = bi;
            Chapter c;
            c.title = plainText(b.inl);
            c.blockStart = bi;
            m.chapters.push_back(std::move(c));
        } else if (b.type == B_H2 && !m.chapters.empty()) {
            m.chapters.back().sections.push_back({ plainText(b.inl), bi });
        }
    }
    if (!m.chapters.empty())
        m.chapters.back().blockEnd = static_cast<int>(m.blocks.size());
    // Fallback: no H1 at all → one chapter covering everything.
    if (m.chapters.empty() && !m.blocks.empty()) {
        Chapter c;
        c.title = "Manual";
        c.blockStart = 0;
        c.blockEnd = static_cast<int>(m.blocks.size());
        m.chapters.push_back(std::move(c));
    }
    m.built = true;
}

const Model& model()
{
    static Model m;
    if (!m.built) buildModel(m);
    return m;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

constexpr int kCodeColour = 0xE6A45CFF;  // warm amber — reads on dark + light
constexpr int kLinkColour = 0x4AA3FFFF;  // blue

ImGui_Font* fontFor(uint8_t style)
{
    ImGui_Font* sans = reasixty_uiSansFont();
    if (style == S_CODE) { ImGui_Font* f = reasixty_uiMonoFont(); return f ? f : sans; }
    if (style == S_BOLD) { ImGui_Font* f = reasixty_uiBoldFont(); return f ? f : sans; }
    return sans;
}

void drawTok(ImGui_Context* ctx, const Tok& t)
{
    if (t.style == S_LINK) {
        ImGui_TextColored(ctx, kLinkColour, t.text.c_str());
        if (ImGui_IsItemClicked(ctx, nullptr) && !t.href.empty())
            reasixty_openUrl(t.href.c_str());
    } else if (t.style == S_CODE) {
        ImGui_TextColored(ctx, kCodeColour, t.text.c_str());
    } else {
        ImGui_Text(ctx, t.text.c_str());
    }
}

// Width of a token's text under its own font.
double tokWidth(ImGui_Context* ctx, const Tok& t, double basePx)
{
    ImGui_Font* f = fontFor(t.style);
    if (f) ImGui_PushFont(ctx, f, basePx);
    double w = 0, h = 0;
    ImGui_CalcTextSize(ctx, t.text.c_str(), &w, &h, nullptr, nullptr);
    if (f) ImGui_PopFont(ctx);
    return w;
}

// Lay out inline tokens with manual word-wrapping. ImGui's cursor sits at the
// next line's start after each item, so we use the classic "decide after the
// item is drawn" idiom (Dear ImGui demo): draw token n, then measure whether
// token n+1 still fits before the content edge and, if so, SameLine() it onto
// the current line with our own inter-word gap.
void renderInline(ImGui_Context* ctx, const std::vector<Tok>& toks,
                  double basePx)
{
    if (toks.empty()) { ImGui_Text(ctx, ""); return; }

    // Right edge in screen space, derived cell-awarely: GetContentRegionAvail
    // reports the *column* width inside a table cell (window-level queries
    // would give the whole body and the cell text would never wrap). The
    // cursor is at the line start on entry, so avail == full line width in
    // both the body and a cell. GetCursorScreenPos matches GetItemRectMax's
    // screen-space basis.
    double availX = 0, availY = 0;
    ImGui_GetContentRegionAvail(ctx, &availX, &availY);
    double curSX = 0, curSY = 0;
    ImGui_GetCursorScreenPos(ctx, &curSX, &curSY);
    const double visibleX2 = curSX + availX;   // screen-space right edge

    ImGui_Font* sans = reasixty_uiSansFont();
    double spaceW = 0, sh = 0;
    if (sans) ImGui_PushFont(ctx, sans, basePx);
    ImGui_CalcTextSize(ctx, " ", &spaceW, &sh, nullptr, nullptr);
    if (sans) ImGui_PopFont(ctx);

    const size_t n = toks.size();
    for (size_t i = 0; i < n; ++i) {
        ImGui_Font* f = fontFor(toks[i].style);
        if (f) ImGui_PushFont(ctx, f, basePx);
        drawTok(ctx, toks[i]);
        if (f) ImGui_PopFont(ctx);

        if (i + 1 < n) {
            double gap = toks[i + 1].spaceBefore ? spaceW : 0.0;
            double wNext = tokWidth(ctx, toks[i + 1], basePx);
            double itemX2 = 0, itemY2 = 0;
            ImGui_GetItemRectMax(ctx, &itemX2, &itemY2);
            if (itemX2 + gap + wNext < visibleX2)
                ImGui_SameLine(ctx, nullptr, &gap);
            // else: leave the default line break so token i+1 wraps.
        }
    }
}

void renderHeading(ImGui_Context* ctx, const Block& b, double basePx)
{
    double mult = (b.type == B_H1) ? 1.5 : (b.type == B_H2) ? 1.22 : 1.06;
    ImGui_Font* bold = reasixty_uiBoldFont();
    ImGui_Font* use = bold ? bold : reasixty_uiSansFont();
    if (b.type != B_H1) { double pad = basePx * 0.5; ImGui_Dummy(ctx, 0.0, pad); }
    if (use) ImGui_PushFont(ctx, use, basePx * mult);
    ImGui_Text(ctx, plainText(b.inl).c_str());
    if (use) ImGui_PopFont(ctx);
    if (b.type != B_H3) ImGui_Separator(ctx);
}

void renderTable(ImGui_Context* ctx, const Block& b, int blockIdx,
                 double basePx)
{
    int ncols = 0;
    for (const auto& row : b.rows)
        ncols = (static_cast<int>(row.size()) > ncols)
                    ? static_cast<int>(row.size()) : ncols;
    if (ncols == 0) return;

    char id[32];
    snprintf(id, sizeof(id), "##man_tbl_%d", blockIdx);
    int flags = ImGui_TableFlags_Borders | ImGui_TableFlags_RowBg
              | ImGui_TableFlags_SizingStretchProp;
    if (!ImGui_BeginTable(ctx, id, ncols, &flags, nullptr, nullptr, nullptr))
        return;
    int colFlags = ImGui_TableColumnFlags_WidthStretch;
    for (int c = 0; c < ncols; ++c) {
        char cn[16]; snprintf(cn, sizeof(cn), "c%d", c);
        ImGui_TableSetupColumn(ctx, cn, &colFlags, nullptr, nullptr);
    }
    for (size_t r = 0; r < b.rows.size(); ++r) {
        ImGui_TableNextRow(ctx, nullptr, nullptr);
        const bool header = (r == 0);
        ImGui_Font* bold = reasixty_uiBoldFont();
        for (int c = 0; c < ncols; ++c) {
            ImGui_TableNextColumn(ctx);
            if (c >= static_cast<int>(b.rows[r].size())) continue;
            if (header && bold) ImGui_PushFont(ctx, bold, basePx);
            renderInline(ctx, b.rows[r][c], basePx);
            if (header && bold) ImGui_PopFont(ctx);
        }
    }
    ImGui_EndTable(ctx);
    ImGui_Dummy(ctx, 0.0, basePx * 0.4);
}

void renderListItem(ImGui_Context* ctx, const Block& b, double basePx)
{
    double indent = basePx * 1.4 * (b.level + 1);
    ImGui_Indent(ctx, &indent);
    ImGui_Text(ctx, b.marker.c_str());
    double gap = basePx * 0.4;
    ImGui_SameLine(ctx, nullptr, &gap);
    renderInline(ctx, b.inl, basePx);
    ImGui_Unindent(ctx, &indent);
}

} // namespace

void ManualView::draw(ImGui_Context* ctx)
{
    const Model& m = model();
    const double basePx = reasixty_uiFontPx();

    // Persisted-across-frames navigation state.
    static int selectedChapter = 0;
    static int pendingScrollBlock = -1;
    if (selectedChapter >= static_cast<int>(m.chapters.size()))
        selectedChapter = 0;
    if (m.chapters.empty()) { ImGui_Text(ctx, "Manual unavailable."); return; }

    // ---- Left navigation rail ---------------------------------------------
    double navW = basePx * 18.0;     // ~ chapter-title width
    int navFlags = ImGui_ChildFlags_Borders;
    if (ImGui_BeginChild(ctx, "##man_nav", &navW, nullptr, &navFlags, nullptr)) {
        if (!m.version.empty()) {
            std::string v = "Manual " + m.version;
            ImGui_TextDisabled(ctx, v.c_str());
            ImGui_Separator(ctx);
        }
        for (int ci = 0; ci < static_cast<int>(m.chapters.size()); ++ci) {
            const Chapter& c = m.chapters[ci];
            bool sel = (ci == selectedChapter);
            char lbl[256];
            snprintf(lbl, sizeof(lbl), "%s##man_ch_%d", c.title.c_str(), ci);
            if (ImGui_Selectable(ctx, lbl, &sel, nullptr, nullptr, nullptr)) {
                selectedChapter = ci;
                pendingScrollBlock = c.blockStart;
            }
            if (ci == selectedChapter && !c.sections.empty()) {
                double ind = basePx * 1.0;
                ImGui_Indent(ctx, &ind);
                for (size_t si = 0; si < c.sections.size(); ++si) {
                    const SectionRef& s = c.sections[si];
                    bool ssel = false;
                    char slbl[300];
                    snprintf(slbl, sizeof(slbl), "%s##man_sec_%d_%zu",
                             s.title.c_str(), ci, si);
                    if (ImGui_Selectable(ctx, slbl, &ssel, nullptr,
                                         nullptr, nullptr)) {
                        pendingScrollBlock = s.blockIdx;
                    }
                }
                ImGui_Unindent(ctx, &ind);
            }
        }
    }
    ImGui_EndChild(ctx);

    ImGui_SameLine(ctx, nullptr, nullptr);

    // ---- Right body --------------------------------------------------------
    int bodyFlags = ImGui_ChildFlags_Borders;
    if (ImGui_BeginChild(ctx, "##man_body", nullptr, nullptr, &bodyFlags,
                         nullptr)) {
        double pad = basePx * 0.6;
        ImGui_Indent(ctx, &pad);
        const Chapter& c = m.chapters[selectedChapter];
        for (int bi = c.blockStart; bi < c.blockEnd; ++bi) {
            const Block& b = m.blocks[bi];
            switch (b.type) {
                case B_H1: case B_H2: case B_H3:
                    renderHeading(ctx, b, basePx); break;
                case B_PARA:
                    renderInline(ctx, b.inl, basePx); break;
                case B_BULLET: case B_NUM:
                    renderListItem(ctx, b, basePx); break;
                case B_TABLE:
                    renderTable(ctx, b, bi, basePx); break;
                case B_RULE:
                    ImGui_Separator(ctx); break;
                case B_SPACER:
                    ImGui_Dummy(ctx, 0.0, basePx * 0.4); break;
            }
            // Anchor-scroll: when a nav target was just clicked, snap the
            // viewport to that block once it's drawn.
            if (bi == pendingScrollBlock) {
                double ratio = 0.0;
                ImGui_SetScrollHereY(ctx, &ratio);
                pendingScrollBlock = -1;
            }
        }
        ImGui_Unindent(ctx, &pad);
    }
    ImGui_EndChild(ctx);
}

} // namespace uf8
