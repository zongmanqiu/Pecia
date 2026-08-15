// MainWindow.cpp - multi-tab main window implementation
#include "MainWindow.h"

#include "editor/Document.h"
#include "ui/FindReplace.h"
#include "ui/GoTo.h"
#include "ui/SettingsDialog.h"
#include "ui/ConfirmDialog.h"
#include "ui/InfoWindow.h"
#include "ui/TitleBar.h"

#include "script/LuaEngine.h"
#include "LuaTool/LuaPipeServer.h"
#include "LuaTool/AiPushServer.h"
#include "core/Config.h"
#include "core/Theme.h"
#include "core/FileManager.h"
#include "core/I18n.h"
#include "core/UiBridge.h"
#include "core/OpLog.h"

#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Group.H>
#include "editor/Editor.h"
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Box.H>
#include <FL/fl_ask.H>
#include <FL/filename.H>
#include <FL/fl_string_functions.h>
#include <FL/platform.H>
#include <FL/fl_draw.H>

#if defined(_WIN32)
#include <windows.h>
#endif

// Case-insensitive ASCII string compare (portable, no platform deps).
#include <FL/x.H>                 // for Windows-specific path helpers
#include "resource.h"             // IDI_PECIA for the app icon

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>               // GET_X_LPARAM / GET_Y_LPARAM
#include <ole2.h>                  // RegisterDragDrop, ReleaseStgMedium, FORMATETC
#include <shellapi.h>              // DragQueryFileW, HDROP
#include <shobjidl.h>              // IFileOpenDialog / IFileSaveDialog
#include <shlobj.h>                // CFSTR_FILEDESCRIPTOR, FILEDESCRIPTOR
#endif

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctime>
#include <algorithm>
#include <vector>
#include <utility>
#include <string>
#include <memory>

#include "ui/DropTarget.h"
#include "ui/Layout.h"
#include "ui/HoverMenuBar.h"

// Buffer sizes above this force word wrap off at open time (the wrap
// Layout constants
// Layout constants are defined in ui/Layout.h (TITLE_H, MENU_H, etc.)

// ---------------------------------------------------------------------------
// Helper: get the Fl_Menu_Item that triggered a callback from the
// source widget. Works for clicks from the menu bar (Fl_Menu_Bar) and
// toolbar dropdown buttons (Fl_Menu_Button) alike — both are Fl_Menu_
// subclasses, so mvalue() returns the just-picked item.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// StatusOutput - Fl_Output subclass that draws its text with a fixed
// left padding (6px from the widget's left edge) instead of the default
// box-dx padding. We override draw() so the text starts at exactly
// x()+LEFT_PAD, which lets us match the spec "status bar text 6px from
// the left edge of the window" (status bar widget itself starts at x=0).
// Fl_Input_::drawtext is protected, so we can call it from this subclass
// of Fl_Output (->Fl_Input->Fl_Input_).
// ---------------------------------------------------------------------------
class StatusOutput : public Fl_Output {
public:
    StatusOutput(int X, int Y, int W, int H, const char *l = nullptr)
        : Fl_Output(X, Y, W, H, l) {}

    void draw() FL_OVERRIDE {
        Fl_Boxtype b = box();
        if (damage() & FL_DAMAGE_ALL) draw_box(b, color());
        // 6px left padding from the widget's left edge (which is the
        // window's left edge, since the status bar widget starts at x=0).
        const int LEFT_PAD = 6;
        Fl_Input_::drawtext(x() + LEFT_PAD,
                            y() + Fl::box_dy(b),
                            w() - LEFT_PAD - Fl::box_dx(b),
                            h() - Fl::box_dh(b));
    }

