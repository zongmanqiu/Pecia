// ToolChrome.cpp - Native window chrome for the standalone tools.
// Copied from the main window's border(0) handling (MainWindow_frame.cpp)
// so the tools share the same frame behavior: edge-resize border, taskbar
// presence, app icon, Win11 corners.
#include "ToolChrome.h"

#include <FL/x.H>
#include <FL/Fl.H>

#include "resource.h"
#include "core/Theme.h"
#include "ui/NativeSubclass.h"
#include "ui/WindowFrame.h"

#if defined(_WIN32)
#include <windows.h>
#include <windowsx.h>
#endif

// ---------------------------------------------------------------------------
// Non-client border for native edge resize.
//
// With border(0), FLTK creates a WS_POPUP window with no non-client area.
// To get external resize detection we:
//   1. Add WS_THICKFRAME to the window style.
//   2. Handle WM_NCCALCSIZE to create a 6px non-client border on
//      left / right / bottom (NOT top - the title bar handles that).
//   3. Handle WM_NCPAINT to paint the non-client border invisible
//      (background color) and draw the 1px gray outer border.
//   4. Handle WM_NCHITTEST to return HTLEFT / HTRIGHT / HTBOTTOM for
//      points in the non-client border.
// ---------------------------------------------------------------------------

#if defined(_WIN32)
static const int NC_PAD = 6;

// Non-client border fill color. Filled with the window's background color
// (set in setupToolChrome from win->color()) so the 6px edge-resize grip
// blends with the content, leaving only the 1px outer border visible.
static COLORREF s_edgeFill = RGB(235, 235, 235);
// The 1px outer border color (theme borderColor when available).
static COLORREF s_edgeBorder = RGB(180, 180, 180);

static LRESULT WINAPI toolSubclassProc(HWND hwnd, UINT msg,
                                       WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCCALCSIZE:
        if (wp == TRUE) {
            if (IsZoomed(hwnd)) return 0;  // no border when maximized
            NCCALCSIZE_PARAMS *p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
            p->rgrc[0].left   += NC_PAD;
            p->rgrc[0].right  -= NC_PAD;
            p->rgrc[0].bottom -= NC_PAD;
            return 0;
        }
        break;

    case WM_NCPAINT: {
        HDC hdc = GetWindowDC(hwnd);
        if (hdc) {
            RECT rcWin;
            GetWindowRect(hwnd, &rcWin);
            int ww = rcWin.right  - rcWin.left;
            int wh = rcWin.bottom - rcWin.top;

            // Fill the invisible resize grip only when the window is a real
            // floating window; a work-area-filling (maximized) window keeps
            // just its 1px outer edge.
            if (!windowCoversWorkArea(fl_find(hwnd))) {
                HBRUSH brush = CreateSolidBrush(s_edgeFill);
                RECT rcL = {0, 0, NC_PAD, wh};
                FillRect(hdc, &rcL, brush);
                RECT rcR = {ww - NC_PAD, 0, ww, wh};
                FillRect(hdc, &rcR, brush);
                RECT rcB = {NC_PAD, wh - NC_PAD, ww - NC_PAD, wh};
                FillRect(hdc, &rcB, brush);
                DeleteObject(brush);
            }

            // Draw 1px border at window's outer edge (theme border color).
            HPEN pen = CreatePen(PS_SOLID, 1, s_edgeBorder);
            HPEN oldPen = (HPEN)SelectObject(hdc, pen);
            HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, 0, 0, ww, wh);
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(pen);

            ReleaseDC(hwnd, hdc);
        }
        return 0;
    }

    case WM_NCACTIVATE:
        return 1;

    case WM_NCHITTEST: {
        POINT pt;
        pt.x = GET_X_LPARAM(lp);
        pt.y = GET_Y_LPARAM(lp);
        RECT rc;
        GetWindowRect(hwnd, &rc);

        if (pt.x < rc.left || pt.x >= rc.right ||
            pt.y < rc.top  || pt.y >= rc.bottom)
            return nativeSubclass::forward(hwnd, msg, wp, lp);

        if (windowCoversWorkArea(fl_find(hwnd)))
            return HTCLIENT;

        bool onTop    = pt.y <  rc.top    + NC_PAD;
        bool onLeft   = pt.x <  rc.left   + NC_PAD;
        bool onRight  = pt.x >= rc.right  - NC_PAD;
        bool onBottom = pt.y >= rc.bottom - NC_PAD;

        if (onTop    && onLeft)   return HTTOPLEFT;
        if (onTop    && onRight)  return HTTOPRIGHT;
        if (onLeft   && onBottom) return HTBOTTOMLEFT;
        if (onRight  && onBottom) return HTBOTTOMRIGHT;

        if (onTop)                return HTTOP;
        if (onLeft)               return HTLEFT;
        if (onRight)              return HTRIGHT;
        if (onBottom)             return HTBOTTOM;

        return HTCLIENT;
    }
    }
    return nativeSubclass::forward(hwnd, msg, wp, lp);
}
#endif

