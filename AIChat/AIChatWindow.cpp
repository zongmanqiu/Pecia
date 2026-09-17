// AIChatWindow.cpp - standalone AI chat window with conversation memory.
// Same custom title bar / theme as the main window (border(0) + TitleBar).
#include "AIChatWindow.h"
#include "AIChatSettingsDialog.h"

#include <windows.h>   // push-pipe polling (CreateFileA/PeekNamedPipe)

#include "core/AiApiClient.h"
#include "core/Config.h"
#include "core/I18n.h"
#include "core/Theme.h"
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
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Input.H>
#include <FL/x.H>

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <thread>
#include <windows.h>

// UTF-8 -> UTF-16：宽字符 Win32 API（CreateProcessW 等）入参转换，
// exe/管道名含中文时 ANSI 版会失效。
static std::wstring widen(const std::string &utf8) {
    if (utf8.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], n);
    while (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

namespace {

const int BTN_H = gBtnH;   // unified button height
const int BAR_H = gBarH;   // unified bar height: button row
const int BTN_GAP = 8;

const size_t kMaxContextTurns = 50;

const char *kSystemPrompt =    "你是 Pecia 的 AI 助手。你既能帮助用户撰写、润色、修改各种文本，也能为文本编辑器 "
    "Pecia 编写 Lua 脚本。Pecia 内置 Lua 5.5，脚本通过全局表 editor 操作当前打开的文档，"
    "可用 API：editor:get_selection() 返回当前选中文本（无选中返回空串）；"
    "editor:replace_selection(text) 用 text 替换选中文本（无选中时在光标处插入并选中新文本，"
    "再次调用会替换上一次插入的内容）；editor:get_cursor() 返回光标位置（字节偏移）；"
    "editor:set_cursor(pos) 移动光标并清除选择；print(...) 输出调试信息。"
    "交互 API：win:message(text) 弹出提示框；win:question(text) 弹是/否询问并返回布尔值；"
    "win:clipboard_get() 读取剪贴板；win:clipboard_set(text) 写入剪贴板；win:open(path_or_url) 用系统默认程序打开文件或网址。"
    "标准库全部可用：string、table、math、os、utf8、coroutine、io（文件读写）、require/package（模块加载）、debug。"
    "文档为 UTF-8 编码，字符串长度按字节计算，处理中文时可用 utf8 库按字符遍历。"
    "规则（重要）：只有当用户消息中出现了『脚本』或『lua』字样（不区分大小写）时，"
    "才编写并输出 Lua 代码——此时只输出纯净的 Lua 代码，不要 markdown 围栏、不要解释文字；"
    "用户消息没有提到这两个词时，绝不输出代码，直接运用你的文本能力完成任务"
    "（数据清洗、去重、整理、翻译、润色、格式化等），把处理结果作为普通文本回复。"
    "请记住对话上下文，如果用户提到之前的脚本或文本，据此修正。"
    "如果用户消息末尾带有『### target text ###』标记，其后内容就是用户在编辑器中选中的文本，"
    "请按用户要求直接处理这段文本（例如翻译、润色、去重等），把处理结果作为回复内容返回。";

} // namespace

std::atomic<AIChatWindow *> AIChatWindow::s_active = nullptr;

AIChatWindow::AIChatWindow(Config *appCfg,
                           const std::string &peciaPipe,
                           int w, int h, const char *title)
    : DialogBase(appCfg, w, h, title, appCfg ? appCfg->getUiFontSize() : 16, ToolWindow)
    , m_appCfg(appCfg)
    , m_peciaPipe(peciaPipe) {
    begin();
    initShell(title);
    applyShortcuts();

    // Chat pane + input pane are edge-to-edge (x=0, full width) matching
    // the main Pecia window. Only the bottom button row is inset: the
    // attach checkbox BTN_GAP from the left edge, the Send button BTN_GAP
    // from the right edge, and the row BTN_GAP above the bottom edge.
    const int x = 0;
    const int cw = w;
    const ThemeColors &tc = m_theme->colors();

    int font = FL_COURIER;
    int fontSize = 13;
    if (m_appCfg) m_appCfg->getFont(font, fontSize);
    const int uiFontSize = m_appCfg ? m_appCfg->getUiFontSize() : 16;

    // Bottom-up layout (no overlaps), mirroring the Lua console:
    //   input    inputY..inputY+140   (flush to the bottom edge)
    //   buttons  btnY..btnY+BAR_H     (attach checkbox left, buttons right;
    //            flush against input and chat, exactly BAR_H tall like
    //            every other bar - no extra gaps above/below)
    //   chat     fills the rest
    const int inputY = h - 140;
    const int btnY = inputY - BAR_H;
    // Buttons are BTN_H tall, vertically centered in the BAR_H row.
    const int buttonY = btnY + (BAR_H - BTN_H) / 2;

    m_chatBuf = new Fl_Text_Buffer();
    m_chatDisp = new ThemedTextDisplay(x, TITLE_H, cw, btnY - TITLE_H, tc);
    m_chatDisp->buffer(m_chatBuf);
    m_chatDisp->textsize(fontSize);
    m_chatDisp->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);   // auto wrap like the main editor

    m_attachSel = new Fl_Check_Button(BTN_GAP, buttonY, 150, BTN_H, I18n::get("chat.attachsel"));
    m_attachSel->labelsize(uiFontSize);   // same size as the rest of the UI
    m_attachSel->labelcolor(tc.text1);
    m_attachSel->color(tc.background2);  // unchecked box blends with chrome bg
    m_attachSel->box(FL_NO_BOX);            // no outer border on the text area
    m_attachSel->down_box(FL_BORDER_BOX);   // square checkbox glyph
    m_attachSel->selection_color(tc.highlight2);  // clicked -> filled box
    m_attachSel->tooltip(I18n::get("chat.attachseltip"));

    m_inputBuf = new Fl_Text_Buffer();
    bool hl = m_appCfg ? m_appCfg->getHighlightCurrentLine() : true;
    m_inputEdit = new ThemedTextEditor(x, inputY, cw, 140, tc, hl);
    m_inputEdit->buffer(m_inputBuf);
    m_inputEdit->textsize(fontSize);
    m_inputEdit->textfont(font);
    m_inputEdit->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);

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
    // Right-aligned buttons: [Settings] [清除] [发送到文档] [发送].
    // fitButtonRow sizes each to its label (auto-width) and right-aligns.
    m_sendBtn = new HoverButton(0, buttonY, 0, BTN_H, I18n::get("chat.send"));
    m_sendBtn->callback(cbSend, this);
    styleBtn(m_sendBtn);
    m_toDocBtn = new HoverButton(0, buttonY, 0, BTN_H, I18n::get("chat.todoc"));
    m_toDocBtn->callback(cbToDoc, this);
    styleBtn(m_toDocBtn);
    m_toLuaBtn = new HoverButton(0, buttonY, 0, BTN_H, I18n::get("chat.tolua"));
    m_toLuaBtn->callback(cbToLua, this);
    styleBtn(m_toLuaBtn);
    m_settingsBtn = new HoverButton(0, buttonY, 0, BTN_H, I18n::get("chat.settings"));
    m_settingsBtn->callback(cbSettings, this);
    styleBtn(m_settingsBtn);
    m_clearBtn = new HoverButton(0, buttonY, 0, BTN_H, I18n::get("chat.clear"));
    m_clearBtn->callback(cbClear, this);
    styleBtn(m_clearBtn);
    // Left-to-right: [API][Clear][To Lua][To Pecia][Enter] (Enter rightmost).
    fitButtonRow({m_settingsBtn, m_clearBtn, m_toLuaBtn, m_toDocBtn, m_sendBtn},
                 w, buttonY + BTN_H / 2, BTN_GAP, BTN_GAP);

    end();
    resizable(m_chatDisp);
    color(tc.background2);
    finalizeShell();
    callback(cbClose, this);        // Alt+F4 / WM_CLOSE

    // Start the cursor blink timer (same cadence as the main window).
    Fl::add_timeout(0.5, cursorBlinkCb, this);

    // Push-channel poll timer: the main window can send text here (e.g.
    // "Send to AI" from the editor's context menu). Derive the push pipe
    // name from the lua pipe argument ("pecia-lua-" -> "pecia-ai-push-");
    // when started without a pipe argument (manual launch) this stays
    // empty and the poll is a no-op.
    size_t pos = m_peciaPipe.find("pecia-lua-");
    if (pos != std::string::npos) {
        m_pushName = m_peciaPipe;
        m_pushName.replace(pos, 10, "pecia-ai-push-");
    }
    Fl::add_timeout(0.15, pushPollCb, this);
}

