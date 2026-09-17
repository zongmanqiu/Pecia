// LuaToolWindow.cpp - standalone Lua script tool.
// Same custom title bar / theme as the main window (border(0) + TitleBar).
#include "LuaToolWindow.h"

#include "core/Config.h"
#include "core/I18n.h"
#include "core/Theme.h"
#include "script/LuaEngine.h"
#include "script/LuaParamParser.h"
#include "ui/ParamDialog.h"
#include "PipeClient.h"
#include "PipeProtocol.h"
#include "core/PipeSecurity.h"
#include "ToolChrome.h"
#include "ThemeWidgets.h"
#include "ui/TitleBar.h"
#include "ui/WindowFrame.h"
#include "ui/Layout.h"
#include "ui/SmokeTest.h"

#include <FL/Fl.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Box.H>
#include <FL/x.H>
#include <shobjidl.h>   // IFileSaveDialog
#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <cwchar>
#include <memory>
#include <mutex>
#include <thread>

namespace {
const int BTN_H = gBtnH;   // unified button height
const int BAR_H = gBarH;   // unified bar height: button row
const int BTN_GAP = 10;
} // namespace

std::atomic<LuaToolWindow *> LuaToolWindow::s_active = nullptr;

LuaToolWindow::LuaToolWindow(Config *appCfg,
                             const std::string &peciaPipe,
                             int w, int h, const char *title)
    : DialogBase(appCfg, w, h, title, appCfg ? appCfg->getUiFontSize() : 16, ToolWindow)
    , m_appCfg(appCfg)
    , m_peciaPipe(peciaPipe) {
    begin();
    initShell(title);

    // Shortcut dispatch registry (window ops + console actions) from
    // the shared shortcut.* config keys.
    applyShortcuts();

    // Text panes (script editor + output) are edge-to-edge (x=0, full
    // width) matching the main Pecia window. Only the output header row
    // and its buttons are inset by BTN_GAP (the same gap as between
    // buttons), and the bottom pane sits BTN_GAP above the bottom edge.
    const int x = 0;
    const int cw = w;
    const int OUT_H = 140;
    const ThemeColors &tc = m_theme->colors();

    int font = FL_COURIER;
    int fontSize = 13;
    if (m_appCfg) m_appCfg->getFont(font, fontSize);
    const int uiFontSize = m_appCfg ? m_appCfg->getUiFontSize() : 16;
    
    // Bottom-up layout (no overlaps):
    //   output   outY..outY+OUT_H
    //   outHdr   outHdrY..outHdrY+BAR_H  (title + Clear/Run buttons on the
    //            right; flush against output and script, exactly BAR_H
    //            tall like every other bar - no extra gaps above/below)
    //   script   scriptTop..outHdrY (fills the rest, no header row)
    const int outY = h - OUT_H;
    const int outHdrY = outY - BAR_H;
    const int scriptTop = TITLE_H;   // flush to the title bar

    m_scriptBuf = new Fl_Text_Buffer();
    bool hl = m_appCfg ? m_appCfg->getHighlightCurrentLine() : true;
    m_scriptEdit = new ThemedTextEditor(x, scriptTop, cw, outHdrY - scriptTop, tc, hl);
    m_scriptEdit->buffer(m_scriptBuf);
    m_scriptEdit->textfont(font);
    m_scriptEdit->textsize(fontSize);
    m_scriptEdit->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
    // NOTE: do NOT override color()/textcolor() here — ThemedTextEditor
    // already derives theme colors from `tc`, and retheme() re-applies them.
    // Hard-coding FL_WHITE/FL_BLACK made the console ignore the theme.

    Fl_Color chromeCol = tc.background2;
    Fl_Color btnFg = tc.text1;
    auto styleBtn = [&](Fl_Button *b) {
        b->color(chromeCol);
        b->selection_color(tc.highlight1);
        if (auto *hb = dynamic_cast<HoverButton *>(b))
            hb->setPressColor(tc.highlight2);
        b->labelcolor(btnFg);
        b->labelsize(uiFontSize);
    };
    // Output header row: title on the left (inset BTN_GAP from the left
    // edge), Clear/Run buttons on the right (BTN_GAP from the right edge).
    // The row is BAR_H tall; the buttons are BTN_H, vertically centered.
    const int btnY = outHdrY + (BAR_H - BTN_H) / 2;
    m_outHdr = new Fl_Box(BTN_GAP, outHdrY, cw - BTN_GAP, BAR_H, I18n::get("lua.secoutput"));
    m_outHdr->labelcolor(tc.text1);
    m_outHdr->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    m_outHdr->labelsize(uiFontSize);

    m_runBtn = new HoverButton(0, btnY, 0, BTN_H, I18n::get("lua.run"));
    m_runBtn->callback(cbRun, this);
    styleBtn(m_runBtn);
    m_clearBtn = new HoverButton(0, btnY, 0, BTN_H, I18n::get("lua.clear"));
    m_clearBtn->callback(cbClear, this);
    styleBtn(m_clearBtn);
    m_saveAsBtn = new HoverButton(0, btnY, 0, BTN_H, I18n::get("lua.saveas"));
    m_saveAsBtn->callback(cbSaveAs, this);
    styleBtn(m_saveAsBtn);
    m_toAiBtn = new HoverButton(0, btnY, 0, BTN_H, I18n::get("lua.toai"));
    m_toAiBtn->callback(cbToAi, this);
    styleBtn(m_toAiBtn);
    m_helpBtn = new HoverButton(0, btnY, 0, BTN_H, I18n::get("lua.help"));
    m_helpBtn->callback(cbHelp, this);
    styleBtn(m_helpBtn);

    // Auto-width right-aligned row: left-to-right
    // [Help][SaveAs][Clear][ToAi][Run] (Run rightmost).
    fitButtonRow({m_helpBtn, m_saveAsBtn, m_clearBtn, m_toAiBtn, m_runBtn},
                 w, btnY + BTN_H / 2, BTN_GAP, BTN_GAP);

    m_outBuf = new Fl_Text_Buffer();
    m_outDisp = new ThemedTextDisplay(x, outY, cw, OUT_H, tc);
    m_outDisp->buffer(m_outBuf);
    m_outDisp->textfont(font);
    m_outDisp->textsize(fontSize);
    // Theme colors come from ThemedTextDisplay + retheme(); no white override.

    end();
    resizable(m_scriptEdit);
    color(tc.background2);
    finalizeShell();
    callback(cbClose, this);        // Alt+F4 / WM_CLOSE also saves

    loadScript();
    // In smoke-test mode skip the detached pipe-listener thread (it blocks
    // forever and references this; the test only constructs/opens windows).
    if (!ui::g_smokeMode) startInsServer();

    // Start the cursor blink timer (same cadence as the main window).
    Fl::add_timeout(0.5, cursorBlinkCb, this);
}

