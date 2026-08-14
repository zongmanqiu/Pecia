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
#include "ToolChrome.h"
#include "ThemeWidgets.h"
#include "ui/TitleBar.h"
#include "ui/WindowFrame.h"
#include "ui/Layout.h"

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
#include <mutex>
#include <thread>

namespace {
const int PAD = 8;
const int BTN_H = gBtnH;   // unified button height
const int BAR_H = gBarH;   // unified bar height: button row
const int BTN_GAP = 10;
} // namespace

std::atomic<LuaToolWindow *> LuaToolWindow::s_active = nullptr;

LuaToolWindow::LuaToolWindow(Config *appCfg,
                             const std::string &peciaPipe,
                             int w, int h, const char *title)
    : Fl_Double_Window(w, h, title)
    , m_appCfg(appCfg)
    , m_peciaPipe(peciaPipe) {
    border(0);                      // draw our own title bar
    m_theme = new Theme();
    m_theme->load(*m_appCfg);
    m_frame = new WindowFrame();

    begin();

    // Custom title bar (single-tab mode shows the window title)
    m_titleBar = new TitleBar(0, 0, w, TITLE_H);
    m_titleBar->box(FL_FLAT_BOX);
    m_titleBar->color(m_theme->colors().bgChrome);
    m_titleBar->setTheme(m_theme);
    m_titleBar->setFontSize(m_appCfg ? m_appCfg->getUiFontSize() : 16);
    m_titleBar->setTabData({TitleBar::TabInfo{title}}, 0);
    m_titleBar->setMultiTab(false);
    m_titleBar->setOnTogglePin([this]() { togglePin(); });
    m_titleBar->setOnMinimize([this]() { iconize(); });
    m_titleBar->setOnToggleMaximize([this]() { toggleMaximize(); });
    m_titleBar->setOnCloseWindow([this]() { cbClose(nullptr, this); });

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
    m_scriptEdit->color(FL_WHITE);
    m_scriptEdit->textcolor(FL_BLACK);

    Fl_Color chromeCol = tc.bgChrome;
    Fl_Color btnFg = tc.textPrimary;
    auto styleBtn = [&](Fl_Button *b) {
        b->color(chromeCol);
        b->selection_color(tc.accentSelection);
        b->labelcolor(btnFg);
        b->labelsize(uiFontSize);
    };
    // Output header row: title on the left (inset BTN_GAP from the left
    // edge), Clear/Run buttons on the right (BTN_GAP from the right edge).
    // The row is BAR_H tall; the buttons are BTN_H, vertically centered.
    const int btnY = outHdrY + (BAR_H - BTN_H) / 2;
    m_outHdr = new Fl_Box(BTN_GAP, outHdrY, cw - BTN_GAP, BAR_H, I18n::get("lua.secoutput"));
    m_outHdr->labelcolor(tc.textPrimary);
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

    int runW = 0, runH = 0, clearW = 0, clearH = 0;
    int saveAsW = 0, saveAsH = 0, helpW = 0, helpH = 0;
    int toAiW = 0, toAiH = 0;
    m_runBtn->measure_label(runW, runH);
    m_clearBtn->measure_label(clearW, clearH);
    m_saveAsBtn->measure_label(saveAsW, saveAsH);
    m_helpBtn->measure_label(helpW, helpH);
    m_toAiBtn->measure_label(toAiW, toAiH);
    runW += 24;      // horizontal label padding
    clearW += 24;
    saveAsW += 24;
    helpW += 24;
    toAiW += 24;
    if (runW < 60) runW = 60;      // never shrink below a tappable width
    if (clearW < 60) clearW = 60;
    if (saveAsW < 60) saveAsW = 60;
    if (helpW < 60) helpW = 60;
    if (toAiW < 60) toAiW = 60;
    int btnRight = w - BTN_GAP;
    m_runBtn->resize(btnRight - runW, btnY, runW, BTN_H);
    m_clearBtn->resize(m_runBtn->x() - BTN_GAP - clearW, btnY, clearW, BTN_H);
    m_toAiBtn->resize(m_clearBtn->x() - BTN_GAP - toAiW, btnY, toAiW, BTN_H);
    m_saveAsBtn->resize(m_toAiBtn->x() - BTN_GAP - saveAsW, btnY, saveAsW, BTN_H);
    m_helpBtn->resize(m_saveAsBtn->x() - BTN_GAP - helpW, btnY, helpW, BTN_H);

    m_outBuf = new Fl_Text_Buffer();
    m_outDisp = new ThemedTextDisplay(x, outY, cw, OUT_H, tc);
    m_outDisp->buffer(m_outBuf);
    m_outDisp->textfont(font);
    m_outDisp->textsize(fontSize);
    m_outDisp->color(FL_WHITE);
    m_outDisp->textcolor(FL_BLACK);

    end();
    resizable(m_scriptEdit);
    color(tc.bgChrome);
    callback(cbClose, this);        // Alt+F4 / WM_CLOSE also saves

    loadScript();
    startInsServer();

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

    // Re-measure and reposition the buttons (labels may have changed width).
    int runW = 0, runH = 0, clearW = 0, clearH = 0;
    int saveAsW = 0, saveAsH = 0, helpW = 0, helpH = 0;
    int toAiW = 0, toAiH = 0;
    if (m_runBtn) m_runBtn->measure_label(runW, runH);
    if (m_clearBtn) m_clearBtn->measure_label(clearW, clearH);
    if (m_saveAsBtn) m_saveAsBtn->measure_label(saveAsW, saveAsH);
    if (m_toAiBtn) m_toAiBtn->measure_label(toAiW, toAiH);
    if (m_helpBtn) m_helpBtn->measure_label(helpW, helpH);
    runW += 24;
    clearW += 24;
    saveAsW += 24;
    helpW += 24;
    toAiW += 24;
    if (runW < 60) runW = 60;
    if (clearW < 60) clearW = 60;
    if (saveAsW < 60) saveAsW = 60;
    if (helpW < 60) helpW = 60;
    if (toAiW < 60) toAiW = 60;
    int btnRight = w() - BTN_GAP;
    if (m_runBtn) m_runBtn->resize(btnRight - runW, m_runBtn->y(), runW, m_runBtn->h());
    if (m_clearBtn) m_clearBtn->resize(m_runBtn->x() - BTN_GAP - clearW, m_clearBtn->y(), clearW, m_clearBtn->h());
    if (m_toAiBtn) m_toAiBtn->resize(m_clearBtn->x() - BTN_GAP - toAiW, m_toAiBtn->y(), toAiW, m_toAiBtn->h());
    if (m_saveAsBtn) m_saveAsBtn->resize(m_toAiBtn->x() - BTN_GAP - saveAsW, m_saveAsBtn->y(), saveAsW, m_saveAsBtn->h());
    if (m_helpBtn) m_helpBtn->resize(m_saveAsBtn->x() - BTN_GAP - helpW, m_helpBtn->y(), helpW, m_helpBtn->h());

    redraw();
}

