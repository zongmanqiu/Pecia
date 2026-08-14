// AIChatWindow.h - standalone AI chat window.
#pragma once

#include <FL/Fl_Double_Window.H>
#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/ShortcutCore.h"

class Fl_Text_Editor;
class Fl_Text_Display;
class Fl_Text_Buffer;
class Fl_Input;
class Fl_Check_Button;
class Fl_Button;
class Fl_Box;
class Config;
class Theme;
class TitleBar;
class WindowFrame;

class AIChatWindow : public Fl_Double_Window {
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

    int handle(int event) FL_OVERRIDE;

private:
    // Dispatch a configurable shortcut (window ops + chat actions).
    // Returns 1 when handled.
    int dispatchShortcut();
    void minimizeWindow();
    void togglePin();
    bool m_pinned = false;
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
    void toggleMaximize();

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
    Theme       *m_theme = nullptr;
    TitleBar    *m_titleBar = nullptr;
    WindowFrame *m_frame = nullptr;

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
};
