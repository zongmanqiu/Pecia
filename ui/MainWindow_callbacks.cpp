// MainWindow_callbacks.cpp - MainWindow menu callbacks (File/Edit/View/
// Settings/Help menu handlers, test dialogs, modify/close callbacks).
// Moved here from MainWindow.cpp so the main window file stays
// manageable. Member functions defined in this translation unit are part
// of the same MainWindow class declared in ui/MainWindow.h.
#include "ui/MainWindow.h"

#include "core/I18n.h"
#include "core/Config.h"
#include "LuaTool/AiPushServer.h"
#include "LuaTool/LuaPipeServer.h"
#include "PipeProtocol.h"

#include "editor/Document.h"
#include "editor/Editor.h"
#include "ui/FindReplace.h"
#include "ui/GoTo.h"
#include "ui/SettingsDialog.h"
#include "ui/ShortcutDialog.h"
#include "ui/ConfirmDialog.h"
#include "ui/InfoWindow.h"
#include "ui/AboutDialog.h"
#include "ui/ParamDialog.h"
#include "core/Updater.h"
#include <thread>
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Progress.H>
#include <cstdlib>

#include "core/Config.h"
#include "core/Theme.h"
#include "core/I18n.h"
#include "core/UiBridge.h"
#include "core/OpLog.h"
#include "script/LuaParamParser.h"
#include "script/ScriptManager.h"
#include "ui/Layout.h"
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/fl_ask.H>
#include <FL/filename.H>
#include <FL/fl_string_functions.h>
#include <FL/fl_draw.H>
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <string>
#include <vector>
#include <cctype>

// UTF-8 -> UTF-16（宽字符 Win32 API 入参；中文路径/URL 下 ANSI 版失效）
static std::wstring widen(const std::string &utf8) {
    if (utf8.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], n);
    while (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}


//   - In multi-tab mode: creates a new tab, leaving existing ones alone.
void MainWindow::cbNew(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (self) self->newFile();
}

// File > New Window - launch a fresh, independent Pecia process so the
// user can work on a separate set of files without disturbing the
// current window's tabs. We pass the current window position via
// PECIA_POS environment variable so the new window cascades.
void MainWindow::cbNewWindow(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
#if defined(_WIN32)
    wchar_t exePathW[MAX_PATH] = L"";
    if (GetModuleFileNameW(nullptr, exePathW, MAX_PATH) > 0) {
        char posValue[128];
        int x = self ? self->x() : 0;
        int y = self ? self->y() : 0;
        snprintf(posValue, sizeof(posValue), "%d,%d", x, y);
        SetEnvironmentVariableA("PECIA_POS", posValue);
        ShellExecuteW(nullptr, L"open", exePathW, nullptr, nullptr, SW_SHOWNORMAL);
    }
#else
    // Re-exec ourselves with PECIA_POS environment variable
    extern char **__argv;
    char posEnv[128];
    int x = self ? self->x() : 0;
    int y = self ? self->y() : 0;
    snprintf(posEnv, sizeof(posEnv), "PECIA_POS=%d,%d", x, y);
    putenv(posEnv);
    char *argv[] = { __argv[0], nullptr };
    execv(__argv[0], argv);
#endif
}

void MainWindow::cbOpen(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self) return;

    // Get starting directory
    char lastDirA[FL_PATH_MAX];
    self->m_cfg->getLastDir(lastDirA, sizeof(lastDirA), ".");
    if (!lastDirA[0]) { lastDirA[0] = '.'; lastDirA[1] = 0; }
    WCHAR lastDirW[FL_PATH_MAX];
    MultiByteToWideChar(CP_UTF8, 0, lastDirA, -1, lastDirW, FL_PATH_MAX);

    IFileOpenDialog *pfd = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IFileOpenDialog, (void**)&pfd);
    if (FAILED(hr) || !pfd) return;

    COMDLG_FILTERSPEC fileTypes[] = {
        { L"所有文件 (*)",               L"*.*" },
        { L"文本文件 (*.txt)",           L"*.txt" },
        { L"Markdown (*.md)",             L"*.md" },
        { L"日志文件 (*.log)",            L"*.log" },
        { L"配置和脚本 (*.ini; *.cfg; *.bat)", L"*.ini;*.cfg;*.bat;*.ps1" },
        { L"数据文件 (*.csv; *.json; *.xml)",  L"*.csv;*.json;*.xml" },
    };
    pfd->SetFileTypes(ARRAYSIZE(fileTypes), fileTypes);
    pfd->SetFileTypeIndex(1);   // 默认：所有文件 (*)

    IShellItem *psiFolder = nullptr;
    hr = SHCreateItemFromParsingName(lastDirW, nullptr, IID_IShellItem, (void**)&psiFolder);
    if (SUCCEEDED(hr) && psiFolder) {
        pfd->SetFolder(psiFolder);
        psiFolder->Release();
    }

    hr = pfd->Show(nullptr);
    if (FAILED(hr)) {
        pfd->Release();
        self->scheduleTrimSoon();   // 对话框关闭：回收其加载的 DLL 工作集
        return;
    }

    IShellItem *psiResult = nullptr;
    hr = pfd->GetResult(&psiResult);
    if (FAILED(hr) || !psiResult) { pfd->Release(); self->scheduleTrimSoon(); return; }

    PWSTR pszPath = nullptr;
    hr = psiResult->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
    if (SUCCEEDED(hr) && pszPath) {
        char chosen[FL_PATH_MAX];
        WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, chosen, FL_PATH_MAX, nullptr, nullptr);
        CoTaskMemFree(pszPath);

        char dirBuf[FL_PATH_MAX];
        fl_strlcpy(dirBuf, chosen, FL_PATH_MAX);
        const char *p = fl_filename_name(dirBuf);
        if (p) { dirBuf[p - dirBuf] = 0; self->m_cfg->setLastDir(dirBuf); }
        self->openFile(chosen);
    }
    psiResult->Release();
    pfd->Release();
    self->scheduleTrimSoon();   // 对话框关闭：回收其加载的 DLL 工作集
}

