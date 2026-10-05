// test_preview_gate.cpp - gates for the markdown preview's "did the text
// change since the last render?" decision, and for the removal of the old
// 4 MB preview limit.
//
// Background (2026-10-05, task #8):
//   refreshPreview() used to bail out with m_preview->clear() when the
//   document was over 4 MB, so a big document produced a silently blank
//   preview panel — indistinguishable from "my markdown is broken".
//   The limit is gone: whatever you can open, you can preview.
//
//   The bail-out existed because of what the code did *before* it: the
//   "unchanged → skip" test kept a full copy of the rendered text
//   (m_lastRenderedMd) and string-compared it against the document, which
//   meant three whole-document allocations per refresh tick (~150 MB and
//   three full copies every 5 s on a 50 MB file, on the UI thread). So the
//   limit was not a policy, it was a symptom.
//
//   The test below pins the *contract* that replaced it:
//     1. the decision is O(1) — it never looks at the document text, only at
//        an identity pair (document pointer, Document::contentRev());
//     2. the decision cannot be fooled by restyle-only buffer callbacks
//        (Document guards those internally, so contentRev must not move);
//     3. the decision still fires when it must — a real edit, a tab switch,
//        a reopen, a forced render;
//     4. a document over the old 4 MB threshold produces a "render" verdict,
//        i.e. it is NOT silently skipped any more.
//
// (4) is deliberately phrased as a verdict, not as a size comparison: the
// constant it used to be gated by is gone, and a test that re-introduces a
// size check would only be asserting a number that no longer means anything.

#include "test_assert.h"
#include "editor/Document.h"
#include "mdview/preview_gate.h"
#include "core/PathUtils.h"   // pathutil::isMarkdownPath   // 生产代码本体，不是复刻

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace fs = std::filesystem;

// The gate under test is PreviewRenderGate — the very class refreshPreview()
// calls. Deliberately NOT a re-implementation: an earlier draft of this file
// carried its own copy of the decision, which meant the "large files are
// silently skipped" behaviour could have come back without turning the test
// red. The extraction into mdview/preview_gate.h is what makes this a gate on
// production code.

static fs::path g_scratch;

static void makeScratch() {
    g_scratch = fs::temp_directory_path() / "pecia_preview_gate";
    fs::remove_all(g_scratch);
    fs::create_directories(g_scratch);
}

static fs::path writeDoc(const char *name, const std::string &text) {
    fs::path p = g_scratch / name;
    FILE *f = nullptr;
    if (fopen_s(&f, p.string().c_str(), "wb") != 0 || !f) return {};
    if (!text.empty()) fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    return p;
}

static void editBuffer(Document &d, const char *text) {
    d.buffer()->text(text);
}

// Convenience: drive the production gate the way refreshPreview() does.
static bool render(Document *d, PreviewRenderGate &g, bool force = false) {
    return g.shouldRender(d, d ? d->contentRev() : 0, force);
}

// ---------------------------------------------------------------------------

static void test_firstRenderAlwaysHappens() {
    // A fresh gate renders on the first call, even for a document that has
    // never been rendered (this is what togglePreview() relies on).
    PreviewRenderGate g;
    Document d;
    editBuffer(d, "# hello");
    CHECK(render(&d, g));
    // Second call with no change: skipped.
    CHECK(!render(&d, g));
}

static void test_realEditTriggersRender() {
    PreviewRenderGate g;
    Document d;
    editBuffer(d, "one");
    CHECK(render(&d, g));
    CHECK(!render(&d, g));

    editBuffer(d, "one two");              // real content change
    CHECK(render(&d, g));
    CHECK(!render(&d, g));
}

static void test_forcedRenderBypassesUnchangedCheck() {
    // The manual Refresh button and the font/scale changes must always
    // re-render even though the text is untouched — otherwise clicking
    // Refresh does nothing visible.
    PreviewRenderGate g;
    Document d;
    editBuffer(d, "stable");
    CHECK(render(&d, g));
    CHECK(!render(&d, g));
    CHECK(render(&d, g, true));            // forced
    CHECK(!render(&d, g));                 // and the force is one-shot
}

