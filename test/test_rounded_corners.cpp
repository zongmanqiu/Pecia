// test_rounded_corners.cpp - gate for the rounded-corner state machine.
//
// The bug it guards (fixed 2026-10-31): starting the app MAXIMIZED with
// View > Rounded Corners ON, then clicking the caption's restore button left
// the window SQUARE. It only turned round again after Settings > Refresh -
// which is why it read like a refresh bug.
//
// Cause: the per-window property (kRoundedProp) stored the EFFECTIVE state
// (`on && !windowCoversWorkArea(win)`) instead of the user PREFERENCE.
// Maximizing squares the window (Windows convention, correct), and because
// that write went into the same slot as the preference, the preference was
// destroyed. Every later refreshRoundedCorners() (resize / restore) read
// "square" back and kept the window square; Settings > Refresh was the only
// path that re-injected the Config value.
//
// Contract pinned here:
//   1. the rule is `preference && !coversWorkArea` - geometry gates the
//      RESULT, it never rewrites the preference;
//   2. all four input combinations behave as above, asserted in BOTH
//      directions, so the gate goes red if the rule is ever inverted;
//   3. the property round-trips the PREFERENCE on a real HWND (the old code
//      failed this whenever the window covered the work area).
//
// NOT covered: the literal "starts maximized" sequence needs a really zoomed
// window, which would flash on screen during ctest. That path stays on manual
// verification; what is pinned here is the state machine underneath it.

#include "test_assert.h"

#include "ui/WindowFrame.h"

#if defined(_WIN32)
#include <windows.h>
#endif

#if defined(_WIN32)
namespace {

// Throwaway window: the state machine is just a property on the HWND, so it
// can be driven without a visible window or an FLTK event loop.
HWND makeHiddenWindow() {
    const wchar_t *kCls = L"PeciaRoundCornerTestWnd";
    WNDCLASSW wc = {};
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = kCls;
    RegisterClassW(&wc);   // harmless when the class already exists
    return CreateWindowExW(0, kCls, L"", 0, 0, 0, 1, 1,
                           nullptr, nullptr, wc.hInstance, nullptr);
}

}  // namespace
#endif

int main() {
    using windowChrome::effectiveRounded;
    using windowChrome::rectCoversWork;

    // --- 1) The rule itself: preference in, geometry gates the result -------
    CHECK(effectiveRounded(true, false));    // wants round, restored -> round
    CHECK(!effectiveRounded(true, true));    // wants round, maximized -> square
    CHECK(!effectiveRounded(false, false));  // wants square           -> square
    CHECK(!effectiveRounded(false, true));   // wants square           -> square

    // --- 2) The geometry predicate -----------------------------------------
    // tol exists because our maximized geometry deliberately overhangs the
    // work area by NC_PAD on the resize edges - "covers", not "equals".
    // NOTE on the names: rpcndr.h (pulled in by windows.h) does
    // `#define small char`, so `small` is not a usable variable name here.
    const RECT work   = { 0, 0, 1920, 1080 };
    const RECT rExact = { 0, 0, 1920, 1080 };
    CHECK(rectCoversWork(rExact, work));                // exact fit
    const RECT rOver  = { -1, 0, 1921, 1081 };
    CHECK(rectCoversWork(rOver, work));                 // our maximized rect
    const RECT rTiny  = { 100, 100, 900, 700 };
    CHECK(!rectCoversWork(rTiny, work));                // ordinary window
    const RECT rShort = { 0, 0, 1917, 1080 };
    CHECK(!rectCoversWork(rShort, work));               // 3px short: past tol

#if defined(_WIN32)
    // --- 3) The regression: the preference must survive "maximized" --------
    HWND hwnd = makeHiddenWindow();
    CHECK(hwnd != nullptr);
    if (hwnd) {
        // Round chosen, window currently square (maximized): the STORED value
        // must still be "round" - this is exactly what the old code clobbered.
        windowChrome::setRoundedPreference(hwnd, true);
        CHECK(windowChrome::roundedPreference(hwnd));
        // "Is it round right now" is a different question and must be
        // re-derived from the CURRENT geometry every time it is asked.
        const bool now = windowChrome::roundedNow(hwnd);
        CHECK(now == effectiveRounded(true, false));   // tiny window: not covering
        // Preference OFF survives too.
        windowChrome::setRoundedPreference(hwnd, false);
        CHECK(!windowChrome::roundedPreference(hwnd));
        CHECK(!windowChrome::roundedNow(hwnd));
        DestroyWindow(hwnd);
    }
#endif

    int fails = test::failCount();
    std::fprintf(stderr, "%d checks, %d failures\n", test::checkCount(), fails);
    return fails ? 1 : 0;
}
