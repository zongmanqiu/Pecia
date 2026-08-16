// MainWindow.h - top-level window with tabs, menu, find bar, status bar
#pragma once

#include "core/Theme.h"
#include "ui/WindowFrame.h"
#include "ui/ShortcutManager.h"
#include "core/ShortcutCore.h"
#include "mdview/preprocess.h"   // PreviewHeading
#include <FL/Fl_Double_Window.H>
#include <vector>
#include <string>
#include <functional>
#include <map>

// Buffer sizes above this force word wrap off at open time (the wrap
// layout measures every character via GDI and takes seconds on large
// files). The user can still enable wrap manually afterwards.
static const int kWrapLimitBytes = 4 * 1024 * 1024;   // 4 MB

// Above this size the status bar counts lines incrementally (the naive
// count_lines(0, pos) scan takes ~0.4 s on a 390 MB file).
static const int kStatLineIncrementalLimitBytes = 32 * 1024 * 1024;  // 32 MB


class Fl_Menu_Bar;
class Fl_Menu_Button;
class Fl_Tabs;
class Fl_Group;
class Editor;
class Fl_Output;
class Fl_Button;
class Fl_Menu_Button;
class HoverMenuBar;
class TitleBar;
class Document;
class FindReplace;
class GoTo;
class Config;
class LuaEngine;
class PreviewPanel;
class PreviewDivider;

struct ShortcutDialogRow;

struct Fl_Menu_Item;
// Main menu bar definition (defined in ui/MenuTable.cpp).
extern Fl_Menu_Item g_menu[];

// MainWindow
//   Multi-tab text editor window. Each tab holds its own Document +
//   Fl_Text_Editor. The find bar, menu bar and status bar are shared.

class MainWindow : public Fl_Double_Window {public:
    // Return the just-picked menu item from a menu callback widget (or
    // nullptr). Shared across translation units (menu callbacks).
    static Fl_Menu_Item *menuClickedItem(Fl_Widget *w);


public:
    MainWindow(int w, int h, const char *title = nullptr);
    ~MainWindow();
    Fl_Menu_Bar    *menuBar() const { return m_menu; }   // 右键菜单复用主菜单项

    // Open a file into a new tab (or reuse an existing tab if the file
    // is already open).
    void openFile(const char *path);

    // Create a fresh empty tab.
    void newFile();

    // Close the active tab. Returns false if the user cancels.
    bool closeCurrentTab();

    // Close a specific tab by index (called from TitleBar).
    void closeTab(int index);

    // Called when the user turns off multi-tab mode. Each non-active tab
    // is detached into its own Pecia process: tabs with a file path are
    // opened in a new window via ShellExecuteA, and tabs with unsaved
    // changes are saved first (Save As for "Unnamed" tabs). The active
    // tab stays in this window. Returns false if the user cancelled a
    // Save As prompt, in which case the multi-tab mode is left unchanged.
    bool detachExtraTabsToNewWindows();

    // Called by handle() / cbModify to refresh the status bar on demand.
    void tick();

    // Persist settings on exit.
    void saveSettings();

    // Query/set the Always-on-Top state (used by both the View menu
    // toggle and the title bar pin button).
    bool alwaysOnTop() const;
    void setAlwaysOnTop(bool on);

public:

    static void cbNew(Fl_Widget *w, void *data);
    static void cbNewWindow(Fl_Widget *w, void *data);
    static void cbOpen(Fl_Widget *w, void *data);
    static void cbSave(Fl_Widget *w, void *data);
    static void cbSaveAs(Fl_Widget *w, void *data);
    static void cbClose(Fl_Widget *w, void *data);
    static void cbExit(Fl_Widget *w, void *data);
    static void cbClearRecent(Fl_Widget *w, void *data);
    static void cbUndo(Fl_Widget *w, void *data);
    static void cbRedo(Fl_Widget *w, void *data);
    static void cbCut(Fl_Widget *w, void *data);
    static void cbCopy(Fl_Widget *w, void *data);
    static void cbPaste(Fl_Widget *w, void *data);
    static void cbDelete(Fl_Widget *w, void *data);
    static void cbSelectAll(Fl_Widget *w, void *data);
    static void cbSearchSelection(Fl_Widget *w, void *data); // 按配置的搜索引擎搜索选中内容
    static void cbSendToAi(Fl_Widget *w, void *data);        // 选中内容直接发给 AI（自动开窗+发送+解除选中）