// Re-read I18n strings after the settings.ini watcher detects a language
// change. Updates button labels, the output header, and the title bar
// (single-tab mode shows the window title). Button widths are re-measured
// because translations have different lengths.
void LuaToolWindow::refreshLabels() {
    if (m_runBtn)    m_runBtn->copy_label(I18n::get("lua.run"));
    if (m_clearBtn)  m_clearBtn->copy_label(I18n::get("lua.clear"));
    if (m_saveAsBtn) m_saveAsBtn->copy_label(I18n::get("lua.saveas"));
    if (m_toAiBtn)   m_toAiBtn->copy_label(I18n::get("lua.toai"));
    if (m_helpBtn)   m_helpBtn->copy_label(I18n::get("lua.help"));
    if (m_outHdr)    m_outHdr->copy_label(I18n::get("lua.secoutput"));
    if (m_titleBar) {
        m_titleBar->setTabData({TitleBar::TabInfo{I18n::get("lua.title")}}, 0);
    }

    // Re-fit the auto-width button row after labels change (translations
    // differ in length). Keep the row's vertical center.
    int rowCenter = m_runBtn ? m_runBtn->y() + m_runBtn->h() / 2 : 0;
    fitButtonRow({m_helpBtn, m_saveAsBtn, m_clearBtn, m_toAiBtn, m_runBtn},
                 w(), rowCenter, BTN_GAP, BTN_GAP);

    redraw();
}

// Window resize: re-assert the intended geometry (see AIChatWindow::resize
// for the rationale): the script editor and output pane span the full
// width, the output header keeps its BTN_GAP inset, and the button row is
// re-fitted (each button keeps its label width, plan.txt #1) right-aligned
// inside the current window width.
void LuaToolWindow::resize(int X, int Y, int W, int H) {
    DialogBase::resize(X, Y, W, H);
    if (!m_scriptEdit) return;
    m_scriptEdit->resize(0, m_scriptEdit->y(), W, m_scriptEdit->h());
    if (m_outDisp) m_outDisp->resize(0, m_outDisp->y(), W, m_outDisp->h());
    if (m_outHdr) m_outHdr->resize(BTN_GAP, m_outHdr->y(), W - BTN_GAP, m_outHdr->h());
    if (m_runBtn) {
        int rowCenter = m_runBtn->y() + m_runBtn->h() / 2;
        fitButtonRow({m_helpBtn, m_saveAsBtn, m_clearBtn, m_toAiBtn, m_runBtn},
                     W, rowCenter, BTN_GAP, BTN_GAP);
    }
    redraw();
}

