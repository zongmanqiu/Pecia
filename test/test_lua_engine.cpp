// test_lua_engine.cpp - unit tests for the embedded Lua sandbox:
// sandbox restrictions, get_selection/replace_selection semantics,
// NUL-byte handling, error paths, and the single-undo apply behavior.
// Links LuaEngine.cpp + lua_static.
#include "test_assert.h"
#include "script/LuaEngine.h"
#include "core/HostOps.h"

#include <FL/Fl_Text_Buffer.H>

#include <cstring>
#include <cstdio>
#include <chrono>
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
// m025: validate / headless / caps / nested-run guard
// (defined after run_all below - forward declarations first)
// ---------------------------------------------------------------------------
static void test_validate_zero_side_effects();
static void test_headless_captures_dialogs();
static void test_output_cap();
static void test_result_cap();
static void test_nested_run_guard();

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
    {
        // Was `CHECK(buf.length() >= 0)`, which is true for every possible
        // value of an int-returning length() - it could not fail. Prove the
        // buffer is really empty instead, and free the copy text() hands out.
        char *t = buf.text();
        CHECK(t != nullptr && t[0] == '\0');
        free(t);
    }
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

// rex.gsub with a FUNCTION repl must follow the lua_api.txt contract:
// nil/false keep the match (Lua string.gsub semantics), numbers/strings
// replace, truthy non-strings (e.g. true) produce an empty replacement.
// The pattern is PCRE2 ([0-9]), not Lua's %d: rex.* compiles with PCRE2 and
// lua_api.txt documents \d / [0-9] style patterns for every rex call.
static void test_rex_gsub_func_repl_values() {
    struct Case { const char *script; const char *expect; };
    static const Case cases[] = {
        {"return rex.gsub('a1b2', '[0-9]', function(d) return nil end)",   "a1b2"},
        {"return rex.gsub('a1b2', '[0-9]', function(d) return false end)", "a1b2"},
        {"return rex.gsub('a1b2', '[0-9]', function(d) return 7 end)",     "a7b7"},
        {"return rex.gsub('a1b2', '[0-9]', function(d) return true end)",  "ab"},
    };
    for (const auto &c : cases) {
        Fixture f("x");
        std::string err;
        CHECK(runOk(f.engine, f.host, c.script, nullptr, &err));
        if (f.text() != c.expect)
            std::fprintf(stderr, "  gsub func repl: got [%s] want [%s]\n",
                         f.text().c_str(), c.expect);
        CHECK(f.text() == c.expect);
    }
}

