// WindowFrame.h - Custom window frame management
#pragma once

#include <cmath>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "ui/BandColourSource.h"   // windowChrome::BandColourSource

class Fl_Window;
class Fl_Group;

// True when the window's outer rectangle covers its monitor's work area,
// i.e. the window is in the maximized / full-screen state.
//
// Deliberately geometry-based instead of relying on IsZoomed(): a window can
// be flagged WS_MAXIMIZE yet float off the work area (after being moved), and
// the tool windows maximize by re-applying geometry without ever setting
// WS_MAXIMIZE. Both cases must be classified by actual size, so that the
// caption icon, the 1px border and the edge-resize hit-test all agree.
bool windowCoversWorkArea(const Fl_Window *win);

// Process-wide rounded-corner setting and per-window application.
//
// Win11: DWMWA_WINDOW_CORNER_PREFERENCE via DWM (anti-aliased corners,
//        system shadow, live toggle).
// Win10: the attribute is unsupported; falls back to SetWindowRgn with a
//        DPI-scaled radius (hard-edged, no shadow) that is refreshed on
//        resize. Maximized / work-area-covering windows are always square.
//
// The border colour is passed as a raw Fl_Color value (unsigned int) so this
// header does not need to pull in FLTK headers (it is included by files that
// already include <windows.h>).
namespace windowChrome {

// Default used by newly shown windows (main process: MainWindow sets this
// from Config; tool processes: DialogBase(Config*) does).
void setRoundedCornersEnabled(bool on);
bool roundedCornersEnabled();

// Current effective per-window state (false for square / maximized / unknown).
bool roundedCornersActive(const Fl_Window *win);

// True when this process runs on Win11 and DWM is drawing the corners. When
// false but roundedCornersActive() is true (Win10), the caller must paint a
// 1px rounded border itself because there is no DWM border.
bool dwmCornersActive();

// DPI-scaled corner radius used by the Win10 region fallback.
int roundedCornerRadiusPx(const Fl_Window *win);

// The whole rounded-corner state machine, as one pure rule: a window that
// covers its monitor's work area is square whatever the preference says.
//
//   wantsRound     - the USER PREFERENCE (View > Rounded Corners, persisted)
//   coversWorkArea - the window currently fills the work area (maximized)
//   returns        - whether the window is rounded RIGHT NOW
//
// A free function, and unit-tested (test/test_rounded_corners.cpp), because
// getting it wrong is invisible until one exact sequence happens: entering the
// maximized state must square the window WITHOUT overwriting the preference.
// Storing the *effective* state is what caused the bug fixed 2026-10-31 - a
// window that STARTS maximized wrote "square" once, and every later refresh
// (resize / restore) read that back and kept it square; only a settings
// refresh, which re-injects the Config value, recovered the rounding.
inline bool effectiveRounded(bool wantsRound, bool coversWorkArea) {
    return wantsRound && !coversWorkArea;
}

#if defined(_WIN32)
// Pure geometry half of windowCoversWorkArea(): does the window rect cover the
// monitor work rect? Split out so the unit test can exercise the "covers"
// branch without really maximizing a window (which would flash on screen
// during ctest).
//
// tol absorbs the NC_PAD overhang our maximized geometry keeps on the resize
// edges - "covers", not "equals", is the right test.
inline bool rectCoversWork(const RECT &win, const RECT &work, int tol = 2) {
    return win.left   <= work.left   + tol &&
           win.top    <= work.top    + tol &&
           win.right  >= work.right  - tol &&
           win.bottom >= work.bottom - tol;
}

// --- The same state machine, driven by a plain HWND ----------------------
//
// The Fl_Window entry points below also touch the window region and assume a
// live FLTK window. The state machine itself needs neither, so it lives here
// where test/test_rounded_corners.cpp can drive it with a throwaway hidden
// HWND.
//
// The stored property is the USER PREFERENCE, never the effective state -
// see the note on effectiveRounded().
void setRoundedPreference(HWND hwnd, bool on);
bool roundedPreference(HWND hwnd);

// "Is this window rounded RIGHT NOW" = preference AND not covering the work
// area. The one question every painter and hit-test asks.
bool roundedNow(HWND hwnd);
#endif

// Apply to one top-level window. `borderColor` is an Fl_Color; on Win11 it is
// also used for the DWM 1px border so the rounded arc gets the same outline
// colour as the straight edges. Safe to call before show (no-op without HWND)
// and repeatedly.
void applyRoundedCorners(Fl_Window *win, bool on, unsigned int borderColor);

// Re-evaluate after a resize / maximize / restore. No-op for windows that
// never had applyRoundedCorners() called.
void refreshRoundedCorners(Fl_Window *win);

// Force a repaint of the non-client band (the 6px strip on left/right/bottom
// that MainWindow_frame/ToolChrome paint in WM_NCPAINT). Windows never
// re-sends WM_NCPAINT just because FLTK repainted the client area, so after a
// theme switch the band would keep the PREVIOUS theme's colour while the
// content next to it shows the new one (measured: band #FFFFFF/#E1E1E1 from
// "light" while the window had already switched to "cream" #FFFFCC/#AAC9B7).
void refreshFrameBand(Fl_Window *win);

// --- The single frame painter -------------------------------------------
//
// Every Pecia window used to carry its own copy of "fill the 6px band by
// sampling the widget tree, then stroke the 1px border" - one in
// MainWindow_frame.cpp, one in ToolChrome.cpp, with subtly different colour
// sampling. That duplication is why border bugs kept coming back one window
// type at a time (band colour lagging, border missing, border colour stuck
// after a theme switch). Both now call these two functions and nothing else.

// Colour a hand-drawn widget shows at (x, y), in `win`'s client space.
// Walks the widget tree, asking every BandColourSource on the way down.
// Returns false when nothing that paints a background covers the point.
bool bandColourAt(Fl_Group *win, int x, int y, unsigned int *colour);

// Paint the non-client frame for `win`: the 1px border, square or hard-drawn
// rounded with per-pixel coverage. There is no band to fill - the non-client
// area IS the border (WindowFrame::NC_PAD == 1), which is what removed the
// whole "coloured strip beside the border" family of bugs. `borderColour` is an
// Fl_Color. Called from WM_NCPAINT; returns false when there is nothing to
// paint (no HWND).
bool paintNonClientFrame(Fl_Window *win, unsigned int borderColour);

// Hand the non-client area over to us: tell DWM to stop rendering it.
// With WS_THICKFRAME + our WM_NCCALCSIZE inset, DWM treats the band between
// EXTENDED_FRAME_BOUNDS and the window rect as a frame and draws its own
// thing there - a bluish, bottom-heavy drop shadow that drowned our glow and
// made the bottom look wider than the top. Worse, while DWM owns the band it
// SILENTLY DISCARDS whatever WM_NCPAINT paints: a pure-red band fill never
// reached the screen (measured 2026-10-22). That is why this must run even
// when the outer glow is switched off - the 1px border is painted there too.
// Rounded corners (DWMWCP_ROUND) and the DWM border colour keep working.
// Call once the final window style is installed (after WS_THICKFRAME).
void takeOverFrame(Fl_Window *win);

// The 1px border colour a window registered through applyRoundedCorners().
// Used by the frame layer (WindowShadow) so the AA'd corner ring is drawn in
// exactly the same colour as the WM_NCPAINT border. FL_BLACK when unknown.
unsigned int windowBorderColour(const Fl_Window *win);

// Register only the border colour, without touching the window region. Used
// by popup menus: they are square and are not managed by applyRoundedCorners,
// but their glow/ring must follow the same theme colour as every other
// window (`g_borderColour`).
void setBorderColour(Fl_Window *win, unsigned int borderColor);

// The outer border is a 1px stroke: the band of points whose signed distance
// to the rounded rectangle is in (-1, 0] (negative = inside the window).
// Coverage must be an AREA fraction of that band, never a point sample of a
// ramp: on the 45-degree stretch of an 8px arc the pixel centres land at
// d = -0.2 and d = -1.6 with nothing in between, so a point sample jumps from
// 72% straight to 0 - the jaggedness we kept chasing (measured 2026-10-22).
// Shared by the frame layer (paints the part outside the window region) and
// the in-window painter (paints the part inside, blended with the content), so
// the two halves meet without a seam.
inline bool strokeHit(double d) { return d > -1.0 && d <= 0.0; }

// Signed distance (px) from a point in WINDOW coordinates to the window's
// rounded rectangle. The frame layer and the in-window painter must agree on
// the shape to the last bit, otherwise the two halves of the stroke drift.
inline double sdRoundBoxWin(double wx, double wy, int W, int H, double r) {
    const double hx = double(W) * 0.5;
    const double hy = double(H) * 0.5;
    const double qx = (wx > hx ? wx - hx : hx - wx) - hx + r;
    const double qy = (wy > hy ? wy - hy : hy - wy) - hy + r;
    const double ox = qx > 0.0 ? qx : 0.0;
    const double oy = qy > 0.0 ? qy : 0.0;
    const double qmax = qx > qy ? qx : qy;
    const double in = qmax < 0.0 ? qmax : 0.0;
    return in + std::sqrt(ox * ox + oy * oy) - r;
}

// Area coverage of one pixel (top-left at px0,py0 in window coordinates),
// 8x8 supersampled membership of the stroke band.
//
// Plain area coverage - no shaping here. It is exact on the straight edges (a
// pixel of the axis-aligned band measures 1.0) and it is the correct 1px
// hairline on the 45-degree stretch of the arc, where a diagonal crossing
// splits the band between two neighbours and no pixel can exceed 0.707.
// Any non-linear "boost" applied here was tried and reverted (2026-10-22): it
// turns the black border of the light theme from a soft AA line into a
// black-and-white bead chain, because the coverage along a diagonal band
// oscillates by nature and amplifying it amplifies the oscillation too.
// The one shaping that IS wanted is light-on-dark compensation, and it belongs
// where the background colour is known - see strokeLift() and its use in
// buildCornerStroke().
inline double strokeCoverageArea(double px0, double py0, int W, int H, double r) {
    const int N = 8;                    // 64 levels, plenty for an 8px radius
    const double inv = 1.0 / N;
    int hit = 0;
    for (int j = 0; j < N; ++j) {
        for (int i = 0; i < N; ++i) {
            if (strokeHit(sdRoundBoxWin(px0 + (i + 0.5) * inv,
                                        py0 + (j + 0.5) * inv, W, H, r)))
                ++hit;
        }
    }
    return double(hit) * (1.0 / (N * N));
}

// Light-on-dark compensation (a.k.a. stem darkening) for one already blended
// corner pixel.
//
// A light hairline on a dark background and a dark hairline on a light one are
// NOT symmetric to the eye: partial coverage on the light-on-dark side reads as
// a dim, broken line, because the eye compares it against the bright desktop
// and the bright straight border next to it, while the same coverage dark-on-
// light just reads as a slightly thinner line. Measured on the real window
// (2026-10-22): with plain coverage the arc's pixels came out 146..236 against
// a 255 straight border, so the white corner looked patched. Shaping the
// coverage upward only in this direction fixes that without touching the light
// theme at all - the black border keeps the coverage the eye already accepted.
// a(2-a) is used rather than a clip: it is a no-op at both ends (a straight
// border pixel stays exactly the border colour) and stays monotonic in between,
// so the AA ramp survives instead of collapsing into a hard-edged staircase
// (min(1, k*a) was measured to produce exactly that bead chain, and a knee at
// 0.55 turned a 1px arc into a ~2px block).
inline double strokeLift(double coverage, double borderLum, double underLum) {
    if (borderLum <= underLum + 8.0) return coverage;   // dark-on-light: as is
    return coverage * (2.0 - coverage);
}

// One already-blended border pixel (see buildCornerStroke).
struct FramePixel { int x, y; unsigned int colour; };

// Top-left corner of the CLIENT area in WINDOW coordinates. WM_NCPAINT's HDC
// is anchored at the window origin while FLTK and bandColourAt work in client
// coordinates; this is the offset between them (0,0 for plain dialogs).
void frameClientOrigin(const Fl_Window *win, int *x, int *y);

// True when (wx, wy) - WINDOW-relative (screen minus GetWindowRect().left/top,
// NOT absolute screen) - lands on a scrollbar.
//
// Only the resize hit test asks. The grip is WindowFrame::RESIZE_ZONE pixels of
// the client's outer edge and a vertical scrollbar sits flush against the right
// edge, so without this the outer 5px of a 10px scrollbar would start a window
// resize instead of dragging the thumb. Everything else keeps the full grip -
// the editor's left edge, the side panel, the status bar all behaved exactly
// this way back when those 6px were pure non-client area.
bool pointOverScrollbar(const Fl_Window *win, int wx, int wy);

// The part of the 1px border that a window region cannot express: the four
// rounded corners. Each pixel's colour is blended with whatever sits under it
// (band / title bar / editor), so callers just poke it down - no alpha needed.
// Coordinates come back in WINDOW space minus (outOriginX, outOriginY): pass
// (0,0) when painting on the WM_NCPAINT HDC, and the client origin when
// painting from Fl_Window::draw(). Returns the pixel count (0 = square).
int buildCornerStroke(const Fl_Window *win, unsigned int borderColour,
                      int outOriginX, int outOriginY,
                      FramePixel *out, int maxPixels);

// Apply to every window previously managed by applyRoundedCorners() in this
// process (open dialogs + main window).
void reapplyRoundedCorners(bool on);

// Ask other Pecia processes (Lua console / AI chat) to reapply as well.
void broadcastRoundedCorners(bool on);

// Registered window message used by the broadcast (0 on non-Windows).
unsigned int roundedCornersMessage();

}  // namespace windowChrome

