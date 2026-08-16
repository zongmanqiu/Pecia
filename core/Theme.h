// Theme.h - Semantic color and style management.
//
// Theme colors live in per-theme files under exeDir/theme/<name>.txt (like
// language files under exeDir/lang/). settings.ini only records the chosen
// theme via `theme.name`; the palette itself comes from the theme file. The
// built-in light defaults below are only a fallback when a theme file is
// missing/corrupt, so the app always opens.
#pragma once

#include <FL/fl_draw.H>
#include <string>

class Config;

#define THEME_PRESET_LIGHT "light"
#define THEME_PRESET_DARK  "dark"

struct ThemeColors {
    Fl_Color bgChrome         = fl_rgb_color(235, 235, 235); // title/tool/status bar, line#, long line
    Fl_Color scrollbarThumb   = fl_rgb_color(170, 170, 170); // vertical/horizontal scrollbar
    Fl_Color scrollbarTrack   = fl_rgb_color(235, 235, 235); // scrollbar track background
    Fl_Color bgPanel          = fl_rgb_color(245, 245, 245); // menu/search/replace/goto bar
    Fl_Color bgEditor         = fl_rgb_color(255, 255, 255); // editor + input fields
    Fl_Color textPrimary      = fl_rgb_color(0, 0, 0);      // main text, menu text
    Fl_Color textSecondary    = fl_rgb_color(170, 170, 170); // comment/secondary text
    Fl_Color accentSelection  = fl_rgb_color(191, 255, 255); // text selection highlight
    Fl_Color searchHighlight  = fl_rgb_color(255, 191, 255); // search all highlight
    Fl_Color lineHighlight    = fl_rgb_color(255, 255, 191); // current line highlight
    Fl_Color linkHover         = fl_rgb_color(6, 69, 173);   // URL hover color (#0645AD)
    Fl_Color hoverBtn         = fl_rgb_color(225, 225, 225); // title bar button hover
    Fl_Color hoverClose       = fl_rgb_color(255, 0, 0);    // close button hover
    Fl_Color borderColor      = fl_rgb_color(127, 127, 127); // window outer border

    // Parse a theme file (key = value lines: bg_chrome = #RRGGBB, ...),
    // setting only the keys it contains; missing keys keep current values.
    // Returns the number of keys applied (< 0 on open failure).
    int loadFromFile(const char *path);
};

class Theme {
public:
    Theme();

    const ThemeColors &colors() const { return m_colors; }
    const std::string &presetName() const { return m_preset; }

    // Load from Config: reads theme.name, then reads the matching
    // exeDir/theme/<name>.txt palette. On a missing/corrupt file, falls
    // back to the built-in light defaults so the window still opens.
    void load(const Config &cfg);

    // Persist only the theme selection (theme.name). Color values live in
    // the theme file, not settings.ini.
    void save(Config &cfg);

    // Switch the selected theme and persist theme.name. Does NOT rewrite
    // color values (they stay in the theme file).
    void setPreset(const char *name, Config &cfg);

private:
    ThemeColors m_colors;
    std::string m_preset = THEME_PRESET_LIGHT;
};
