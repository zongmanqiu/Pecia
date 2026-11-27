// MainWindow_lua.cpp - MainWindow Lua scripting integration
// (Tools > Lua Script... dialog and the script runner). Moved here from
// MainWindow.cpp so the main window file stays manageable. Member
// functions defined in this translation unit are part of the same
// MainWindow class declared in ui/MainWindow.h.
#include "ui/MainWindow.h"

#include "editor/Document.h"
#include "editor/Editor.h"
#include "AIChat/AIChatPane.h"
#include "ui/InfoWindow.h"
#include "ui/ScriptDialogs.h"
#include "ui/WidgetRegistry.h"
#include "script/LuaEngine.h"
#include "script/LuaParamParser.h"
#include "ui/ParamDialog.h"
#include "ui/ConfirmDialog.h"
#include "script/ScriptManager.h"
#include "core/Config.h"
#include "core/OpLog.h"   // the deferred-run catch boundary logs here
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
#include <memory>
#include <string.h>
#include <string>
#include <vector>


// Calculate the appropriate line number column width for each editor
// ---------------------------------------------------------------------------
// (m025: launchTool/spawnTool/activateByTitle are gone - the Lua console
//  and the AI chat are in-process windows; nothing is spawned or activated
//  by window title any more.)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Lua draft (m037): the console window is gone, the draft is a string.
//
// It stays in the same script.last.txt the console used - a plain text file
// next to the exe, opened through the wide path because the install directory
// may be Chinese and GetModuleFileNameA + fopen break there. Keeping the
// format means a user's last draft survives the upgrade, and console.set from
// an agent still persists across restarts.
// ---------------------------------------------------------------------------

static std::wstring luaDraftPath() {
    wchar_t exePath[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"script.last.txt";
    wchar_t *slash = wcsrchr(exePath, L'\\');
    if (slash) *slash = 0;
    return std::wstring(exePath) + L"\\script.last.txt";
}

const std::string &MainWindow::luaDraftText() {
    if (!m_luaDraftLoaded) {
        m_luaDraftLoaded = true;
        FILE *f = _wfopen(luaDraftPath().c_str(), L"rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (size > 0 && size < (64 << 20)) {   // sanity: < 64 MB
                std::string buf((size_t)size, '\0');
                if (fread(&buf[0], 1, (size_t)size, f) == (size_t)size) {
                    if (buf.size() >= 3 && (unsigned char)buf[0] == 0xEF &&
                        (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF)
                        buf.erase(0, 3);           // strip a UTF-8 BOM
                    m_luaDraft = std::move(buf);
                }
            }
            fclose(f);
        }
    }
    return m_luaDraft;
}

void MainWindow::setLuaDraft(const std::string &text) {
    m_luaDraft = text;
    m_luaDraftLoaded = true;
    // Write to a temp file and rename it over the target: a direct truncate+
    // write destroys the previous draft when the process dies half-way
    // through (and this file is rewritten by every external agent call to
    // console.set, so "half-way through" is a normal event).
    const std::wstring path = luaDraftPath();
    std::wstring tmp = path + L".tmp";
    FILE *f = _wfopen(tmp.c_str(), L"wb");
    if (!f) return;
    const size_t written = text.empty() ? 0 : fwrite(text.data(), 1, text.size(), f);
    const bool ok = (written == text.size());
    fclose(f);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return;
    }
    // MoveFileExW replaces the target atomically (same pattern as
    // core/Config.cpp's settings.ini write).
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
    }
}

// Put the draft in front of the user: a fresh tab in the ordinary editor, not
// a special window. The tab is unnamed, so saving it is the user's choice
// (Ctrl+Shift+S > script/<name>.lua, or script.save from an agent).
bool MainWindow::showLuaDraftAsTab(std::string *errMsg) {
    const std::string &draft = luaDraftText();
    if (draft.empty()) {
        if (errMsg) *errMsg = I18n::get("lua.draftempty");
        return false;
    }
    // The draft goes into a NEW tab, and that is not negotiable:
    //  - it must NEVER replace the current document: that document may
    //    carry unsaved work, and "show me a script" is not a licence to
    //    throw it away;
    //  - it must never open a modal "save your changes?" box: a modal
    //    blocks the whole UI thread, and on the agent path that means the
    //    MCP call times out while the box sits there.
    // In single-tab mode the tab strip stays hidden, so the draft covers
    // the current document on screen; the document itself is untouched and
    // comes back when the draft tab is closed (Ctrl+W).
    Document *doc = new Document();
    const int idx = addTab(doc, "Unnamed");
    if (idx < 0) {
        delete doc;
        if (errMsg) *errMsg = I18n::get("lua.nodoc");
        return false;
    }
    Tab *t = activeTab();
    if (!t || !t->editor || !t->doc) {
        if (errMsg) *errMsg = I18n::get("lua.nodoc");
        return false;
    }
    Fl_Text_Buffer *buf = t->doc->buffer();
    buf->text(draft.c_str());          // the editor draws the buffer it shares
    updateTabLabel(activeTabIndex());
    updateTitle();
    updateStatusBar();
    layoutTabs();
    Fl::focus(t->editor);
    return true;
}