// Re-read I18n strings after the settings.ini watcher detects a language
// change. Updates button labels, the attach-selection checkbox (label +
// tooltip), and the title bar. Button widths are re-measured because
// translations have different lengths.
void AIChatWindow::retheme() {
    if (!m_appCfg) return;
    const Theme &th = DialogBase::retheme(*m_appCfg);   // reload owned theme + shell chrome
    const ThemeColors &tc = th.colors();

    // DialogBase::retheme() resets the window color to background1 (work
    // area). This tool window is chrome-themed, so restore the frame color
    // used at construction (AIChatWindow ctor). Without this the button-row
    // strip turns white after any theme/language/shortcut change.
    color(tc.background2);

    // Chat display + input editor + checkbox + buttons pick up the palette.
    if (m_chatDisp) {
        m_chatDisp->color(tc.background1);
        m_chatDisp->textcolor(tc.text1);
        m_chatDisp->selection_color(tc.highlight2);
    }
    if (m_inputEdit) {
        m_inputEdit->color(tc.background1);
        m_inputEdit->textcolor(tc.text1);
        m_inputEdit->selection_color(tc.highlight2);
        m_inputEdit->cursor_color(tc.text1);
    }
    if (m_attachSel) {
        m_attachSel->labelcolor(tc.text1);
        m_attachSel->color(tc.background2);
        m_attachSel->selection_color(tc.highlight2);
    }
    for (auto *b : { m_sendBtn, m_toDocBtn, m_toLuaBtn, m_clearBtn, m_settingsBtn }) {
        if (b) b->labelcolor(tc.text1);
    }
    redraw();
}

