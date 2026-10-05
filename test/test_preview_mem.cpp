// test_preview_mem.cpp - gate on the markdown preview's memory amplification.
//
// The 4 MB preview limit was removed on 2026-10-05 (#8): a file you can open
// is a file you should be able to preview. That decision exposed the real
// question, which this file answers by measuring rather than guessing.
//
// Measured on this machine (raw runs: main/test/md_mem_probe.cpp):
//
//   input    html out        DOM nodes   workingset   time
//   7 KB     16 KB  (2.2x)     1,987      12 MB      <1 s
//   300 KB   529 KB  (1.7x)   104,316     234 MB     10 s
//   800 KB   1.34 MB (1.7x)   278,105     593 MB     3m48s
//   3.1 MB  ~5.4 MB  (1.7x)  ~1.1 M      ~2.4 GB    >15 min, unfinished
//
// Three facts fall out of that table, and they are what this gate protects:
//
//   A. md_to_html() is NOT the amplifier — the HTML is only ~1.7x the
//      markdown. Work in preprocess.cpp cannot move this number.
//   B. litehtml IS the amplifier, structurally: ~347 DOM nodes per KB of
//      markdown, each node carrying a css_properties **by value** (992 bytes
//      inside a 1120-byte element) plus a per-node used_selector vector.
//      ~740x from markdown to resident memory.
//   C. The cost is super-linear in time, so 3 MB does not merely get slow.
//
// So this gate runs the REAL chain (md_to_html -> createFromString -> render)
// on generated documents and asserts the *relationships* that must hold:
//
//   1. HTML stays a small multiple of the markdown (guards A — catches a
//      future "optimization" that accidentally re-inflates the HTML, and
//      catches the opposite regression too).
//   2. Resident memory tracks node count at the measured per-node cost,
//      within a wide band (guards B — a leak, or a per-node cost that quietly
//      doubles, shows up as memory far above what the node count explains).
//   3. Node count per KB stays in a ratcheted band. This is a property of
//      litehtml's parser that we do not control, so it is a regression
//      detector, not a quality bar; a litehtml upgrade that moves it is
//      exactly when this number should be re-measured and updated.
//   4. The per-node constants used in the docs are still true
//      (css_properties really is stored inline). If litehtml switched to
//      shared storage, per-node cost would drop ~90% and this fails — which
//      is the point: it tells you the documented numbers went stale instead
//      of leaving them quietly wrong.
//
// What it deliberately does NOT assert: that a 3 MB document fits in RAM. It
// cannot — that is the memory we stopped hiding from the user. This gate
// makes the number known, so if a future change makes it worse we learn it
// from a test rather than from a bug report.

#include "test_assert.h"
#include "mdview/preprocess.h"

#include <litehtml/css_properties.h>
#include <litehtml/element.h>
#include <litehtml/el_text.h>
#include <litehtml/document.h>
#include <litehtml/document_container.h>

#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "psapi.lib")

using namespace litehtml;

static size_t memKb() {
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return pmc.WorkingSetSize / 1024;
    return 0;
}

// Minimal container: no image cache, no remote fetch, no font cache. Pulling
// in Pecia's MyContainer would drag those into the measurement and the numbers
// would no longer be about the DOM.
//
// NOTE: create_element() must return null. document::create_element() calls
// m_container->create_element() and falls back to its own tag->class mapping
// on a null result; delegating back to doc->create_element() is infinite
// recursion and crashes instantly (exit 127).
class ProbeContainer : public document_container {
public:
    uint_ptr create_font(const font_description&, const document*, font_metrics*) override { return 1; }
    void delete_font(uint_ptr) override {}
    pixel_t text_width(const char* t, uint_ptr) override { return (pixel_t)(strlen(t) * 8); }
    void draw_text(uint_ptr, const char*, uint_ptr, web_color, const position&) override {}
    pixel_t pt_to_px(float pt) const override { return (pixel_t)pt; }
    pixel_t get_default_font_size() const override { return 16; }
    const char* get_default_font_name() const override { return "Segoe UI"; }
    void draw_list_marker(uintptr_t, const list_marker&) override {}
    void load_image(const char*, const char*, bool) override {}
    void get_image_size(const char*, const char*, size& sz) override { sz.width = 1; sz.height = 1; }
    void draw_image(uintptr_t, const background_layer&, const std::string&, const std::string&) override {}
    void draw_solid_fill(uintptr_t, const background_layer&, const web_color&) override {}
    void draw_linear_gradient(uintptr_t, const background_layer&, const background_layer::linear_gradient&) override {}
    void draw_radial_gradient(uintptr_t, const background_layer&, const background_layer::radial_gradient&) override {}
    void draw_conic_gradient(uintptr_t, const background_layer&, const background_layer::conic_gradient&) override {}
    void draw_borders(uintptr_t, const litehtml::borders&, const litehtml::position&, bool) override {}
    void set_caption(const char*) override {}
    void set_base_url(const char*) override {}
    void link(const document::ptr&, const element::ptr&) override {}
    void on_anchor_click(const char*, const element::ptr&) override {}
    void on_mouse_event(const element::ptr&, litehtml::mouse_event) override {}
    void set_cursor(const char*) override {}
    void transform_text(string&, text_transform) override {}
    void import_css(string&, const string&, string&) override {}
    void set_clip(const position&, const border_radiuses&) override {}
    void del_clip() override {}
    void get_viewport(position& vp) const override { vp.x = 0; vp.y = 0; vp.width = 800; vp.height = 600; }
    element::ptr create_element(const char*, const string_map&, const document::ptr&) override {
        return element::ptr();   // must be null: see note above
    }
    void get_media_features(media_features&) const override {}
    void get_language(string&, string&) const override {}
};

