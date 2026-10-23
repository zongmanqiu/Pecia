// WindowShadow.cpp - layered shadow window that sits under a border(0) window.
//
// Geometry note (measured, see temp/probe_shadow3.txt and verify_shadow3.py):
// the shadow is aligned to GetWindowRect(), NOT to DWMWA_EXTENDED_FRAME_BOUNDS.
//
// EXTENDED_FRAME_BOUNDS is right for a plain WS_THICKFRAME window, whose outer
// strip is an *invisible* DWM resize border: there the visual edge really is
// inset, and aligning to the window rect would smear the shadow's opaque core
// over that transparent strip (a flat dark outline, not a gradient).
//
// Pecia is the opposite case: our border(0) windows handle WM_NCCALCSIZE and
// paint the NC_PAD strip themselves (WM_NCPAINT fills it with the window
// background), so the strip is opaque and the visual edge IS the window rect.
// Measured: EXTENDED_FRAME_BOUNDS comes back 5px inset on left/right/bottom,
// which buried 5 of the 8 ramp pixels under the window's own border and left
// only the faintest 3px visible.
//
// Z-order note (measured): SetWindowPos(A, B) inserts A directly *behind* B,
// so the shadow is placed with SetWindowPos(shadow, mainHwnd, ...). It must be
// shown first - combining SWP_SHOWWINDOW with an insert-after handle on a
// not-yet-shown window silently leaves the shadow on top, which tints the
// whole window grey.
#include "WindowShadow.h"

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/x.H>

#include "WindowFrame.h"   // windowCoversWorkArea / windowChrome::*
#include "ui/NativeSubclass.h"
#include "core/Theme.h"       // g_borderColour (same colour as every frame)

#if defined(_WIN32)
#include <windows.h>
#include <dwmapi.h>
#include <cmath>
#endif

