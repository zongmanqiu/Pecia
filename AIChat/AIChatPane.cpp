// AIChatPane.cpp - AI conversation pane docked in the main window's right
// zone (m037). The logic is unchanged from the old AIChatPane; what is gone
// is the window shell: no title bar, no native chrome, no taskbar button.
#include "AIChat/AIChatPane.h"
#include "AiChatLog.h"

#include "core/AiApiClient.h"
#include "core/Config.h"
#include "core/I18n.h"
#include "core/Theme.h"
#include "ui/ToolHost.h"
#include "ThemeWidgets.h"
#include "ui/Layout.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>
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


namespace {

const int BTN_H = gBtnH;   // unified button height
const int BAR_H = gBarH;   // unified bar height: button row
const int BTN_GAP = 8;

// (r042d: the context depth is user-configurable via
// Config::kDefaultAiContextTurns / the AI settings page - see doSend.)

const char *kSystemPrompt =    "你是 Pecia 的 AI 助手，帮助用户撰写、润色、翻译和整理各种文本。"
    "请直接运用你的文本能力完成任务（数据清洗、去重、整理、翻译、润色、格式化等），"
    "把处理结果作为普通文本回复。Pecia 的自动化（Lua 脚本、正则、宿主操作）由外部 "
    "AI 通过 MCP 接入完成，不属于本对话窗口的职责；除非用户主动要求，不要输出代码。"
    "文档为 UTF-8 编码。"
    "请记住对话上下文，如果用户提到之前的内容，据此修正。"
    "如果用户消息末尾带有『### target text ###』标记，其后内容就是用户在编辑器中选中的文本，"
    "请按用户要求直接处理这段文本（例如翻译、润色、去重等），把处理结果作为回复内容返回。";

} // namespace

std::atomic<AIChatPane *> AIChatPane::s_active = nullptr;

AIChatPane::AIChatPane(Config *appCfg, const Theme *theme, int w, int h)
    : Fl_Group(0, 0, w, h)
    , m_appCfg(appCfg)
    , m_theme(theme) {
    begin();
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

    // Bottom-up layout (no overlaps), same shell as every other window:
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
    m_chatDisp = new ThemedTextDisplay(x, 0, cw, btnY, tc);
    m_chatDisp->buffer(m_chatBuf);
    m_chatDisp->textsize(fontSize);
    m_chatDisp->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);   // auto wrap like the main editor

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
    // Right-aligned buttons: [Clear] [Send] - m040 dropped the API, To-Document
    // and attach-selection entries. fitButtonRow sizes each to its label.
    m_sendBtn = new HoverButton(0, buttonY, 0, BTN_H, I18n::get("chat.send"));
    m_sendBtn->callback(cbSend, this);
    styleBtn(m_sendBtn);
    m_clearBtn = new HoverButton(0, buttonY, 0, BTN_H, I18n::get("chat.clear"));
    m_clearBtn->callback(cbClear, this);
    styleBtn(m_clearBtn);
    // Left-to-right: [API][Clear][To Pecia][Enter] (Enter rightmost).
    fitButtonRow({m_clearBtn, m_sendBtn},
                 w, buttonY + BTN_H / 2, BTN_GAP, BTN_GAP);

    end();
    resizable(m_chatDisp);
    color(tc.background2);
    relayout();

    // Restore the persisted conversation (ai_chat.txt): rebuilds
    // m_history and replays the last turns into the chat pane, each with
    // its original date-time from the log.
    loadChatLog();

    // Start the cursor blink timer (same cadence as the main window).
    Fl::add_timeout(0.5, cursorBlinkCb, this);

    // Log-file poll timer: the log file is the single source of truth for
    // the conversation. The main window appends "Send to AI" selections as
    // user turns; this timer notices the file growing, reloads the display
    // and auto-sends a trailing unanswered user turn. No window-to-window
    // push pipe is involved (see pollChatLog()).
    Fl::add_timeout(0.15, logPollCb, this);
}

