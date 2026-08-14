// I18n.h - simple key=value string table loaded from lang/*.txt
#pragma once

// Maximum number of string entries we expect to load
#define I18N_MAX_ENTRIES 256
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

private:
    struct Entry {
        char key[I18N_MAX_LEN];
        char value[I18N_MAX_LEN];
    };

    static Entry  s_entries[I18N_MAX_ENTRIES];
    static int    s_count;
    static char   s_code[16];
};
