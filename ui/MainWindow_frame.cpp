// MainWindow_frame.cpp - MainWindow window-frame handling (event
// dispatch, native border subclassing, maximize/restore, custom draw
// and resize). Moved here from MainWindow.cpp so the main window file
// stays manageable. Member functions defined in this translation unit
// are part of the same MainWindow class declared in ui/MainWindow.h.
#include "ui/MainWindow.h"

#include "editor/Document.h"
#include "editor/Editor.h"
#include "ui/FindReplace.h"
#include "ui/GoTo.h"
#include "ui/TitleBar.h"
#include "ui/WindowFrame.h"
#include "ui/ShortcutManager.h"
#include "ui/DropTarget.h"
#include "core/Config.h"
#include "core/Theme.h"
#include "core/I18n.h"
#include "ui/Layout.h"
#include "resource.h"
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/platform.H>
#include <FL/fl_draw.H>
#include <windows.h>
#include <windowsx.h>
#include <ole2.h>
#include <dwmapi.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "mdview/preview_panel.h"

// --------------------------------------------------------------------------

int MainWindow::handle(int event) {
    // --- Title bar event handling ---
    // With border(0), we draw our own title bar. Route mouse events
    // that fall within the title bar area to the TitleBar first.
    // We do this BEFORE the default FLTK dispatch so caption buttons
    // and title-drag always work reliably.
    if (event == FL_MOVE    || event == FL_PUSH  || event == FL_DRAG ||
        event == FL_RELEASE || event == FL_LEAVE) {
        int mx = Fl::event_x();
        int my = Fl::event_y();
        if (m_titleBar) {
            // 拖动中：DRAG/RELEASE 无条件转发给 TitleBar（鼠标可能移出标题栏区域）
            if (event == FL_DRAG || event == FL_RELEASE) {
                int ret = m_titleBar->handle(event);
                if (ret) return ret;
            } else if (my >= 0 && my < TITLE_H && mx >= 0 && mx < w()) {
                int ret = m_titleBar->handle(event);
                if (ret) return ret;
            } else if (event == FL_MOVE) {
                m_titleBar->handle(FL_LEAVE);
            }
        }
    }

    // --- Global shortcut dispatch ---
    // We handle FL_KEYDOWN AND FL_SHORTCUT (which is the menu shortcut
    // event). Catching both ensures shortcuts work even when the
    // focused widget would otherwise eat them (e.g. Fl_Text_Editor
    // consumes Ctrl+A for Select All, but if we check here first we
    // can still let other shortcuts through).
    if (event == FL_KEYDOWN || event == FL_SHORTCUT) {
        int key = Fl::event_key();
        int state = Fl::event_state();
        if (dispatchShortcut(key, state)) return 1;
    }

    switch (event) {
    case FL_MOUSEWHEEL: {
        bool ctrl = (Fl::event_state() & (FL_CTRL | FL_COMMAND)) != 0;
        int dy = Fl::event_dy();
        // 鼠标在预览区：普通滚轮滚动预览；Ctrl+滚轮在预览区不缩放（预览无缩放功能）
        if (m_previewActive) {
            int divX = (int)(w() * m_previewRatio);
            if (Fl::event_x() >= divX) {
                if (!ctrl) scrollPreview(dy * 40);
                return 1;
            }
        }
        // 编辑区：Ctrl + wheel 缩放编辑器字号
        if (ctrl) {
            if (dy < 0) setFontSize(fontSize() + 1);
            else if (dy > 0) setFontSize(fontSize() - 1);
            return 1;
        }
        break;
    }

    case FL_KEYDOWN: {
        // After key handling, fall through so FLTK updates the editor,
        // then refresh the status bar on the next event loop tick.
        // add_timeout fires exactly once - no leak risk like add_idle.
        Fl::add_timeout(0.0, statusUpdateCb, this);
        // Reset the blink phase so the cursor is immediately visible
        // after any keypress (including arrows, Home/End, etc.).
        m_cursorVisible = true;
        break;
    }
    case FL_SHORTCUT: {
        // FLTK's default window handling closes the window when Esc is
        // pressed (it reaches FL_SHORTCUT after the focused widget
        // declines it). We never bind Esc to anything, so swallow it:
        // an editor must not quit on a stray Esc keypress.
        if (Fl::event_key() == FL_Escape) return 1;
        break;
    }
    case FL_PUSH: {
        Fl::add_timeout(0.0, statusUpdateCb, this);
        m_cursorVisible = true;
        break;
    }
    case FL_RELEASE:
        Fl::add_timeout(0.0, statusUpdateCb, this);
        m_cursorVisible = true;
        break;
    case FL_DRAG:
        Fl::add_timeout(0.0, statusUpdateCb, this);
        m_cursorVisible = true;
        break;
    }

    int result = Fl_Double_Window::handle(event);

    return result;
}

