// test_block_layout.cpp - gate on the block (virtualized) preview layout (P3).
//
// Why this exists: BlockLayout replaces "one document holds the whole page" with
// "only the blocks near the viewport hold a document". The failure modes of that
// change are all SILENT -- no crash, no exception, just content that quietly
// isn't there:
//
//   1. Lossy split. A cut point landing inside <pre> or <table>, or a void
//      element (<br>/<hr>/<img>) whose end offset is empty, drops a node.
//      The first prototype dropped 1,081 nodes this way and still "looked fine"
//      on a screenshot. So the contract asserted here is per-element coverage:
//      every top-level element of the original <body> appears in exactly ONE
//      block -- not "at least once" (duplicates double content) and not
//      "roughly all" (the bug above was ~5% missing).
//   2. Unbounded window. If ensure_window never releases, memory goes back to
//      scaling with document length -- the whole point of P3 disappears while
//      every test still passes. Asserted by live_count() after moving the
//      window.
//   3. Geometry that doesn't advance. Offsets must be strictly increasing and
//      cover the document; otherwise block_at() lands on the wrong block and
//      the page scrolls to nothing. Asserted structurally, not by pixel value.
//   4. Stale real height after a width change. A block measured at 800 px is
//      kept and reused at another width, so the scrollbar total is wrong and
//      the end of the document is unreachable. Asserted by measuring a block,
//      changing width, and requiring the measured height to stop being used.
//
// The assertions are on the CHAIN's contract, not on BlockLayout's internals:
// no block index, no byte budget and no px-per-KB constant is asserted here.
// Those are tuning knobs -- this file fails when content is lost or the working
// set stops being bounded, which are the properties users actually feel.

#include "test_assert.h"

#include "mdview/block_layout.h"

// gumbo.h unconditionally #define _CRT_SECURE_NO_WARNINGS while the test target
// defines the same macro on the command line -> C4005. Retract ours first; this
// file calls no CRT function that the macro applies to.
#ifdef _CRT_SECURE_NO_WARNINGS
#undef _CRT_SECURE_NO_WARNINGS
#endif
#include <gumbo.h>

#include <litehtml.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Minimal container: enough for createFromString() to lay a block out. Text is
// measured as len*8 px, which is crude but monotonic -- the geometry assertions
// only need heights to be positive and ordered, never to match a font.
// NOTE: create_element() must return null. document::create_element() falls
// back to litehtml's own tag mapping on null; delegating back to it is infinite
// recursion (this already bit the P3 prototype).
class StubContainer : public litehtml::document_container {
public:
    litehtml::uint_ptr create_font(const litehtml::font_description&, const litehtml::document*,
                                   litehtml::font_metrics*) override { return 1; }
    void delete_font(litehtml::uint_ptr) override {}
    litehtml::pixel_t text_width(const char* t, litehtml::uint_ptr) override
    {
        return (litehtml::pixel_t)(std::strlen(t) * 8);
    }
    void draw_text(litehtml::uint_ptr, const char*, litehtml::uint_ptr, litehtml::web_color,
                   const litehtml::position&) override {}
    litehtml::pixel_t pt_to_px(float pt) const override { return (litehtml::pixel_t)pt; }
    litehtml::pixel_t get_default_font_size() const override { return 16; }
    const char* get_default_font_name() const override { return "Segoe UI"; }
    void draw_list_marker(uintptr_t, const litehtml::list_marker&) override {}
    void load_image(const char*, const char*, bool) override {}
    void get_image_size(const char*, const char*, litehtml::size& sz) override
    {
        sz.width = 1; sz.height = 1;
    }
    void draw_image(uintptr_t, const litehtml::background_layer&, const std::string&,
                    const std::string&) override {}
    void draw_solid_fill(uintptr_t, const litehtml::background_layer&,
                         const litehtml::web_color&) override {}
    void draw_linear_gradient(uintptr_t, const litehtml::background_layer&,
                              const litehtml::background_layer::linear_gradient&) override {}
    void draw_radial_gradient(uintptr_t, const litehtml::background_layer&,
                              const litehtml::background_layer::radial_gradient&) override {}
    void draw_conic_gradient(uintptr_t, const litehtml::background_layer&,
                             const litehtml::background_layer::conic_gradient&) override {}
    void draw_borders(uintptr_t, const litehtml::borders&, const litehtml::position&,
                      bool) override {}
    void set_caption(const char*) override {}
    void set_base_url(const char*) override {}
    void link(const litehtml::document::ptr&, const litehtml::element::ptr&) override {}
    void on_anchor_click(const char*, const litehtml::element::ptr&) override {}
    void on_mouse_event(const litehtml::element::ptr&, litehtml::mouse_event) override {}
    void set_cursor(const char*) override {}
    void transform_text(litehtml::string&, litehtml::text_transform) override {}
    void import_css(litehtml::string&, const litehtml::string&, litehtml::string&) override {}
    void set_clip(const litehtml::position&, const litehtml::border_radiuses&) override {}
    void del_clip() override {}
    void get_viewport(litehtml::position& vp) const override
    {
        vp.x = 0; vp.y = 0; vp.width = 800; vp.height = 600;
    }
    litehtml::element::ptr create_element(const char*, const litehtml::string_map&,
                                          const litehtml::document::ptr&) override
    {
        return litehtml::element::ptr();
    }
    void get_media_features(litehtml::media_features&) const override {}
    void get_language(litehtml::string&, litehtml::string&) const override {}
};