// Re-read I18n strings after the settings.ini watcher detects a language
// change. Updates button labels, the attach-selection checkbox (label +
// tooltip), and the title bar. Button widths are re-measured because
// translations have different lengths.
void AIChatPane::applyTheme() {
    if (!m_theme) return;
    const ThemeColors &tc = m_theme->colors();
    // The pane is chrome-themed (the button row sits on background2), unlike
    // a work-area widget; re-assert it on every theme/language switch.
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
        // 正文颜色由行高亮样式表决定,改色后必须重建样式表,
        // 否则输入区残留旧主题色(与主窗口编辑器同一套机制)。
        m_inputEdit->syncHighlightBuffer();
    }
    for (auto *b : { m_sendBtn, m_clearBtn }) {
        if (b) b->labelcolor(tc.text1);
    }
    redraw();
}

void AIChatPane::refreshLabels() {
    if (m_sendBtn)      m_sendBtn->copy_label(I18n::get("chat.send"));
    if (m_clearBtn)     m_clearBtn->copy_label(I18n::get("chat.clear"));
    if (m_sendTime) emitElapsed((long long)(time(nullptr) - m_sendTime));

    // Re-fit the auto-width button row after labels change (translations
    // differ in length). Buttons already carry their new labels above;
    // re-center on the row they already sit in.
    relayout();
}

// secs >= 0 hands the host the elapsed count for the tab label; -1 means idle.
void AIChatPane::emitElapsed(long long secs) {
    if (m_onElapsed) m_onElapsed(secs);
}

// The pane is a CHILD of the main window since m037, so every child
// coordinate is absolute: a layout written against 0..w (the old top-level
// window's client area) paints the chat over the editor while the pane
// itself sits in the right zone. resize() therefore re-runs the whole
// internal layout from the pane's own x()/y().
void AIChatPane::resize(int X, int Y, int W, int H) {
    Fl_Group::resize(X, Y, W, H);
    relayout();
}

// One source for the internal geometry: chat on top, button row, input at
// the bottom edge. Called by the constructor, by resize() and after a
// language change (button widths are label-driven).
void AIChatPane::relayout() {
    if (!m_chatDisp) return;
    const int X = x(), Y = y(), W = w(), H = h();
    const int inputH = 140;
    const int barY = Y + H - inputH - BAR_H;
    const int buttonY = barY + (BAR_H - BTN_H) / 2;
    m_chatDisp->resize(X, Y, W, barY - Y);
    if (m_inputEdit) m_inputEdit->resize(X, Y + H - inputH, W, inputH);
    if (m_sendBtn) {
        fitButtonRow({m_clearBtn, m_sendBtn},
                     X + W, buttonY + BTN_H / 2, BTN_GAP, BTN_GAP);
    }
    redraw();
}

AIChatPane::~AIChatPane() {
    Fl::remove_timeout(cursorBlinkCb, this);
    Fl::remove_timeout(logPollCb, this);    // cancel the log poll (dangling this otherwise)
    Fl::remove_timeout(s_timerCb, this);    // cancel the elapsed-time timer (dangling this otherwise)
    if (s_active.load() == this) s_active.store(nullptr);
}

int AIChatPane::handle(int event) {
    // Configurable shortcuts first (window ops + chat actions): a
    // text field may otherwise consume the key before we see it.
    if (event == FL_KEYDOWN || event == FL_SHORTCUT) {
        if (dispatchShortcut()) return 1;
    }
    return Fl_Group::handle(event);
}

