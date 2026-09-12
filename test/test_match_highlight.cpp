// test_match_highlight.cpp - lock-in tests for ui/MatchHighlight + the
// Editor's unified style rendering. Verifies that matches get style 'D'
// at the right byte ranges, non-matches stay 'A', the empty needle marks
// nothing, URLs get 'C' (plain underline) / 'E' (underline + line
// highlight) / 'F' (underline + match bg), and clearing restores 'A'.
#include "test_assert.h"
#include "ui/MatchHighlight.h"
#include "editor/Editor.h"

#include <FL/Fl_Text_Buffer.H>

#include <cstring>

// Editor needs a SettingsProvider for URL detection; a null provider
// means "URL detection enabled" (updateUrlHighlight only skips when a
// provider exists AND says off).
static Editor *makeEditor(const char *text) {
    auto *ed = new Editor(0, 0, 400, 200);
    auto *buf = new Fl_Text_Buffer(64);
    buf->text(text);
    ed->buffer(buf);
    return ed;
}

static void test_marks_matches() {
    Editor *ed = makeEditor("cat category scatter cat");
    MatchHighlight h;
    // Match "cat" whole-text (no whole-word filter here): positions
    // 0-2, 4-6 (inside "category"), 13-15 (inside "scatter"), 21-23.
    bool applied = h.highlightAll(ed, "cat", 3, false);
    CHECK(applied);
    CHECK_EQ((int)h.ranges().size(), 4);
    const char *s = ed->debugStyleString();
    CHECK_EQ((int)strlen(s), 24);
    // style 'D' at every "cat" byte, 'A' elsewhere
    CHECK(s[0] == 'D' && s[1] == 'D' && s[2] == 'D');      // cat@0
    CHECK(s[3] == 'A');                                    // ' '
    CHECK(s[4] == 'D' && s[5] == 'D' && s[6] == 'D');      // cat inside category
    CHECK(s[7] == 'A');
    CHECK(s[21] == 'D' && s[22] == 'D' && s[23] == 'D');   // cat@21
    CHECK(s[10] == 'A');                                   // plain text
    delete ed;
}

static void test_case_sensitive() {
    Editor *ed = makeEditor("Cat cat");
    MatchHighlight h;
    bool applied = h.highlightAll(ed, "cat", 3, true);   // case-sensitive
    CHECK(applied);
    CHECK_EQ((int)h.ranges().size(), 1);
    const char *s = ed->debugStyleString();
    // "Cat"@0 must NOT match (uppercase C), "cat"@4 must
    CHECK(s[0] == 'A');
    CHECK(s[4] == 'D' && s[5] == 'D' && s[6] == 'D');
    delete ed;
}

static void test_empty_needle_marks_nothing() {
    Editor *ed = makeEditor("hello world");
    MatchHighlight h;
    bool applied = h.highlightAll(ed, nullptr, 0, false);
    CHECK(applied);   // buffer small, highlight proceeds with no matches
    CHECK(h.ranges().empty());
    const char *s = ed->debugStyleString();
    CHECK_EQ((int)strlen(s), 11);
    for (int i = 0; i < 11; ++i) CHECK(s[i] == 'A');
    delete ed;
}

static void test_url_styles() {
    // "https://a https://b" with the line highlight on (cursor at 0):
    // first URL gets 'E' (underline + line bg), the space 'B', and the
    // second URL (inside a match) gets 'F' (underline + match bg).
    Editor *ed = makeEditor("https://a https://b");
    ed->setLineHighlight(true);
    MatchHighlight h;
    CHECK(h.highlightAll(ed, "https://b", 9, false));
    const char *s = ed->debugStyleString();
    CHECK_EQ((int)strlen(s), 19);
    for (int i = 0; i < 9; ++i) CHECK(s[i] == 'E');
    CHECK(s[9] == 'B');                                  // space (current line)
    for (int i = 10; i < 19; ++i) CHECK(s[i] == 'F');
    delete ed;
}

static void test_cursor_leaves_url_line_keeps_underline() {
    // Regression: after the cursor moves to a new line, the URL on the
    // line it left must keep its underline (updateLineHighlight used to
    // revert the whole old line to 'A', wiping URL styles for good).
    Editor *ed = makeEditor("https://a\nx");
    ed->setLineHighlight(true);   // full rebuild: URL on line 0 is 'E'
    const char *s = ed->debugStyleString();
    for (int i = 0; i < 9; ++i) CHECK(s[i] == 'E');
    CHECK(s[9] == 'A');   // the newline's own style
    CHECK(s[10] == 'A');  // 'x' is on line 1, not the current line
    // Move the cursor to line 1 and sync like typing Enter would.
    ed->insert_position(11);
    ed->updateLineHighlight();
    s = ed->debugStyleString();
    // URL is no longer on the current line: plain 'C' underline stays.
    for (int i = 0; i < 9; ++i) CHECK(s[i] == 'C');
    CHECK(s[9] == 'A');
    CHECK(s[10] == 'B');
    delete ed;
}

static void test_clear_restores_styles() {
    Editor *ed = makeEditor("cat");
    MatchHighlight h;
    CHECK(h.highlightAll(ed, "cat", 3, false));
    CHECK(h.active());
    CHECK(ed->debugStyleString()[0] == 'D');
    h.clear(ed);
    CHECK(!h.active());
    CHECK(ed->debugStyleString()[0] == 'A');
    delete ed;
}

int main() {
    test_marks_matches();
    test_case_sensitive();
    test_empty_needle_marks_nothing();
    test_url_styles();
    test_cursor_leaves_url_line_keeps_underline();
    test_clear_restores_styles();
    int fails = test::failCount();
    std::fprintf(stderr, "%d checks, %d failures\n",
                 test::checkCount(), fails);
    return fails ? 1 : 0;
}