namespace {

#if defined(_WIN32)

const wchar_t *kShadowHwnd = L"Pecia.WindowShadow.Hwnd";
const wchar_t *kShadowW    = L"Pecia.WindowShadow.W";
const wchar_t *kShadowH    = L"Pecia.WindowShadow.H";
const wchar_t *kShadowR    = L"Pecia.WindowShadow.R";
const wchar_t *kShadowG    = L"Pecia.WindowShadow.Glow";
// Colour the 1px ring was rasterised with (stored +1 so 0 stays "never built").
// The ring is part of this bitmap, so a theme switch has to rebuild it -
// without this key the arcs kept the previous theme's colour while the straight
// border (painted live from the theme) already had the new one, which read as
// "the border is missing in places" (measured 2026-10-22).
const wchar_t *kShadowB    = L"Pecia.WindowShadow.Border";
// "This window takes part in the glow" - survives detach() so a window that
// was shown while the glow was off can still be found when it is turned on.
const wchar_t *kShadowManaged = L"Pecia.WindowShadow.Managed";

// Gradient tuning.
//   kRampPx   width of the ramp in logical px (scaled by DPI)
//   kAlphaMax opacity of the un-blurred shadow at the window edge
//             (the Gaussian profile halves this at d=0)
//   kSigmaPx  Gaussian blur radius in logical px. The old profile was a
//             linear 6px/80-alpha falloff: it read as a hard dark band with
//             no tail. The reference shadow measured 2026-10-22 has an edge
//             alpha of ~44/255 and a Gaussian tail (~8px), which this models.
const int    kRampPx   = 9;
const int    kAlphaMax = 90;
const double kSigmaPx  = 3.0;

// Which colour the outward ramp is drawn in (experiment 2026-10-22).
//
//   false - the ramp is BLACK: a classic drop shadow. It darkens whatever is
//           behind the window, so it shows on light backgrounds and is
//           invisible on black ones (0 x (1-a) = 0).
//   true  - the ramp uses the window's BORDER colour. In a dark theme that is
//           white, so the shadow becomes a rim glow instead of a shadow: it
//           shows on dark backgrounds and vanishes on white ones.
//
// Flip this one constant to go back. Nothing else depends on the choice: the
// ramp geometry, alpha and caching key are identical either way.
const bool kGlowFollowsBorder = true;

bool s_classReady = false;
bool s_enabled    = true;   // View > Window Shadow (default on)
UINT s_shadowMessage = 0;

int dpiOf(HWND hwnd) {
    using GetDpiForWindowFn = UINT (WINAPI *)(HWND);
    static GetDpiForWindowFn getDpiForWindow = []() -> GetDpiForWindowFn {
        HMODULE u = GetModuleHandleW(L"user32.dll");
        return u ? (GetDpiForWindowFn)GetProcAddress(u, "GetDpiForWindow") : nullptr;
    }();
    if (getDpiForWindow) {
        UINT dpi = getDpiForWindow(hwnd);
        if (dpi >= 96) return int(dpi);
    }
    HDC dc = GetDC(hwnd);
    int dpi = 96;
    if (dc) {
        dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(hwnd, dc);
    }
    return dpi < 96 ? 96 : dpi;
}

void ensureShadowClass() {
    if (s_classReady) return;
    WNDCLASSW wc = {};
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"Pecia.WindowShadow";
    // No background brush: the window is fully layered, GDI never paints it.
    s_classReady = (RegisterClassW(&wc) != 0);
}

// The rect a human calls "the window": for Pecia's self-painted frames that
// is the window rect (see the geometry note above).
void visualRect(HWND hwnd, RECT *out) {
    GetWindowRect(hwnd, out);
}

// Build the frame layer bitmap. W x H is the visual window size, N is the
// outward ramp, `radius` the corner radius (0 = square).
//
// TWO jobs in one bitmap, because a layered window is the only surface in our
// stack that can hold per-pixel alpha:
//
//   1. the 1px BORDER RING along the window's outline, anti-aliased by real
//      coverage (see below). The window region is inset 1px, so this ring is
//      the visible edge - that is what removes the staircase.
//   2. the outward GLOW (black, alpha ramp), when View > Window Shadow is on.
//
// The distance is the signed distance to the *rounded rectangle*, not to the
// plain rectangle: the little triangle between the rectangle's corner and the
// arc is OUTSIDE the window, and treating it as inside paints an opaque wedge.
HBITMAP makeFrameBmp(int W, int H, int N, int radius, int aMax, double sigma,
                     bool glow, unsigned int borderFl, unsigned int glowFl) {
    const int bw = W + 2 * N, bh = H + 2 * N;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize     = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth    = bw;
    bi.bmiHeader.biHeight   = -bh;          // top-down
    bi.bmiHeader.biPlanes   = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) return nullptr;

    uchar br = 0, bg = 0, bb = 0;
    Fl::get_color((Fl_Color)borderFl, br, bg, bb);
    // The outward ramp is drawn in its own colour (black for a classic drop
    // shadow, the border colour for a rim glow - see kGlowFollowsBorder).
    uchar gr = 0, gg = 0, gb = 0;
    Fl::get_color((Fl_Color)glowFl, gr, gg, gb);

    const double r  = double(radius);
    const double hx = double(W) * 0.5;      // half size of the window rect
    const double hy = double(H) * 0.5;
    const double cx = double(N) + hx;       // centre of the window rect
    const double cy = double(N) + hy;

