// Theme.cpp - Theme implementation.
//
// Colors come from per-theme files under exeDir/theme/<name>.txt (mirroring
// exeDir/lang/<code>.txt). settings.ini only stores theme.name (the chosen
// theme); the actual palette is read from the theme file at load time. The
// light defaults below are a fallback only, so a missing/corrupt theme file
// never leaves the app invisible.
#include "Theme.h"
#include "Config.h"
#include "PathUtils.h"
#include <FL/filename.H>   // FL_PATH_MAX
#include <FL/fl_utf8.h>    // fl_fopen (UTF-8 safe)
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#endif


// Parse a color value: "#RRGGBB" or "R,G,B".
static bool parseColor(const char *str, Fl_Color &out) {
    if (!str || !*str) return false;
    if (str[0] == '#') {
        unsigned r = 0, g = 0, b = 0;
        if (sscanf(str + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
            out = fl_rgb_color((uchar)r, (uchar)g, (uchar)b);
            return true;
        }
        return false;
    }
    int r = 0, g = 0, b = 0;
    if (sscanf(str, "%d,%d,%d", &r, &g, &b) == 3) {
        out = fl_rgb_color((uchar)r, (uchar)g, (uchar)b);
        return true;
    }
    return false;
}

// Built-in light defaults: used as the initial object and as the fallback
// when a theme file is missing or corrupt. NOT the source of truth — the
// palette normally comes from exeDir/theme/<name>.txt.
static ThemeColors lightDefaults() {
    // ThemeColors has in-class light defaults; nothing extra to set here.
    return ThemeColors();
}

// Apply one color key to the matching field. Returns true if the key was
// recognised and the value parsed; an unrecognised key or a bad value is
// ignored (so a partial/corrupt key never wipes a colour).
static bool applyKey(ThemeColors &tc, const char *key, const char *val) {
    Fl_Color c;
    if (parseColor(val, c)) {
        if      (strcmp(key, "text1")       == 0) { tc.text1       = c; return true; }
        else if (strcmp(key, "text2")       == 0) { tc.text2       = c; return true; }
        else if (strcmp(key, "background1") == 0) { tc.background1 = c; return true; }
        else if (strcmp(key, "background2") == 0) { tc.background2 = c; return true; }
        else if (strcmp(key, "background3") == 0) { tc.background3 = c; return true; }
        else if (strcmp(key, "highlight1")  == 0) { tc.highlight1  = c; return true; }
        else if (strcmp(key, "highlight2")  == 0) { tc.highlight2  = c; return true; }
        else if (strcmp(key, "highlight3")  == 0) { tc.highlight3  = c; return true; }
    }
    // Non-color keys: display name translations.
    if (strcmp(key, "name") == 0) {
        tc.displayName = val;
        return true;
    }
    // name.zh-CN, name.ja, name.ko, etc.
    if (strncmp(key, "name.", 5) == 0) {
        const char *lang = key + 5;
        if (strcmp(lang, "zh-CN") == 0) {
            tc.displayNameZh = val;
            return true;
        }
        // Future: store other languages in a map if needed.
    }
    return false;
}

int ThemeColors::loadFromFile(const char *path) {
    FILE *fp = path ? fl_fopen(path, "rb") : nullptr;
    if (!fp) return -1;
    int applied = 0;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        // Skip comments and blank lines.
        char *p = line;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
        if (*p == 0 || *p == '#') continue;
        // Split key = value at the first '='.
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = p;
        char *val = eq + 1;
        // Trim key trailing spaces.
        char *e = key + strlen(key);
        while (e > key && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
        // Trim val leading spaces.
        while (*val == ' ' || *val == '\t') ++val;
        // Trim val trailing spaces/\r/\n.
        char *ve = val + strlen(val);
        while (ve > val && (ve[-1] == ' ' || ve[-1] == '\t' || ve[-1] == '\r' || ve[-1] == '\n')) *--ve = 0;
        if (applyKey(*this, key, val)) ++applied;
    }
    fclose(fp);
    return applied;
}

// ---------------------------------------------------------------------------
// Themed FL_BORDER_BOX replacement
// ---------------------------------------------------------------------------

// Global border colour — initialised to the light-theme borderLine()
// default so the app looks right even before the first theme load.
Fl_Color g_borderColour = fl_color_average(fl_rgb_color(0, 0, 0),
                                           fl_rgb_color(255, 255, 255), 0.55f);
Fl_Color g_popupCheckColor = 0;  // set before popup() to theme highlight2

// Custom draw function for FL_BORDER_BOX that uses g_borderColour
// instead of FLTK's hardcoded FL_BLACK.
static void themed_border_box(int x, int y, int w, int h, Fl_Color bgcolor) {
    Fl::set_box_color(bgcolor);
    fl_rectf(x, y, w, h);
    Fl::set_box_color(g_borderColour);
    fl_rect(x, y, w, h);
}

// Custom draw function for FL_BORDER_FRAME that uses g_borderColour
// instead of FLTK's hardcoded FL_BLACK.
static void themed_border_frame(int x, int y, int w, int h, Fl_Color /*c*/) {
    Fl::set_box_color(g_borderColour);
    fl_rect(x, y, w, h);
}

void theme_registerBorderBox() {
    // Replace FLTK's built-in FL_BORDER_BOX draw function + insets
    // (1,1,2,2 = 1px border on all sides, matching the stock definition).
    Fl::set_boxtype(FL_BORDER_BOX, themed_border_box, 1, 1, 2, 2);
    
    // Replace FLTK's built-in FL_BORDER_FRAME draw function + insets
    // (1,1,2,2 = 1px border on all sides, matching the stock definition).
    Fl::set_boxtype(FL_BORDER_FRAME, themed_border_frame, 1, 1, 2, 2);
}

// ---------------------------------------------------------------------------

Theme::Theme() {
    m_colors = lightDefaults();
    m_preset = THEME_PRESET_LIGHT;
}

std::string Theme::displayName(const char *lang) const {
    // Try Chinese first if requested.
    if (lang && strcmp(lang, "zh-CN") == 0 && !m_colors.displayNameZh.empty()) {
        return m_colors.displayNameZh;
    }
    // Fall back to English name.
    if (!m_colors.displayName.empty()) {
        return m_colors.displayName;
    }
    // Last resort: return the preset key itself.
    return m_preset;
}

void Theme::load(const Config &cfg) {
    char name[16];
    cfg.readStr("theme.name", name, sizeof(name), THEME_PRESET_LIGHT);
    // Accept any theme name; if no matching theme file exists, light.txt is
    // the fallback (via lightDefaults or by re-resolving below).
    m_preset = name ? name : THEME_PRESET_LIGHT;

    // Resolve exeDir/theme/<name>.txt; start from the light fallback, then
    // overlay the theme file. If the file is missing we keep light.
    std::string dir = pathutil::exeDir();
    m_colors = lightDefaults();
    if (!dir.empty() && !m_preset.empty()) {
        char path[FL_PATH_MAX];
        snprintf(path, sizeof(path), "%s/theme/%s.ini", dir.c_str(), m_preset.c_str());
        if (m_colors.loadFromFile(path) < 0) {
            // Missing/corrupt file -> light fallback and remember it.
            m_preset = THEME_PRESET_LIGHT;
            m_colors = lightDefaults();
        }
    }
}

void Theme::save(Config &cfg) {
    // settings.ini only records which theme is selected. The color values
    // live in the theme file, so nothing else is persisted here.
    cfg.writeStr("theme.name", m_preset.c_str());
}

void Theme::setPreset(const char *name, Config &cfg) {
    if (!name || !*name) return;
    m_preset = name;
    cfg.writeStr("theme.name", m_preset.c_str());
}

// Scan theme/ directory and return all .ini file stems (excluding
// light/dark which are built-in).
std::vector<std::string> Theme::scanExtraThemes() {
    std::vector<std::string> themes;
    std::string dir = pathutil::exeDir();
    if (dir.empty()) return themes;

    char pattern[FL_PATH_MAX];
    snprintf(pattern, sizeof(pattern), "%s/theme/*.ini", dir.c_str());

    // Convert to wide string for Windows API
    wchar_t wPattern[FL_PATH_MAX];
    int len = MultiByteToWideChar(CP_UTF8, 0, pattern, -1, wPattern, FL_PATH_MAX);
    if (len <= 0) return themes;

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(wPattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return themes;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        // Convert wide filename to UTF-8
        char fileName[256];
        WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, fileName, sizeof(fileName), nullptr, nullptr);

        // Extract stem (remove .ini extension)
        char *dot = strrchr(fileName, '.');
        if (dot) *dot = '\0';

        // Skip built-in themes
        if (strcmp(fileName, "light") == 0) continue;

        themes.push_back(fileName);
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return themes;
}