void LuaToolWindow::retheme() {
    if (!m_appCfg) return;
    const Theme &th = DialogBase::retheme(*m_appCfg);   // reload owned theme + shell chrome
    const ThemeColors &tc = th.colors();

    // DialogBase::retheme() resets the window color to background1 (work
    // area). This tool window is chrome-themed, so restore the frame color
    // used at construction (line 143). Without this, the button-row strip
    // (its background is the window color showing through the transparent
    // output-header box) turns white after any theme/language/shortcut
    // change - i.e. the row's gray "disappears" while the buttons keep
    // background2.
    color(tc.background2);

    if (m_scriptEdit) {
        m_scriptEdit->color(tc.background1);
        m_scriptEdit->textcolor(tc.text1);
        m_scriptEdit->selection_color(tc.highlight2);
        m_scriptEdit->cursor_color(tc.text1);
    }
    if (m_outDisp) {
        m_outDisp->color(tc.background1);
        m_outDisp->textcolor(tc.text1);
        m_outDisp->selection_color(tc.highlight2);
    }
    for (auto *b : { m_runBtn, m_clearBtn, m_saveAsBtn, m_toAiBtn, m_helpBtn }) {
        if (b) { b->color(tc.background2); b->labelcolor(tc.text1); b->selection_color(tc.highlight1); }
    }
    if (m_outHdr) m_outHdr->labelcolor(tc.text1);
    redraw();
}

LuaToolWindow::~LuaToolWindow() {
    Fl::remove_timeout(cursorBlinkCb, this);
    saveScript();
    if (s_active.load() == this) s_active.store(nullptr);
    // Stop the INS listener thread (owned, see startInsServer): clear the
    // run flag, wake a blocked connect/read, then join. Previously the
    // thread was detached and ran forever, serving requests even after the
    // window was destroyed.
    m_insRunning.store(false);
    if (m_insThread.joinable()) {
        // Interrupt any in-flight blocking read/write.
        HANDLE cur = m_insPipe.load();
        if (cur) CancelIoEx(cur, nullptr);
        // Wake a blocked connect: connect to our own pipe so the accept
        // wait completes and the loop observes m_insRunning == false.
        HANDLE hWake = CreateFileA(PIPE_LUA_TOOL, GENERIC_READ | GENERIC_WRITE,
                                   0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hWake != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            const char bad[4] = { 'S', 'T', 'O', 'P' };   // any non-INS tag
            WriteFile(hWake, bad, 4, &written, nullptr);
            DWORD zero = 0;
            WriteFile(hWake, &zero, 4, &written, nullptr);
            CloseHandle(hWake);
        }
        m_insThread.join();
    }
    delete m_localEngine;
    delete m_localBuf;
}

int LuaToolWindow::handle(int event) {
    // Configurable shortcuts first (window ops + console actions): a
    // text field may otherwise consume the key before we see it.
    if (event == FL_KEYDOWN || event == FL_SHORTCUT) {
        if (dispatchShortcut()) return 1;
    }
    // Title-bar event forwarding and the outer border are handled by
    // DialogBase (shared by every window).
    return DialogBase::handle(event);
}

int LuaToolWindow::dispatchShortcut() {
    int key = Fl::event_key();
    int state = Fl::event_state();
    if (const std::string *id = m_shortcutRegistry.find(key, state)) {
        auto it = m_shortcutHandlers.find(*id);
        if (it != m_shortcutHandlers.end()) {
            it->second();
            return 1;
        }
    }
    return 0;
}

