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

int WindowFrame::hitResizeEdge(int mx, int my, int winW, int winH) const {
    int edge = 0;
    if (mx < NC_PAD)           edge |= 1; // left
    if (my < NC_PAD)           edge |= 2; // top
    if (mx >= winW - NC_PAD)   edge |= 4; // right
    if (my >= winH - NC_PAD)   edge |= 8; // bottom
    return edge;
}

int WindowFrame::hitResizeEdge(int mx, int my, Fl_Window *win) const {
    if (!win) return 0;
    return hitResizeEdge(mx, my, win->w(), win->h());
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

void WindowFrame::fixTaskbar(HWND hwnd, Fl_Window *win) {
#if defined(_WIN32)
    if (!hwnd || !win) return;

    // Ensure WS_EX_APPWINDOW is set so the window appears on the taskbar.
    // border(0) strips WS_EX_APPWINDOW on Windows, making the window
    // invisible to the taskbar. We re-apply it here using the native
    // Windows API because FLTK's border(1) call would re-enable the
    // OS title bar which we don't want (we draw our own).
    LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (!(exStyle & WS_EX_APPWINDOW)) {
        SetWindowLongW(hwnd, GWL_EXSTYLE, exStyle | WS_EX_APPWINDOW);
    }

    // Re-apply the NC_PAD resize grip area after the window frame
    // style change (which causes a WM_NCCALCSIZE recalculation).
    // Move left by NC_PAD and grow by 2*NC_PAD wide / NC_PAD tall
    // so the client area stays the same while we gain NC_PAD pixels
    // on the left, right, and bottom for edge resizing.
    RECT rc;
    if (GetWindowRect(hwnd, &rc)) {
        SetWindowPos(hwnd, nullptr,
                     rc.left - WindowFrame::NC_PAD, rc.top,
                     (rc.right - rc.left) + 2 * WindowFrame::NC_PAD,
                     (rc.bottom - rc.top) + WindowFrame::NC_PAD,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // Force right-angle corners (Windows 11 defaults to rounded).
    // This is called for every window we create — no config needed.
    DWORD cornerPref = 1; // DWMWCP_DONOTROUND
    DWORD borderColor = 0x007F7F7F; // COLORREF: #7F7F7F
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (hDwm) {
        typedef HRESULT (WINAPI *DwmSWA)(HWND, DWORD, LPCVOID, DWORD);
        auto fn = (DwmSWA)GetProcAddress(hDwm, "DwmSetWindowAttribute");
        if (fn) {
            fn(hwnd, 33, &cornerPref, sizeof(cornerPref));       // DWMWA_WINDOW_CORNER_PREFERENCE
            fn(hwnd, 34, &borderColor, sizeof(borderColor));      // DWMWA_BORDER_COLOR (Win11+)
        }
        FreeLibrary(hDwm);
    }

#endif
}