    // 确保 PeciaAIChat.exe 正在运行（未运行则带管道参数启动之）。
    bool ensureAiChatRunning();

    // 把任意文本推送给 AI 聊天窗口（自动确保窗口运行；文本作为用户
    // 消息出现在 AI 对话框）。供菜单“发送给 AI”与管道 SEND 请求共用。
    bool sendToAiChat(const std::string &text, std::string *errMsg);
    static void cbFind(Fl_Widget *w, void *data);
    static void cbFindNext(Fl_Widget *w, void *data);
    static void cbFindPrev(Fl_Widget *w, void *data);
    static void cbReplace(Fl_Widget *w, void *data);
    static void cbZoomIn(Fl_Widget *w, void *data);
    static void cbZoomOut(Fl_Widget *w, void *data);
    static void cbZoomReset(Fl_Widget *w, void *data);
    static void cbAbout(Fl_Widget *w, void *data);
    static void cbToggleWrap(Fl_Widget *w, void *data);
    static void cbToggleLineNumbers(Fl_Widget *w, void *data);
    static void cbSettings(Fl_Widget *w, void *data);
    static void cbShortcuts(Fl_Widget *w, void *data);
    static void cbReloadScripts(Fl_Widget *w, void *data);
    static void cbOpenScriptFolder(Fl_Widget *w, void *data);
    static void cbCheckUpdates(Fl_Widget *w, void *data);
    // Test dialogs (for verifying custom styles)
    static void cbTestSaveConfirm(Fl_Widget *w, void *data);
    static void cbTestLargeFile(Fl_Widget *w, void *data);
    static void cbTestFileError(Fl_Widget *w, void *data);
    static void cbTestInfoWindow(Fl_Widget *w, void *data);
    static void cbTestExtensions(Fl_Widget *w, void *data);
    static void cbTestParams(Fl_Widget *w, void *data);
    static void cbTestAIChat(Fl_Widget *w, void *data);
    static void cbTestLua(Fl_Widget *w, void *data);
    // New View menu toggles
    static void cbToggleStatusbar(Fl_Widget *w, void *data);
    static void cbToggleScriptBar(Fl_Widget *w, void *data);
    static void cbToggleHighlightLine(Fl_Widget *w, void *data);
    static void cbToggleShowWhitespace(Fl_Widget *w, void *data);
    // New View menu dialogs
    static void cbSelectFont(Fl_Widget *w, void *data);
    static void cbStatistics(Fl_Widget *w, void *data);
    static void cbGotoLine(Fl_Widget *w, void *data);

    // Tools > Lua Script... - opens the Lua script dialog.
    static void cbLuaScript(Fl_Widget *w, void *data);
    static void cbAIChat(Fl_Widget *w, void *data);

    // View > Markdown Preview - toggle the two-pane preview layout.
    static void cbTogglePreview(Fl_Widget *w, void *data);
    // Markdown menu: manual refresh / open HTML in browser / auto-refresh interval.
    static void cbRefreshPreview(Fl_Widget *w, void *data);
    static void cbOpenPreviewInBrowser(Fl_Widget *w, void *data);
    static void cbSetAutoRefresh(Fl_Widget *w, void *data);
    static void cbToggleScrollSync(Fl_Widget *w, void *data);

    // Run a Lua script against the active document. The engine applies
    // the result as one undoable operation. `output` receives the print()
    // sink contents, `errMsg` the Lua error on failure. Returns false if
    // there is no active document or the script failed.
    bool executeLuaScript(const std::string &script,
                          std::string *output = nullptr,
                          std::string *errMsg = nullptr);

    // Insert text into the active document: replaces the selection if one
    // exists, otherwise inserts at the cursor. Used by the AI chat tool
    // (INS pipe frame). Returns false with *errMsg set on failure.
    bool insertAiText(const std::string &text, std::string *errMsg = nullptr);

    // Active tab's full UTF-8 text (Markdown preview source); empty when
    // there is no active tab.
    std::string activeDocumentText() const;

