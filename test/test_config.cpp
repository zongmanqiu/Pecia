// test_config.cpp - lock-in tests for Config: defaults, round-trip
// persistence, invalid-value repair, and recent-file list. Tests run in a
// scratch directory (Config::Config(path) test hook) so the real
// settings.ini next to Pecia.exe is never touched.
#include "test_assert.h"
#include "core/Config.h"

#include <FL/filename.H>   // FL_PATH_MAX

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static fs::path g_scratch;

static void makeScratch() {
    // Unique scratch dir per run under the test executable's directory.
    g_scratch = fs::temp_directory_path() / "pecia_cfg_test";
    fs::remove_all(g_scratch);
    fs::create_directories(g_scratch);
}

// ---------------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------------

static void test_defaults() {
    Config c(g_scratch);
    CHECK(c.getWrap());
    CHECK(c.getLineNumbers());
    CHECK_EQ(c.getTabWidth(), 4);
    CHECK(!c.getAutoIndent());           // default off (2026-08 调整)
    CHECK(c.getShowStatusbar());
    CHECK(!c.getMultiTab());            // default 0
    CHECK(!c.getAlwaysOnTop());         // default 0
    CHECK_EQ(c.getLongLineMarker(), 0);
    CHECK_EQ(c.getDialogPad(), 16);
    CHECK(c.getHighlightCurrentLine());
    CHECK(c.getShowWhitespace());
    CHECK(!c.getTrimTrailingWhitespace());  // default 0
    CHECK_EQ(c.getAutoSaveInterval(), 30);
    CHECK(c.getDetectUrls());

    char buf[64];
    c.getScheme(buf, sizeof(buf));
    CHECK(strcmp(buf, "gtk+") == 0);
    c.getLang(buf, sizeof(buf));
    CHECK(strcmp(buf, "en") == 0);
    c.getEditorFont(buf, sizeof(buf));
    CHECK(strcmp(buf, "Consolas") == 0);

    int font = 0, size = 0;
    c.getFont(font, size);
    CHECK(size >= 6 && size <= 48);
}

// ---------------------------------------------------------------------------
// Round-trip persistence
// ---------------------------------------------------------------------------

static void test_roundtrip() {
    {
        Config c(g_scratch);
        c.setTabWidth(8);
        c.setWrap(false);
        c.setMultiTab(true);
        c.setAutoSaveInterval(120);
        c.setLang("zh-CN");
    }
    // New instance reads the persisted values back.
    Config c2(g_scratch);
    CHECK_EQ(c2.getTabWidth(), 8);
    CHECK(!c2.getWrap());
    CHECK(c2.getMultiTab());
    CHECK_EQ(c2.getAutoSaveInterval(), 120);
    char buf[64];
    c2.getLang(buf, sizeof(buf));
    CHECK(strcmp(buf, "zh-CN") == 0);
}

// ---------------------------------------------------------------------------
// Language persistence: any shipped language code (not just en/zh-CN) must
// survive a save -> reload round-trip. Regression test for the bug where a
// hard-coded whitelist in Config::getLang reset every other language to
// English on restart.
// ---------------------------------------------------------------------------

static void test_lang_persists_non_whitelisted_code() {
    {
        Config c(g_scratch);
        c.setLang("ja");          // Japanese, not in the old {en,zh-CN} set
    }
    Config c2(g_scratch);
    char buf[64];
    c2.getLang(buf, sizeof(buf), "en");
    CHECK(strcmp(buf, "ja") == 0);   // must NOT be reset to "en"

    // And an empty stored value still falls back to the provided default.
    {
        Config c(g_scratch);
        c.writeStr("lang", "");
    }
    Config c3(g_scratch);
    c3.getLang(buf, sizeof(buf), "en");
    CHECK(strcmp(buf, "en") == 0);
}

// ---------------------------------------------------------------------------
// Invalid-value repair (values outside [min,max] fall back to defaults)
// ---------------------------------------------------------------------------

static void test_invalid_repair() {
    {
        Config c(g_scratch);
        // Write out-of-range values directly to the INI.
        c.writeInt("tab_width", 999);
        c.writeInt("font_size", 1);    // below min 6
        c.writeInt("wrap", 7);         // not 0/1
        c.saveAll();
    }
    Config c2(g_scratch);
    CHECK_EQ(c2.getTabWidth(), 4);      // repaired to default
    int font = 0, size = 0;
    c2.getFont(font, size);
    CHECK(size >= 6 && size <= 48);
    CHECK(c2.getWrap());                // repaired to default 1
}