    int handle(int event) FL_OVERRIDE {
        // Reject focus and all mouse events — the status bar is
        // display-only, not interactive.
        if (event == FL_FOCUS || event == FL_UNFOCUS)
            return 0;
        // Never show the text I-beam: Fl_Input_ sets FL_CURSOR_INSERT on
        // enter/move. Force the default arrow (FL_LEAVE falls through to
        // Fl_Input_, which restores it).
        if (event == FL_ENTER || event == FL_MOVE ||
            event == FL_PUSH || event == FL_RELEASE ||
            event == FL_DRAG) {
            if (window()) window()->cursor(FL_CURSOR_DEFAULT);
            return 0;
        }
        return Fl_Output::handle(event);
    }
};



// --------------------------------------------------------------------------
// Menu table
// --------------------------------------------------------------------------
// FLTK menu paths use "/" for submenu nesting.
// Tab characters in the label separate the label from the shortcut text.
// The menu is rebuilt (with translated labels) whenever the language changes.
//
// We keep this as a static mutable array because Fl_Menu_Bar::copy() wants
// a non-const source.

// --------------------------------------------------------------------------
// File chooser localization - applied once when the first dialog opens.
// Fl_File_Chooser exposes its labels as static const char* pointers we
// can reassign at runtime.
// --------------------------------------------------------------------------


// --------------------------------------------------------------------------
// Construction
// --------------------------------------------------------------------------

