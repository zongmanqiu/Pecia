// test_lua_engine.cpp - unit tests for the embedded Lua sandbox:
// sandbox restrictions, get_selection/replace_selection semantics,
// NUL-byte handling, error paths, and the single-undo apply behavior.
// Links LuaEngine.cpp + lua_static.
#include "test_assert.h"
#include "script/LuaEngine.h"

#include <FL/Fl_Text_Buffer.H>

#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

struct Fixture {
    Fl_Text_Buffer buf;
    LuaEditorHost host;
    LuaEngine engine;

    Fixture(const char *text, int cursor = 0) : buf(64) {
        if (text) buf.insert(0, text);
        host.buffer = &buf;
        host.cursorPos = cursor;
        host.selStart = host.selEnd = 0;
        host.hasSelection = false;
    }
    std::string text() const {
        char *t = buf.text();
        std::string s = t ? t : "";
        if (t) free(t);
        return s;
    }
};

static bool runOk(LuaEngine &e, LuaEditorHost &h, const char *script,
                  std::string *out = nullptr, std::string *err = nullptr) {
    return e.run(script, h, out, err);
}

// ---------------------------------------------------------------------------
// standard library availability (industry-standard trust model: all libs
// are open, matching Lite XL / Loom / SciTE)
// ---------------------------------------------------------------------------

static void test_stdlib_io_ok() {
    Fixture f("hello");
    std::string err;
    CHECK(runOk(f.engine, f.host, "local f = io.open('_pecia_test_tmp.txt', 'w') if f then f:write('x') f:close() end", nullptr, &err));
    if (!err.empty()) fprintf(stderr, "io test err: %s\n", err.c_str());
}

static void test_stdlib_require_ok() {
    Fixture f("hello");
    std::string err;
    CHECK(runOk(f.engine, f.host, "local m = require('string')", nullptr, &err));
    if (!err.empty()) fprintf(stderr, "require test err: %s\n", err.c_str());
}

static void test_stdlib_execute_ok() {
    Fixture f("hello");
    std::string err;
    CHECK(runOk(f.engine, f.host, "local ok, code = os.execute('') -- empty cmd, harmless", nullptr, &err));
    if (!err.empty()) fprintf(stderr, "os.execute test err: %s\n", err.c_str());
}

static void test_stdlib_loadfile_ok() {
    Fixture f("hello");
    std::string err;
    CHECK(runOk(f.engine, f.host, "local fn, e = loadfile('no_such_file_xyz.lua') -- returns nil+err, no crash", nullptr, &err));
    if (!err.empty()) fprintf(stderr, "loadfile test err: %s\n", err.c_str());
}

static void test_stdlib_os_time_ok() {
    Fixture f("hello");
    CHECK(runOk(f.engine, f.host, "local t = os.time()"));
}

static void test_stdlib_math_ok() {
    Fixture f("hello");
    CHECK(runOk(f.engine, f.host, "local x = math.floor(1.7)"));
}

static void test_stdlib_utf8_ok() {
    Fixture f("hello");
    CHECK(runOk(f.engine, f.host, "local n = utf8.len('中文')"));
}

// ---------------------------------------------------------------------------
// win table (user-interaction helpers) - verify registration only, never
// actually pop dialogs / touch the clipboard in unit tests.
// ---------------------------------------------------------------------------

static void test_win_table_registered() {
    Fixture f("hello");
    std::string out;
    CHECK(runOk(f.engine, f.host,
                "local t = type(win) print(t)", &out));
    CHECK(out.find("table") != std::string::npos);
}

static void test_win_functions_registered() {
    Fixture f("hello");
    std::string out;
    CHECK(runOk(f.engine, f.host,
                "print(type(win.message), type(win.question), type(win.clipboard_get), type(win.clipboard_set), type(win.open))",
                &out));
    CHECK(out.find("function") != std::string::npos);
    // All five entries must be functions.
    int count = 0;
    size_t pos = 0;
    while ((pos = out.find("function", pos)) != std::string::npos) { ++count; pos += 8; }
    CHECK_EQ(count, 5);
}