    // Clear the active document's selection (used by the AI chat tool
    // after it consumed the selection for Append Selection - otherwise
    // the AI reply sent back via insertAiText would replace the original
    // selection). Returns false with *errMsg set when no document.
    bool clearSelection(std::string *errMsg = nullptr);

    // Return the active document's current selection text. Used by the AI
    // chat tool (GSEL pipe frame). Returns false with *errMsg set when
    // there is no active document or no selection to copy.
    bool getSelectionText(std::string *text, std::string *errMsg = nullptr);

    // Snapshot the active document for off-process execution: payload is
    // "cursor:selStart:selEnd\n" + full doc text (UTF-8). Used by the Lua
    // console (GDOC pipe frame). Returns false with *errMsg set when
    // there is no active document or it is read-only.
    bool getDocumentSnapshot(std::string *payload, std::string *errMsg = nullptr);

    // Apply a Lua console result back to the active document. `payload`
    // has the same "cursor:selStart:selEnd\n" + text shape as the
    // snapshot. Applies the text as ONE replacement (not undoable, same
    // as the main-window Lua path) and restores cursor/selection. Used by
    // the Lua console (APPL pipe frame). Returns false with *errMsg on
    // failure (no doc / read-only / malformed payload).
    bool applyDocumentSnapshot(const std::string &payload,
                               std::string *errMsg = nullptr);

    // Re-scan the exe-adjacent script/ folder and rebuild the script bar
    // buttons. Called by the Lua console after a Save As so a newly
    // saved script appears in the bar immediately (RLOAD pipe frame).
    void reloadScriptBar();

    // Scripts callback - runs the selected .lua file from the
    // exe-adjacent script/ folder. `data` is the script's relative
    // path ("pair" or "Formatting/trim"), owned by the menu/button
    // that fired. Works for the Tools > Scripts menu items, the
    // group dropdown buttons, and the root-script buttons alike.
    static void cbRunScript(Fl_Widget *w, void *data);
    // Switch the UI language. The selected language code is stashed in
    // the menu item's user_data (e.g. "en", "zh-CN"). The callback
    // reloads the I18n table, persists the choice, and refreshes the
    // status bar so the new language takes effect immediately.
    static void cbSetLanguage(Fl_Widget *w, void *data);
    // Switch the color theme preset (light/dark). user_data = preset name.
    static void cbSetTheme(Fl_Widget *w, void *data);
    // Open a file from the Recent Files submenu. The index is passed in
    // user_data; we look it up in Config::recentGet().
    static void cbOpenRecent(Fl_Widget *w, void *data);

    // Buffer modify callback - keeps the * mark on the title in sync
    static void cbModify(int pos, int nInserted, int nDeleted,
                         int nRestyled, const char *deletedText,
                         void *cbArg);

    // Window close (X button) callback - distinct from cbClose (Ctrl+W)
    // which only closes the current tab.
    static void cbWindowClose(Fl_Widget *w, void *data);

    // Tab close button callback - called when the user clicks the X on
    // a tab in multi-tab mode.
    static void cbTabClose(Fl_Widget *w, void *data);
    // Individual tab page callback - handles FL_REASON_CLOSED from
    // FLTK's built-in tab close button.
    static void cbTabPageClose(Fl_Widget *w, void *data);

protected:
    // Drag & drop support + Ctrl+wheel zoom + global shortcut dispatch
    int handle(int event) override;
    void draw() override;
    void resize(int X, int Y, int W, int H) override;

private:
    struct Tab {
        Document       *doc;
        Fl_Group       *page;     // the tab page (parent of the editor)
        Editor         *editor;
    };