void MainWindow::cbSave(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (self) self->doSave(self->activeTabIndex());
    if (self) self->scheduleTrimSoon();
}

void MainWindow::cbSaveAs(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (self) self->doSaveAs(self->activeTabIndex());
    if (self) self->scheduleTrimSoon();
}

void MainWindow::cbClose(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (self) self->closeCurrentTab();
}

void MainWindow::cbExit(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self) return;
    // Close all tabs (prompting for each unsaved one). When only one tab
    // is left, closeCurrentTab() hides the window instead of deleting the
    // tab, so loop only while more than one tab remains, then close the
    // last tab explicitly (it hides the window) and hide_all_windows()
    // as a fallback. The old `while (!m_tabsList.empty())` loop never
    // terminated because the final closeCurrentTab() keeps the list
    // non-empty - a main-thread infinite hang (fixed).
    while (self->m_tabsList.size() > 1) {
        if (!self->closeCurrentTab()) return;   // user cancelled
    }
    if (!self->m_tabsList.empty()) {
        if (!self->closeCurrentTab()) return;   // user cancelled (last tab)
    }
    Fl::hide_all_windows();
}

void MainWindow::cbUndo(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self || !self->activeTab() || self->activeTab()->doc->isReadOnly()) return;
    opLog("undo: begin len=%d", self->activeTab()->editor->buffer()->length());
    Fl_Text_Editor::kf_undo(0, self->activeTab()->editor);
    opLog("undo: end len=%d canUndo=%d",
          self->activeTab()->editor->buffer()->length(),
          self->activeTab()->editor->buffer()->can_undo());
}

void MainWindow::cbRedo(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self || !self->activeTab() || self->activeTab()->doc->isReadOnly()) return;
    Fl_Text_Editor::kf_redo(0, self->activeTab()->editor);
}

void MainWindow::cbCut(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self || !self->activeTab() || self->activeTab()->doc->isReadOnly()) return;
    Fl_Text_Editor::kf_cut(0, self->activeTab()->editor);
}

void MainWindow::cbCopy(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (self && self->activeTab()) Fl_Text_Editor::kf_copy(0, self->activeTab()->editor);
}

void MainWindow::cbPaste(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self || !self->activeTab() || self->activeTab()->doc->isReadOnly()) return;
    Fl_Text_Editor::kf_paste(0, self->activeTab()->editor);
}

void MainWindow::cbDelete(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self || !self->activeTab() || self->activeTab()->doc->isReadOnly()) return;
    Fl_Text_Buffer *buf = self->activeTab()->doc->buffer();
    int start, end;
    // With a selection: remove it. Without one: delete the character at
    // the cursor (same as the editor's native Delete binding, so the
    // Delete menu shortcut never swallows character deletion).
    if (buf->selection_position(&start, &end)) {
        buf->remove(start, end);
    } else {
        Fl_Text_Editor::kf_delete(0, self->activeTab()->editor);
    }
}

void MainWindow::cbSelectAll(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self || !self->activeTab()) return;
    Fl_Text_Buffer *buf = self->activeTab()->doc->buffer();
    buf->select(0, buf->length());
    self->activeTab()->editor->redraw();
}

