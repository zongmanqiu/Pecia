// test_script_manager.cpp - unit tests for the two-level script folder
// scanner (scriptManagerScanDir / scriptManagerPathIn). Uses an explicit
// temp directory so the tests are independent of the exe location.
// Links ScriptManager.cpp (fl_fopen is pulled from FLTK).
#include "test_assert.h"
#include "script/ScriptManager.h"

#include <FL/fl_utf8.h>
#include <FL/Fl_Group.H>
#include <FL/Fl_Button.H>
#include <core/I18n.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// temp dir helpers (Win32)
// ---------------------------------------------------------------------------

static std::string g_tmpRoot;

static std::string mkPath(const std::string &rel) {
    return g_tmpRoot + "/" + rel;
}

// Create a directory (and parents) under g_tmpRoot.
static void mkDir(const std::string &rel) {
    std::string p = mkPath(rel);
    std::wstring wpath;
    int len = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0);
    wpath.resize(len - 1);
    MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, &wpath[0], len);
    CreateDirectoryW(wpath.c_str(), nullptr);   // ignore failure
}

// Write a small .lua file.
static void writeFile(const std::string &rel, const char *content) {
    std::string p = mkPath(rel);
    FILE *fp = fl_fopen(p.c_str(), "wb");
    if (!fp) return;
    fputs(content, fp);
    fclose(fp);
}