LuaToolWindow::~LuaToolWindow() {
    Fl::remove_timeout(cursorBlinkCb, this);
    saveScript();
    if (s_active.load() == this) s_active.store(nullptr);
    delete m_localEngine;
    delete m_localBuf;
    delete m_theme;
    delete m_frame;
}

int LuaToolWindow::handle(int event) {
    // Configurable shortcuts first (window ops + console actions): a
    // text field may otherwise consume the key before we see it.
    if (event == FL_KEYDOWN || event == FL_SHORTCUT) {
        if (dispatchShortcut()) return 1;
    }
    // Route title-bar mouse events to the custom TitleBar (border(0)).
    if (event == FL_MOVE || event == FL_PUSH || event == FL_DRAG ||
        event == FL_RELEASE || event == FL_LEAVE) {
        int mx = Fl::event_x();
        int my = Fl::event_y();
        if (m_titleBar) {
            if (event == FL_DRAG || event == FL_RELEASE) {
                int ret = m_titleBar->handle(event);
                if (ret) return ret;
            } else if (my >= 0 && my < TITLE_H && mx >= 0 && mx < w()) {
                int ret = m_titleBar->handle(event);
                if (ret) return ret;
            } else if (event == FL_MOVE) {
                m_titleBar->handle(FL_LEAVE);
            }
        }
    }
    return Fl_Window::handle(event);
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

void LuaToolWindow::togglePin() {
#if defined(_WIN32)
    HWND hwnd = fl_xid(this);
    if (!hwnd) return;
    m_pinned = !m_pinned;
    SetWindowPos(hwnd, m_pinned ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (m_titleBar) m_titleBar->setPinned(m_pinned);
#endif
}

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
    setupToolChrome(this, m_theme);
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
    std::string path = scriptLastPath();
    FILE *f = fopen(path.c_str(), "rb");
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
        std::string path = scriptLastPath();
        FILE *f = fopen(path.c_str(), "wb");
        if (f) {
            fwrite(script, 1, strlen(script), f);
            fclose(f);
        }
        free(script);
    }
}