// Re-read the shortcut.* config keys (shared settings.ini) and rebuild
// the local dispatch registry. Called at startup and whenever the
// settings.ini watcher fires (e.g. after the main window's Shortcuts
// dialog saved new combos).
void LuaToolWindow::applyShortcuts() {
    m_shortcutRegistry.clear();
    m_shortcutHandlers.clear();
    if (!m_appCfg) return;

    auto bind = [this](const char *id, int defKey, unsigned defMods,
                       std::function<void()> fn) {
        ShortcutCombo combo;
        char key[160];
        snprintf(key, sizeof(key), "shortcut.%s", id ? id : "");
        std::string text = m_appCfg->getShortcut(id);
        if (m_appCfg->hasKey(key)) {
            if (!text.empty()) shortcutParse(text, &combo);   // empty = explicit none
        } else {
            combo.key = defKey;
            combo.mods = defMods;
        }
        if (combo.empty()) return;
        m_shortcutRegistry.set(id, combo.key, combo.mods);
        m_shortcutHandlers[id] = std::move(fn);
    };

    bind("win.pin", 0, 0, [this]() { togglePin(); });
    bind("win.minimize", 0, 0, [this]() { minimizeWindow(); });
    bind("win.maximize", 0, 0, [this]() { toggleMaximize(); });
    bind("win.close", 0, 0, [this]() { cbClose(nullptr, this); });
    // Tool buttons (Window > Button group): one config key, bound here.
    bind("win.button.run", FL_Enter, FL_CTRL, [this]() { doRun(); });
    bind("win.button.clear", 0, 0,
         [this]() { if (m_outBuf) m_outBuf->text(""); });
    bind("win.button.toai", 0, 0, [this]() { cbToAi(nullptr, this); });
    bind("win.button.saveas", 0, 0, [this]() { doSaveAs(); });
    bind("win.button.help", 0, 0, [this]() { cbHelp(nullptr, this); });
    // Lua Save As follows the main window's Save As shortcut
    // (menu.file.saveas) - no separate config key.
    bind("menu.file.saveas", 's', FL_CTRL | FL_SHIFT, [this]() { doSaveAs(); });
}

void LuaToolWindow::minimizeWindow() {
#if defined(_WIN32)
    HWND hwnd = fl_xid(this);
    if (hwnd) ShowWindow(hwnd, SW_MINIMIZE);
#else
    iconize();
#endif
}

// Pin/maximize are implemented in DialogBase (onCaptionTogglePin /
// onCaptionMaximize); the header's togglePin()/toggleMaximize() wrappers
// forward to them so the configurable win.* shortcut bindings work.

void LuaToolWindow::openDialog() {
    // Re-center every time the window is opened (user may have moved it
    // by dragging the title bar; next open comes back to screen center).
    position((Fl::w() - w()) / 2, (Fl::h() - h()) / 2);
    show();
    // Render the first frame synchronously BEFORE touching the native
    // chrome: otherwise the window is visible but its double buffer is
    // still empty (black) while setupToolChrome runs. This is exactly
    // the FLTK-documented pattern (show -> wait_for_expose -> flush).
    wait_for_expose();
    Fl::flush();
    // Apply the native chrome (taskbar presence, corners, border, icon,
    // edge resize). The HWND exists after show(); waiting for expose
    // first means the window never shows unpainted.
    applyToolChrome();
    if (m_scriptEdit) {
        m_scriptEdit->take_focus();
        m_scriptEdit->insert_position(m_scriptBuf->length());
    }
}

