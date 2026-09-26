// AIChatWindow.h - standalone AI chat window.
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/ShortcutCore.h"
#include "ui/DialogBase.h"

class Fl_Text_Editor;
class Fl_Text_Display;
class Fl_Text_Buffer;
class Fl_Input;
class Fl_Check_Button;
class Fl_Button;
class Fl_Box;
class Config;

class AIChatWindow : public DialogBase {
public:
    AIChatWindow(Config *appCfg, const std::string &peciaPipe,
                 int w, int h, const char *title);
    ~AIChatWindow() override;

    void openDialog();

    // Re-read I18n strings after a language change (called from the
    // settings.ini watcher callback on the main thread).
    void refreshLabels();

    // Re-read the shortcut.* config keys and rebuild the local dispatch
    // registry (called at startup and by the settings.ini watcher).
    void applyShortcuts();

    // Reload the theme from Config and re-apply the palette (window shell,
    // chat display, input editor, buttons). Called by the settings.ini
    // watcher so the window tracks the main window's View > Theme switch.
    void retheme();

    int handle(int event) FL_OVERRIDE;
    void onCaptionClose() FL_OVERRIDE { cbClose(nullptr, this); }
    // Keep full-width panes tracking the window width and re-fit the
    // fixed-width button row on every resize (FLTK's proportional child
    // scaling would otherwise stretch them when the window narrows).
    void resize(int X, int Y, int W, int H) FL_OVERRIDE;

private:
    // Dispatch a configurable shortcut (window ops + chat actions).
    // Returns 1 when handled.
    int dispatchShortcut();
    void minimizeWindow();
    // Pin/maximize live in DialogBase (shared WindowFrame); these wrappers
    // are kept so the configurable win.* shortcut bindings can trigger them.
    void togglePin() { onCaptionTogglePin(); }
    void toggleMaximize() { onCaptionMaximize(); }
    ShortcutRegistry m_shortcutRegistry;
    std::map<std::string, std::function<void()>> m_shortcutHandlers;
    static void cbSend(Fl_Widget *, void *);
    static void cbToDoc(Fl_Widget *, void *);
    static void cbToLua(Fl_Widget *, void *);   // extract ```lua block -> Lua console
    static void retryToLuaCb(void *data);       // retry send after launching Lua tool
    void launchLuaTool();                       // start PeciaLua.exe (single-instance)
    static void cbClear(Fl_Widget *, void *);
    static void cbSettings(Fl_Widget *, void *);
    static void cbClose(Fl_Widget *, void *);

    void doSend();
    // Read script\lua_api.txt relative to the exe on EVERY send, so
    // edits to the API doc take effect without restarting the window.
    // Returns "" when the file is missing (help is then omitted).
    static std::string readLuaApiHelp();
    void sendTo(const std::string &pipe, const char *failKey);
    void setStatus(const char *);   // shows a status message in the chat pane
    void appendTurn(const char *roleLabel, const std::string &text);

    // Title bar text: the window title, plus a "(12s)" elapsed-time suffix
    // while waiting for the AI reply (secs < 0 = plain title, no suffix).
    // The elapsed count lives here instead of on the Send button so the
    // button keeps a fixed width regardless of how long the request runs.
    void showTitleElapsed(long long secs);

    // Cursor blink: toggles the input editor's cursor every 500ms,
    // same as MainWindow::cursorBlinkCb (FLTK has no built-in blink).
    static void cursorBlinkCb(void *data);
    bool m_cursorVisible = true;

    // Push-channel polling: the main process pushes text ("Send to AI").
    static void pushPollCb(void *data);
    void pollPushPipe();
    void handlePushMessage(const std::string &text);
    void *m_pushPipe = nullptr;     // push-pipe handle (nullptr = not connected)
    std::string m_pushName;

    struct Result {
        AIChatWindow *owner;
        bool ok;
        std::string content;
        std::string error;
    };
    static void s_handleResult(void *data);
    static std::atomic<AIChatWindow *> s_active;

    Config      *m_appCfg;
    std::string  m_peciaPipe;

    Fl_Text_Display *m_chatDisp = nullptr;
    Fl_Text_Buffer  *m_chatBuf = nullptr;
    Fl_Text_Editor  *m_inputEdit = nullptr;
    Fl_Text_Buffer  *m_inputBuf = nullptr;
    Fl_Check_Button *m_attachSel = nullptr;
    Fl_Button *m_sendBtn = nullptr, *m_toDocBtn = nullptr, *m_clearBtn = nullptr;
    Fl_Button *m_toLuaBtn = nullptr;
    Fl_Button *m_settingsBtn = nullptr;

    std::vector<std::pair<std::string, std::string>> m_history;
    std::string m_lastReply;
    bool        m_busy = false;

    // Cancellation flag for the in-flight AI request. Shared ownership
    // with the request thread: the thread captures a copy of this
    // shared_ptr, so the flag outlives the window if the window is
    // destroyed while the request (up to 300s) is still in flight.
    // Previously the thread captured &m_cancelRequested (a member
    // address) - a use-after-free once Fl::run() returned.
    std::shared_ptr<std::atomic<bool>> m_cancelRequested;

    // Elapsed-time display: while waiting for the AI reply the title bar
    // shows "AI Chat (12s)" so the user knows it's still processing.
    time_t m_sendTime = 0;        // 0 = not waiting
    static void s_timerCb(void *data);
};
