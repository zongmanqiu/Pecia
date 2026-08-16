// Theme.cpp - Theme implementation.
//
// Colors come from per-theme files under exeDir/theme/<name>.txt (mirroring
// exeDir/lang/<code>.txt). settings.ini only stores theme.name (the chosen
// theme); the actual palette is read from the theme file at load time. The
// light defaults below are a fallback only, so a missing/corrupt theme file
// never leaves the app invisible.
#include "Theme.h"
#include "Config.h"
#include <FL/filename.H>   // FL_PATH_MAX
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#endif

// Resolve the directory containing the running executable (same rule as
// Config::getExeDir / I18n's exeDir) so theme/<name>.txt is found next to
// the exe regardless of the working directory.
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
    ThemeColors tc;
    tc.bgChrome        = fl_rgb_color(235, 235, 235);
    tc.scrollbarThumb  = fl_rgb_color(170, 170, 170);
    tc.scrollbarTrack  = fl_rgb_color(235, 235, 235);
    tc.bgPanel         = fl_rgb_color(245, 245, 245);
    tc.bgEditor        = fl_rgb_color(255, 255, 255);
    tc.textPrimary     = fl_rgb_color(0, 0, 0);
    tc.textSecondary   = fl_rgb_color(170, 170, 170);
    tc.accentSelection = fl_rgb_color(191, 255, 255);
    tc.searchHighlight = fl_rgb_color(255, 191, 255);
    tc.lineHighlight   = fl_rgb_color(255, 255, 191);
    tc.linkHover       = fl_rgb_color(6, 69, 173);
    tc.hoverBtn        = fl_rgb_color(225, 225, 225);
    tc.hoverClose      = fl_rgb_color(255, 0, 0);
    tc.borderColor     = fl_rgb_color(127, 127, 127);
    return tc;
}

// Apply one color key to the matching field. Returns true if the key was
// recognised and the value parsed; an unrecognised key or a bad value is
// ignored (so a partial/corrupt key never wipes a colour).
static bool applyKey(ThemeColors &tc, const char *key, const char *val) {
    Fl_Color c;
    if (!parseColor(val, c)) return false;
    if      (strcmp(key, "bg_chrome")    == 0) { tc.bgChrome      = c; return true; }
    else if (strcmp(key, "scrollbar_thumb")==0){ tc.scrollbarThumb= c; return true; }
    else if (strcmp(key, "scrollbar_track")==0){ tc.scrollbarTrack= c; return true; }
    else if (strcmp(key, "bg_panel")     == 0) { tc.bgPanel       = c; return true; }
    else if (strcmp(key, "bg_editor")    == 0) { tc.bgEditor      = c; return true; }
    else if (strcmp(key, "text_primary") == 0) { tc.textPrimary   = c; return true; }
    else if (strcmp(key, "text_secondary")==0){ tc.textSecondary = c; return true; }
    else if (strcmp(key, "accent_selection")==0){ tc.accentSelection = c; return true; }
    else if (strcmp(key, "search_highlight")==0){ tc.searchHighlight = c; return true; }
    else if (strcmp(key, "line_highlight")==0) { tc.lineHighlight = c; return true; }
    else if (strcmp(key, "link_hover")   == 0) { tc.linkHover     = c; return true; }
    else if (strcmp(key, "hover_btn")    == 0) { tc.hoverBtn      = c; return true; }
    else if (strcmp(key, "hover_close")  == 0) { tc.hoverClose    = c; return true; }
    else if (strcmp(key, "border_color") == 0) { tc.borderColor   = c; return true; }
    return false;
}

int ThemeColors::loadFromFile(const char *path) {
    FILE *fp = path ? fopen(path, "rb") : nullptr;
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

Theme::Theme() {
    m_colors = lightDefaults();
    m_preset = THEME_PRESET_LIGHT;
}

void Theme::load(const Config &cfg) {
    char name[16];
    cfg.readStr("theme.name", name, sizeof(name), THEME_PRESET_LIGHT);
    // Accept any theme name; if no matching theme file exists, light.txt is
    // the fallback (via lightDefaults or by re-resolving below).
    m_preset = name ? name : THEME_PRESET_LIGHT;

    // Resolve exeDir/theme/<name>.txt; start from the light fallback, then
    // overlay the theme file. If the file is missing we keep light.
    std::string dir = exeDir();
    m_colors = lightDefaults();
    if (!dir.empty() && !m_preset.empty()) {
        char path[FL_PATH_MAX];
        snprintf(path, sizeof(path), "%s/theme/%s.txt", dir.c_str(), m_preset.c_str());
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