static void test_invalidateForcesRender() {
    // Same intent, the way togglePreview() and the font switch express it.
    PreviewRenderGate g;
    Document d;
    editBuffer(d, "stable");
    CHECK(render(&d, g));
    CHECK(!render(&d, g));
    g.invalidate();
    CHECK(render(&d, g));
    CHECK(!render(&d, g));
}

static void test_tabSwitchTriggersRender() {
    // Two tabs holding byte-identical text. The old full-text comparison
    // would have skipped the second one; identity is part of the key, so it
    // must not. (Preview follows the active tab, so skipping here would show
    // the previous tab's content.)
    PreviewRenderGate g;
    Document a, b;
    editBuffer(a, "same text");
    editBuffer(b, "same text");
    CHECK(render(&a, g));
    CHECK(render(&b, g));                  // different document → render
    CHECK(render(&a, g));                  // switching back → render
}

static void test_restyleDoesNotBumpRevision() {
    // Fl_Text_Buffer fires modify callbacks for restyle-only and zero-size
    // notifications. Document::modifyCallback must ignore them, so a
    // re-style (font load, tab distance) cannot cause a spurious full
    // re-render of a large document.
    Document d;
    editBuffer(d, "content");
    const unsigned long long before = d.contentRev();

    d.buffer()->tab_distance(4);            // fires (0, len, len, ...) restyle
    CHECK_EQ(d.contentRev(), before);

    // A whole-buffer replace of equal length but different bytes IS a real
    // change and must move the revision.
    editBuffer(d, "CONTENT");
    CHECK(d.contentRev() != before);
}

static void test_largeDocumentIsNotSkipped() {
    // The regression this whole change exists for: a document over the old
    // 4 MB threshold must still get a render verdict. It used to get
    // "skipped" (blank panel, no explanation).
    makeScratch();
    std::string big;
    big.reserve(6 * 1024 * 1024);
    const std::string chunk = "line of markdown text to bulk this up\n";
    while (big.size() < 6u * 1024 * 1024) big += chunk;
    CHECK(big.size() > 4u * 1024 * 1024);

    fs::path p = writeDoc("big.md", big);
    CHECK(!p.empty());

    Document d;
    CHECK(d.loadFile(p.string().c_str()));
    CHECK(d.buffer()->length() > 4 * 1024 * 1024);   // really over the old cap

    PreviewRenderGate g;
    CHECK(render(&d, g));                           // NOT silently skipped
    // And it still behaves sanely afterwards: no edit → no re-render.
    CHECK(!render(&d, g));
    editBuffer(d, "x");
    CHECK(render(&d, g));
}

static void test_reopenBumpsRevision() {
    // Opening a different file into the same Document must render, even
    // though loading goes through m_buffer->text() — whose modify callback
    // Document deliberately treats as "unchanged" when the length matches.
    makeScratch();
    fs::path p1 = writeDoc("a.md", std::string(4096, 'a'));
    fs::path p2 = writeDoc("b.md", std::string(4096, 'b'));
    CHECK(!p1.empty() && !p2.empty());

    Document d;
    PreviewRenderGate g;
    CHECK(d.loadFile(p1.string().c_str()));
    CHECK(render(&d, g));
    CHECK(!render(&d, g));

    CHECK(d.loadFile(p2.string().c_str()));
    CHECK(render(&d, g));                  // new content → new render
    CHECK(!render(&d, g));
}

static void test_nullDocumentHandled() {
    // Closing the preview or having no tab must not crash the gate, and
    // coming back to a document must render again.
    PreviewRenderGate g;
    Document d;
    editBuffer(d, "x");
    CHECK(render(&d, g));
    CHECK(render(nullptr, g));             // no active tab
    CHECK(!render(nullptr, g));
    CHECK(render(&d, g));                  // back to a document
}

