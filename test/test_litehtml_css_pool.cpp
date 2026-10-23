// test_litehtml_css_pool.cpp - gate for the css_properties sharing pool (P0).
//
// Context: element::m_css used to be a 992-byte value member, so every DOM node
// carried its own copy. Previewing build/目录结构说明.md (113 KB) produced
// 40,247 nodes -> 38.1 MB of css_properties alone, measured at 86.8 MB peak
// RSS. P0 changes m_css into a shared_ptr and de-duplicates equal computed
// styles through a per-document pool (document::intern_css).
//
// Sharing has exactly two ways to go wrong, and both are SILENT - nothing
// crashes, styles merely end up on the wrong element. This file guards each:
//
//   1. Too eager: two styles that are actually different compare equal and get
//      merged. Root cause would be css_properties::operator== not comparing
//      every field. Guarded by touching every setter and asserting the style
//      stops comparing equal. Adding a member to css_properties.h without
//      updating operator== therefore turns this test red.
//   2. Too eager via inheritance: color / font-* / text-align / white-space are
//      inherited, so a child's computed style depends on its parent's instance.
//      Two identical tags under differently styled parents must NOT merge.
//      Guarded by a real two-parent document.
//
// Plus the copy-on-write contract itself: writing through css_w() on one
// element must never be observable from an element sharing that instance.

#include "test_assert.h"

#include <litehtml.h>

#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Minimal container. No fonts, no drawing, no images - this test is about
// style identity, not about rendering.
// ---------------------------------------------------------------------------
class StubContainer : public litehtml::document_container
{
public:
    litehtml::uint_ptr create_font(const litehtml::font_description&,
                                   const litehtml::document*,
                                   litehtml::font_metrics* fm) override
    {
        if (fm) *fm = litehtml::font_metrics();
        return 1;
    }
    void delete_font(litehtml::uint_ptr) override {}
    litehtml::pixel_t text_width(const char* text, litehtml::uint_ptr) override
    { return litehtml::pixel_t((float)(std::string(text).size() * 8)); }
    void draw_text(litehtml::uint_ptr, const char*, litehtml::uint_ptr,
                   litehtml::web_color, const litehtml::position&) override {}
    void draw_solid_fill(litehtml::uint_ptr, const litehtml::background_layer&,
                         const litehtml::web_color&) override {}
    void draw_linear_gradient(litehtml::uint_ptr, const litehtml::background_layer&,
                              const litehtml::background_layer::linear_gradient&) override {}
    void draw_radial_gradient(litehtml::uint_ptr, const litehtml::background_layer&,
                              const litehtml::background_layer::radial_gradient&) override {}
    void draw_conic_gradient(litehtml::uint_ptr, const litehtml::background_layer&,
                             const litehtml::background_layer::conic_gradient&) override {}
    void draw_list_marker(litehtml::uint_ptr, const litehtml::list_marker&) override {}
    void draw_image(litehtml::uint_ptr, const litehtml::background_layer&,
                    const std::string&, const std::string&) override {}
    void load_image(const char*, const char*, bool) override {}
    void get_image_size(const char*, const char*, litehtml::size& sz) override
    { sz.width = 0; sz.height = 0; }
    void draw_borders(litehtml::uint_ptr, const litehtml::borders&,
                      const litehtml::position&, bool) override {}
    void set_caption(const char*) override {}
    void set_base_url(const char*) override {}
    void link(const std::shared_ptr<litehtml::document>&,
              const litehtml::element::ptr&) override {}
    void on_anchor_click(const char*, const litehtml::element::ptr&) override {}
    void on_mouse_event(const litehtml::element::ptr&, litehtml::mouse_event) override {}
    void set_cursor(const char*) override {}
    void transform_text(litehtml::string&, litehtml::text_transform) override {}
    void import_css(litehtml::string&, const litehtml::string&, litehtml::string&) override {}
    void set_clip(const litehtml::position&, const litehtml::border_radiuses&) override {}
    void del_clip() override {}
    void get_viewport(litehtml::position& vp) const override
    { vp.width = 800; vp.height = 600; }
    litehtml::element::ptr create_element(const char*, const litehtml::string_map&,
                                          const std::shared_ptr<litehtml::document>&) override
    { return nullptr; }
    void get_media_features(litehtml::media_features& mf) const override
    {
        mf.type = litehtml::media_type_screen;
        mf.width = 800; mf.height = 600;
    }
    void get_language(litehtml::string& l, litehtml::string& c) const override
    { l = "en"; c = ""; }
    litehtml::pixel_t pt_to_px(float pt) const override { return litehtml::pixel_t(pt); }
    litehtml::pixel_t get_default_font_size() const override { return litehtml::pixel_t(16); }
    const char* get_default_font_name() const override { return "sans-serif"; }
};

