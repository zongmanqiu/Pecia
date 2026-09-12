// Layout.h - Shared layout constants
#pragma once

// ============================================================================
// Layout Sizes
// ============================================================================
// These values define the fixed heights of the various UI bars. They are
// RUNTIME globals, initialized once at startup from settings.ini keys
// (see ui/Layout.cpp and initLayoutHeights()):
//
//   bar_height = 32   -- bar height for bars WITH bordered buttons:
//                        title bar, find/replace (2x), go-to bar, dialog
//                        button bars, Lua/AI tool button rows.
//   btn_height = 24   -- unified button height, vertically centered inside
//                        the bars (bar_height - btn_height gap is the same
//                        everywhere).
//   text_bar_height = 24 -- bar height for borderless text bars: menu
//                        bar, toolbar and status bar (lower so they do
//                        not look top-heavy).
//
// The find/replace bar spans two rows (2 x bar_height). Editor content
// fills the remaining vertical space.
//
// Layout.cpp owns the definitions; every other translation unit sees the
// extern declarations below.
// ----------------------------------------------------------------------------

// Unified bar height (settings.ini: bar_height, default 32): bars that
// contain bordered buttons (title bar, find/go-to bars, dialog button
// bars, tool button rows).
extern int gBarH;
// Unified button height (settings.ini: btn_height, default 24).
extern int gBtnH;
// Borderless text-bar height (settings.ini: text_bar_height, default 24):
// menu bar, toolbar and status bar - lower so they do not look heavy.
extern int gTextBarH;

// Bars (all follow gBarH):
extern int FINDBAR_H;  // Find/replace bar height (2 rows)
extern int GOTOBAR_H;  // Go To bar height

// Borderless text bars (follow gTextBarH):
extern int TITLE_H;    // Custom title bar height
extern int MENU_H;     // Menu bar height
extern int SCRIPTBAR_H;  // Tools bar height (under menu bar)
extern int STATUS_H;   // Status bar height

constexpr int TABS_H     = 0;     // Fl_Tabs tab strip (hidden; tabs drawn by TitleBar)

// Initial window size shared by all three processes (Pecia, Lua console,
// AI chat): every window opens at the same 800x600.
constexpr int DEFAULT_WIN_W = 800;
constexpr int DEFAULT_WIN_H = 600;

// Scrollbar width (used by Editor)
constexpr int SCROLLBAR_SIZE = 10;

// Apply the settings.ini bar_height / btn_height values to every bar
// height global above. Must run once at startup, before any widget is
// created. Defined in ui/Layout.cpp.
class Config;
void initLayoutHeights(const Config &cfg);

