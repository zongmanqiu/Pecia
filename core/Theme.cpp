// Theme.cpp - Theme implementation (preset-aware: light / dark).
#include "Theme.h"
#include "Config.h"
#include <stdio.h>
#include <string.h>

static Fl_Color parseHexOrRgb(const char *str, Fl_Color fallback) {
    if (!str || !*str) return fallback;
    if (str[0] == '#') {
        unsigned r = 0, g = 0, b = 0;
        if (sscanf(str + 1, "%02x%02x%02x", &r, &g, &b) == 3)
            return fl_rgb_color((uchar)r, (uchar)g, (uchar)b);
        return fallback;
    }
    int r = 0, g = 0, b = 0;
    if (sscanf(str, "%d,%d,%d", &r, &g, &b) == 3)
        return fl_rgb_color((uchar)r, (uchar)g, (uchar)b);
    return fallback;
}

static void formatHex(Fl_Color c, char *buf, int len) {
    unsigned char r, g, b;
    Fl::get_color(c, r, g, b);
    snprintf(buf, len, "#%02X%02X%02X", (unsigned)r, (unsigned)g, (unsigned)b);
}

static Fl_Color readThemeColor(const Config &cfg, const char *key, Fl_Color fallback) {
    char defaultHex[8];
    formatHex(fallback, defaultHex, sizeof(defaultHex));
    char val[16];
    cfg.readStr(key, val, sizeof(val), defaultHex);
    return parseHexOrRgb(val, fallback);
}

static void writeThemeColor(Config &cfg, const char *key, Fl_Color color) {
    char buf[8];
    formatHex(color, buf, sizeof(buf));
    cfg.writeStr(key, buf);
}

// ---- Light preset (default / current look) --------------------------------
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
    tc.linkHover       = fl_rgb_color(6, 69, 173);   // #0645AD
    tc.hoverBtn        = fl_rgb_color(225, 225, 225);
    tc.hoverClose      = fl_rgb_color(255, 0, 0);
    tc.borderColor     = fl_rgb_color(127, 127, 127);
    return tc;
}

// ---- Dark preset (modern, low-contrast) -----------------------------------
static ThemeColors darkDefaults() {
    ThemeColors tc;
    // Dark gray chrome/frame, slightly lighter editor, near-white text.
    tc.bgChrome        = fl_rgb_color(45, 50, 56);
    tc.scrollbarThumb  = fl_rgb_color(90, 98, 108);
    tc.scrollbarTrack  = fl_rgb_color(35, 39, 45);
    tc.bgPanel         = fl_rgb_color(38, 42, 48);
    tc.bgEditor        = fl_rgb_color(30, 33, 38);
    tc.textPrimary     = fl_rgb_color(220, 222, 226);
    tc.textSecondary   = fl_rgb_color(135, 141, 150);
    tc.accentSelection = fl_rgb_color(70, 120, 200);   // subdued blue selection
    tc.searchHighlight = fl_rgb_color(120, 70, 150);   // muted magenta
    tc.lineHighlight   = fl_rgb_color(60, 70, 40);     // muted olive line highlight
    tc.linkHover       = fl_rgb_color(90, 160, 255);   // brighter link on dark
    tc.hoverBtn        = fl_rgb_color(60, 66, 74);
    tc.hoverClose      = fl_rgb_color(200, 45, 50);
    tc.borderColor     = fl_rgb_color(120, 126, 134);   // lighter so the 1px frame reads on dark
    return tc;
}

bool ThemeColors::applyPreset(const char *name) {
    if (!name) return false;
    if (strcmp(name, THEME_PRESET_LIGHT) == 0) { *this = lightDefaults(); return true; }
    if (strcmp(name, THEME_PRESET_DARK)  == 0) { *this = darkDefaults();  return true; }
    return false;
}

// ---- ThemeColors Serialization -------------------------------------------

