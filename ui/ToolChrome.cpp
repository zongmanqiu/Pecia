// ToolChrome.cpp - Native window chrome for the standalone tools.
// Copied from the main window's border(0) handling (MainWindow_frame.cpp)
// so the tools share the same frame behavior: edge-resize border, taskbar
// presence, app icon, Win11 corners.
#include "ToolChrome.h"

#include <FL/x.H>
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Group.H>


#include "resource.h"
#include "core/Theme.h"
#include "ui/DialogBase.h"
#include "ui/NativeSubclass.h"
#include "ui/WindowFrame.h"
#include "ui/WindowShadow.h"

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
//   2. Handle WM_NCCALCSIZE to reserve ONE non-client pixel on left / right /
//      bottom (NOT top - the title bar handles that). The 1px border lives in
//      that pixel, so no FLTK child can paint over it and no band needs
//      sampling (WindowFrame.h explains why the old 6px strip is gone).
//   3. Handle WM_NCPAINT to stroke that border (ui/WindowFrame.cpp).
//   4. Handle WM_NCHITTEST to return HTLEFT / HTRIGHT / HTBOTTOM / HTTOP within
//      RESIZE_ZONE px of an edge - most of that zone is client area.
// ---------------------------------------------------------------------------

#if defined(_WIN32)
// Client inset / grip width, same numbers as the main window (see
// WindowFrame.h): 1px of non-client strip - it is just the border - and a
// 6px grip that lives mostly inside the client.
static const int NC_PAD = WindowFrame::NC_PAD;

// Band colour sampling + the frame painter now live in ui/WindowFrame.cpp
// (windowChrome::bandColourAt / paintNonClientFrame) so the main window and
// the tool windows run exactly the same code.

// Border colour is resolved LIVE, on every paint - never cached.
//
// This file used to keep a file-scope COLORREF that setupToolChrome() filled in
// once, at creation. That is invisible to a theme switch: DialogBase::retheme()
// reloads the theme and calls refreshFrameBand(), the painters below then
// stroked the 1px line (and every corner arc, which blends against the passed
// colour) with the theme the process was STARTED with, while the top line -
// drawn by DialogBase::draw() straight from m_theme - already had the new one.
// Result: three sides plus both arcs kept the old colour, which on a white
// border over a paper background looked like "the frame vanished" (reported
// 2026-10-22 for both the Lua console and the AI chat window).
//
// windowChrome::windowBorderColour() reads the property that
// applyRoundedCorners() refreshes - the very same value the layered frame
// bitmap rasterises its out-of-region arc halves with, so the two halves can
// never disagree. The theme is only a fallback for a window whose property has
// not been set yet; the painter must not see 0 (FL_BLACK) there, or a light
// theme would lose its black border entirely.
static unsigned int toolBorderColour(Fl_Window *win) {
    if (!win) return (unsigned int)FL_BLACK;
    const unsigned int c = windowChrome::windowBorderColour(win);
    if (c != (unsigned int)FL_BLACK) return c;
    if (auto *dlg = dynamic_cast<DialogBase *>(win)) {
        if (dlg->theme()) return (unsigned int)dlg->theme()->colors().text1;
    }
    return c;
}