// One original body child, reduced to a fingerprint taken from its own source
// text. Using the source slice (not a reconstructed tag name) is what makes the
// coverage assertion able to catch a VOID element going missing: "<br>" and
// "<img src=...>" have no end tag to look for, and 1,081 of them vanished in
// the prototype without any structural count noticing.
struct Fingerprint {
    std::string text;    // first N bytes of the element's source slice
};

constexpr size_t kFpLen = 48;
// Below this the fingerprint is too short to be unique (a bare "<p>" would match
// every paragraph); such elements are not individually asserted.
constexpr size_t kMinUniqueFp = 12;

// End of the start tag beginning at `start`: scan to '>' but ignore '>' inside
// quoted attribute values. Same rule the splitter uses for void elements.
size_t tag_end(const std::string& s, size_t start)
{
    char quote = 0;
    for (size_t i = start; i < s.size(); ++i)
    {
        const char c = s[i];
        if (quote)
        {
            if (c == quote) quote = 0;
        }
        else if (c == '"' || c == '\'')
        {
            quote = c;
        }
        else if (c == '>')
        {
            return i + 1;
        }
    }
    return s.size();
}

std::vector<Fingerprint> body_children(const std::string& html)
{
    std::vector<Fingerprint> out;
    // Parse the ORIGINAL html with the same parser the splitter uses, so the
    // offsets are in the same coordinate system (byte offsets into `html`).
    GumboOutput* g = gumbo_parse_with_options(&kGumboDefaultOptions,
                                              html.c_str(), html.size());
    if (!g) return out;
    struct Walk {
        const std::string& src;
        std::vector<Fingerprint>& out;
        void operator()(GumboNode* n)
        {
            if (!n || n->type != GUMBO_NODE_ELEMENT) return;
            if (n->v.element.tag == GUMBO_TAG_BODY)
            {
                const GumboVector& kids = n->v.element.children;
                for (unsigned i = 0; i < kids.length; ++i)
                {
                    GumboNode* k = (GumboNode*)kids.data[i];
                    if (k->type != GUMBO_NODE_ELEMENT) continue;
                    const GumboSourcePosition& s = k->v.element.start_pos;
                    if (s.offset == kGumboEmptySourcePosition.offset) continue;
                    size_t end = k->v.element.end_pos.offset;
                    // Void element (<br>/<hr>/<img>): measured on this gumbo,
                    // end_pos == start_pos (NOT the empty sentinel), so the test
                    // is `end <= start`, not `end == 0`. Getting this wrong makes
                    // the fixtures silently skip every void node -- and then a
                    // splitter that loses them still passes. Take the start tag.
                    if (end <= s.offset) end = tag_end(src, s.offset);
                    if (end <= s.offset) continue;
                    size_t len = end - s.offset;
                    if (len > kFpLen) len = kFpLen;
                    Fingerprint f;
                    f.text = src.substr(s.offset, len);
                    out.push_back(f);
                }
                return;
            }
            const GumboVector& kids = n->v.element.children;
            for (unsigned i = 0; i < kids.length; ++i) (*this)((GumboNode*)kids.data[i]);
        }
    } walk{html, out};
    walk(g->root);
    gumbo_destroy_output(&kGumboDefaultOptions, g);
    return out;
}

