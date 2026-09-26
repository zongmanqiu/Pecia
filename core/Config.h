// Config.h - persistent application settings via settings.ini
#pragma once

#include "SettingsProvider.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <filesystem>
#include <chrono>
#include <mutex>
#include <atomic>
#include <thread>

// Config
//   Thin wrapper around a plain-text INI file (settings.ini) sitting
//   next to Pecia.exe. The file format is a single [Settings] section:
//
//     [Settings]
//     key1=value1
//     key2=value2
//
//   All settings are read/written immediately to disk so multiple Pecia
//   windows stay in sync. The implementation is fully cross-platform -
//   it uses std::filesystem for paths and file timestamps, and parses
//   the INI format itself (no reliance on Win32 GetPrivateProfile*).
//
//   Recent files are stored separately in "recent file.ini" next to
//   the exe, so the user can delete / scrub the recent-files list
//   independently of the rest of the configuration.
//
//   File-change detection:
//     checkChanged() compares the current on-disk modification time
//     with the cached time. Call it periodically (e.g. every 5 s) to
//     detect edits made by other Pecia processes and reload.
//     reload() refreshes the cached timestamp so subsequent calls to
//     checkChanged() return false until the file changes again.
class Config : public SettingsProvider {
public:
Config();

    // Test hook: construct with an explicit config-directory instead of
    // the executable's directory. Production code keeps using Config().
    explicit Config(const std::filesystem::path &configDir);

    // Standalone tools (PeciaLua / PeciaAIChat): use their OWN ini file
    // next to the exe so three processes never race on settings.ini.
    explicit Config(const std::string &iniFileName);
    ~Config();

    // Window geometry
    void getWindow(int &x, int &y, int &w, int &h, int &flags) const;
    void setWindow(int x, int y, int w, int h, int flags);

    // Tool window size (standalone tools): width/height persisted under
    // "<prefix>_win_w" / "<prefix>_win_h" in the same ini. Position is
    // not persisted - tools always re-center on open.
    void getToolSize(const char *prefix, int &w, int &h,
                     int defW, int defH) const;
    void setToolSize(const char *prefix, int w, int h);

    // Font (FLTK font id + size), derived at call time from the single
    // source of truth `editor_font` (font NAME, View > Font). The legacy
    // font_id/font_size keys are dead (no writer ever existed) and are
    // scrubbed from settings.ini on the next save. setFont() is kept for
    // SettingsProvider ABI compatibility but is a no-op by design.
    void getFont(int &font, int &size) const;
    void setFont(int font, int size);

    // Theme / scheme name ("base", "gtk+", "plastic", "gleam", "oxy")
    void getScheme(char *buf, int len, const char *fallback = "gtk+") const;
    void setScheme(const char *name);

    // Editor options
    bool getWrap() const;
    void setWrap(bool on);
    bool getLineNumbers() const;
    void setLineNumbers(bool on);
    int  getTabWidth() const;
    void setTabWidth(int w);
    // Auto-indent: Enter copies the previous line's leading whitespace.
    bool getAutoIndent() const;
    void setAutoIndent(bool on);

    // View > Status Bar - show/hide the bottom status bar.
    bool getShowStatusbar() const;
    void setShowStatusbar(bool on);

    // View > Tools Bar - show/hide the toolbar below the menu bar.
    bool getShowToolbar() const;
    void setShowToolbar(bool on);

    // View > Menu Bar - show/hide the menu bar.
    bool getShowMenuBar() const;
    void setShowMenuBar(bool on);

    // View > Multi Tab - show/hide the tab strip and allow multiple tabs.
    bool getMultiTab() const;
    void setMultiTab(bool on);

    // View > Always on Top - keep the window above other windows.
    bool getAlwaysOnTop() const;
    void setAlwaysOnTop(bool on);

    // Long line marker: column at which a vertical guide line is drawn.
    // 0 (default) = off; >0 = draw at that column.
    int  getLongLineMarker() const;
    void setLongLineMarker(int col);

    // Dialog content padding (px): uniform gap above the first row and
    // below the last row of modal dialogs (Options, AI settings...).
    // Single source of truth so all dialogs keep identical spacing.
    int  getDialogPad() const;
    void setDialogPad(int px);

    // Unified UI bar/button heights (settings.ini: bar_height /
    // btn_height). Every bar (title/menu/toolbar/find/go-to/status and
    // dialog button bars) uses these, so one ini edit re-lays out the
    // whole application.
    int getBarHeight() const;
    int getBtnHeight() const;
    // Borderless text bars (menu/toolbar/status): text_bar_height.
    int getTextBarHeight() const;