static LRESULT WINAPI toolSubclassProc(HWND hwnd, UINT msg,
                                       WPARAM wp, LPARAM lp) {
    // Main window broadcasts: keep this tool process in step with the two
    // View toggles that affect every Pecia window (PeciaLua / PeciaAIChat).
    static const UINT kRoundedMsg = windowChrome::roundedCornersMessage();
    if (kRoundedMsg && msg == kRoundedMsg) {
        windowChrome::reapplyRoundedCorners(wp != 0);
        windowShadow::syncAll();   // the ramp follows the corner radius
        return 0;
    }
    static const UINT kShadowMsg = windowShadow::shadowMessage();
    if (kShadowMsg && msg == kShadowMsg) {
        windowShadow::setEnabled(wp != 0);
        windowShadow::applyAll();
        return 0;
    }
    switch (msg) {
    case WM_WINDOWPOSCHANGED:
    case WM_SHOWWINDOW: {
        LRESULT ret = nativeSubclass::forward(hwnd, toolSubclassProc, msg, wp, lp);
        windowShadow::sync(fl_find(hwnd));
        // Windows re-sends WM_NCPAINT on a frame change, but the band's colour
        // is sampled from the widget tree - and the tree may have been rebuilt
        // or recoloured since the last paint (theme switch, tab switch, a pane
        // appearing). Repaint the band explicitly so it never lags one layout
        // behind the client area beside it.
        windowChrome::refreshFrameBand(fl_find(hwnd));
        return ret;
    }

    case WM_DESTROY:
    case WM_NCDESTROY:
        windowShadow::detachHwnd(reinterpret_cast<void *>(hwnd));
        nativeSubclass::detachAll(hwnd);
        break;
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
        // Same single painter as the main window (ui/WindowFrame.cpp): the
        // non-client area is the 1px border itself. The colour is read live
        // (toolBorderColour) - caching it here is what made the tool windows
        // keep the previous theme's frame after View > Theme.
        Fl_Window *fwin = fl_find(hwnd);
        windowChrome::paintNonClientFrame(fwin, toolBorderColour(fwin));
        return 0;
    }

    // Repaint the ring AFTER the client area - same reason as the main window
    // (top run on client row 0, arcs inside the client rect, WM_NCPAINT not
    // resent for client paints).
    case WM_PAINT: {
        LRESULT ret = nativeSubclass::forward(hwnd, toolSubclassProc, msg, wp, lp);
        Fl_Window *fwin = fl_find(hwnd);
        windowChrome::paintNonClientFrame(fwin, toolBorderColour(fwin));
        return ret;
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
            return nativeSubclass::forward(hwnd, toolSubclassProc, msg, wp, lp);

        if (windowCoversWorkArea(fl_find(hwnd)))
            return HTCLIENT;

        // Scrollbars that touch the edge keep their clicks (see
        // windowChrome::pointOverScrollbar); everywhere else the grip is the
        // full RESIZE_ZONE.
        const int grip = windowChrome::pointOverScrollbar(
                             fl_find(hwnd), pt.x - rc.left, pt.y - rc.top)
                         ? WindowFrame::NC_PAD
                         : WindowFrame::RESIZE_ZONE;

        bool onTop    = pt.y <  rc.top    + grip;
        bool onLeft   = pt.x <  rc.left   + grip;
        bool onRight  = pt.x >= rc.right  - grip;
        bool onBottom = pt.y >= rc.bottom - grip;

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
    return nativeSubclass::forward(hwnd, toolSubclassProc, msg, wp, lp);
}
#endif

void setupToolChrome(Fl_Window *win, const Theme *theme) {
#if defined(_WIN32)
    if (!win) return;
    HWND hwnd = fl_xid(win);
    if (!hwnd) return;

    // The 1px border colour is resolved live on every paint (toolBorderColour)
    // and re-registered by DialogBase::retheme(), so nothing is captured here.
    (void)theme;

    // border(0) sets WS_POPUP | WS_EX_TOOLWINDOW: remove TOOLWINDOW and
    // add APPWINDOW so the window behaves like a normal top-level window
    // on the taskbar.
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    exStyle &= ~WS_EX_TOOLWINDOW;
    exStyle |= WS_EX_APPWINDOW;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);

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

    // Hand the NC area over to us (drops the DWM shadow and, more
    // importantly, stops DWM from discarding our WM_NCPAINT) - the 1px
    // border needs it whether or not the outer glow is enabled.
    windowChrome::takeOverFrame(win);
    // Same outer glow as the main window, one switch for the whole app.
    windowShadow::attach(win);

    // Explicitly activate so the taskbar button appears immediately.
    SetForegroundWindow(hwnd);
#else
    (void)win; (void)theme;
#endif
}