void setupToolChrome(Fl_Window *win, const Theme *theme) {
#if defined(_WIN32)
    if (!win) return;
    HWND hwnd = fl_xid(win);
    if (!hwnd) return;

    // Fill the 6px edge-resize grip with the window's background color so
    // it blends with the content; only the 1px outer border stays visible.
    {
        uchar r, g, b;
        Fl::get_color(win->color(), r, g, b);
        s_edgeFill = RGB(r, g, b);
    }
    // The 1px border color follows the theme when one is given.
    if (theme) {
        uchar r, g, b;
        Fl::get_color(theme->colors().text1, r, g, b);
        s_edgeBorder = RGB(r, g, b);
    }

    // border(0) sets WS_POPUP | WS_EX_TOOLWINDOW: remove TOOLWINDOW and
    // add APPWINDOW so the window behaves like a normal top-level window
    // on the taskbar.
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    exStyle &= ~WS_EX_TOOLWINDOW;
    exStyle |= WS_EX_APPWINDOW;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);

    // Force right-angle corners + border color (Win11).
    DWORD cornerPref = 1; // DWMWCP_DONOTROUND
    DWORD borderColor = 0x007F7F7F;
    if (theme) {
        Fl_Color c = theme->colors().text1;
        uchar r, g, b;
        Fl::get_color(c, r, g, b);
        borderColor = RGB(r, g, b);   // COLORREF is BGR (fixed: was RGB(b,g,r), red/blue swapped)
    }
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (hDwm) {
        typedef HRESULT (WINAPI *DwmSWA)(HWND, DWORD, LPCVOID, DWORD);
        auto fn = (DwmSWA)GetProcAddress(hDwm, "DwmSetWindowAttribute");
        if (fn) {
            fn(hwnd, 33, &cornerPref, sizeof(cornerPref));
            fn(hwnd, 34, &borderColor, sizeof(borderColor));
        }
        FreeLibrary(hDwm);
    }

    // App icon from the exe resource section.
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    HICON hIconSmall = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_PECIA),
                                         IMAGE_ICON, 16, 16, LR_SHARED);
    HICON hIconBig   = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_PECIA),
                                         IMAGE_ICON, 32, 32, LR_SHARED);
    if (hIconSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
    if (hIconBig)   SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)hIconBig);

    // Native edge resize: subclass first, then add WS_THICKFRAME and
    // adjust the outer size so the client area stays the same. The
    // original proc is stored per window (not a shared static), so
    // subclassing a second tool window can't clobber the first one's.
    nativeSubclass::install(hwnd, toolSubclassProc);

    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    style |= WS_THICKFRAME;
    // WS_SYSMENU | WS_MINIMIZEBOX are what let the shell minimize the window
    // when its taskbar button is clicked (it toggles via SC_MINIMIZE); a
    // frameless WS_POPUP window without them ignores that click. No visible
    // chrome is added because we paint and hit-test the NC area ourselves.
    style |= WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);

    RECT rc;
    GetWindowRect(hwnd, &rc);
    SetWindowPos(hwnd, nullptr,
                 rc.left - NC_PAD, rc.top,
                 (rc.right - rc.left) + 2 * NC_PAD,
                 (rc.bottom - rc.top) + NC_PAD,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    // Explicitly activate so the taskbar button appears immediately.
    SetForegroundWindow(hwnd);
#else
    (void)win; (void)theme;
#endif
}
