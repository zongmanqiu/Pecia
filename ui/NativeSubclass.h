// NativeSubclass.h - per-HWND window-proc subclassing helper.
//
// FLTK's border(0) windows need a subclassed native wndproc for edge
// resize / non-client painting. The old implementation stored the
// original wndproc in a file-static WNDPROC (s_origWndProc); when a
// second window was subclassed it overwrote the first window's stored
// proc, so messages from the first window were forwarded to the wrong
// wndproc (crashes / wrong behavior once more than one tool window
// existed). This helper stores the original proc as a per-window
// property instead, so any number of windows can be subclassed safely.
#pragma once

#if defined(_WIN32)

#include <windows.h>

namespace nativeSubclass {

// Property name under which each window keeps its original wndproc.
inline const wchar_t *propName() {
    static const wchar_t name[] = L"Pecia.NativeOrigWndProc";
    return name;
}

// Remember hwnd's current wndproc (per window) and install `proc`.
inline WNDPROC install(HWND hwnd, WNDPROC proc) {
    WNDPROC prev = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
    SetPropW(hwnd, propName(), (HANDLE)prev);
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)proc);
    return prev;
}

// Forward a message to hwnd's own original wndproc (fall back to the
// default proc if the window property is gone, e.g. during teardown).
inline LRESULT forward(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    WNDPROC orig = (WNDPROC)GetPropW(hwnd, propName());
    if (orig) return CallWindowProcW(orig, hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace nativeSubclass

#endif // _WIN32