void LuaToolWindow::loadScript() {
    if (!m_scriptBuf) return;
    // The last script is stored as a plain text file next to the exe
    // (script.last.txt), NOT inside the INI: scripts are multi-line and
    // the INI writer does not escape newlines, which corrupted the file.
    // Open via the wide path so a non-ASCII install directory (Chinese)
    // works - GetModuleFileNameA + fopen break on those.
    std::wstring path = scriptLastPath();
    FILE *f = _wfopen(path.c_str(), L"rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size > 0 && size < (64 << 20)) {   // sanity: < 64 MB
        std::string buf((size_t)size, '\0');
        if (fread(&buf[0], 1, (size_t)size, f) == (size_t)size) {
            // Strip a UTF-8 BOM if the file was saved with one.
            if (buf.size() >= 3 && (unsigned char)buf[0] == 0xEF &&
                (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
                buf.erase(0, 3);
            }
            m_scriptBuf->text(buf.c_str());
        }
    }
    fclose(f);
}

void LuaToolWindow::saveScript() {
    if (!m_scriptBuf) return;
    char *script = m_scriptBuf->text();
    if (script) {
        std::wstring path = scriptLastPath();
        FILE *f = _wfopen(path.c_str(), L"wb");
        if (f) {
            fwrite(script, 1, strlen(script), f);
            fclose(f);
        }
        free(script);
    }
}

// Resolve <exe dir>/script.last.txt (UTF-8 filename, so use wide-char path
// to survive a non-ASCII exe directory).
std::wstring LuaToolWindow::scriptLastPath() {
    wchar_t exePath[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"script.last.txt";
    wchar_t *slash = wcsrchr(exePath, L'\\');
    if (slash) *slash = 0;
    return std::wstring(exePath) + L"\\script.last.txt";
}

void LuaToolWindow::appendOutput(const std::string &text) {
    if (!m_outBuf) return;
    // Timestamp header line (hh:mm:ss) so each run is visibly delimited.
    char ts[32];
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(ts, sizeof(ts), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    int pos = m_outBuf->length();
    m_outBuf->insert(pos, ts);
    m_outBuf->insert(pos + (int)strlen(ts), "\n");
    if (!text.empty()) {
        pos = m_outBuf->length();
        m_outBuf->insert(pos, text.c_str(), (int)text.size());
        m_outBuf->insert(m_outBuf->length(), "\n");
    }
    m_outDisp->insert_position(m_outBuf->length());
    m_outDisp->show_insert_position();
}

void LuaToolWindow::cbRun(Fl_Widget *, void *data) {
    static_cast<LuaToolWindow *>(data)->doRun();
}
void LuaToolWindow::cbClear(Fl_Widget *, void *data) {
    LuaToolWindow *t = static_cast<LuaToolWindow *>(data);
    if (t->m_outBuf) t->m_outBuf->text("");
}

void LuaToolWindow::cbSaveAs(Fl_Widget *, void *data) {
    static_cast<LuaToolWindow *>(data)->doSaveAs();
}

// 把当前脚本 + 输出区最后一次运行反馈组成一条用户消息，发给 AI 聊天
// 窗口（经主进程的 SEND 管道请求转发到 AIChat）。消息内容会原样显示
// 在 AI 对话框里（lua_api.txt 的 system 拼接与这里无关）。
void LuaToolWindow::cbToAi(Fl_Widget *, void *data) {
    LuaToolWindow *t = static_cast<LuaToolWindow *>(data);
    if (!t) return;

    std::string msg;
    msg += I18n::get("lua.msgscript");
    msg += "\n";
    if (t->m_scriptBuf && t->m_scriptBuf->length() > 0) {
        char *code = t->m_scriptBuf->text();
        msg += code;
        ::free(code);
    } else {
        msg += I18n::get("lua.msgempty");
    }
    msg += "\n\n";
    msg += I18n::get("lua.msgoutput");
    msg += "\n";
    if (t->m_outBuf && t->m_outBuf->length() > 0) {
        char *out = t->m_outBuf->text();
        msg += out;
        ::free(out);
    } else {
        msg += I18n::get("lua.msgnoout");
    }

    PipeReply rep = pipeSend(t->m_peciaPipe, PIPE_TAG_SEND, msg);
    if (rep.peerConnected && rep.ok) {
        t->appendOutput(I18n::get("lua.sentai"));
    } else {
        t->appendOutput(rep.payload.empty() ? I18n::get("lua.sendfail")
                                            : rep.payload);
    }
}

// Save the script editor's content to a .lua file. The dialog opens in
// the exe-adjacent script/ folder (created on first use); saving there
// makes the script show up in the main window's script bar after a reload.
void LuaToolWindow::doSaveAs() {
    if (!m_scriptBuf) return;

    // Resolve <exe dir>/script (create it if missing) as the default folder.
    // 宽字符取 exe 路径：中文安装目录下 GetModuleFileNameA 会得到乱码。
    wchar_t exePathW[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    wchar_t *slashW = wcsrchr(exePathW, L'\\');
    if (slashW) *slashW = 0;
    std::wstring scriptDir(exePathW);
    {
        scriptDir += L"\\script";
        CreateDirectoryW(scriptDir.c_str(), nullptr);   // no-op if already exists
    }

    // Reuse the script content as the suggested file name (sanitized).
    char *txt = m_scriptBuf->text();
    std::string content = txt ? txt : "";
    if (txt) free(txt);
    std::wstring defName = L"script.lua";

    IFileSaveDialog *pfd = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_IFileSaveDialog,
                                  (void **)&pfd);
    if (FAILED(hr) || !pfd) {
        if (pfd) pfd->Release();
        return;
    }

    if (!scriptDir.empty()) {
        IShellItem *psiFolder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(scriptDir.c_str(), nullptr,
                                                  IID_IShellItem,
                                                  (void **)&psiFolder))) {
            pfd->SetDefaultFolder(psiFolder);
            psiFolder->Release();
        }
    }
    pfd->SetFileName(defName.c_str());
    COMDLG_FILTERSPEC filter = { L"Lua Script (*.lua)", L"*.lua" };
    pfd->SetFileTypes(1, &filter);
    pfd->SetDefaultExtension(L"lua");

    hr = pfd->Show(fl_xid(this));
    if (SUCCEEDED(hr)) {
        IShellItem *psi = nullptr;
        if (SUCCEEDED(pfd->GetResult(&psi)) && psi) {
            PWSTR path = nullptr;
            if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &path)) &&
                path) {
                // Write UTF-8 with BOM so the editor loads it correctly.
                // Open via the wide path: fopen() uses the ANSI code page and
                // would mangle/deny a non-ASCII save location.
                FILE *f = _wfopen(path, L"wb");
                if (f) {
                    fwrite("\xEF\xBB\xBF", 1, 3, f);   // UTF-8 BOM
                    fwrite(content.data(), 1, content.size(), f);
                    fclose(f);
                    // Tell the main window to refresh the script bar so the
                    // newly saved script appears immediately.
                    pipeSend(m_peciaPipe, PIPE_TAG_RELOAD, "");
                }
                CoTaskMemFree(path);
            }
            psi->Release();
        }
    }
    pfd->Release();
}