    // --- Keyboard shortcut configuration (Settings > Shortcuts...) ---
    // (Re)read the shortcut.* config keys and rebuild:
    //   - every menu item's shortcut_ (popup labels + FLTK dispatch)
    //   - the handler registry (window ops, scripts, go-to bar)
    void applyShortcuts();
    // Effective combo for an action id: config value if set, else the
    // given default.
    ShortcutCombo shortcutComboFor(const char *id, int defKey,
                                   unsigned defMods);
    // Build the ShortcutDialog row list (menu + scripts + window ops +
    // Lua console + AI chat actions).
    std::vector<ShortcutDialogRow> buildShortcutRows();
    // Handler actions (dispatched by dispatchShortcut).
    void toggleGoToBar();        // menu.edit.goto (keeps toggle behavior)
    void cycleTab(int dir);      // win.nexttab / win.prevtab
    void minimizeWindow();       // win.minimize
    void togglePin();            // win.pin (always-on-top)
    ShortcutRegistry m_shortcutRegistry;
    std::map<std::string, std::function<void()>> m_shortcutHandlers;

    Fl_Menu_Bar    *m_menu;
    Fl_Tabs        *m_tabs;
    FindReplace    *m_findBar;
    GoTo           *m_goToBar;
    Fl_Output      *m_status;

    // ---- Markdown preview (two-pane: editor | preview) ----
    PreviewPanel   *m_preview = nullptr;      // 预览面板（首次开启时创建）
    PreviewDivider *m_previewDivider = nullptr; // 分隔条（比例 20%-80%）
    HoverMenuBar   *m_previewBar = nullptr;  // 预览顶部按钮栏（与主菜单同样式）
    std::vector<Fl_Menu_Item> m_previewBarItems; // 按钮栏菜单项：{刷新},{目录},{nullptr}
    class TocPopup *m_tocPopup = nullptr;    // 目录弹出面板（限高+滚动条）
    std::vector<Fl_Menu_Item> m_tocMenu;     // 目录菜单项（末尾 {nullptr} 终止）
    std::vector<std::string> m_tocLabels;    // 菜单项文本（text 指针指向其 c_str，先 reserve 保地址稳定）
    std::vector<std::string> m_tocIds;       // 菜单项锚点 id（user_data 指向其 c_str，先 reserve 保地址稳定）
    bool            m_previewActive = false;  // 预览模式是否开启
    float           m_previewRatio = 0.5f;    // 左栏（编辑区）占比
    bool            m_autoRefreshArmed = false; // 自动刷新循环定时器已挂起
    bool            m_previewDestroyPending = false; // 关闭预览的延迟销毁已排期
    std::string     m_lastRenderedMd;          // 上次渲染的文本（内容不变不重渲染）
    std::string     m_previewDir;              // 预览输出目录（exe 同级 temp/<文档名>/）
    bool            m_scrollSyncArmed = false;// 滚动同步轮询已挂起
    bool            m_scrollSyncEnabled = false; // 滚动同步开关（Markdown > Scroll Sync）
    int             m_lastEditorScroll = 0;   // 上次编辑滚动条位置（滚动同步去抖）
    int             m_autoRefreshMs = 5000;   // 自动刷新间隔(ms)，0=关闭(仅手动)
    void            syncAutoRefreshMenu();    // 同步 Auto Refresh 菜单勾选
    static void     scrollSyncCb(void *data); // 编辑滚动 → 预览滚动（150ms 轮询）
    void            startScrollSync();
    void            stopScrollSync();
    static void     autoRefreshLoopCb(void *data); // 固定间隔循环：无条件按档位刷新
    static void     destroyPreviewCb(void *data);  // 关闭预览的延迟销毁（0.1s 后）
    void            startAutoRefreshLoop();   // 按 m_autoRefreshMs 启动循环
    void            stopAutoRefreshLoop();
    void            refreshPreview();           // 立即用当前标签内容刷新
    void            togglePreview();            // 开/关预览模式
    void            layoutPreviewPanes(int contentTop, int tabsH); // 两栏排布
    void            rebuildPreviewToc(const std::vector<PreviewHeading> &headings); // 重建目录数据
    void            showTocPopup();          // 弹出目录面板（按钮下方，限高+滚动）
    static void     cbPreviewRefreshBtn(Fl_Widget *, void *); // 工具条：手动刷新
    static void     cbPreviewTocBtn(Fl_Widget *, void *);    // 工具条：弹出目录
    void            setAutoRefresh(int ms);     // 设置自动刷新间隔并持久化
    void            openPreviewInBrowser();     // 导出 HTML 并用默认浏览器打开
public:
    void  scrollPreview(int dy);                // 预览垂直滚动
    static void cleanupDeadPreviewDirs();       // 启动时清理死进程残留预览目录
    void  cleanupOwnPreviewDir();               // 退出时删除自己的预览目录
    void  cleanupOldTempDirs();                 // 退出时清理 7 天前的临时文件（启用时）
    class Fl_Box   *m_topBorder = nullptr;   // editor area top border line
    class Fl_Box   *m_botBorder = nullptr;   // editor area bottom border line
    Config         *m_cfg;
    Theme           m_theme;       // theme colors (loaded from Config)
    class FileManager *m_fileMgr = nullptr;  // file operations
    WindowFrame      *m_winFrame = nullptr;   // window frame management
    ShortcutManager  *m_shortcuts = nullptr;  // keyboard shortcut dispatch
    TitleBar       *m_titleBar;   // custom-drawn title bar (replaces OS title bar)