// ---------------------------------------------------------------------------
// selection / replacement semantics
// ---------------------------------------------------------------------------

static void test_replace_selection() {
    Fixture f("hello world", 5);
    f.buf.select(0, 5);                       // select "hello"
    f.host.hasSelection = true;
    f.host.selStart = 0; f.host.selEnd = 5;
    CHECK(runOk(f.engine, f.host, "editor:replace_selection('bye')"));
    CHECK(f.text() == "bye world");
    CHECK_EQ(f.host.cursorPos, 3);            // cursor after "bye"
    CHECK(f.host.hasSelection);
    CHECK_EQ(f.host.selStart, 0);
    CHECK_EQ(f.host.selEnd, 3);
}

static void test_insert_at_cursor() {
    Fixture f("hello", 0);
    CHECK(runOk(f.engine, f.host, "editor:replace_selection('>')"));
    CHECK(f.text() == ">hello");
    CHECK_EQ(f.host.cursorPos, 1);
}

static void test_get_selection_empty() {
    Fixture f("hello");
    std::string out;
    CHECK(runOk(f.engine, f.host,
                "local s = editor:get_selection() print(#s)", &out));
    CHECK(out.find("0") != std::string::npos);
}

static void test_get_selection_text() {
    Fixture f("hello world", 5);
    f.buf.select(6, 11);                      // select "world"
    f.host.hasSelection = true;
    f.host.selStart = 6; f.host.selEnd = 11;
    std::string out;
    CHECK(runOk(f.engine, f.host,
                "local s = editor:get_selection() print(s)", &out));
    CHECK(out.find("world") != std::string::npos);
}

// ---------------------------------------------------------------------------
// cursor API (used by the wrapping-pair scripts: insert an empty pair
// and leave the cursor between the two halves)
// ---------------------------------------------------------------------------

static void test_get_cursor() {
    Fixture f("hello", 3);
    std::string out;
    CHECK(runOk(f.engine, f.host,
                "print(editor:get_cursor())", &out));
    CHECK(out.find("3") != std::string::npos);
}

static void test_set_cursor() {
    Fixture f("hello");
    CHECK(runOk(f.engine, f.host,
                "editor:set_cursor(2)"));
    CHECK_EQ(f.host.cursorPos, 2);
}

static void test_set_cursor_clears_selection() {
    // Moving the cursor cancels the selection (editor semantics): after
    // insert-pair + set_cursor the pair is NOT selected, so typing
    // inserts inside it instead of replacing it.
    Fixture f("hello", 0);
    CHECK(runOk(f.engine, f.host,
                "editor:replace_selection('()')"
                "editor:set_cursor(1)"));
    CHECK(f.text() == "()hello");
    CHECK_EQ(f.host.cursorPos, 1);
    CHECK(!f.host.hasSelection);
}

static void test_set_cursor_clamped() {
    Fixture f("hello");
    // Out-of-range values clamp to the buffer length.
    CHECK(runOk(f.engine, f.host,
                "editor:set_cursor(9999)"));
    CHECK_EQ(f.host.cursorPos, 5);
    CHECK(runOk(f.engine, f.host,
                "editor:set_cursor(-5)"));
    CHECK_EQ(f.host.cursorPos, 0);
}

// The wrapping-pair pattern: with no selection, insert an empty pair
// and put the cursor between the halves (offset = cursor + 1 byte);
// set_cursor also clears the selection so typing goes inside the pair.
static void test_insert_pair_and_place_cursor() {
    Fixture f("hello", 0);
    CHECK(runOk(f.engine, f.host,
                "local c = editor:get_cursor()"
                "editor:replace_selection('()')"
                "editor:set_cursor(c + 1)"));
    CHECK(f.text() == "()hello");
    CHECK_EQ(f.host.cursorPos, 1);   // between '(' and ')'
    CHECK(!f.host.hasSelection);
}

// ---------------------------------------------------------------------------
// NUL-byte handling (regression: strlen truncation)
// ---------------------------------------------------------------------------