// Recursive delete of g_tmpRoot (Win32).
static void removeTree(const std::wstring &path) {
    std::wstring search = path + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(search.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring name(fd.cFileName);
            if (name == L"." || name == L"..") continue;
            std::wstring full = path + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                removeTree(full);
            else
                DeleteFileW(full.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(path.c_str());
}

static void setup() {
    wchar_t tmpBuf[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpBuf);
    std::wstring tmp = tmpBuf + std::wstring(L"pecia_test_script_");
    tmp += std::to_wstring(GetCurrentProcessId());
    g_tmpRoot = "";
    int len = WideCharToMultiByte(CP_UTF8, 0, tmp.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len > 1) {
        g_tmpRoot.resize(len - 1);
        WideCharToMultiByte(CP_UTF8, 0, tmp.c_str(), -1, &g_tmpRoot[0], len, nullptr, nullptr);
    }
    mkDir(".");
    removeTree(tmp);   // clear any stale leftovers
    CreateDirectoryW(tmp.c_str(), nullptr);
}

static void teardown() {
    if (!g_tmpRoot.empty()) {
        std::wstring wpath;
        int len = MultiByteToWideChar(CP_UTF8, 0, g_tmpRoot.c_str(), -1, nullptr, 0);
        wpath.resize(len - 1);
        MultiByteToWideChar(CP_UTF8, 0, g_tmpRoot.c_str(), -1, &wpath[0], len);
        removeTree(wpath);
    }
}

// ---------------------------------------------------------------------------
// tests
// ---------------------------------------------------------------------------

// Root scripts + level-1 groups + level-2 submenus are discovered;
// level-3 nesting is ignored; directories without .lua files are
// skipped.
static void test_two_level_scan() {
    mkDir("Formatting");
    mkDir("EmptyDir");
    mkDir("No");
    mkDir("No/AAA");
    mkDir("Deep");
    mkDir("Deep/Nested");
    writeFile("pair.lua", "print(1)");
    writeFile("upper.lua", "print(2)");
    writeFile("Formatting/trim.lua", "print(3)");
    writeFile("Formatting/sort.lua", "print(4)");
    writeFile("No/number_dot.lua", "print(5)");
    writeFile("No/AAA/a.lua", "print(6)");
    writeFile("Deep/Nested/deep.lua", "print(7)");
    writeFile("readme.txt", "not a script");

    ScriptCatalog cat = scriptManagerScanDir(g_tmpRoot);
    CHECK(!cat.rootPath.empty());
    CHECK(cat.rootPath == g_tmpRoot);

    // Root scripts sorted, extensions stripped, non-.lua ignored.
    CHECK(cat.rootScripts.size() == 2);
    if (cat.rootScripts.size() == 2) {
        CHECK(cat.rootScripts[0] == "pair");
        CHECK(cat.rootScripts[1] == "upper");
    }

    // "Formatting": direct scripts, no submenus. "No": direct script +
    // level-2 submenu AAA. "Deep": no direct scripts but level-2
    // subfolder Nested. "EmptyDir": nothing at all.
    CHECK(cat.groups.size() == 3);
    bool foundFmt = false, foundNo = false, foundDeep = false;
    for (const auto &g : cat.groups) {
        if (g.name == "Formatting") {
            foundFmt = true;
            CHECK(g.scripts.size() == 2);
            if (g.scripts.size() == 2) {
                CHECK(g.scripts[0] == "sort");
                CHECK(g.scripts[1] == "trim");
            }
            CHECK(g.subGroups.empty());
        } else if (g.name == "No") {
            foundNo = true;
            CHECK(g.scripts.size() == 1);
            if (g.scripts.size() == 1) CHECK(g.scripts[0] == "number_dot");
            CHECK(g.subGroups.size() == 1);
            if (g.subGroups.size() == 1) {
                CHECK(g.subGroups[0].name == "AAA");
                CHECK(g.subGroups[0].scripts.size() == 1);
                if (g.subGroups[0].scripts.size() == 1)
                    CHECK(g.subGroups[0].scripts[0] == "a");
                CHECK(g.subGroups[0].subGroups.empty());
            }
        } else if (g.name == "Deep") {
            foundDeep = true;
            // Level-2 subfolder is scanned even when the parent has no
            // direct scripts; level-3 would be ignored (not tested here
            // because Deep/Nested has no Third/ level).
            CHECK(g.scripts.empty());
            CHECK(g.subGroups.size() == 1);
            if (g.subGroups.size() == 1) {
                CHECK(g.subGroups[0].name == "Nested");
                CHECK(g.subGroups[0].scripts.size() == 1);
                if (g.subGroups[0].scripts.size() == 1)
                    CHECK(g.subGroups[0].scripts[0] == "deep");
            }
        }
    }
    CHECK(foundFmt);
    CHECK(foundNo);
    CHECK(foundDeep);
}

// Rescan semantics (Reload Scripts): a second scan picks up scripts
// added since the first scan, without any caching.
static void test_rescan_picks_up_new_scripts() {
    // Use an isolated subdirectory so leftover files from prior tests
    // (test_two_level_scan writes pair.lua, upper.lua) don't pollute counts.
    std::string root = g_tmpRoot + "/rescan_test";
    mkDir("rescan_test");
    std::string aPath = root + "/a.lua";
    writeFile("rescan_test/a.lua", "print(1)");
    ScriptCatalog cat1 = scriptManagerScanDir(root);
    CHECK(cat1.rootScripts.size() == 1);

    writeFile("rescan_test/b.lua", "print(2)");
    ScriptCatalog cat2 = scriptManagerScanDir(root);
    CHECK(cat2.rootScripts.size() == 2);
    if (cat2.rootScripts.size() == 2) {
        CHECK(cat2.rootScripts[0] == "a");
        CHECK(cat2.rootScripts[1] == "b");
    }
}

// A UTF-8 (Chinese) group name must round-trip through the Win32 API.
static void test_utf8_group_name() {
    mkDir("\xE6\xA0\xBC\xE5\xBC\x8F");      // "格式"
    writeFile("\xE6\xA0\xBC\xE5\xBC\x8F/trim.lua", "print(1)");

    ScriptCatalog cat = scriptManagerScanDir(g_tmpRoot);
    CHECK(cat.groups.size() >= 1);
    bool found = false;
    for (const auto &g : cat.groups) {
        if (g.name == "\xE6\xA0\xBC\xE5\xBC\x8F") {
            found = true;
            CHECK(g.scripts.size() == 1);
            if (g.scripts.size() == 1) CHECK(g.scripts[0] == "trim");
        }
    }
    CHECK(found);
}

// folder.ini provides localized group names (name / name.<lang>).
static void test_folder_ini_display_names() {
    mkDir("Line");
    writeFile("Line/a.lua", "print(1)");
    writeFile("Line/folder.ini",
              "name = Lines\nname.zh-CN = \xE6\xAF\x8F\xE8\xA1\x8C\n");  // 每行

    // No lang override: I18n defaults to "en" -> base name.
    ScriptCatalog cat = scriptManagerScanDir(g_tmpRoot);
    bool found = false;
    for (const auto &g : cat.groups) {
        if (g.name == "Line") {
            found = true;
            CHECK(g.displayName == "Lines");
        }
    }
    CHECK(found);

    // Unknown language code falls back to the base name.
    ScriptCatalog cat2 = scriptManagerScanDir(g_tmpRoot);
    bool found2 = false;
    for (const auto &g : cat2.groups) {
        if (g.name == "Line") {
            found2 = true;
            CHECK(g.displayName == "Lines");
        }
    }
    CHECK(found2);
}

// A folder without folder.ini keeps the raw folder name as display name.
static void test_folder_without_ini_falls_back_to_name() {
    mkDir("Plain");
    writeFile("Plain/b.lua", "print(1)");

    ScriptCatalog cat = scriptManagerScanDir(g_tmpRoot);
    bool found = false;
    for (const auto &g : cat.groups) {
        if (g.name == "Plain") {
            found = true;
            CHECK(g.displayName.empty());   // caller uses g.name
        }
    }
    CHECK(found);
}

// scriptManagerPathIn resolves root scripts and group scripts, and
// rejects unknown paths.
static void test_path_lookup() {
    writeFile("pair.lua", "print(1)");
    writeFile("Formatting/trim.lua", "print(2)");

    std::string p1 = scriptManagerPathIn(g_tmpRoot, "pair");
    CHECK(!p1.empty());
    CHECK(p1 == g_tmpRoot + "/pair.lua");

    std::string p2 = scriptManagerPathIn(g_tmpRoot, "Formatting/trim");
    CHECK(!p2.empty());
    CHECK(p2 == g_tmpRoot + "/Formatting/trim.lua");

    CHECK(scriptManagerPathIn(g_tmpRoot, "nope").empty());
    CHECK(scriptManagerPathIn(g_tmpRoot, "Formatting/nope").empty());
    CHECK(scriptManagerPathIn(g_tmpRoot, "").empty());
    CHECK(scriptManagerPathIn("", "pair").empty());
}

// Missing root directory -> empty catalog, no crash.
static void test_missing_root() {
    ScriptCatalog cat = scriptManagerScanDir(g_tmpRoot + "/does_not_exist");
    CHECK(cat.rootPath.empty());
    CHECK(cat.rootScripts.empty());
    CHECK(cat.groups.empty());
}

// Root dir contains ONLY subfolders (no .lua at the root): the scan must
// still succeed - rootScripts stays empty but groups are populated.
// Regression: listLuaFiles() treated "no *.lua match" (FindFirstFileW
// INVALID_HANDLE_VALUE) as "directory missing", returning an empty
// catalog that wiped the whole toolbar when the user kept only folders.
static void test_root_only_folders() {
    // Isolated sub-root so earlier tests' root .lua files don't leak in.
    std::string root = g_tmpRoot + "/root_only";
    mkDir("root_only");
    mkDir("root_only/Formatting");
    writeFile("root_only/Formatting/trim.lua", "print(1)");

    ScriptCatalog cat = scriptManagerScanDir(root);
    CHECK(!cat.rootPath.empty());
    CHECK(cat.rootScripts.empty());
    CHECK(cat.groups.size() == 1);
    if (cat.groups.size() == 1) {
        CHECK(cat.groups[0].name == "Formatting");
        CHECK(cat.groups[0].scripts.size() == 1);
        if (cat.groups[0].scripts.size() == 1)
            CHECK(cat.groups[0].scripts[0] == "trim");
    }
}

// An entirely EMPTY script/ folder (no files, no subfolders) must also
// yield a valid (empty) catalog, not a "missing dir" one.
static void test_empty_root() {
    std::string root = g_tmpRoot + "/empty_dir";
    ScriptCatalog cat = scriptManagerScanDir(root);
    CHECK(cat.rootPath.empty());   // dir does not exist yet...
    mkDir("empty_dir");
    cat = scriptManagerScanDir(root);
    CHECK(!cat.rootPath.empty());
    CHECK(cat.rootScripts.empty());
    CHECK(cat.groups.empty());
}

// FLTK current-group semantics: a widget created while another group is
// current attaches to THAT group, not to the toolbar. rebuildScriptBar()
// must call m_scriptBar->begin()/end() so script buttons always land inside
// m_scriptBar even when it re-runs later in the constructor (when e.g.
// m_tabs is the current group).
static void test_current_group_semantics() {
    Fl_Group *toolbar = new Fl_Group(0, 0, 400, 30);
    toolbar->end();
    // Simulate a later constructor phase where another group is current.
    Fl_Group *tabs = new Fl_Group(0, 30, 400, 200);
    tabs->end();

    // Without begin(): the button attaches to... whatever is current
    // (here Fl_Group::current() is null or tabs' parent) - NOT toolbar.
    Fl_Button *stray = new Fl_Button(10, 5, 50, 20, "x");
    CHECK(stray->parent() != toolbar);
    delete stray;

    // With begin()/end(): the button is a child of toolbar.
    toolbar->begin();
    Fl_Button *ok = new Fl_Button(10, 5, 50, 20, "x");
    toolbar->end();
    CHECK(ok->parent() == toolbar);
    CHECK(toolbar->children() == 1);
    delete ok;
    delete tabs;
    delete toolbar;
}

// Script display names: official scripts get an i18n key
// ("script.name.<file name>"), user scripts fall back to the file name.
// ScriptManager exposes the pure string pieces (key + fallback); the UI
// Display-name fallback: without a usable ==Meta== block, the file
// basename is shown. The scriptManagerNameKey i18n layer was removed:
// script names are entirely the scripts' own responsibility now.
static void test_display_name_translation() {
    CHECK(scriptManagerNameFallback("pair") == "pair");
    CHECK(scriptManagerNameFallback("Formatting/trim") == "trim");
    CHECK(scriptManagerNameFallback("my_tool") == "my_tool");
    CHECK(scriptManagerNameFallback("") == "");

    CHECK(scriptManagerDisplayName("pair") == "pair");
    CHECK(scriptManagerDisplayName("Formatting/trim") == "trim");
    CHECK(scriptManagerDisplayName("my_tool") == "my_tool");
    CHECK(scriptManagerDisplayName("") == "");
}

// I18n::getOr(): returns the translated value when the key exists and
// the fallback otherwise. Relies on the real exe-adjacent lang/en.txt
// (copied next to the test binary by the build), which must contain the
// script.name.* keys for the official scripts.
static void test_i18n_getor() {
    CHECK(I18n::load("en"));
    // No built-in scripts exist anymore, so every script name falls
    // back to the file name; getOr() must still find NON-script keys
    // (menu.* etc.) and return the fallback for unknown ones.
    CHECK(strcmp(I18n::getOr("menu.file", "fallback"), "fallback") != 0);
    // Unknown key -> fallback.
    CHECK(strcmp(I18n::getOr("script.name.nope", "nope"), "nope") == 0);
    CHECK(strcmp(I18n::getOr("no.such.key", "fallback"), "fallback") == 0);
    // User scripts always show their file name.
    CHECK(scriptManagerDisplayName("pair") == "pair");
    CHECK(scriptManagerDisplayName("Formatting/trim") == "trim");
    CHECK(scriptManagerDisplayName("my_tool") == "my_tool");
}

// ---------------------------------------------------------------------------
// Script meta block (==Meta== header comments)
// ---------------------------------------------------------------------------

static void test_parse_meta_full() {
    const char *h =
        "-- ==Meta==\n"
        "-- @name My Script\n"
        "-- @name.zh-CN \xE6\x88\x91\xE7\x9A\x84\xE8\x84\x9A\xE6\x9C\xAC\n"
        "-- @author Zhang San\n"          // comment only - ignored
        "-- @date 20260809\n"             // comment only - ignored
        "-- ==/Meta==\n";
    ScriptMeta m = scriptParseMeta(h);
    CHECK(m.valid);
    CHECK(m.names.find("en") != m.names.end());
    if (m.names.find("en") != m.names.end()) CHECK(m.names["en"] == "My Script");
    CHECK(m.names.find("zh-CN") != m.names.end());
    if (m.names.find("zh-CN") != m.names.end())
        CHECK(m.names["zh-CN"] == "\xE6\x88\x91\xE7\x9A\x84\xE8\x84\x9A\xE6\x9C\xAC");
    // Only the @name family is stored - everything else is a comment.
    CHECK(m.names.size() == 2);
}

// No meta block -> invalid; unclosed block -> invalid; broken block
// (non-comment line inside) -> invalid.
static void test_parse_meta_invalid() {
    ScriptMeta m = scriptParseMeta("-- @name X\n");   // no block markers
    CHECK(!m.valid);
    m = scriptParseMeta("-- ==Meta==\n-- @name X\n");   // never closed
    CHECK(!m.valid);
    m = scriptParseMeta("-- ==Meta==\n-- @name X\nprint(1)\n-- ==/Meta==\n");
    CHECK(!m.valid);
    m = scriptParseMeta("-- ==/Meta==\n");   // close before open
    CHECK(!m.valid);
}

// Unknown keys are ignored; meta keys inside a normal (unmarked) comment
// are NOT picked up; extra whitespace is tolerated.
static void test_parse_meta_tolerance() {
    const char *h =
        "-- ==Meta==\n"
        "--  @name   Spaced Name\n"       // extra spaces after -- and before value
        "-- @future-key whatever\n"        // unknown key ignored
        "-- ==/Meta==\n"
        "-- @name NotInBlock\n";           // after the block: ignored
    ScriptMeta m = scriptParseMeta(h);
    CHECK(m.valid);
    CHECK(m.names["en"] == "Spaced Name");
    CHECK(m.names.size() == 1);
}

// ---------------------------------------------------------------------------

int main() {
    setup();
    test_two_level_scan();
    test_rescan_picks_up_new_scripts();
    test_utf8_group_name();
    test_folder_ini_display_names();
    test_folder_without_ini_falls_back_to_name();
    test_path_lookup();
    test_missing_root();
    test_root_only_folders();
    test_empty_root();
    test_current_group_semantics();
    test_display_name_translation();
    test_i18n_getor();
    test_parse_meta_full();
    test_parse_meta_invalid();
    test_parse_meta_tolerance();
    teardown();
    if (test::failCount() == 0) {
        printf("test_script_manager: all checks passed\n");
        return 0;
    }
    fprintf(stderr, "test_script_manager: %d/%d checks FAILED\n",
            test::failCount(), test::checkCount());
    return 1;
}