    // Lua scripting (Tools > Lua Script...). The script tool runs as a
    // SEPARATE process (PeciaLua.exe) connected over a named pipe, so the
    // main window only hosts the Lua engine and the pipe server.
    class LuaEngine *m_luaEngine = nullptr;
    class LuaPipeServer *m_luaPipe = nullptr;
    class AiPushServer *m_aiPush = nullptr;   // push channel to the AI chat window
    void showLuaDialog();
    // Tools > AI Chat... - launches/activates the standalone AIChat tool.
    void showAIChat();

    // Tools bar. The toolbar is a HoverMenuBar: every top-level item is
    // one script button ("Scripts"/Misc for loose root .lua files, one
    // item per level-1 subfolder), each with a submenu of its scripts.
    // Being a real menu bar gives it menu-bar hover/switch behaviour:
    // clicking one item pops its menu, then hovering other items
    // switches menus (stock Fl_Menu_Button::popup() cannot do this).
    class HoverMenuBar *m_scriptBar = nullptr;
    bool                m_showScriptBar = true;
    // Owned toolbar menu array (built by rebuildScriptBar() from
    // the script/ folder scan): one FL_SUBMENU item per toolbar button,
    // each followed by its script items (level-2 folders become nested
    // FL_SUBMENUs). Freed in the destructor and on every rebuild.
    Fl_Menu_Item *m_scriptBarMenu = nullptr;
    // Element count of m_scriptBarMenu: the array contains nested submenus
    // each terminated by an FLTK null entry, so the total length != the
    // position of the first null `.text` - we must walk the full count to
    // free every _strdup'd text/user_data_.
    int m_scriptBarMenuCount = 0;

    std::vector<Tab> m_tabsList;

    // Cached visibility of the status bar (View > Status Bar). When
    // false the bottom STATUS_H pixels are reclaimed by the editor.
    bool            m_showStatusbar = true;

    // Incremental status-bar line tracking: (cursor pos, line, line start)
    // cached from the last update, so big-file updates only scan the delta.
    int             m_statPos = -1;
    int             m_statLine = 1;
    int             m_statLineStart = 0;

    // ---- 内存整理（EmptyWorkingSet，见 MainWindow_timers.cpp）----
    void            scheduleTrimSoon();     // 打开/保存/另存为对话框结束后主动整理
    static void     trimSoonCb(void *data);

    // Reposition every tab page (and its editor) based on the current
    // status-bar / toolbar visibility. Pages always fill the full m_tabs
    // area vertically (no tab strip is shown - single-window mode only).
    void layoutTabs();

    // Build the script bar (script-driven HoverMenuBar only).
    void buildScriptBar();
    // (Re)build the script toolbar menu array from the script/ folder
    // scan: one "Scripts" dropdown item for the root .lua files (always
    // present; "(no scripts)" when empty), plus one dropdown item per
    // subfolder. Any existing m_scriptBarMenu array is freed first. Called
    // by buildScriptBar().
    void rebuildScriptBar();
    // Apply the shortcut.<id> combo to a script toolbar item (empty
    // combo -> no shortcut shown).
    void applyScriptItemShortcut(Fl_Menu_Item *item, const char *relPath);
    // Re-translate toolbar item labels after a language change.
    void updateScriptBarLabels();

