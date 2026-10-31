// WindowFrame.cpp - Custom window frame implementation
#include "WindowFrame.h"

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Widget.H>
#include <FL/Fl_Scrollbar.H>
#include <FL/x.H>

#if defined(_WIN32)
#include <windows.h>
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM
#include <dwmapi.h>    // DWMWA_WINDOW_CORNER_PREFERENCE / DWMWA_BORDER_COLOR
#include <cmath>

#endif

namespace {
// Upper bound for one window's corner stroke: at radius 8 the whole band is
// ~4 x 60 pixels, so 1024 leaves a lot of headroom (even at r = 40px).
const int kMaxFramePixels = 1024;

}  // namespace

WindowFrame::WindowFrame() {
}

#if defined(_WIN32)
namespace {
// Geometry test only - kept separate from the Fl_Window wrapper so the HWND
// state machine below (and its unit test) can share it.
bool hwndCoversWorkArea(HWND hwnd) {
    if (!hwnd) return false;

    RECT rc;
    if (!GetWindowRect(hwnd, &rc)) return false;

    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfoW(mon, &mi)) return false;

    // Our maximized geometry intentionally overhangs the work area by NC_PAD
    // on the resize edges, so "covers" (not "equals") is the right test.
    const RECT &work = mi.rcWork;
    return windowChrome::rectCoversWork(rc, work);
}
}  // namespace

bool windowCoversWorkArea(const Fl_Window *win) {
    if (!win) return false;
    return hwndCoversWorkArea(fl_xid(win));
}
#else
bool windowCoversWorkArea(const Fl_Window *) { return false; }
#endif

// ---------------------------------------------------------------------------
// Rounded corners
// ---------------------------------------------------------------------------

namespace {

#if defined(_WIN32)
const wchar_t *kRoundedProp = L"Pecia.RoundedCorners";
const wchar_t *kBorderProp  = L"Pecia.RoundedBorderColor";

bool s_cornersEnabled = false;
UINT s_roundMessage = 0;

// NOTE (2026-10-22): corners are HARD-DRAWN on every Windows version.
//
// This used to fork: Win11 used DWMWCP_ROUND (DWM draws an anti-aliased arc
// and its own border line via DWMWA_BORDER_COLOR), Win10/8 used SetWindowRgn
// plus a hand-drawn RoundRect. Two paint paths meant two sets of bugs - the
// DWM border colour was cached inside DWM and only refreshed when some other
// window forced a re-apply, which read exactly like "switching theme does not
// recolour the main window's border". Keeping only the region path makes the
// 1px line come from one place (WM_NCPAINT, reading the live theme) on every
// OS, so what we develop and test on Win11 is what Win10/8 actually run.
int cornerRadiusPx(HWND hwnd) {
    UINT dpi = 96;
    using GetDpiForWindowFn = UINT (WINAPI *)(HWND);
    static GetDpiForWindowFn getDpiForWindow = []() -> GetDpiForWindowFn {
        HMODULE u = GetModuleHandleW(L"user32.dll");
        if (!u) return nullptr;
        return (GetDpiForWindowFn)GetProcAddress(u, "GetDpiForWindow");
    }();
    if (getDpiForWindow) {
        dpi = getDpiForWindow(hwnd);
    } else {
        HDC dc = GetDC(hwnd);
        if (dc) {
            dpi = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
            ReleaseDC(hwnd, dc);
        }
    }
    int radius = MulDiv(8, (int)dpi, 96);   // 8 logical px, same feel as Win11
    if (radius < 2) radius = 2;
    return radius;
}

void setWindowRegion(HWND hwnd, bool round) {
    if (!round) {
        SetWindowRgn(hwnd, nullptr, TRUE);
        return;
    }
    RECT rc;
    if (!GetWindowRect(hwnd, &rc)) return;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    int radius = cornerRadiusPx(hwnd);

    // No inset: the window must own every pixel it can paint, because the
    // inner half of the border stroke has to be blended against the window's
    // own content (a region cannot do that - it is 1-bit). The outer half of
    // the stroke lies outside this region and is painted by the frame layer.
    HRGN rgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, radius * 2, radius * 2);
    if (rgn && !SetWindowRgn(hwnd, rgn, TRUE))
        DeleteObject(rgn);   // SetWindowRgn did not take ownership
}
#endif

}  // namespace