size_t count_occurrences(const std::string& hay, const std::string& needle)
{
    if (needle.empty()) return 0;
    size_t n = 0, pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos)
    {
        ++n;
        pos += needle.size();
    }
    return n;
}

// Build a document that is definitely large enough to split, with content that
// exercises the traps: void elements, nested lists, a <pre> block (must never be
// cut through) and enough bytes to fill many 12 KB blocks.
std::string make_doc(int sections)
{
    std::string h =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<style>body{margin:0;padding:0;font-size:16px;line-height:1.6}"
        "p{margin:8px 0}pre{background:#eee}</style></head><body>";
    for (int i = 0; i < sections; ++i)
    {
        h += "<h2 id=\"s" + std::to_string(i) + "\">Section " + std::to_string(i) + "</h2>";
        h += "<p>Paragraph with <b>bold</b> and <i>italic</i> text for section ";
        h += std::to_string(i) + ". It is deliberately long enough to wrap at 800 px.</p>";
        h += "<br><hr>";
        h += "<ul><li>first</li><li>second<ul><li>nested</li></ul></li></ul>";
        h += "<pre>line one\nline two\nline three</pre>";
        h += "<table><tr><td>cell A</td><td>cell B</td></tr></table>";
        h += "<img src=\"x.png\" alt=\"picture\">";
    }
    h += "</body></html>";
    return h;
}

}  // namespace