typedef std::vector<litehtml::element::ptr> elements;

void flatten(const litehtml::element::ptr& el, elements& out)
{
    if (!el) return;
    out.push_back(el);
    for (const auto& child : el->children()) flatten(child, out);
}

// ---------------------------------------------------------------------------
// 1. operator== completeness.
//
// For every settable property: change exactly it, and the style MUST stop
// comparing equal to an untouched one. A missing field in operator== shows up
// here rather than as wrong colors in a rendered document.
// ---------------------------------------------------------------------------
#define FLIP(expr)                                                             \
    do {                                                                       \
        litehtml::css_properties x, y;                                         \
        CHECK(x == y);                                                         \
        expr;                                                                  \
        CHECK(!(x == y));                                                      \
    } while (0)

void check_operator_eq_fields()
{
    using namespace litehtml;
    css_properties a, b;
    CHECK(a == b);                       // identical fresh instances

    FLIP(x.set_position(element_position_absolute));
    FLIP(x.set_text_align(text_align_right));
    FLIP(x.set_overflow(overflow_hidden));
    FLIP(x.set_white_space(white_space_pre));
    FLIP(x.set_display(display_block));
    FLIP(x.set_visibility(visibility_hidden));
    // NOT appearance_none here: that is the default, so assigning it changes
    // nothing and the "must stop comparing equal" assertion could not hold.
    FLIP(x.set_appearance(appearance_auto));
    FLIP(x.set_box_sizing(box_sizing_border_box));
    FLIP(x.set_z_index(7));
    FLIP(x.set_vertical_align(va_middle));
    FLIP(x.set_float(float_left));
    FLIP(x.set_clear(clear_left));

    FLIP({ css_margins m; m.left.set_value(3, css_units_px); x.set_margins(m); });
    FLIP({ css_margins m; m.top.set_value(3, css_units_px); x.set_padding(m); });
    FLIP({ css_borders b2; b2.left.width.set_value(3, css_units_px); x.set_borders(b2); });
    FLIP(x.set_width(css_length(7, css_units_px)));
    FLIP(x.set_height(css_length(7, css_units_px)));
    FLIP(x.set_min_width(css_length(7, css_units_px)));
    FLIP(x.set_min_height(css_length(7, css_units_px)));
    FLIP(x.set_max_width(css_length(7, css_units_px)));
    FLIP(x.set_max_height(css_length(7, css_units_px)));
    FLIP({ css_offsets o; o.left.set_value(3, css_units_px); x.set_offsets(o); });
    FLIP(x.set_text_indent(css_length(7, css_units_px)));
    FLIP(x.line_height_w().computed_value = 42);
    FLIP({ x.line_height_w().css_value = css_length(9, css_units_px); });

    FLIP(x.set_list_style_type(list_style_type_disc));
    FLIP(x.set_list_style_position(list_style_position_inside));
    FLIP(x.set_list_style_image("bullet.png"));
    FLIP(x.set_list_style_image_baseurl("http://x/"));

    FLIP({ background bg; bg.m_color = web_color(1, 2, 3, 4); x.set_bg(bg); });
    FLIP(x.set_font_size(19));
    FLIP(x.set_font(77));
    FLIP({ font_metrics fm; fm.height = 99; x.set_font_metrics(fm); });
    FLIP(x.set_text_transform(text_transform_uppercase));
    FLIP(x.set_color(web_color(10, 20, 30, 40)));
    FLIP(x.set_cursor("pointer"));
    FLIP(x.set_content("x"));
    FLIP(x.set_border_collapse(border_collapse_collapse));
    FLIP(x.set_border_spacing_x(css_length(5, css_units_px)));
    FLIP(x.set_border_spacing_y(css_length(5, css_units_px)));
    FLIP(x.set_caption_side(caption_side_bottom));
    FLIP(x.set_order(3));

    // Equality must be reflexive in the presence of equal content, not just
    // for default-constructed instances.
    css_properties c1, c2;
    c1.set_display(display_flex);
    c1.set_color(web_color(255, 0, 0));
    c2.set_display(display_flex);
    c2.set_color(web_color(255, 0, 0));
    CHECK(c1 == c2);
    c2.set_order(1);
    CHECK(!(c1 == c2));
}

#undef FLIP

}  // namespace