// 取选中文本（trim 后返回）；无选中返回空
static std::string selected_trimmed(Fl_Text_Buffer *buf)
{
    if (!buf || !buf->selection_text()) return {};
    char *raw = buf->selection_text();
    std::string s = raw;
    ::free((void *)raw);   // selection_text() returns malloc'd memory
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// URL 编码（查询参数用）
static std::string url_encode(const std::string &s)
{
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += (char)c;
        else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

// 打开选中的 URL（http/https）；非 URL 无操作（已移除——Ctrl+Click 覆盖）
// （无）

// 按配置的搜索引擎 URL 模板搜索选中内容（%s = 选中内容）
void MainWindow::cbSearchSelection(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self || !self->activeTab()) return;
    std::string sel = selected_trimmed(self->activeTab()->doc->buffer());
    if (sel.empty()) return;
    char tmpl[512];
    self->m_cfg->getSearchEngineUrl(tmpl, sizeof(tmpl));
    std::string url = tmpl;
    std::string enc = url_encode(sel);
    size_t pos = url.find("%s");
    if (pos != std::string::npos)
        url.replace(pos, 2, enc);
    else
        url += enc;   // 模板不含 %s：追加到末尾
    ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// 选中内容直接发给 AI：自动确保 AI 窗口运行，通过推送管道把选中文本
// 原样送达，AI 窗口收到后自动发送（并解除主窗口选中，内容不删除）。
// 与“搜索选中内容”不同：不加工内容、不选搜索引擎——选中即消息。
void MainWindow::cbSendToAi(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self) return;
    Tab *tab = self->activeTab();
    if (!tab || !tab->doc) return;
    std::string sel = selected_trimmed(tab->doc->buffer());
    if (sel.empty()) return;
    self->sendToAiChat(sel, nullptr);
}

// 把文本推送给 AI 窗口（自动确保运行）；AI 窗口收到后作为用户消息
// 自动发送。菜单“发送给 AI”与 Lua 控制台的 SEND 管道请求共用。
bool MainWindow::sendToAiChat(const std::string &text, std::string *errMsg) {
    if (text.empty()) {
        if (errMsg) *errMsg = "empty text";
        return false;
    }
    opLog("sendtoai: %zu bytes", text.size());
    // AI 窗口通过 lua 管道连回主进程：先确保 server 存在。
    if (!m_luaPipe) m_luaPipe = new LuaPipeServer(this);
    // AI 窗口必须正在运行（未运行则启动，带管道名参数才能连回主进程）。
    ensureAiChatRunning();
    // 推送（无连接时排队，AI 窗口连上后送达）。
    if (!m_aiPush) m_aiPush = new AiPushServer();
    m_aiPush->push(text);
    opLog("sendtoai: pushed");
    return true;
}

bool MainWindow::ensureAiChatRunning() {
    HANDLE hMutex = CreateMutexA(nullptr, FALSE, MUTEX_AI_CHAT);
    bool already = (GetLastError() == ERROR_ALREADY_EXISTS);
    CloseHandle(hMutex);
    if (already) return true;

    // 启动 PeciaAIChat.exe（与当前 exe 同目录），带 lua 管道名与语言参数。
    // 宽字符路径：中文安装目录下 GetModuleFileNameA/CreateProcessA 失效。
    wchar_t exePathW[MAX_PATH];
    GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
    wchar_t *slashW = wcsrchr(exePathW, L'\\');
    if (!slashW) return false;
    wcscpy_s(slashW + 1, MAX_PATH - (slashW + 1 - exePathW), L"PeciaAIChat.exe");
    if (GetFileAttributesW(exePathW) == INVALID_FILE_ATTRIBUTES) return false;

    char langCode[16];
    m_cfg->getLang(langCode, sizeof(langCode), "en");
    std::wstring cmd = std::wstring(L"\"") + exePathW + L"\" \"" +
                       widen(m_luaPipe->pipeName()) + L"\" " + widen(langCode);

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

void MainWindow::cbFind(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self) return;
    if (self->m_findBar->visible()) {
        self->m_findBar->hide();
        self->layoutTabs();
    } else {
        self->showFindBar(false);
    }
}

void MainWindow::cbFindNext(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self) return;
    if (!self->m_findBar->visible()) self->showFindBar(false);
    self->m_findBar->findNext();
}

void MainWindow::cbFindPrev(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self) return;
    if (!self->m_findBar->visible()) self->showFindBar(false);
    self->m_findBar->findPrev();
}

void MainWindow::cbReplace(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self) return;
    if (self->m_findBar->visible()) {
        self->m_findBar->hide();
        self->layoutTabs();
    } else {
        self->showFindBar(true);
    }
}


void MainWindow::cbZoomIn(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (self) self->setFontSize(self->fontSize() + 1);
}

