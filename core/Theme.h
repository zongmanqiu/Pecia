// Theme.h - Semantic color and style management (8-key palette).
//
// Theme colors live in per-theme files under exeDir/theme/<name>.txt (like
// language files under exeDir/lang/). settings.ini only records the chosen
// theme via `theme.name`; the palette itself comes from the theme file. The
// built-in light defaults below are only a fallback when a theme file is
// missing/corrupt, so the app always opens.
//
// 8-key design (2026-08): every UI element maps to one of eight colors:
//   text1        main text + all border/dividers
//   text2        secondary/hint/disabled text + tab close glyph
//   background1  main work area background (editor, input fields, dialogs)
//   background2  secondary frame background (title/status bar, scrollbar thumb)
//   background3  transition background (menu bar, line#, find/go-to bars)
//   highlight1   selection / hover / active (primary accent)
//   highlight2   current line + pressed state
//   highlight3   special semantics (close hover, search hits, links, conflicts)
#pragma once

#include <FL/fl_draw.H>
#include <string>
#include <vector>

class Config;

#define THEME_PRESET_LIGHT "light"
#define THEME_PRESET_DARK  "dark"

// Forderground color with best contrast on the given background (black
// or white). Used for selection text and similar "pick opposite" spots.
inline Fl_Color theme_contrast_text(Fl_Color bg) {
    // fl_contrast needs a foreground list; use black vs white directly.
    return fl_contrast(FL_BLACK, bg) ? FL_WHITE : FL_BLACK;
}

struct ThemeColors {
    // 8 semantic keys.
    Fl_Color text1        = fl_rgb_color(0, 0, 0);        // main text + borders/dividers
    Fl_Color text2        = fl_rgb_color(140, 140, 140);  // secondary/hint/disabled text
    Fl_Color background1  = fl_rgb_color(255, 255, 255);  // work area (editor/inputs)
    Fl_Color background2  = fl_rgb_color(225, 225, 225);  // frame (title/status bar, scrollbar thumb)
    Fl_Color background3  = fl_rgb_color(240, 240, 240);  // transition (menu/line#/find-bar track)
    Fl_Color highlight1   = fl_rgb_color(230, 255, 230);  // hover/current line/tab active/selected
    Fl_Color highlight2   = fl_rgb_color(150, 255, 200);  // selection/pressed/menu check
    Fl_Color highlight3   = fl_rgb_color(255, 100, 100);  // search hit/link/close hover/conflict

    // Theme display name (from theme file: name = Light, name.zh-CN = 亮色).
    std::string displayName;      // fallback English name
    std::string displayNameZh;    // Chinese translation

    // Derived colors (not theme keys; computed on demand).
    // Selection text: pick contrast against the selection background.
    Fl_Color selectionText() const {
        return theme_contrast_text(highlight1);
    }
    // Border/divider: a softened text1 so a 1px rule reads as a divider,
    // not a hard outline (text1 blended toward the background).
    Fl_Color borderLine() const {
        return fl_color_average(text1, background1, 0.55f);
    }

    // Parse a theme file (key = value lines: text1 = #RRGGBB, ...),
    // setting only the keys it contains; missing keys keep current values.
    // Returns the number of keys applied (< 0 on open failure).
    int loadFromFile(const char *path);
};

// ---------------------------------------------------------------------------
// Themed border box: global replacement for FLTK's built-in FL_BORDER_BOX.
//
// The stock FLTK FL_BORDER_BOX hardcodes FL_BLACK for the 1px rect()
// border.  We override it once at startup with a function that reads
// a process-wide border colour — derived from text1 via borderLine().
// Every widget using FL_BORDER_BOX (inputs, buttons, checkboxes,
// dropdowns, menu check glyphs) automatically picks up the new colour.
// Call theme_registerBorderBox() once before any FLTK window is shown,
// and theme_updateBorderColour() whenever the active theme changes.
// ---------------------------------------------------------------------------

extern Fl_Color g_borderColour;   // process-wide border colour
extern Fl_Color g_popupCheckColor; // popup menu checkbox fill (theme highlight2)

// Register the themed FL_BORDER_BOX.  Call once at app startup.
void theme_registerBorderBox();

// Update the global border colour from the current theme palette.
inline void theme_updateBorderColour(const ThemeColors &tc) {
    g_borderColour = tc.text1;
}

class Theme {
public:
    Theme();

    const ThemeColors &colors() const { return m_colors; }
    const std::string &presetName() const { return m_preset; }

    // Get display name for the current language.
    // Falls back to English name if translation not available.
    std::string displayName(const char *lang = nullptr) const;

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

    // Scan theme/ directory and return all .ini file stems (excluding
    // light/dark which are built-in). Used to populate the Theme menu.
    static std::vector<std::string> scanExtraThemes();

private:
    ThemeColors m_colors;
    std::string m_preset = THEME_PRESET_LIGHT;
};
