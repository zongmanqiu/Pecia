// MainWindow_lua.cpp - MainWindow Lua scripting integration
// (Tools > Lua Script... dialog and the script runner). Moved here from
// MainWindow.cpp so the main window file stays manageable. Member
// functions defined in this translation unit are part of the same
// MainWindow class declared in ui/MainWindow.h.
#include "ui/MainWindow.h"

#include "editor/Document.h"
#include "editor/Editor.h"
#include "LuaTool/LuaPipeServer.h"
#include "script/LuaEngine.h"
#include "script/LuaParamParser.h"
#include "ui/ParamDialog.h"
#include "ui/ConfirmDialog.h"
#include "script/ScriptManager.h"
#include "core/Config.h"
#include "core/Theme.h"
#include "core/I18n.h"
#include "core/OpLog.h"
#include <FL/fl_ask.H>
#include <FL/fl_utf8.h>
#include <FL/fl_string_functions.h>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl.H>
#include <windows.h>
#include <stdio.h>
#include "core/PipeProtocol.h"
#include <memory>
#include <string.h>
#include <string>


// Calculate the appropriate line number column width for each editor
// ---------------------------------------------------------------------------
// Tool launcher: single-instance per tool. If the tool is already running
// (mutex), bring its window to the front instead of launching a second one.
// ---------------------------------------------------------------------------