    struct Px { BYTE b, g, r, a; };
    Px *p = static_cast<Px *>(bits);
    for (int y = 0; y < bh; ++y) {
        for (int x = 0; x < bw; ++x) {
            // Rounded-rect SDF (negative inside).
            const double qx = std::fabs(double(x) + 0.5 - cx) - hx + r;
            const double qy = std::fabs(double(y) + 0.5 - cy) - hy + r;
            const double qmax = qx > qy ? qx : qy;
            const double inside  = qmax < 0.0 ? qmax : 0.0;
            const double ox = qx > 0.0 ? qx : 0.0;
            const double oy = qy > 0.0 ? qy : 0.0;
            const double d = inside + std::sqrt(ox * ox + oy * oy) - r;

            Px &q = p[size_t(y) * bw + x];
            q.b = q.g = q.r = q.a = 0;

            // Coverage of the border stroke at this pixel: 4x4 supersampled
            // area, not a point sample - on the 45-degree stretch of an 8px
            // arc point samples jump straight from 72% to 0, which is the
            // residual jaggedness. Only the part outside the window region is
            // visible from here; the inner half is painted by the window
            // itself (buildCornerStroke), against the colour it has
            // underneath. Same profile on both sides, so there is no seam.
            const double bcov =
                (d > -1.5 && d < 1.5)
                    ? windowChrome::strokeCoverageArea(double(x - N), double(y - N),
                                                       W, H, r)
                    : 0.0;

            double ga = 0.0;
            if (glow && d > 0.0) {
                // Gaussian blur of the shadow edge. The reference (measured
                // 2026-10-22) is erfc-like: alpha ~44/255 at d=0, ~23 at d=2,
                // ~9 at d=4, ~1 at d=6. 0.5*erfc(d/(sigma*sqrt(2))) with
                // sigma=3px reproduces that curve; aMax is the un-blurred
                // opacity, so the edge value is aMax/2.
                ga = (double(aMax) / 255.0) * 0.5 *
                     std::erfc(d / (sigma * std::sqrt(2.0)));
                if (ga < 1.0 / 255.0) ga = 0.0;
            }
            if (bcov <= 0.0 && ga <= 0.0) continue;

            const double A = bcov + ga * (1.0 - bcov);
            // Premultiplied: the ring contributes its colour times its own
            // coverage; the glow contributes (glow colour) x (its alpha) x
            // (the part of the pixel the ring does not already cover).
            q.a = BYTE(A * 255.0 + 0.5);
            q.r = BYTE(double(br) * bcov + double(gr) * ga * (1.0 - bcov) + 0.5);
            q.g = BYTE(double(bg) * bcov + double(gg) * ga * (1.0 - bcov) + 0.5);
            q.b = BYTE(double(bb) * bcov + double(gb) * ga * (1.0 - bcov) + 0.5);
        }
    }
    return bmp;
}

#endif  // _WIN32

}  // namespace

namespace windowShadow {

#if defined(_WIN32)
// Popup menus are transient Fl_Window objects; FLTK creates a fresh HWND for
// every popup. Keep the glow glued to it (menus can be repositioned as the
// mouse moves between submenus) and destroy it with the menu. The menu's
// own Fl_Window::hide() hook detaches first; this proc is the safety net for
// the native window disappearing.
static LRESULT WINAPI menuShadowProc(HWND hwnd, UINT msg, WPARAM wp,
                                     LPARAM lp) {
    switch (msg) {
    case WM_WINDOWPOSCHANGED:
    case WM_SHOWWINDOW: {
        LRESULT ret = nativeSubclass::forward(hwnd, menuShadowProc, msg, wp, lp);
        sync(fl_find(hwnd));
        return ret;
    }
    case WM_DESTROY:
    case WM_NCDESTROY:
        detachHwnd(reinterpret_cast<void *>(hwnd));
        nativeSubclass::detachAll(hwnd);
        break;
    }
    return nativeSubclass::forward(hwnd, menuShadowProc, msg, wp, lp);
}
#endif

void setEnabled(bool on) {
    s_enabled = on;
}

bool enabled() {
    return s_enabled;
}

void attach(Fl_Window *win) {
#if defined(_WIN32)
    if (!win) return;
    HWND hwnd = fl_xid(win);
    if (!hwnd) return;

    // Popups are created/destroyed per invocation; hook their HWND so later
    // position changes keep the glow attached and a destroyed menu takes its
    // shadow with it.
    if (win->menu_window()) nativeSubclass::install(hwnd, menuShadowProc);

    // Opt in first, then ALWAYS create the layer: it also carries the 1px
    // anti-aliased border ring, which has to be there whether or not the glow
    // is switched on.
    SetPropW(hwnd, kShadowManaged, (HANDLE)(INT_PTR)1);
    if (GetPropW(hwnd, kShadowHwnd)) { sync(win); return; }

    ensureShadowClass();
    RECT rc;
    visualRect(hwnd, &rc);
    int W = rc.right - rc.left, H = rc.bottom - rc.top;
    if (W <= 0 || H <= 0) return;
    const int N = MulDiv(kRampPx, dpiOf(hwnd), 96);

    HWND sh = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT |
                              WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                              L"Pecia.WindowShadow", L"", WS_POPUP,
                              rc.left - N, rc.top - N, W + 2 * N, H + 2 * N,
                              nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!sh) return;