static void test_replace_with_nul() {
    Fixture f("hello");
    // text()-based apply (FLTK 1.4.5 undo-safety fix) truncates at the
    // first NUL, like any C-string buffer API. Embedded NULs in script
    // output are not preserved - acceptable for a text editor (binary
    // files are out of scope).
    CHECK(runOk(f.engine, f.host,
                "editor:replace_selection('a'..string.char(0)..'b')"));
    CHECK_EQ(f.buf.length(), 1);   // "a" only; NUL+rest truncated
    CHECK(strcmp(f.buf.text(), "a") == 0);
}

static void test_get_selection_with_nul() {
    Fixture f("a\0bhello");
    f.buf.select(0, 3);                       // select "a\0b"
    f.host.hasSelection = true;
    f.host.selStart = 0; f.host.selEnd = 3;
    std::string out;
    CHECK(runOk(f.engine, f.host,
                "local s = editor:get_selection() print(#s)", &out));
    // print uses luaL_tolstring -> C-string, so #s must come from the
    // length argument, not strlen. Expect "3".
    CHECK(out.find("3") != std::string::npos);
}

// ---------------------------------------------------------------------------
// error paths
// ---------------------------------------------------------------------------

static void test_syntax_error() {
    Fixture f("hello");
    std::string err;
    CHECK(!runOk(f.engine, f.host, "this is not lua", nullptr, &err));
    CHECK(!err.empty());
    // Buffer must be untouched after a failed run.
    CHECK(f.text() == "hello");
}

static void test_runtime_error_keeps_buffer() {
    Fixture f("hello");
    std::string err;
    CHECK(!runOk(f.engine, f.host, "editor:replace_selection('x') error('boom')", nullptr, &err));
    CHECK(!err.empty());
    // replace happened on scratch, then error -> scratch discarded.
    CHECK(f.text() == "hello");
}

// ---------------------------------------------------------------------------
// single-undo apply
// ---------------------------------------------------------------------------

static void test_changed_applies_once() {
    Fixture f("hello");
    // No selection: replace_selection inserts at cursor (0) -> "byehello".
    CHECK(runOk(f.engine, f.host, "editor:replace_selection('bye')"));
    CHECK(f.text() == "byehello");
}

static void test_noop_keeps_text() {
    Fixture f("hello");
    CHECK(runOk(f.engine, f.host, "local x = 1 + 1"));
    CHECK(f.text() == "hello");
}

// ---------------------------------------------------------------------------
// undo behavior (regression: Lua runs must be undoable as ONE undo
// transaction - FLTK_PATCHES.md Patch 6 - and undo must never corrupt
// the undo stack / hang)
// ---------------------------------------------------------------------------

static void test_undo_after_lua_run() {
    Fixture f("abc def ghi", 11);
    f.buf.select(0, 11);
    f.host.hasSelection = true;
    f.host.selStart = 0; f.host.selEnd = 11;
    CHECK(runOk(f.engine, f.host,
                "local s = editor:get_selection() editor:replace_selection('['..s..']')"));
    CHECK(f.text() == "[abc def ghi]");

    // PATCHED by Pecia (FLTK_PATCHES.md Patch 6): a Lua script applies
    // as ONE undo transaction - one Ctrl+Z reverts it fully.
    CHECK(f.buf.can_undo());
    int cursor = 0;
    CHECK(f.buf.undo(&cursor) == 1);
    CHECK(f.text() == "abc def ghi");   // whole script reverted, no hang
}