int MainWindow::dispatchShortcut(int key, int state) {
    // 1) Configurable handler actions (window ops, toolbar scripts, the
    //    go-to bar). Registered by applyShortcuts() from the
    //    shortcut.* settings; empty combos are never bound.
    if (const std::string *id = m_shortcutRegistry.find(key, state)) {
        auto it = m_shortcutHandlers.find(*id);
        if (it != m_shortcutHandlers.end()) {
            it->second();
            return 1;
        }
    }
    // 2) Menu-native shortcuts: FLTK executes the matching item with
    //    full toggle/radio/mvalue semantics (identical to clicking it),
    //    including the save-as style Shift+S normalization.
    if (m_menu && m_menu->test_shortcut()) return 1;
    return 0;
}

// menu.edit.goto keeps its toggle-bar behavior even when the shortcut
// is customized (the menu callback itself only shows the bar).
void MainWindow::toggleGoToBar() {
    if (m_goToBar && m_goToBar->visible()) {
        m_goToBar->hide();
        layoutTabs();
        if (Tab *t = activeTab()) Fl::focus(t->editor);
    } else {
        showGoToBar();
    }
}

// win.pin: toggle always-on-top (same action as the title bar pin).
void MainWindow::togglePin() {
    setAlwaysOnTop(!alwaysOnTop());
    if (m_titleBar) m_titleBar->setPinned(alwaysOnTop());
}

// win.nexttab / win.prevtab: cycle through open tabs.
void MainWindow::cycleTab(int dir) {
    int n = (int)m_tabsList.size();
    if (n > 1) {
        int i = activeTabIndex();
        i = (i + dir + n) % n;
        switchToTab(i);
    }
}

// win.minimize: minimize the window (border(0) windows need the native
// call; FLTK's iconize() is unreliable for them).
void MainWindow::minimizeWindow() {
#if defined(_WIN32)
    HWND hwnd = fl_xid(this);
    if (hwnd) ShowWindow(hwnd, SW_MINIMIZE);
#else
    iconize();
#endif
}

// --------------------------------------------------------------------------
// Windows-native edge resize via WS_THICKFRAME + WM_NCHITTEST subclass.
//
// With border(0), FLTK creates a WS_POPUP window with no non-client area,
// so all mouse events go to the client area.  To get external resize
// detection (outside the visible content, not inside), we:
//   1. Add WS_THICKFRAME to the window style.
//   2. Handle WM_NCCALCSIZE to create a 6px non-client border on
//      left / right / bottom (NOT top - the title bar handles that).
//   3. Handle WM_NCPAINT to paint the non-client border invisible
//      (background color) and draw the 1px gray outer border.
//   4. Handle WM_NCHITTEST to return HTLEFT / HTRIGHT / HTBOTTOM for
//      points in the non-client border.
// Windows then handles the resize cursor and drag automatically.
// --------------------------------------------------------------------------

#if defined(_WIN32)
static WNDPROC s_origWndProc = nullptr;
static const int NC_PAD = 6;  // non-client resize border width (px)

// Non-client edge-resize grip fill color, set from MainWindow::color()
// so the 6px grip blends with the content (only the 1px border shows).
static COLORREF s_mainEdgeFill = RGB(235, 235, 235);
static COLORREF s_mainEdgeBorder = RGB(180, 180, 180);