// ---------------------------------------------------------------------------
// Value whitespace trimming (hand-edited INI: stray spaces after '=')
// ---------------------------------------------------------------------------

static void test_value_trim() {
    {
        // Write a value with leading/trailing spaces directly to the INI
        // (simulates a hand-edited file: "key= value " style).
        Config c(g_scratch);
        c.setAiKey("  abc123  ");   // setter stores verbatim
        c.saveAll();
    }
    // loadAll must trim the value on read.
    Config c2(g_scratch);
    char buf[FL_PATH_MAX];
    c2.getAiKey(buf, sizeof(buf), "");
    CHECK(strcmp(buf, "abc123") == 0);
}

// ---------------------------------------------------------------------------
// Recent-file list
// ---------------------------------------------------------------------------

static void test_recent_files() {
    {
        Config c(g_scratch);
        c.recentClear();
        c.recentAdd("C:\\work\\a.txt");
        c.recentAdd("C:\\work\\b.txt");
        c.recentAdd("C:\\work\\c.txt");
    }
    Config c2(g_scratch);
    char buf[FL_PATH_MAX];
    c2.recentGet(0, buf, sizeof(buf));
    CHECK(strcmp(buf, "C:\\work\\c.txt") == 0);   // most recent first
    c2.recentGet(1, buf, sizeof(buf));
    CHECK(strcmp(buf, "C:\\work\\b.txt") == 0);
    c2.recentGet(2, buf, sizeof(buf));
    CHECK(strcmp(buf, "C:\\work\\a.txt") == 0);
    c2.recentClear();
    c2.recentGet(0, buf, sizeof(buf));
    CHECK(buf[0] == '\0');
}

// Two Config objects model two Pecia windows (single-tab mode starts one
// process per opened file). recentAdd must merge with what is already on
// disk - the old code wrote this process's startup snapshot back and erased
// entries other windows had just added.
static void test_recent_files_multi_process() {
    {
        Config a(g_scratch);
        a.recentClear();
        Config b(g_scratch);                 // both saw an empty list
        a.recentAdd("C:\\work\\a.txt");
        b.recentAdd("C:\\work\\b.txt");
        Config c(g_scratch);
        c.recentAdd("C:\\work\\c.txt");
    }
    Config c(g_scratch);
    char buf[FL_PATH_MAX];
    c.recentGet(0, buf, sizeof(buf));
    CHECK(strcmp(buf, "C:\\work\\c.txt") == 0);
    c.recentGet(1, buf, sizeof(buf));
    CHECK(strcmp(buf, "C:\\work\\b.txt") == 0);
    c.recentGet(2, buf, sizeof(buf));
    CHECK(strcmp(buf, "C:\\work\\a.txt") == 0);
}

// A malformed line must not wipe the whole history (the old parser deleted
// "recent file.ini" on the first unrecognised line).
static void test_recent_files_tolerant_parse() {
    {
        Config c(g_scratch);
        c.recentClear();
        c.recentAdd("C:\\work\\keep.txt");
    }
    const fs::path p = g_scratch / "recent file.ini";
    {
        std::ofstream out(p, std::ios::binary | std::ios::app);
        out << "not a recent-file line\n";
        out << "File2=\n";
    }
    Config c(g_scratch);
    char buf[FL_PATH_MAX];
    CHECK_EQ(c.recentCount(), 1);
    c.recentGet(0, buf, sizeof(buf));
    CHECK(strcmp(buf, "C:\\work\\keep.txt") == 0);
    CHECK(fs::exists(p));                    // not deleted
}