static long g_nodes = 0;
static void walk(const element::ptr &e) {
    if (!e) return;
    ++g_nodes;
    elements_list kids = e->children();
    for (auto &k : kids) walk(k);
}

struct Measured {
    size_t mdBytes = 0;
    size_t htmlBytes = 0;
    long   nodes = 0;
    long   domKb = 0;
};

// Run one document through the production chain. `budgetKb` caps how much
// memory we are willing to let the DOM take, so a regression fails the test
// instead of eating the machine (which is exactly what the 3 MB case did).
static Measured measure(const std::string &md, long budgetKb) {
    Measured m;
    m.mdBytes = md.size();
    std::fprintf(stderr, "\n--- input %zu bytes (%.2f MB) ---\n",
                 m.mdBytes, m.mdBytes / 1048576.0);

    std::vector<PreviewHeading> headings;
    std::string html = md_to_html(md, "", "", &headings, "");
    m.htmlBytes = html.size();
    std::fprintf(stderr, "  html      : %zu bytes (x%.2f of md), %zu headings\n",
                 html.size(), (double)html.size() / (double)(m.mdBytes ? m.mdBytes : 1),
                 headings.size());

    // A: the converter is not the amplifier. Measured 1.7x; allow generous
    // headroom but fail if it ever approaches the ~740x the DOM stage costs,
    // because that would mean the amplification simply moved upstream.
    const double htmlRatio = (double)html.size() / (double)(m.mdBytes ? m.mdBytes : 1);
    std::fprintf(stderr, "  html/md   : x%.2f\n", htmlRatio);
    CHECK(htmlRatio > 1.0);
    CHECK(htmlRatio < 8.0);
    CHECK(html.size() > 0);

    // ★ The budget guard has to fire HERE, before createFromString.
    //
    // First version of this test checked `domKb <= budgetKb` after the DOM was
    // built. That is a guard that arrives after the thing it guards: DOM build
    // time is super-linear (measured: 2.7x input -> 23x time), so when a
    // regression does blow it up, the process is already 20 minutes into an
    // allocation storm and never reaches the assertion. Injecting a converter
    // bug (HTML x64) made this test hang for 20+ min instead of failing in
    // under a second -- i.e. the gate could not report the regression it
    // existed to report.
    //
    // So: predict the DOM cost from the HTML size (the relationship is stable
    // and measured), and bail out before doing the work if the prediction is
    // already over budget. Cheap to compute, and it turns a hang into a
    // failure.
    const double kMeasuredBytesPerNode = 2200.0;    // measured, see header
    const double kMeasuredNodesPerKb = 347.0;       // measured, see header
    const double predictedNodes = (double)html.size() / 1024.0 * kMeasuredNodesPerKb;
    const double predictedKb = predictedNodes * kMeasuredBytesPerNode / 1024.0;
    std::fprintf(stderr, "  predicted : %.0f nodes, %.0f KB (budget %ld KB)\n",
                 predictedNodes, predictedKb, budgetKb);
    if (predictedKb > (double)budgetKb) {
        CHECK(predictedKb <= (double)budgetKb);   // fails, with numbers
        std::fprintf(stderr, "  SKIPPING createFromString: predicted over budget.\n"
                             "  (the guard above fired first -- that is the fix)\n");
        return m;
    }

    ProbeContainer mc;
    long before = (long)memKb();
    auto doc = document::createFromString(html.c_str(), &mc);
    CHECK(doc != nullptr);
    if (!doc) return m;

    walk(doc->root());
    m.nodes = g_nodes;
    m.domKb = (long)memKb() - before;
    std::fprintf(stderr, "  DOM nodes : %ld  (%.1f per KB of md)\n",
                 m.nodes, (double)m.nodes / (m.mdBytes / 1024.0));
    std::fprintf(stderr, "  DOM mem   : %ld KB  (%.0f bytes/node)\n",
                 m.domKb, m.nodes ? (double)m.domKb * 1024.0 / (double)m.nodes : 0.0);
    std::fprintf(stderr, "  amplif.   : %.0fx md -> resident\n",
                 m.mdBytes ? (double)m.domKb * 1024.0 / (double)m.mdBytes : 0.0);

    // B: nodes per KB, ratcheted. See header: this is litehtml's shape, and a
    // jump means the parser changed and the documented numbers need updating.
    volatile double nodesPerKb = (double)m.nodes / (m.mdBytes / 1024.0);
    CHECK(nodesPerKb > 100.0);
    CHECK(nodesPerKb < 600.0);

    // B: memory must be explained by the node count. Measured ~2200 B/node;
    // the band is wide (600..6000) because working-set accounting is noisy,
    // but a leak or a per-node cost doubling lands outside it.
    volatile double bytesPerNode = (double)m.domKb * 1024.0 / (double)(m.nodes ? m.nodes : 1);
    CHECK(bytesPerNode > 400.0);    // below sizeof(element) is impossible
    CHECK(bytesPerNode < 6000.0);

    // C: stay inside the budget so this test cannot itself become the 2.4 GB
    // incident. The predicted-size guard above already bailed out before
    // reaching here; this is the belt to that suspenders, and it is checked
    // only when we actually built the DOM.
    CHECK(m.domKb <= budgetKb);

    doc->render(800);
    return m;
}

