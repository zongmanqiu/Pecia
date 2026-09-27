// main.cpp - Pecia entry point
#include <FL/Fl.H>
#include <FL/platform.H>
#include <FL/Fl_Preferences.H>
#include "ui/MainWindow.h"
#include "ui/Layout.h"
#include "core/Config.h"
#include "core/I18n.h"
#include "core/CrashReport.h"
#include "core/Theme.h"
#include "core/PathUtils.h"
#include <string>
#include <filesystem>
#include <fstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>   // CommandLineToArgvW
#endif

// Load `path` when FLTK's open-document mechanism fires (macOS open,
// Windows DDE open, or Fl::add_handler-based drops).
static void pecia_open_cb(const char *path) {
    Fl_Window *win = Fl::first_window();
    if (win) {
        MainWindow *mw = static_cast<MainWindow *>(win);
        mw->openFile(path);
    }
}

int main(int /*argc*/, char ** /*argv*/) {
    // 便携：禁止 FLTK 核心在 exe 之外读写 prefs（Windows 上默认落在
    // %APPDATA%\<vendor>\<app>.prefs）。Fl_File_Chooser 的收藏夹/预览开关、
    // Fl::option 等都走 CORE 权限，APP_OK 会把它们全部挡掉，且程序自身也不
    // 使用 Fl_Preferences。必须在任何 FLTK 调用之前设置。
    Fl_Preferences::file_access(Fl_Preferences::APP_OK);
#if defined(_WIN32)
    // Install the crash handler early so we capture failures during
    // scheme setup, config load, and window construction too.
    CrashReport::install("pecia");
#endif

#if defined(_WIN32)
    // 命名互斥体只作「电脑上是否还开着另一个 Pecia」的探针：每个 Pecia
    // 进程启动时都创建并持有这个句柄，句柄随进程退出自动释放。创建时就
    // 已存在（ERROR_ALREADY_EXISTS）说明还有别的 Pecia 在跑——多开、新建
    // 窗口、分离标签都算。仅用于「固定启动文档」的判断，不影响多开本身。
    HANDLE instanceMutex = CreateMutexA(nullptr, FALSE, "Pecia.MainInstance");
    bool anotherPeciaRunning =
        instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS;
#else
    bool anotherPeciaRunning = false;
#endif

    // Double-buffered visual for flicker-free drawing
    Fl::visual(FL_DOUBLE | FL_RGB);

    // Replace FLTK's stock FL_BORDER_BOX with our themed version so all
    // bordered widgets (inputs, buttons, checkboxes, dropdowns, menus)
    // draw their 1px border using the theme's text1-derived colour
    // instead of FLTK's hardcoded FL_BLACK.
    theme_registerBorderBox();

    // Apply the persisted scheme early so all widgets created below
    // (menu bar, tabs, status bar, file dialog) pick it up.
    Config cfg;
    // Unified bar/button heights from settings.ini - must run before
    // any widget is created so every bar uses the configured rhythm.
    initLayoutHeights(cfg);
    char scheme[32];
    cfg.getScheme(scheme, sizeof(scheme), "gtk+");
    // One-time migration: if the user was on the old default "gleam",
    // upgrade them to the new default "gtk+" so the change takes effect
    // immediately even when they have a persisted value.
    if (strcmp(scheme, "gleam") == 0) {
        cfg.setScheme("gtk+");
        snprintf(scheme, sizeof(scheme), "%s", "gtk+");
    }
    Fl::scheme(scheme);

#if defined(_WIN32)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif

    // Load persisted language preference. On first run there is no
    // "lang" key yet: seed settings.ini with the OS user's language
    // (English if no matching language file exists), so users generally
    // never need to switch manually.
    if (!cfg.hasKey("lang")) {
        cfg.setLang(I18n::detectSystemLang());
    }
    char langCode[16];
    cfg.getLang(langCode, sizeof(langCode), "en");
    if (!I18n::load(langCode)) {
        // Fall back to English
        I18n::load("en");
    }

    // Tell FLTK to call us back when a file should be opened
    fl_open_callback(pecia_open_cb);

    // Persisted window size (main_win_w/main_win_h in settings.ini, saved
    // by MainWindow::saveSettings on exit). Clamp to a sane minimum and to
    // the current desktop. 800x600 is only a fallback for a missing/absurd
    // value - a real remembered size is always honoured.
    int x = 0, y = 0, w = DEFAULT_WIN_W, h = DEFAULT_WIN_H;
    cfg.getToolSize("main", w, h, DEFAULT_WIN_W, DEFAULT_WIN_H);
    if (w < 400) w = 400;
    if (h < 300) h = 300;
    if (w > Fl::w()) w = Fl::w();
    if (h > Fl::h()) h = Fl::h();

    // Remembered position (main_win_x/main_win_y). Only trusted when BOTH
    // keys exist: the legacy win_x/win_y keys defaulted to 0,0 and would
    // otherwise pin the window to the top-left corner. Display layout
    // changes (resolution / monitor removed) can leave the saved point off
    // screen, in which case we fall back to centering.
    bool hasSavedPos = false;
    if (cfg.hasKey("main_win_x") && cfg.hasKey("main_win_y")) {
        int sx = 0, sy = 0;
        cfg.getWindowPos(sx, sy);
#if defined(_WIN32)
        const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        const int minVis = 64;  // 标题栏至少留这么多像素可供点击
        if (sx <= vx + vw - minVis && sx + w >= vx + minVis &&
            sy >= vy && sy + 32 <= vy + vh) {
            hasSavedPos = true;
        }
#else
        hasSavedPos = true;
#endif
        if (hasSavedPos) { x = sx; y = sy; }
    }

    // Whether the window was maximized on the last exit (main_win_max).
    bool restoreMax = cfg.getWindowMax();

    MainWindow win(w, h, "Pecia");

    // Parse PECIA_POS environment variable for cascading new windows
    int offsetX = 0, offsetY = 0;
    bool hasPosEnv = false;
    const char *posEnv = getenv("PECIA_POS");
    if (posEnv && *posEnv) {
        if (sscanf(posEnv, "%d,%d", &offsetX, &offsetY) == 2) {
            hasPosEnv = true;
        }
    }

    int targetX, targetY;
    if (hasPosEnv) {
        // New window: cascade from the given position, wrap around if off-screen
        // Offset by title bar height so the original title bar remains visible
        int screenW = Fl::w();
        int screenH = Fl::h();
        int offset = 32;  // title bar height
        targetX = offsetX + offset;  // offset right
        targetY = offsetY + offset;  // offset down
        if (targetX + w > screenW || targetY + h > screenH) {
            targetX = 0;
            targetY = 0;
        }
        restoreMax = false;  // cascaded windows always start normal
    } else if (hasSavedPos) {
        // First window with a saved position: use it
        targetX = x;
        targetY = y;
    } else {
        // First launch (or unusable saved point): center on screen
        targetX = (Fl::w() - w) / 2;
        targetY = (Fl::h() - h) / 2;
    }
    // Approximate placement before show() (avoids a flash at the origin).
    // The exact position - and the maximized restore - are applied in
    // fixTaskbarCb once the custom frame is installed, because that frame
    // shifts the window by -NC_PAD.
    win.position(targetX, targetY);
    win.setRestoreGeometry(targetX, targetY, restoreMax);

    // Show the window WITHOUT passing argc/argv. Fl_Window::show(argc,argv)
    // calls Fl::args() which prints "options are:..." to stderr and pops a
    // MessageBox whenever the command line contains a non-switch argument
    // (e.g. a file path passed by ShellExecuteA when detaching tabs).
    // We parse argv ourselves below.
    win.show();
    // If a file was passed on the command line, open it. Skip any
    // argument that starts with '-' (FLTK-style switch) so we don't
    // accidentally treat e.g. "-geometry" as a filename.
    // Windows: CRT argv is converted through the ANSI code page and mangles
    // paths that the code page can't represent (e.g. Chinese names on an
    // English system); parse the real wide command line and convert to
    // UTF-8, which is the encoding openFile/Document expect.
#if defined(_WIN32)
    bool openedFromArgs = false;
    int wargc = 0;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    for (int i = 1; wargv && i < wargc; ++i) {
        if (wargv[i] && wargv[i][0] && wargv[i][0] != L'-') {
            int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1,
                                        nullptr, 0, nullptr, nullptr);
            if (n > 0) {
                std::string u8(n - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1,
                                    &u8[0], n, nullptr, nullptr);
                win.openFile(u8.c_str());
                openedFromArgs = true;
                break;  // only one file for now
            }
        }
    }
    if (wargv) LocalFree(wargv);
#else
    bool openedFromArgs = false;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] && argv[i][0] && argv[i][0] != '-') {
            win.openFile(argv[i]);
            openedFromArgs = true;
            break;  // only one file for now
        }
    }
#endif

    // 设置-选项 > 固定启动文档：电脑上没有别的 Pecia 进程、且本次启动
    // 原本只会打开一个空白新文档（命令行没带文件）时，改为打开 exe 同级
    // 目录的 Test.txt；文件不存在就先建一个空文件再打开。这样开机双击
    // 就能直接记录、Ctrl+S 保存，不必再走文件对话框。
    if (!openedFromArgs && !anotherPeciaRunning && cfg.getFixedStartupDoc()) {
        std::string dir = pathutil::exeDir();
        if (!dir.empty()) {
            std::string fixedPath = dir + "/Test.txt";
            std::filesystem::path fp = pathutil::fromUtf8(fixedPath);
            std::error_code ec;
            if (!std::filesystem::exists(fp, ec)) {
                std::ofstream out(fp, std::ios::binary);   // 新建空文件
            }
            win.openFile(fixedPath.c_str());
        }
    }

    int rc = Fl::run();
    return rc;
}





