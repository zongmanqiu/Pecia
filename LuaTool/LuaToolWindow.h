// LuaToolWindow.h - standalone Lua script tool window.
// Shares the main window's custom title bar and theme so the tool
// looks exactly like the main Pecia window.
#pragma once

#include <FL/Fl_Double_Window.H>
#include <atomic>
#include <functional>
#include <map>
#include <string>

#include "core/ShortcutCore.h"
#include "ui/ThemeWidgets.h"

class Fl_Text_Display;
class Fl_Text_Buffer;
class Fl_Button;
class Fl_Box;
class Config;
class Theme;
class TitleBar;
class WindowFrame;
class LuaEngine;

class LuaToolWindow : public Fl_Double_Window {
public:
    LuaToolWindow(Config *appCfg, const std::string &peciaPipe,
                  int w, int h, const char *title);
    ~LuaToolWindow() override;

    void openDialog();

    // Re-read I18n strings after a language change (called from the
    // settings.ini watcher callback on the main thread).
    void refreshLabels();

    // Re-read the shortcut.* config keys and rebuild the local dispatch
    // registry (called at startup and by the settings.ini watcher).
    void applyShortcuts();

    int handle(int event) FL_OVERRIDE;

private:
    // Dispatch a configurable shortcut (window ops + console actions).
    // Returns 1 when handled.
    int dispatchShortcut();
    void minimizeWindow();
    void togglePin();
    bool m_pinned = false;
    ShortcutRegistry m_shortcutRegistry;
    std::map<std::string, std::function<void()>> m_shortcutHandlers;
    static void cbRun(Fl_Widget *, void *);
    static void cbClear(Fl_Widget *, void *);
    static void cbSaveAs(Fl_Widget *, void *);
    static void cbToAi(Fl_Widget *, void *);   // send script + last output to AIChat
    static void cbHelp(Fl_Widget *, void *);
    static void cbClose(Fl_Widget *, void *);

    void doRun();
    void doSaveAs();
    void loadScript();
    void saveScript();
    std::string scriptLastPath();
    void appendOutput(const std::string &text);
    void toggleMaximize();

    void startInsServer();
    static void s_handleIns(void *data);

    // Cursor blink: toggles the script editor's cursor every 500ms,
    // same as MainWindow::cursorBlinkCb (FLTK has no built-in blink).
    static void cursorBlinkCb(void *data);
    bool m_cursorVisible = true;

    Config      *m_appCfg;
    std::string  m_peciaPipe;
    Theme       *m_theme = nullptr;
    TitleBar    *m_titleBar = nullptr;
    WindowFrame *m_frame = nullptr;
    ThemedTextEditor *m_scriptEdit = nullptr;
    Fl_Text_Buffer *m_scriptBuf = nullptr;
    Fl_Text_Display *m_outDisp = nullptr;
    Fl_Text_Buffer *m_outBuf = nullptr;
    Fl_Button *m_runBtn = nullptr, *m_clearBtn = nullptr;
    Fl_Button *m_saveAsBtn = nullptr, *m_helpBtn = nullptr;
    Fl_Button *m_toAiBtn = nullptr;
    Fl_Box    *m_outHdr = nullptr;

    LuaEngine    *m_localEngine = nullptr;
    Fl_Text_Buffer *m_localBuf = nullptr;

    static std::atomic<LuaToolWindow *> s_active;
};