// ---------------------------------------------------------------------------
// Human feedback for a script run (m037). The console's output pane is gone
// and the script bar used to throw out/err away, so the result lands in the
// status line as a short verb, and a failure's full text goes to the info
// window - honest reporting, no modal box, no silent success.
//
// The number next to the verb is the WALL-CLOCK COST of the run, not the
// line count of the script's output: "脚本完成 0" told the user nothing -
// it read like a counter that never counts anything, and it did not say
// whether the script was fast or slow. The cost is what the person who
// clicked the button actually wants to know.
//
// The notice is stored as an i18n KEY (not as rendered text), so switching
// the UI language re-renders it in the new language instead of leaving the
// old wording on screen - see MainWindow::renderNotice().
// ---------------------------------------------------------------------------
void MainWindow::reportScriptOutcome(bool ok, const std::string &out,
                                     const std::string &err, bool showDetails,
                                     long long elapsedMs) {
    // Unknown cost (nothing measured this run) => no time in the notice.
    // An invented "0 ms" would be worse than no number at all.
    if (elapsedMs < 0) elapsedMs = m_lastScriptMs;
    const bool timed = elapsedMs >= 0;

    if (ok) {
        m_scriptNotice.key = timed ? "lua.ok_time" : "lua.ok_short";
        m_scriptNotice.arg = elapsedMs;
        m_scriptNotice.hasArg = timed;
    } else {
        m_scriptNotice.key = timed ? "lua.fail_time" : "lua.fail_short";
        m_scriptNotice.arg = elapsedMs;
        m_scriptNotice.hasArg = timed;
        if (!showDetails) { updateStatusBar(); return; }
        std::vector<InfoRow> rows;
        InfoRow r1;
        r1.label = I18n::get("lua.error");
        r1.value = err.empty() ? std::string("(?)") : err;
        rows.push_back(r1);
        if (!out.empty()) {
            InfoRow r2;
            r2.label = I18n::get("lua.row_output");
            r2.value = out;
            rows.push_back(r2);
        }
        InfoWindow info;
        // The window title stays the plain verb: a title is a label, not a
        // measurement, and "(12 ms)" in a title bar is noise.
        info.setTitle(I18n::get("lua.fail_short"));
        info.setTheme(&m_theme, m_cfg->getUiFontSize());
        info.setWidth(520);
        info.setRows(rows);
        info.show();
    }
    updateStatusBar();
}

// ---------------------------------------------------------------------------
// Script UI hooks (m025): the themed dialogs a Lua script may ask for.
// Installed once per MainWindow; cleared on teardown so a later window or a
// unit test can never dispatch into a destroyed theme.
// ---------------------------------------------------------------------------

int MainWindow::uiFontSizeForDialogs() const
{
    return m_cfg ? m_cfg->getUiFontSize() : 16;
}

static int scriptUiInput(void *ud, const char *prompt, const char *dflt,
                         char *out, int outCap)
{
    MainWindow *self = static_cast<MainWindow *>(ud);
    if (!self) return 0;
    PromptDialog dlg(prompt && *prompt ? prompt : "Input",
                     prompt ? prompt : "Input:", dflt ? dflt : "",
                     self->themePtr(), self->uiFontSizeForDialogs());
    if (!dlg.run()) return 0;
    if (!dlg.ok()) return 0;
    const std::string &v = dlg.value();
    int n = (int)v.size();
    if (n > outCap - 1) n = outCap - 1;
    memcpy(out, v.data(), (size_t)n);
    out[n] = 0;
    return 1;
}

static int scriptUiSelect(void *ud, const char *prompt,
                          const char *const *items, int n, int pre)
{
    MainWindow *self = static_cast<MainWindow *>(ud);
    if (!self || n <= 0) return -1;
    std::vector<std::string> v;
    v.reserve((size_t)n);
    for (int i = 0; i < n; ++i) v.emplace_back(items[i] ? items[i] : "");
    PickDialog dlg(prompt && *prompt ? prompt : "Select",
                   prompt ? prompt : "Select:", v, pre,
                   self->themePtr(), self->uiFontSizeForDialogs());
    if (!dlg.run()) return -1;
    return dlg.choice();
}

void MainWindow::installScriptUiHooks()
{
    LuaUiHooks h;
    h.ud = this;
    h.input = scriptUiInput;
    h.select = scriptUiSelect;
    luaSetUiHooks(&h);
}

void MainWindow::clearScriptUiHooks()
{
    luaSetUiHooks(nullptr);
}

