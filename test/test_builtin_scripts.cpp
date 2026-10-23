// test_builtin_scripts.cpp - gate for the BUILT-IN Lua scripts.
//
// Why this test exists:
//   script/scripts/ holds 92 shipped scripts (numbered / pairing /
//   transform / row / formatting / markdown). Until now NOTHING parsed
//   them - the test suite only covered the Lua ENGINE (test_lua_engine)
//   and the folder SCANNER (test_script_manager), never the script
//   files themselves. A typo in a shipped script therefore shipped
//   silently: 02.Pair/02_doublequote.lua had `local left, right =
//   """, """` (a syntax error) since 1.0.0 and only surfaced when a user
//   picked that entry from the Pair menu.
//
// What this test does:
//   1. SYNTAX: every *.lua under script/scripts/ must parse with the
//      real Lua 5.5.1 parser (luaL_loadstring).
//   2. BEHAVIOUR: the 13 pairing scripts must wrap the selection in
//      the correct pair of delimiters (a syntax-clean file can still
//      pair the wrong characters).
//   3. COVERAGE: the script tree must not silently shrink - a missing
//      file is as much a regression as a broken one.
//
// The scripts are read from ${CMAKE_SOURCE_DIR}/script/scripts, i.e.
// the SOURCE tree, not the build output - that is the copy that gets
// shipped.
#include "test_assert.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

// Locate the source tree the same way test_docs does: the test exe lives
// in the build output dir, and main/ is a sibling of it. Falling back to
// the CWD keeps the test usable when run by hand.
static fs::path findScriptRoot() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    if (GetModuleFileNameW(nullptr, buf, MAX_PATH) > 0) {
        const fs::path exeDir = fs::path(buf).parent_path();
        // <repo>/build/test_builtin_scripts.exe -> <repo>/main/script/scripts
        const fs::path cand = exeDir.parent_path() / "main" / "script" / "scripts";
        if (fs::is_directory(cand)) return cand;
    }
#endif
    return fs::path("script") / "scripts";
}

static std::string readFile(const fs::path &p, bool *ok) {
    FILE *f = nullptr;
    if (fopen_s(&f, p.string().c_str(), "rb") != 0 || !f) {
        if (ok) *ok = false;
        return std::string();
    }
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    if (ok) *ok = true;
    return out;
}

// Run a script with `input` bound to the given selection and return
// the string it returns. Mirrors what LuaEngine does for the globals
// a pairing script touches (input / insert / params).
static bool runScriptWithInput(const std::string &src, const std::string &input,
                               std::string &out, std::string &err) {
    lua_State *L = luaL_newstate();
    if (!L) { err = "luaL_newstate failed"; return false; }
    luaL_openlibs(L);

    lua_pushstring(L, input.c_str());
    lua_setglobal(L, "input");
    // insert(text, offset): the no-selection branch of the pair scripts
    lua_pushcfunction(L, [](lua_State *L) -> int {
        size_t n = 0;
        const char *t = luaL_checklstring(L, 1, &n);
        lua_pushlstring(L, t, n);
        return 1;
    });
    lua_setglobal(L, "insert");
    // params: injected by LuaEngine on every run (--!param scripts)
    lua_newtable(L);
    lua_setglobal(L, "params");
    lua_newtable(L);
    lua_setglobal(L, "editor");

    if (luaL_loadstring(L, src.c_str()) != LUA_OK) {
        const char *s = lua_tostring(L, -1);
        err = s ? s : "load error";
        lua_close(L);
        return false;
    }
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        const char *s = lua_tostring(L, -1);
        err = s ? s : "runtime error";
        lua_close(L);
        return false;
    }
    if (lua_isstring(L, -1)) out = lua_tostring(L, -1);
    lua_close(L);
    return true;
}