    SetPropW(hwnd, kShadowHwnd, sh);
    SetPropW(hwnd, kShadowW, nullptr);   // 0 = no bitmap cached yet
    SetPropW(hwnd, kShadowH, nullptr);
    SetPropW(hwnd, kShadowR, nullptr);
    SetPropW(hwnd, kShadowG, nullptr);
    SetPropW(hwnd, kShadowB, nullptr);
    sync(win);
#else
    (void)win;
#endif
}

void sync(Fl_Window *win) {
#if defined(_WIN32)
    if (!win) return;
    HWND hwnd = fl_xid(win);
    if (!hwnd) return;
    HWND sh = static_cast<HWND>(GetPropW(hwnd, kShadowHwnd));
    if (!sh) return;

    RECT rc;
    visualRect(hwnd, &rc);
    int W = rc.right - rc.left, H = rc.bottom - rc.top;
    if (W <= 0 || H <= 0) { ShowWindow(sh, SW_HIDE); return; }

    // Ramp width and corner radius are independent: the ramp is how far the
    // glow reaches, the radius is the window's own corner shape that the glow
    // has to wrap around.
    const int N      = MulDiv(kRampPx, dpiOf(hwnd), 96);
    const int radius = windowChrome::roundedCornersActive(win)
                           ? windowChrome::roundedCornerRadiusPx(win) : 0;

    const int cachedW = int(INT_PTR(GetPropW(hwnd, kShadowW)));
    const int cachedH = int(INT_PTR(GetPropW(hwnd, kShadowH)));
    const int cachedR = int(INT_PTR(GetPropW(hwnd, kShadowR)));
    const bool cachedG = GetPropW(hwnd, kShadowG) != nullptr;
    HANDLE cachedBP = GetPropW(hwnd, kShadowB);
    const bool hasCachedB = cachedBP != nullptr;
    const unsigned int cachedB = hasCachedB
                                     ? (unsigned int)((INT_PTR)cachedBP - 1) : 0;
    const unsigned int borderNow = windowChrome::windowBorderColour(win);
    // The ring follows the corner radius and its colour, the glow follows the
    // toggle - each of those needs a rebuild even though the size did not
    // change.
    const bool rebuild = (cachedW != W || cachedH != H ||
                          cachedR != radius || cachedG != s_enabled ||
                          !hasCachedB || cachedB != borderNow);

    if (rebuild) {
        const unsigned int border = borderNow;
        const unsigned int glowColour =
            kGlowFollowsBorder ? borderNow : (unsigned int)FL_BLACK;
        HBITMAP bmp = makeFrameBmp(W, H, N, radius, kAlphaMax,
                                   kSigmaPx * dpiOf(hwnd) / 96.0,
                                   s_enabled, border, glowColour);
        if (bmp) {
            HDC sdc = GetDC(nullptr);
            HDC mdc = CreateCompatibleDC(sdc);
            HGDIOBJ old = SelectObject(mdc, bmp);
            BLENDFUNCTION bf = { 0, 0, 255, AC_SRC_ALPHA };
            POINT ptSrc = { 0, 0 };
            SIZE  sz    = { W + 2 * N, H + 2 * N };
            UpdateLayeredWindow(sh, sdc, nullptr, &sz, mdc, &ptSrc, 0, &bf, ULW_ALPHA);
            SelectObject(mdc, old);
            DeleteDC(mdc);
            ReleaseDC(nullptr, sdc);
            DeleteObject(bmp);   // UpdateLayeredWindow copied the pixels
            SetPropW(hwnd, kShadowW, (HANDLE)(INT_PTR)W);
            SetPropW(hwnd, kShadowH, (HANDLE)(INT_PTR)H);
            SetPropW(hwnd, kShadowR, (HANDLE)(INT_PTR)radius);
            SetPropW(hwnd, kShadowG, s_enabled ? (HANDLE)(INT_PTR)1 : nullptr);
            SetPropW(hwnd, kShadowB, (HANDLE)(INT_PTR)(border + 1));
        }
    }

    // Visibility is decided AFTER the bitmap is built, not before: a window
    // that calls attach() while still hidden (DialogBase::show does, so the
    // glow is ready in the same frame the window appears) would otherwise
    // skip the build and pay the whole bitmap cost on its first visible
    // sync - that is the "glow shows up a beat late" the About dialog had.
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd) || windowCoversWorkArea(win)) {
        ShowWindow(sh, SW_HIDE);
        return;
    }

    // Show first, then insert directly behind the real window - see the
    // Z-order note at the top of this file.
    ShowWindow(sh, SW_SHOWNOACTIVATE);
    SetWindowPos(sh, hwnd, rc.left - N, rc.top - N, W + 2 * N, H + 2 * N,
                 SWP_NOACTIVATE);