static LRESULT WINAPI mainWindowSubclassProc(HWND hwnd, UINT msg,
                                              WPARAM wp, LPARAM lp) {
    switch (msg) {
    // --- Define the non-client border: 6px on left/right/bottom, 0 on top
    case WM_NCCALCSIZE:
        if (wp == TRUE) {
            if (IsZoomed(hwnd)) return 0;  // no border when maximized
            NCCALCSIZE_PARAMS *p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
            p->rgrc[0].left   += NC_PAD;
            p->rgrc[0].right  -= NC_PAD;
            p->rgrc[0].bottom -= NC_PAD;
            // Top unchanged — top resize is handled in WM_NCHITTEST
            // (no NCCALCSIZE inset needed, which would break TitleBar layout)
            return 0;
        }
        break;

    // --- Paint the non-client border invisible + 1px gray outer edge
    case WM_NCPAINT: {
        HDC hdc = GetWindowDC(hwnd);
        if (hdc) {
            RECT rcWin;
            GetWindowRect(hwnd, &rcWin);
            int ww = rcWin.right  - rcWin.left;
            int wh = rcWin.bottom - rcWin.top;

            // Fill non-client border with window background color
            if (!IsZoomed(hwnd)) {
                HBRUSH brush = CreateSolidBrush(s_mainEdgeFill);
                RECT rcL = {0, 0, NC_PAD, wh};
                FillRect(hdc, &rcL, brush);
                RECT rcR = {ww - NC_PAD, 0, ww, wh};
                FillRect(hdc, &rcR, brush);
                RECT rcB = {NC_PAD, wh - NC_PAD, ww - NC_PAD, wh};
                FillRect(hdc, &rcB, brush);
                DeleteObject(brush);
            }

            // Draw 1px gray border at window's outer edge
            HPEN pen = CreatePen(PS_SOLID, 1, s_mainEdgeBorder);
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

    // --- Prevent non-client visual changes on activate/deactivate
    case WM_NCACTIVATE:
        return 1;

    // --- Hit-test: return resize codes for non-client border
    case WM_NCHITTEST: {
        POINT pt;
        pt.x = GET_X_LPARAM(lp);
        pt.y = GET_Y_LPARAM(lp);
        RECT rc;
        GetWindowRect(hwnd, &rc);

        // Outside the window rect - let default handle it
        if (pt.x < rc.left || pt.x >= rc.right ||
            pt.y < rc.top  || pt.y >= rc.bottom)
            return CallWindowProc(s_origWndProc, hwnd, msg, wp, lp);

        if (IsZoomed(hwnd))
            return HTCLIENT;  // no resize when maximized

        bool onTop    = pt.y <  rc.top    + NC_PAD;
        bool onLeft   = pt.x <  rc.left   + NC_PAD;
        bool onRight  = pt.x >= rc.right  - NC_PAD;
        bool onBottom = pt.y >= rc.bottom - NC_PAD;

        // Corners first (order matters: top corners before top edge)
        if (onTop    && onLeft)   return HTTOPLEFT;
        if (onTop    && onRight)  return HTTOPRIGHT;
        if (onLeft   && onBottom) return HTBOTTOMLEFT;
        if (onRight  && onBottom) return HTBOTTOMRIGHT;

        // Edges
        if (onTop)                return HTTOP;
        if (onLeft)               return HTLEFT;
        if (onRight)              return HTRIGHT;
        if (onBottom)             return HTBOTTOM;

        return HTCLIENT;
    }
    }
    return CallWindowProc(s_origWndProc, hwnd, msg, wp, lp);
}
#endif

// One-shot timeout that re-applies WS_EX_APPWINDOW on Windows after
// the window has been shown. border(0) sets WS_POPUP | WS_EX_TOOLWINDOW
// (wintype 0 in Fl_WinAPI_Window_Driver::makeWindow), which:
//   1. Removes the window from the taskbar (WS_EX_TOOLWINDOW does this)
//   2. Causes ShowWindow to use SW_SHOWNOACTIVATE, so the window is
//      never activated at startup and the taskbar never gets notified
//
// To fix both issues we:
//   - Remove WS_EX_TOOLWINDOW
//   - Add WS_EX_APPWINDOW
//   - Force a frame change via SetWindowPos(SWP_FRAMECHANGED)
//   - Explicitly activate the window with SetForegroundWindow so the
//     taskbar receives a WM_ACTIVATE and creates the taskbar button
//     immediately, instead of waiting for the user to click the window.
void MainWindow::fixTaskbarCb(void *data) {
    auto *self = static_cast<MainWindow *>(data);
#if defined(_WIN32)
    HWND hwnd = fl_xid(self);
    if (hwnd) {
        // Fill the 6px edge-resize grip with the window background color
        // so it blends with the content (only the 1px border shows).
        {
            uchar r, g, b;
            Fl::get_color(self->color(), r, g, b);
            s_mainEdgeFill = RGB(r, g, b);
        }
        // The 1px border follows the current theme.
        {
            const ThemeColors &mc = self->m_theme.colors();
            uchar r, g, b;
            Fl::get_color(mc.borderColor, r, g, b);
            s_mainEdgeBorder = RGB(r, g, b);
        }

        LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        // Remove WS_EX_TOOLWINDOW (added by FLTK's border(0)), add
        // WS_EX_APPWINDOW so the window behaves like a normal top-level
        // window on the taskbar.
        exStyle &= ~WS_EX_TOOLWINDOW;
        exStyle |= WS_EX_APPWINDOW;
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);

        // Force Windows to re-evaluate the window frame (non-client
        // metrics etc.) so the style change takes effect immediately.
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOACTIVATE | SWP_FRAMECHANGED);

        // Force right-angle corners (Windows 11 defaults to rounded).
        DWORD cornerPref = 1; // DWMWCP_DONOTROUND
        DWORD borderColor = 0x007F7F7F;
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

        // Explicitly bring the window to the foreground and activate
        // it. border(0) + WS_EX_TOOLWINDOW caused ShowWindow to use
        // SW_SHOWNOACTIVATE, so the window was never activated and
        // the taskbar button never appeared until the user clicked
        // the window (triggering WM_ACTIVATE).
        SetForegroundWindow(hwnd);

        // Set the window icon from the exe's resource section. border(0)
        // strips the caption including the system-assigned icon, so we
        // must re-apply ICON_SMALL / ICON_BIG ourselves. The taskbar uses
        // ICON_BIG; Alt+Tab and the title bar use ICON_SMALL. Load
        // multiple sizes so Windows can pick the best fit.
        HINSTANCE hInst = GetModuleHandleW(nullptr);
        HICON hIconSmall = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_PECIA),
                                             IMAGE_ICON, 16, 16, LR_SHARED);
        HICON hIconBig   = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_PECIA),
                                             IMAGE_ICON, 32, 32, LR_SHARED);
        if (hIconSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
        if (hIconBig)   SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)hIconBig);

        // --- Native edge resize via WS_THICKFRAME ---
        // Install the subclass proc FIRST so WM_NCCALCSITE from
        // SetWindowPos(SWP_FRAMECHANGED) is handled correctly.
        s_origWndProc = (WNDPROC)SetWindowLongPtrW(
            hwnd, GWLP_WNDPROC, (LONG_PTR)mainWindowSubclassProc);

        // Add WS_THICKFRAME to enable native resize.
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        style |= WS_THICKFRAME;
        SetWindowLongPtrW(hwnd, GWL_STYLE, style);

        // Adjust the window outer size so the CLIENT area stays the
        // same (NC_PAD pixels on left/right/bottom are now non-client).
        // Move left by NC_PAD and grow by 2*NC_PAD wide / NC_PAD tall.
        RECT rc;
        GetWindowRect(hwnd, &rc);
        SetWindowPos(hwnd, nullptr,
                     rc.left - NC_PAD, rc.top,
                     (rc.right - rc.left) + 2 * NC_PAD,
                     (rc.bottom - rc.top) + NC_PAD,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        // --- Register a native OLE drop target for file drag&drop ---
        // border(0) + the style changes above appear to break FLTK's
        // built-in DnD (RegisterDragDrop done in Fl_win32.cxx during
        // makeWindow, before our style changes). We revoke the (possibly
        // stale) FLTK registration and install our own PeciaDropTarget,
        // which only handles CF_HDROP file drops and calls openFile().
        RevokeDragDrop(hwnd);  // revoke any previous registration
        PeciaDropTarget *dt = new PeciaDropTarget(hwnd, [self](const char *path) { self->openFile(path); });
        HRESULT hr = RegisterDragDrop(hwnd, dt);
        if (SUCCEEDED(hr)) {
            self->m_dropTarget = dt;  // keep a reference for cleanup
        } else {
            dt->Release();  // registration failed, release our ref
        }
    }
