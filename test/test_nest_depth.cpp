// Deep-nesting safety of the litehtml build that Pecia actually ships (MSVC).
//
// Background
// ----------
// litehtml recurses on nesting depth in several places, with no bound:
//   * src/css_parser.cpp  consume_function() <-> consume_component_value()
//     mutually recurse once per level of `var(..,var(..,..))`;
//   * src/document.cpp    create_node() recurses per DOM level.
// With enough nesting the recursion blows the thread stack and the process
// dies with 0xC00000FD (STATUS_STACK_OVERFLOW) - not an exception the app can
// catch and recover from, so the whole editor goes down.
//
// Why this test exists
// --------------------
// A crash loses whatever the user was editing. Silent mis-rendering would be
// bad enough to deserve a test; an uncatchable process kill deserves one too.
// The fix is to bound the recursion, and this test is what proves the bound
// works and keeps working.
//
// Modes
// -----
//   default            assertions for the depths we guarantee (used by ctest)
//   --scan <kind>      binary-search-ish sweep; each measurement is spawned as
//                      a CHILD PROCESS so that a crash reports instead of
//                      killing the sweep. Diagnostic tool, not part of ctest.

#include "test_assert.h"

#include <litehtml.h>
// gumbo.h is pulled in directly only for the "gumbo_raw" diagnostic variant,
// which parses without building a litehtml tree. That isolates whether a
// remaining overflow happens inside gumbo (parse / destroy) or in litehtml.
// It redefines _CRT_SECURE_NO_WARNINGS unconditionally, so drop the one we get
// from the command line first to keep the build warning-free.
#ifdef _CRT_SECURE_NO_WARNINGS
#undef _CRT_SECURE_NO_WARNINGS
#endif
#include <gumbo.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

// windows.h after litehtml.h, and with the macros that would otherwise collide
// with the standard library turned off.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// ---- container stub -------------------------------------------------------
// Kept deliberately minimal: this measures parsing recursion, not rendering,
// so drawing callbacks do nothing. create_element MUST return nullptr or
// litehtml recursion never terminates (documented trap in this codebase).

class NestContainer : public litehtml::document_container
{
public:
    litehtml::uint_ptr create_font(const litehtml::font_description&,
                                   const litehtml::document*,
                                   litehtml::font_metrics* fm) override
    {
        *fm = litehtml::font_metrics();
        fm->height = 20; fm->ascent = 16; fm->descent = 4; fm->x_height = 10;
        return 0;
    }
    void delete_font(litehtml::uint_ptr) override {}
    litehtml::pixel_t text_width(const char* text, litehtml::uint_ptr) override
    {
        return litehtml::pixel_t((float)(strlen(text) * 8));
    }
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
    void transform_text(litehtml::string& t, litehtml::text_transform tt) override
    {
        if (tt == litehtml::text_transform_uppercase)
            for (auto& c : t) c = (char)toupper((unsigned char)c);
        else if (tt == litehtml::text_transform_lowercase)
            for (auto& c : t) c = (char)tolower((unsigned char)c);
    }
    void import_css(litehtml::string&, const litehtml::string&, litehtml::string&) override {}
    void set_clip(const litehtml::position&, const litehtml::border_radiuses&) override {}
    void del_clip() override {}
    void get_viewport(litehtml::position& vp) const override
    { vp.width = 800; vp.height = 600; }
    litehtml::element::ptr create_element(const char*, const litehtml::string_map&,
                                          const std::shared_ptr<litehtml::document>&) override
    {
        return nullptr;
    }
    void get_media_features(litehtml::media_features& mf) const override
    {
        mf.type = litehtml::media_type_screen;
        mf.width = 800; mf.height = 600;
    }
    void get_language(litehtml::string& l, litehtml::string& c) const override
    { l = "en"; c = ""; }
    litehtml::pixel_t pt_to_px(float pt) const override
    { return litehtml::pixel_t(pt); }
    litehtml::pixel_t get_default_font_size() const override
    { return litehtml::pixel_t(16); }
    const char* get_default_font_name() const override { return "sans-serif"; }
};