    // Keyboard shortcuts: canonical combo text ("Ctrl+F", "Esc", "" =
    // none) for an action id ("win.cancel", "menu.file.save",
    // "script.Formatting/trim", ...). Stored under "shortcut.<id>" in
    // the shared settings.ini so all Pecia processes stay in sync.
    // An EMPTY value is an explicit "no shortcut" (cleared by the user),
    // while a MISSING key means "use the built-in default" - hasKey()
    // lets callers tell the two apart.
    std::string getShortcut(const char *id) const;
    void setShortcut(const char *id, const std::string &combo);
    bool hasKey(const char *key) const;

    // View > Highlight Current Line.
    bool getHighlightCurrentLine() const;
    void setHighlightCurrentLine(bool on);

    // View > Show Space Symbol.
    bool getShowWhitespace() const;
    void setShowWhitespace(bool on);

    // Save > Trim Trailing Whitespace - strip trailing spaces/tabs on save.
    bool getTrimTrailingWhitespace() const;
    void setTrimTrailingWhitespace(bool on);

    // Save > formatting: trim leading/ending blank lines+spaces of the
    // document, and expand tabs to spaces (tab_width) on save.
    bool getTrimLeadingBlank() const;
    void setTrimLeadingBlank(bool on);
    bool getTrimEndingBlank() const;
    void setTrimEndingBlank(bool on);
    bool getExpandTabsOnSave() const;
    void setExpandTabsOnSave(bool on);

    // Save > Auto-Save Interval (seconds). 0 = off.
    int  getAutoSaveInterval() const;
    void setAutoSaveInterval(int secs);

    // View > Detect URLs - highlight clickable URLs with underline.
    bool getDetectUrls() const;
    void setDetectUrls(bool on);
    // 退出时清理 7 天前的临时文件（build/temp 残留）
    bool getCleanupTempOld() const;
    void setCleanupTempOld(bool on);

    // 固定启动文档：电脑上没有其它 Pecia 进程时，启动（或该次启动的首个
    // 标签）原本要打开空白新文档的话，改为打开 exe 同级目录下的
    // Test.txt（不存在则新建空文件），省去开机时手动找文件。
    bool getFixedStartupDoc() const;
    void setFixedStartupDoc(bool on);

    // System integration: comma-separated list of extensions (with leading
    // dot, e.g. ".txt,.md,.log") registered for the "Open with" menu.
    // Empty string means "no extensions selected". The default (when the
    // key is missing) is the full supported list - see kDefaultOpenWithExts
    // in SettingsDialog.cpp.
    void getOpenWithExts(char *buf, int len, const char *fallback = "") const;
    void setOpenWithExts(const char *exts);

    // UI chrome font size (menus, title bar, status bar, etc.).
    int  getUiFontSize() const;
    void setUiFontSize(int size);

    // Markdown preview auto-refresh interval in ms (0 = off).
    int  getPreviewAutoRefreshMs() const;
    void setPreviewAutoRefreshMs(int ms);

    // Markdown preview scroll sync (editor scroll → preview scroll).
    bool getPreviewScrollSync() const;
    void setPreviewScrollSync(bool on);

    // View > Editor font face.
    void getEditorFont(char *buf, int len, const char *fallback = "Consolas") const;
    void setEditorFont(const char *name);

    // 编辑区缩放记忆（Ctrl +/-/0、Ctrl+滚轮）。独立于 ui_font_size：
    // ui_font_size 是设置对话框里的基准字号，editor_zoom_size 是本次
    // 缩放后的绝对字号；0 表示从未缩放（跟随基准）。
    int  getEditorZoomSize() const;
    void setEditorZoomSize(int size);

    // 搜索选中内容的搜索引擎 URL 模板（%s = 选中内容）
    void getSearchEngineUrl(char *buf, int len) const;
    void setSearchEngineUrl(const char *url);

    // Language code ("en", "zh-CN", ...)
    void getLang(char *buf, int len, const char *fallback = "en") const;
    void setLang(const char *code);

    // AI assistant: API key + model + endpoint for the Lua-script helper.
    void getAiKey(char *buf, int len, const char *fallback = "") const;
    void setAiKey(const char *key);
    void getAiModel(char *buf, int len,
                    const char *fallback = "glm-4-flash") const;
    void setAiModel(const char *model);
    void getAiEndpoint(char *buf, int len,
                       const char *fallback =
                           "https://open.bigmodel.cn/api/paas/v4/chat/completions") const;
    void setAiEndpoint(const char *url);

    // Last used directory
    void getLastDir(char *buf, int len, const char *fallback = "") const;
    void setLastDir(const char *dir);