int main() {
    const fs::path root = findScriptRoot();
    CHECK(fs::is_directory(root));

    std::vector<fs::path> scripts;
    for (const auto &e : fs::recursive_directory_iterator(root)) {
        if (e.is_regular_file() && e.path().extension() == ".lua")
            scripts.push_back(e.path());
    }
    std::sort(scripts.begin(), scripts.end());

    // --- 1. coverage: the shipped script tree must not shrink ---------
    // 92 scripts as of 1.1.0. A drop means a file was lost.
    CHECK(scripts.size() >= 92);

    // --- 2. syntax: every script must parse --------------------------
    int syntaxErrors = 0;
    for (const auto &p : scripts) {
        bool ok = false;
        const std::string src = readFile(p, &ok);
        if (!ok) {
            std::fprintf(stderr, "  cannot read %s\n", p.string().c_str());
            ++syntaxErrors;
            continue;
        }
        lua_State *L = luaL_newstate();
        luaL_openlibs(L);
        if (luaL_loadstring(L, src.c_str()) != LUA_OK) {
            const char *s = lua_tostring(L, -1);
            std::fprintf(stderr, "  SYNTAX %s\n    %s\n",
                         p.filename().string().c_str(), s ? s : "?");
            ++syntaxErrors;
            lua_pop(L, 1);
        }
        lua_close(L);
    }
    CHECK(syntaxErrors == 0);

    // --- 3. behaviour: the pairing scripts must use the right pair ----
    struct PairCase {
        const char *file;
        const char *expect;   // expected result for input "hi"
    };
    static const PairCase pairs[] = {
        {"01_backtick.lua",     "`hi`"},
        {"02_doublequote.lua",  "\"hi\""},
        {"03_curlyquote.lua",   "\xE2\x80\x9Chi\xE2\x80\x9D"},
        {"04_round.lua",        "(hi)"},
        {"05_fullparen.lua",    "\xEF\xBC\x88hi\xEF\xBC\x89"},
        {"06_square.lua",       "[hi]"},
        {"07_fullsquare.lua",   "\xE3\x80\x90hi\xE3\x80\x91"},
        {"08_tortoise.lua",     "\xE3\x80\x94hi\xE3\x80\x95"},
        {"09_brace.lua",        "{hi}"},
        {"10_angle.lua",        "<hi>"},
        {"11_booktitle.lua",    "\xE3\x80\x8Ahi\xE3\x80\x8B"},
        {"12_corner.lua",       "\xE3\x80\x8Chi\xE3\x80\x8D"},
        {"13_doublecorner.lua", "\xE3\x80\x8Ehi\xE3\x80\x8F"},
    };
    for (const auto &c : pairs) {
        const fs::path p = root / "02.Pair" / c.file;
        bool ok = false;
        const std::string src = readFile(p, &ok);
        CHECK(ok);
        if (!ok) continue;
        std::string out, err;
        if (!runScriptWithInput(src, "hi", out, err)) {
            std::fprintf(stderr, "  RUN FAIL %s: %s\n", c.file, err.c_str());
            CHECK(false);
            continue;
        }
        if (out != c.expect) {
            std::fprintf(stderr, "  PAIR MISMATCH %s: got [%s] want [%s]\n",
                         c.file, out.c_str(), c.expect);
        }
        CHECK(out == c.expect);
    }

    // --- 4. behaviour: numbering / transform / heading scripts ---------
    // A script can be syntactically valid and still number the wrong way
    // (or with the wrong symbol). Expected values are written as escaped
    // UTF-8 bytes so the check does not depend on the file's encoding.
    struct BehCase {
        const char *rel;     // relative to the script root
        const char *input;
        const char *expect;
    };
    static const BehCase behs[] = {
        // 01.No - numbering
        {"01.No/01_number_dot.lua",   "item", "1. item"},
        {"01.No/02_number_paren.lua", "item", "(1) item"},
        {"01.No/03_bracket.lua",      "item", "[1] item"},
        {"01.No/06_num_cn.lua",       "item", "\xE4\xB8\x80\xE3\x80\x81item"},
        {"01.No/07_paren_cn.lua",     "item", "\xEF\xBC\x88\xE4\xB8\x80\xEF\xBC\x89item"},
        {"01.No/08_dash.lua",         "item", "- item"},
        {"01.No/09_star.lua",         "item", "* item"},
        {"01.No/10_bullet.lua",       "item", "\xE2\x97\x8F item"},
        {"01.No/11_square.lua",       "item", "\xE2\x96\xA0 item"},
        // 03.Trans - case / width
        {"03.Trans/01_upper.lua",        "abc", "ABC"},
        {"03.Trans/02_lower.lua",        "ABC", "abc"},
        {"03.Trans/07_to_fullwidth.lua", "abc", "\xEF\xBD\x81\xEF\xBD\x82\xEF\xBD\x83"},
        {"03.Trans/08_to_halfwidth.lua", "\xEF\xBD\x81\xEF\xBD\x82\xEF\xBD\x83", "abc"},
        // 06.Markdown - headings
        {"06.Markdown/01.Heading/01_heading1.lua", "title", "# title"},
        {"06.Markdown/01.Heading/02_heading2.lua", "title", "## title"},
        {"06.Markdown/01.Heading/03_heading3.lua", "title", "### title"},
    };
    for (const auto &c : behs) {
        bool ok = false;
        const std::string src = readFile(root / fs::path(c.rel), &ok);
        CHECK(ok);
        if (!ok) continue;
        std::string out, err;
        if (!runScriptWithInput(src, c.input, out, err)) {
            std::fprintf(stderr, "  RUN FAIL %s: %s\n", c.rel, err.c_str());
            CHECK(false);
            continue;
        }
        if (out != c.expect) {
            std::fprintf(stderr, "  BEH MISMATCH %s: got [%s] want [%s]\n",
                         c.rel, out.c_str(), c.expect);
        }
        CHECK(out == c.expect);
    }

    std::fprintf(stderr, "%d checks, %d failures\n",
                 test::checkCount(), test::failCount());
    return test::failCount() ? 1 : 0;
}
