// Theme.h - Semantic color and style management.
// Many UI elements deliberately share one color (e.g. bgChrome is the
// title/menu/status/toolbar/line-number background). The 14 semantic keys
// map 1:1 to `theme.*` ini keys; a theme preset (light/dark) is just a full
// set of defaults for those keys, and any key present in the ini overrides it.
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

    // Fill this object with the built-in default values for the named
    // preset ("light" / "dark"). Returns true on success.
    bool applyPreset(const char *name);

    void loadFrom(const Config &cfg);
    void saveTo(Config &cfg) const;
};

class Theme {
public:
    Theme();

    const ThemeColors &colors() const { return m_colors; }
    const std::string &presetName() const { return m_preset; }

    // Load theme from Config: reads theme.name, fills the matching preset
    // defaults, then lets any theme.* key already in the ini override them.
    void load(const Config &cfg);
    void save(Config &cfg);

    // Switch to a named preset and save it (does NOT persist color values).
    void setPreset(const char *name, Config &cfg);

private:
    ThemeColors m_colors;
    std::string m_preset = THEME_PRESET_LIGHT;
};