void AIChatWindow::refreshLabels() {
    if (m_sendBtn)      m_sendBtn->copy_label(I18n::get("chat.send"));
    if (m_toDocBtn)     m_toDocBtn->copy_label(I18n::get("chat.todoc"));
    if (m_toLuaBtn)     m_toLuaBtn->copy_label(I18n::get("chat.tolua"));
    if (m_clearBtn)     m_clearBtn->copy_label(I18n::get("chat.clear"));
    if (m_settingsBtn)  m_settingsBtn->copy_label(I18n::get("chat.settings"));
    if (m_attachSel) {
        m_attachSel->copy_label(I18n::get("chat.attachsel"));
        m_attachSel->tooltip(I18n::get("chat.attachseltip"));
    }
    if (m_titleBar) {
        m_titleBar->setTabData({TitleBar::TabInfo{I18n::get("chat.title")}}, 0);
    }

    // Re-fit the auto-width button row after labels change (translations
    // differ in length). Buttons already carry their new labels above;
    // re-center on the row they already sit in.
    int rowCenter = m_sendBtn ? m_sendBtn->y() + m_sendBtn->h() / 2 : 0;
    fitButtonRow({m_settingsBtn, m_clearBtn, m_toLuaBtn, m_toDocBtn, m_sendBtn},
                 w(), rowCenter, BTN_GAP, BTN_GAP);

    redraw();
}

// Window resize: FLTK scales children that overlap the resizable pane
// proportionally, which stretches the fixed-width button row and can
// push the edge-to-edge panes out of sync with the window width.
// Re-assert the intended geometry after every resize: panes span the
// full width, the attach checkbox stays inset from the left edge, and
// the button row is re-fitted (each button keeps its label width,
// plan.txt #1) right-aligned inside the current window width.
void AIChatWindow::resize(int X, int Y, int W, int H) {
    DialogBase::resize(X, Y, W, H);
    if (!m_chatDisp) return;
    m_chatDisp->resize(0, m_chatDisp->y(), W, m_chatDisp->h());
    if (m_inputEdit) m_inputEdit->resize(0, m_inputEdit->y(), W, m_inputEdit->h());
    if (m_attachSel) m_attachSel->position(BTN_GAP, m_attachSel->y());
    if (m_sendBtn) {
        int rowCenter = m_sendBtn->y() + m_sendBtn->h() / 2;
        fitButtonRow({m_settingsBtn, m_clearBtn, m_toLuaBtn, m_toDocBtn, m_sendBtn},
                     W, rowCenter, BTN_GAP, BTN_GAP);
    }
    redraw();
}