namespace {

bool activateByTitle(const char *title) {
    HWND w = FindWindowA(nullptr, title);
    if (w) {
        ShowWindow(w, SW_RESTORE);
        SetForegroundWindow(w);
        return true;
    }
    return false;
}

bool launchTool(const char *exeName, const char *mutexName,
                const char *windowTitle, const char *pipeName, const char *langCode) {
    // Transient single-instance check: release the handle immediately so
    // the TOOL process owns the mutex (otherwise the tool's own check
    // would see our handle and wrongly exit as a duplicate).
    HANDLE hMutex = CreateMutexA(nullptr, FALSE, mutexName);
    bool alreadyRunning = (GetLastError() == ERROR_ALREADY_EXISTS);
    CloseHandle(hMutex);
    if (alreadyRunning) {
        if (activateByTitle(windowTitle)) return true;
        Sleep(300);                       // tool may still be starting up
        activateByTitle(windowTitle);
        return true;
    }

    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    char *slash = strrchr(exePath, '\\');
    if (slash) *slash = 0;
    char toolPath[MAX_PATH + 32];
    snprintf(toolPath, sizeof(toolPath), "%s\\%s", exePath, exeName);

    if (GetFileAttributesA(toolPath) == INVALID_FILE_ATTRIBUTES) {
        fl_message_title("Pecia");
        fl_alert("未找到 %s\n请确认构建产物完整。", toolPath);
        return false;
    }

    char langBuf[16] = "en";
    if (langCode && *langCode) fl_strlcpy(langBuf, langCode, sizeof(langBuf));
    std::string cmd = std::string("\"") + toolPath + "\" \"" + pipeName +
                      "\" \"" + langBuf + "\"";
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessA(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0,
                        nullptr, exePath, &si, &pi)) {
        fl_message_title("Pecia");
        fl_alert("无法启动 %s：%lu", exeName, (unsigned long)GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

} // namespace

void MainWindow::cbLuaScript(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (self) self->showLuaDialog();
}

void MainWindow::showLuaDialog() {
    if (!m_luaEngine) m_luaEngine = new LuaEngine();
    if (!m_luaPipe) m_luaPipe = new LuaPipeServer(this);
    char langCode[16];
    m_cfg->getLang(langCode, sizeof(langCode), "en");
    launchTool("PeciaLua.exe", MUTEX_LUA_TOOL, I18n::get("lua.title"),
               m_luaPipe->pipeName(), langCode);
}

void MainWindow::showAIChat() {
    if (!m_luaPipe) m_luaPipe = new LuaPipeServer(this);
    char langCode[16];
    m_cfg->getLang(langCode, sizeof(langCode), "en");
    launchTool("PeciaAIChat.exe", MUTEX_AI_CHAT, I18n::get("chat.title"),
               m_luaPipe->pipeName(), langCode);
}

void MainWindow::cbAIChat(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (self) self->showAIChat();
}

// Scripts toolbar callback - runs the selected .lua file. `w` is the
// toolbar HoverMenuBar (script item; parent is the MainWindow).
// `data` is the script's slash-separated relative path ("pair" or
// "Formatting/trim"), owned by the menu array.
//
// IMPORTANT: the actual script execution is DEFERRED via
// Fl::add_timeout(0.0, ...). This callback fires inside the menu's
// pulldown() nested event loop, while the menu window is still up. Modifying the document
// buffer there (buffer->text() -> cbModify -> redraws) corrupts FLTK's
// nested drawing state and crashes the app (0xC0000005) - the same
// failure mode the old Pair/No. toolbar buttons avoided by deferring.
// Running the script on the next event-loop tick, after the menu has
// fully closed, is safe.
struct DeferredRunScript {
    MainWindow *self;
    char       *relPath;   // strdup'd, freed by the timeout callback
};

static void runScriptDeferred(void *data) {
    std::unique_ptr<DeferredRunScript> d(static_cast<DeferredRunScript *>(data));
    if (!d || !d->self || !d->relPath) return;
    std::string rel(d->relPath);

    std::string path = scriptManagerPath(rel);
    if (path.empty()) return;

    FILE *fp = fl_fopen(path.c_str(), "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    std::string script;
    if (sz > 0) {
        script.resize((size_t)sz);
        if (fread(&script[0], 1, (size_t)sz, fp) != (size_t)sz)
            script.clear();
    }
    fclose(fp);
    if (script.empty()) return;

    std::string out, err;
    d->self->executeLuaScript(script, &out, &err);
}

void MainWindow::cbRunScript(Fl_Widget *w, void *data) {
    MainWindow *self = nullptr;
    if (Fl_Menu_Bar *mb = dynamic_cast<Fl_Menu_Bar *>(w)) {
        self = dynamic_cast<MainWindow *>(mb->parent());
    } else if (w) {
        self = static_cast<MainWindow *>(w->user_data());
    }
    if (!self) return;

    const char *rel = static_cast<const char *>(data);
    if (!rel || !*rel) return;

    auto *d = new DeferredRunScript();
    d->self = self;
    d->relPath = _strdup(rel);
    Fl::add_timeout(0.0, runScriptDeferred, d);
}

bool MainWindow::executeLuaScript(const std::string &script,
                                  std::string *output,
                                  std::string *errMsg) {
    Tab *t = activeTab();
    if (!t || !t->editor || !t->doc) {
        if (errMsg) *errMsg = I18n::get("lua.nodoc");
        return false;
    }
    if (t->doc->isReadOnly()) {
        if (errMsg) *errMsg = I18n::get("lua.readonly");
        return false;
    }
    if (!m_luaEngine) m_luaEngine = new LuaEngine();

    // Script-header parameter declarations: parse, prompt, then inject.
    // UI texts resolved for the current UI language (script carries its
    // own translations via --!pui lines).
    std::vector<std::pair<std::string, std::string>> paramValues;
    char langCode[16] = "en";
    m_cfg->getLang(langCode, sizeof(langCode), "en");
    LuaParamSet decls = luaParseParams(script, langCode);
    if (!decls.params.empty()) {
        const std::string dlgTitle = decls.title.empty()
            ? std::string(I18n::get("param.title"))
            : decls.title;
        ParamDialog dlg(dlgTitle.c_str(), decls.params, &m_theme,
                        m_cfg->getUiFontSize());
        if (!dlg.run()) {
            if (errMsg) *errMsg = I18n::get("lua.cancelled");
            return false;   // user cancelled - do not run
        }
        const std::vector<std::string> &vals = dlg.results();
        for (size_t i = 0; i < decls.params.size() && i < vals.size(); ++i)
            paramValues.push_back({decls.params[i].name, vals[i]});
    }

    Editor *ed = t->editor;
    Fl_Text_Buffer *buf = t->doc->buffer();
    LuaEditorHost host;
    host.buffer = buf;
    host.cursorPos = ed->insert_position();
    host.hasSelection = buf->selection_position(&host.selStart, &host.selEnd);

    bool ok = m_luaEngine->run(script, host, output, errMsg,
                               paramValues.empty() ? nullptr : &paramValues);

    // Apply the script's final cursor/selection to the editor.
    int len = buf->length();
    if (host.cursorPos < 0) host.cursorPos = 0;
    if (host.cursorPos > len) host.cursorPos = len;
    ed->insert_position(host.cursorPos);
    ed->show_insert_position();
    if (host.selStart < 0) host.selStart = 0;
    if (host.selEnd > len) host.selEnd = len;
    if (host.hasSelection && host.selEnd > host.selStart)
        buf->select(host.selStart, host.selEnd);
    else
        buf->unselect();

    // The single buffer replace already fired cbModify (title, dirty,
    // line highlight); refresh the cursor-dependent status bar once and
    // move the current-line highlight to the script's final cursor.
    ed->notifyCursorMoved();
    updateStatusBar();

    // Script failed: surface the RAW error message in our themed window
    // (script bar, pipe RUN requests, etc. all funnel through here).
    if (!ok) {
        const char *emsg = (errMsg && !errMsg->empty())
                               ? errMsg->c_str() : I18n::get("lua.error");
        ConfirmDialog(I18n::get("lua.error"), emsg,
                      I18n::get("settings.ok"), nullptr, nullptr,
                      &m_theme, m_cfg->getUiFontSize()).run();
    }
    return ok;
}

bool MainWindow::insertAiText(const std::string &text, std::string *errMsg) {
    Tab *t = activeTab();
    if (!t || !t->editor || !t->doc) {
        if (errMsg) *errMsg = I18n::get("lua.nodoc");
        return false;
    }
    if (t->doc->isReadOnly()) {
        if (errMsg) *errMsg = I18n::get("lua.readonly");
        return false;
    }
    if (text.empty()) return true;

    Fl_Text_Buffer *buf = t->doc->buffer();
    buf->undo_begin();   // PATCHED by Pecia: AI insertion = 1 undo step
    int start = 0, end = 0;
    int pos;
    if (buf->selection_position(&start, &end) && end > start) {
        buf->replace_selection(text.c_str());
        pos = start + (int)text.size();
    } else {
        pos = t->editor->insert_position();
        buf->insert(pos, text.c_str());
        pos += (int)text.size();
    }
    buf->undo_end();

    t->editor->insert_position(pos);
    t->editor->show_insert_position();
    t->editor->redraw();
    updateStatusBar();
    return true;
}

bool MainWindow::clearSelection(std::string *errMsg) {
    Tab *t = activeTab();
    if (!t || !t->editor || !t->doc) {
        if (errMsg) *errMsg = I18n::get("lua.nodoc");
        return false;
    }
    Fl_Text_Buffer *buf = t->doc->buffer();
    // selection_position() dereferences its out-pointers unconditionally
    // (FLTK writes *start/*end even when nothing is selected) - never
    // pass nullptr.
    int ss = 0, se = 0;
    if (buf && buf->selection_position(&ss, &se))
        buf->unselect();
    t->editor->redraw();
    return true;
}

bool MainWindow::getSelectionText(std::string *text, std::string *errMsg) {
    Tab *t = activeTab();
    if (!t || !t->editor || !t->doc) {
        if (errMsg) *errMsg = I18n::get("lua.nodoc");
        return false;
    }
    Fl_Text_Buffer *buf = t->doc->buffer();
    int start = 0, end = 0;
    if (!buf->selection_position(&start, &end) || end <= start) {
        if (errMsg) *errMsg = I18n::get("ai.nosel");
        return false;
    }
    char *sel = buf->text_range(start, end);
    if (!sel) {
        if (errMsg) *errMsg = I18n::get("ai.nosel");
        return false;
    }
    if (text) text->assign(sel);
    free(sel);
    return true;
}

bool MainWindow::getDocumentSnapshot(std::string *payload,
                                     std::string *errMsg) {
    Tab *t = activeTab();
    if (!t || !t->editor || !t->doc) {
        if (errMsg) *errMsg = I18n::get("lua.nodoc");
        return false;
    }
    if (t->doc->isReadOnly()) {
        if (errMsg) *errMsg = I18n::get("lua.readonly");
        return false;
    }
    Fl_Text_Buffer *buf = t->doc->buffer();
    char *txt = buf->text();
    if (!txt) {
        if (errMsg) *errMsg = I18n::get("lua.nodoc");
        return false;
    }
    int start = 0, end = 0;
    buf->selection_position(&start, &end);   // out-params only; return not needed
    int cursor = t->editor->insert_position();
    std::string p;
    p.reserve(64 + strlen(txt));
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "%d:%d:%d\n", cursor, start, end);
    p = hdr;
    p += txt;
    free(txt);
    if (payload) *payload = std::move(p);
    return true;
}

bool MainWindow::applyDocumentSnapshot(const std::string &payload,
                                       std::string *errMsg) {
    Tab *t = activeTab();
    if (!t || !t->editor || !t->doc) {
        if (errMsg) *errMsg = I18n::get("lua.nodoc");
        return false;
    }
    if (t->doc->isReadOnly()) {
        if (errMsg) *errMsg = I18n::get("lua.readonly");
        return false;
    }
    // Header line: cursor:selStart:selEnd\n, then the full document text.
    size_t nl = payload.find('\n');
    if (nl == std::string::npos) {
        if (errMsg) *errMsg = I18n::get("lua.error");
        return false;
    }
    int cursor = 0, selStart = 0, selEnd = 0;
    if (sscanf(payload.substr(0, nl).c_str(), "%d:%d:%d",
               &cursor, &selStart, &selEnd) != 3) {
        if (errMsg) *errMsg = I18n::get("lua.error");
        return false;
    }
    const std::string text = payload.substr(nl + 1);

    Fl_Text_Buffer *buf = t->doc->buffer();
    buf->text(text.c_str());   // one replacement, not undoable (as before)
    if (selStart >= 0 && selEnd >= selStart && selEnd <= (int)text.size())
        buf->select(selStart, selEnd);
    else
        buf->unselect();
    Editor *ed = t->editor;
    int len = buf->length();
    if (cursor < 0) cursor = 0;
    if (cursor > len) cursor = len;
    ed->insert_position(cursor);
    ed->show_insert_position();
    ed->notifyCursorMoved();
    updateStatusBar();
    return true;
}

// Language menu callback. Triggered when the user picks an entry from
// the Language submenu.
//
// FLTK limitation we have to work around: Fl_Menu_::copy(g_menu, this)
// walks every menu item and OVERWRITES user_data_ with `this` for any
// item that has a callback - regardless of what we put in the static
// g_menu table. So we can't stash the language code ("en" / "zh-CN")
// in user_data; after copy() runs, both Language entries' user_data
// point at the MainWindow, not at our string literals.
//
// Instead we identify the picked language by the menu item's label
// text. The two Language radio entries ("English" / "简体中文") are
// deliberately NOT registered in m_menuKeys, so applyLanguageToMenu()
// never touches their labels - they stay in their original form for