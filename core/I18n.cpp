// I18n.cpp - simple key=value string table loader
#include "I18n.h"

#include <FL/filename.H>
#include <FL/fl_string_functions.h>
#include <FL/fl_utf8.h>      // fl_fopen

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <errno.h>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

// Static storage
I18n::Entry I18n::s_entries[I18N_MAX_ENTRIES];
int        I18n::s_count = 0;
char       I18n::s_code[16] = "en";

// Trim leading/trailing whitespace in place. Returns the new start pointer.
static char *trim(char *s) {
    while (*s && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) ++s;
    if (*s == 0) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        *end = 0;
        --end;
    }
    return s;
}

// Resolve the directory containing the running executable. Returns an
// empty string on platforms where we can't determine it, in which case
// the caller falls back to the CWD-relative path. This must match the
// behaviour of Config::getExeDir() so that lang files and settings.ini
// are found in the same place regardless of the working directory the
// app was launched from (Explorer double-click, IDE, Start menu, etc.).
static std::string exeDir() {
#if defined(_WIN32)
    wchar_t wbuf[FL_PATH_MAX] = L"";
    if (GetModuleFileNameW(nullptr, wbuf, FL_PATH_MAX) > 0) {
        int len = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            std::string utf8(len - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, &utf8[0], len, nullptr, nullptr);
            auto p = utf8.find_last_of("\\/");
            if (p != std::string::npos) utf8.resize(p);
            return utf8;
        }
    }
#endif
    return std::string();
}

bool I18n::load(const char *code) {
    if (!code || !code[0]) return false;

    // Try, in order:
    //   1. <exe dir>/lang/<code>.txt  - works regardless of CWD
    //   2. lang/<code>.txt            - relative to CWD (legacy fallback)
    // The exe-dir lookup mirrors Config::getExeDir() so lang files and
    // settings.ini always resolve to the same directory. Without it,
    // launching Pecia from anywhere other than build/ causes I18n::load
    // to fail and I18n::get() returns the raw keys ("menu.file", ...).
    char path[FL_PATH_MAX];
    FILE *fp = nullptr;

    std::string dir = exeDir();
    if (!dir.empty()) {
        snprintf(path, sizeof(path), "%s/lang/%s.txt", dir.c_str(), code);
        fp = fl_fopen(path, "rb");
    }
    if (!fp) {
        snprintf(path, sizeof(path), "lang/%s.txt", code);
        fp = fl_fopen(path, "rb");
    }
    if (!fp) return false;

    clear();
    fl_strlcpy(s_code, code, sizeof(s_code));

    char line[I18N_MAX_LEN + 64];
    bool firstLine = true;
    while (fgets(line, sizeof(line), fp)) {
        // Strip a UTF-8 BOM if present (some editors add one; the BOM
        // bytes would otherwise corrupt the first key's lookup).
        if (firstLine) {
            unsigned char *u = (unsigned char *)line;
            if (u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF)
                memmove(line, line + 3, strlen(line + 3) + 1);
            firstLine = false;
        }
        // Skip comments and section markers
        char *p = trim(line);
        if (*p == 0 || *p == '#' || *p == '[') continue;

        // Split at first '='
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = trim(p);
        char *val = trim(eq + 1);

        // Unescape the literal 2-char sequence \n into a real newline so
        // multi-line dialog messages can be stored in one key=value line.
        // No other escape sequences are interpreted.
        {
            char *src = val, *dst = val;
            while (*src) {
                if (src[0] == '\\' && src[1] == 'n') { *dst++ = '\n'; src += 2; }
                else *dst++ = *src++;
            }
            *dst = 0;
        }

        if (s_count >= I18N_MAX_ENTRIES) break;
        fl_strlcpy(s_entries[s_count].key,   key, I18N_MAX_LEN);
        fl_strlcpy(s_entries[s_count].value, val, I18N_MAX_LEN);
        ++s_count;
    }
    fclose(fp);
    return true;
}

const char *I18n::get(const char *key) {
    if (!key) return "";
    for (int i = 0; i < s_count; ++i) {
        if (strcmp(s_entries[i].key, key) == 0)
            return s_entries[i].value;
    }
    // Return the key itself as a fallback so missing strings are obvious
    return key;
}

const char *I18n::currentCode() {
    return s_code;
}

// Detect the OS user's UI language (Windows): map the LANGID to one of
// our language files, then verify the file actually exists by loading
// it - if the mapped candidate has no file, fall back to English.
const char *I18n::detectSystemLang() {
#if defined(_WIN32)
    const LANGID id = GetUserDefaultUILanguage();
    const char *candidate = "en";
    switch (id) {
        case 0x0009:                       candidate = "en";    break; // en
        case 0x0409:                       candidate = "en";    break; // en-US
        case 0x0804:                       candidate = "zh-CN"; break; // zh-CN
        case 0x0404:                       candidate = "zh-CN"; break; // zh-TW (no zh-TW.txt; zh-CN is closest shipped)
        default:
            // Any other Chinese sublanguage (zh-HK, zh-SG, ...) has no
            // file; zh-CN is the closest match we ship.
            if ((id & 0xFF) == LANG_CHINESE) candidate = "zh-CN";
            break;
    }
    if (load(candidate)) return candidate;
#endif
    load("en");
    return "en";
}

const char *I18n::getOr(const char *key, const char *fallback) {
    if (!key) return fallback ? fallback : "";
    for (int i = 0; i < s_count; ++i) {
        if (strcmp(s_entries[i].key, key) == 0)
            return s_entries[i].value;
    }
    return fallback ? fallback : "";
}

void I18n::clear() {
    s_count = 0;
    s_code[0] = 0;
}

const char *I18n::strerrorLocalized(int errn) {
    char key[32];
    snprintf(key, sizeof(key), "errno.%d", errn);
    const char *v = get(key);
    // Only accept a real localized value (not the key echoed back for a
    // missing key), otherwise fall back to the OS text.
    if (v != key && v && *v) return v;
    return strerror(errn);
}