    // Recent Files submenu backing array. The menu table uses
    // FL_SUBMENU_POINTER on the "Recent Files" entry, which makes FLTK
    // read the submenu from this array. We rebuild it whenever a file
    // is opened or saved (see refreshRecentMenu()).
    Fl_Menu_Item   *m_recentMenu;
    int             m_recentMenuSize;
    // Cached pointer to the "Recent Files" parent menu item, captured
    // once in the constructor (while the menu is still in English) so
    // refreshRecentMenu() can update its user_data() without having to
    // find_item() by English path - which would fail after the menu
    // has been translated to Chinese.
    Fl_Menu_Item   *m_recentMenuItem = nullptr;
    // Cached pointers to View menu toggle items, captured in the
    // constructor (while menu is still English) so saveSettings() and
    // applySettings() can read/update their state without find_item()
    // by English path - which fails after applyLanguageToMenu() has
    // translated the menu to Chinese.
    Fl_Menu_Item   *m_wrapItem = nullptr;
    Fl_Menu_Item   *m_lineNumbersItem = nullptr;
    // Cached pointers for the new View menu toggles.
    Fl_Menu_Item   *m_statusbarItem = nullptr;
    Fl_Menu_Item   *m_scriptBarItem = nullptr;
    Fl_Menu_Item   *m_highlightLineItem = nullptr;
    Fl_Menu_Item   *m_showWhitespaceItem = nullptr;
    Fl_Menu_Item   *m_fontFirstItem = nullptr;

    // Rebuild the Recent Files submenu from Config::recentGet(). Allocates
    // a fresh m_recentMenu; the old one is freed. Safe to call when the
    // list is empty - a single disabled "(empty)" entry is shown.
    void refreshRecentMenu();



    // Tab management
    int  findTabByPath(const char *path) const;
    int  addTab(Document *doc, const char *label);
    void switchToTab(int index);
    int  activeTabIndex() const;
    Tab *activeTab();
    void updateTabLabel(int index);
    void updateAllTabLabels();

    // State updates
    void updateTitle();
    void updateStatusBar();

    // File operations
    bool checkSaveBeforeClose(int tabIndex);
    bool doSave(int tabIndex);
    bool doSaveAs(int tabIndex);

    // Zoom helpers
    void setFontSize(int size);
    int  fontSize() const;

    // Find helpers
    void showFindBar(bool withReplace);
    void showGoToBar();

    // Recalculate line number column width based on the buffer's line
    // count. Called when files are opened, line numbers are toggled,
    // settings are applied, or the buffer is modified. Only updates
    // when the number of digits changes to avoid unnecessary redraws.
    void updateLinenumberWidth();

    // Style an Fl_File_Chooser with the current scheme + colors.

    // One-shot status bar refresh callback. We use Fl::add_timeout()
    // instead of Fl::add_idle() because timeouts fire exactly once and
    // don't need to be removed manually - this avoids idle-handler
    // leaks that would otherwise pile up on every key/mouse event.
    static void statusUpdateCb(void *data);

    // Cursor blink callback. Re-arms itself on each tick (~500ms) and
    // toggles the active editor's cursor visibility. Only the focused
    // editor's cursor is blinked to avoid waking hidden editors.
    static void cursorBlinkCb(void *data);
    static void autoSaveCb(void *data);
    bool m_cursorVisible;   // tracks the blink phase so we can re-apply it
                            // when switching tabs (newly-shown editor
                            // should start with a visible cursor)

    // Apply settings from Config to all open editors (called after the
    // settings dialog closes).
    void applySettings(class Config &cfg);

    // Re-read every setting from Config and update the UI to match.
    // Called by the file-change watcher when settings.ini is modified
    // by another Pecia process.
    void syncFromConfig();
    void scheduleAutoSave();

    // Reload the theme preset from m_cfg and re-colour the whole window
    // (chrome, menus, status bar, tabs and every open editor). Called by
    // cbSetTheme and after a settings.ini-driven theme change.
    void applyThemeColors();

    // Persisted scheme
    void applyScheme(const char *name);

    // Sync title bar state (tab data, maximized, pinned, etc.) and redraw.
    void syncTitleBar();

