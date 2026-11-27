// MainWindow_files.cpp - MainWindow file operations (new/open/save/close,
// status bar update, auto-save). Moved here from MainWindow.cpp so the
// main window file stays manageable. Member functions defined in this
// translation unit are part of the same MainWindow class declared in
// ui/MainWindow.h.
#include "ui/MainWindow.h"

#include "editor/Document.h"
#include "editor/Editor.h"
#include "ui/FindReplace.h"
#include "ui/GoTo.h"
#include "ui/ConfirmDialog.h"
#include "ui/SettingsDialog.h"
#include "core/Config.h"
#include "core/PathUtils.h"   // pathutil::isMarkdownPath（自动预览的扩展名判定）
#include "core/Theme.h"
#include "core/I18n.h"
#include "core/UiBridge.h"
#include "core/OpLog.h"
#include "ui/Layout.h"
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Output.H>
#include <FL/fl_ask.H>
#include <FL/filename.H>
#include <FL/fl_string_functions.h>
#include <FL/fl_draw.H>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

// UTF-8 -> UTF-16：中文文件路径传给 Win32 宽字符 API（ANSI 版在非本地
// 代码页路径下会变问号）
static std::wstring widen(const std::string &utf8) {
    if (utf8.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], n);
    while (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

// 登记到系统"最近使用"（任务栏右键 Jump List）。一次调用即可，
// Windows 资源管理器自动聚合，不新增任何界面。
static void notifyRecentDocs(const char *path) {
    if (!path || !path[0]) return;
    SHAddToRecentDocs(SHARD_PATHW, widen(path).c_str());
}

// 把本地化文案里的唯一占位符换成已经格式化好的参数值。
//
// 语言文件里的句子带 %d / %lld / %s，但译文绝不能直接当 snprintf 的
// 格式串：任何一条译文里出现孤立的 '%'（比如 "100%"）都会被当成转换符，
// 读到不存在的参数，属于未定义行为；16 种语言的文本也无法由编译器校验。
// 这里只把译文当纯文本：定位项目实际使用的三种占位符，前后两段都作为
// %s 参数交给代码内的字面量格式串拼接，不新增任何 I18n 键。
static std::string fillI18nArg(const char *tpl, const std::string &value) {
    if (!tpl) return {};
    static const char *const marks[] = { "%lld", "%d", "%s" };
    for (const char *mark : marks) {
        const char *p = strstr(tpl, mark);
        if (!p) continue;
        const std::string prefix(tpl, p);
        const std::string suffix(p + strlen(mark));
        char out[1024];
        snprintf(out, sizeof(out), "%s%s%s",
                 prefix.c_str(), value.c_str(), suffix.c_str());
        return std::string(out);
    }
    return tpl;   // 没有占位符：译文原样返回
}



void MainWindow::newFile() {
    bool multi = m_cfg->getMultiTab();

    if (multi) {
        // Multi-tab mode: create a new tab without touching existing ones.
        Document *doc = new Document();
        addTab(doc, "Unnamed");
        updateTitle();
        updateStatusBar();
        // 新标签按 x=0 全宽创建 → 重排落到内容区左缘；侧栏回到"未打开文件"
        layoutTabs();
        syncSidePanel();
        redraw();
        return;
    }

    // Single-tab mode: replace the current document with a fresh empty
    // one. If the current document has unsaved changes, prompt first.
    int i = activeTabIndex();
    if (i >= 0 && m_tabsList[i].doc->isDirty()) {
        if (!checkSaveBeforeClose(i)) return;  // user cancelled
    }
    // Close the current tab in-place (without re-prompting): erase it
    // from m_tabsList, remove its page from Fl_Tabs, and delete the doc.
    // We do NOT call closeCurrentTab() here because that helper would
    // recursively call newFile() when m_tabsList becomes empty.
    if (i >= 0) {
        Tab t = m_tabsList[i];
        m_tabsList.erase(m_tabsList.begin() + i);
        // Detach the editor from its buffer and destroy the page BEFORE
        // deleting the document (matches openFile): the editor may still be
        // focused and would touch the freed buffer on the next FL_UNFOCUS,
        // and m_tabs->remove() only unlinks the page - it does not free it.
        t.editor->buffer(nullptr);
        m_tabs->remove(t.page);
        delete t.page;
        delete t.doc;
    }
    // Create a fresh empty document in place of the old one.
    Document *doc = new Document();
    addTab(doc, "Unnamed");
    updateTitle();
    updateStatusBar();
    layoutTabs();          // 新标签 x=0 全宽 → 重排落到内容区左缘
    syncSidePanel();       // 新文档无路径 → 左侧面板回到"未打开文件"
    redraw();
}

bool MainWindow::closeCurrentTab() {
    int i = activeTabIndex();
    if (i < 0) return false;
    if (!checkSaveBeforeClose(i)) return false;

    // Single-tab-close-equals-close-window rule: when only one tab is
    // left, closing it (via Ctrl+W, middle-click, or the tab ✕ button)
    // closes the whole window instead of leaving an empty editor.
    if (m_tabsList.size() <= 1) {
        // Hide the window; Fl::hide_all_windows() will be picked up by
        // the FLTK main loop and the app will exit. Destructor cleans
        // up the remaining tab storage.
        hide();
        return true;
    }

    Tab t = m_tabsList[i];
    m_tabsList.erase(m_tabsList.begin() + i);

    // Detach the editor from its buffer and destroy the page BEFORE
    // deleting the document (matches openFile). m_tabs->remove() only
    // unlinks the page from Fl_Tabs - it does NOT free it, so we must
    // delete t.page (which owns the editor) explicitly.
    t.editor->buffer(nullptr);
    m_tabs->remove(t.page);
    delete t.page;
    delete t.doc;

    // Fl_Tabs::value_ still points at the deleted page after remove(), so
    // activeTabIndex() is -1 here and "fallback 0" made every close jump to
    // the first tab. Pick the successor by position instead: the tab that
    // took the closed one's slot, or the new last tab if one was cut off.
    int target = i >= (int)m_tabsList.size() ? (int)m_tabsList.size() - 1 : i;
    switchToTab(target);
    syncTitleBar();
    redraw();
    return true;
}

// Close a specific tab by index (called from TitleBar).
void MainWindow::closeTab(int index) {
    if (index < 0 || index >= (int)m_tabsList.size()) return;
    if (!checkSaveBeforeClose(index)) return;

    if (m_tabsList.size() <= 1) {
        hide();
        return;
    }

    Tab t = m_tabsList[index];
    m_tabsList.erase(m_tabsList.begin() + index);
    // Same detach + destroy-page-before-doc ordering as openFile (see the
    // closeCurrentTab comment): avoid UAF and the page/editor leak.
    t.editor->buffer(nullptr);
    m_tabs->remove(t.page);
    delete t.page;
    delete t.doc;

    int target = index >= (int)m_tabsList.size() ? (int)m_tabsList.size() - 1 : index;
    switchToTab(target);   // see closeCurrentTab: activeTabIndex() is stale after remove()
    syncTitleBar();
    redraw();
}

// Launch a new Pecia process with the given file (UTF-8 path). Used by
// openFile in single-tab mode: a second open must not replace the open
// document (plan.txt #6), so the new file goes into its own window.
// Returns false when the process could not be started.
bool MainWindow::spawnWindowWithFile(const char *path) {
    if (!path || !*path) return false;
    // 宽字符路径：中文安装目录下 GetModuleFileNameA/CreateProcessA 失效。
    wchar_t exePathW[MAX_PATH];
    GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
    // Only the product may spawn a copy of ITSELF. A test or probe binary
    // would otherwise spawn ITSELF - and that copy would spawn another one
    // (the copy reaches the same code path), which is how the smoke test
    // used to end in a self-perpetuating process tree. The product's exe is
    // "Pecia.exe" (a renamed build still starts with "Pecia").
    const wchar_t *base = wcsrchr(exePathW, L'\\');
    base = base ? base + 1 : exePathW;
    if (_wcsnicmp(base, L"Pecia", 5) != 0) return false;
    std::wstring cmd = std::wstring(L"\"") + exePathW + L"\" \"" +
                       widen(path) + L"\"";
    // 让新进程相对当前窗口层叠：否则它会读到同一个已保存位置，
    // 与父窗口完全重叠（同 cbNewWindow / 分离标签的做法）。
    char posValue[64];
    snprintf(posValue, sizeof(posValue), "%d,%d", x(), y());
    SetEnvironmentVariableA("PECIA_POS", posValue);
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exePathW, &cmd[0], nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &si, &pi)) {
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// --------------------------------------------------------------------------
// "打开 .md 文件时：无操作 / 分屏预览 / 仅预览" (Options > Interface & System)
// --------------------------------------------------------------------------

void MainWindow::autoPreviewCb(void *data) {
    // 定时器回调边界：异常不能逃出事件循环（AGENTS.md 一.6 / 开发指南十.2）。
    try {
        MainWindow *self = static_cast<MainWindow *>(data);
        if (!self) return;
        // 延后一帧期间用户可能已经自己开了（或关了）预览、甚至关了窗口，
        // 所以这里再判一次；setPreviewOnly() 是幂等的"设定目标状态"，
        // 不会把用户手动开的预览又翻回去。
        const int mode = self->m_pendingAutoPreviewMode;
        self->m_pendingAutoPreviewMode = Config::AutoPreviewNone;
        if (mode == Config::AutoPreviewNone) return;
        self->setPreviewOnly(mode == Config::AutoPreviewOnly);
    } catch (...) {
        opLog("autoPreviewCb: unknown exception");
    }
}

void MainWindow::openFile(const char *path) {
    if (!path || !*path) return;

    // Reuse an existing tab if the file is already open
    int existing = findTabByPath(path);
    if (existing >= 0) {
        switchToTab(existing);
        return;
    }

    // NOTE: we never load into an existing tab's live buffer. Loading via
    // buffer->text() fires the attached Fl_Text_Display's modify callback,
    // which re-lays out the ENTIRE buffer (per-character GDI width
    // measurement for word wrap) synchronously - seconds for a 14 MB file,
    // minutes/crash for hundreds of MB. Always load into a fresh Document
    // (no display attached) and mount it afterwards.

    const bool multi = m_cfg->getMultiTab();
    if (!multi) {
        // Single-tab mode: never silently replace an open document. If the
        // current tab already holds content, open the new file in its own
        // Pecia process instead (plan.txt #6). An untouched empty "Unnamed"
        // placeholder is still replaced in place.
        int i = activeTabIndex();
        bool occupied = false;
        if (i >= 0) {
            Document *d = m_tabsList[i].doc;
            occupied = d->isDirty() || d->filePath()[0] ||
                       (d->buffer() && d->buffer()->length() > 0);
        }
        if (occupied && spawnWindowWithFile(path)) return;
        // Spawn failed (e.g. exe path unavailable) - fall through to the
        // historic replace behaviour so the file still opens.
        if (i >= 0 && m_tabsList[i].doc->isDirty()) {
            if (!checkSaveBeforeClose(i)) return;  // user cancelled
        }
        if (!replaceActiveTabWithFile(path)) return;
    } else {
        if (!addFileAsNewTab(path)) return;
    }
    afterFileOpened(path);
}

// ---------------------------------------------------------------------------
// Host/agent open (core/HostOps "doc.open").
//
// The agent is connected to THIS window, so the file must open HERE. The
// normal openFile() would, in single-tab mode, hand the file to a SECOND
// Pecia process (spawnWindowWithFile) and leave the agent editing the
// previous document while another window shows the new one - the op would
// then report success for something that did not happen here.
//
// Never spawns a process, never opens a modal dialog: when the active
// document has unsaved changes the call REFUSES instead of discarding them.
// ---------------------------------------------------------------------------
bool MainWindow::openFileForHost(const char *path, std::string *err) {
    if (!path || !*path) {
        if (err) *err = "bad_arg: empty path";
        return false;
    }
    int existing = findTabByPath(path);
    if (existing >= 0) {
        switchToTab(existing);
        return true;
    }
    int i = activeTabIndex();
    if (i >= 0 && m_tabsList[i].doc && m_tabsList[i].doc->isDirty()) {
        if (err) *err = "unsaved_changes: the active document has unsaved changes; "
                        "save it first (doc.save) before opening another file";
        return false;
    }
    if (!replaceActiveTabWithFile(path)) {
        if (err) *err = "io: cannot open the file";
        return false;
    }
    afterFileOpened(path);
    return true;
}

// ---------------------------------------------------------------------------
// Host/agent new (core/HostOps "doc.new"): same "no modal, no data loss"
// contract as openFileForHost.
// ---------------------------------------------------------------------------
bool MainWindow::newFileForHost(std::string *err) {
    if (!m_cfg->getMultiTab()) {
        int i = activeTabIndex();
        if (i >= 0 && m_tabsList[i].doc && m_tabsList[i].doc->isDirty()) {
            if (err) *err = "unsaved_changes: the active document has unsaved changes; "
                            "save it first (doc.save) before starting a new one";
            return false;
        }
    }
    newFile();
    return true;
}

// ---------------------------------------------------------------------------
// Host/agent save (core/HostOps "doc.save"): never opens a modal. An
// untitled document has no path to save into, and the Save-As chooser is a
// blocking modal - refuse instead of stalling the UI thread behind it.
// ---------------------------------------------------------------------------
bool MainWindow::doSaveForHost(int tabIndex, std::string *err) {
    if (tabIndex < 0 || tabIndex >= (int)m_tabsList.size() ||
        !m_tabsList[tabIndex].doc) {
        if (err) *err = "not_found: no active document";
        return false;
    }
    Document *doc = m_tabsList[tabIndex].doc;
    if (doc->isReadOnly()) {
        if (err) *err = "read_only: the document cannot be overwritten";
        return false;
    }
    if (!doc->filePath()[0]) {
        if (err) *err = "no_path: the document has no file path; "
                        "give it a name before saving";
        return false;
    }
    // A legacy/ANSI target cannot represent every character. The interactive
    // path asks the user ("save as UTF-8?"); the agent path refuses, because
    // a confirmation dialog would block the whole UI thread.
    if (!doc->canEncode(doc->encoding())) {
        if (err) *err = "encoding: saving in the current encoding would lose data; "
                        "convert the document to UTF-8 first";
        return false;
    }
    const bool ok = doc->saveFile();
    if (ok) {
        updateTabLabel(tabIndex);
        updateTitle();
        updateStatusBar();
    } else if (err) {
        *err = "io: cannot write the file";
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Shared open-file plumbing
// ---------------------------------------------------------------------------

// Load the file and put it in place of the active tab. The file is loaded
// FIRST: the old order erased the tab before loading, so a failed load
// destroyed the document that was open (and left an empty one behind).
bool MainWindow::replaceActiveTabWithFile(const char *path) {
    Document *doc = new Document();
    if (!doc->loadFile(path)) {
        delete doc;
        return false;
    }
    int i = activeTabIndex();
    if (i >= 0) {
        Tab t = m_tabsList[i];
        m_tabsList.erase(m_tabsList.begin() + i);
        m_tabs->remove(t.page);
        // Detach the old editor from its buffer and destroy the page
        // BEFORE deleting the document: the old editor may still be the
        // focused widget and would otherwise touch the freed buffer on
        // the next FL_UNFOCUS (use-after-free -> intermittent crash).
        t.editor->buffer(nullptr);
        delete t.page;
        delete t.doc;
    }
    addTab(doc, fl_filename_name(path));
    return true;
}

// Load the file into a brand new tab (multi-tab mode).
bool MainWindow::addFileAsNewTab(const char *path) {
    Document *doc = new Document();
    if (!doc->loadFile(path)) {
        delete doc;
        return false;
    }
    addTab(doc, fl_filename_name(path));

    // If the only other tab is an untouched "Unnamed" placeholder from
    // startup (no text, not dirty, no file path), close it so launching
    // "Pecia.exe <file>" doesn't leave an empty Unnamed tab behind.
    if (m_tabsList.size() >= 2) {
        Tab &t0 = m_tabsList[0];
        Document *d0 = t0.doc;
        if (!d0->isDirty() && !d0->filePath()[0] &&
            d0->buffer() && d0->buffer()->length() == 0) {
            // Switch to the new tab first, then remove index 0.
            switchToTab((int)m_tabsList.size() - 1);
            m_tabs->remove(t0.page);
            t0.editor->buffer(nullptr);
            delete t0.page;
            delete t0.doc;
            m_tabsList.erase(m_tabsList.begin());
            layoutTabs();
            syncTitleBar();
        }
    }
    return true;
}

// The tail every successful open shares.
void MainWindow::afterFileOpened(const char *path) {
    // Add to recent files
    m_cfg->recentAdd(path);
    notifyRecentDocs(path);
    refreshRecentMenu();
    updateTitle();
    updateStatusBar();
    // 新标签的编辑器/页面在 addTab 里按 x=0 全宽创建，必须重排一次才能
    // 落到「内容区左缘」（有侧栏时右移，否则编辑器画在面板上面）。
    layoutTabs();
    // Markdown 预览：打开文件后立即刷新（预览先开启的场景也能同步呈现）
    if (m_previewActive) refreshPreview();

    // 左侧面板：新文件的目录/路径变了，重建文件树并高亮当前文件
    syncSidePanel();

    // 打开 .md/.markdown 时按配置开预览（Options > Interface & System）。
    // 只有走到这里才算"新打开了一个文件" —— 已经打开过的走 switchToTab()、
    // 加载失败、以及用户在保存确认框点了取消，都在上面提前 return 了。
    // 所以切标签页和另存为都不会触发这里（另存为根本不经过 openFile）。
    // 延后一帧：refreshPreview() 是同步渲染，大文档会把"打开"这一步本身
    // 拖成白屏无响应；延后一帧则窗口先画出来、文件先显示，之后才渲染。
    // 预览已经开着时不再改动排布（用户手动选的布局优先）。
    if (!m_previewActive && m_cfg && path && pathutil::isMarkdownPath(path)) {
        const int mode = m_cfg->getAutoPreviewMode();
        if (mode != Config::AutoPreviewNone) {
            m_pendingAutoPreviewMode = mode;
            // 连续打开多个 md 时会连续入队同一个回调；不去重就会排队执行
            // 多次 setPreviewOnly（最后一次覆盖前面用户手动改的布局）。
            Fl::remove_timeout(autoPreviewCb, this);
            Fl::add_timeout(0.0, autoPreviewCb, this);
        }
    }
}

// --------------------------------------------------------------------------
// Detach extra tabs into independent windows when multi-tab is turned off.
// --------------------------------------------------------------------------

bool MainWindow::detachExtraTabsToNewWindows() {
    int active = activeTabIndex();
    if (active < 0) return true;
    if ((int)m_tabsList.size() <= 1) return true;  // nothing to detach

#if defined(_WIN32)
    // First pass: for any Unnamed+dirty tab, prompt Save As. If the user
    // cancels any of them, abort the whole detach operation so nothing
    // is lost.
    for (int i = 0; i < (int)m_tabsList.size(); ++i) {
        if (i == active) continue;
        Tab &t = m_tabsList[i];
        const char *path = t.doc->filePath();
        if ((!path || !path[0]) && t.doc->isDirty()) {
            // Make this tab visible while prompting so the user sees it
            switchToTab(i);
            if (!doSaveAs(i)) {
                // User cancelled - abort the detach entirely. Restore
                // the previously-active tab and signal failure.
                switchToTab(active);
                return false;
            }
        }
    }

    // Second pass: for dirty tabs that already have a file path, save
    // them in place (no prompt).
    for (int i = 0; i < (int)m_tabsList.size(); ++i) {
        if (i == active) continue;
        Tab &t = m_tabsList[i];
        const char *path = t.doc->filePath();
        if (path && path[0] && t.doc->isDirty()) {
            doSave(i);
        }
    }

    wchar_t exePathW[MAX_PATH] = L"";
    if (GetModuleFileNameW(nullptr, exePathW, MAX_PATH) == 0) {
        // Cannot get exe path - fall back to closing extra tabs (caller
        // already set multi-tab off; we just won't spawn new windows).
    }

    // Third pass: launch a new Pecia process for each non-active tab
    // (skipping Unnamed+non-dirty tabs, which have nothing to show).
    // We erase from m_tabsList as we go, so collect the paths first.
    struct Launch { std::string path; int tabIndex; };
    std::vector<Launch> toLaunch;
    std::vector<int> toRemove;

    for (int i = 0; i < (int)m_tabsList.size(); ++i) {
        if (i == active) continue;
        Tab &t = m_tabsList[i];
        const char *path = t.doc->filePath();
        if (!path || !path[0]) {
            // Unnamed, non-dirty (dirty ones were saved above or the
            // user cancelled, in which case we already returned false).
            toRemove.push_back(i);
            continue;
        }
        toLaunch.push_back({std::string(path), i});
    }

    // Launch new processes. We pass the file path as the command-line
    // argument (main.cpp opens argv[1] when present).
    // Each process gets an incrementing delay to stagger their fixTaskbarCb
    // calls - when multiple processes start simultaneously, their
    // SetForegroundWindow calls conflict and some taskbar buttons fail.
    int delayMs = 0;
    for (const auto &item : toLaunch) {
        if (exePathW[0]) {
            char posValue[128];
            snprintf(posValue, sizeof(posValue), "%d,%d", x(), y());
            SetEnvironmentVariableA("PECIA_POS", posValue);
            // Set delay for fixTaskbarCb to avoid SetForegroundWindow conflicts
            char delayValue[16];
            snprintf(delayValue, sizeof(delayValue), "%d", delayMs);
            SetEnvironmentVariableA("PECIA_DELAY", delayValue);
            // ShellExecuteEx with the file path as the lpParameters.
            // The path is quoted to handle spaces; wide form keeps a
            // Chinese path intact (main.cpp re-parses it via
            // CommandLineToArgvW, not ANSI argv).
            std::wstring params = L"\"" + widen(item.path) + L"\"";
            ShellExecuteW(nullptr, L"open", exePathW, params.c_str(), nullptr, SW_SHOWNORMAL);
            delayMs += 100;
        }
    }

    // Remove the detached tabs from this window. We must erase in
    // reverse order so the indices stay valid during erasure.
    // Build a combined list of indices to remove (launched + Unnamed).
    std::vector<int> allRemove = toRemove;
    for (const auto &item : toLaunch) allRemove.push_back(item.tabIndex);
    std::sort(allRemove.rbegin(), allRemove.rend());
    for (int idx : allRemove) {
        Tab t = m_tabsList[idx];
        m_tabsList.erase(m_tabsList.begin() + idx);
        // Same ordering as the other tab-close paths (see closeCurrentTab):
        // detach editor from buffer and free the page before deleting the
        // document, so a still-focused editor can't touch a freed buffer and
        // the page/editor isn't leaked (m_tabs->remove() only unlinks it).
        t.editor->buffer(nullptr);
        m_tabs->remove(t.page);
        delete t.page;
        delete t.doc;
    }

    // Switch to the (now only) remaining tab.
    int newActive = active;
    // After removal, the active tab's index may have shifted. Recompute
    // by clamping to the new size.
    if (newActive >= (int)m_tabsList.size()) newActive = (int)m_tabsList.size() - 1;
    if (newActive < 0) newActive = 0;
    switchToTab(newActive);
#else
    // Non-Windows: no ShellExecuteA, just close extra tabs (best-effort).
    (void)active;
#endif
    syncTitleBar();
    redraw();
    return true;
}

// --------------------------------------------------------------------------
// State updates
// --------------------------------------------------------------------------

void MainWindow::updateTitle() {
    Tab *t = activeTab();
    if (!t) { copy_label("Pecia"); syncTitleBar(); return; }
    const char *path = t->doc->filePath();
    char buf[FL_PATH_MAX + 32];
    if (path && path[0]) {
        const char *name = fl_filename_name(path);
        if (t->doc->isDirty())
            snprintf(buf, sizeof(buf), "*%s - Pecia", name);   // *filename - Pecia
        else
            snprintf(buf, sizeof(buf), "%s - Pecia", name);
    } else {
        if (t->doc->isDirty())
            snprintf(buf, sizeof(buf), "*Unnamed - Pecia");
        else
            snprintf(buf, sizeof(buf), "Unnamed - Pecia");
    }
    copy_label(buf);
    // Explicitly redraw the custom title bar: copy_label() damages the
    // window but the TitleBar child widget doesn't pick up the new label
    // text unless it's damaged directly. Without this, the dirty mark (*)
    // only appears after the next unrelated redraw (e.g. cursor blink),
    // causing a noticeable delay.
    syncTitleBar();
}

void MainWindow::updateStatusBar() {
    Tab *t = activeTab();
    if (!t || !m_status) return;
    int pos = t->editor->insert_position();
    Fl_Text_Buffer *buf = t->doc->buffer();

    // Cache is keyed to its buffer: after a tab switch the previous
    // document's (pos, line) anchor is meaningless for this one and must
    // be discarded (it caused wrong line numbers + a worst-case full
    // count_lines scan over the new file on every switch).
    if (buf != m_statBuf) {
        m_statBuf = buf;
        m_statPos = -1;
        m_statLine = 1;
        m_statLineStart = -1;
    }

    // Defensive: FLTK 1.4.5 count_lines/line_start hang when a position
    // exceeds the buffer length (undo can leave the cursor stale). Clamp
    // both the cursor and the cached position before any scan.
    if (pos < 0) pos = 0;
    int blen = buf->length();
    if (pos > blen) pos = blen;
    if (m_statPos > blen) m_statPos = blen;
    if (m_statPos < 0) m_statPos = 0;

    // Incremental line tracking: count_lines(0, pos) is a naive per-byte
    // scan that takes ~0.4 s on a 390 MB file, so on large buffers we only
    // count the lines between the previous and current cursor positions
    // (usually a few MB between search matches - tens of ms).
    int line;
    int lineStart;
    if (pos == m_statPos) {
        line = m_statLine;
        lineStart = m_statLineStart;
    } else if (m_statPos >= 0 &&
               buf->length() > kStatLineIncrementalLimitBytes) {
        if (pos >= m_statPos) {
            line = m_statLine + buf->count_lines(m_statPos, pos);
        } else {
            line = m_statLine - buf->count_lines(pos, m_statPos);
        }
        if (line < 1) line = 1;
        lineStart = buf->line_start(pos);
    } else {
        line = buf->count_lines(0, pos) + 1;
        lineStart = buf->line_start(pos);
    }
    m_statPos = pos;
    m_statLine = line;
    m_statLineStart = lineStart;

    // Use UTF-8 character count instead of byte offset so that CJK
    // characters (3 bytes in UTF-8) advance col by 1, not 3.
    int col = buf->count_displayed_characters(lineStart, pos) + 1;

    // Zoom percentage: derived from current font size relative to the
    // persisted "base" size captured at startup. If the user's saved
    // font size is 13, 13px == 100%, 26px == 200%, etc. If a previous
    // session saved 14, then 14px == 100% on this startup.
    int pct = (m_baseFontSize > 0)
              ? (int)((long)fontSize() * 100 / m_baseFontSize)
              : 100;

    const char *enc = t->doc->encodingName();
    if (!enc) enc = "";

    // File size: use the buffer length (UTF-8 byte count) and format
    // it with an adaptive B/KB/MB suffix and 2 decimal places.
    long bytes = buf->length();
    char sizeStr[32];
    if (bytes < 1024) {
        snprintf(sizeStr, sizeof(sizeStr), "%ld B", bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(sizeStr, sizeof(sizeStr), "%.2f KB", bytes / 1024.0);
    } else {
        snprintf(sizeStr, sizeof(sizeStr), "%.2f MB", bytes / (1024.0 * 1024.0));
    }

    // Read-only notice (large files open read-only) appears only when
    // needed, appended after Size - keeping the normal layout untouched
    // so the enc/Size spacing stays single when not read-only.
    //
    // Position is shown as bare "line:column" with no label: the previous
    // "Ln 12 / Col 5" was hardcoded English in an otherwise fully
    // translated UI, and the two numbers are the ones everybody reads
    // first, so the shortest form wins (2026-10-31).
    //
    // 仅预览时编辑区被隐藏、光标不存在，行列号只是"上一个光标"的陈旧值：
    // 整段去掉，与菜单栏「编辑」的置灰状态保持一致（2026-11-01 用户定）。
    // 缩放/编码/大小描述的是文档本身，照旧显示。
    //
    // Every WORD on this bar comes from the language file. It used to carry
    // hardcoded English ("Read-only", "Size", "AI") in an otherwise fully
    // translated UI - the only part of the status line the user could not
    // read in their own language.
    std::string ro;
    if (t->doc->isReadOnly())
        ro = std::string("     ") + I18n::get("status.readonly");
    // Last script-run result (m037): short verb + the run's wall-clock cost,
    // same conditional append style as the read-only notice, so the normal
    // spacing is untouched when nothing has run. renderNotice() renders the
    // stored KEY in the CURRENT language, so switching the language
    // re-renders the notice instead of leaving the old wording on screen.
    const std::string notice = m_scriptNotice.empty()
                             ? std::string()
                             : std::string("     ") + renderNotice(m_scriptNotice);
    // While the AI column has a request in flight its elapsed seconds belong
    // here: the pane has no title bar of its own any more (m039), and the
    // number is the only thing the wait is worth describing.
    std::string aiSeg;
    if (m_aiChatDocked && m_aiChatElapsed >= 0) {
        char num[32];
        snprintf(num, sizeof(num), "%lld", m_aiChatElapsed);
        aiSeg = std::string("     ")
              + fillI18nArg(I18n::get("status.ai_elapsed"), num);
    }
    // The bar is assembled as a string, not through one big snprintf: the
    // localized segments have very different lengths, and a fixed buffer
    // would silently cut the tail (the script notice lives there) in the
    // languages that need more room.
    std::string bar;
    if (!m_previewOnly) {
        char posSeg[32];
        snprintf(posSeg, sizeof(posSeg), "%d:%d", line, col);
        bar += posSeg;
        bar += "     ";
    }
    char pctSeg[16];
    snprintf(pctSeg, sizeof(pctSeg), "%d%%", pct);
    bar += pctSeg;
    bar += "     ";
    bar += enc;
    bar += "     ";
    bar += I18n::get("status.size");
    bar += " ";
    bar += sizeStr;
    bar += ro;
    bar += notice;
    bar += aiSeg;
    m_status->value(bar.c_str());
}

// The text the status line currently shows. A test asserts on it through
// this accessor instead of hunting for the right Fl_Output in the widget
// tree: the bar's content is localized, so a content-sniffing lookup would
// only work in one language.
std::string MainWindow::statusBarText() const {
    if (!m_status) return std::string();
    const char *v = m_status->value();
    return v ? std::string(v) : std::string();
}

// Render a status notice in the CURRENT UI language. The notice keeps its
// i18n key (see MainWindow::StatusNotice), so this is the single place
// where its wording is decided - and it runs on every repaint, which is
// what makes a language switch re-render the notice.
std::string MainWindow::renderNotice(const StatusNotice &n) const {
    if (n.key.empty()) return std::string();
    if (!n.hasArg) return std::string(I18n::get(n.key.c_str()));
    char num[32];
    snprintf(num, sizeof(num), "%lld", n.arg);
    return fillI18nArg(I18n::get(n.key.c_str()), num);
}

void MainWindow::tick() {
    // Light-weight refresh - just the status bar. Title is updated via
    // cbModify when the buffer changes.
    updateStatusBar();
}

// --------------------------------------------------------------------------
// File operations
// --------------------------------------------------------------------------

bool MainWindow::checkSaveBeforeClose(int tabIndex) {
    if (tabIndex < 0 || tabIndex >= (int)m_tabsList.size()) return true;
    Document *doc = m_tabsList[tabIndex].doc;
    if (!doc->isDirty()) return true;
    // Switch to the tab so the user sees what they're being asked about
    switchToTab(tabIndex);
    int r = ConfirmDialog(I18n::get("confirm.unsaved.title"),
        I18n::get("confirm.unsaved.msg"),
        I18n::get("dlg.save"), I18n::get("dlg.dontsave"), I18n::get("dlg.cancel"),
        &m_theme, m_cfg->getUiFontSize()).run();
    if (r == 0) return doSave(tabIndex);
    if (r == 1) return true;
    return false;
}

// Self-update closes this process, so unsaved documents would be lost.
// Ask once, then save every dirty tab (Save As included), aborting the
// update if the user cancels anything.
bool MainWindow::confirmUnsavedBeforeUpdate() {
    std::vector<int> dirty;
    for (int i = 0; i < (int)m_tabsList.size(); ++i) {
        if (m_tabsList[i].doc && m_tabsList[i].doc->isDirty()) dirty.push_back(i);
    }
    if (dirty.empty()) return true;

    char num[32];
    snprintf(num, sizeof(num), "%d", (int)dirty.size());
    const std::string msg = fillI18nArg(I18n::get("update.unsaved.msg"), num);
    int r = ConfirmDialog(I18n::get("update.unsaved.title"), msg.c_str(),
                          I18n::get("dlg.save"), I18n::get("dlg.cancel"), nullptr,
                          &m_theme, m_cfg->getUiFontSize()).run();
    if (r != 0) return false;   // cancelled

    for (int i : dirty) {
        if (i >= (int)m_tabsList.size()) continue;
        if (!m_tabsList[i].doc || !m_tabsList[i].doc->isDirty()) continue;
        switchToTab(i);                 // show the document being saved
        if (!doSave(i)) return false;   // cancelled Save As / write error
    }
    return true;
}

bool MainWindow::resolveLossyEncoding(Document *doc, Encoding &enc) {
    if (!doc) return false;
    if (doc->canEncode(enc)) return true;

    // A legacy/ANSI target cannot represent some characters. Never write
    // '?' substitutions: offer Notepad's "save as Unicode" escape hatch.
    if (!doc->canEncode(Encoding::UTF8)) {
        if (UiBridge *b = uiBridge())
            b->message(I18n::get("error.title"), I18n::get("error.encode_failed"),
                       I18n::get("dlg.ok"));
        return false;
    }
    const std::string msg = fillI18nArg(I18n::get("confirm.lossy_save.msg"),
                                        encodingDisplayName(enc));
    int r = ConfirmDialog(I18n::get("confirm.lossy_save.title"), msg.c_str(),
                          I18n::get("dlg.save_as_utf8"), I18n::get("dlg.cancel"),
                          nullptr, &m_theme, m_cfg->getUiFontSize()).run();
    if (r != 0) return false;   // cancelled / closed
    enc = Encoding::UTF8;
    return true;
}

bool MainWindow::doSave(int tabIndex) {
    if (tabIndex < 0 || tabIndex >= (int)m_tabsList.size()) return false;
    Document *doc = m_tabsList[tabIndex].doc;
    // Read-only documents (e.g. >100 MB large files) cannot be overwritten;
    // the user may still use Save As to export a copy. Say so: a silent
    // "nothing happened" on Ctrl+S reads like the save succeeded.
    if (doc->isReadOnly()) {
        const std::string _msg = fillI18nArg(I18n::get("error.readonly_save"),
                                             doc->filePath());
        if (UiBridge *b = uiBridge())
            b->message(I18n::get("error.title"), _msg.c_str(), I18n::get("dlg.ok"));
        return false;
    }
    if (doc->filePath()[0]) {
        Encoding enc = doc->encoding();
        if (!resolveLossyEncoding(doc, enc)) return false;
        if (enc != doc->encoding()) doc->setEncoding(enc);
        bool ok = doc->saveFile();
        if (ok) {
            updateTabLabel(tabIndex);
            updateTitle();
            updateStatusBar();
        }
        return ok;
    }
    return doSaveAs(tabIndex);
}

bool MainWindow::doSaveAs(int tabIndex) {
    if (tabIndex < 0 || tabIndex >= (int)m_tabsList.size()) return false;
    Document *doc = m_tabsList[tabIndex].doc;

    // Default directory: existing file's dir, or last-used dir
    char startDirA[FL_PATH_MAX] = ".";
    if (doc->filePath()[0]) {
        fl_strlcpy(startDirA, doc->filePath(), FL_PATH_MAX);
    } else {
        char lastDir[FL_PATH_MAX];
        m_cfg->getLastDir(lastDir, sizeof(lastDir), "");
        if (lastDir[0]) fl_strlcpy(startDirA, lastDir, FL_PATH_MAX);
    }
    WCHAR startDirW[FL_PATH_MAX];
    MultiByteToWideChar(CP_UTF8, 0, startDirA, -1, startDirW, FL_PATH_MAX);

    IFileSaveDialog *pfd = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IFileSaveDialog, (void**)&pfd);
    if (FAILED(hr) || !pfd) return false;

    // Filter labels follow the UI language; the patterns stay language-neutral.
    // Only three types, like Notepad's short list: text, Markdown, and "all
    // files" - the last one is the only way to keep an arbitrary extension
    // (.csv, .log, .ini ...) when re-saving a file that already has one.
    const std::wstring wAll  = widen(I18n::get("filedlg.all"));
    const std::wstring wText = widen(I18n::get("filedlg.text"));
    const std::wstring wMd   = widen(I18n::get("filedlg.markdown"));
    COMDLG_FILTERSPEC fileTypes[] = {
        { wText.c_str(), L"*.txt" },
        { wMd.c_str(),   L"*.md"  },
        { wAll.c_str(),  L"*.*"   },
    };
    pfd->SetFileTypes(ARRAYSIZE(fileTypes), fileTypes);
    // Default type: .md -> Markdown, .txt -> text, any OTHER extension ->
    // "all files" (picking *.txt there would hide from the folder listing
    // the very file being re-saved), no extension (new file) -> text.
    UINT defIdx = 1;
    const char *curPath = doc->filePath();
    if (curPath && curPath[0]) {
        const char *ext = fl_filename_ext(curPath);
        if (ext && ext[0]) {
            if (_stricmp(ext, ".md") == 0)       defIdx = 2;
            else if (_stricmp(ext, ".txt") != 0) defIdx = 3;
        }
    }
    pfd->SetFileTypeIndex(defIdx);

    // Set default folder
    IShellItem *psiFolder = nullptr;
    hr = SHCreateItemFromParsingName(startDirW, nullptr, IID_IShellItem, (void**)&psiFolder);
    if (SUCCEEDED(hr) && psiFolder) {
        pfd->SetFolder(psiFolder);
        psiFolder->Release();
    }

    // If file already has a path, suggest the file name
    if (doc->filePath()[0]) {
        const char *fname = fl_filename_name(doc->filePath());
        if (fname && fname[0]) {
            WCHAR fileW[FL_PATH_MAX];
            MultiByteToWideChar(CP_UTF8, 0, fname, -1, fileW, FL_PATH_MAX);
            pfd->SetFileName(fileW);
        }
    }

    // Encoding dropdown, like Notepad's Save As. The list is filtered by
    // runtime capability (GB18030 only appears when CP54936 works).
    static const DWORD kEncodingComboId = 1001;
    std::vector<Encoding> encodings = availableEncodings();
    IFileDialogCustomize *customize = nullptr;
    if (SUCCEEDED(pfd->QueryInterface(IID_PPV_ARGS(&customize))) && customize) {
        const std::wstring label = widen(I18n::get("filedlg.encoding"));
        customize->AddComboBox(kEncodingComboId);
        customize->SetControlLabel(kEncodingComboId, label.c_str());
        DWORD selected = 0;
        for (size_t i = 0; i < encodings.size(); ++i) {
            const std::wstring item = widen(encodingDisplayName(encodings[i]));
            customize->AddControlItem(kEncodingComboId, (DWORD)i, item.c_str());
            if (encodings[i] == doc->encoding()) selected = (DWORD)i;
        }
        customize->SetSelectedControlItem(kEncodingComboId, selected);
    }

    hr = pfd->Show(nullptr);
    if (FAILED(hr)) {
        if (customize) customize->Release();
        pfd->Release();
        return false;
    }

    // Read the encoding choice while the custom-control interface is alive.
    size_t chosenIdx = 0;
    if (customize) {
        DWORD sel = 0;
        if (SUCCEEDED(customize->GetSelectedControlItem(kEncodingComboId, &sel)) &&
            sel < encodings.size())
            chosenIdx = sel;
        customize->Release();
        customize = nullptr;
    }
    Encoding chosenEnc = encodings.empty() ? Encoding::UTF8 : encodings[chosenIdx];

    IShellItem *psiResult = nullptr;
    hr = pfd->GetResult(&psiResult);
    if (FAILED(hr) || !psiResult) { pfd->Release(); return false; }

    PWSTR pszPath = nullptr;
    bool ok = false;
    hr = psiResult->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
    if (SUCCEEDED(hr) && pszPath) {
        char chosen[FL_PATH_MAX];
        WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, chosen, FL_PATH_MAX, nullptr, nullptr);
        CoTaskMemFree(pszPath);

        // Auto-append the extension of the selected file type when the
        // user typed a bare name (no extension), e.g. "abc" + "*.txt"
        // becomes "abc.txt". The common file dialog does not do this by
        // default unless the filter shows "*.txt" and the user includes
        // it, so we fix it up here.
        if (!fl_filename_ext(chosen)[0] && strcmp(chosen, ".") != 0) {
            UINT selIdx = 1;
            pfd->GetFileTypeIndex(&selIdx);
            const wchar_t *extW = nullptr;
            switch (selIdx) {
                case 2:  extW = L".md";  break;
                default: extW = L".txt"; break;  // type 1 (txt)
            }
            // Type 3 is "*.*": never append there, or a deliberate
            // extensionless name would silently become "name.txt".
            if (extW && (selIdx <= 2)) {
                size_t len = strlen(chosen);
                if (len + 5 < FL_PATH_MAX) {
                    // extW already includes the leading dot (".txt"); do NOT
                    // prepend another "." or the result is "z..txt".
                    WideCharToMultiByte(CP_UTF8, 0, extW, -1,
                                        chosen + strlen(chosen), FL_PATH_MAX - (int)len - 1,
                                        nullptr, nullptr);
                }
            }
        }

        if (!resolveLossyEncoding(doc, chosenEnc)) {
            psiResult->Release();
            pfd->Release();
            return false;
        }
        doc->setEncoding(chosenEnc);
        ok = doc->saveFile(chosen);
        if (ok) {
            char dirBuf[FL_PATH_MAX];
            fl_strlcpy(dirBuf, chosen, FL_PATH_MAX);
            const char *p = fl_filename_name(dirBuf);
            if (p) { dirBuf[p - dirBuf] = 0; m_cfg->setLastDir(dirBuf); }
            m_cfg->recentAdd(chosen);
            notifyRecentDocs(chosen);
            refreshRecentMenu();
            updateTabLabel(tabIndex);
            updateTitle();
            updateStatusBar();
        }
    }
    psiResult->Release();
    pfd->Release();
    return ok;
}

// --------------------------------------------------------------------------
// Zoom helpers
// --------------------------------------------------------------------------

int MainWindow::fontSize() const {
    if (m_tabsList.empty()) return 13;
    return m_tabsList[0].editor->textsize();
}