AIChatWindow::~AIChatWindow() {
    Fl::remove_timeout(cursorBlinkCb, this);
    Fl::remove_timeout(pushPollCb, this);   // cancel the pending push poll (dangling this otherwise)
    if (s_active.load() == this) s_active.store(nullptr);
}

int AIChatWindow::handle(int event) {
    // Configurable shortcuts first (window ops + chat actions): a
    // text field may otherwise consume the key before we see it.
    if (event == FL_KEYDOWN || event == FL_SHORTCUT) {
        if (dispatchShortcut()) return 1;
    }
    // Title-bar event forwarding and the outer border are handled by
    // DialogBase (shared by every window).
    return DialogBase::handle(event);
}

int AIChatWindow::dispatchShortcut() {
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
void AIChatWindow::applyShortcuts() {
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
    bind("win.button.run", FL_Enter, FL_CTRL, [this]() { doSend(); });
    bind("win.button.clear", 0, 0, [this]() { cbClear(nullptr, this); });
    bind("win.button.todoc", 0, 0, [this]() { cbToDoc(nullptr, this); });
    bind("win.button.tolua", 0, 0, [this]() { cbToLua(nullptr, this); });
    bind("win.button.settings", 0, 0, [this]() { cbSettings(nullptr, this); });
}

void AIChatWindow::minimizeWindow() {
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

// Push-channel poll: connect to the main process's push pipe (retrying
// until it exists), then drain 4-byte-length-prefixed frames. A frame
// is sent as an AI message immediately (auto-send) and the main window's
// selection is cleared so a later reply insertion replaces nothing.
void AIChatWindow::pushPollCb(void *data) {
    auto *self = static_cast<AIChatWindow *>(data);
    if (self) self->pollPushPipe();
    Fl::add_timeout(0.15, pushPollCb, data);
}

void AIChatWindow::pollPushPipe() {
    if (m_pushName.empty()) return;
    if (m_pushPipe == nullptr) {
        HANDLE h = CreateFileA(m_pushName.c_str(),
                               GENERIC_READ | GENERIC_WRITE,
                               0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;   // server not up yet
        m_pushPipe = h;
    }
    DWORD avail = 0;
    if (!PeekNamedPipe(static_cast<HANDLE>(m_pushPipe), nullptr, 0, nullptr, &avail, nullptr)) {
        CloseHandle(static_cast<HANDLE>(m_pushPipe));   // disconnected: reconnect next poll
        m_pushPipe = nullptr;
        return;
    }
    if (avail < 4) return;
    // Read the 4-byte LE length, looping over partial reads rather than
    // treating a short single read as a disconnect.
    unsigned char lenBytes[4];
    {
        size_t got = 0;
        while (got < 4) {
            DWORD n = 0;
            if (!ReadFile(static_cast<HANDLE>(m_pushPipe), lenBytes + got, 4 - (DWORD)got,
                          &n, nullptr) || n == 0) {
                CloseHandle(static_cast<HANDLE>(m_pushPipe));   // peer gone
                m_pushPipe = nullptr;
                return;
            }
            got += n;
        }
    }
    DWORD len = ((DWORD)lenBytes[0]) | ((DWORD)lenBytes[1] << 8) |
                ((DWORD)lenBytes[2] << 16) | ((DWORD)lenBytes[3] << 24);
    if (len == 0 || len > (16 << 20)) {
        // Invalid frame length, and the 4-byte header is already consumed:
        // the push stream is permanently desynchronized. Drop the connection
        // and reconnect on the next poll - previously this just returned,
        // which misaligned every later frame (garbage / dropped pushes).
        CloseHandle(static_cast<HANDLE>(m_pushPipe));
        m_pushPipe = nullptr;
        return;
    }
    std::string msg(len, '\0');
    char *p = &msg[0];
    DWORD remaining = len;
    while (remaining > 0) {
        DWORD n = 0;
        if (!ReadFile(static_cast<HANDLE>(m_pushPipe), p, remaining, &n, nullptr) || n == 0) break;
        p += n;
        remaining -= n;
    }
    if (remaining > 0) {
        CloseHandle(static_cast<HANDLE>(m_pushPipe));
        m_pushPipe = nullptr;
        return;
    }
    handlePushMessage(msg);
}

void AIChatWindow::handlePushMessage(const std::string &text) {
    if (text.empty() || !m_inputBuf) return;
    m_inputBuf->text(text.c_str());
    doSend();                       // auto-send; no-op when busy or unconfigured
    pipeSend(m_peciaPipe, PIPE_TAG_CLRSEL, "");   // clear selection, keep text
}

void AIChatWindow::cursorBlinkCb(void *data) {
    auto *self = static_cast<AIChatWindow *>(data);
    if (!self) return;
    self->m_cursorVisible = !self->m_cursorVisible;
    // Only blink when the input editor has keyboard focus.
    if (self->m_inputEdit && Fl::focus() == self->m_inputEdit &&
        self->shown()) {
        self->m_inputEdit->show_cursor(self->m_cursorVisible ? 1 : 0);
        self->m_inputEdit->damage(FL_DAMAGE_CHILD);
    }
    Fl::add_timeout(0.5, cursorBlinkCb, self);
}

void AIChatWindow::openDialog() {
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
    if (m_inputEdit) {
        m_inputEdit->take_focus();
        m_inputEdit->insert_position(m_inputBuf->length());
    }
}

void AIChatWindow::setStatus(const char *text) {
    // Status messages are shown in the chat pane instead of a dedicated
    // status line (which the user asked to remove). They are NOT added to
    // the conversation history so they never reach the AI as context.
    if (!m_chatBuf || !text || !*text) return;
    std::string block = std::string("── ") + I18n::get("chat.ai") + " ──\n" +
                        text + "\n\n";
    int pos = m_chatBuf->length();
    m_chatBuf->insert(pos, block.c_str(), (int)block.size());
    m_chatDisp->insert_position(m_chatBuf->length());
    m_chatDisp->show_insert_position();
}

void AIChatWindow::appendTurn(const char *roleLabel, const std::string &text) {
    if (!m_chatBuf) return;
    // One-line header: "── hh:mm:ss role ──" (time + role merged,
    // double-dash kept as the visual message boundary).
    char ts[32];
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(ts, sizeof(ts), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    std::string block = std::string("── ") + ts + " " + roleLabel +
                        " ──\n" + text + "\n\n";
    int pos = m_chatBuf->length();
    m_chatBuf->insert(pos, block.c_str(), (int)block.size());
    m_chatDisp->insert_position(m_chatBuf->length());
    m_chatDisp->show_insert_position();
}

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

void AIChatWindow::cbSend(Fl_Widget *, void *data) {
    static_cast<AIChatWindow *>(data)->doSend();
}
void AIChatWindow::cbToDoc(Fl_Widget *, void *data) {
    AIChatWindow *self = static_cast<AIChatWindow *>(data);
    self->sendTo(self->m_peciaPipe, "chat.todocfail");
}

// Extract the first fenced Lua code block (```lua ... ``` or a plain
// ``` ... ``` block) from a reply. Returns "" when none is found.
static std::string extractLuaBlock(const std::string &text) {
    const char *langs[] = { "```lua", "```Lua", "```LUA" };
    size_t start = std::string::npos;
    size_t markerLen = 0;
    for (const char *m : langs) {
        size_t p = text.find(m);
        if (p != std::string::npos && (start == std::string::npos || p < start)) {
            start = p;
            markerLen = strlen(m);
        }
    }
    if (start == std::string::npos) {   // fall back to a plain ``` fence
        size_t p = text.find("```");
        if (p == std::string::npos) return {};
        start = p;
        markerLen = 3;
    }
    size_t begin = start + markerLen;
    while (begin < text.size() && (text[begin] == '\r' || text[begin] == '\n'))
        ++begin;
    size_t end = text.find("```", begin);
    if (end == std::string::npos) return {};
    std::string code = text.substr(begin, end - begin);
    while (!code.empty() && (code.back() == '\r' || code.back() == '\n' ||
                             code.back() == ' '))
        code.pop_back();
    return code;
}

// 把 AI 回复中的 Lua 代码块（去掉 ```lua 包裹）发送到 Lua 控制台的
// 脚本编辑器（PIPE_LUA_TOOL），方便直接运行调试。Lua 控制台未打开时
// 自动启动它（单实例 Mutex 兜底），随后定时重试发送。
namespace {
struct PendingToLua {
    AIChatWindow *self;
    std::string  code;
    int          tries = 0;
};
} // namespace

void AIChatWindow::cbToLua(Fl_Widget *, void *data) {
    AIChatWindow *self = static_cast<AIChatWindow *>(data);
    if (self->m_lastReply.empty()) {
        self->setStatus(I18n::get("chat.noreply"));
        return;
    }
    std::string code = extractLuaBlock(self->m_lastReply);
    if (code.empty()) {
        self->setStatus(I18n::get("chat.noluacode"));
        return;
    }
    PipeReply rep = pipeSend(PIPE_LUA_TOOL, PIPE_TAG_INS, code);
    if (rep.peerConnected && rep.ok) {
        self->setStatus(I18n::get("chat.senttolua"));
        return;
    }
    // Lua console not open: launch it, then retry a few times while it starts.
    self->launchLuaTool();
    auto *p = new PendingToLua{self, std::move(code), 0};
    Fl::add_timeout(0.3, retryToLuaCb, p);
}

void AIChatWindow::retryToLuaCb(void *data) {
    PendingToLua *p = static_cast<PendingToLua *>(data);
    PipeReply rep = pipeSend(PIPE_LUA_TOOL, PIPE_TAG_INS, p->code);
    if (rep.peerConnected && rep.ok) {
        p->self->setStatus(I18n::get("chat.senttolua"));
        delete p;
        return;
    }
    if (++p->tries >= 10) {
        delete p;   // give up silently: the window opens itself - if it
        return;     // still isn't there after ~3s something is very wrong
    }
    Fl::add_timeout(0.3, retryToLuaCb, p);
}

void AIChatWindow::launchLuaTool() {
    // 宽字符路径：exe 位于中文目录时 GetModuleFileNameA/CreateProcessA 会失效
    wchar_t exePathW[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePathW, MAX_PATH)) return;
    wchar_t *slash = wcsrchr(exePathW, L'\\');
    if (!slash) return;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - exePathW), L"PeciaLua.exe");
    if (GetFileAttributesW(exePathW) == INVALID_FILE_ATTRIBUTES) return;
    std::wstring cmd = std::wstring(L"\"") + exePathW + L"\" \"" +
                       widen(m_peciaPipe) + L"\" " + widen(I18n::currentCode());
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(exePathW, &cmd[0], nullptr, nullptr, FALSE, 0,
                       nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}
void AIChatWindow::cbClear(Fl_Widget *, void *data) {
    AIChatWindow *self = static_cast<AIChatWindow *>(data);
    if (!self) return;

    // Cancel any in-flight AI request.
    if (self->m_busy) {
        if (self->m_cancelRequested) self->m_cancelRequested->store(true);
        // Stop the timer and restore the send button label.
        self->m_sendTime = 0;
        Fl::remove_timeout(s_timerCb, self);
        if (self->m_sendBtn) self->m_sendBtn->copy_label(I18n::get("chat.send"));
        self->m_busy = false;
    }

    self->m_history.clear();
    self->m_chatBuf->text("");
}
void AIChatWindow::cbSettings(Fl_Widget *, void *data) {
    AIChatWindow *self = static_cast<AIChatWindow *>(data);
    if (!self) return;
    AIChatSettingsDialog dlg(self->m_appCfg, self->m_theme,
                             self->m_appCfg ? self->m_appCfg->getUiFontSize() : 16);
    dlg.runModal();
}

void AIChatWindow::cbClose(Fl_Widget *, void *data) {
    AIChatWindow *self = static_cast<AIChatWindow *>(data);
    self->hide();
}

// ---------------------------------------------------------------------------
// Send / receive
// ---------------------------------------------------------------------------

// Read script\lua_api.txt next to the exe (CMake copies
// main/script/scripts/* into build/script/). Read fresh on every send so
// doc edits apply immediately; "" when missing. Uses wide-char paths so a
// non-ASCII install directory (Chinese, etc.) works - GetModuleFileNameA +
// fopen_s break on those.
std::string AIChatWindow::readLuaApiHelp() {
    wchar_t apiPath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, apiPath, MAX_PATH) == 0) return {};
    wchar_t *apiSlash = wcsrchr(apiPath, L'\\');
    if (!apiSlash) return {};
    wcscpy_s(apiSlash + 1, MAX_PATH - (apiSlash + 1 - apiPath), L"script\\lua_api.txt");
    FILE *f = nullptr;
    if (_wfopen_s(&f, apiPath, L"rb") != 0 || !f) return {};
    std::string out;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n > 0) {
        fseek(f, 0, SEEK_SET);
        out.resize((size_t)n);
        size_t got = fread(&out[0], 1, (size_t)n, f);
        out.resize(got);
    }
    fclose(f);
    return out;
}