void ThemeColors::loadFrom(const Config &cfg) {
    bgChrome        = readThemeColor(cfg, "theme.bg_chrome",          bgChrome);
    scrollbarThumb  = readThemeColor(cfg, "theme.scrollbar_thumb",    scrollbarThumb);
    scrollbarTrack  = readThemeColor(cfg, "theme.scrollbar_track",    scrollbarTrack);
    bgPanel         = readThemeColor(cfg, "theme.bg_panel",           bgPanel);
    bgEditor        = readThemeColor(cfg, "theme.bg_editor",          bgEditor);
    textPrimary     = readThemeColor(cfg, "theme.text_primary",       textPrimary);
    textSecondary   = readThemeColor(cfg, "theme.text_secondary",     textSecondary);
    accentSelection = readThemeColor(cfg, "theme.accent_selection",   accentSelection);
    searchHighlight = readThemeColor(cfg, "theme.search_highlight",   searchHighlight);
    lineHighlight   = readThemeColor(cfg, "theme.line_highlight",     lineHighlight);
    linkHover       = readThemeColor(cfg, "theme.link_hover",          linkHover);
    hoverBtn        = readThemeColor(cfg, "theme.hover_btn",          hoverBtn);
    hoverClose      = readThemeColor(cfg, "theme.hover_close",        hoverClose);
    borderColor     = readThemeColor(cfg, "theme.border_color",       borderColor);
}

void ThemeColors::saveTo(Config &cfg) const {
    writeThemeColor(cfg, "theme.bg_chrome",          bgChrome);
    writeThemeColor(cfg, "theme.scrollbar_thumb",    scrollbarThumb);
    writeThemeColor(cfg, "theme.scrollbar_track",    scrollbarTrack);
    writeThemeColor(cfg, "theme.bg_panel",           bgPanel);
    writeThemeColor(cfg, "theme.bg_editor",          bgEditor);
    writeThemeColor(cfg, "theme.text_primary",       textPrimary);
    writeThemeColor(cfg, "theme.text_secondary",     textSecondary);
    writeThemeColor(cfg, "theme.accent_selection",   accentSelection);
    writeThemeColor(cfg, "theme.search_highlight",   searchHighlight);
    writeThemeColor(cfg, "theme.line_highlight",     lineHighlight);
    writeThemeColor(cfg, "theme.link_hover",          linkHover);
    writeThemeColor(cfg, "theme.hover_btn",          hoverBtn);
    writeThemeColor(cfg, "theme.hover_close",        hoverClose);
    writeThemeColor(cfg, "theme.border_color",       borderColor);
}

// ---- Theme ---------------------------------------------------------------

Theme::Theme() {
    m_colors = lightDefaults();
    m_preset = THEME_PRESET_LIGHT;
}

// Load theme: pick the active preset from theme.name, apply its defaults,
// then let any per-key theme.* value already in the ini override. So a user
// who hand-edits theme.bg_editor in settings.ini keeps that override while
// the rest of the colors follow the selected preset.
void Theme::load(const Config &cfg) {
    char name[16];
    cfg.readStr("theme.name", name, sizeof(name), THEME_PRESET_LIGHT);
    if (!m_colors.applyPreset(name)) {
        // Unknown preset in ini -> fall back to light and remember it.
        name[0] = 0;
        m_colors = lightDefaults();
        strncpy(name, THEME_PRESET_LIGHT, sizeof(name) - 1);
        name[sizeof(name) - 1] = 0;
    }
    m_preset = name;
    m_colors.loadFrom(cfg);
}

void Theme::save(Config &cfg) {
    cfg.writeStr("theme.name", m_preset.c_str());
    m_colors.saveTo(cfg);
}

void Theme::setPreset(const char *name, Config &cfg) {
    if (!m_colors.applyPreset(name)) return;   // reject unknown preset
    m_preset = name ? name : "";
    cfg.writeStr("theme.name", m_preset.c_str());
}