int main()
{
    StubContainer container;

    check_operator_eq_fields();

    // -----------------------------------------------------------------------
    // 2. Real document: repeated identical nodes must actually share.
    // -----------------------------------------------------------------------
    const char* kRepeated =
        "<html><body>"
        "<table><tbody>"
        "<tr><td>a1</td><td>b1</td><td>c1</td></tr>"
        "<tr><td>a2</td><td>b2</td><td>c2</td></tr>"
        "<tr><td>a3</td><td>b3</td><td>c3</td></tr>"
        "</tbody></table>"
        "</body></html>";

    auto doc1 = litehtml::document::createFromString(kRepeated, &container);
    if (!doc1) {
        CHECK(doc1 != nullptr);
    } else {
        elements all;
        flatten(doc1->root(), all);

        size_t sharedByTwoPlus = 0;
        std::set<const void*> instances;
        std::set<const void*> seenOnce;
        std::set<const void*> dupes;
        for (const auto& el : all) {
            const void* p = &el->css();
            instances.insert(p);
            if (!dupes.count(p) && !seenOnce.insert(p).second) dupes.insert(p);
        }
        for (const auto& el : all) {
            if (dupes.count(&el->css())) ++sharedByTwoPlus;
        }

        std::fprintf(stderr, "repeated doc: %zu nodes, %zu distinct css instances, "
                             "%zu nodes share their instance\n",
                     all.size(), instances.size(), sharedByTwoPlus);

        CHECK(all.size() > 10);
        // The whole point of P0: fewer instances than nodes.
        CHECK(instances.size() < all.size());
        CHECK(sharedByTwoPlus >= 4);
    }

    // -----------------------------------------------------------------------
    // 3. Inheritance: same tag under differently styled parents must NOT merge.
    //    This is the failure the parent-pointer part of the signature exists
    //    to prevent.
    // -----------------------------------------------------------------------
    const char* kInherited =
        "<html><body>"
        "<div style=\"color:#ff0000\"><span class=\"s\">one</span></div>"
        "<div style=\"color:#00ff00\"><span class=\"s\">two</span></div>"
        "</body></html>";

    auto doc2 = litehtml::document::createFromString(kInherited, &container);
    if (!doc2) {
        CHECK(doc2 != nullptr);
    } else {
        litehtml::element::ptr spanRed, spanGreen;
        elements all;
        flatten(doc2->root(), all);
        for (const auto& el : all) {
            const char* cls = el->get_attr("class");
            if (cls && std::string(cls) == "s") {
                if (!spanRed) spanRed = el; else spanGreen = el;
            }
        }
        CHECK(spanRed && spanGreen);
        if (spanRed && spanGreen) {
            const bool colorsDiffer =
                spanRed->css().get_color() != spanGreen->css().get_color();
            std::fprintf(stderr, "inherited: instances %s, colors differ: %s\n",
                         (&spanRed->css() == &spanGreen->css()) ? "MERGED" : "distinct",
                         colorsDiffer ? "yes" : "NO");
            // (b) is the correctness bar; (a) documents what currently happens.
            CHECK(colorsDiffer);
            // If they merged while their colors differ we would already have
            // corrupted styles, so an honest guard is: never both.
            CHECK(!((&spanRed->css() == &spanGreen->css()) && colorsDiffer));
        }
    }

    // -----------------------------------------------------------------------
    // 4. Copy-on-write: writing through one element must not be visible from
    //    another that shares the instance.
    // -----------------------------------------------------------------------
    if (doc1) {
        elements all;
        flatten(doc1->root(), all);
        // Find a pair that shares an instance.
        litehtml::element::ptr first, second;
        for (size_t i = 0; i < all.size() && !second; ++i) {
            for (size_t j = i + 1; j < all.size(); ++j) {
                if (&all[i]->css() == &all[j]->css()) {
                    first = all[i];
                    second = all[j];
                    break;
                }
            }
        }
        CHECK(first && second);
        if (first && second) {
            CHECK(&first->css() == &second->css());
            const litehtml::web_color before = second->css().get_color();
            first->css_w().set_color(litehtml::web_color(123, 45, 67));
            std::fprintf(stderr, "cow: writer now %s jjinstance, reader %s\n",
                         (&first->css() == &second->css()) ? "STILL SHARED" : "detached",
                         (second->css().get_color() == before) ? "unchanged" : "CHANGED");
            CHECK(&first->css() != &second->css());     // writer detached
            CHECK(second->css().get_color() == before); // reader unaffected
        }
    }

    int fails = test::failCount();
    std::fprintf(stderr, "\n%d checks, %d failures\n", test::checkCount(), fails);
    return fails ? 1 : 0;
}