// Resolve <exe dir>/script.last.txt (UTF-8, no BOM; BOM accepted on load).
std::string LuaToolWindow::scriptLastPath() {
    char exePath[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return "script.last.txt";
    char *slash = strrchr(exePath, '\\');
    if (slash) *slash = 0;
    return std::string(exePath) + "\\script.last.txt";
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
    char exePath[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    char *slash = strrchr(exePath, '\\');
    if (slash) *slash = 0;
    std::wstring scriptDir;
    {
        std::string dir = std::string(exePath) + "\\script";
        CreateDirectoryA(dir.c_str(), nullptr);   // no-op if already exists
        int len = MultiByteToWideChar(CP_UTF8, 0, dir.c_str(), -1, nullptr, 0);
        if (len > 0) {
            scriptDir.resize(len - 1);
            MultiByteToWideChar(CP_UTF8, 0, dir.c_str(), -1, &scriptDir[0], len);
        }
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
                std::string utf8;
                {
                    int wlen = (int)wcslen(path);
                    int blen = WideCharToMultiByte(CP_UTF8, 0, path, wlen,
                                                   nullptr, 0, nullptr, nullptr);
                    utf8.resize(blen);
                    WideCharToMultiByte(CP_UTF8, 0, path, wlen, &utf8[0], blen,
                                        nullptr, nullptr);
                }
                FILE *f = fopen(utf8.c_str(), "wb");
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
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    char *slash = strrchr(exePath, '\\');
    if (slash) *slash = 0;
    std::string doc = std::string(exePath) + "\\script\\lua_api.txt";
    ShellExecuteA(nullptr, "open", doc.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
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

void LuaToolWindow::toggleMaximize() {
    if (!m_frame) return;
    bool maxed = m_frame->toggleMaximize(this);
    if (m_titleBar) m_titleBar->setMaximized(maxed);
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
    LuaParamSet decls = luaParseParams(code, langCode);
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
} // namespace

void LuaToolWindow::s_handleIns(void *data) {
    InsRequest *r = static_cast<InsRequest *>(data);
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
    std::thread([this]() {
        for (;;) {
            HANDLE hPipe = CreateNamedPipeA(PIPE_LUA_TOOL, PIPE_ACCESS_DUPLEX,
                                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                            1, 4096, 4096, 0, nullptr);
            if (hPipe == INVALID_HANDLE_VALUE) return;
            if (!ConnectNamedPipe(hPipe, nullptr) &&
                GetLastError() != ERROR_PIPE_CONNECTED) {
                CloseHandle(hPipe);
                continue;
            }
            char tag[4] = {0};
            DWORD got = 0;
            bool ok = ReadFile(hPipe, tag, 4, &got, nullptr) && got == 4 &&
                      memcmp(tag, PIPE_TAG_INS, 4) == 0;
            DWORD len = 0;
            if (ok) ok = ReadFile(hPipe, &len, 4, &got, nullptr) && got == 4 &&
                        len <= (1 << 20);
            std::string text;
            if (ok && len > 0) {
                text.resize(len);
                ok = ReadFile(hPipe, &text[0], len, &got, nullptr) && got == len;
            }
            if (ok) {
                InsRequest req;
                req.text = std::move(text);
                Fl::awake(s_handleIns, &req);
                {
                    std::unique_lock<std::mutex> lk(req.mu);
                    req.cv.wait_for(lk, std::chrono::seconds(10),
                                    [&req]() { return req.done; });
                }
                WriteFile(hPipe, PIPE_TAG_OK, 4, &got, nullptr);
            } else {
                WriteFile(hPipe, PIPE_TAG_ERR, 4, &got, nullptr);
            }
            FlushFileBuffers(hPipe);
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
        }
    }).detach();
}

