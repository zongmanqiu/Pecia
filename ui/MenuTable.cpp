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
    //   ─────────────────────────
    //   Find...          Ctrl+F
    //   Find Next        F3
    //   Find Previous    Shift+F3
    //   Replace...       Ctrl+H
    //   Go To...         Ctrl+G
    //   ─────────────────────────
    //   Time/Date        F5          ← 插入（记事本同款），与下面"基于选区
    //   ─────────────────────────     的操作"归在一组，不属于选择/复制类
    //   Search Selection
    //   Send to AI
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
    { "Time/Date",     FL_F + 5,              MainWindow::cbTimeDate,   nullptr, 0 },
    { "Search Selection", 0,                  MainWindow::cbSearchSelection, nullptr },
    { "Send to AI",       0,                  MainWindow::cbSendToAi,      nullptr },
    { nullptr },

    // View
    //   Menu Bar / Script Bar / Status Bar / Side Panel / Line Numbers /
    //   Highlight Current Line / Show Space Symbol / Word Wrap   (toggles)
    //   ─────────────────────────
    //   Font
    //   Rounded Corners
    //   Window Shadow
    //   Theme  ▶
    //   Zoom  ▶        (Zoom Out Ctrl+- / Zoom In Ctrl++ / Reset Zoom Ctrl+0)
    //   ─────────────────────────
    //   Statistics...
    { "View", 0, nullptr, nullptr, FL_SUBMENU },
    { "Menu Bar",                 0, MainWindow::cbToggleMenuBar,           nullptr, FL_MENU_TOGGLE },
    { "Script Bar",                0, MainWindow::cbToggleScriptBar,          nullptr, FL_MENU_TOGGLE },
    { "Status Bar",             0, MainWindow::cbToggleStatusbar,        nullptr, FL_MENU_TOGGLE },
    { "Side Panel",  FL_COMMAND | 'b', MainWindow::cbToggleSidePanel,         nullptr, FL_MENU_TOGGLE },
    { "Line Numbers",           0, MainWindow::cbToggleLineNumbers,      nullptr, FL_MENU_TOGGLE },
    { "Highlight Current Line", 0, MainWindow::cbToggleHighlightLine,    nullptr, FL_MENU_TOGGLE },
    { "Show Space Symbol",      0, MainWindow::cbToggleShowWhitespace,   nullptr, FL_MENU_TOGGLE },
    { "Word Wrap",              0, MainWindow::cbToggleWrap,             nullptr, FL_MENU_TOGGLE | FL_MENU_DIVIDER },
    // Font - opens the system font picker (FontDialog). 原先分隔开关组的那条
    // 线(FL_MENU_DIVIDER)移到了 Word Wrap 上——FLTK 的分隔线画在带旗标的
    // 项之后,线必须保持在“开关组 | 其余”之间。
    //
    // This used to be a radio submenu with a hard-coded 9-entry list, one
    // "representative face" per writing system. That list was wrong: SimHei /
    // NSimSun / Gulim are not installed on most machines and Noto Sans SC is
    // invisible to FLTK's default ANSI-only enumeration, so selecting any of
    // them silently fell back to Courier. The dialog enumerates what is
    // actually installed instead - see ui/FontDialog.h.
    { "Font", 0, MainWindow::cbSelectFont, nullptr, 0 },
    // Rounded corners for every app window (DWM on Win11, region fallback).
    { "Rounded Corners", 0, MainWindow::cbToggleRoundedCorners, nullptr, FL_MENU_TOGGLE },
    // Outer glow: one switch for every app window (main, dialogs, and the
    // Lua / AI tool processes). Default on.
    { "Window Shadow", 0, MainWindow::cbToggleWindowShadow, nullptr, FL_MENU_TOGGLE },
    // Theme submenu - dynamically populated via FL_SUBMENU_POINTER
    { "Theme", 0, nullptr, nullptr, FL_SUBMENU_POINTER },
    // Zoom submenu - 置于「主题颜色」之下、间隔线之上(用户指定的顺序)。
    // FL_SUBMENU makes FLTK pop the submenu out to the right. 分隔旗标在
    // Zoom 头上:FLTK 把线画在带旗标的项之后,所以线保持在 Zoom 与
    // Statistics 之间,与原先 Theme 头上带旗标时同位。
    { "Zoom", 0, nullptr, nullptr, FL_SUBMENU | FL_MENU_DIVIDER },
    { "Zoom Out",    FL_COMMAND | '-', MainWindow::cbZoomOut,   nullptr },
    { "Zoom In",     FL_COMMAND | '+', MainWindow::cbZoomIn,    nullptr },
    { "Reset Zoom",  FL_COMMAND | '0', MainWindow::cbZoomReset, nullptr },
    { nullptr },
    { "Statistics...",      0, MainWindow::cbStatistics,             nullptr, 0 },
    { nullptr },

    // Markdown（样式与 Settings/Language 一致：无分割线）
    { "Markdown", 0, nullptr, nullptr, FL_SUBMENU },
    { "Preview",              FL_COMMAND | 'p', MainWindow::cbTogglePreview,        nullptr, FL_MENU_TOGGLE },
    { "Preview Only",         FL_COMMAND | FL_ALT | 'p', MainWindow::cbTogglePreviewOnly, nullptr, FL_MENU_TOGGLE },
    { "Scroll Sync",          0,                MainWindow::cbToggleScrollSync,      nullptr, FL_MENU_TOGGLE },
    // Label must stay identical to the side-panel tab (panel.outline):
    // buildMenuKeys() maps this item to the very same i18n key.
    { "Outline",              0,                MainWindow::cbShowOutlineInPanel,    nullptr, 0 },
    { "Refresh",              FL_COMMAND | 'r', MainWindow::cbRefreshPreview,       nullptr, 0 },
    { "Auto Refresh",         0,                nullptr,                            nullptr, FL_SUBMENU },
    { "Off",                  0, MainWindow::cbSetAutoRefresh, (void *)(intptr_t)0,      FL_MENU_RADIO },
    { "1s",                   0, MainWindow::cbSetAutoRefresh, (void *)(intptr_t)1000,  FL_MENU_RADIO },
    { "5s",                   0, MainWindow::cbSetAutoRefresh, (void *)(intptr_t)5000,  FL_MENU_RADIO },
    { "10s",                  0, MainWindow::cbSetAutoRefresh, (void *)(intptr_t)10000, FL_MENU_RADIO },
    { "30s",                  0, MainWindow::cbSetAutoRefresh, (void *)(intptr_t)30000, FL_MENU_RADIO },
    { nullptr },
    { "Open in Browser",      0,                MainWindow::cbOpenPreviewInBrowser, nullptr, 0 },
    { "Export HTML...",       0,                MainWindow::cbExportHtml,             nullptr, 0 },
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