void AIChatWindow::doSend() {
    if (m_busy) return;
    char keyBuf[512], modelBuf[256], endpointBuf[512];
    if (m_appCfg) {
        m_appCfg->getAiKey(keyBuf, sizeof(keyBuf), "");
        m_appCfg->getAiModel(modelBuf, sizeof(modelBuf), "glm-4-flash");
        m_appCfg->getAiEndpoint(endpointBuf, sizeof(endpointBuf),
            "https://open.bigmodel.cn/api/paas/v4/chat/completions");
    } else {
        keyBuf[0] = modelBuf[0] = endpointBuf[0] = 0;
    }
    const char *key = keyBuf;
    const char *model = modelBuf;
    const char *endpoint = endpointBuf;
    const char *prompt = m_inputBuf->text();
    if (!key || !*key) { free((void *)prompt); setStatus(I18n::get("ai.keyhint")); return; }
    if (!model || !*model || !endpoint || !*endpoint) { free((void *)prompt); setStatus(I18n::get("ai.error")); return; }
    if (!prompt || !*prompt) { free((void *)prompt); setStatus(I18n::get("ai.noprompt")); return; }

    std::string message = prompt;
    free((void *)prompt);

    if (m_attachSel && m_attachSel->value()) {
        PipeReply rep = pipeSend(m_peciaPipe, PIPE_TAG_GETSEL, "");
        if (!rep.peerConnected) {
            setStatus(I18n::get("chat.attachfail"));
            return;   // keep the checkbox checked
        }
        if (!rep.ok) {
            setStatus(rep.payload.empty() ? I18n::get("ai.nosel") : rep.payload.c_str());
            return;   // keep the checkbox checked
        }
        message += "\n\n### target text ###\n";
        message += rep.payload;
        // Consume the selection: clear it in the main window so the AI
        // reply sent back via To Document inserts at the cursor instead of
        // replacing the original selection.
        pipeSend(m_peciaPipe, PIPE_TAG_CLRSEL, "");
        m_attachSel->value(0);   // single use: uncheck after a successful send
    }

    m_history.push_back({"user", message});
    appendTurn(I18n::get("chat.you"), message);
    m_inputBuf->text("");

    std::vector<AiChatMessage> msgs;
    std::string sysPrompt = kSystemPrompt;
    // Language hint: tell the model the user's UI language (display name
    // from the lang file's `name` key + the code suffix), so it replies
    // in the right language even when the message text is in another
    // language (e.g. pasted error messages). Falls back to the bare code
    // when the lang file has an empty/absent `name`.
    const char *langCode = I18n::currentCode();
    std::string langName = I18n::getOr("name", "");
    sysPrompt += "\n\n";
    if (!langName.empty()) {
        sysPrompt += "The user's interface language is ";
        sysPrompt += langName;
        sysPrompt += " (";
        sysPrompt += langCode;
        sysPrompt += ").";
    } else {
        sysPrompt += "The user's interface language code is ";
        sysPrompt += langCode;
        sysPrompt += ".";
    }
    sysPrompt +=
        " Always respond in that language, unless the user explicitly "
        "asks otherwise.\n";
    // Read the API doc fresh on every send (never compiled in, never
    // cached): edits to lua_api.txt apply immediately.
    std::string luaHelp = readLuaApiHelp();
    if (!luaHelp.empty()) {
        sysPrompt +=
            "\n\n##### 如果我们本轮对话需要写 lua 代码，请参考以下帮助信息；"
            "如果不需要，则忽略此处往后的所有内容 #####\n"
            "##### If this conversation needs to write Lua code, please refer "
            "to the help below; otherwise ignore everything from here on #####\n";
        sysPrompt += luaHelp;
    }
    msgs.push_back({"system", sysPrompt});
    size_t start = m_history.size() > kMaxContextTurns
                       ? m_history.size() - kMaxContextTurns : 0;
    for (size_t i = start; i < m_history.size(); ++i) {
        msgs.push_back({m_history[i].first.c_str(), m_history[i].second});
    }

    m_busy = true;
    // Start the elapsed-time display on the send button.
    m_sendTime = time(nullptr);
    Fl::add_timeout(1.0, s_timerCb, this);
    // No status banner while waiting - the user sees the request is in
    // flight; a "Generating..." line would just pollute the chat pane.

    s_active.store(this);
    Result *r = new Result();
    r->owner = this;
    // Reset cancel flag for this new request. The flag is heap-allocated
    // and shared with the worker thread (shared_ptr), so it stays valid
    // even if this window is destroyed while the request is in flight.
    m_cancelRequested = std::make_shared<std::atomic<bool>>(false);
    std::thread([key = std::string(key), model = std::string(model),
                 endpoint = std::string(endpoint), msgs, r,
                 cancelFlag = m_cancelRequested]() {
        AiChatResult res = aiChatCompletion(key, model, msgs, endpoint, 300000, cancelFlag.get());
        r->ok = res.ok;
        r->content = res.content;
        r->error = res.error;
        Fl::awake(s_handleResult, r);
    }).detach();
}

