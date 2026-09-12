// test_docs.cpp - document consistency checks.
// Verifies factual claims in the shipped docs against the actual tree:
//   * key files/directories exist (core/script/ui/plugin/doc, third-party)
//   * every test_*.cpp named in 目录结构说明.md exists
//   * every API documented in doc/lua_api.txt is registered in LuaEngine
//   * en/zh-CN lang files have identical key sets
//   * third-party libs named in README.md exist under .thirdparty/
//   * every file listed in doc/DOCS_MANIFEST.md exists
// Runs from build/ (ctest), so main/ = exeDir/../main.
#include "test_assert.h"

#include "core/I18n.h"   // I18N_MAX_ENTRIES (lang table capacity gate)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace fs = std::filesystem;

static std::wstring g_mainDirW;   // absolute path of main/ (wide)

static std::wstring exeDirW() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t s = p.find_last_of(L'\\');
    if (s != std::wstring::npos) p.resize(s);
    return p;
}

static std::string readFile(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static bool exists(const std::string &rel) {
    // fs::u8path interprets the string as UTF-8, so Chinese filenames
    // resolve correctly regardless of the active code page.
    return fs::exists(fs::path(g_mainDirW) / fs::u8path(rel));
}

// ---------------------------------------------------------------------------
// 1. Key files / directories exist
// ---------------------------------------------------------------------------
static void test_key_files_exist() {
    CHECK(exists("script/scripts/lua_api.txt"));
    CHECK(exists("DOCS_MANIFEST.md"));
    CHECK(exists("core/CrashReport.h"));
    CHECK(exists("core/ShortcutCore.h"));
    CHECK(exists("core/Config.h"));
    CHECK(exists("script/ScriptManager.h"));
    CHECK(exists("script/LuaParamParser.h"));
    CHECK(exists("ui/ParamDialog.h"));
    CHECK(exists("ui/ShortcutDialog.h"));
    CHECK(exists("ui/MatchHighlight.h"));
    CHECK(exists("LuaTool/main_lua.cpp"));
    CHECK(exists("AIChat/main_ai.cpp"));
    CHECK(exists("script/scripts/01.No/folder.ini"));
    CHECK(exists("script/scripts/04.Row/01.Insert/01.prefix.lua"));
}

// ---------------------------------------------------------------------------
// 2. Every test_*.cpp mentioned in 目录结构说明.md exists in test/
// ---------------------------------------------------------------------------
static void test_doc_test_list_matches() {
    // Wide literal: the doc filename is Chinese; a narrow literal would
    // be UTF-8 bytes misread as the ACP by std::filesystem.
    std::string doc = readFile(fs::path(g_mainDirW) / L"\u76ee\u5f55\u7ed3\u6784\u8bf4\u660e.md");
    CHECK(!doc.empty());
    size_t pos = 0, missing = 0, found = 0;
    while ((pos = doc.find("test_", pos)) != std::string::npos) {
        size_t end = pos;
        while (end < doc.size() && (isalnum((unsigned char)doc[end]) || doc[end] == '_' || doc[end] == '.'))
            ++end;
        std::string name = doc.substr(pos, end - pos);
        if (name.find(".cpp") == std::string::npos) { pos = end; continue; }
        if (exists("test/" + name)) ++found;
        else { ++missing; std::fprintf(stderr, "  doc mentions test/%s - MISSING\n", name.c_str()); }
        pos = end;
    }
    CHECK(found >= 7);      // the doc lists the older test set
    CHECK_EQ((int)missing, 0);
}

// ---------------------------------------------------------------------------
// 3. APIs documented in script/scripts/lua_api.txt are registered in LuaEngine.cpp
// ---------------------------------------------------------------------------
static void test_documented_apis_registered() {
    std::string doc = readFile(fs::path(g_mainDirW) / "script/scripts/lua_api.txt");
    std::string eng = readFile(fs::path(g_mainDirW) / "script/LuaEngine.cpp");
    std::set<std::string> missing;
    const char *kPrefixes[] = { "editor:", "win:" };
    size_t pos = 0;
    while (pos < doc.size()) {
        size_t best = std::string::npos;
        size_t bestLen = 0;
        for (const char *pfx : kPrefixes) {
            size_t f = doc.find(pfx, pos);
            if (f != std::string::npos && (best == std::string::npos || f < best)) {
                best = f;
                bestLen = strlen(pfx);
            }
        }
        if (best == std::string::npos) break;
        size_t nameStart = best + bestLen;   // skip the whole prefix
        size_t end = nameStart;
        while (end < doc.size() && (isalnum((unsigned char)doc[end]) || doc[end] == '_'))
            ++end;
        std::string api = doc.substr(nameStart, end - nameStart);
        if (!api.empty() && eng.find("\"" + api + "\"") == std::string::npos)
            missing.insert(api);
        pos = end;
    }
    for (const auto &m : missing)
        std::fprintf(stderr, "  documented API missing in LuaEngine: %s\n", m.c_str());
    CHECK(missing.empty());
}

// ---------------------------------------------------------------------------
// 4. lang en/zh-CN key sets are identical
// ---------------------------------------------------------------------------
static std::set<std::string> langKeys(const std::string &rel) {
    std::set<std::string> keys;
    std::string txt = readFile(fs::path(g_mainDirW) / fs::path(rel));
    std::istringstream ss(txt);
    std::string line;
    while (std::getline(ss, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        char c0 = line[0];
        if (c0 == '#' || c0 == '[') continue;
        keys.insert(line.substr(0, eq));
    }
    return keys;
}

// ---------------------------------------------------------------------------
// 4b. lang files must fit in the fixed I18n table (I18N_MAX_ENTRIES).
// I18n::load() silently drops every key past the cap, so an overflow shows up
// as missing UI strings rather than as an error. Fail the build instead.
// ---------------------------------------------------------------------------
static int langKeyCount(const std::string &rel) {
    std::string txt = readFile(fs::path(g_mainDirW) / fs::path(rel));
    std::istringstream ss(txt);
    std::string line;
    int n = 0;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = line;
        size_t b = t.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        if (t[b] == '#' || t[b] == '[') continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = t.substr(0, eq);
        size_t e = key.find_last_not_of(" \t");
        if (e == std::string::npos) continue;
        key = key.substr(0, e + 1);
        if (key.empty()) continue;
        ++n;
    }
    return n;
}

static void test_lang_entry_capacity() {
    const char *files[] = {"lang/en.ini", "lang/zh-CN.ini"};
    for (const char *f : files) {
        int n = langKeyCount(f);
        std::fprintf(stderr, "  %s: %d entries (cap %d)\n", f, n, I18N_MAX_ENTRIES);
        if (n > I18N_MAX_ENTRIES)
            std::fprintf(stderr, "  the last %d key(s) would be SILENTLY dropped\n",
                         n - I18N_MAX_ENTRIES);
        CHECK(n <= I18N_MAX_ENTRIES);
        // Warn (do not fail) when headroom gets low, so it is fixed early.
        if (n * 2 > I18N_MAX_ENTRIES)
            std::fprintf(stderr, "  note: %s is using >50%% of the lang table\n", f);
    }
}

static void test_lang_key_parity() {
    std::set<std::string> en = langKeys("lang/en.ini");
    std::set<std::string> zh = langKeys("lang/zh-CN.ini");
    for (const auto &k : en)
        if (!zh.count(k)) std::fprintf(stderr, "  en key missing in zh-CN: %s\n", k.c_str());
    for (const auto &k : zh)
        if (!en.count(k)) std::fprintf(stderr, "  zh-CN key missing in en: %s\n", k.c_str());
    CHECK_EQ((int)en.size(), (int)zh.size());
    CHECK(en == zh);
}

// ---------------------------------------------------------------------------
// 5. Third-party libs named in README.md exist under .thirdparty/
// ---------------------------------------------------------------------------
static void test_readme_thirdparty_dirs() {
    std::string readme = readFile(fs::path(g_mainDirW) / "README.md");
    auto thirdparty = [](const std::string &prefix) {
        fs::path tp = fs::path(g_mainDirW).parent_path() / ".thirdparty";
        if (!fs::exists(tp)) return false;
        for (const auto &e : fs::directory_iterator(tp))
            if (e.is_directory() &&
                e.path().filename().string().find(prefix) == 0)
                return true;
        return false;
    };
    CHECK(thirdparty("fltk-"));
    CHECK(thirdparty("lua-"));
    if (readme.find("PCRE2") != std::string::npos)
        CHECK(thirdparty("pcre2-"));
}

// ---------------------------------------------------------------------------
// 6. Every file listed in DOCS_MANIFEST.md exists
// ---------------------------------------------------------------------------
static void test_manifest_files_exist() {
    std::string manifest = readFile(fs::path(g_mainDirW) / "DOCS_MANIFEST.md");
    CHECK(!manifest.empty());
    std::istringstream ss(manifest);
    std::string line;
    int missing = 0, checked = 0;
    while (std::getline(ss, line)) {
        size_t bt = line.find('`');
        if (bt == std::string::npos) continue;
        size_t be = line.find('`', bt + 1);
        if (be == std::string::npos) continue;
        std::string path = line.substr(bt + 1, be - bt - 1);
        if (path.find('.') == std::string::npos) continue;
        // All manifest entries (incl. AGENTS.md) are resolved relative to
        // main/ — AGENTS.md was moved into main/ so it is version-controlled.
        std::string rel = path;
        if (exists(rel)) ++checked;
        else {
            ++missing;
            std::fprintf(stderr, "  manifest lists %s - MISSING\n", path.c_str());
        }
    }
    CHECK(checked >= 6);
    CHECK_EQ((int)missing, 0);
}

int main() {
    g_mainDirW = (fs::path(exeDirW()) / L".." / L"main").lexically_normal();
    test_key_files_exist();
    test_doc_test_list_matches();
    test_documented_apis_registered();
    test_lang_entry_capacity();
    test_lang_key_parity();
    test_readme_thirdparty_dirs();
    test_manifest_files_exist();
    int fails = test::failCount();
    std::fprintf(stderr, "%d checks, %d failures\n",
                 test::checkCount(), fails);
    return fails ? 1 : 0;
}
