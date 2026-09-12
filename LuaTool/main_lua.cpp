// main_lua.cpp - PeciaLua standalone Lua script tool entry point.
// Single instance: a second launch activates the existing window.
#include <FL/Fl.H>
#include <FL/fl_string_functions.h>
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>

#include "core/Config.h"
#include "ui/Layout.h"
#include "core/I18n.h"
#include "core/CrashReport.h"
#include "core/Theme.h"
#include "LuaToolWindow.h"
#include "PipeProtocol.h"

// Parse the command line: argv[0] = quoted pipe name, argv[1] = optional
// lang (lpCmdLine from CreateProcess excludes the exe path). Uses the
// standard CommandLineToArgvW parser instead of hand-rolled token
// walking (which mis-handled quoted arguments).
static std::string parseArg(LPSTR cmdLine, int index) {
    char buf[128] = "";
    int wlen = MultiByteToWideChar(CP_ACP, 0, cmdLine, -1, nullptr, 0);
    if (wlen <= 0) return buf;
    std::wstring wcmd(wlen, L'\0');
    MultiByteToWideChar(CP_ACP, 0, cmdLine, -1, &wcmd[0], wlen);
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(wcmd.c_str(), &argc);
    if (!argv || index >= argc) {
        if (argv) LocalFree(argv);
        return buf;
    }
    std::wstring wa = argv[index];
    LocalFree(argv);
    int blen = WideCharToMultiByte(CP_UTF8, 0, wa.c_str(), -1, nullptr, 0,
                                   nullptr, nullptr);
    if (blen <= 0) return buf;
    if (blen >= (int)sizeof(buf)) blen = (int)sizeof(buf) - 1;
    WideCharToMultiByte(CP_UTF8, 0, wa.c_str(), -1, buf, blen, nullptr, nullptr);
    return buf;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR cmdLine, int) {
    // Crash handler first: capture failures during startup too.
    CrashReport::install("pecialua");
    // COM (STA) for IFileSaveDialog in the Save As flow.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::string pipeName = parseArg(cmdLine, 0);
    std::string langArg = parseArg(cmdLine, 1);
    if (!langArg.empty() && langArg != "en" && langArg != "zh-CN")
        langArg.clear();

    HANDLE hMutex = CreateMutexA(nullptr, FALSE, MUTEX_LUA_TOOL);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        Fl::scheme("gtk+");
        if (!langArg.empty()) I18n::load(langArg.c_str());
        else {
            Config tmp;               // settings.ini (same as main process)
            char lc[16];
            tmp.getLang(lc, sizeof(lc), "en");
            if (!I18n::load(lc)) I18n::load("en");
        }
        const char *title = I18n::get("lua.title");
        HWND w = FindWindowA(nullptr, title);
        if (w) {
            ShowWindow(w, SW_RESTORE);
            SetForegroundWindow(w);
        }
        CloseHandle(hMutex);
        return 0;
    }

    Fl::visual(FL_DOUBLE | FL_RGB);
    Fl::scheme("gtk+");
    theme_registerBorderBox();

    Config appCfg;
    initLayoutHeights(appCfg);                    // shares settings.ini with the main process
    char langCode[16];
    appCfg.getLang(langCode, sizeof(langCode), "en");
    if (!langArg.empty()) fl_strlcpy(langCode, langArg.c_str(), sizeof(langCode));
    if (!I18n::load(langCode)) I18n::load("en");

    // Fixed initial size (800x600, same as Pecia). Not persisted: every
    // launch opens with the same size; the user can resize freely.
    int winW = DEFAULT_WIN_W, winH = DEFAULT_WIN_H;
    LuaToolWindow win(&appCfg, pipeName, winW, winH, I18n::get("lua.title"));
    win.openDialog();

    // Watch settings.ini for external changes (language switches made in
    // the main Pecia window). When the language changes, re-load I18n and
    // refresh every translated label in the console. The callback runs on
    // the main FLTK thread (Config uses Fl::awake internally).
    appCfg.setChangeCallback([&win, &appCfg]() {
        char lang[16];
        appCfg.getLang(lang, sizeof(lang), "en");
        if (strcmp(I18n::currentCode(), lang) != 0) {
            if (I18n::load(lang)) {
                win.refreshLabels();
            }
        }
        // Shortcut keys may have changed in the main window's
        // Shortcuts dialog: rebuild the local dispatch registry.
        win.applyShortcuts();
        // The theme may have changed (main window View > Theme): reload
        // and re-apply the palette so the console stays in sync.
        win.retheme();
    });
    appCfg.startWatcher();

    int rc = Fl::run();
    CloseHandle(hMutex);
    CoUninitialize();
    return rc;
}