void MainWindow::cbZoomOut(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (self) self->setFontSize(self->fontSize() - 1);
}

void MainWindow::cbZoomReset(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (self) self->setFontSize(self->m_baseFontSize);
}

void MainWindow::cbToggleWrap(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self) return;
    bool on = !self->m_cfg->getWrap();
    for (auto &t : self->m_tabsList) {
        t.editor->wrap_mode(on ? Fl_Text_Display::WRAP_AT_BOUNDS
                                : Fl_Text_Display::WRAP_NONE, 0);
    }
    self->m_cfg->setWrap(on);
    if (self->m_wrapItem) { if (on) self->m_wrapItem->set(); else self->m_wrapItem->clear(); }
}

void MainWindow::cbToggleLineNumbers(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self) return;
    bool on = !self->m_cfg->getLineNumbers();
    if (!on) {
        for (auto &t : self->m_tabsList) {
            t.editor->linenumber_width(0);
            t.editor->redraw();
        }
    }
    self->m_cfg->setLineNumbers(on);
    if (self->m_lineNumbersItem) { if (on) self->m_lineNumbersItem->set(); else self->m_lineNumbersItem->clear(); }
    if (on) self->updateLinenumberWidth();
}

void MainWindow::refreshAll() {
    // 1. Re-read settings.ini for any external changes (e.g. another
    //    Pecia process changed theme/language, or the user hand-edited it).
    m_cfg->reloadFromDisk();

    // 2. Re-scan script/ folder and rebuild the script toolbar buttons.
    rebuildScriptBar();

    // 3. Reload the current language file.
    char lang[32] = "en";
    m_cfg->getLang(lang, sizeof(lang));
    I18n::load(lang);

    // 4. Reload the color theme and apply to all UI.
    m_theme.load(*m_cfg);
    applyThemeColors();

    // 5. Re-apply shortcuts (may have changed on disk).
    applyShortcuts();

    // 6. Update menu radio states (theme/language toggles).
    if (m_statusbarItem) {
        if (m_showStatusbar) m_statusbarItem->set();
        else m_statusbarItem->clear();
    }

    redraw();
}

void MainWindow::cbRefreshAll(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self) return;
    self->refreshAll();
}

