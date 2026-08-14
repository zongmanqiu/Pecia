// Theme.cpp - Theme implementation
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

// ---- Light defaults -----------------------------------------------------
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
tc.linkHover        = fl_rgb_color(6, 69, 173);   // #0645AD
    tc.hoverBtn        = fl_rgb_color(225, 225, 225);
    tc.hoverClose      = fl_rgb_color(255, 0, 0);
    tc.borderColor     = fl_rgb_color(127, 127, 127);
    return tc;
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
}

void Theme::load(const Config &cfg) {
    m_colors.loadFrom(cfg);
}

void Theme::save(Config &cfg) {
    m_colors.saveTo(cfg);
}