    // Recent files (max 10, most recent first). Backed by an external
    // "recent file.ini" next to the exe.
    int  recentCount();
    void recentGet(int index, char *buf, int len);
    void recentAdd(const char *path);
    void recentClear();

    // File-change detection for multi-window sync.
    // Returns true if settings.ini has been modified by another process.
    bool checkChanged();
    // Refresh the cached modification time so checkChanged() returns
    // false until the next external edit.
    void reload();

    // Restrict which keys this Config instance may write to the INI.
    // Empty (default) = unrestricted. Reads are never restricted.
    // Used by the AI chat process: it may only persist the ai_* keys so
    // it can never clobber settings owned by the main process (e.g. the
    // UI language). If settings.ini does not exist yet the whitelist is
    // ignored and a full default file is generated instead.
    void setWriteWhitelist(std::initializer_list<const char *> keys);

    // Set a callback to be invoked when settings.ini is modified by
    // another process. The callback is called on the main FLTK thread.
    void setChangeCallback(std::function<void()> callback);
    // Check if a change callback has been set (for static handler access)
    bool hasChangeCallback() const { return m_changeCallback ? true : false; }
    // Invoke the change callback
    void invokeChangeCallback() { if (m_changeCallback) m_changeCallback(); }

    // Start/stop the file watcher thread. On Windows, this uses
    // ReadDirectoryChangesW for immediate notification. On other
    // platforms, a fallback polling mechanism is used.
    void startWatcher();
    void stopWatcher();

private:
    // Watcher thread main function (Windows only)
    void watcherThreadFunc();

    std::filesystem::path m_iniPath;

    // In-memory cache of every key in [Settings]. Each getter does a
    // single map lookup; each setter updates the map and rewrites the
    // file. This is what makes checkChanged() + reload() O(N) instead
    // of re-parsing the INI on every read.
    std::map<std::string, std::string> m_entries;

    // Keys modified by this process since the last saveAll()/reload().
    // saveAll() merges with the on-disk file: only these keys may
    // overwrite disk values, everything else keeps the current disk
    // content (multi-process safety, see saveAll()).
    std::set<std::string> m_dirtyKeys;

    // Non-empty = writes restricted to these keys (setWriteWhitelist).
    std::vector<std::string> m_writeWhitelist;

    // Cached file modification time. std::filesystem::file_time_type is
    // platform-portable (Windows FILETIME / POSIX nanoseconds under the
    // hood). -1 means "not yet initialized" or "file does not exist".
    std::filesystem::file_time_type m_lastWriteTime{};

    // In-memory cache of recent file paths.
    std::vector<std::string> m_recentFiles;
    std::filesystem::path    m_recentFilePath;

    // All file I/O is serialized through a mutex so concurrent reads and
    // writes from the watcher timeout and the UI don't corrupt the file
    // or trip the file-change check.
    mutable std::mutex m_ioMutex;

    // File-change notification via ReadDirectoryChangesW (Windows).
    // When settings.ini is modified by another process, the watcher
    // thread wakes up and signals the main thread to reload.
    std::atomic<bool> m_watcherRunning{false};
    std::thread       m_watcherThread;

    // Callback for file change notifications. When a change is detected,
    // this callback is invoked on the main FLTK thread.
    std::function<void()> m_changeCallback;

    // Internal helpers
    std::filesystem::path getExeDir() const;

    // Shared constructor body: resolves paths, loads entries, validates
    // defaults, and persists if needed. Used by both constructors.
    void initFromDisk();
    void refreshFileTime();

public:
    // INI read/write helpers (public so Theme can serialize colors)
    int  readInt(const char *key, int fallback) const;
    void readStr(const char *key, char *buf, int len, const char *fallback) const;
    void writeInt(const char *key, int value);
    void writeStr(const char *key, const char *value);

    // Ensure a key has a valid integer value. If missing/out of range,
    // sets it to fallback. Returns true if the entry was modified.
    bool validateKey(const char *key, int fallback, int minVal, int maxVal);

    // Force a full rewrite of settings.ini from the current in-memory
    // state. Used after the watcher reloads it from disk and merges in
    // external edits.
    void saveAll();

    // Re-read settings.ini from disk into the in-memory cache (keeps
    // keys modified in this session). Called by refreshAll() so the
    // user can hand-edit settings.ini and pick up changes without restart.
    void reloadFromDisk();

    // Load the entire settings.ini into memory. Returns an empty map
    // (not nullptr) if the file does not exist.
    void loadAll(std::vector<std::pair<std::string, std::string>> &entries);

    // Locate "recent file.ini" next to the running exe and load it.
    void recentLoad();
    // Write the in-memory list back to "recent file.ini".
    void recentSave();
};
