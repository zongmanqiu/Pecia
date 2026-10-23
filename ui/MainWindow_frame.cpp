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
#include "ui/WindowShadow.h"
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
#include "ui/NativeSubclass.h"

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

    // --- Tab strip right-click context menu ---
    // Fl_Tabs only handles the left button; right-clicks bubble up to the
    // window. Hit-test the tab header strip here (TABS_H from Layout.h;
    // Fl_Tabs::tab_height() itself is protected) and open the context
    // menu (close others / copy path) for the tab under the cursor.
    if (event == FL_PUSH && Fl::event_button() == FL_RIGHT_MOUSE && m_tabs &&
        m_cfg && m_cfg->getMultiTab()) {
        int mx = Fl::event_x(), my = Fl::event_y();
        if (mx >= m_tabs->x() && mx < m_tabs->x() + m_tabs->w() &&
            my >= m_tabs->y() && my < m_tabs->y() + TABS_H) {
            if (Fl_Widget *tab = m_tabs->which(mx, my)) {
                for (int i = 0; i < (int)m_tabsList.size(); ++i) {
                    if (m_tabsList[i].page == tab) {
                        showTabContextMenu(i);
                        return 1;
                    }
                }
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
            // 内容区可能被左侧面板挤窄：分界点 = 侧栏宽 + 内容区内编辑区宽
            const int cl = contentLeft();
            const int divX = cl + (int)((w() - cl) * m_previewRatio);
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
//   2. Handle WM_NCCALCSIZE to reserve ONE non-client pixel on left / right /
//      bottom (NOT top - the title bar starts on window row 0). That single
//      row/column is where the 1px border lives: it belongs to the window, so
//      no FLTK child can ever paint over it and no band has to be sampled.
//   3. Handle WM_NCPAINT to stroke that border (ui/WindowFrame.cpp).
//   4. Handle WM_NCHITTEST to return HTLEFT / HTRIGHT / HTBOTTOM / HTTOP for
//      points within RESIZE_ZONE px of an edge - most of that zone is CLIENT
//      area (see WindowFrame.h: NC_PAD); Windows then handles the resize
//      cursor and drag automatically.
// --------------------------------------------------------------------------

#if defined(_WIN32)
// Single source of truth for both numbers: WindowFrame::NC_PAD is the
// client inset (now 1px - the strip is just the border) and RESIZE_ZONE
// is how far inside the window edge a point still counts as a grip.
static const int NC_PAD = WindowFrame::NC_PAD;

// Minimum client area matching size_range(200,100) set in the ctor.
// Enforced via WM_GETMINMAXINFO ptMinTrackSize (FLTK's own handler is
// bypassed by our subclass proc).
static const int kMinClientW = 200;
static const int kMinClientH = 100;

// The band's colour sampling now lives in windowChrome::bandColourAt
// (ui/WindowFrame.cpp) so the main window and the tool windows share one
// implementation - hand-drawn widgets (Editor, SidePanel) report their own
// colours through windowChrome::BandColourSource. The painter is
// windowChrome::paintNonClientFrame, called from WM_NCPAINT below.

// Fingerprint of everything the band's colours depend on: every widget's
// geometry, visibility and colour. Cheap enough (a few dozen nodes) to run on
// every draw, which is how the band stays in sync - Windows never re-sends
// WM_NCPAINT when only the client area changes.
static unsigned long long bandSignature(Fl_Widget *w, int depth) {
    unsigned long long h = 1469598103934665603ull;   // FNV-1a
    auto mix = [&h](unsigned long long v) { h ^= v; h *= 1099511628211ull; };
    mix((unsigned long long)w->x());
    mix((unsigned long long)w->y());
    mix((unsigned long long)w->w());
    mix((unsigned long long)w->h());
    mix(w->visible() ? 1ull : 0ull);
    if (auto *ed = dynamic_cast<Editor *>(w)) mix((unsigned long long)ed->bgColor());
    else mix((unsigned long long)w->color());
    if (depth > 0) {
        if (auto *g = dynamic_cast<Fl_Group *>(w))
            for (int i = 0; i < g->children(); ++i)
                mix(bandSignature(g->child(i), depth - 1));
    }
    return h;
}

static LRESULT WINAPI mainWindowSubclassProc(HWND hwnd, UINT msg,
                                              WPARAM wp, LPARAM lp) {
    switch (msg) {
    // --- Detect maximize/restore from system (Win+Up, system menu, etc.)
    case WM_SIZE: {
        // Let FLTK handle WM_SIZE first (it updates x/y/w/h).
        LRESULT ret = nativeSubclass::forward(hwnd, mainWindowSubclassProc, msg, wp, lp);
        // Sync our m_maximized flag with the actual Windows state.
        Fl_Window *win = fl_find(hwnd);
        MainWindow *self = dynamic_cast<MainWindow*>(win);
        if (self) {
            bool wasMax = self->m_maximized;
            // The real zoom state drives the caption icon. A window that
            // merely fills the work area without being zoomed is not
            // maximized, so the icon must not claim otherwise.
            bool isMax  = (wp == SIZE_MAXIMIZED);
            if (wasMax != isMax) {
                self->m_maximized = isMax;
                self->syncTitleBar();
            }
        }
        return ret;
    }

    // --- Keep the outer-gradient shadow window glued to the window.
    // WM_WINDOWPOSCHANGED covers moves, resizes, show/hide, minimize/restore
    // and Z-order changes in one place.
    case WM_WINDOWPOSCHANGED:
    case WM_SHOWWINDOW: {
        LRESULT ret = nativeSubclass::forward(hwnd, mainWindowSubclassProc, msg, wp, lp);
        windowShadow::sync(fl_find(hwnd));
        return ret;
    }

    // --- Tear down the outer glow.
    // Must go through the native handle: fl_find(hwnd) already returns null
    // this late (FLTK drops the window from its table), and a Fl_Window-based
    // detach would return early and leave the glow floating on screen after
    // the window has closed.
    case WM_DESTROY:
    case WM_NCDESTROY:
        windowShadow::detachHwnd(reinterpret_cast<void *>(hwnd));
        nativeSubclass::detachAll(hwnd);
        break;

    // --- Constrain maximized size to work area (excludes taskbar)
    // WM_GETMINMAXINFO is the standard Windows mechanism for this.
    // The maximized outer size must include the NC border (NC_PAD on
    // left/right/bottom) so that after WM_NCCALCSIZE shrinks the
    // client area, the client fills exactly the work area.
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = reinterpret_cast<MINMAXINFO*>(lp);
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfoW(mon, &mi)) {
            RECT work = mi.rcWork;  // work area (excludes taskbar)
            mmi->ptMaxPosition.x = work.left - NC_PAD;
            mmi->ptMaxPosition.y = work.top;
            mmi->ptMaxSize.x = (work.right  - work.left) + 2 * NC_PAD;
            mmi->ptMaxSize.y = (work.bottom - work.top) + NC_PAD;
        }
        // Enforce minimum window size (replaces FLTK's set_minmax which
        // is bypassed by our subclass intercepting WM_GETMINMAXINFO).
        mmi->ptMinTrackSize.x = kMinClientW + 2 * NC_PAD;
        mmi->ptMinTrackSize.y = kMinClientH + NC_PAD;
        return 0;
    }

    // --- Define the non-client border: 1px on left/right/bottom, 0 on top
    // Always apply NC_PAD (including when maximized) so that FLTK's
    // AdjustWindowRectEx()计算的外部尺寸始终正确 —— 如果最大化时
    // return 0，FLTK 会额外加上 NC 边框导致窗口超出工作区。
    case WM_NCCALCSIZE:
        if (wp == TRUE) {
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
        // ONE painter for every window (ui/WindowFrame.cpp). The non-client
        // area is the 1px border itself, so all it needs is the colour -
        // square or hard-drawn rounded, same code on Win8/10 and Win11.
        // Colours are read live from the theme, so a theme switch only needs a
        // repaint (refreshFrameBand); nothing is cached inside DWM.
        Fl_Window *win = fl_find(hwnd);
        MainWindow *self = dynamic_cast<MainWindow *>(win);
        unsigned int border = (unsigned int)FL_BLACK;
        if (self) border = (unsigned int)self->m_theme.colors().text1;
        windowChrome::paintNonClientFrame(win, border);
        return 0;
    }

    // --- Repaint the border AFTER the client area.
    // The 1px ring is not (only) in the non-client area: the top run sits on
    // client row 0 and the arcs reach `radius` px inside the client rect, so
    // any client paint goes straight over them. WM_NCPAINT is only sent for
    // frame/size changes and Fl_Window::draw() only runs when the WINDOW is
    // damaged - neither happens when a caption button repaints on hover, or
    // when FLTK blits its whole offscreen buffer after a theme switch. That is
    // the "corner turns into a staircase until restart" report (2026-10-22).
    // Forwarding first means FLTK has flushed (its WM_PAINT is where the
    // double-buffer blit happens), so these pixels stay on top.
    case WM_PAINT: {
        LRESULT ret = nativeSubclass::forward(hwnd, mainWindowSubclassProc, msg, wp, lp);
        Fl_Window *win = fl_find(hwnd);
        if (auto *self = dynamic_cast<MainWindow *>(win))
            windowChrome::paintNonClientFrame(
                win, (unsigned int)self->m_theme.colors().text1);
        return ret;
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
            return nativeSubclass::forward(hwnd, mainWindowSubclassProc, msg, wp, lp);

        if (IsZoomed(hwnd))
            return HTCLIENT;  // no resize when maximized

        // The grip is RESIZE_ZONE px inside the window edge (the 1px band is
        // part of it). A scrollbar that touches the edge keeps its clicks -
        // otherwise the outer 5px of a 10px scrollbar would resize the window
        // instead of dragging the thumb - so there the grip shrinks back to
        // the 1px band.
        const int grip = windowChrome::pointOverScrollbar(
                             fl_find(hwnd), pt.x - rc.left, pt.y - rc.top)
                         ? WindowFrame::NC_PAD
                         : WindowFrame::RESIZE_ZONE;

        bool onTop    = pt.y <  rc.top    + grip;
        bool onLeft   = pt.x <  rc.left   + grip;
        bool onRight  = pt.x >= rc.right  - grip;
        bool onBottom = pt.y >= rc.bottom - grip;

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
    return nativeSubclass::forward(hwnd, mainWindowSubclassProc, msg, wp, lp);
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
        // SetWindowPos(SWP_FRAMECHANGED) is handled correctly. The
        // original proc is remembered per window (not a shared static),
        // so subclassing another window cannot clobber this one's.
        nativeSubclass::install(hwnd, mainWindowSubclassProc);

        // Add WS_THICKFRAME to enable native resize, plus the system menu
        // and the min/max boxes. WS_SYSMENU | WS_MINIMIZEBOX are required for
        // the shell's "click the taskbar button to minimize" behavior (it
        // toggles the window through SC_MINIMIZE); a frameless WS_POPUP
        // window without them cannot be minimized from the taskbar. They add
        // no visible chrome because we paint/hit-test the NC area ourselves.
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        style |= WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
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

        // Apply the persisted startup geometry LAST, once the custom frame
        // (WS_THICKFRAME + NC_PAD) is fully installed: the SetWindowPos below
        // moves the OUTER rect to the saved screen position, undoing the
        // -NC_PAD shift applied earlier in this callback. Then re-maximize
        // when the window was closed maximized.
        if (self->m_hasRestoreGeom) {
            RECT cur;
            GetWindowRect(hwnd, &cur);
            SetWindowPos(hwnd, nullptr,
                         self->m_restoreGeomX, self->m_restoreGeomY,
                         cur.right - cur.left, cur.bottom - cur.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            if (self->m_restoreGeomMax) {
                self->m_maximized = true;
                self->m_startedMaximized = true;
                ShowWindow(hwnd, SW_MAXIMIZE);
            }
            self->m_hasRestoreGeom = false;
            self->syncTitleBar();
        }

        // Rounded / right-angle corners + theme border colour. Do this after
        // WS_THICKFRAME and the startup geometry are final (DWM needs the
        // final frame; Win10's region needs the final size).
        windowChrome::setRoundedCornersEnabled(self->m_cfg->getRoundedCorners());
        windowChrome::applyRoundedCorners(self, self->m_cfg->getRoundedCorners(),
                                          self->m_theme.colors().text1);

        // Hand the NC area to us before anything is painted into it: DWM
        // discards WM_NCPAINT while it owns the band, which is what made the
        // outer border vanish "sometimes". Independent of the glow switch.
        windowChrome::takeOverFrame(self);
        // Outer gradient last: it needs the final frame geometry and the
        // final corner state to line the ramp up with the border line.
        windowShadow::attach(self);
        // Same first-frame guarantee as DialogBase::show(): ShowWindow already
        // put the window on screen and everything above ran after that. Force
        // frame + border + corner arcs to be painted synchronously so the
        // first DWM composite never shows a bare undecorated frame.
        // (Reuses this function's `hwnd`; re-declaring it here shadowed the
        // outer one and tripped /W4 C4456.)
        RedrawWindow(hwnd, nullptr, nullptr,
                     RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
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
    // 1px outer border is drawn in WM_NCPAINT (non-client area) - except the
    // TOP line, which this repaints. See the comment below.
    //
    // The outer 1px border is a non-client thing: WM_NCCALCSIZE insets 1px on
    // left/right/bottom, so those three lines sit in the NC strip and only
    // WM_NCPAINT touches them. The top has NO inset, so its 1px line lands on
    // client row 0 - and every client repaint wipes it, while Windows only
    // re-sends WM_NCPAINT on move/size/frame changes.
    // Measured (2026-10-22, temp/diag_border.py): moving the mouse off the
    // title bar (which redraws the caption buttons) erased the whole top line;
    // it came back only after dragging the window.
    // Repainting it here, after all children, keeps it present; FLTK's clip on
    // a partial damage trims it to the region being redrawn.
    // Top 1px border. The top edge has NO non-client inset (an inset there
    // would shift the title bar), so this line lands on client row 0 and every
    // client repaint wipes it while Windows only re-sends WM_NCPAINT on move /
    // size / frame changes. Repaint it here, after all children - measured
    // 2026-10-22: moving the mouse off the title bar erased the whole top line
    // until the window was dragged.
    // Corners are hard-drawn since 2026-10-22, so the rounded case must draw
    // the straight part itself and leave only the two arcs to the NC painter;
    // previously this was skipped entirely and relied on DWM.
    {
        fl_color(m_theme.colors().text1);
        if (windowChrome::roundedCornersActive(this)) {
            const int rad = windowChrome::roundedCornerRadiusPx(this);
            // The arc lives in WINDOW coordinates (drawn by the NC painter),
            // but fl_rectf works in CLIENT coordinates, and the client origin
            // is inset by NC_PAD on the left (no inset at the top). Using
            // `rad` raw started the line NC_PAD late on the left and ran NC_PAD
            // too far on the right - a bare NC_PAD-wide gap at every
            // top corner (measured 2026-10-22, temp/hc_main_rounded1_full.png).
            const int x0 = rad - NC_PAD;
            const int len = w() - 2 * (rad - NC_PAD);
            if (len > 0) fl_rectf(x0, 0, len, 1);
        } else {
            fl_rectf(0, 0, w(), 1);
        }
    }
#if defined(_WIN32)
    // The corner arcs are anti-aliased pixel by pixel against whatever is
    // under them. The NC painter draws the same pixels, but everything it
    // touches on client row 0 / inside the client rect is erased by the next
    // FLTK repaint (same reason the top border line above is drawn here), so
    // the client-side half has to be redrawn from draw() as well. Overlap is
    // harmless: the colour is computed from the background, not accumulated.
    if (windowChrome::roundedCornersActive(this)) {
        static windowChrome::FramePixel pix[1024];
        const int n = windowChrome::buildCornerStroke(
            this, (unsigned int)m_theme.colors().text1, NC_PAD, 0,
            pix, (int)(sizeof(pix) / sizeof(pix[0])));
        for (int i = 0; i < n; ++i) {
            fl_color((Fl_Color)pix[i].colour);
            fl_rectf(pix[i].x, pix[i].y, 1, 1);
        }
    }
#endif
#if defined(_WIN32)
    // Keep the non-client border in sync with the client area. Windows
    // re-sends WM_NCPAINT on frame/size changes but NOT when only the client
    // repaints, so toggling a bar, opening the preview or switching a tab could
    // leave the border stale (measured, back when the strip was filled with
    // sampled colours: band segments
    // that no longer lined up with anything, one of them bare FLTK grey).
    // Re-invalidate whenever the layout or any widget colour changes.
    if (HWND hwnd = fl_xid(this)) {
        static unsigned long long s_bandSig = 0;
        const unsigned long long sig = bandSignature(this, 3);
        if (sig != s_bandSig) {
            s_bandSig = sig;
            RedrawWindow(hwnd, nullptr, nullptr,
                         RDW_FRAME | RDW_INVALIDATE | RDW_NOCHILDREN);
        }
    }
#endif
}

// Override resize so that when the window size changes (edge drag,
// maximize/restore, or user-initiated resize) all child widgets are
// repositioned correctly. layoutTabs() handles the title bar, menu bar,
// tabs, find bar, and status bar.
void MainWindow::resize(int X, int Y, int W, int H) {
    Fl_Double_Window::resize(X, Y, W, H);
    layoutTabs();
    // Win10 region fallback must be rebuilt for the new size; also keeps the
    // maximized/restored corner state in sync.
    windowChrome::refreshRoundedCorners(this);
}

// Record the geometry to apply once the custom frame is ready (fixTaskbarCb).
void MainWindow::setRestoreGeometry(int x, int y, bool maximized) {
    m_hasRestoreGeom = true;
    m_restoreGeomX = x;
    m_restoreGeomY = y;
    m_restoreGeomMax = maximized;
}

// Toggle between maximized and restored (previous geometry).
void MainWindow::toggleMaximize() {
    HWND hwnd = fl_xid(this);
    if (!hwnd) return;

    // Decide by the real zoom state only. A window that merely fills the
    // work area (e.g. its persisted size happens to equal the maximized
    // size) is NOT maximized; treating it as such made the maximize button
    // a no-op (SW_RESTORE on a non-zoomed window does nothing).
    if (IsZoomed(hwnd)) {
        // Restore — ShowWindow triggers WM_SIZE(SIZE_RESTORED) which
        // FLTK handles to update its internal coordinates.
        ShowWindow(hwnd, SW_RESTORE);
        m_maximized = false;
        if (m_startedMaximized) {
            m_startedMaximized = false;
            // 托底：启动时窗口就是最大化（上次退出时最大化），那么首次
            // 还原不回到上次的普通尺寸，而是回到默认 800x600 并居中。
            RECT work;
            if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
                const int outerW = DEFAULT_WIN_W + 2 * WindowFrame::NC_PAD;
                const int outerH = DEFAULT_WIN_H + WindowFrame::NC_PAD;
                int nx = work.left + ((work.right  - work.left) - outerW) / 2;
                int ny = work.top  + ((work.bottom - work.top)  - outerH) / 2;
                // SetWindowPos 的 WM_SIZE 会让 FLTK 同步 w/h 并触发 layoutTabs。
                SetWindowPos(hwnd, nullptr, nx, ny, outerW, outerH, SWP_NOZORDER);
            }
        }
    } else {
        // Maximize — ShowWindow triggers WM_GETMINMAXINFO (which we
        // handle to constrain to the work area) and WM_NCCALCSIZE
        // (which defines our custom NC border). No FLTK fake_X_wm()
        // interference, so the geometry is always correct.
        m_maximized = true;
        ShowWindow(hwnd, SW_MAXIMIZE);
    }
    syncTitleBar();
    // Maximized windows are square; restore re-applies the user setting.
    windowChrome::refreshRoundedCorners(this);
}

// Apply the round/right-angle corner preference via DWM API.
//   roundCorner=true  → DWMWCP_DEFAULT (let the system decide, rounded on Win11)
//   roundCorner=false → DWMWCP_DONOTROUND (right-angle)
//
// After setting the attribute we must force a non-client area repaint
// --------------------------------------------------------------------------
// Settings persistence
// --------------------------------------------------------------------------