namespace windowChrome {

#if defined(_WIN32)
void setRoundedPreference(HWND hwnd, bool on) {
    if (!hwnd) return;
    // Non-null even when off: SetPropW(..., 0) would REMOVE the property and
    // make the window look "unmanaged" to refresh/reapply.
    SetPropW(hwnd, kRoundedProp, (HANDLE)(INT_PTR)(on ? 2 : 1));
}

bool roundedPreference(HWND hwnd) {
    if (!hwnd) return false;
    return GetPropW(hwnd, kRoundedProp) == (HANDLE)(INT_PTR)2;
}

bool roundedNow(HWND hwnd) {
    return effectiveRounded(roundedPreference(hwnd), hwndCoversWorkArea(hwnd));
}
#endif

void setRoundedCornersEnabled(bool on) {
#if defined(_WIN32)
    s_cornersEnabled = on;
#else
    (void)on;
#endif
}

bool roundedCornersEnabled() {
#if defined(_WIN32)
    return s_cornersEnabled;
#else
    return false;
#endif
}

bool roundedCornersActive(const Fl_Window *win) {
#if defined(_WIN32)
    if (!win) return false;
    HWND hwnd = fl_xid(win);
    if (!hwnd) return false;
    // Maximized / work-area-covering windows are square even though the stored
    // property (the user preference) still says "rounded".
    return roundedNow(hwnd);
#else
    (void)win;
    return false;
#endif
}

bool dwmCornersActive() {
#if defined(_WIN32)
    // Corners are always hard-drawn now (see the note in the anonymous
    // namespace), so DWM never owns the arc: every caller must paint the
    // 1px rounded line itself. Kept as a function because the NC painters
    // and DialogBase gate their border drawing on it.
    return false;
#else
    return false;
#endif
}

int roundedCornerRadiusPx(const Fl_Window *win) {
#if defined(_WIN32)
    if (!win) return 8;
    HWND hwnd = fl_xid(win);
    return hwnd ? cornerRadiusPx(hwnd) : 8;
#else
    (void)win;
    return 8;
#endif
}

void applyRoundedCorners(Fl_Window *win, bool on, unsigned int borderColor) {
#if defined(_WIN32)
    if (!win) return;
    HWND hwnd = fl_xid(win);
    if (!hwnd) return;

    // Store the PREFERENCE, then ask the state machine what that means for
    // the current geometry. Writing the effective state here instead was the
    // bug fixed 2026-10-31 - see effectiveRounded() in the header.
    setRoundedPreference(hwnd, on);
    const bool round = roundedNow(hwnd);
    SetPropW(hwnd, kBorderProp,
             (HANDLE)(INT_PTR)(borderColor + 1));   // 0 is a valid Fl_Color

    // One path for every supported Windows version.
    setWindowRegion(hwnd, round);
#else
    (void)win; (void)on; (void)borderColor;
#endif
}

void refreshRoundedCorners(Fl_Window *win) {
#if defined(_WIN32)
    if (!win) return;
    HWND hwnd = fl_xid(win);
    if (!hwnd) return;
    HANDLE p = GetPropW(hwnd, kRoundedProp);
    if (!p) return;   // window never opted in
    // Re-derive the effective state from the stored preference + the CURRENT
    // geometry. This is the call that used to read the effective state back
    // and thereby made a restored window stay square forever.
    setWindowRegion(hwnd, roundedNow(hwnd));
#else
    (void)win;
#endif
}

void refreshFrameBand(Fl_Window *win) {
#if defined(_WIN32)
    if (!win) return;
    HWND hwnd = fl_xid(win);
    if (!hwnd) return;
    // RDW_FRAME invalidates the non-client region, which is what makes Windows
    // re-send WM_NCPAINT. RDW_INVALIDATE alone only touches the client area.
    RedrawWindow(hwnd, nullptr, nullptr,
                 RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOCHILDREN);
#else
    (void)win;
#endif
}

void takeOverFrame(Fl_Window *win) {
#if defined(_WIN32)
    if (!win) return;
    HWND hwnd = fl_xid(win);
    if (!hwnd) return;
    // DWM must stop drawing the non-client area. With WS_THICKFRAME and our
    // WM_NCCALCSIZE inset it treats the band between EXTENDED_FRAME_BOUNDS and
    // the window rect as a frame and fills it with its own drop shadow, and -
    // worse - silently DISCARDS whatever WM_NCPAINT paints there (a pure-red
    // fill never reached the screen, measured 2026-10-22). Since 2026-10-22 the
    // 1px border and the rounded arc are hard-drawn by us on every Windows
    // version, so this is what makes them reach the screen at all.
    using DwmSetAttrFn = HRESULT (WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
    static DwmSetAttrFn setFn = []() -> DwmSetAttrFn {
        HMODULE h = LoadLibraryW(L"dwmapi.dll");
        return h ? (DwmSetAttrFn)GetProcAddress(h, "DwmSetWindowAttribute")
                 : nullptr;
    }();
    if (setFn) {
        DWORD ncrp = 1;   // DWMNCRP_DISABLED
        setFn(hwnd, 2 /*DWMWA_NCRENDERING_POLICY*/, &ncrp, sizeof(ncrp));
    }
#else
    (void)win;
#endif
}

bool bandColourAt(Fl_Group *g, int x, int y, unsigned int *colour) {
    if (!g || !colour) return false;
    for (int i = g->children() - 1; i >= 0; --i) {   // last child draws on top
        Fl_Widget *c = g->child(i);
        if (!c || !c->visible()) continue;
        // FLTK child coordinates are PARENT-RELATIVE, so every descent has to
        // shift the point. Comparing them against client pixels makes every
        // nested lookup miss and the band falls back to the window colour -
        // which is exactly how a 5px grey strip appeared beside a white editor
        // (measured 2026-10-22, temp/scan_band_full.py).
        if (x < c->x() || x >= c->x() + c->w()) continue;
        if (y < c->y() || y >= c->y() + c->h()) continue;
        // Hand-drawn widgets know their own colours; ask them first.
        if (auto *src = dynamic_cast<BandColourSource *>(c)) {
            unsigned int col = 0;
            if (src->bandColourAt(x - c->x(), y - c->y(), &col)) {
                *colour = col;
                return true;
            }
        }
        if (auto *sub = dynamic_cast<Fl_Group *>(c)) {
            if (bandColourAt(sub, x - c->x(), y - c->y(), colour)) return true;
        }
        if (c->box() != FL_NO_BOX) {
            *colour = (unsigned int)c->color();
            return true;
        }
        // Transparent (FL_NO_BOX): keep looking at what sits behind it.
    }
    if (g->box() != FL_NO_BOX) {
        *colour = (unsigned int)g->color();
        return true;
    }
    return false;
}

// One painter for every Pecia window (main window and both tool windows).
//
// The non-client area is exactly ONE pixel wide (WindowFrame::NC_PAD) and is
// entirely covered by the border stroked below, so there is NO strip to fill.
// That is the whole point: the old 6px strip had to be painted with colours
// sampled out of the widget tree (bandColourAt), and every wrong guess showed
// up as a coloured band inside the border - the scrollbar / tab-strip / AI-body
// seams chased through 2026-10-22. bandColourAt survives only for the corner
// arcs, which must blend with whatever they are drawn over.
bool paintNonClientFrame(Fl_Window *win, unsigned int borderColour) {
#if defined(_WIN32)
    if (!win) return false;
    HWND hwnd = fl_xid(win);
    if (!hwnd) return false;
    HDC hdc = GetWindowDC(hwnd);
    if (!hdc) return false;

    RECT rcWin;
    GetWindowRect(hwnd, &rcWin);
    const int ww = rcWin.right  - rcWin.left;
    const int wh = rcWin.bottom - rcWin.top;
    // 1px border at the window's outer edge. Rounded: the straight runs are
    // filled here and the four arcs are painted pixel by pixel with real
    // coverage (buildCornerStroke) - a GDI RoundRect stroke is a hard
    // staircase and was the "jagged corner" we kept chasing. Square: no arcs,
    // so the plain rectangle is exactly right.
    const bool rounded = roundedCornersActive(win);
    {
        uchar r = 0, g = 0, b = 0;
        Fl::get_color((Fl_Color)borderColour, r, g, b);
        if (rounded) {
            const int rad = roundedCornerRadiusPx(win);
            HBRUSH br = CreateSolidBrush(RGB(r, g, b));
            if (br) {
                RECT rcL = { 0, rad, 1, wh - rad };
                RECT rcR = { ww - 1, rad, ww, wh - rad };
                RECT rcB = { rad, wh - 1, ww - rad, wh };
                FillRect(hdc, &rcL, br);
                FillRect(hdc, &rcR, br);
                FillRect(hdc, &rcB, br);
                DeleteObject(br);
            }
            FramePixel pix[kMaxFramePixels];
            const int n = buildCornerStroke(win, borderColour, 0, 0,
                                            pix, kMaxFramePixels);
            for (int i = 0; i < n; ++i) {
                uchar cr, cg, cb;
                Fl::get_color((Fl_Color)pix[i].colour, cr, cg, cb);
                SetPixelV(hdc, pix[i].x, pix[i].y, RGB(cr, cg, cb));
            }
        } else {
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(r, g, b));
            HPEN oldPen = (HPEN)SelectObject(hdc, pen);
            HBRUSH oldBrush =
                (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, 0, 0, ww, wh);
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(pen);
        }
    }

    ReleaseDC(hwnd, hdc);
    return true;
#else
    (void)win; (void)borderColour; (void)fallbackFill;
    return false;
#endif
}

void frameClientOrigin(const Fl_Window *win, int *x, int *y) {
    if (x) *x = 0;
    if (y) *y = 0;
#if defined(_WIN32)
    if (!win) return;
    HWND hwnd = fl_xid(win);
    if (!hwnd) return;
    RECT wr = {};
    if (!GetWindowRect(hwnd, &wr)) return;
    POINT c = { 0, 0 };
    if (!ClientToScreen(hwnd, &c)) return;
    if (x) *x = c.x - wr.left;
    if (y) *y = c.y - wr.top;
#endif
}

bool pointOverScrollbar(const Fl_Window *win, int wx, int wy) {
#if defined(_WIN32)
    if (!win) return false;
    auto *root = dynamic_cast<Fl_Group *>(const_cast<Fl_Window *>(win));
    if (!root) return false;
    int ox = 0, oy = 0;
    frameClientOrigin(win, &ox, &oy);
    const int x0 = wx - ox, y0 = wy - oy;
    if (x0 < 0 || y0 < 0 || x0 >= win->w() || y0 >= win->h()) return false;

    // Descend to the topmost widget under the point. Children are laid out in
    // PARENT-relative coordinates, so subtract as we go (the same trap that
    // made bandColourAt miss from the second nesting level down).
    const Fl_Widget *hit = nullptr;
    const Fl_Group  *g = root;
    int x = x0, y = y0;
    for (int depth = 0; depth < 32; ++depth) {
        const Fl_Widget *next = nullptr;
        for (int i = g->children() - 1; i >= 0; --i) {   // last child on top
            const Fl_Widget *c = g->child(i);
            if (!c || !c->visible()) continue;
            if (x < c->x() || x >= c->x() + c->w()) continue;
            if (y < c->y() || y >= c->y() + c->h()) continue;
            next = c;
            break;
        }
        if (!next) break;
        auto *sub = dynamic_cast<const Fl_Group *>(next);
        if (!sub) { hit = next; break; }   // leaf: this is the answer
        x -= next->x();
        y -= next->y();
        g = sub;                           // group: keep looking inside it
    }
    return hit && dynamic_cast<const Fl_Scrollbar *>(hit) != nullptr;
#else
    (void)win; (void)wx; (void)wy;
    return false;
#endif
}

int buildCornerStroke(const Fl_Window *win, unsigned int borderColour,
                      int outOriginX, int outOriginY,
                      FramePixel *out, int maxPixels) {
#if defined(_WIN32)
    if (!win || !out || maxPixels <= 0) return 0;
    if (!roundedCornersActive(win)) return 0;
    HWND hwnd = fl_xid(win);
    if (!hwnd) return 0;
    RECT wr = {};
    if (!GetWindowRect(hwnd, &wr)) return 0;
    const int W = wr.right - wr.left, H = wr.bottom - wr.top;
    if (W <= 2 || H <= 2) return 0;
    const double r = double(roundedCornerRadiusPx(win));
    if (r < 1.5) return 0;

    int ox = 0, oy = 0;
    frameClientOrigin(win, &ox, &oy);

    uchar br = 0, bg = 0, bb = 0;
    Fl::get_color((Fl_Color)borderColour, br, bg, bb);

    Fl_Group *const root = const_cast<Fl_Group *>(
        static_cast<const Fl_Group *>(win));
    const int box = int(r) + 2;
    int n = 0;
    for (int corner = 0; corner < 4; ++corner) {
        const int bx = (corner & 1) ? W - box : 0;
        const int by = (corner & 2) ? H - box : 0;
        for (int y = by; y < by + box; ++y) {
            for (int x = bx; x < bx + box; ++x) {
                const double c = strokeCoverageArea(double(x), double(y),
                                                    W, H, r);
                if (c <= 0.02) continue;
                if (n >= maxPixels) return n;
                // What the window has under this pixel: the colour it just
                // filled the band with, or the widget that lives there.
                // The band is a copy of the NEAREST client pixel, so points
                // that fall in the non-client strip (x < padX, y >= h) are
                // clamped onto the client rect instead of being asked for -
                // bandColourAt answers "no" there and the fallback used to
                // paint bright specks along the left/bottom halves of the arcs
                // (measured 2026-10-22: 243/211 where the content was 225).
                int qx = x - ox, qy = y - oy;
                if (qx < 0) qx = 0;
                if (qy < 0) qy = 0;
                if (qx >= win->w()) qx = win->w() - 1;
                if (qy >= win->h()) qy = win->h() - 1;
                unsigned int under = (unsigned int)win->color();
                if (!bandColourAt(root, qx, qy, &under))
                    under = (unsigned int)win->color();
                uchar ur = 0, ug = 0, ub = 0;
                Fl::get_color((Fl_Color)under, ur, ug, ub);
                // Light-on-dark compensation only (see strokeLift): the lift
                // depends on whether the border is the bright side of the
                // pair, so the light theme's black border keeps the coverage
                // it always had.
                const double borderLum = 0.299 * br + 0.587 * bg + 0.114 * bb;
                const double underLum  = 0.299 * ur + 0.587 * ug + 0.114 * ub;
                const double a = strokeLift(c, borderLum, underLum);
                const double k = 1.0 - a;
                out[n].x = x - outOriginX;
                out[n].y = y - outOriginY;
                out[n].colour = (unsigned int)fl_rgb_color(
                    uchar(double(br) * a + double(ur) * k + 0.5),
                    uchar(double(bg) * a + double(ug) * k + 0.5),
                    uchar(double(bb) * a + double(ub) * k + 0.5));
                ++n;
            }
        }
    }
    return n;
#else
    (void)win; (void)borderColour; (void)outOriginX; (void)outOriginY;
    (void)out; (void)maxPixels;
    return 0;
#endif
}

unsigned int windowBorderColour(const Fl_Window *win) {
#if defined(_WIN32)
    if (!win) return (unsigned int)FL_BLACK;
    HWND hwnd = fl_xid(win);
    if (!hwnd) return (unsigned int)FL_BLACK;
    HANDLE p = GetPropW(hwnd, kBorderProp);
    return p ? (unsigned int)((INT_PTR)p - 1) : (unsigned int)FL_BLACK;
#else
    (void)win;
    return (unsigned int)FL_BLACK;
#endif
}

void setBorderColour(Fl_Window *win, unsigned int borderColor) {
#if defined(_WIN32)
    if (!win) return;
    HWND hwnd = fl_xid(win);
    if (!hwnd) return;
    SetPropW(hwnd, kBorderProp, (HANDLE)(INT_PTR)(borderColor + 1));
#else
    (void)win;
    (void)borderColor;
#endif
}

void reapplyRoundedCorners(bool on) {
#if defined(_WIN32)
    for (Fl_Window *w = Fl::first_window(); w; w = Fl::next_window(w)) {
        HWND hwnd = fl_xid(w);
        if (!hwnd) continue;
        HANDLE p = GetPropW(hwnd, kBorderProp);
        if (!p) continue;   // not a window we manage (menus, tooltips, ...)
        unsigned int color = (unsigned int)((INT_PTR)p - 1);
        applyRoundedCorners(w, on, color);
        // The old rounded arcs were painted into the CLIENT pixels (and the
        // 1px ring into the NC band). Changing the region alone leaves those
        // pixels behind until something forces a full repaint - which is why
        // the residue only disappeared after minimize/restore. Mark the whole
        // window + children damaged so FLTK redraws every pixel, and
        // invalidate the NC band so its ring is repainted with the new shape.
        w->redraw();
        RedrawWindow(hwnd, nullptr, nullptr,
                     RDW_FRAME | RDW_INVALIDATE | RDW_NOCHILDREN);
    }
#else
    (void)on;
#endif
}

void broadcastRoundedCorners(bool on) {
#if defined(_WIN32)
    UINT msg = roundedCornersMessage();
    if (msg) PostMessageW(HWND_BROADCAST, msg, on ? 1 : 0, 0);
#else
    (void)on;
#endif
}

unsigned int roundedCornersMessage() {
#if defined(_WIN32)
    if (!s_roundMessage)
        s_roundMessage = RegisterWindowMessageW(L"Pecia.RoundedCorners");
    return s_roundMessage;
#else
    return 0;
#endif
}

}  // namespace windowChrome

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