MainWindow::MainWindow(int w, int h, const char *title)
    : Fl_Double_Window(w, h, title)
    , m_menu(nullptr)
    , m_tabs(nullptr)
    , m_findBar(nullptr)
    , m_status(nullptr)
    , m_cfg(new Config())
    , m_titleBar(nullptr)
    , m_recentMenu(nullptr)
    , m_recentMenuSize(0)
    , m_cursorVisible(true) {
    // Remove the OS-provided title bar / border so we can draw our own
    // custom title bar (with a pin button for always-on-top). All edge
    // resizing is handled in handle().
    border(0);
    // Minimum size so the caption buttons always fit
    size_range(400, 300);

    // Capture the persisted base font size once. Zoom operations modify
    // the editor's textsize() but never m_baseFontSize, so a saved size
    // of 14 (e.g. from a previous Ctrl+wheel in an older build) still
    // displays as 100% on this startup since both values are 14.
    {
        m_baseFontSize = m_cfg->getUiFontSize();
    }

    begin();

    // Custom title bar at the very top
    m_titleBar = new TitleBar(0, 0, w, TITLE_H);
    m_titleBar->box(FL_FLAT_BOX);
    m_titleBar->color(FL_BACKGROUND2_COLOR);
    m_titleBar->setOnNewFile([this]() { newFile(); });
    m_titleBar->setOnCloseTab([this](int idx) { closeTab(idx); });
    m_titleBar->setOnSwitchToTab([this](int idx) { switchToTab(idx); });
    m_titleBar->setOnTogglePin([this]() { setAlwaysOnTop(!alwaysOnTop()); });
    m_titleBar->setOnMinimize([this]() { iconize(); });
    m_titleBar->setOnToggleMaximize([this]() { toggleMaximize(); });
    m_titleBar->setOnCloseWindow([this]() { cbWindowClose(this, this); });
    m_titleBar->setFontSize(m_cfg->getUiFontSize());

    // Load theme colors from config BEFORE applying to TitleBar
    m_theme.load(*m_cfg);
    m_titleBar->color(m_theme.colors().bgChrome);
    m_titleBar->setTheme(&m_theme);
    // Ensure theme colors are persisted to INI (writes defaults if missing)
    m_theme.save(*m_cfg);

    int menuY = TITLE_H;
    m_menu = new HoverMenuBar(0, menuY, w, MENU_H);
    m_menu->box(FL_FLAT_BOX);
    m_menu->color(m_theme.colors().bgPanel);
    m_menu->textcolor(m_theme.colors().textPrimary);
    m_menu->selection_color(m_theme.colors().accentSelection);
    m_menu->copy(g_menu, this);
    m_menu->textsize(m_cfg->getUiFontSize());

    // Set label color on all menu items so toggle checkmarks use textPrimary too.
    for (int i = 0; i < m_menu->size(); ++i) {
        Fl_Menu_Item *item = (Fl_Menu_Item *)m_menu->menu() + i;
        if (item->text) item->labelcolor(m_theme.colors().textPrimary);
    }

    // Markdown preview: auto-refresh interval (persisted) with the menu
    // radio synced at startup. The Markdown menu is not translated, so
    // the English path stays valid in every language.
    m_autoRefreshMs = m_cfg->getPreviewAutoRefreshMs();
    syncAutoRefreshMenu();
    // 启动时清理上次崩溃/退出残留的预览目录（只删进程已不存在的）
    cleanupDeadPreviewDirs();
    m_scrollSyncEnabled = m_cfg->getPreviewScrollSync();
    if (m_scrollSyncEnabled) {
        std::string p = std::string(I18n::get("menu.markdown")) + "/" + I18n::get("menu.markdown.scrollsync");
        Fl_Menu_Item *ss = (Fl_Menu_Item *)m_menu->find_item("Markdown/Scroll Sync");
        if (!ss) ss = (Fl_Menu_Item *)m_menu->find_item(p.c_str());
        if (ss) ss->set();
    }

    // Wire the "Recent Files" submenu placeholder (FL_SUBMENU_POINTER)
    // to our dynamically-managed m_recentMenu array. FLTK reads the
    // submenu pointer from menu()->user_data(), so we set it to point
    // at our member. refreshRecentMenu() will fill m_recentMenu below.
    //
    // We also cache the parent item pointer in m_recentMenuItem so
    // refreshRecentMenu() can update its user_data() later without
    // having to find_item() by the English path - which would fail
    // once applyLanguageToMenu() translates the menu to Chinese.
    const Fl_Menu_Item *ri = m_menu->find_item("File/Recent Files");
    if (ri) {
        m_recentMenuItem = const_cast<Fl_Menu_Item *>(ri);
        m_recentMenuItem->user_data(m_recentMenu);   // initial: nullptr -> empty
    }

    // Tools bar - sits between the menu bar and the editor area.
    // Script-driven buttons are created here via rebuildScriptBar().
    m_showScriptBar = m_cfg->getShowToolbar();
    buildScriptBar();

    // Tabs occupy the area between the menu/toolbar and the find bar /
    // status bar. We leave the find bar slot always reserved even when
    // hidden so the editor doesn't resize when it pops up.
    int toolbarH = m_showScriptBar ? SCRIPTBAR_H : 0;
    int contentTop = TITLE_H + MENU_H + toolbarH;
    // Use runtime status bar visibility for initial layout
    int statusH = m_showStatusbar ? STATUS_H : 0;
    int statusY = h - statusH;
    int editorH = statusY - contentTop;

    m_tabs = new Fl_Tabs(0, contentTop, w, editorH);
    m_tabs->box(FL_FLAT_BOX);   // no inner border; only outer window border remains
    // Use the same background color as the text editor so that any
    // Fl_Tabs selection border area blends in seamlessly.
    m_tabs->color(m_theme.colors().bgEditor);
    m_tabs->selection_color(m_theme.colors().bgEditor);
    // Disable Fl_Tabs' default resizable (which is itself). With
    // resizable()==this, Fl_Group::resize() re-layouts children using
    // its bounds array, but Fl_Tabs children use window-absolute
    // coordinates and we position them manually in layoutTabs().
    // This prevents stale bounds data from causing bottom gaps.
    m_tabs->resizable(nullptr);
    // Close-tab callback (fires when the user clicks the X on a tab
    // in multi-tab mode, or switches tabs).
    m_tabs->callback(cbTabClose, this);
    // Fl_Tabs constructor calls begin(), which makes m_tabs the current
    // group. We must end it now so the find bar and status bar created
    // below are added to *this* window, not to m_tabs (otherwise Fl_Tabs
    // treats them as extra tab pages with empty labels).
    m_tabs->end();
    Fl_Group::current(this);

    // The find bar floats above the editor's bottom edge when shown.
    m_findBar = new FindReplace(0, statusY - FINDBAR_H, w, FINDBAR_H);
    m_findBar->setFontSize(m_cfg->getUiFontSize());
    m_findBar->setTheme(&m_theme);
    m_findBar->setCloseCallback([this]() { layoutTabs(); });
    m_findBar->setHighlightCallback([this](bool active) {
        // When search highlight turns off, re-apply line highlight
        // (FindReplace's highlight_data() overrides ours while active).
        if (!active) {
            Tab *t = activeTab();
            if (t && m_cfg->getHighlightCurrentLine()) {
                t->editor->refreshLineHighlight();
            }
        }
    });

    m_goToBar = new GoTo(0, statusY - GOTOBAR_H, w, GOTOBAR_H);
    m_goToBar->setFontSize(m_cfg->getUiFontSize());
    m_goToBar->setTheme(&m_theme);
    m_goToBar->setCloseCallback([this]() { layoutTabs(); });
    m_goToBar->setGoCallback([this](long lineNum) {
        Tab *t = activeTab();
        if (!t) return;
        Fl_Text_Editor *ed = t->editor;
        if (!ed) return;
        Fl_Text_Buffer *buf = ed->buffer();
        if (!buf) return;

        int totalLines = buf->count_lines(0, buf->length()) + 1;
        if (totalLines < 1) return;
        if (lineNum > totalLines) lineNum = totalLines;

        // skip_lines(startPos, nLines) jumps forward nLines lines from
        // startPos and returns the position at the start of the next line.
        // To get the position of line N (1-based), skip (N-1) lines from 0.
        int pos = buf->skip_lines(0, (int)(lineNum - 1));

        ed->insert_position(pos);
        ed->show_insert_position();
        ed->redraw();
        updateStatusBar();
        Fl::focus(ed);
    });

    m_status = new StatusOutput(0, statusY, w, STATUS_H);
    m_status->box(FL_FLAT_BOX);
    m_status->color(m_theme.colors().bgChrome);
    m_status->textfont(FL_HELVETICA);
    m_status->textsize(m_cfg->getUiFontSize());
    m_status->textcolor(m_theme.colors().textPrimary);
    m_status->value("");

    end();

    // Initial empty tab
    newFile();

    // Window close button (X) handling
    callback(cbWindowClose, this);
    iconlabel("Pecia");

    // Apply persisted scheme
    char scheme[32];
    m_cfg->getScheme(scheme, sizeof(scheme), "gtk+");
    applyScheme(scheme);

    // Install the UI bridge so core/editor layers (Document, FileManager)
    // can report errors and confirmations without depending on UI widgets.
    installUiBridge(&m_theme, m_cfg->getUiFontSize());

    // Apply persisted menu toggles. We cache the item pointers here
    // (while the menu is still in English) because saveSettings() and
    // applySettings() will run after applyLanguageToMenu() has changed
    // the labels to Chinese - at that point find_item() by English
    // path would return nullptr.
    m_wrapItem = (Fl_Menu_Item *)m_menu->find_item("View/Word Wrap");
    if (m_wrapItem) {
        if (m_cfg->getWrap()) m_wrapItem->set(); else m_wrapItem->clear();
    }
    m_lineNumbersItem = (Fl_Menu_Item *)m_menu->find_item("View/Line Numbers");
    if (m_lineNumbersItem) {
        if (m_cfg->getLineNumbers()) m_lineNumbersItem->set(); else m_lineNumbersItem->clear();
    }

    // Apply persisted status bar visibility (default on). Cached pointer
    // is used by applySettings() and saveSettings() too.
    m_showStatusbar = m_cfg->getShowStatusbar();
    m_statusbarItem = (Fl_Menu_Item *)m_menu->find_item("View/Status Bar");
    if (m_statusbarItem) {
        if (m_showStatusbar) m_statusbarItem->set(); else m_statusbarItem->clear();
    }

    // Cache the "View > Script Bar" toggle pointer and sync it with the
    // persisted state. m_showScriptBar was already read above (before
    // buildScriptBar) so the initial layout uses the correct value; here
    // we just sync the menu checkmark.
    m_scriptBarItem = (Fl_Menu_Item *)m_menu->find_item("View/Script Bar");
    if (m_scriptBarItem) {
        if (m_showScriptBar) m_scriptBarItem->set(); else m_scriptBarItem->clear();
    }
    // Multi Tab toggle has moved to the Settings dialog; nothing to
    // cache here anymore.
    // Cache the remaining View toggle pointers and sync them with the
    // persisted state. The corresponding features are still being
    // implemented, but the menu state must be consistent so toggling
    // them on/off doesn't lose the user's choice across sessions.
    m_highlightLineItem = (Fl_Menu_Item *)m_menu->find_item("View/Highlight Current Line");
    if (m_highlightLineItem) {
        if (m_cfg->getHighlightCurrentLine()) m_highlightLineItem->set(); else m_highlightLineItem->clear();
    }
    m_showWhitespaceItem = (Fl_Menu_Item *)m_menu->find_item("View/Show Space Symbol");
    if (m_showWhitespaceItem) {
        if (m_cfg->getShowWhitespace()) m_showWhitespaceItem->set(); else m_showWhitespaceItem->clear();
    }

    // Apply persisted always-on-top state to the window right away so
    // that windows restored as topmost actually appear above other
    // windows from the very first paint. setAlwaysOnTop() also refreshes
    // the title bar pin button. Doing this in the constructor is safe:
    // HWND_TOPMOST affects window Z-order metadata that Windows keeps
    // even before the window is shown.
    setAlwaysOnTop(m_cfg->getAlwaysOnTop());

    // Populate the Recent Files submenu from Config. This MUST run
    // before any m_menu->setonly() call: setonly() walks the whole
    // menu (including FL_SUBMENU_POINTER entries like "Recent Files")
    // via first_submenu_item(), which recurses into the submenu
    // pointer stored in user_data_. If m_recentMenu is still nullptr
    // here, the recursion dereferences nullptr and crashes the app.
    refreshRecentMenu();

    // Scan the exe-adjacent script/ folder and populate the script
    // toolbar buttons (must happen after the window exists, like
    // refreshRecentMenu).
    rebuildScriptBar();

    // Sync the Language radio submenu with the persisted choice. The
    // language file itself was already loaded by main() at startup; we
    // just need to mark the right entry as checked so the radio dot
    // shows on the active language. We use m_menu->setonly() (not
    // Fl_Menu_Item::set()) because setonly() walks the radio group
    // and clears the FL_MENU_VALUE flag on the other entries - set()
    // only sets the flag on one item and leaves the other selected,
    // which is what caused "both radio dots were blank" before.
    // Note: Config::getLang() already validates the value and clears
    // corrupted registry entries, so curLang is guaranteed to be a
    // known code ("en" or "zh-CN") here.
    char curLang[64] = "en";
    m_cfg->getLang(curLang, sizeof(curLang), "en");
    Fl_Menu_Item *langItem = nullptr;
    if (strcmp(curLang, "zh-CN") == 0) {
        langItem = (Fl_Menu_Item *)m_menu->find_item("Settings/Language/简体中文 (zh-CN)");
    } else {
        langItem = (Fl_Menu_Item *)m_menu->find_item("Settings/Language/English (en)");
    }
    if (langItem) m_menu->setonly(langItem);

    // Sync the Font radio submenu with the persisted choice.
    // find_item() must run before buildMenuKeys()/applyLanguageToMenu()
    // because the Font submenu header gets translated afterwards.
    char curFont[64];
    m_cfg->getEditorFont(curFont, sizeof(curFont));
    m_fontFirstItem = (Fl_Menu_Item *)m_menu->find_item("View/Font/Consolas");
    for (int i = 0; i < FontUtils::FONT_COUNT; ++i) {
        char path[128];
        snprintf(path, sizeof(path), "View/Font/%s", FontUtils::FONT_LIST[i]);
        Fl_Menu_Item *fi = (Fl_Menu_Item *)m_menu->find_item(path);
        if (fi && FontUtils::strCaseCmp(FontUtils::FONT_LIST[i], curFont) == 0) {
            m_menu->setonly(fi);
            break;
        }
    }

    // Sync the Theme radio submenu with the persisted preset. Reads the
    // same theme.name value earlier loaded into m_theme.presetName().
    {
        Fl_Menu_Item *themeItem = nullptr;
        if (strcmp(m_theme.presetName().c_str(), THEME_PRESET_DARK) == 0)
            themeItem = (Fl_Menu_Item *)m_menu->find_item("View/Theme/Dark");
        else
            themeItem = (Fl_Menu_Item *)m_menu->find_item("View/Theme/Light");
        if (themeItem) m_menu->setonly(themeItem);
    }

    // Build the menu-key mapping *before* any translation is applied
    // so find_item() can still match the English labels in g_menu, then
    // translate every menu label to the persisted language. This makes
    // the menu bar start up in the user's chosen language instead of
    // the hard-coded English defaults.
    buildMenuKeys();
    applyLanguageToMenu();

    // Build the shortcut dispatch registry + menu shortcut labels from
    // the shortcut.* config keys (Settings > Shortcuts...). Must run
    // after buildMenuKeys() so the menu item pointers exist.
    applyShortcuts();

    updateTitle();
    updateStatusBar();

    // Lay out the initial tab page
    layoutTabs();

    // Start the cursor blink timer. The callback re-arms itself on each
    // tick, so this single add_timeout is enough to keep the cursor
    // blinking for the lifetime of the window. See cursorBlinkCb().
    Fl::add_timeout(0.5, cursorBlinkCb, this);

    // Idle callback to refresh status bar (caret position, file size).
    // We don't keep the idle permanently installed - it would prevent
    // the process from being paged out and keep waking it. Instead we
    // install it only when the caret moves or buffer changes (see
    // cbModify / handle()).

    // One-shot timeout to fix the taskbar button. On Windows, border(0)
    // strips WS_EX_APPWINDOW which makes the window disappear from the
    // taskbar. We re-add it after the window is shown (the timeout
    // fires after the first Fl::run() iteration, by which time the
    // HWND exists and is visible).
    //
    // When multiple Pecia processes start simultaneously (e.g., when
    // detaching tabs), their SetForegroundWindow calls conflict. We
    // read PECIA_DELAY to stagger the fixTaskbarCb calls.
    double taskbarDelay = 0.0;
#if defined(_WIN32)
    const char *delayEnv = getenv("PECIA_DELAY");
    if (delayEnv && *delayEnv) {
        taskbarDelay = atoi(delayEnv) / 1000.0;
    }
#endif
    Fl::add_timeout(taskbarDelay, fixTaskbarCb, this);

    // Start the file-change watcher so this window stays in sync with other
    // Pecia processes that write to settings.ini. Uses ReadDirectoryChangesW
    // on Windows for immediate notification.
    m_cfg->setChangeCallback([this]() { syncFromConfig(); });
    m_cfg->startWatcher();

    // Start the auto-save timer. Repeats every getAutoSaveInterval() seconds
    // (0 = off). The callback re-arms itself on each tick.
    scheduleAutoSave();
}

