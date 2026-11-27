// AIChatPane.h - the AI conversation pane docked in the main window's right
// zone. It is an Fl_Group child of MainWindow, laid out like the Markdown
// preview; the right-zone tab strip draws its title and request-progress
// label. There is no separate chat window and no second executable: the
// editor and this pane live in the same Pecia.exe process.
//
// m040: the pane no longer touches the document at all - the To-Document and
// attach-selection entries are gone, so the IToolHost parameter went with
// them. Conversation state still lives in ai_chat.txt (file-as-state,
// see AiChatLog.h); selection -> AI goes through the editor's context menu.
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <FL/Fl_Group.H>

#include "core/ShortcutCore.h"

class Fl_Text_Display;
class Fl_Text_Buffer;
class ThemedTextEditor;   // ui/ThemeWidgets.h (input's real type, see .cpp)
class Fl_Button;
class Config;
class Theme;

class AIChatPane : public Fl_Group {
public:
    AIChatPane(Config *appCfg, const Theme *theme, int w, int h);
    ~AIChatPane() override;

    // Re-read I18n strings after a language change (called from the
    // settings.ini watcher callback on the main thread).
    void refreshLabels();

    // Re-read the shortcut.* config keys and rebuild the local dispatch
    // registry (called at startup and by the settings.ini watcher).
    void applyShortcuts();

    // Re-apply the host's current theme to every child. The theme object is
    // owned by MainWindow, so this only repaints - no reload.
    void applyTheme();

    // The tab strip shows "AI 对话 (12s) ..." while a request runs, so the
    // elapsed tick is handed out instead of being painted here.
    void setElapsedCallback(std::function<void(long long secs)> cb) {
        m_onElapsed = std::move(cb);
    }

    // The button row's own minimum width, measured in the current language:
    // the column floor used to be a hardcoded 320px that FOUR buttons needed
    // (m040 shrank the row to two), and a number cannot follow translations.
    int minRowWidth() const;

    // MainWindow forwards FL_SHORTCUT/FL_KEYDOWN here while the pane is
    // visible AND holds the keyboard: FLTK sends shortcuts to the top-level
    // window, and this pane is a child, so its bindings would otherwise
    // never be seen - but they must not steal the editor's keys either.
    int dispatchShortcut();

    // Opening the zone puts the caret in the input box:
    // Fl_Group::take_focus() would pick the last focusable child instead,
    // which is a button.
    void focusInput();

    // Internal geometry, from this pane's own x/y/w/h (see .cpp).
    void relayout();

    int handle(int event) FL_OVERRIDE;
    // Overlaid chrome: two 1px borderLine rules around the button row (r042
    // item 3 - the row's only background is the pane color, which reads as
    // "none"; the light lines give it edges instead of a heavy fill).
    void draw() FL_OVERRIDE;
    // Keep the edge-to-edge panes tracking the pane width and re-fit the
    // auto-width button row (FLTK's proportional child scaling would stretch
    // the row when the zone narrows).
    void resize(int X, int Y, int W, int H) FL_OVERRIDE;

private:
    static void cbSend(Fl_Widget *, void *);
    static void cbClear(Fl_Widget *, void *);

    void doSend();
    void setStatus(const char *);   // shows a status message in the chat pane
    void emitElapsed(long long secs);
    // ts 为空 = 用当前时间；恢复历史时传文件里存的时间戳。
    void appendTurn(const char *roleLabel, const std::string &text,
                    const std::string &ts = {});

    // --- 历史落盘（照 script.last.txt 的模式：纯文本文件，不进 INI）------
    // exe 同级 ai_chat.txt，追加式日志：每条消息发送/收到时立即落盘，
    // "清除"才清空——不点清除就不消失。格式与解析见 AIChat/AiChatLog.h。
    static std::wstring chatLogPath();
    void appendChatFile(const char *role, const std::string &text);
    void loadChatLog();

    // Cursor blink: toggles the input editor's cursor every 500ms,
    // same as MainWindow::cursorBlinkCb (FLTK has no built-in blink).
    static void cursorBlinkCb(void *data);
    bool m_cursorVisible = true;

    // 文件轮询：日志文件是对话的**唯一状态源**。主窗口"发送到 AI"把选中
    // 文本作为 user 记录直接追加进文件；这里每 0.15s 看一眼文件大小，变了
    // 就重载显示；若尾部是一条尚未回复的 user 记录且当前空闲，就自动发起
    // 请求（面板没打开时主窗口追加的推送，会在下次打开以同样的规则补发）。
    static void logPollCb(void *data);
    void pollChatLog();
    // 以 m_history（= 文件缓存，最近 100 条）为上下文发起一次请求。
    // doSend()（输入框路径）在追加完 user 记录后也走到这里。
    void startRequest();
    long long m_logSize = -1;   // 上次已知的日志文件字节数（-1 = 尚未统计）

    struct Result {
        AIChatPane *owner;
        unsigned long gen;   // request generation when the worker started
        bool ok;
        std::string content;
        std::string error;
    };
    static void s_handleResult(void *data);
    // The in-flight request posts back through Fl::awake; only one pane is
    // alive per process, and the token in Result keeps a late reply from
    // touching a destroyed owner.
    static std::atomic<AIChatPane *> s_active;

    ShortcutRegistry m_shortcutRegistry;
    std::map<std::string, std::function<void()>> m_shortcutHandlers;

    Config    *m_appCfg;
    const Theme *m_theme;     // owned by MainWindow, never deleted here

    Fl_Text_Display *m_chatDisp = nullptr;
    Fl_Text_Buffer  *m_chatBuf = nullptr;
    ThemedTextEditor *m_inputEdit = nullptr;
    Fl_Text_Buffer  *m_inputBuf = nullptr;
    Fl_Button *m_sendBtn = nullptr, *m_clearBtn = nullptr;

    std::vector<std::pair<std::string, std::string>> m_history;
    bool        m_busy = false;

    // Cancellation flag for the in-flight AI request. Shared ownership with
    // the request thread: the thread captures a copy, so the flag outlives
    // the pane if a request (up to 300s) is still running when it dies.
    std::shared_ptr<std::atomic<bool>> m_cancelRequested;

    // Request generation: bumped on every startRequest() and on clear(), so a
    // reply landing after 清除 cannot resurrect itself in history and the file
    // (audit B18).
    unsigned long m_requestGen = 0;

    // Elapsed-time display: secs while waiting for the reply, -1 when idle.
    // The number lives here; the tab strip renders it.
    time_t m_sendTime = 0;        // 0 = not waiting
    std::function<void(long long)> m_onElapsed;
    static void s_timerCb(void *data);
};