int AIChatPane::dispatchShortcut() {
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

// r042b item 3: the button row gets a filled backdrop using the same color
// parameter the status bar uses (background2), replacing the two hairline
// rules from r042 - the user prefers a solid row over edge lines.
void AIChatPane::draw() {
    const ThemeColors &tc = m_theme->colors();
    const int inputH = 140;
    const int barY = y() + h() - inputH - BAR_H;
    // fill BEFORE the children draw, so the buttons sit on top of the row
    fl_rectf(x(), barY, w(), BAR_H, tc.background2);
    Fl_Group::draw();
}

// Measured, not guessed: HoverButton::fit() sized each button to its label in
// the current language, so this is the narrowest column that still shows both.
int AIChatPane::minRowWidth() const
{
    int need = BTN_GAP;
    for (Fl_Button *b : { m_clearBtn, m_sendBtn }) {
        if (!b || b->w() <= 0) continue;
        need += b->w() + BTN_GAP;
    }
    return need > 1 ? need : 1;
}

void AIChatPane::focusInput() {
    if (m_inputEdit && m_inputEdit->visible()) m_inputEdit->take_focus();
}

// Re-read the shortcut.* config keys (shared settings.ini) and rebuild
// the local dispatch registry. Called at startup and whenever the
// settings.ini watcher fires (e.g. after the main window's Shortcuts
// dialog saved new combos).
void AIChatPane::applyShortcuts() {
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

    // Tool buttons (Window > Button group): one config key, bound here.
    bind("win.button.run", FL_Enter, FL_CTRL, [this]() { doSend(); });
    bind("win.button.clear", 0, 0, [this]() { cbClear(nullptr, this); });
}

// Push-channel poll: connect to the main process's push pipe (retrying
// until it exists), then drain 4-byte-length-prefixed frames. A frame
// is sent as an AI message immediately (auto-send) and the main window's
// selection is cleared so a later reply insertion replaces nothing.
void AIChatPane::logPollCb(void *data) {
    auto *self = static_cast<AIChatPane *>(data);
    if (self) self->pollChatLog();
    Fl::add_timeout(0.15, logPollCb, data);
}

void AIChatPane::pollChatLog() {
    // 文件大小变了 = 有人追加了记录（主窗口的"发送到 AI"，或本窗口自己——
    // 自己的追加已经在写入时更新过 m_logSize，不会走到这里）。重载与
    // "尾部未回复 user 记录 → 自动发送"的检查都在 loadChatLog() 里
    // （构造启动加载也走它：主窗口先追加、后拉起本窗口的场景里，启动读到
    // 的大小就是终值，轮询永远看不到"变化"——检查必须挂在加载上而不是
    // 变化上，否则窗口关闭期间的推送永远无人补发）。
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(ai_chat_log::defaultLogPath().c_str(),
                              GetFileExInfoStandard, &fa))
        return;   // 文件还不存在：没有历史，也没有待发记录
    long long size = ((long long)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
    if (m_logSize >= 0 && size == m_logSize) return;
    loadChatLog();
}

void AIChatPane::cursorBlinkCb(void *data) {
    auto *self = static_cast<AIChatPane *>(data);
    if (!self) return;
    self->m_cursorVisible = !self->m_cursorVisible;
    // Only blink when the input editor has keyboard focus and the pane is
    // the zone's occupant (the old window's shown() flag has no meaning
    // for a docked child - visible() is the same question).
    if (self->m_inputEdit && Fl::focus() == self->m_inputEdit &&
        self->visible()) {
        self->m_inputEdit->show_cursor(self->m_cursorVisible ? 1 : 0);
        self->m_inputEdit->damage(FL_DAMAGE_CHILD);
    }
    Fl::add_timeout(0.5, cursorBlinkCb, self);
}

void AIChatPane::setStatus(const char *text) {
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

void AIChatPane::appendTurn(const char *roleLabel, const std::string &text,
                              const std::string &ts) {
    if (!m_chatBuf) return;
    // One-line header: "── hh:mm:ss role ──" (time + role merged,
    // double-dash kept as the visual message boundary). Restored history
    // entries carry their original date-time from the log file instead.
    char tsbuf[48];
    if (!ts.empty()) {
        snprintf(tsbuf, sizeof(tsbuf), "%s", ts.c_str());
    } else {
        SYSTEMTIME st;
        GetLocalTime(&st);
        snprintf(tsbuf, sizeof(tsbuf), "%02d:%02d:%02d",
                 st.wHour, st.wMinute, st.wSecond);
    }
    std::string block = std::string("── ") + tsbuf + " " + roleLabel +
                        " ──\n" + text + "\n\n";
    int pos = m_chatBuf->length();
    m_chatBuf->insert(pos, block.c_str(), (int)block.size());
    m_chatDisp->insert_position(m_chatBuf->length());
    m_chatDisp->show_insert_position();
}

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

void AIChatPane::cbSend(Fl_Widget *, void *data) {
    static_cast<AIChatPane *>(data)->doSend();
}
void AIChatPane::cbClear(Fl_Widget *, void *data) {
    AIChatPane *self = static_cast<AIChatPane *>(data);
    if (!self) return;

    // Cancel any in-flight AI request.
    if (self->m_busy) {
        if (self->m_cancelRequested) self->m_cancelRequested->store(true);
        // Stop the elapsed-time display and restore the plain title.
        self->m_sendTime = 0;
        Fl::remove_timeout(s_timerCb, self);
        self->emitElapsed(-1);
        self->m_busy = false;
        // Invalidate the in-flight request's result: bumping the generation
        // makes s_handleResult drop it even if the cancel flag checked too
        // late and the reply already arrived.
        ++self->m_requestGen;
    }

    self->m_history.clear();
    self->m_chatBuf->text("");
    // 历史日志跟着清空：文件是唯一持久层，内存清了文件不清，
    // 下次启动又全回来了（"不点清除就不消失"的反面——点了清除就消失）。
    FILE *f = _wfopen(chatLogPath().c_str(), L"wb");
    if (f) fclose(f);
    self->m_logSize = 0;   // 记账归零，轮询不会把"刚清空"当成外部追加
}
// ---------------------------------------------------------------------------
// Send / receive
// ---------------------------------------------------------------------------

void AIChatPane::doSend() {
    // 输入框路径：用户在窗口里手动发送。校验输入 → 把 user 记录追加进
    // 日志文件（+内存缓存+对话窗格）→ startRequest() 完成实际请求。
    // key/endpoint 的校验在 startRequest() 里做（两条路径共用）。
    if (m_busy) return;
    const char *prompt = m_inputBuf->text();
    if (!prompt || !*prompt) { free((void *)prompt); setStatus(I18n::get("ai.noprompt")); return; }

    std::string message = prompt;
    free((void *)prompt);

    // m040: the "attach selection" checkbox is gone. Selection -> AI stays
    // available where it is unambiguous: the editor's context menu
    // "Send to AI" appends the selection as its own user message.

    m_history.push_back({"user", message});
    appendTurn(I18n::get("chat.you"), message);
    appendChatFile("user", message);   // 发送即落盘：崩溃也不丢这条输入
    m_inputBuf->text("");

    startRequest();
}

// 以 m_history（= 日志文件缓存）为上下文发起一次请求。两条路径汇合点：
// 输入框手动发送（doSend 追加完 user 记录）、文件轮询发现主窗口追加的
// 推送记录、回复落盘后发现仍有未回复的 user 记录（排队）。
void AIChatPane::startRequest() {
    if (m_busy || m_history.empty()) return;
    char keyBuf[512], modelBuf[256], endpointBuf[512];
    if (m_appCfg) {
        m_appCfg->getAiKey(keyBuf, sizeof(keyBuf), "");
        // The defaults belong to Config, not to this call site.
        m_appCfg->getAiModel(modelBuf, sizeof(modelBuf));
        m_appCfg->getAiEndpoint(endpointBuf, sizeof(endpointBuf));
    } else {
        keyBuf[0] = modelBuf[0] = endpointBuf[0] = 0;
    }
    const char *key = keyBuf;
    const char *model = modelBuf;
    const char *endpoint = endpointBuf;
    if (!key || !*key) { setStatus(I18n::get("ai.notconfigured")); return; }
    // Not configured is not the same failure as a network error: telling the
    // user to check the key when the URL is blank sends them to the wrong page.
    if (!model || !*model || !endpoint || !*endpoint) { setStatus(I18n::get("ai.notconfigured")); return; }

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
    sysPrompt +=
        "\n\n##### 提示词结束，以下是与用户的对话 #####\n";
    msgs.push_back({"system", sysPrompt});
    // Context depth is user-configurable (r042d): turns from the AI settings
    // page, converted to messages here. The getter re-clamps, so a
    // hand-edited settings.ini cannot poison the request builder; the appCfg
    // pointer is null in tests, which fall back to the historical default.
    size_t maxMessages = (size_t)Config::kDefaultAiContextTurns * 2;
    if (m_appCfg) {
        int turns = m_appCfg->getAiContextTurns();
        maxMessages = (size_t)turns * 2;
    }
    size_t start = m_history.size() > maxMessages
                       ? m_history.size() - maxMessages : 0;
    for (size_t i = start; i < m_history.size(); ++i) {
        msgs.push_back({m_history[i].first.c_str(), m_history[i].second});
    }

    m_busy = true;
    // Start the elapsed-time display in the title bar (Send button width
    // stays fixed). A queued request can be started from s_handleResult
    // while the previous request's repeating timeout is still registered;
    // remove it first or the elapsed callback runs twice per second.
    Fl::remove_timeout(s_timerCb, this);
    m_sendTime = time(nullptr);
    Fl::add_timeout(1.0, s_timerCb, this);
    // No status banner while waiting - the user sees the request is in
    // flight; a "Generating..." line would just pollute the chat pane.

    s_active.store(this);
    Result *r = new Result();
    r->owner = this;
    r->gen = ++m_requestGen;
    // Reset cancel flag for this new request. The flag is heap-allocated
    // and shared with the worker thread (shared_ptr), so it stays valid
    // even if this window is destroyed while the request is in flight.
    m_cancelRequested = std::make_shared<std::atomic<bool>>(false);
    std::thread([key = std::string(key), model = std::string(model),
                 endpoint = std::string(endpoint), msgs, r,
                 cancelFlag = m_cancelRequested]() {
        // Red line #6: an escaping exception in a thread entry is a
        // fastfail 0xC0000409 that bypasses CrashReport entirely (no dump)
        // and leaves m_busy stuck. Same shape as UpdateDialog's workers.
        try {
            AiChatResult res = aiChatCompletion(key, model, msgs, endpoint, 300000, cancelFlag.get());
            r->ok = res.ok;
            r->content = res.content;
            r->error = res.error;
        } catch (const std::exception &e) {
            r->ok = false;
            r->error = e.what();
        } catch (...) {
            r->ok = false;
            r->error = "internal error";
        }
        Fl::awake(s_handleResult, r);
    }).detach();
}

void AIChatPane::s_handleResult(void *data) {
    Result *r = static_cast<Result *>(data);
    AIChatPane *self = r->owner;
    if (self == s_active.load() && self && r->gen == self->m_requestGen) {
        self->m_busy = false;
        // Stop the elapsed-time display, restore the plain title. A queued
        // request below starts a fresh timer; leaving this one registered
        // would double the tick rate.
        Fl::remove_timeout(s_timerCb, self);
        self->m_sendTime = 0;
        self->emitElapsed(-1);
        if (r->ok) {
            self->m_history.push_back({"assistant", r->content});
            self->appendTurn(I18n::get("chat.ai"), r->content);
            self->appendChatFile("assistant", r->content);
            // No "Sent" status line - it would just pollute the chat pane.
        } else {
            self->setStatus(r->error.empty() ? I18n::get("ai.error") : r->error.c_str());
        }
        // 排队检查：请求进行中主窗口又追加了 user 记录（连续"发送到 AI"）
        // → 回复落盘后还有未回答的 user 轮次，立即接着发。判据用 user/
        // assistant 计数而不是看尾部（回复追加后尾部是 assistant，新推送
        // 夹在中间）。只在本请求**成功**时排队：失败的请求（网络/Key 问题）
        // 不自动重试，避免错误风暴；恢复手段是用户看到错误提示后手动重发。
        if (r->ok) {
            size_t users = 0, assistants = 0;
            for (const auto &t : self->m_history)
                (t.first == "user" ? users : assistants)++;
            if (users > assistants) self->startRequest();
        }
    }
    delete r;
}

void AIChatPane::s_timerCb(void *data) {
    AIChatPane *self = static_cast<AIChatPane *>(data);
    if (!self || self->m_sendTime == 0) return;   // reply arrived, stop
    // Show the elapsed time in the title bar (not on the Send button, so
    // the button width stays constant for the whole request).
    self->emitElapsed((long long)(time(nullptr) - self->m_sendTime));
    Fl::repeat_timeout(1.0, s_timerCb, data);
}

// ---------------------------------------------------------------------------
// 历史落盘（照 Lua 控制台 script.last.txt 的模式）：
//   · 文件 = exe 同级 ai_chat.txt（宽字符路径，兼容中文安装目录）
//   · 追加式日志：每条消息发送/收到时立即落盘，"清除"才清空——不点清除
//     就不消失。不需要"窗口关闭时保存"：没有未落盘的窗口期。
//   · 格式与容错解析见 AIChat/AiChatLog.h（有独立门禁 test_ai_chat_log）。
// ---------------------------------------------------------------------------
std::wstring AIChatPane::chatLogPath() {
    return ai_chat_log::defaultLogPath();   // 与主窗口共用（见 AiChatLog.h）
}

void AIChatPane::appendChatFile(const char *role, const std::string &text) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char ts[40];
    snprintf(ts, sizeof(ts), "%04u-%02u-%02u %02u:%02u:%02u",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);
    std::string block = ai_chat_log::serializeTurn(role, ts, text);
    FILE *f = _wfopen(chatLogPath().c_str(), L"ab");
    if (!f) return;   // 磁盘只读等场景静默放弃：聊天功能本身不受影响
    fwrite(block.data(), 1, block.size(), f);
    fclose(f);
    // 自己的追加不需要等轮询发现：直接记账，pollChatLog() 比较大小时
    // 就不会把这条当成"外部写入"而多做一次全量重载。
    m_logSize += (long long)block.size();
}

void AIChatPane::loadChatLog() {
    // 读全文件（宽路径打开，兼容中文安装目录；64MB sanity 同 loadScript）。
    FILE *f = _wfopen(chatLogPath().c_str(), L"rb");
    if (!f) { m_logSize = -1; return; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size >= (64 << 20)) { fclose(f); m_logSize = size; return; }
    std::string buf((size_t)size, '\0');
    bool ok = fread(&buf[0], 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    if (!ok) return;

    // 文件是唯一状态源：重载 = 清空内存缓存与对话窗格后整体回放。轮询发现
    // 外部追加（主窗口"发送到 AI"）时走同一条路。
    m_history.clear();
    if (m_chatBuf) m_chatBuf->text("");

    std::vector<ai_chat_log::Entry> entries = ai_chat_log::parseChatLog(buf);
    // UI/内存只回放最近 100 条：更早的仍留在文件里（"不点清除就不消失"），
    // 但对话窗格与发送上下文（最近 50 轮）不需要无限增长。
    const size_t total = entries.size();
    const size_t skip = total > 100 ? total - 100 : 0;
    for (size_t i = skip; i < total; ++i) {
        const ai_chat_log::Entry &e = entries[i];
        m_history.push_back({e.role, e.content});
        appendTurn(e.role == "user" ? I18n::get("chat.you") : I18n::get("chat.ai"),
                   e.content, e.time);
    }
    m_logSize = size;   // 记账到当前读取位置，pollChatLog 以此判断外部追加

    // ★ 尾部是一条尚未回复的 user 记录（主窗口追加的推送；或上次窗口关闭
    //   时请求被打断）→ 自动发起请求。检查必须在这里而不是 pollChatLog：
    //   "主窗口先追加、后拉起窗口"的启动场景里，启动读到的就是终值大小，
    //   轮询永远看不到变化。忙时跳过（回复落盘后 s_handleResult 会按
    //   user/assistant 计数再次检查，形成自然的排队）；文件加载本身不改
    //   文件大小，这里不会自己触发自己造成循环。
    if (!m_history.empty() && m_history.back().first == "user" && !m_busy)
        startRequest();
}