// Open the exe-adjacent script/ folder in Explorer so the user can add,
// edit, or delete their Lua scripts directly. Creates the folder on first
// use so the entry point always works.
void MainWindow::cbOpenScriptFolder(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self) return;

    ScriptCatalog cat = scriptManagerScan();
    std::string dir = cat.rootPath;
    if (dir.empty()) {
        // No script/ folder yet: create it next to the exe.
        // 宽字符路径：中文安装目录下 GetModuleFileNameA 会得到乱码。
        wchar_t exePathW[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            for (DWORD i = n; i > 0; --i) {
                if (exePathW[i - 1] == L'\\') { exePathW[i - 1] = 0; break; }
            }
            std::wstring dirW(exePathW);
            dirW += L"\\script";
            CreateDirectoryW(dirW.c_str(), nullptr);
            int u8n = WideCharToMultiByte(CP_UTF8, 0, dirW.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (u8n > 0) {
                dir.resize(u8n - 1);
                WideCharToMultiByte(CP_UTF8, 0, dirW.c_str(), -1, &dir[0], u8n, nullptr, nullptr);
            }
        }
    }
    if (!dir.empty()) {
        ShellExecuteW(nullptr, L"open", widen(dir).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void MainWindow::cbSettings(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self) return;

    // Remember the old Multi Tab state so applySettings() can detect a
    // transition from on -> off and run the detach flow. If the detach
    // fails (user cancels a Save As prompt), we revert the config.
    bool oldMultiTab = self->m_cfg->getMultiTab();

    int dlgPad = self->m_cfg->getDialogPad();
    SettingsDialog dlg(480, 260 + 2 * dlgPad, I18n::get("settings.title"), &self->m_theme,
                       self->m_cfg->getUiFontSize(), dlgPad);
    dlg.loadFrom(*self->m_cfg);
    if (!dlg.runModal()) return;       // user cancelled

    bool changed = dlg.saveTo(*self->m_cfg);
    if (!changed) return;

    // Multi Tab off -> on, or on -> off: the tab layout has to be
    // rebuilt. Turning OFF with multiple tabs open detaches the extra
    // tabs into independent windows; if that fails we revert.
    bool newMultiTab = self->m_cfg->getMultiTab();
    if (!newMultiTab && oldMultiTab) {
        // Persist multi_tab=false BEFORE spawning the detached windows,
        // otherwise the new processes would read the still-true value
        // and their later saveSettings() could overwrite our false.
        self->m_cfg->setMultiTab(false);
        if (!self->detachExtraTabsToNewWindows()) {
            // User cancelled a Save As during detach - revert.
            self->m_cfg->setMultiTab(true);
            // Still apply the rest of the settings (tab width, etc.).
            self->applySettings(*self->m_cfg);
            return;
        }
    }

    self->applySettings(*self->m_cfg);
}

// Style an Fl_File_Chooser to match the application's look.
// We customize:
//   - file list background (soft white) and text font/size
//   - icon size (slightly larger than default)
//   - OK button label is set by the caller (打开/保存)
// Fl_File_Chooser picks up the current Fl::scheme() automatically
// so its buttons/scrollbars already match the rest of the app.



void MainWindow::applySettings(Config &cfg) {
    int fontId, fontSize;
    cfg.getFont(fontId, fontSize);
    // Use the UI font size for the editor text so everything is consistent.
    // The font face (fontId) still comes from getFont().
    fontSize = cfg.getUiFontSize();
    bool ln = cfg.getLineNumbers();
    bool wrap = cfg.getWrap();

    for (auto &t : m_tabsList) {
        t.editor->textfont(fontId);
        t.editor->textsize(fontSize);
        t.editor->linenumber_size(fontSize);
        t.editor->linenumber_width(0);  // will be set by updateLinenumberWidth()
        t.editor->wrap_mode(wrap ? Fl_Text_Display::WRAP_AT_BOUNDS
                                  : Fl_Text_Display::WRAP_NONE, 0);
        t.editor->buffer()->tab_distance(cfg.getTabWidth());
        t.editor->setLongLineMarker(cfg.getLongLineMarker());
        t.doc->setTrimTrailing(cfg.getTrimTrailingWhitespace());
        t.doc->setSaveFormat(cfg.getTrimLeadingBlank(),
                             cfg.getTrimEndingBlank(),
                             cfg.getExpandTabsOnSave(),
                             cfg.getTabWidth());
        t.editor->redraw();
    }
    if (ln) updateLinenumberWidth();

    // Apply theme.
    char scheme[32];
    cfg.getScheme(scheme, sizeof(scheme), "gtk+");
    applyScheme(scheme);

    // Sync the View menu toggle items with the persisted state.
    // Use cached pointers (saved in constructor) so this still works
    // after the menu has been translated to Chinese.
    if (m_wrapItem) {
        if (wrap) m_wrapItem->set(); else m_wrapItem->clear();
    }
    if (m_lineNumbersItem) {
        if (ln) m_lineNumbersItem->set(); else m_lineNumbersItem->clear();
    }

    // Re-apply window corner style (the user may have toggled Round
    // Rebuild the tab strip layout (the user may have toggled Multi Tab
    // in the Settings dialog; even when unchanged, this is cheap and
    // makes sure the editor area matches the current status bar /
    // single/multi-tab mode).
    layoutTabs();
    syncTitleBar();

    // Refresh auto-save timer (interval may have changed).
    scheduleAutoSave();

    redraw();
}

void MainWindow::cbAbout(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    showAboutDialog(self ? &self->m_theme : nullptr,
                    self ? self->m_cfg->getUiFontSize() : 14);
}

// Help > Check for Updates.
namespace {

// Modeless "downloading" window. Without it the UI looks frozen for the
// several seconds it takes to pull the ~7 MB zip on the worker thread.
// It is removed by whoever finishes the download (success or failure) and
// ignores the close button so it cannot vanish mid-transfer.
class UpdateProgressWin : public Fl_Window {
public:
    UpdateProgressWin(const char *title, const char *label)
        : Fl_Window(380, 116, title) {
        begin();
        m_text = new Fl_Box(12, 14, 356, 26, label);
        m_text->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        m_bar = new Fl_Progress(12, 50, 356, 22);
        m_bar->minimum(0.0);
        m_bar->maximum(1.0);
        m_bar->value(0.0);
        m_bar->selection_color(fl_rgb_color(0, 110, 220));
        m_bar->color(FL_BACKGROUND2_COLOR);
        end();
        // Swallow close attempts: only the download decides when this ends.
        callback([](Fl_Widget *, void *) {}, nullptr);
        set_modal();
    }

    // frac < 0 => total size unknown (indeterminate).
    void setProgress(double frac, const std::string &text) {
        m_buf = text;
        m_text->label(m_buf.c_str());
        m_bar->value(static_cast<float>(frac < 0 ? 0.5 : frac));
        redraw();
    }

private:
    Fl_Box      *m_text  = nullptr;
    Fl_Progress *m_bar   = nullptr;
    std::string  m_buf;   // owns the bytes behind m_text->label()
};

// Byte count as "3.2 MB" / "812 KB" for the progress label.
std::string formatBytes(long long n) {
    char buf[64];
    if (n >= 1024 * 1024)      snprintf(buf, sizeof(buf), "%.1f MB", (double)n / 1048576.0);
    else if (n >= 1024)        snprintf(buf, sizeof(buf), "%.0f KB", (double)n / 1024.0);
    else                       snprintf(buf, sizeof(buf), "%lld B", n);
    return buf;
}

struct UpdateResult {
    UpdateInfo   info;
    MainWindow  *self  = nullptr;
    const Theme *theme = nullptr;
    int          fs    = 14;
};

// Marshalled progress report (worker thread -> UI thread via Fl::awake).
struct UpdateProgressMsg {
    UpdateProgressWin *win;
    long long          received;
    long long          total;
};

// Marshalled failure: message + the theme/font needed to draw it.
struct UpdateFailMsg {
    std::string   msg;
    const Theme  *theme;
    int           fs;
    UpdateProgressWin *win;
};

// Marshalled launch request (UI thread).
struct UpdateLaunchMsg {
    std::string        bat;
    UpdateProgressWin *win;
};

void startUpdate(const UpdateInfo &info, const Theme *theme, int fs);

// Runs on the FLTK UI thread (via Fl::awake). Shows the outcome of the check.
// Shows the outcome of the check in Pecia's themed ConfirmDialog (the
// same window Lua errors use), so the look is consistent whether or not
// a new version was found.
void handleCheckResult(const UpdateResult &r) {
    if (!r.info.error.empty()) {
        char msg[512];
        snprintf(msg, sizeof(msg), I18n::get("update.error"), r.info.error.c_str());
        // single "OK" button, mirroring the Lua error dialog.
        ConfirmDialog(I18n::get("update.title"), msg,
                      I18n::get("settings.ok"), nullptr, nullptr,
                      r.theme, r.fs).run();
        return;
    }
    if (!r.info.hasUpdate) {
        ConfirmDialog(I18n::get("update.title"), I18n::get("update.latest"),
                      I18n::get("settings.ok"), nullptr, nullptr,
                      r.theme, r.fs).run();
        return;
    }
    std::string notes = r.info.notes;
    if (notes.size() > 800) { notes.resize(800); notes += "\n..."; }
    char msg[2048];
    snprintf(msg, sizeof(msg), I18n::get("update.newAvailable"),
             r.info.latest.c_str(), notes.c_str());
    ConfirmDialog dlg(I18n::get("update.title"), msg,
                      I18n::get("update.update"), I18n::get("update.later"), nullptr,
                      r.theme, r.fs);
    if (dlg.run() != 0) return;
    // Updating exits this process, so never lose work silently: give the
    // user the chance to save (or cancel) every dirty document first.
    if (r.self && !r.self->confirmUnsavedBeforeUpdate()) return;
    startUpdate(r.info, r.theme, r.fs);
}

// Download + write the updater bat on a worker thread, then hand off to the
// UI thread to launch it and exit the current process.
void startUpdate(const UpdateInfo &info, const Theme *theme, int fs) {
    // Called on the UI thread: safe to create/show the progress window here.
    auto *win = new UpdateProgressWin(I18n::get("update.title"),
                                      I18n::get("update.downloading"));
    win->position((Fl::w() - win->w()) / 2, (Fl::h() - win->h()) / 2);
    win->show();

    struct Ctx {
        UpdateInfo        info;
        const Theme      *theme;
        int               fs;
        UpdateProgressWin *win;
    };
    auto *ctx = new Ctx{info, theme, fs, win};

    std::thread([ctx]() {
        // Report a failure in Pecia's own themed dialog (not fl_message).
        auto fail = [ctx](const char *key) {
            auto *f = new UpdateFailMsg{I18n::get(key), ctx->theme, ctx->fs, ctx->win};
            Fl::awake([](void *d) {
                auto *ff = static_cast<UpdateFailMsg *>(d);
                if (ff->win) { ff->win->hide(); delete ff->win; }
                ConfirmDialog(I18n::get("update.title"), ff->msg.c_str(),
                              I18n::get("settings.ok"), nullptr, nullptr,
                              ff->theme, ff->fs).run();
                delete ff;
            }, f);
        };

        std::string exeDir = getExeDirUtf8();
        std::string base = ctx->info.latest;
        if (!base.empty() && (base[0] == 'v' || base[0] == 'V')) base = base.substr(1);
        std::string zipPath = exeDir + "Pecia_x64_" + base + ".zip";

        auto progress = [ctx](long long received, long long total) {
            auto *p = new UpdateProgressMsg{ctx->win, received, total};
            Fl::awake([](void *d) {
                auto *pp = static_cast<UpdateProgressMsg *>(d);
                std::string t;
                if (pp->total > 0) {
                    char buf[160];
                    snprintf(buf, sizeof(buf), "%s / %s  (%.0f%%)",
                             formatBytes(pp->received).c_str(),
                             formatBytes(pp->total).c_str(),
                             100.0 * (double)pp->received / (double)pp->total);
                    t = buf;
                } else {
                    t = std::string(I18n::get("update.downloading")) +
                        "  " + formatBytes(pp->received);
                }
                if (pp->win)
                    pp->win->setProgress(
                        pp->total > 0 ? (double)pp->received / (double)pp->total : -1.0, t);
                delete pp;
            }, p);
        };

        if (!downloadFile(ctx->info.url, zipPath, progress)) { fail("update.dlfail"); delete ctx; return; }

        std::string batPath = exeDir + "pecia_update.bat";
        if (!writeUpdateBat(exeDir, zipPath, batPath)) { fail("update.batfail"); delete ctx; return; }

        auto *l = new UpdateLaunchMsg{batPath, ctx->win};
        Fl::awake([](void *d) {
            auto *ll = static_cast<UpdateLaunchMsg *>(d);
            if (ll->win) { ll->win->hide(); delete ll->win; }
            std::string bat = ll->bat;
            delete ll;
            launchUpdater(bat);
            exit(0);
        }, l);
        delete ctx;
    }).detach();
}
} // namespace

void MainWindow::cbCheckUpdates(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    const Theme *theme = self ? &self->m_theme : nullptr;
    int fs = self ? self->m_cfg->getUiFontSize() : 14;

    // Network check on a worker thread so the UI never blocks.
    std::thread([self, theme, fs]() {
        UpdateInfo info = checkForUpdate(PECIA_VERSION);
        auto *r = new UpdateResult{info, self, theme, fs};
        Fl::awake([](void *d) {
            auto *res = static_cast<UpdateResult *>(d);
            handleCheckResult(*res);
            delete res;
        }, r);
    }).detach();
}

// Settings > Shortcuts - opens the shortcut customization dialog.
// On OK the dialog has already written every row to Config; here we
// rebuild the menu labels and the dispatch registry so the change
// takes effect immediately (other Pecia processes pick it up via their
// settings.ini watchers).
void MainWindow::cbShortcuts(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self) return;

    ShortcutDialog dlg(520, 560, I18n::get("shortcut.dialog.title"),
                       self->m_cfg, &self->m_theme,
                       self->m_cfg->getUiFontSize());
    dlg.setRows(self->buildShortcutRows());
    if (!dlg.runModal()) return;   // cancelled
    self->applyShortcuts();
    // Rebuild the script bar so script shortcut labels (added / cleared)
    // show up in the toolbar popups immediately.
    self->rebuildScriptBar();
}

// 工具窗口（独立进程）——通过 Tools 菜单触发（cbAIChat/cbLuaScript）。

void MainWindow::cbModify(int pos, int nInserted, int nDeleted,
                          int nRestyled, const char *deletedText,
                          void *cbArg) {
    MainWindow *self = static_cast<MainWindow *>(cbArg);
    if (!self) return;

    // 在 Shift+Tab 循环期间跳过所有回调——循环结束后会统一刷新
    Tab *t = self->activeTab();
    if (t && t->editor->isShiftTab()) return;

    if (nInserted || nDeleted) {
        // 先手动执行 Document 的 dirty 判定：FLTK 的 modify 回调按
        // “后注册先执行”，cbModify 先于 Document::modifyCallback 运行，
        // 第一个字符时 dirty 还是 0——updateTitle 会错过未保存标记。
        // （Document 的回调随后仍会执行一次，setDirty 幂等，无副作用）
        Tab *dt = self->activeTab();
        if (dt && dt->doc) {
            Document::modifyCallback(pos, nInserted, nDeleted, nRestyled,
                                     deletedText, dt->doc);
        }
        // FLTK 1.4.5 的 count_lines()/line_start()/next_char() 在位置参数
        // 超过 buffer 长度时死循环(next_char 越界返回原位置,调用循环
        // 永不前进)。undo/redo 时本回调在 Fl_Text_Display 更新光标位置
        // 之前被触发,insert_position() 可能陈旧于新长度——先把光标
        // clamp 到 [0, length],保护所有后续位置计算(状态栏/行高亮/绘制)。
        Tab *at = self->activeTab();
        if (at && at->editor && at->editor->buffer()) {
            int blen = at->editor->buffer()->length();
            int bpos = at->editor->insert_position();
            if (bpos > blen) {
                at->editor->insert_position(blen);
                at->editor->show_insert_position();
            }
        }
        // Update the active tab label and window title
        int i = self->activeTabIndex();
        if (i >= 0) self->updateTabLabel(i);
        self->updateTitle();
        self->updateStatusBar();
        // Reset the blink phase so the cursor is immediately visible
        // after the user types something - matches the behavior of
        // virtually every other editor.
        self->m_cursorVisible = true;
        Tab *t2 = self->activeTab();
        if (t2) {
            t2->editor->show_cursor(1);
            t2->editor->damage(FL_DAMAGE_CHILD);
            // Markdown 预览：内容变化由固定间隔循环定时器检测（此处无需干预）
            // Sync line highlight style buffer with text buffer changes
            if (self->m_cfg->getHighlightCurrentLine()) {
                t2->editor->syncLineHighlightBuffer();
            } else {
                // While line highlighting is off, the style buffer still
                // exists (created on-demand) — refresh URL highlights.
                t2->editor->refreshUrlHighlight();
            }
        }
        // Defer line number width update so it doesn't run on every
        // keystroke - the width only needs to change when the number of
        // digits in the line count changes (e.g. 9→10, 99→100, etc.)
        // Use a named callback (with dedupe) so the destructor can cancel
        // it - an anonymous lambda armed with add_timeout could otherwise
        // fire on a destroyed MainWindow (use-after-free).
        Fl::remove_timeout(s_updateLinenumberWidthCb, self);
        Fl::add_timeout(0.0, s_updateLinenumberWidthCb, self);
    }
}

// Window close (X button) callback - distinct from cbClose (Ctrl+W)
// which only closes the current tab.
//
// NOTE: do NOT loop closeCurrentTab() here - it would re-create a fresh
// tab when the list becomes empty (see closeCurrentTab), causing an
// infinite loop and unbounded memory growth. Instead, iterate over the
// tabs once to prompt for unsaved changes, then hide the window - the
// destructor cleans up the actual tab storage.
void MainWindow::cbWindowClose(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self) return;

    // Walk through all tabs. For each dirty one, switch to it so the
    // user sees what they're being asked about, then prompt.
    for (int i = 0; i < (int)self->m_tabsList.size(); ++i) {
        Document *doc = self->m_tabsList[i].doc;
        if (!doc->isDirty()) continue;
        self->switchToTab(i);
        int r = ConfirmDialog(I18n::get("confirm.unsaved.title"),
            I18n::get("confirm.unsaved.msg"),
            I18n::get("dlg.save"), I18n::get("dlg.dontsave"), I18n::get("dlg.cancel"),
            &self->m_theme, self->m_cfg->getUiFontSize()).run();
        if (r == 2) return;                       // Cancel -> abort exit
        if (r == 0) {
            if (!self->doSave(i)) return;          // Save failed/cancelled
        }
        // r == 2 -> Don't Save, continue
    }
    Fl::hide_all_windows();
}

