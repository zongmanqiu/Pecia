// LuaToolWindow.h - standalone Lua script tool window.
// Shares the main window's custom title bar and theme; built on DialogBase
// (same shell as every other window).
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <string>

#include "core/ShortcutCore.h"
#include "ui/ThemeWidgets.h"
#include "ui/DialogBase.h"

class Fl_Text_Display;
class Fl_Text_Buffer;
class Fl_Button;
class Fl_Box;
class Config;
class LuaEngine;

class LuaToolWindow : public DialogBase {
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
    void onCaptionClose() FL_OVERRIDE { cbClose(nullptr, this); }

private:
    // Dispatch a configurable shortcut (window ops + console actions).
    // Returns 1 when handled.
    int dispatchShortcut();
    void minimizeWindow();
    // Pin/maximize live in DialogBase (shared WindowFrame).
    void togglePin() { onCaptionTogglePin(); }
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
    std::wstring scriptLastPath();
    void appendOutput(const std::string &text);
    void toggleMaximize() { onCaptionMaximize(); }

    void startInsServer();
    static void s_handleIns(void *data);

    // Cursor blink: toggles the script editor's cursor every 500ms,
    // same as MainWindow::cursorBlinkCb (FLTK has no built-in blink).
    static void cursorBlinkCb(void *data);
    bool m_cursorVisible = true;

    Config      *m_appCfg;
    std::string  m_peciaPipe;
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