// Non-ASCII config directory: fl_fopen(path::string()) treated the ANSI
// bytes as UTF-8 and failed silently, so the list file was never created.
static void test_recent_files_unicode_dir() {
    const fs::path dir = g_scratch / fs::path(L"\u6D4B\u8BD5\u76EE\u5F55");
    std::error_code ec;
    fs::create_directories(dir, ec);
    {
        Config c(dir);
        c.recentClear();
        c.recentAdd("C:\\work\\a.txt");
        c.recentAdd("C:\\work\\b.txt");
    }
    Config c2(dir);
    char buf[FL_PATH_MAX];
    CHECK_EQ(c2.recentCount(), 2);
    c2.recentGet(0, buf, sizeof(buf));
    CHECK(strcmp(buf, "C:\\work\\b.txt") == 0);
    CHECK(fs::exists(dir / "recent file.ini"));
}

// recentSync() lets a window pick up entries another window wrote while it
// was already running.
static void test_recent_sync() {
    Config stale(g_scratch);                 // startup snapshot
    {
        Config other(g_scratch);
        other.recentClear();
        other.recentAdd("C:\\work\\new.txt");
    }
    char buf[FL_PATH_MAX];
    stale.recentGet(0, buf, sizeof(buf));    // cache is still the old one
    CHECK(strcmp(buf, "C:\\work\\new.txt") != 0);
    stale.recentSync();
    stale.recentGet(0, buf, sizeof(buf));
    CHECK(strcmp(buf, "C:\\work\\new.txt") == 0);
}

// ---------------------------------------------------------------------------
// Write whitelist: a restricted Config may only persist its allowed keys
// ---------------------------------------------------------------------------

static void test_write_whitelist() {
    Config c(g_scratch);
    c.setWriteWhitelist({"ai_endpoint", "ai_model", "ai_api_key"});
    // Simulate another process having set the language to zh-CN on disk.
    c.setLang("en");                     // dirty: language (not whitelisted)
    c.saveAll();
    {
        // Rewrite the file by hand with a Chinese language, as the main
        // process would after a language switch.
        std::ofstream(g_scratch / "settings.ini", std::ios::trunc)
            << "[Settings]\n"
            << "lang=zh-CN\n"
            << "ai_model=glm-4-flash\n"
            << "ai_api_key=oldkey\n";
    }
    // AI process changes only its key; saveAll must keep zh-CN + oldkey.
    c.setAiKey("newkey");
    c.saveAll();
    {
        Config c2(g_scratch);
        char buf[FL_PATH_MAX];
        c2.getLang(buf, sizeof(buf), "");
        CHECK(strcmp(buf, "zh-CN") == 0);       // language untouched
        c2.getAiKey(buf, sizeof(buf), "");
        CHECK(strcmp(buf, "newkey") == 0);      // ai key written
    }
}

// ---------------------------------------------------------------------------
// Dirty-merge: a plain process must not overwrite disk values it did not
// change, even if its in-memory snapshot is stale
// ---------------------------------------------------------------------------

static void test_merge_preserves_disk() {
    {
        Config c(g_scratch);
        c.setTabWidth(4);
    }
    {
        // Simulate another process switching the language on disk.
        std::ofstream(g_scratch / "settings.ini", std::ios::trunc)
            << "[Settings]\n"
            << "tab_width=8\n"
            << "lang=zh-CN\n";
    }
    Config c(g_scratch);                 // loads zh-CN + tab_width=8
    // A stale snapshot (constructed before the switch) writes a value.
    Config c_stale(g_scratch);
    c_stale.setUiFontSize(20);           // dirty: ui_font_size only
    c_stale.saveAll();
    {
        Config c2(g_scratch);
        char buf[FL_PATH_MAX];
        c2.getLang(buf, sizeof(buf), "");
        CHECK(strcmp(buf, "zh-CN") == 0);       // kept from disk
        CHECK_EQ(c2.getTabWidth(), 8);          // kept from disk
        CHECK_EQ(c2.getUiFontSize(), 20);       // our write applied
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

static void run_all() {
    makeScratch();
    test_defaults();
    test_roundtrip();
    test_lang_persists_non_whitelisted_code();
    test_invalid_repair();
    test_recent_files();
    test_recent_files_multi_process();
    test_recent_files_tolerant_parse();
    test_recent_files_unicode_dir();
    test_recent_sync();
    test_value_trim();
    test_write_whitelist();
    test_merge_preserves_disk();
}

int main() {
    run_all();
    int fails = test::failCount();
    std::fprintf(stderr, "%d checks, %d failures\n",
                 test::checkCount(), fails);
    return fails ? 1 : 0;
}