void LuaToolWindow::cbHelp(Fl_Widget *, void *data) {
    (void)data;
    // Open the API reference doc next to the exe (single bilingual file).
    wchar_t exePathW[MAX_PATH];
    GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
    wchar_t *slashW = wcsrchr(exePathW, L'\\');
    if (slashW) *slashW = 0;
    std::wstring doc = std::wstring(exePathW) + L"\\script\\lua_api.txt";
    ShellExecuteW(nullptr, L"open", doc.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
void LuaToolWindow::cbClose(Fl_Widget *, void *data) {
    LuaToolWindow *t = static_cast<LuaToolWindow *>(data);
    t->saveScript();
    t->hide();
}

void LuaToolWindow::cursorBlinkCb(void *data) {
    auto *self = static_cast<LuaToolWindow *>(data);
    if (!self) return;
    self->m_cursorVisible = !self->m_cursorVisible;
    // Only blink when the script editor has keyboard focus.
    if (self->m_scriptEdit && Fl::focus() == self->m_scriptEdit &&
        self->shown()) {
        self->m_scriptEdit->show_cursor(self->m_cursorVisible ? 1 : 0);
        self->m_scriptEdit->damage(FL_DAMAGE_CHILD);
    }
    Fl::add_timeout(0.5, cursorBlinkCb, self);
}

void LuaToolWindow::doRun() {
    char *script = m_scriptBuf->text();
    std::string code = script ? script : "";
    if (script) free(script);

    // Script-header parameter declarations: parse, prompt, inject.
    // The dialog runs on this (PeciaLua) process so a script with params
    // still executes entirely here - only the document snapshot round-trip
    // touches Pecia. UI texts are resolved for the current UI language
    // (the script carries its own translations via --!pui lines).
    std::vector<std::pair<std::string, std::string>> paramValues;
    char langCode[16] = "en";
    if (m_appCfg) m_appCfg->getLang(langCode, sizeof(langCode), "en");
    // Every shipped language must be recognized as a --!pui.<code>
    // override, otherwise script-carried translations for added
    // languages (ja/de/zh-TW/...) silently fall back to English.
    std::vector<std::string> knownLangs;
    for (const auto &li : I18n::scanLanguages()) knownLangs.push_back(li.code);
    LuaParamSet decls = luaParseParams(code, langCode, knownLangs);
    if (!decls.params.empty()) {
        const std::string dlgTitle = decls.title.empty()
            ? std::string(I18n::get("param.title"))
            : decls.title;
        ParamDialog dlg(dlgTitle.c_str(), decls.params, m_theme,
                        m_appCfg ? m_appCfg->getUiFontSize() : 16);
        if (!dlg.run()) { saveScript(); return; }   // cancelled - do not run
        const std::vector<std::string> &vals = dlg.results();
        for (size_t i = 0; i < decls.params.size() && i < vals.size(); ++i)
            paramValues.push_back({decls.params[i].name, vals[i]});
    }

    if (!m_localEngine) {
        m_localEngine = new LuaEngine();
        m_localBuf = new Fl_Text_Buffer();
    }

    std::string out, err;
    bool applied = false;

    // Fetch the document snapshot from Pecia (probe: no document text in
    // the script execution, only in the round-trip).
    PipeReply snap = pipeSend(m_peciaPipe, PIPE_TAG_GETDOC, "");
    if (snap.peerConnected && snap.ok && !snap.payload.empty()) {
        // Parse "cursor:selStart:selEnd\n" + document text.
        size_t nl = snap.payload.find('\n');
        if (nl != std::string::npos) {
            int cursor = 0, selStart = 0, selEnd = 0;
            sscanf(snap.payload.substr(0, nl).c_str(), "%d:%d:%d",
                   &cursor, &selStart, &selEnd);
            const std::string docText = snap.payload.substr(nl + 1);

            // Build a scratch buffer mirroring the document; the engine
            // runs the script against it (scratch semantics unchanged).
            m_localBuf->text(docText.c_str());
            LuaEditorHost host;
            host.buffer = m_localBuf;
            host.cursorPos = cursor;
            host.hasSelection = (selEnd > selStart);
            host.selStart = selStart;
            host.selEnd = selEnd;

            bool ok = m_localEngine->run(code, host, &out, &err,
                                         paramValues.empty() ? nullptr
                                                             : &paramValues,
                                         langCode);
            if (!ok) {
                appendOutput(err.empty() ? I18n::get("lua.error") : err);
            } else {
                if (!out.empty()) appendOutput(out);
                appendOutput(I18n::get("lua.done"));   // 框架自动反馈运行状态
            }

            // Send the result back to Pecia to apply to the document.
            char *ft = m_localBuf->text();
            std::string payload;
            if (ft) {
                char hdr[64];
                snprintf(hdr, sizeof(hdr), "%d:%d:%d\n", host.cursorPos,
                         host.hasSelection ? host.selStart : -1,
                         host.hasSelection ? host.selEnd : -1);
                payload = hdr;
                payload += ft;
                free(ft);
            }
            PipeReply rep = pipeSend(m_peciaPipe, PIPE_TAG_APPLY, payload);
            applied = rep.peerConnected;
            if (applied && !rep.ok && !rep.payload.empty())
                appendOutput(rep.payload);
        }
    } else if (snap.peerConnected) {
        // Pecia reachable but snapshot failed (no doc / read-only).
        appendOutput(snap.payload.empty() ? I18n::get("lua.nodoc")
                                          : snap.payload);
    } else {
        // Pecia not running: local mode without a document (script may
        // still do pure text work, editor:* is unavailable).
        m_localBuf->text("");
        LuaEditorHost host;
        host.buffer = m_localBuf;
        host.cursorPos = 0;
        host.hasSelection = false;
        bool ok = m_localEngine->run(code, host, &out, &err,
                                     paramValues.empty() ? nullptr
                                                         : &paramValues,
                                     langCode);
        if (!ok) {
            appendOutput(err.empty() ? I18n::get("lua.error") : err);
        } else {
            if (!out.empty()) appendOutput(out);
            appendOutput(I18n::get("lua.done"));   // 框架自动反馈运行状态
        }
    }

    saveScript();
}

// ---------------------------------------------------------------------------
// INS server: AIChat inserts code into the script editor.
// ---------------------------------------------------------------------------

namespace {
struct InsRequest {
    std::string text;
    std::mutex  mu;
    std::condition_variable cv;
    bool        done = false;
};

// Read exactly n bytes (loop over partial reads - a byte-mode pipe can
// return less than requested in one ReadFile, which the previous code
// treated as a hard error, dropping valid requests).
bool readExact(HANDLE h, void *buf, DWORD n) {
    char *p = static_cast<char *>(buf);
    while (n > 0) {
        DWORD got = 0;
        // NOTE: synchronous I/O on overlapped handle — see LuaPipeServer readAll() comment.
        if (!ReadFile(h, p, n, &got, nullptr) || got == 0) return false;
        p += got;
        n -= got;
    }
    return true;
}
} // namespace

void LuaToolWindow::s_handleIns(void *data) {
    // `data` is a heap std::shared_ptr<InsRequest>*; take ownership so the
    // request stays alive even if the listener already dropped its ref
    // (fixes a use-after-scope of the old stack local when the wait_for
    // timeout fired and the UI callback ran late).
    std::shared_ptr<InsRequest> *wrapper =
        static_cast<std::shared_ptr<InsRequest> *>(data);
    std::shared_ptr<InsRequest> r = std::move(*wrapper);
    delete wrapper;
    LuaToolWindow *self = s_active.load();
    if (self && self->m_scriptBuf)
        self->m_scriptBuf->text(r->text.c_str());
    {
        std::lock_guard<std::mutex> lk(r->mu);
        r->done = true;
    }
    r->cv.notify_one();
}

void LuaToolWindow::startInsServer() {
    s_active.store(this);
    m_insRunning.store(true);
    m_insThread = std::thread([this]() {
        // Bounded wait so teardown is observed even without a client.
        const DWORD kPollMs = 200;
        while (m_insRunning.load()) {
            // User-only DACL (see core/PipeSecurity.h): the fixed-name pipe
            // receives scripts/text; restrict it to the current user.
            SECURITY_DESCRIPTOR sd;
            SECURITY_ATTRIBUTES sa;
            PACL acl = nullptr;
            bool secOk = pipeSec::makeUserOnlySa(sd, sa, acl);
            HANDLE hPipe = CreateNamedPipeA(PIPE_LUA_TOOL, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                            1, 4096, 4096, 0, secOk ? &sa : nullptr);
            if (acl) LocalFree(acl);
            if (hPipe == INVALID_HANDLE_VALUE) return;
            // Register the handle so the destructor can CancelIoEx an
            // in-flight blocking read/write during teardown.
            m_insPipe.store(hPipe, std::memory_order_relaxed);

            // Wait for a client with a bounded, cancellable connect: the
            // old synchronous ConnectNamedPipe could block forever and the
            // detached thread had no exit path at all.
            HANDLE hEvt = CreateEventA(nullptr, TRUE, FALSE, nullptr);
            if (!hEvt) { CloseHandle(hPipe); return; }
            OVERLAPPED ov = {};
            ov.hEvent = hEvt;
            BOOL connOk = ConnectNamedPipe(hPipe, &ov);
            DWORD err = GetLastError();
            if (connOk || err == ERROR_PIPE_CONNECTED) {
                // already connected (client was waiting)
            } else if (err == ERROR_IO_PENDING) {
                for (;;) {
                    DWORD wr = WaitForSingleObject(hEvt, kPollMs);
                    if (wr == WAIT_OBJECT_0) break;          // client arrived
                    if (!m_insRunning.load()) {              // told to stop
                        CancelIoEx(hPipe, nullptr);
                        break;
                    }
                }
                if (!m_insRunning.load()) {
                    CloseHandle(hEvt);
                    m_insPipe.store(nullptr, std::memory_order_relaxed);
                    CloseHandle(hPipe);
                    break;
                }
            } else {
                CloseHandle(hEvt);
                m_insPipe.store(nullptr, std::memory_order_relaxed);
                CloseHandle(hPipe);
                continue;
            }
            CloseHandle(hEvt);

            char tag[4] = {0};
            bool ok = readExact(hPipe, tag, 4) &&
                      memcmp(tag, PIPE_TAG_INS, 4) == 0;
            DWORD len = 0;
            if (ok) ok = readExact(hPipe, &len, 4) && len <= (1 << 20);
            std::string text;
            if (ok && len > 0) {
                text.resize(len);
                ok = readExact(hPipe, &text[0], len);
            }
            DWORD written = 0;
            if (ok && m_insRunning.load()) {
                std::shared_ptr<InsRequest> req = std::make_shared<InsRequest>();
                req->text = std::move(text);
                Fl::awake(s_handleIns, new std::shared_ptr<InsRequest>(req));
                {
                    std::unique_lock<std::mutex> lk(req->mu);
                    // Wait in bounded slices and abort on teardown so the
                    // destructor's join() never stalls for the full 10 s.
                    while (!req->done && m_insRunning.load()) {
                        req->cv.wait_for(lk, std::chrono::milliseconds(200));
                    }
                }
                if (m_insRunning.load())
                    WriteFile(hPipe, PIPE_TAG_OK, 4, &written, nullptr);
            } else {
                // Include a teardown wake-up reply: the destructor connects
                // to the pipe; the frame (any non-INS tag) ends the accept
                // wait and lets this loop notice m_insRunning == false.
                WriteFile(hPipe, PIPE_TAG_ERR, 4, &written, nullptr);
            }
            FlushFileBuffers(hPipe);
            DisconnectNamedPipe(hPipe);
            m_insPipe.store(nullptr, std::memory_order_relaxed);
            CloseHandle(hPipe);
        }
    });
}

