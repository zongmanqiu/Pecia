// I18n.cpp - simple key=value string table loader
#include "I18n.h"
#include "PathUtils.h"

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

    std::string dir = pathutil::exeDir();
    if (!dir.empty()) {
        snprintf(path, sizeof(path), "%s/lang/%s.ini", dir.c_str(), code);
        fp = fl_fopen(path, "rb");
    }
    if (!fp) {
        snprintf(path, sizeof(path), "lang/%s.ini", code);
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

// Scan lang/ directory and return all language codes and display names.
std::vector<I18n::LangInfo> I18n::scanLanguages() {
    std::vector<LangInfo> languages;
    std::string dir = pathutil::exeDir();
    if (dir.empty()) return languages;

    char pattern[FL_PATH_MAX];
    snprintf(pattern, sizeof(pattern), "%s/lang/*.ini", dir.c_str());

    wchar_t wPattern[FL_PATH_MAX];
    int len = MultiByteToWideChar(CP_UTF8, 0, pattern, -1, wPattern, FL_PATH_MAX);
    if (len <= 0) return languages;

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(wPattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return languages;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        char fileName[256];
        WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, fileName, sizeof(fileName), nullptr, nullptr);

        // Extract stem (remove .ini extension)
        char *dot = strrchr(fileName, '.');
        if (dot) *dot = '\0';

        // Read the display name from the file (name = line)
        char filePath[FL_PATH_MAX];
        snprintf(filePath, sizeof(filePath), "%s/lang/%s.ini", dir.c_str(), fileName);
        FILE *fp = fl_fopen(filePath, "r");
        char displayName[256] = "";
        if (fp) {
            char line[512];
            while (fgets(line, sizeof(line), fp)) {
                if (strncmp(line, "name =", 6) == 0) {
                    char *val = line + 6;
                    while (*val == ' ' || *val == '\t') ++val;
                    // Trim newline
                    char *nl = strchr(val, '\n');
                    if (nl) *nl = '\0';
                    fl_strlcpy(displayName, val, sizeof(displayName));
                    break;
                }
            }
            fclose(fp);
        }

        // If no name found, use the code itself
        if (!displayName[0]) {
            fl_strlcpy(displayName, fileName, sizeof(displayName));
        }

        languages.push_back({fileName, displayName});
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return languages;
}