// Fl_Tabs callback - fires when the user clicks a tab to switch to it.
// Also handles middle-click to close the tab in multi-tab mode.
//
// Note: the per-tab close button (✕) is handled by cbTabPageClose(),
// which is set as the callback on each page widget with FL_WHEN_CLOSED.
void MainWindow::cbTabClose(Fl_Widget *w, void *data) {
    Fl_Tabs *tabs = (Fl_Tabs *)w;
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self || !tabs) return;

    // Middle-click on a tab closes it.
    if (Fl::event_button() == FL_MIDDLE_MOUSE) {
        self->closeCurrentTab();
        return;
    }

    // Normal tab switch: find the newly-selected tab and update UI state.
    Fl_Widget *cur = tabs->value();
    for (size_t i = 0; i < self->m_tabsList.size(); ++i) {
        if (self->m_tabsList[i].page == cur) {
            self->switchToTab((int)i);
            break;
        }
    }
}

// Callback for individual tab pages - fires when the built-in close
// button (FL_WHEN_CLOSED) is clicked.
void MainWindow::cbTabPageClose(Fl_Widget *w, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self) return;

    if (Fl::callback_reason() == FL_REASON_CLOSED) {
        // Find the tab index for this page, then defer to
        // closeCurrentTab() so the save-prompt and single-tab-closes-
        // window rules are applied consistently across all close paths
        // (Ctrl+W, middle-click, and the tab ✕ button).
        Fl_Widget *page = w;
        for (size_t i = 0; i < self->m_tabsList.size(); ++i) {
            if (self->m_tabsList[i].page == page) {
                self->switchToTab((int)i);
                self->closeCurrentTab();
                return;
            }
        }
    }
}
