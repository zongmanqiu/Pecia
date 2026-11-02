// test_preview_selection.cpp - gate on preview text selection (P2).
//
// Why this exists: litehtml 0.10 has no selection API. Selection is rebuilt
// from scratch on top of what it does expose -- every text node already split
// into word / space elements, each with its own placement box. Errors here are
// all silent: nothing crashes, the highlight is simply offset from the glyphs,
// or the copied text loses a word, doubles a space, or glues two lines that
// should have been separated.
//
// The contract asserted here:
//   1. Document-order runs. Words must come back in reading order with the
//      spaces and <br> between them, because both the highlight and the copied
//      string are slices of that flat list.
//   2. Hit offsets are character offsets. The container measures 8 px per byte
//      in this test, so "click the middle of Hello" must resolve to offset 2,
//      not to a pixel position or a byte count.
//   3. hit_text is strict, hit is loose. Pressing inside the page margin must
//      not start a selection (that used to be a pan drag); dragging outside the
//      text still has to track the nearest run.
//   4. Rectangles line up with the run boxes. A rectangle for "llo" has to start
//      at the width of "He" and be exactly "llo"'s width; otherwise the
//      highlight drifts while the copied text still looks right.
//   5. Copy text: real <br> becomes '\n', spaces collapse to single spaces, and
//      a reversed drag yields the same text as a forward drag.
//
// The stub container measures every char as 8 px, which makes the expected
// geometry exact instead of font-dependent.

#include "test_assert.h"

#include "mdview/selection.h"

#include <litehtml/document.h>
#include <litehtml/document_container.h>
#include <litehtml/element.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

