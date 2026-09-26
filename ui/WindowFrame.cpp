// WindowFrame.cpp - Custom window frame implementation
#include "WindowFrame.h"

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/x.H>

#if defined(_WIN32)
#include <windows.h>
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM

#endif

WindowFrame::WindowFrame() {
}

#if defined(_WIN32)
bool windowCoversWorkArea(const Fl_Window *win) {
    if (!win) return false;
    HWND hwnd = fl_xid(win);
    if (!hwnd) return false;

    RECT rc;
    if (!GetWindowRect(hwnd, &rc)) return false;

    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfoW(mon, &mi)) return false;

    // Our maximized geometry intentionally overhangs the work area by NC_PAD
    // on the resize edges, so "covers" (not "equals") is the right test.
    const RECT &work = mi.rcWork;
    const int tol = 2;
    return rc.left   <= work.left   + tol &&
           rc.top    <= work.top    + tol &&
           rc.right  >= work.right  - tol &&
           rc.bottom >= work.bottom - tol;
}
#else
bool windowCoversWorkArea(const Fl_Window *) { return false; }
#endif

void WindowFrame::saveRestoreRect(int x, int y, int w, int h) {
    m_restoreX = x; m_restoreY = y;
    m_restoreW = w; m_restoreH = h;
}

void WindowFrame::getRestoreRect(int &x, int &y, int &w, int &h) const {
    x = m_restoreX; y = m_restoreY;
    w = m_restoreW; h = m_restoreH;
}

bool WindowFrame::toggleMaximize(Fl_Window *win) {
    if (!win) return m_maximized;
    if (windowCoversWorkArea(win)) {
        // Currently filling the work area -> restore the saved geometry.
        m_maximized = false;
        win->resize(m_restoreX, m_restoreY, m_restoreW, m_restoreH);
    } else {
        m_restoreX = win->x(); m_restoreY = win->y();
        m_restoreW = win->w(); m_restoreH = win->h();
        m_maximized = true;
        applyMaximized(win);
    }
    return m_maximized;
}

void WindowFrame::applyMaximized(Fl_Window *win) {
#if defined(_WIN32)
    if (!win) return;
    HWND hwnd = fl_xid(win);
    if (!hwnd) return;

    // Get the work area (screen minus taskbar)
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    // Account for the custom frame offset:
    // The NC_PAD pixels on left/right/bottom are non-client areas that
    // we've reserved for edge resizing (via NCCALCSIZE). When maximized
    // we need to expand the client area to cover those edges since the
    // window fills the whole screen and there's nothing to resize to.
    SetWindowPos(hwnd, HWND_TOP,
                 work.left - NC_PAD, work.top,
                 (work.right - work.left) + 2 * NC_PAD,
                 (work.bottom - work.top) + NC_PAD,
                 SWP_NOZORDER);
#endif
}