// ---------------------------------------------------------------------------
// Source-level backstop for the removal itself.
//
// The behavioural tests above pin what the gate *does*, but nothing stops
// somebody reintroducing a size cutoff in the caller — and when that happens
// the symptom is a blank panel with no error, which is exactly what made the
// original limit so confusing. So: assert the preview code path contains no
// size-based bail-out at all. This is a "must not come back" check, not a
// restatement of how rendering works.
//
// It reads main/ui/MainWindow_preview.cpp relative to the exe (tests run from
// build/), the same way test_docs finds main/.
// ---------------------------------------------------------------------------
static std::string readFileText(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void test_previewPathHasNoSizeCutoff() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    fs::path exe(buf);
    fs::path mainDir = exe.parent_path().parent_path() / "main";
    fs::path src = mainDir / "ui" / "MainWindow_preview.cpp";
    std::string text = readFileText(src);
    CHECK(!text.empty());
    if (text.empty()) return;   // layout unexpected; don't cascade noise

    // The old guard was `buffer()->length() > kWrapLimitBytes` followed by
    // m_preview->clear(). kWrapLimitBytes is now only meaningful for word
    // wrap, so its mere presence in the preview path means a size gate is
    // back. Also reject the clearer form of the same thing.
    CHECK(text.find("kWrapLimitBytes") == std::string::npos);
    CHECK(text.find("too_large") == std::string::npos);
    CHECK(text.find("tooLarge") == std::string::npos);
    CHECK(text.find("too big") == std::string::npos);
}

// ---------------------------------------------------------------------------
// "Open .md files with preview" (Options > Interface & System) decides whether
// to turn the preview on by looking at the file extension. Pinned here because
// the edges are easy to regress: someone "simplifying" this to ext == ".md"
// would silently drop .markdown, and a naive strrchr('.') would treat the dot
// in a *directory* name as the extension.
// ---------------------------------------------------------------------------
static void test_markdownExtensionDetection() {
    // The two extensions that must work, and case must not matter.
    CHECK(pathutil::isMarkdownPath("notes.md"));
    CHECK(pathutil::isMarkdownPath("notes.markdown"));
    CHECK(pathutil::isMarkdownPath("NOTES.MD"));
    CHECK(pathutil::isMarkdownPath("NOTES.MARKDOWN"));
    CHECK(pathutil::isMarkdownPath("Notes.Md"));

    // Full paths, both separators.
    CHECK(pathutil::isMarkdownPath("C:\\docs\\readme.md"));
    CHECK(pathutil::isMarkdownPath("C:/docs/readme.markdown"));

    // Not markdown.
    CHECK(!pathutil::isMarkdownPath("notes.txt"));
    CHECK(!pathutil::isMarkdownPath("main.cpp"));
    CHECK(!pathutil::isMarkdownPath("a.md.txt"));      // only the last ext counts
    CHECK(!pathutil::isMarkdownPath("markdown"));      // "markdown" is not ".markdown"

    // No extension at all.
    CHECK(!pathutil::isMarkdownPath("README"));
    CHECK(!pathutil::isMarkdownPath("C:\\docs\\README"));
    CHECK(!pathutil::isMarkdownPath(""));

    // A dot in a DIRECTORY name is not an extension.
    CHECK(!pathutil::isMarkdownPath("C:\\my.dir\\notes"));
    CHECK(!pathutil::isMarkdownPath("C:\\my.dir\\notes.txt"));
    // ...but a markdown file inside such a directory still is markdown.
    CHECK(pathutil::isMarkdownPath("C:\\my.dir\\notes.md"));

    // Trailing dot: not an extension.
    CHECK(!pathutil::isMarkdownPath("notes."));
    CHECK(!pathutil::isMarkdownPath("C:\\docs\\notes."));
}

static void runAll() {
    makeScratch();
    test_firstRenderAlwaysHappens();
    test_realEditTriggersRender();
    test_forcedRenderBypassesUnchangedCheck();
    test_invalidateForcesRender();
    test_tabSwitchTriggersRender();
    test_restyleDoesNotBumpRevision();
    test_largeDocumentIsNotSkipped();
    test_reopenBumpsRevision();
    test_nullDocumentHandled();
    test_previewPathHasNoSizeCutoff();
    test_markdownExtensionDetection();
    fs::remove_all(g_scratch);
}

int main() {
    runAll();
    int fails = test::failCount();
    std::fprintf(stderr, "%d checks, %d failures\n",
                 test::checkCount(), fails);
    return fails ? 1 : 0;
}