// ---- input builders -------------------------------------------------------

namespace {

std::string make_html_div(int depth)
{
    std::string s = "<html><head></head><body>";
    s.reserve(size_t(depth) * 11 + 64);
    for (int i = 0; i < depth; i++) s += "<div>";
    s += "x";
    for (int i = 0; i < depth; i++) s += "</div>";
    s += "</body></html>";
    return s;
}

// Unclosed tags: closer to what reaches us when markdown inlines a chunk of
// hand-written HTML, and exercises gumbo's error-recovery path.
std::string make_html_open(int depth)
{
    std::string s = "<html><head></head><body>";
    s.reserve(size_t(depth) * 12 + 64);
    for (int i = 0; i < depth; i++) s += "<div><span>";
    s += "x</body></html>";
    return s;
}

std::string make_css_nest(const char* fn, int depth)
{
    std::string v;
    v.reserve(size_t(depth) * 12 + 32);
    v = "padding-left:";
    for (int i = 0; i < depth; i++) { v += fn; v += "("; }
    v += "1px";
    for (int i = 0; i < depth; i++) v += ")";
    v += ";";
    std::string s = "<html><head></head><body><div style=\"";
    s += v;
    s += "\">x</div></body></html>";
    return s;
}

std::string build(const char* kind, int depth)
{
    if (!strcmp(kind, "html_div"))  return make_html_div(depth);
    if (!strcmp(kind, "html_open")) return make_html_open(depth);
    if (!strcmp(kind, "css_var"))   return make_css_nest("var", depth);
    if (!strcmp(kind, "css_calc"))  return make_css_nest("calc", depth);
    // Same input as html_div; the difference is what child_main does with it.
    if (!strcmp(kind, "gumbo_raw")) return make_html_div(depth);
    return std::string();
}

// Parse only. Recursion happens here; rendering adds nothing to stack depth
// worth measuring and would just add noise.
bool parse_only(const std::string& html)
{
    NestContainer pc;
    auto doc = litehtml::document::createFromString(html.c_str(), &pc);
    if (doc) doc->render(litehtml::pixel_t(800));
    return true;
}

} // namespace

// ---- child process mode ---------------------------------------------------

static int child_main(const char* kind, int depth)
{
    std::string html = build(kind, depth);
    if (html.empty())
    {
        printf("BAD_KIND %s\n", kind);
        return 2;
    }

    // Diagnostic variant: gumbo only, no litehtml tree. Prints a marker before
    // and after each stage so a crash tells us WHICH stage overflowed. The
    // markers matter: stdout is flushed each time, so whatever printed before
    // the process died is trustworthy.
    if (!strcmp(kind, "gumbo_raw"))
    {
        GumboOptions opts = kGumboDefaultOptions;
        opts.fragment_context = GUMBO_TAG_BODY;
        printf("STAGE parse_begin html=%zu\n", html.size());
        fflush(stdout);
        GumboOutput* out = gumbo_parse_with_options(&opts, html.c_str(),
                                                   strlen(html.c_str()));
        printf("STAGE parse_done\n");
        fflush(stdout);
        gumbo_destroy_output(&kGumboDefaultOptions, out);
        printf("STAGE destroy_done\n");
        fflush(stdout);
        return 0;
    }

    parse_only(html);
    printf("OK %s depth=%d html=%zu\n", kind, depth, html.size());
    fflush(stdout);
    return 0;
}

// ---- scan mode ------------------------------------------------------------