MainWindow::~MainWindow() {
    // Revoke the OLE drop target before the window is destroyed.
    // RevokeDragDrop releases the reference held by OLE; if our
    // m_dropTarget was the last reference, the object deletes itself.
#if defined(_WIN32)
    if (m_dropTarget) {
        HWND hwnd = fl_xid(this);
        if (hwnd) RevokeDragDrop(hwnd);
        static_cast<PeciaDropTarget *>(m_dropTarget)->Release();
        m_dropTarget = nullptr;
    }
#endif

    // Stop the file-change watcher thread
    if (m_cfg) m_cfg->stopWatcher();

    saveSettings();

    // 退出时删除本进程的预览目录（正常退出；崩溃残留由下次启动清理兜底）
    cleanupOwnPreviewDir();
    // 退出时清理 7 天前的临时文件残留（可选，默认开）
    cleanupOldTempDirs();

    // Remove all pending timeouts that capture 'this' so they don't fire
    // after the object is destroyed.
    Fl::remove_timeout(cursorBlinkCb, this);
    Fl::remove_timeout(autoSaveCb, this);
    Fl::remove_timeout(statusUpdateCb, this);
    Fl::remove_timeout(fixTaskbarCb, this);
    Fl::remove_timeout(s_applyLangDeferred, this);
    Fl::remove_timeout(s_updateLinenumberWidthCb, this);

    for (auto &t : m_tabsList) {
        // Detach the editor from the buffer BEFORE deleting the document:
        // Fl_Text_Display's destructor calls remove_modify_callback() on
        // its buffer, which must still be alive (otherwise FLTK warns
        // "Can't find modify CB to remove" at exit).
        t.editor->buffer(nullptr);
        delete t.doc;       // deletes its Fl_Text_Buffer too
        // t.page and t.editor are children of m_tabs and are deleted by FLTK
    }
    m_tabsList.clear();

    // Free the Lua scripting resources. The Lua tool itself is a separate
    // process (PeciaLua.exe) - only the engine and the pipe server live
    // here, and neither touches m_cfg in its destructor. Delete them
    // BEFORE m_cfg anyway to keep the ordering unambiguous.
    delete m_luaPipe;
    m_luaPipe = nullptr;
    delete m_luaEngine;
    m_luaEngine = nullptr;

    // AI push channel (owns a listener thread that we join in its dtor).
    delete m_aiPush;
    m_aiPush = nullptr;

    delete m_cfg;

    // Free the recent-files submenu strings and array (we own them
    // because they were allocated with _strdup / new[] in refreshRecentMenu).
    if (m_recentMenu) {
        for (int i = 0; i < m_recentMenuSize; ++i) {
            if (m_recentMenu[i].text) free((void *)m_recentMenu[i].text);
        }
        delete[] m_recentMenu;
        m_recentMenu = nullptr;
    }
    // Free the script toolbar menu array (the toolbar widget itself is
    // deleted by FLTK after this destructor via the window). The array
    // contains nested submenus each null-terminated, so walk the full
    // recorded count rather than stopping at the first null .text.
    if (m_scriptBarMenu) {
        for (int i = 0; i < m_scriptBarMenuCount; ++i) {
            free((void *)m_scriptBarMenu[i].text);
            if (m_scriptBarMenu[i].user_data_) free(m_scriptBarMenu[i].user_data_);
        }
        delete[] m_scriptBarMenu;
        m_scriptBarMenu = nullptr;
        m_scriptBarMenuCount = 0;
    }
}

void MainWindow::setFontSize(int size) {
    if (size < 6) size = 6;
    if (size > 48) size = 48;
    for (auto &t : m_tabsList) {
        t.editor->textsize(size);
        t.editor->linenumber_size(size);
        t.editor->syncStyleFont();
    }
    // NOTE: We intentionally do NOT call m_cfg->setFont() here. Zoom
    // (Ctrl+/-/0, Ctrl+wheel) is treated as a per-session adjustment:
    // the user's chosen "base" font size (set via the Font dialog,
    // default 13) is persisted by the Font dialog and saveSettings(),
    // while the zoom level itself is not. This keeps the zoom
    // percentage display at 100% on every fresh startup instead of
    // inheriting the previous session's zoom (which previously showed
    // 107% because a saved size of 14 was compared against the
    // hard-coded BASE_FONT of 13).
    updateStatusBar();
}