// WindowFrame
//   Manages custom window frame behavior: edge resizing, maximize/restore,
//   rounded corners (Windows 11 DWM), and taskbar integration.
//   Encapsulates the Win32-specific window message handling.
class WindowFrame {
public:
    WindowFrame();

    // Edge resize constants.
    //
    // NC_PAD is the CLIENT INSET: how many pixels of the window rect belong to
    // the non-client strip on left / right / bottom (the top edge has none -
    // the title bar starts on window row 0).
    //
    // Why 1px and not 6 (2026-10-22, second pass): the strip only exists so the
    // window's own 1px border has somewhere to live that FLTK cannot paint
    // over. Every pixel of strip beyond that line was dead space the user sees
    // as "the scrollbar stops short of the border", "the close button is not
    // flush", "there is a coloured band inside the border" - and the band had
    // to be GUESSED by sampling the widget tree (windowChrome::bandColourAt);
    // every wrong guess showed up as a visible seam (measured 2026-10-22:
    // #CCFFFF band beside a #A6C2D4 tab strip, then #FFFFCC beside a #CCFFFF
    // AI-chat body). At 1px the strip IS the border, so there is nothing left
    // to guess and the whole class of bug is gone by construction.
    //
    // The resize grip therefore sits mostly INSIDE the client: RESIZE_ZONE is
    // how near the window edge a point must be to become HTLEFT / HTRIGHT /
    // HTBOTTOM / HTTOP. Returning those codes from the client area is how the
    // top edge has always worked here, so it is well-trodden ground.
    //
    // RESIZE_ZONE_OVER_SCROLLBAR is the same grip over a scrollbar, where a
    // full 6px would swallow most of a 10px scrollbar. It must stay well
    // above NC_PAD (1px): the editor's and the preview's vertical scrollbars
    // are flush with the window's right edge, so NC_PAD there meant the most
    // used resize edge had a single pixel of grip (fixed 2026-10-31).
    enum { NC_PAD = 1, RESIZE_ZONE = 6, RESIZE_ZONE_OVER_SCROLLBAR = 3 };

    // Maximize / restore state
    bool isMaximized() const { return m_maximized; }
    void setMaximized(bool on) { m_maximized = on; }

    // Save/restore geometry for maximize toggle
    void saveRestoreRect(int x, int y, int w, int h);
    void getRestoreRect(int &x, int &y, int &w, int &h) const;

    // Toggle maximize: returns the new maximized state
    bool toggleMaximize(Fl_Window *win);

    // Apply maximized geometry to the window (Win32)
    void applyMaximized(Fl_Window *win);

private:
    bool m_maximized = false;
    int  m_restoreX = 0, m_restoreY = 0;
    int  m_restoreW = 0, m_restoreH = 0;
};