void AIChatWindow::s_handleResult(void *data) {
    Result *r = static_cast<Result *>(data);
    AIChatWindow *self = r->owner;
    if (self == s_active.load() && self) {
        self->m_busy = false;
        // Stop the elapsed-time display, restore the button label.
        self->m_sendTime = 0;
        if (self->m_sendBtn)
            self->m_sendBtn->copy_label(I18n::get("chat.send"));
        if (r->ok) {
            self->m_lastReply = r->content;
            self->m_history.push_back({"assistant", r->content});
            self->appendTurn(I18n::get("chat.ai"), r->content);
            // No "Sent" status line - it would just pollute the chat pane.
        } else {
            self->setStatus(r->error.empty() ? I18n::get("ai.error") : r->error.c_str());
        }
    }
    delete r;
}

void AIChatWindow::sendTo(const std::string &pipe, const char *failKey) {
    if (m_lastReply.empty()) {
        setStatus(I18n::get("chat.noreply"));
        return;
    }
    PipeReply rep = pipeSend(pipe, PIPE_TAG_INS, m_lastReply);
    if (rep.peerConnected && rep.ok) {
        setStatus(I18n::get("chat.inserted"));
    } else {
        setStatus(I18n::get(failKey));
    }
}

void AIChatWindow::s_timerCb(void *data) {
    AIChatWindow *self = static_cast<AIChatWindow *>(data);
    if (!self || self->m_sendTime == 0) return;   // reply arrived, stop
    time_t elapsed = time(nullptr) - self->m_sendTime;
    char buf[32];
    if (elapsed < 60)
        snprintf(buf, sizeof(buf), "%s (%llds)",
                 I18n::get("chat.send"), (long long)elapsed);
    else
        snprintf(buf, sizeof(buf), "%s (%lldm%02llds)",
                 I18n::get("chat.send"),
                 (long long)(elapsed / 60), (long long)(elapsed % 60));
    if (self->m_sendBtn) self->m_sendBtn->copy_label(buf);
    Fl::repeat_timeout(1.0, s_timerCb, data);
}
