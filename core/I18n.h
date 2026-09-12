// I18n.h - simple key=value string table loaded from lang/*.txt
#pragma once

#include <string>
#include <vector>

// Maximum number of string entries we expect to load.
//
// WARNING: I18n::load() stops reading the file once this many entries have
// been stored, and it does so SILENTLY -- every remaining key in the file is
// dropped and I18n::get() then echoes the raw key back for those. That once
// hid a real bug: adding 8 keys pushed lang/*.ini past 256 and the last 5
// (about.donate / about.wechat / about.alipay / tabs.closeothers /
// tabs.copypath) silently vanished, so the About dialog lost its "赞助" line.
// Keep headroom here, and rely on the capacity check in test_docs to catch
// any future overflow at build time instead of at runtime.
#define I18N_MAX_ENTRIES 512
// Maximum length of a single value string
#define I18N_MAX_LEN 256

// I18n
//   Loads key=value pairs from lang/<code>.txt. Lines starting with '#'
//   or '[' are comments; everything is UTF-8. The class is a singleton:
//   call I18n::load(code) once at startup, then I18n::get(key) anywhere.
class I18n {
public:
    // Load a language file (e.g. "en", "zh-CN"). Returns false on failure.
    static bool load(const char *code);

    // Look up a key. Returns the value if found, otherwise returns the
    // key itself so missing strings are visible during development.
    static const char *get(const char *key);

    // Look up a key with an explicit fallback: returns the value if
    // found, otherwise `fallback`. Used when the caller wants to hide
    // missing keys (e.g. user-supplied script names that have no
    // translation entry).
    static const char *getOr(const char *key, const char *fallback);

    // Current language code (e.g. "en", "zh-CN") for display.
    static const char *currentCode();

    // Detect the OS user's UI language and return a language code we
    // actually have a file for: the mapped candidate (e.g. "zh-CN") if
    // loading it succeeds, otherwise "en". Used on first run to seed
    // settings.ini with the system language.
    static const char *detectSystemLang();

    // Localized reason for an OS error code (`errno`). Uses the errno.<code>
    // translation keys when present (so the message matches the UI language
    // regardless of the OS locale); falls back to the raw strerror() text.
    static const char *strerrorLocalized(int errn);

    // Clear all loaded strings (called by load before populating).
    static void clear();

    // Scan lang/ directory and return all language codes and display names.
    // Used to populate the Language menu dynamically.
    struct LangInfo {
        std::string code;       // e.g. "en", "zh-CN"
        std::string displayName; // e.g. "English", "简体中文"
    };
    static std::vector<LangInfo> scanLanguages();

private:
    struct Entry {
        char key[I18N_MAX_LEN];
        char value[I18N_MAX_LEN];
    };

    static Entry  s_entries[I18N_MAX_ENTRIES];
    static int    s_count;
    static char   s_code[16];
};