// The chat is a pane child of this window (m037), not a window of its own:
// no size persistence, no centering, no taskbar entry. layoutPreviewPanes()
// gives it the right zone; the initial 200x200 is a placeholder until then.
AIChatPane *MainWindow::ensureAiChatPane() {
    if (!m_aiChat) {
        m_aiChat = new AIChatPane(m_cfg, &m_theme, 200, 200);
        add(m_aiChat);
        m_aiChat->hide();
        // The elapsed counter belongs to the chat, but it is shown in the
        // status line - one place renders it, the pane only reports the number.
        m_aiChat->setElapsedCallback([this](long long secs) {
            m_aiChatElapsed = secs;
            updateStatusBar();
        });
    }
    return m_aiChat;
}

// Open the AI column. Idempotent: ai.send and "send selection" call it on
// every message, and a chat that is already open must stay open. It never
// touches the preview - the two columns are independent (m039).
void MainWindow::openAiChatZone() {
    ensureAiChatPane();
    ensureZoneDivider();
    ensureAiDivider();
    m_aiChatDocked = true;
    layoutTabs();
    m_aiChat->show();
    m_aiChat->redraw();
    m_aiChat->focusInput();
}

void MainWindow::closeAiZone() {
    m_aiChatDocked = false;
    if (m_aiChat) m_aiChat->hide();
    layoutTabs();
}

void MainWindow::showAIChat() { openAiChatZone(); }

// View > AI Chat toggles only its own column; the preview column keeps
// whatever state the user left it in.
void MainWindow::cbAIChat(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self) return;
    if (self->m_aiChatDocked) self->closeAiZone();
    else self->openAiChatZone();
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
    std::shared_ptr<std::atomic<bool>> alive;   // cleared by ~MainWindow
};

static void runScriptDeferred(void *data) {
    const std::unique_ptr<DeferredRunScript> d(static_cast<DeferredRunScript *>(data));
    if (!d || !d->self || !d->relPath) return;
    // The menu that armed this can be followed by the window closing before
    // the next tick: the token is the only thing that says so.
    if (!d->alive || !d->alive->load()) return;
    // Red line 6: timer callback. An escaping exception is fastfail
    // 0xC0000409 and bypasses the crash handler, so nothing gets logged.
    try {
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
        const bool ok = d->self->executeLuaScript(script, &out, &err);
        // The console output pane is gone: without this the human sees nothing
        // at all when a script bar run fails.
        d->self->reportScriptOutcome(ok, out, err);
    } catch (const std::exception &e) {
        opLog("deferred script run failed: %s", e.what());
    } catch (...) {
        opLog("deferred script run failed: unknown exception");
    }
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
    d->alive = self->m_alive;
    d->relPath = _strdup(rel);
    Fl::add_timeout(0.0, runScriptDeferred, d);
}

bool MainWindow::executeLuaScript(const std::string &script,
                                  std::string *output,
                                  std::string *errMsg,
                                  const std::vector<std::pair<std::string, std::string>> *preSuppliedParams) {
    // Unknown until the engine answers: a run that never starts (no
    // document, read-only, cancelled parameter dialog) must not leave the
    // previous run's time on the status line.
    m_lastScriptMs = -1;
    Tab *t = activeTab();
    if (!t || !t->editor || !t->doc) {
        if (errMsg) *errMsg = I18n::get("lua.nodoc");
        return false;
    }
    if (!t->doc->isEditable()) {
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
    // Every shipped language must be recognized as a --!pui.<code>
    // override, otherwise script-carried translations for added
    // languages (ja/de/zh-TW/...) silently fall back to English.
    std::vector<std::string> knownLangs;
    for (const auto &li : I18n::scanLanguages()) knownLangs.push_back(li.code);
    LuaParamSet decls = luaParseParams(script, langCode, knownLangs);
    if (preSuppliedParams) {
        // Console path: parameters were already collected by the console's
        // own dialog - inject them and skip the in-house prompt entirely.
        paramValues = *preSuppliedParams;
    } else if (!decls.params.empty()) {
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

    // The engine measures the wall-clock cost of the run; the status line
    // reports it next to the outcome verb.
    LuaRunResult res;
    bool ok = m_luaEngine->run(script, host, output, errMsg,
                               paramValues.empty() ? nullptr : &paramValues,
                               nullptr, &res, nullptr);
    m_lastScriptMs = res.elapsedMs;

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

    // A failed script is reported by the CALLER, not here: this function is
    // also the agent path (core/HostOps "lua.run"), and a modal dialog there
    // blocks the whole UI thread - the MCP call would time out while the
    // dialog sits there waiting for a click that no user will ever make.
    // The script bar calls reportScriptOutcome() (one info window); the
    // agent path calls it with showDetails=false (status line only).
    return ok;
}

bool MainWindow::insertAiText(const std::string &text, std::string *errMsg) {
    Tab *t = activeTab();
    if (!t || !t->editor || !t->doc) {
        if (errMsg) *errMsg = I18n::get("lua.nodoc");
        return false;
    }
    if (!t->doc->isEditable()) {
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
    if (!t->doc->isEditable()) {
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
    if (!t->doc->isEditable()) {
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