static void test_multi_lua_run_undo_no_hang() {
    Fixture f("hello world hello world hello world", 0);
    // Run the script 3 times - each actual edit is one undo transaction.
    for (int i = 0; i < 3; ++i) {
        CHECK(runOk(f.engine, f.host, "editor:replace_selection('PAIR')"));
    }
    // The first run changed the text (1 transaction); the second/third
    // runs produce identical text so nothing is written back.
    int steps = 0;
    while (f.buf.can_undo() && steps < 30) {
        int cursor = 0;
        if (f.buf.undo(&cursor) == 0) break;
        ++steps;
        fprintf(stderr, "multi-run step%d: '%s'\n", steps, f.text().c_str());
        if (f.text() == "hello world hello world hello world") break;
    }
    CHECK(steps >= 1);   // the script run is undoable
    CHECK(f.text() == "hello world hello world hello world");
    fprintf(stderr, "multi-run: steps=%d text='%s'\n", steps, f.text().c_str());
    CHECK(steps < 30);   // no infinite loop, no corruption
}

// EXACT user reproduction: type several chars, run one Lua pair script,
// undo twice. With Patch 6 the first undo reverts the whole script,
// the second reverts the typed chars - never hangs, never wipes.
static void test_user_repro_typed_chars_then_lua_then_double_undo() {
    Fl_Text_Buffer buf(64);
    buf.insert(0, "a", 1);
    buf.insert(1, "b", 1);
    buf.insert(2, "c", 1);

    LuaEditorHost host;
    host.buffer = &buf;
    host.cursorPos = 3;
    host.selStart = 0; host.selEnd = 3;
    host.hasSelection = true;
    LuaEngine engine;
    CHECK(engine.run(
        "local s = editor:get_selection() editor:replace_selection('['..s..']')",
        host, nullptr, nullptr));
    CHECK_EQ(buf.length(), 5);   // "[abc]"

    // After the Lua edit the undo stack holds exactly one transaction.
    CHECK(buf.can_undo());

    // Ctrl+Z #1: reverts the whole script - must not hang.
    int cursor = 0;
    int r = buf.undo(&cursor);
    CHECK(r == 1);
    CHECK_EQ(buf.length(), 3);   // back to "abc"
    CHECK(strcmp(buf.text(), "abc") == 0);

    // Ctrl+Z #2: reverts the typed characters (history intact).
    r = buf.undo(&cursor);
    CHECK(r == 1);
    CHECK_EQ(buf.length(), 0);   // fully reverted
    fprintf(stderr, "after undo2 len=%d text='%s' r=%d\n", buf.length(), buf.text(), r);
    CHECK(buf.length() >= 0);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

// ==========================================================================
// Smoke tests for new Textus-compatible API: input, return, insert, rex
// ==========================================================================

static void test_input_global() {
    // `input` should contain the selected text; return replaces the selection
    Fixture f("hello world");
    f.host.hasSelection = true;
    f.host.selStart = 0;
    f.host.selEnd = 5;
    std::string err;
    CHECK(runOk(f.engine, f.host, "return string.upper(input)", nullptr, &err));
    CHECK(f.text() == "HELLO world");
    if (!err.empty()) fprintf(stderr, "input test err: %s\n", err.c_str());
}

static void test_input_empty_when_no_selection() {
    Fixture f("hello");
    std::string err;
    CHECK(runOk(f.engine, f.host, "return input", nullptr, &err));
    // No selection: input="", return "" replaces entire document (Textus convention)
    CHECK(f.text() == "");
    if (!err.empty()) fprintf(stderr, "input empty test err: %s\n", err.c_str());
}

static void test_return_replaces_selection() {
    Fixture f("hello world");
    f.host.hasSelection = true;
    f.host.selStart = 0;
    f.host.selEnd = 5;
    std::string err;
    CHECK(runOk(f.engine, f.host, "return 'HELLO'", nullptr, &err));
    CHECK(f.text() == "HELLO world");
    if (!err.empty()) fprintf(stderr, "return replace test err: %s\n", err.c_str());
}

static void test_return_inserts_when_no_selection() {
    Fixture f("hello");
    std::string err;
    CHECK(runOk(f.engine, f.host, "return ' world'", nullptr, &err));
    // No selection: return replaces entire document (Textus convention)
    CHECK(f.text() == " world");
    if (!err.empty()) fprintf(stderr, "return insert test err: %s\n", err.c_str());
}

static void test_insert_function() {
    Fixture f("hello");
    f.host.cursorPos = 5;
    std::string err;
    CHECK(runOk(f.engine, f.host, "insert(' world')", nullptr, &err));
    CHECK(f.text() == "hello world");
    if (!err.empty()) fprintf(stderr, "insert test err: %s\n", err.c_str());
}

static void test_insert_with_negative_offset() {
    Fixture f("ab");
    f.host.cursorPos = 0;
    std::string err;
    // Insert "[]()" at cursor, then move cursor back 3 (inside brackets)
    CHECK(runOk(f.engine, f.host, "insert('[]()', -3)", nullptr, &err));
    CHECK(f.text() == "[]()ab");
    // Cursor should be at position 1 (after '[')
    CHECK(f.host.cursorPos == 1);
    if (!err.empty()) fprintf(stderr, "insert offset test err: %s\n", err.c_str());
}

static void test_insert_with_positive_offset() {
    Fixture f("ab");
    f.host.cursorPos = 0;
    std::string err;
    // Insert "`" — cursor ends at position 1 (after the backtick, default offset=0)
    CHECK(runOk(f.engine, f.host, "insert('`')", nullptr, &err));
    CHECK(f.text() == "`ab");
    CHECK(f.host.cursorPos == 1);
    if (!err.empty()) fprintf(stderr, "insert +offset test err: %s\n", err.c_str());
}

static void test_rex_gsub() {
    Fixture f("a1b22c333");
    f.host.hasSelection = true;
    f.host.selStart = 0;
    f.host.selEnd = 9;
    std::string err;
    CHECK(runOk(f.engine, f.host,
        "return rex.gsub(input, '[0-9]+', '#')",
        nullptr, &err));
    CHECK(f.text() == "a#b#c#");
    if (!err.empty()) fprintf(stderr, "rex.gsub test err: %s\n", err.c_str());
}

// rex.gsub with a STRING replacement must expand $0/$&/$1/$2/$$ and must
// honour the max count argument (regression: the old pcre2_substitute path
// did not support $& and mis-handled the max_count + string-repl combination).
static void test_rex_gsub_string_repl() {
    {
        Fixture f("x");
        f.host.hasSelection = true;
        f.host.selStart = 0;
        f.host.selEnd = 1;
        std::string err;
        // $& (whole match) — documented in lua_api.txt
        CHECK(runOk(f.engine, f.host,
            "return rex.gsub('ab-cd', '([a-z]+)', '<$&>')",
            nullptr, &err));
        CHECK(f.text() == "<ab>-<cd>");
        if (!err.empty()) fprintf(stderr, "rex.gsub $& err: %s\n", err.c_str());
    }
    {
        Fixture f("x");
        f.host.hasSelection = true;
        f.host.selStart = 0;
        f.host.selEnd = 1;
        std::string err;
        // numbered backrefs
        CHECK(runOk(f.engine, f.host,
            "return rex.gsub('2026-09-04', '(\\\\d+)-(\\\\d+)-(\\\\d+)', '$3/$2/$1')",
            nullptr, &err));
        CHECK(f.text() == "04/09/2026");
    }
    {
        Fixture f("x");
        f.host.hasSelection = true;
        f.host.selStart = 0;
        f.host.selEnd = 1;
        std::string err;
        // max_count with a STRING repl (previously mis-routed to the callback path)
        CHECK(runOk(f.engine, f.host,
            "return rex.gsub('a-b-c-d', '-', '/', 2)",
            nullptr, &err));
        CHECK(f.text() == "a/b/c-d");
    }
}

static void test_rex_find() {
    Fixture f("hello world");
    std::string err;
    CHECK(runOk(f.engine, f.host,
        "local from, to, cap1 = rex.find('hello world', '(\\\\w+) (\\\\w+)')\n"
        "return cap1",
        nullptr, &err));
    CHECK(f.text() == "hello");
    if (!err.empty()) fprintf(stderr, "rex.find test err: %s\n", err.c_str());
}

static void test_rex_match() {
    Fixture f("hello world");
    std::string err;
    CHECK(runOk(f.engine, f.host,
        "local m = rex.match('hello world', '\\\\w+')\n"
        "return m",
        nullptr, &err));
    CHECK(f.text() == "hello");
    if (!err.empty()) fprintf(stderr, "rex.match test err: %s\n", err.c_str());
}

static void test_rex_split() {
    Fixture f("a,b,,c");
    f.host.hasSelection = true;
    f.host.selStart = 0;
    f.host.selEnd = 6;
    std::string err;
    CHECK(runOk(f.engine, f.host,
        "local parts = rex.split(input, ',')\n"
        "return table.concat(parts, '|')",
        nullptr, &err));
    CHECK(f.text() == "a|b||c");
    if (!err.empty()) fprintf(stderr, "rex.split test err: %s\n", err.c_str());
}

static void test_rex_gmatch() {
    Fixture f("a1b2");
    f.host.hasSelection = true;
    f.host.selStart = 0;
    f.host.selEnd = 4;
    std::string err;
    CHECK(runOk(f.engine, f.host,
        "local matches = rex.gmatch(input, '([a-z])([0-9])')\n"
        "return matches[1][2] .. matches[2][2]",
        nullptr, &err));
    std::string actual = f.text();
    fprintf(stderr, "rex.gmatch: expected='ab' actual='%s'(%d)\n", actual.c_str(), (int)actual.size());
    CHECK(actual == "ab");
    if (!err.empty()) fprintf(stderr, "rex.gmatch test err: %s\n", err.c_str());
}

static void test_rex_version() {
    Fixture f("");
    std::string err;
    CHECK(runOk(f.engine, f.host,
        "return rex.version()",
        nullptr, &err));
    // Version should be something like "10.47"
    CHECK(f.text().size() > 0);
    if (!err.empty()) fprintf(stderr, "rex.version test err: %s\n", err.c_str());
}

static void test_rex_new_and_methods() {
    Fixture f("a1b2");
    f.host.hasSelection = true;
    f.host.selStart = 0;
    f.host.selEnd = 4;
    std::string err;
    CHECK(runOk(f.engine, f.host,
        "local re = rex.new('[0-9]+')\n"
        "return re:gsub(input, 'X')",
        nullptr, &err));
    CHECK(f.text() == "aXbX");
    if (!err.empty()) fprintf(stderr, "rex.new test err: %s\n", err.c_str());
}

static void run_all() {
    // New Textus-compatible API tests
    test_input_global();
    test_input_empty_when_no_selection();
    test_return_replaces_selection();
    test_return_inserts_when_no_selection();
    test_insert_function();
    test_insert_with_negative_offset();
    test_insert_with_positive_offset();
    test_rex_gsub();
    test_rex_gsub_string_repl();
    test_rex_find();
    test_rex_match();
    test_rex_split();
    test_rex_gmatch();
    test_rex_version();
    test_rex_new_and_methods();
    // Legacy API tests
    test_stdlib_io_ok();
    test_stdlib_require_ok();
    test_stdlib_execute_ok();
    test_stdlib_loadfile_ok();
    test_stdlib_os_time_ok();
    test_stdlib_math_ok();
    test_stdlib_utf8_ok();
    test_win_table_registered();
    test_win_functions_registered();
    test_replace_selection();
    test_insert_at_cursor();
    test_get_selection_empty();
    test_get_selection_text();
    test_get_cursor();
    test_set_cursor();
    test_set_cursor_clamped();
    test_set_cursor_clears_selection();
    test_insert_pair_and_place_cursor();
    test_replace_with_nul();
    test_get_selection_with_nul();
    test_syntax_error();
    test_runtime_error_keeps_buffer();
    test_changed_applies_once();
    test_noop_keeps_text();
    test_undo_after_lua_run();
    test_multi_lua_run_undo_no_hang();
    test_user_repro_typed_chars_then_lua_then_double_undo();
}

int main() {
    run_all();
    int fails = test::failCount();
    std::fprintf(stderr, "%d checks, %d failures\n",
                 test::checkCount(), fails);
    return fails ? 1 : 0;
}
