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
    if (m_maximized) {
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