int main()
{
    StubContainer mc;
    // split() refuses anything that would make fewer than ~2 blocks
    // (< kContentBudget * 2 bytes), so the fixture has to be comfortably past
    // that: ~70 KB here, i.e. 6-7 blocks at the 12 KB content budget.
    const std::string html = make_doc(200);

    // --- 1. split succeeds and produces a usable block count ------------------
    pecia::BlockLayout bl;
    CHECK(bl.split(html));
    CHECK(bl.active());
    CHECK(bl.count() >= 4);
    std::fprintf(stderr, "split: %zu blocks from %zu bytes of html\n", bl.count(), html.size());
    // Everything below indexes blocks; without a split there is nothing to test
    // and the indexing would be undefined behaviour rather than a failure.
    if (!bl.active()) return 1;

    // --- 2. lossless, non-duplicating coverage --------------------------------
    // Contract: for every top-level body element, the number of times its source
    // slice appears across all blocks equals the number of times it appears in
    // the original document. Comparing counts (not just "appears at least once")
    // catches both halves of the bug: a dropped element and an element emitted
    // into two blocks (which double-renders it and shifts everything below).
    std::string all;
    for (size_t i = 0; i < bl.count(); ++i) all += bl.block(i).html;
    const std::vector<Fingerprint> fps = body_children(html);
    CHECK(fps.size() >= 200);
    size_t missing = 0, extra = 0, checked = 0;
    for (const Fingerprint& f : fps)
    {
        if (f.text.size() < kMinUniqueFp) continue;
        ++checked;
        const size_t want = count_occurrences(html, f.text);
        const size_t got = count_occurrences(all, f.text);
        if (got < want) ++missing;
        if (got > want) ++extra;
    }
    std::fprintf(stderr, "coverage: %zu elements checked, %zu short, %zu extra\n",
                 checked, missing, extra);
    CHECK(checked >= 200);
    CHECK_EQ(missing, 0);
    CHECK_EQ(extra, 0);

    // --- 3. geometry advances and covers the document ------------------------
    CHECK(bl.total_height() > 0);
    int prev_off = -1;
    int sum_h = 0;
    for (size_t i = 0; i < bl.count(); ++i)
    {
        CHECK(bl.height_of(i) > 0);
        CHECK(bl.offset_of(i) > prev_off);
        prev_off = bl.offset_of(i);
        sum_h += bl.height_of(i);
    }
    CHECK_EQ(sum_h, bl.total_height());
    // block_at() must land inside the block's own vertical range.
    for (size_t i = 0; i < bl.count(); ++i)
    {
        const int probe = bl.offset_of(i) + bl.height_of(i) / 2;
        CHECK_EQ(bl.block_at(probe), (int)i);
    }

    // --- 4. bounded working set ---------------------------------------------
    // Only the requested window may hold a document. This is the property that
    // makes memory independent of document length; if the release loop is
    // dropped, everything still renders and only this check notices.
    const int n = (int)bl.count();
    CHECK(n >= 4);
    CHECK_EQ(bl.live_count(), 0);
    CHECK(bl.ensure_window(2, 3, &mc));
    CHECK_EQ(bl.live_count(), 2);
    CHECK(bl.block(2).doc != nullptr);
    CHECK(bl.block(3).doc != nullptr);
    CHECK(bl.block(0).doc == nullptr);
    CHECK(bl.block((size_t)n - 1).doc == nullptr);
    // Real heights arrive with the documents: measuring the window must make
    // the geometry use measured values, not the initial guess.
    CHECK(bl.block(2).realHeight > 0);
    CHECK(bl.block(2).renderWidth == bl.width());

    // Moving the window releases the previous one (not "releases nothing").
    CHECK(bl.ensure_window(n - 2, n - 1, &mc));
    CHECK_EQ(bl.live_count(), 2);
    CHECK(bl.block(2).doc == nullptr);
    CHECK(bl.block((size_t)n - 1).doc != nullptr);
    CHECK_EQ(bl.measured_count(), 4);

    // Window edges are clamped, never out of range.
    bl.ensure_window(-5, n + 5, &mc);
    CHECK_EQ(bl.live_count(), n);
    bl.ensure_window(0, 0, &mc);
    CHECK_EQ(bl.live_count(), 1);

    // --- 4b. in-block anchor resolution ------------------------------------
    // "Jump to the block's first line" is not good enough: a 12 KB block is
    // ~2600-3100 px tall (about three screens), so a heading near its tail would
    // land outside the viewport. The contract is that anchor_y() points at the
    // element itself, somewhere inside its own block.
    {
        bl.ensure_window(0, 0, &mc);
        const int y0 = bl.anchor_y(0, "s0");
        CHECK(y0 >= bl.offset_of(0));
        CHECK(y0 < bl.offset_of(0) + bl.height_of(0));

        // The LAST heading inside block 0 must resolve well below the block
        // head. This single assertion is what catches a regression back to
        // "scroll to block start" (which would return offset_of(0) exactly).
        std::string lastId;
        {
            const std::string& bh = bl.block(0).html;
            size_t p = 0;
            while ((p = bh.find("id=\"s", p)) != std::string::npos)
            {
                p += 4;
                const size_t e = bh.find('"', p);
                if (e == std::string::npos) break;
                lastId = bh.substr(p, e - p);
                p = e;
            }
        }
        CHECK(!lastId.empty());
        CHECK(bl.anchor_y(0, lastId) > bl.offset_of(0) + 100);

        // Uninstantiated block: no DOM, so it must say "unknown" instead of
        // guessing. An unknown id behaves the same way (caller falls back).
        bl.ensure_window(n - 1, n - 1, &mc);
        CHECK_EQ(bl.anchor_y(0, "s0"), -1);
        CHECK_EQ(bl.anchor_y((size_t)n - 1, "no-such-id"), -1);
    }

    // --- 5. width change invalidates measured heights -----------------------
    // A height measured at 800 px is meaningless at another width. If it were
    // still used, the scrollbar's total would be wrong and the end of the
    // document unreachable -- a bug that only shows up as "can't scroll to the
    // bottom".
    const int real800 = bl.block(0).realHeight;
    CHECK(real800 > 0);
    bl.ensure_window(0, 0, &mc);
    CHECK_EQ(bl.height_of(0), real800);
    bl.set_width(420);
    CHECK_EQ(bl.live_count(), 0);              // documents released on resize
    CHECK(bl.height_of(0) != real800);         // measured value no longer used
    CHECK_EQ(bl.measured_count(), 0);
    // Re-measuring at the new width is adopted again.
    bl.ensure_window(0, 0, &mc);
    CHECK(bl.block(0).renderWidth == 420);
    CHECK_EQ(bl.height_of(0), bl.block(0).realHeight);

    // --- 6. documents that must NOT be split ---------------------------------
    // Falling back to the full path is the safe answer for anything unusual;
    // splitting a fragment or a doc with too few top-level elements produces
    // blocks that lose inherited styles or margins.
    {
        pecia::BlockLayout tiny;
        CHECK(!tiny.split("<p>hello</p>"));
        CHECK(!tiny.active());
    }
    {
        // No <body> at all: gumbo synthesizes one, but there is only a single
        // top-level element, so the splitter must refuse.
        pecia::BlockLayout frag;
        CHECK(!frag.split("<div>a</div><div>b</div>"));
        CHECK(!frag.active());
    }

    // --- 7. an oversized <pre> is cut between lines --------------------------
    // A document that is "one huge code block" has only two top-level elements
    // (<h1> + <pre>), so top-level cutting alone cannot bound it: measured
    // 405 KB of markdown kept RSS at 235 MB and the whole virtualization was a
    // no-op. The splitter must therefore cut INSIDE <pre> at '\n' boundaries,
    // with every piece carrying its own <pre><code> shell -- otherwise the
    // pieces are bare text with no code-block background or monospace font.
    //
    // Contract asserted here, not implementation: the pieces concatenated in
    // order must reproduce the original code bytes exactly (no line dropped, no
    // line duplicated, no line halved), and each piece must be self-contained.
    {
        std::string code;
        for (int i = 0; i < 4000; ++i)
            code += "int func_" + std::to_string(i) + "(int a, int b) { return a * " +
                    std::to_string(i) + " + b; }  // line " + std::to_string(i) + "\n";
        const std::string pre_html =
            "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
            "<style>pre{background:#eee;padding:16px}</style></head>"
            "<body><h1>Big code</h1><pre><code class=\"language-cpp\">" +
            code + "</code></pre></body></html>";

        pecia::BlockLayout pb;
        CHECK(pb.split(pre_html));
        CHECK(pb.active());
        CHECK(pb.count() >= 4);
        if (pb.active())
        {
            std::string joined;
            size_t pieces = 0, maxPiece = 0;
            for (size_t i = 0; i < pb.count(); ++i)
            {
                const std::string& h = pb.block(i).html;
                const size_t pre = h.find("<pre");
                const size_t codeAt = h.find("<code");
                if (pre == std::string::npos || codeAt == std::string::npos) continue;
                const size_t bodyAt = h.find('>', codeAt);
                const size_t endAt = h.rfind("</code>");
                CHECK(bodyAt != std::string::npos && endAt != std::string::npos);
                if (bodyAt == std::string::npos || endAt == std::string::npos ||
                    endAt <= bodyAt)
                    continue;
                // Self-contained: this piece opens <pre> and closes </pre>.
                CHECK(h.find("</pre>", endAt) != std::string::npos);
                ++pieces;
                maxPiece = std::max(maxPiece, endAt - bodyAt - 1);
                joined.append(h, bodyAt + 1, endAt - bodyAt - 1);
            }
            std::fprintf(stderr, "pre split: %zu pieces, %zu of %zu code bytes\n",
                         pieces, joined.size(), code.size());
            CHECK(pieces >= 4);
            CHECK(joined == code);                    // lossless, ordered, no dupes
            CHECK(maxPiece > 0);
            CHECK(maxPiece * 3 <= code.size());        // really cut, not one giant piece
        }
    }

    std::fprintf(stderr, "%d checks, %d failures\n", test::checkCount(), test::failCount());
    return test::failCount() ? 1 : 0;
}