#else
    (void)win;
#endif
}

void detachHwnd(void *nativeHandle) {
#if defined(_WIN32)
    HWND hwnd = static_cast<HWND>(nativeHandle);
    if (!hwnd) return;
    HWND sh = static_cast<HWND>(GetPropW(hwnd, kShadowHwnd));
    // Destroy (not just hide): a hidden-but-alive shadow still costs a window
    // handle, and if the process winds down before the next sync it can flash
    // on screen after the real window is gone.
    if (sh) DestroyWindow(sh);
    RemovePropW(hwnd, kShadowHwnd);
    RemovePropW(hwnd, kShadowW);
    RemovePropW(hwnd, kShadowH);
    RemovePropW(hwnd, kShadowR);
    RemovePropW(hwnd, kShadowG);
    // kShadowManaged deliberately survives: the window stays a glow candidate
    // so another attach() (switch toggled back on) can find it.
#else
    (void)nativeHandle;
#endif
}

void detach(Fl_Window *win) {
#if defined(_WIN32)
    if (!win) return;
    detachHwnd(fl_xid(win));
#else
    (void)win;
#endif
}

void applyAll() {
#if defined(_WIN32)
    for (Fl_Window *w = Fl::first_window(); w; w = Fl::next_window(w)) {
        HWND hwnd = fl_xid(w);
        if (!hwnd) continue;
        // Only windows that called attach() - keeps menus, tooltips and any
        // other FLTK helper window out of it.
        if (!GetPropW(hwnd, kShadowManaged)) continue;
        // Always (re)build: the layer is also the 1px AA border ring, so it
        // must exist even with the glow switched off - only the glow part of
        // the bitmap is dropped.
        attach(w);
    }
#endif
}

void syncAll() {
#if defined(_WIN32)
    for (Fl_Window *w = Fl::first_window(); w; w = Fl::next_window(w)) {
        HWND hwnd = fl_xid(w);
        if (!hwnd) continue;
        if (GetPropW(hwnd, kShadowHwnd)) sync(w);
    }
#endif
}

unsigned int shadowMessage() {
#if defined(_WIN32)
    if (!s_shadowMessage)
        s_shadowMessage = RegisterWindowMessageW(L"Pecia.WindowShadow");
    return s_shadowMessage;
#else
    return 0;
#endif
}

void broadcastEnabled(bool on) {
#if defined(_WIN32)
    UINT msg = shadowMessage();
    if (msg) PostMessageW(HWND_BROADCAST, msg, on ? 1 : 0, 0);
#else
    (void)on;
#endif
}

}  // namespace windowShadow

// FLTK menu-window hooks (see the Pecia patch in Fl_Menu.cxx). They live here
// because WindowShadow.cpp is linked into Pecia, PeciaLua, PeciaAIChat and the
// two UI smoke tests; other fltk.lib consumers resolve the stub via
// /alternatename in Fl_Menu.cxx.
void peciaMenuShadowShown(Fl_Window *win) {
    if (win && windowShadow::enabled()) {
        // Menus do not go through applyRoundedCorners(), so register the same
        // theme border colour the menu's FL_BORDER_BOX is drawn with. The
        // ring/glow then matches windows in both light and dark themes.
        windowChrome::setBorderColour(win, (unsigned int)g_borderColour);
        windowShadow::attach(win);
    }
}

void peciaMenuShadowHidden(Fl_Window *win) {
    if (win) windowShadow::detach(win);
}