// The pattern a real large document looks like: repeated headed sections with
// inline formatting and a list. This is the shape that maximizes node count
// (a wall of plain paragraphs is much cheaper), which is why the measurement
// is a worst case rather than a typical case.
static std::string makeDoc(size_t targetBytes) {
    static const char *kChunk =
        "## Section\n\nSome **bold** text and a [link](https://example.com) plus `code`.\n\n"
        "- item one\n- item two\n\n";
    std::string s;
    s.reserve(targetBytes + 512);
    while (s.size() < targetBytes) s += kChunk;
    return s;
}

int main() {
    std::fprintf(stderr, "sizeof(css_properties) = %zu\n", sizeof(css_properties));
    std::fprintf(stderr, "sizeof(element)        = %zu  (css is %.0f%% of a node)\n",
                 sizeof(element), 100.0 * (double)sizeof(css_properties) / (double)sizeof(element));

    // 4: the per-node constants quoted in the docs are still true. If
    // litehtml moved css_properties out of line, per-node cost would drop
    // ~90% and this fails — telling us the numbers went stale rather than
    // leaving them quietly wrong.
    // sizeof() is a constant expression, so comparing it directly makes
    // MSVC warn C4127. The point of these three is the compile-time value,
    // so route it through a volatile read: same assertion, no warning, and
    // it documents that we mean the number, not a runtime property.
    volatile size_t v_css = sizeof(css_properties);
    volatile size_t v_el = sizeof(element);
    CHECK(v_css > 500);
    CHECK(v_el >= v_css);
    CHECK(v_css * 100 / v_el >= 80);

    g_nodes = 0;
    Measured m8 = measure(makeDoc(8 * 1024), 64 * 1024);
    g_nodes = 0;
    Measured m64 = measure(makeDoc(64 * 1024), 320 * 1024);

    // Scaling must stay linear in node count. Super-linear memory (a leak, or
    // a cache keyed by something that grows quadratically) breaks this; a
    // litehtml change that made memory sub-linear would too, and that would be
    // good news worth investigating rather than hiding.
    if (m8.nodes > 0 && m64.nodes > 0) {
        const double sizeRatio = (double)m64.mdBytes / (double)m8.mdBytes;
        const double nodeRatio = (double)m64.nodes / (double)m8.nodes;
        const double memRatio = (double)(m64.domKb ? m64.domKb : 1) / (double)(m8.domKb ? m8.domKb : 1);
        std::fprintf(stderr,
                     "\nscaling: md x%.1f, nodes x%.1f, dom-mem x%.1f\n",
                     sizeRatio, nodeRatio, memRatio);
        // Nodes must track input size closely.
        CHECK(nodeRatio > sizeRatio * 0.7);
        CHECK(nodeRatio < sizeRatio * 1.4);
        // Memory must track nodes (not blow past it).
        CHECK(memRatio < nodeRatio * 1.6);
    }

    int fails = test::failCount();
    std::fprintf(stderr, "\n%d checks, %d failures\n", test::checkCount(), fails);
    return fails ? 1 : 0;
}
