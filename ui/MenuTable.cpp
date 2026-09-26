// MenuTable.cpp - the MainWindow menu bar definition (Fl_Menu_Item table).
// Kept in its own translation unit so the menu structure - a large static
// data table - does not clutter the window implementation. The table is
// declared extern in ui/MainWindow.h and consumed by the constructor via
// m_menu->copy(g_menu, this).
#include "ui/MainWindow.h"

#include <FL/Fl_Menu_Item.H>

Fl_Menu_Item g_menu[] = {
    // File
    // All items are aligned left - no leading spaces for indentation.
    // Toggle items show a checkbox; non-toggle items start at the same margin.
    // This matches the layout of most native text editors.
    // This matches the layout of most native text editors.
    // This matches the layout of most native text editors.
    //
    // File menu layout (per spec):
    //   New Window       Ctrl+Shift+N
    //   Open...          Ctrl+O
    //   ─────────────────────────
    //   Save             Ctrl+S
    //   Save As...       Ctrl+Shift+S
    //   ─────────────────────────
    //   Recent Files  ▶  (submenu, pops out to the right)
    //   Clear Recent Files
    //   ─────────────────────────
    //   Exit             Ctrl+Q
    { "File", 0, nullptr, nullptr, FL_SUBMENU },
    { "New",              FL_COMMAND | 'n',                  MainWindow::cbNew,           nullptr, 0 },
    { "New Window",       FL_COMMAND | FL_SHIFT | 'n',       MainWindow::cbNewWindow,     nullptr, FL_MENU_DIVIDER },
    { "Open...",          FL_COMMAND | 'o',                  MainWindow::cbOpen,          nullptr, 0 },
    { "Save",             FL_COMMAND | 's',                  MainWindow::cbSave,          nullptr, 0 },
    { "Save As...",       FL_COMMAND | 'S',                  MainWindow::cbSaveAs,        nullptr, FL_MENU_DIVIDER },
    // Recent Files submenu: dynamically populated by refreshRecentMenu().
    // The FL_SUBMENU_POINTER flag lets us supply the submenu via a separate
    // Fl_Menu_Item array (m_recentMenu) that we rebuild at runtime. FLTK
    // automatically pops the submenu out to the right on hover.
    { "Recent Files",     0,                                 nullptr,                     nullptr, FL_SUBMENU_POINTER },
    { "Clear Recent Files", 0,                                 MainWindow::cbClearRecent,   nullptr, FL_MENU_DIVIDER },
    { "Exit",             FL_COMMAND | 'q',                  MainWindow::cbExit,          nullptr, 0 },
    { nullptr },

    // Edit
    //   Undo             Ctrl+Z
    //   Redo             Ctrl+Y
    //   ─────────────────────────
    //   Cut              Ctrl+X
    //   Copy             Ctrl+C
    //   Paste            Ctrl+V
    //   Delete
    //   ─────────────────────────
    //   Find...          Ctrl+F
    //   Find Next        F3
    //   Find Previous    Shift+F3
    //   Replace...       Ctrl+H
    //   Go To...         Ctrl+G
    //   ─────────────────────────
    //   Select All       Ctrl+A
    //   Time/Date        F5
    { "Edit", 0, nullptr, nullptr, FL_SUBMENU },
    { "Undo",          FL_COMMAND | 'z',      MainWindow::cbUndo,       nullptr, 0 },
    { "Redo",          FL_COMMAND | 'y',      MainWindow::cbRedo,       nullptr, FL_MENU_DIVIDER },
    { "Cut",           FL_COMMAND | 'x',      MainWindow::cbCut,        nullptr },
    { "Copy",          FL_COMMAND | 'c',      MainWindow::cbCopy,       nullptr },
    { "Paste",         FL_COMMAND | 'v',      MainWindow::cbPaste,      nullptr },
    { "Delete",        FL_Delete,           MainWindow::cbDelete,      nullptr },
    { "Select All",    FL_COMMAND | 'a',      MainWindow::cbSelectAll,  nullptr, FL_MENU_DIVIDER },
    { "Find...",       FL_COMMAND | 'f',      MainWindow::cbFind,       nullptr },
    { "Find Next",     FL_F + 3,              MainWindow::cbFindNext,   nullptr },
    { "Find Previous", FL_F + 3 | FL_SHIFT,   MainWindow::cbFindPrev,   nullptr },
    { "Replace...",    FL_COMMAND | 'h',      MainWindow::cbReplace,    nullptr },
    { "Go To...",      FL_COMMAND | 'g',      MainWindow::cbGotoLine,   nullptr, FL_MENU_DIVIDER },
    { "Search Selection", 0,                  MainWindow::cbSearchSelection, nullptr },
    { "Send to AI",       0,                  MainWindow::cbSendToAi,      nullptr },
    { nullptr },

    // View
    //   Status Bar
    //   Line Numbers
    //   ─────────────────────────
    //   Highlight Current Line
    //   Show Space Symbol
    //   Word Wrap
    //   Font  ▶
    //     Consolas / SimHei / NSimSun / MS Gothic / Gulim
    //     Segoe UI / Nirmala UI / Leelawadee UI / Noto Sans SC
    //     ...
    //   Statistics...
    //   ─────────────────────────
    //   Zoom  ▶
    //     Zoom Out       Ctrl+-
    //     Zoom In        Ctrl++
    //     Reset Zoom     Ctrl+0
    //   Multi Tab
    { "View", 0, nullptr, nullptr, FL_SUBMENU },
    { "Menu Bar",                 0, MainWindow::cbToggleMenuBar,           nullptr, FL_MENU_TOGGLE },
    { "Script Bar",                0, MainWindow::cbToggleScriptBar,          nullptr, FL_MENU_TOGGLE },
    { "Status Bar",             0, MainWindow::cbToggleStatusbar,        nullptr, FL_MENU_TOGGLE },
    { "Line Numbers",           0, MainWindow::cbToggleLineNumbers,      nullptr, FL_MENU_TOGGLE },
    { "Highlight Current Line", 0, MainWindow::cbToggleHighlightLine,    nullptr, FL_MENU_TOGGLE },
    { "Show Space Symbol",      0, MainWindow::cbToggleShowWhitespace,   nullptr, FL_MENU_TOGGLE },
    { "Word Wrap",              0, MainWindow::cbToggleWrap,             nullptr, FL_MENU_TOGGLE },
    // Font submenu - radio items, 每文字系统一个代表字体（与
    // FontUtils::FONT_LIST 保持一致，其余文字由系统回退兜底）。
    { "Font", 0, nullptr, nullptr, FL_SUBMENU | FL_MENU_DIVIDER },
    { "Consolas",      0, MainWindow::cbSelectFont, nullptr, FL_MENU_RADIO },
    { "SimHei",        0, MainWindow::cbSelectFont, nullptr, FL_MENU_RADIO },
    { "NSimSun",       0, MainWindow::cbSelectFont, nullptr, FL_MENU_RADIO },
    { "MS Gothic",     0, MainWindow::cbSelectFont, nullptr, FL_MENU_RADIO },
    { "Gulim",         0, MainWindow::cbSelectFont, nullptr, FL_MENU_RADIO },
    { "Segoe UI",      0, MainWindow::cbSelectFont, nullptr, FL_MENU_RADIO },
    { "Nirmala UI",    0, MainWindow::cbSelectFont, nullptr, FL_MENU_RADIO },
    { "Leelawadee UI", 0, MainWindow::cbSelectFont, nullptr, FL_MENU_RADIO },
    { "Noto Sans SC",  0, MainWindow::cbSelectFont, nullptr, FL_MENU_RADIO },
    { nullptr },
    // Zoom submenu. FL_SUBMENU makes FLTK pop the submenu out to the
    // right; the nested items use an 8-space indent so they align
    // with the 4-space indent used by non-toggle items elsewhere,
    // while still reading as "deeper" inside the Zoom submenu.
    { "Zoom", 0, nullptr, nullptr, FL_SUBMENU },
    { "Zoom Out",    FL_COMMAND | '-', MainWindow::cbZoomOut,   nullptr },
    { "Zoom In",     FL_COMMAND | '+', MainWindow::cbZoomIn,    nullptr },
    { "Reset Zoom",  FL_COMMAND | '0', MainWindow::cbZoomReset, nullptr },
    { nullptr },
    // Theme submenu - dynamically populated via FL_SUBMENU_POINTER
    { "Theme", 0, nullptr, nullptr, FL_SUBMENU_POINTER | FL_MENU_DIVIDER },
    { "Statistics...",      0, MainWindow::cbStatistics,             nullptr, 0 },
    { nullptr },

    // Markdown（样式与 Settings/Language 一致：无分割线）
    { "Markdown", 0, nullptr, nullptr, FL_SUBMENU },
    { "Preview",              FL_COMMAND | 'p', MainWindow::cbTogglePreview,        nullptr, FL_MENU_TOGGLE },
    { "Scroll Sync",          0,                MainWindow::cbToggleScrollSync,      nullptr, FL_MENU_TOGGLE },
    { "Refresh",              0,                MainWindow::cbRefreshPreview,       nullptr, 0 },
    { "Auto Refresh",         0,                nullptr,                            nullptr, FL_SUBMENU },
    { "Off",                  0, MainWindow::cbSetAutoRefresh, (void *)(intptr_t)0,      FL_MENU_RADIO },
    { "1s",                   0, MainWindow::cbSetAutoRefresh, (void *)(intptr_t)1000,  FL_MENU_RADIO },
    { "5s",                   0, MainWindow::cbSetAutoRefresh, (void *)(intptr_t)5000,  FL_MENU_RADIO },
    { "10s",                  0, MainWindow::cbSetAutoRefresh, (void *)(intptr_t)10000, FL_MENU_RADIO },
    { "30s",                  0, MainWindow::cbSetAutoRefresh, (void *)(intptr_t)30000, FL_MENU_RADIO },
    { nullptr },
    { "Open in Browser",      0,                MainWindow::cbOpenPreviewInBrowser, nullptr, 0 },
    { nullptr },

    // Tools
    { "Tools", 0, nullptr, nullptr, FL_SUBMENU },
    { "Lua Script...", 0, MainWindow::cbLuaScript, nullptr, 0 },
    { "AI Chat...", 0, MainWindow::cbAIChat, nullptr, 0 },
    { nullptr },

    // Settings - Preferences, Shortcuts, and Language (radio submenu).
    { "Settings", 0, nullptr, nullptr, FL_SUBMENU },
    { "Options...",   0,                      MainWindow::cbSettings,      nullptr, 0 },
    { "Shortcuts...",                   0, MainWindow::cbShortcuts,     nullptr, 0 },
    { "Open Script Folder...", 0, MainWindow::cbOpenScriptFolder, nullptr, 0 },
    { "Refresh", 0, MainWindow::cbRefreshAll, nullptr, 0 },
    // Language submenu - dynamically populated via FL_SUBMENU_POINTER
    { "Language", 0, nullptr, nullptr, FL_SUBMENU_POINTER },
    { nullptr },

    // Help
    { "Help", 0, nullptr, nullptr, FL_SUBMENU },
    { "Check for Updates", 0, MainWindow::cbCheckUpdates, nullptr, 0 },
    { "About",             0, MainWindow::cbAbout,        nullptr, 0 },
    { nullptr },

    { nullptr }
};