#endif
    (void)self;
}

// --------------------------------------------------------------------------
// Settings sync across multiple Pecia windows
// --------------------------------------------------------------------------

// even with border(0). The title bar and menu bar draw their own
// backgrounds; we just outline the outermost pixels.
void MainWindow::draw() {
    Fl_Double_Window::draw();
    // 1px outer border is now drawn in WM_NCPAINT (non-client area)
}

// Override resize so that when the window size changes (edge drag,
// maximize/restore, or user-initiated resize) all child widgets are
// repositioned correctly. layoutTabs() handles the title bar, menu bar,
// tabs, find bar, and status bar.
void MainWindow::resize(int X, int Y, int W, int H) {
    Fl_Double_Window::resize(X, Y, W, H);
    layoutTabs();
}

// Toggle between maximized and restored (previous geometry).
void MainWindow::toggleMaximize() {
    if (m_maximized) {
        // Restore
        m_maximized = false;
        resize(m_restoreX, m_restoreY, m_restoreW, m_restoreH);
    } else {
        // Save current geometry for later restore
        m_restoreX = x(); m_restoreY = y();
        m_restoreW = w(); m_restoreH = h();
        m_maximized = true;
        applyMaximized();
    }
    syncTitleBar();
}

// Compute the work area (screen minus taskbar) and resize to fill it.
void MainWindow::applyMaximized() {
    int mx, my, mw, mh;
    Fl::screen_work_area(mx, my, mw, mh, Fl::screen_num(x(), y()));
    resize(mx, my, mw, mh);
}

// Apply the round/right-angle corner preference via DWM API.
//   roundCorner=true  → DWMWCP_DEFAULT (let the system decide, rounded on Win11)
//   roundCorner=false → DWMWCP_DONOTROUND (right-angle)
//
// After setting the attribute we must force a non-client area repaint
// --------------------------------------------------------------------------
// Settings persistence
// --------------------------------------------------------------------------
