// main.cpp - Pecia entry point
#include <FL/Fl.H>
#include <FL/platform.H>
#include "ui/MainWindow.h"
#include "ui/Layout.h"
#include "core/Config.h"
#include "core/I18n.h"
#include "core/CrashReport.h"
#include "core/Theme.h"
#include <string>

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
#if defined(_WIN32)
    // Install the crash handler early so we capture failures during
    // scheme setup, config load, and window construction too.
    CrashReport::install("pecia");
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
    // the current desktop. Position keeps the existing centering/cascade
    // logic below.
    int x = -1, y = -1, w = DEFAULT_WIN_W, h = DEFAULT_WIN_H;
    cfg.getToolSize("main", w, h, DEFAULT_WIN_W, DEFAULT_WIN_H);
    if (w < 400) w = 400;
    if (h < 300) h = 300;
    if (w > Fl::w()) w = Fl::w();
    if (h > Fl::h()) h = Fl::h();
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

    if (hasPosEnv) {
        // New window: cascade from the given position, wrap around if off-screen
        // Offset by title bar height so the original title bar remains visible
        int screenW = Fl::w();
        int screenH = Fl::h();
        int offset = 32;  // title bar height
        int nx = offsetX + offset;  // offset right
        int ny = offsetY + offset;  // offset down
        if (nx + w > screenW || ny + h > screenH) {
            nx = 0;
            ny = 0;
        }
        win.position(nx, ny);
    } else if (x >= 0 && y >= 0) {
        // First window with saved position: use it
        win.position(x, y);
    } else {
        // First launch: center on screen
        win.position((Fl::w() - w) / 2, (Fl::h() - h) / 2);
    }

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
                break;  // only one file for now
            }
        }
    }
    if (wargv) LocalFree(wargv);
#else
    for (int i = 1; i < argc; ++i) {
        if (argv[i] && argv[i][0] && argv[i][0] != '-') {
            win.openFile(argv[i]);
            break;  // only one file for now
        }
    }
#endif

    int rc = Fl::run();
    return rc;
}