    // Re-translate every menu item label using the currently-loaded
    // I18n table. Called at startup and whenever the user picks a new
    // language from the Language submenu. Non-toggle items get the
    // standard 4-space leading indent so they align with toggle items'
    // checkbox slot; toggle items have no indent (matching g_menu).
    void applyLanguageToMenu();

    // One-time mapping of each localizable menu item to its I18n key,
    // captured while the menu is still in its original English form so
    // find_item() can locate every entry by its English path. After
    // this runs, applyLanguageToMenu() can re-translate labels at any
    // time without needing the (now translated) paths to find items.
    void buildMenuKeys();

    // Mapping table populated by buildMenuKeys() and consumed by
    // applyLanguageToMenu(). Each entry pins a pointer to a menu item
    // together with the I18n key used to translate its label. defShortcut
    // captures the item's BUILT-IN shortcut (from g_menu, before
    // applyShortcuts() applied any user config) so the Shortcuts dialog
    // can offer "Restore Defaults" with the real factory values.
    struct MenuKeyEntry {
        Fl_Menu_Item *item;
        const char   *key;
        bool          isTopLevel;  // true for menu bar entries (File, Edit, ...)
        int           defShortcut; // factory shortcut (key|mods), pre-config
    };
    std::vector<MenuKeyEntry> m_menuKeys;

    // Backing storage for translated menu labels. applyLanguageToMenu()
    // rewrites this vector on every call, then points each
    // Fl_Menu_Item::text at the c_str() of the corresponding entry.
    // The vector is reserve()d up front so push_back doesn't relocate
    // its std::string elements - that keeps the c_str() pointers
    // obtained in the second pass stable until the next call.
    std::vector<std::string> m_menuTexts;

    // Deferred language switch. When the user picks a new language from
    // the Language submenu we don't immediately call I18n::load + apply
    // - FLTK's pulldown menu window may still be holding pointers into
    // our menu items and rewriting their text mid-pulldown crashes the
    // app. Instead cbSetLanguage stashes the requested code here and
    // arms s_applyLangDeferred via Fl::add_timeout(0.0, ...) so the
    // work runs on the next event loop tick, after FLTK has fully
    // torn down the menu window.
    const char *m_pendingLangCode = nullptr;
    static void s_applyLangDeferred(void *data);

    // Deferred theme switch (mirrors the language pattern): the preset is
    // stashed and applied on the next event-loop tick via the static
    // callback, so FLTK has fully torn down the pulldown menu first.
    std::string m_pendingTheme;
    static void s_applyThemeDeferred(void *data);

    // Deferred line-number width refresh (named callback so it can be
    // removed in the destructor; an anonymous lambda timeout would fire on
    // a destroyed 'this' if the window closes within the 0.0s window).
    static void s_updateLinenumberWidthCb(void *data);

private:
    // Helper used by handle() to dispatch global shortcuts before FLTK
    // sends them to whichever widget has focus.
    int dispatchShortcut(int key, int state);

    // One-shot timeout that re-applies WS_EX_APPWINDOW after the window
    // is shown (border(0) strips it, which hides the app from the
    // taskbar on Windows).
    static void fixTaskbarCb(void *data);

    // Native OLE drop target (PeciaDropTarget*) for file drag&drop.
    // Stored as void* to avoid exposing COM interfaces in the header.
    void *m_dropTarget = nullptr;

    // --- Custom frame support (border(0) + self-drawn title bar) ---
    // Maximized state. When maximized we save the restore rectangle
    // so the user can go back to the previous size/position.
    bool m_maximized = false;
    int  m_restoreX = 0, m_restoreY = 0, m_restoreW = 0, m_restoreH = 0;

    // Toggle maximized / restored.
    void toggleMaximize();
    // Apply maximized state geometry.
    void applyMaximized();
    // The "100% zoom" reference font size, captured once at startup from
    // Config::getFont(). The status bar's zoom percentage is computed as
    // (current_textsize * 100) / m_baseFontSize. Zoom operations (Ctrl+/-,
    // Ctrl+wheel) modify the editor's textsize() but NOT this value, so a
    // saved size of 14 from a previous session still displays as 100%
    // (since m_baseFontSize would also be 14 in that case).
    int  m_baseFontSize = 13;
};