// Minimal container. Same one the preview gates use: no image cache, no font
// cache, deterministic text width.
// NOTE: create_element() must return null. document::create_element() falls
// back to litehtml's own tag mapping on null; delegating back to it is infinite
// recursion (this already bit the P3 prototype).
class StubContainer : public litehtml::document_container {
public:
    // Non-zero metrics are mandatory: a zero line height makes every inline
    // box 0 px tall, TextSelection::build() then drops every text run (it
    // ignores boxes with height < 0.5) and the whole selection comes back
    // empty. Same stub as test_litehtml_css_pool / test_nest_depth.
    litehtml::uint_ptr create_font(const litehtml::font_description&, const litehtml::document*,
                                   litehtml::font_metrics* fm) override
    {
        fm->height = 16;
        fm->ascent = 12;
        fm->descent = 4;
        fm->x_height = 8;
        return 1;
    }
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

struct WordBox {
    std::string text;
    litehtml::position box;
};

// The test's own walk, deliberately NOT TextSelection's: expected word order and
// boxes have to come from an independent traversal, otherwise the test would
// only prove that build() agrees with itself.
void collect_words(const litehtml::element::ptr& el, std::vector<WordBox>& out, int depth = 0)
{
    if (!el || depth > 200) return;
    if (el->is_text()) {
        if (el->is_space() || el->is_break()) return;
        std::string t;
        el->get_text(t);
        if (t.empty()) return;
        const litehtml::position p = el->get_placement();
        if (p.width < 0.5f || p.height < 0.5f) return;
        out.push_back(WordBox{t, p});
        return;
    }
    for (const auto& c : el->children()) collect_words(c, out, depth + 1);
}

litehtml::element::ptr find_body(const litehtml::element::ptr& root)
{
    if (!root) return nullptr;
    if (root->is_body()) return root;
    for (const auto& c : root->children()) {
        if (c && c->is_body()) return c;
    }
    return root;
}

void collect_body_words(const litehtml::document::ptr& doc, std::vector<WordBox>& out)
{
    collect_words(find_body(doc->root()), out);
}

int center_x(const WordBox& w) { return (int)w.box.left() + (int)w.box.width / 2; }
int center_y(const WordBox& w) { return (int)w.box.top() + (int)w.box.height / 2; }

// Two lines of text separated by a real <br>. Word runs alternate with space
// runs, and the <br> contributes one run of its own:
//   0 "Hello"  1 space  2 "world"  3 <br>  4 "Second"  5 space  6 "line"
const char* kTwoLineHtml =
    "<html><body><p style=\"margin:0\">Hello world<br>Second line</p></body></html>";

void test_two_line_document()
{
    StubContainer container;
    litehtml::document::ptr doc =
        litehtml::document::createFromString(kTwoLineHtml, &container);
    CHECK(doc != nullptr);
    if (!doc) return;
    doc->render(400);

    std::vector<WordBox> words;
    collect_body_words(doc, words);
    CHECK_EQ((int)words.size(), 4);
    if (words.size() != 4) return;
    CHECK(words[0].text == "Hello");
    CHECK(words[1].text == "world");
    CHECK(words[2].text == "Second");
    CHECK(words[3].text == "line");

    pecia::TextSelection sel;
    sel.build(doc.get(), &container);
    CHECK(sel.valid());
    CHECK(sel.document_alive());
    CHECK(sel.document() == doc.get());

    // --- all of the text -----------------------------------------------------
    sel.select_all();
    CHECK(sel.has_range());
    CHECK(sel.text() == "Hello world\nSecond line");

    std::vector<litehtml::position> rects;
    sel.rects(rects);
    CHECK(rects.size() >= 4);
    for (const auto& r : rects) {
        CHECK(r.width > 0);
        CHECK(r.height > 0);
    }

    // --- hit_text: runs are in document order ---------------------------------
    // Expected run index per word: word 0->0, 1->2, 2->4, 3->6 (space and <br>
    // runs sit between them). Any other index means the flat run list is wrong
    // and both the highlight and the copied text would be slices of the wrong
    // element.
    const int kExpectedRun[4] = {0, 2, 4, 6};
    for (int i = 0; i < 4; ++i) {
        pecia::TextSelection::Pos pos;
        CHECK(sel.hit_text((litehtml::pixel_t)center_x(words[(size_t)i]),
                           (litehtml::pixel_t)center_y(words[(size_t)i]), pos));
        CHECK_EQ(pos.run, kExpectedRun[i]);
    }

    // Middle of "Hello" is char offset 2: "He" is 16 px, "Hel" is 24 px, and
    // 20 px lands between them. Proves offsets are characters, not pixels.
    {
        pecia::TextSelection::Pos pos;
        CHECK(sel.hit_text((litehtml::pixel_t)center_x(words[0]),
                           (litehtml::pixel_t)center_y(words[0]), pos));
        CHECK_EQ(pos.run, 0);
        CHECK_EQ(pos.offset, 2);
    }

    // --- hit_text is strict ---------------------------------------------------
    // Page margin to the right of the first line: no run's box contains it,
    // so pressing here must keep the pan-scroll behaviour, not start a selection.
    // +100 keeps it clear of hit_text's 2 px horizontal tolerance ("world"
    // ends 48 px right of "Hello", so +50 would land exactly on that edge).
    {
        pecia::TextSelection::Pos pos;
        CHECK(!sel.hit_text((litehtml::pixel_t)(words[0].box.right() + 100),
                            (litehtml::pixel_t)center_y(words[0]), pos));
    }
    // Above the first line (beyond the 1 px tolerance).
    {
        pecia::TextSelection::Pos pos;
        CHECK(!sel.hit_text((litehtml::pixel_t)center_x(words[0]),
                            (litehtml::pixel_t)(words[0].box.top() - 6), pos));
    }
    // --- ... while hit() is loose --------------------------------------------
    // Dragging past the end of the line still has to resolve to the nearest run.
    {
        pecia::TextSelection::Pos pos;
        CHECK(sel.hit((litehtml::pixel_t)(words[0].box.right() + 100),
                      (litehtml::pixel_t)center_y(words[0]), pos));
    }

    // --- partial selection: "llo world" --------------------------------------
    // Anchor at offset 2 of "Hello", drag to the end of "world".
    sel.begin(pecia::TextSelection::Pos{0, 2});
    CHECK(!sel.has_range());   // an anchor alone is not a range
    sel.extend(pecia::TextSelection::Pos{2, 5});
    CHECK(sel.has_range());
    CHECK(sel.text() == "llo world");

    rects.clear();
    sel.rects(rects);
    // "llo" + the space run + "world" = three rectangles.
    CHECK_EQ((int)rects.size(), 3);
    if (rects.size() == 3) {
        // "llo" starts at the width of "He" (16 px) and is 24 px wide.
        CHECK_EQ((int)rects[0].x, (int)words[0].box.left() + 16);
        CHECK_EQ((int)rects[0].width, 24);
        // The space run is highlighted as its whole box, not as a zero-width sliver.
        // Its box spans from the end of "Hello" to the start of "world".
        CHECK_EQ((int)rects[1].x, (int)words[0].box.right());
        CHECK_EQ((int)rects[1].width,
                 (int)(words[1].box.left() - words[0].box.right()));
        CHECK_EQ((int)rects[2].x, (int)words[1].box.left());
        CHECK_EQ((int)rects[2].width, (int)words[1].box.width);
    }

    // --- reversed drag yields the same text -----------------------------------
    sel.begin(pecia::TextSelection::Pos{2, 5});
    sel.extend(pecia::TextSelection::Pos{0, 2});
    CHECK(sel.has_range());
    CHECK(sel.text() == "llo world");

    // --- space hit uses the midpoint -----------------------------------------
    // Space run is one character wide; clicking its right half collapses
    // forward (offset 1), not backward.
    {
        pecia::TextSelection::Pos pos;
        // The space box sits between the two words; take its own middle.
        const litehtml::position spaceBox(
            words[0].box.right(), words[0].box.top(),
            (litehtml::pixel_t)(words[1].box.left() - words[0].box.right()),
            words[0].box.height);
        CHECK(sel.hit_text((litehtml::pixel_t)(spaceBox.left() + spaceBox.width / 2),
                           (litehtml::pixel_t)(spaceBox.top() + spaceBox.height / 2), pos));
        CHECK_EQ(pos.run, 1);
        CHECK_EQ(pos.offset, 1);
    }

    // --- teardown -------------------------------------------------------------
    sel.clear();
    CHECK(!sel.valid());
    CHECK(!sel.has_range());

    doc.reset();
    CHECK(!sel.document_alive());
}

// 预览内查找：在渲染文本（run 拼接串）里查找。断言的是"命中落在哪个
// run/偏移、Next/Prev 往哪边推进、回绕、序号与总数"——这些错了都不会崩，
// 只是高亮跳到别的字上（或者根本不出现），肉眼极难发现。
void test_preview_find()
{
    StubContainer container;
    litehtml::document::ptr doc =
        litehtml::document::createFromString(kTwoLineHtml, &container);
    CHECK(doc != nullptr);
    if (!doc) return;
    doc->render(400);

    pecia::TextSelection sel;
    sel.build(doc.get(), &container);
    CHECK(sel.valid());
    if (!sel.valid()) return;

    // run 顺序见 test_two_line_document：0 "Hello" 1 space 2 "world"
    //                                 3 <br>   4 "Second" 5 space 6 "line"
    const pecia::TextSelection::Pos kNoAnchor;   // run = -1：从头/从尾开始
    pecia::TextSelection::Pos from, s, e;
    int idx = -1, total = -1;
    bool wrapped = true;

    // 1) 单词命中：只在 run 2 里
    CHECK(sel.find("world", false, false, true, kNoAnchor, true,
                   s, e, &idx, &total, &wrapped));
    CHECK_EQ(s.run, 2);
    CHECK_EQ(s.offset, 0);
    CHECK_EQ(e.run, 2);
    CHECK_EQ(e.offset, 5);
    CHECK_EQ(idx, 1);
    CHECK_EQ(total, 1);
    CHECK(!wrapped);

    // 2) 跨 run 命中：横跨 word / space / word 三个 run 也要能拼出来
    CHECK(sel.find("Hello world", false, false, true, kNoAnchor, true,
                   s, e, &idx, &total, &wrapped));
    CHECK_EQ(s.run, 0);
    CHECK_EQ(s.offset, 0);
    CHECK_EQ(e.run, 2);
    CHECK_EQ(e.offset, 5);
    CHECK_EQ(total, 1);

    // 3) 连续 Next / Prev：'e' 共 3 处（Hello / Second / line）
    CHECK(sel.find("e", false, false, true, kNoAnchor, true,
                   s, e, &idx, &total, &wrapped));
    CHECK_EQ(idx, 1);
    CHECK_EQ(total, 3);
    CHECK_EQ(s.run, 0);
    CHECK_EQ(s.offset, 1);

    // Next 从上一处命中的**终点**继续
    from = e;
    CHECK(sel.find("e", false, false, true, from, true,
                   s, e, &idx, &total, &wrapped));
    CHECK_EQ(idx, 2);
    CHECK_EQ(s.run, 4);
    CHECK_EQ(s.offset, 1);

    from = e;
    CHECK(sel.find("e", false, false, true, from, true,
                   s, e, &idx, &total, &wrapped));
    CHECK_EQ(idx, 3);
    CHECK_EQ(s.run, 6);
    CHECK_EQ(s.offset, 3);

    // 已到最后一处：Next 回绕到第一处
    from = e;
    CHECK(sel.find("e", false, false, true, from, true,
                   s, e, &idx, &total, &wrapped));
    CHECK_EQ(idx, 1);
    CHECK_EQ(s.run, 0);
    CHECK(wrapped);

    // Prev 从上一处命中的**起点**往回
    from = s;
    CHECK(sel.find("e", false, false, false, from, true,
                   s, e, &idx, &total, &wrapped));
    CHECK_EQ(idx, 3);
    CHECK_EQ(s.run, 6);
    CHECK(wrapped);

    // 不回绕：从最后一处往后找，必须"没找到"而不是绕回第一处
    from = s;
    from.run = 6;
    from.offset = 4;   // "line" 末尾
    CHECK(sel.find("e", false, false, true, from, false,
                   s, e, &idx, &total, &wrapped));
    CHECK_EQ(s.run, -1);   // 未命中
    CHECK(!wrapped);

    // 4) 大小写：可区分时 "HELLO" 不存在，不可区分时命中 run 0
    CHECK(sel.find("HELLO", true, false, true, kNoAnchor, true,
                   s, e, &idx, &total, &wrapped));
    CHECK_EQ(s.run, -1);
    CHECK(sel.find("HELLO", false, false, true, kNoAnchor, true,
                   s, e, &idx, &total, &wrapped));
    CHECK_EQ(s.run, 0);
    CHECK_EQ(s.offset, 0);
    CHECK_EQ(e.run, 0);
    CHECK_EQ(e.offset, 5);

    // 5) 整词：单个 'l' 不是任何整词 → 0 处
    CHECK(sel.find("l", false, true, true, kNoAnchor, true,
                   s, e, &idx, &total, &wrapped));
    CHECK_EQ(s.run, -1);
    CHECK_EQ(total, 0);
    // 去掉整词限制就能命中
    CHECK(sel.find("l", false, false, true, kNoAnchor, true,
                   s, e, &idx, &total, &wrapped));
    CHECK(s.run >= 0);

    // 6) 命中区间直接设成选区，取文与矩形都跟着走
    sel.find("world", false, false, true, kNoAnchor, true,
             s, e, &idx, &total, &wrapped);
    sel.set_range(s, e);
    CHECK(sel.has_range());
    CHECK(sel.text() == "world");
    std::vector<litehtml::position> rects;
    sel.rects(rects);
    CHECK_EQ((int)rects.size(), 1);
    if (rects.size() == 1) CHECK_EQ((int)rects[0].width, 5 * 8);
}

// 复制文本的块边界与 pre 空白。两个契约：
//   1. 相邻块（<p>、<li> 等）之间必须补换行。不能靠「行间距 > 行高」判断，
//      因为 preprocess 已把 p+p / li+li 的外边距清零，段间距恰好等于行距。
//      所以 build() 记录每个 run 所属的块编号，text() 按编号变化补换行。
//   2. white-space: pre* 里的空白是内容：行首缩进、连续空格必须原样保留，
//      否则复制出来的代码不能直接运行。
void test_block_and_pre_text()
{
    StubContainer container;
    const char* html =
        "<html><body>"
        "<p>First line</p><p>Second line</p>"
        "<pre><code>if (x) {\n    y();\n}</code></pre>"
        "</body></html>";
    litehtml::document::ptr doc = litehtml::document::createFromString(html, &container);
    CHECK(doc != nullptr);
    if (!doc) return;
    doc->render(400);

    pecia::TextSelection sel;
    sel.build(doc.get(), &container);
    CHECK(sel.valid());
    if (!sel.valid()) return;

    sel.select_all();
    CHECK(sel.text() == "First line\nSecond line\nif (x) {\n    y();\n}");
}

void test_empty_document()
{
    StubContainer container;
    const char* html = "<html><body></body></html>";
    litehtml::document::ptr doc = litehtml::document::createFromString(html, &container);
    CHECK(doc != nullptr);
    if (!doc) return;
    doc->render(400);

    pecia::TextSelection sel;
    sel.build(doc.get(), &container);
    CHECK(!sel.valid());      // no text runs: nothing can be selected
    CHECK(!sel.has_range());

    pecia::TextSelection::Pos pos;
    CHECK(!sel.hit(10, 10, pos));
    CHECK(!sel.hit_text(10, 10, pos));

    sel.select_all();
    CHECK(!sel.has_range());
    CHECK(sel.text().empty());
}

}  // namespace

// 直接调用各用例：TEST_MAIN() 只生成返回码、不会自动跑用例（本文件此前
// 就是这样，两个用例从未执行、门禁显示 "0 checks" —— 等于没测）。
int main()
{
    test_two_line_document();
    test_block_and_pre_text();
    test_empty_document();
    test_preview_find();

    const int fails = test::failCount();
    std::fprintf(stderr, "%d checks, %d failures\n", test::checkCount(), fails);
    return fails ? 1 : 0;
}