static DWORD spawn(const char* kind, int depth, DWORD timeout_ms, bool* timed_out)
{
    char cmd[512];
    char self[MAX_PATH];
    GetModuleFileNameA(NULL, self, MAX_PATH);
    snprintf(cmd, sizeof(cmd), "\"%s\" __child %s %d", self, kind, depth);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    if (timed_out) *timed_out = false;

    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        printf("  depth=%-7d SPAWN_FAILED\n", depth);
        return 0xFFFFFFFF;
    }
    DWORD wait = WaitForSingleObject(pi.hProcess, timeout_ms);
    DWORD code = 0;
    if (wait == WAIT_TIMEOUT)
    {
        if (timed_out) *timed_out = true;
        TerminateProcess(pi.hProcess, 0xFFFFFFFD);
        code = 0xFFFFFFFD;
    }
    else if (!GetExitCodeProcess(pi.hProcess, &code))
    {
        code = 0xFFFFFFFE;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

static const char* status_name(DWORD code)
{
    switch (code)
    {
    case 0:                return "ok";
    case 0xC00000FD:       return "STACK_OVERFLOW";
    case 0xC0000005:       return "ACCESS_VIOLATION";
    case 0xC0000374:       return "HEAP_CORRUPTION";
    case 0xC0000409:       return "STACK_BUFFER_OVERRUN";
    default:               return "?";
    }
}

static void scan(const char* kind)
{
    const int steps[] = { 100, 200, 400, 600, 800, 1000, 1500, 2000, 3000,
                          4000, 6000, 8000, 16000, 32000, 64000 };
    printf("scan kind=%s (each depth runs in a fresh child)\n", kind);
    for (int d : steps)
    {
        bool to = false;
        DWORD code = spawn(kind, d, 20000, &to);
        printf("  depth=%-7d exit=0x%08X %s%s\n", d, code, status_name(code),
               to ? " (timeout)" : "");
        fflush(stdout);
    }
}

// ---- assertion mode -------------------------------------------------------
//
// Each case runs in a CHILD process. Two reasons, both learned the hard way:
//   1. STATUS_STACK_OVERFLOW cannot be recovered from - even when SEH catches
//      the first-chance exception the stack is already gone, so an in-process
//      run would either die outright or report nothing useful.
//   2. It also avoids MSVC C2712 ("cannot use __try in functions that require
//      object unwinding") entirely, since nothing here needs a destructor.
// The cost is one process spawn per case, which is microseconds next to the
// parse being measured.

int main(int argc, char** argv)
{
    if (argc >= 2 && !strcmp(argv[1], "__child"))
    {
        if (argc < 4) return 2;
        return child_main(argv[2], atoi(argv[3]));
    }
    if (argc >= 2 && !strcmp(argv[1], "--scan"))
    {
        if (argc < 3) { printf("usage: %s --scan <kind> [kind2]\n", argv[0]); return 2; }
        scan(argv[2]);
        if (argc > 3) scan(argv[3]);
        return 0;
    }

    // Guaranteed-safe depths. Well past anything a real document produces
    // (nested <div>s rarely exceed a few dozen levels), and chosen to sit
    // above the depth where an UNGUARDED build overflows - that is what makes
    // this a real gate rather than a tautology: remove the bound in litehtml
    // and these cases start dying with 0xC00000FD.
    struct { const char* kind; int depth; } cases[] =
    {
        { "html_div",  5000 },
        { "html_open", 5000 },
        { "css_var",   5000 },
        { "css_calc",  5000 },
    };

    bool all_ok = true;
    for (auto& c : cases)
    {
        bool to = false;
        DWORD code = spawn(c.kind, c.depth, 60000, &to);
        if (code == 0)
        {
            printf("ok   %s depth=%d survived\n", c.kind, c.depth);
        }
        else
        {
            all_ok = false;
            printf("FAIL %s depth=%d exit=0x%08X %s%s\n",
                   c.kind, c.depth, code, status_name(code),
                   to ? " (timeout)" : "");
        }
        fflush(stdout);
    }

    CHECK(all_ok);

    // CHECK only records; the exit code is what ctest reads. Returning 0 after
    // a failed CHECK is the classic false-green: the "FAIL" lines scroll by in
    // the log and the suite still reports success.
    if (test::failCount() != 0)
    {
        fprintf(stderr, "%d checks, %d failures\n",
                test::checkCount(), test::failCount());
        return 1;
    }
    printf("all nest-depth cases survived (%d checks)\n", test::checkCount());
    return 0;
}