// pecia.* trampoline: fake ops registered directly into the registry prove
// the whole Lua<->HostOps path (name mapping, positional + table args,
// perm gate, nil,err returns) without needing a MainWindow.
static void test_pecia_trampoline() {
    HostRegistry &reg = HostRegistry::instance();
    reg.add({"test.echo", "", {{"text", "string", "t"}}, OpPerm::Read,
             [](const std::map<std::string, OpValue> &a) -> OpOutcome {
        return OpOutcome::success(OpValue::makeStr("echo:" + a.at("text").getString()));
    }});
    reg.add({"test.write_op", "", {{"n", "int?", "num"}}, OpPerm::Write,
             [](const std::map<std::string, OpValue> &) -> OpOutcome {
        return OpOutcome::success(OpValue::makeInt(42));
    }});
    {
        Fixture f("x");
        std::string err;
        CHECK(runOk(f.engine, f.host, "return pecia.test_echo('hi')", nullptr, &err));
        CHECK(f.text() == "echo:hi");
    }
    {   // table-call form + declared positional order
        Fixture f("x");
        std::string err;
        CHECK(runOk(f.engine, f.host,
            "return pecia.test_echo({ text = 'tab' })", nullptr, &err));
        CHECK(f.text() == "echo:tab");
    }
    {   // perm gate: no gate installed => write op denies, script survives
        Fixture f("x");
        std::string err;
        CHECK(runOk(f.engine, f.host,
            "local v, e = pecia.test_write_op(1)\n"
            "return v == nil and e and 'denied:' .. e:sub(1,12) or tostring(v)",
            nullptr, &err));
        CHECK(f.text() == "denied:perm_denied:");
    }
    {   // ops() introspection sees both entries in sorted order
        Fixture f("x");
        std::string err;
        CHECK(runOk(f.engine, f.host,
            "local n = 0; local first = ''\n"
            "for _, o in ipairs(pecia.ops()) do if n == 0 then first = o.name end n = n + 1 end\n"
            "return tostring(n) .. ',' .. first",
            nullptr, &err));
        CHECK(f.text() == "2,test.echo");
    }
    reg.removeAll();
    {   // after teardown the table is rebuilt without the ops (fresh engine)
        Fixture f("x");
        std::string err;
        CHECK(runOk(f.engine, f.host, "return type(pecia.test_echo)", nullptr, &err));
        CHECK(f.text() == "nil");
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

// Regression: the two gsub loops once lacked the zero-width advance guard
// that gmatch/split have, so a pattern matching the empty string ('x*')
// looped forever on the UI thread. Fixture must terminate; if the guard is
// removed again this test hangs the suite (visible red on timeout).
static void test_rex_gsub_zero_width() {
    {
        Fixture f("x");
        f.host.hasSelection = true;
        f.host.selStart = 0;
        f.host.selEnd = 1;
        std::string err;
        CHECK(runOk(f.engine, f.host,
            "return rex.gsub('abc', 'x*', 'Y')",
            nullptr, &err));
        CHECK(f.text() == "YaYbYcY");   // matches native string.gsub("abc","x*","Y")
        if (!err.empty()) fprintf(stderr, "rex.gsub zero-width err: %s\n", err.c_str());
    }
    {   // re:gsub (method form) had the same missing guard
        Fixture f("x");
        f.host.hasSelection = true;
        f.host.selStart = 0;
        f.host.selEnd = 1;
        std::string err;
        CHECK(runOk(f.engine, f.host,
            "local re = rex.new('x*')\n"
            "return re:gsub('abc', 'Y')",
            nullptr, &err));
        CHECK(f.text() == "YaYbYcY");   // matches native string.gsub("abc","x*","Y")
        if (!err.empty()) fprintf(stderr, "re:gsub zero-width err: %s\n", err.c_str());
    }
}

// M1 red-proof (m026 audit): pathological regex work must fail fast, not
// hold the UI thread. Two traps this test avoids:
//  * `(a+)+$` is auto-possessified by PCRE2 and matches linearly - useless.
//  * a match-limit abort returns nil from rex.find (not an error), so the
//    probe must measure WALL TIME, not the error path.
// ^(a+)*b$ cannot be possessified (b follows the quantifier), and the 'c'
// tail keeps the required-char start optimization happy ('b' IS in the
// subject) while making the overall match FAIL - so each of the 20 calls
// grinds through real backtracking up to whatever limit is configured
// before returning nil. With the 1M limit a call costs ~30 ms (600 ms
// total); at PCRE2's 10M default it is ~300 ms per call (6 s total, where
// the 5 s wall-clock hook backstop also fires). The 1600 ms threshold sits
// far from both readings.
static void test_rex_pathological_fails_fast() {
    Fixture f("x");
    f.host.hasSelection = true;
    f.host.selStart = 0;
    f.host.selEnd = 1;
    std::string err;
    auto t0 = std::chrono::steady_clock::now();
    bool ok = runOk(f.engine, f.host,
        "local s = string.rep('a', 40) .. 'bc'\n"
        "for i = 1, 20 do rex.find(s, '^(a+)*b$') end\n"
        "return 'done'",
        nullptr, &err);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    fprintf(stderr, "pathological rex.find x20: %lld ms, ok=%d, err: %s\n",
            (long long)ms, ok ? 1 : 0, err.c_str());
    CHECK(ms < 1600);
}

// lua_api.txt declares `to` EXCLUSIVE and its worked examples pin the
// numbers (fixture from the doc: rex.find("abc123def", "\\d+") -> 4, 7).
static void test_rex_find_exclusive_to() {
    Fixture f("x");
    f.host.hasSelection = true;
    f.host.selStart = 0;
    f.host.selEnd = 1;
    std::string err;
    CHECK(runOk(f.engine, f.host,
        "local from, to = rex.find('abc123def', '\\\\d+')\n"
        "return from .. ',' .. to",
        nullptr, &err));
    CHECK(f.text() == "4,7");
    if (!err.empty()) fprintf(stderr, "rex.find to err: %s\n", err.c_str());
}

// lua_api.txt documents colon calls (win:message("hi")); the C functions
// once read arg1 as the string (dot shape), so every documented colon call
// failed with "table is not a string". The win table itself is the probe.
static void test_win_colon_call() {
    {
        Fixture f("x");
        std::string err;
        CHECK(runOk(f.engine, f.host,
            "local w = win\nw:clipboard_set('claude3')\nreturn w:clipboard_get()",
            nullptr, &err));
        CHECK(f.text() == "claude3");
        if (!err.empty()) fprintf(stderr, "win colon err: %s\n", err.c_str());
    }
    {   // dot form must keep working (built-in scripts use it)
        Fixture f("x");
        std::string err;
        CHECK(runOk(f.engine, f.host,
            "win.clipboard_set('claude4')\nreturn win.clipboard_get()",
            nullptr, &err));
        CHECK(f.text() == "claude4");
        if (!err.empty()) fprintf(stderr, "win dot err: %s\n", err.c_str());
    }
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
    // m025: engine-facing capabilities
    test_validate_zero_side_effects();
    test_headless_captures_dialogs();
    test_output_cap();
    test_result_cap();
    test_nested_run_guard();
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
    test_rex_gsub_func_repl_values();
    test_rex_find();
    test_pecia_trampoline();
    test_rex_gsub_zero_width();
    test_rex_find_exclusive_to();
    test_rex_pathological_fails_fast();
    test_win_colon_call();
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

// ---------------------------------------------------------------------------
// m025: validate / headless / caps / nested-run guard
// ---------------------------------------------------------------------------

// validate() compiles without executing: a script whose first line would
// corrupt the document must leave the buffer byte-identical.
static void test_validate_zero_side_effects() {
    Fixture f("original");
    std::string err;
    CHECK(f.engine.validate("local x = 1 return x", &err));
    CHECK(f.text() == "original");

    // syntax error: ok=false, Lua's own words, still zero side effects
    CHECK(!f.engine.validate("local x = ", &err));
    CHECK(!err.empty());
    CHECK(f.text() == "original");

    // a script that WOULD edit on run must not edit under validate
    CHECK(f.engine.validate("editor:replace_selection('HACKED') return ''", &err));
    CHECK(f.text() == "original");

    // io side effects likewise impossible (never executed)
    CHECK(f.engine.validate("local fh = io.open('_pecia_validate_probe.txt','w') if fh then fh:write('x') fh:close() end", &err));
    FILE *probe = fopen("_pecia_validate_probe.txt", "rb");
    CHECK(!probe);
    if (probe) { fclose(probe); remove("_pecia_validate_probe.txt"); }
}

// headless: win dialogs must never be shown (no MessageBox/FLTK modal here in
// a headless test process) and instead captured into the output sink.
static void test_headless_captures_dialogs() {
    Fixture f("doc");
    LuaRunOptions opts;
    opts.headless = true;
    opts.answer = true;
    LuaRunResult res;
    std::string err;
    CHECK(f.engine.run("win:message('hello') local q = win:question('go?') "
                       "local i = win:input('give', 'd') local s = win:select('pick', {'a','b'}, 1) "
                       "return (q and 'yes' or 'no') .. '/' .. tostring(i) .. '/' .. tostring(s)",
                       f.host, nullptr, &err, nullptr, nullptr, &res, &opts));
    CHECK(res.output.find("[win:message] hello") != std::string::npos);
    CHECK(res.output.find("[win:question] go? -> yes") != std::string::npos);
    CHECK(res.output.find("[win:input]") != std::string::npos);
    CHECK(res.output.find("[win:select]") != std::string::npos);
    CHECK(res.result == "yes/nil/nil");
    CHECK(res.applied);
}

// output cap: print sink clamps at outputCap and flags truncation.
static void test_output_cap() {
    Fixture f("doc");
    LuaRunOptions opts;
    opts.headless = true;
    opts.outputCap = 100;
    LuaRunResult res;
    std::string err;
    CHECK(f.engine.run("for i=1,200 do print('0123456789') end return ''",
                       f.host, nullptr, &err, nullptr, nullptr, &res, &opts));
    CHECK((int)res.output.size() == 100);
    CHECK(res.truncated);
}

// result cap: long `return` string clamps and flags resultTruncated, while
// the document still receives the capped replacement deterministically.
static void test_result_cap() {
    Fixture f("seed");
    // no selection -> replace whole doc with returned string
    LuaRunOptions opts;
    opts.headless = true;
    opts.resultCap = 64;
    LuaRunResult res;
    std::string err;
    std::string script = "local t = {} for i=1,100 do t[i]='x' end return table.concat(t)";
    CHECK(f.engine.run(script, f.host, nullptr, &err, nullptr, nullptr, &res, &opts));
    CHECK((int)res.result.size() == 64);
    CHECK(res.resultTruncated);
    CHECK(f.text() == std::string(64, 'x'));   // capped replacement applied
}

// nested run: a script dispatching back into the engine is rejected cleanly.
// The registry op below calls engine.run() from inside a running script.
static LuaEngine *g_nestedEngine = nullptr;
static LuaEditorHost *g_nestedHost = nullptr;
static void test_nested_run_guard() {
    Fixture f("doc");
    g_nestedEngine = &f.engine;
    g_nestedHost = &f.host;
    HostRegistry &reg = HostRegistry::instance();
    reg.removeAll();
    // an earlier test may have left a deny-write gate installed; the point
    // of THIS test is engine re-entry, not the permission gate (dispatch
    // fails safe with no gate, so install an allow-all one)
    reg.setWriteGate([] { return true; });
    reg.add({"test.nest", "re-enter the engine", {}, OpPerm::Exec,
             [](const std::map<std::string, OpValue> &) -> OpOutcome {
                 std::string e2;
                 bool ok = g_nestedEngine->run("return 'inner'", *g_nestedHost,
                                               nullptr, &e2);
                 // success either way; the payload records what the inner
                 // run saw (an error string when the guard fired)
                 return OpOutcome::success(OpValue::makeStr(
                     ok ? "ALLOWED" : e2));
             }});
    f.engine.registerHostOpsLua();
    std::string out, err;
    // outer script calls pecia.test_nest() -> inner run() must be refused
    bool ok = f.engine.run("if type(pecia.test_nest) ~= 'function' then return 'KEY MISSING:'..type(pecia.test_nest) end "
                           "local a, b = pecia.test_nest() "
                           "return 'A='..tostring(a)..' B='..tostring(b)", f.host, &out, &err);
    CHECK(ok);
    CHECK(err.empty());
    // the inner refusal message must be visible (guard fired, engine intact)
    char *t = f.host.buffer->text();
    std::string applied = t ? t : "";
    if (t) free(t);
    CHECK(applied.find("nested run") != std::string::npos);
    // and the engine still works normally afterwards
    std::string err2;
    CHECK(f.engine.run("return 'after'", f.host, nullptr, &err2));
    reg.removeAll();
    reg.setWriteGate(nullptr);   // restore "no gate" for later tests
    g_nestedEngine = nullptr;
    g_nestedHost = nullptr;
}

int main() {
    run_all();
    int fails = test::failCount();
    std::fprintf(stderr, "%d checks, %d failures\n",
                 test::checkCount(), fails);
    return fails ? 1 : 0;
}
