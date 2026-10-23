// NativeSubclass.h - per-HWND window-proc subclassing helper.
//
// FLTK's border(0) windows need a subclassed native wndproc for edge
// resize / non-client painting. Several independent components subclass the
// same window (the frame procs, and ui/ImeInPlace.cpp's WM_IME_* filter),
// they may install in any order, and they may install AGAIN later (a dialog
// is show()n twice, a tool window is re-themed ...).
//
// A single "original proc" slot cannot express that: installing B on top of
// A, then installing A again, stored B as A's "original" while B's own
// bookkeeping still pointed back at A - a two-node cycle, and the next
// message recursed CallWindowProcW until the stack died (measured
// 2026-10-22: clicking the Font dialog's button bar, exit 0xC00000FD; the
// cycle is visible frame-by-frame in temp/av_stack.txt).
//
// So this helper keeps the WHOLE chain per window instead of one slot:
//
//   chain[0]        the innermost proc (FLTK's own wndproc)
//   chain[1..n-1]   our procs, innermost-first
//   chain.back()    the window's CURRENT wndproc
//
// forward() requires the caller to identify itself, so each level forwards
// to exactly the one below it. install() is idempotent per proc, so re-runs
// are safe in any order.
#pragma once

#if defined(_WIN32)

#include <windows.h>

#include <map>
#include <vector>

namespace nativeSubclass {

// All chains, keyed by hwnd. FLTK windows live on one UI thread, so no lock.
inline std::map<HWND, std::vector<WNDPROC>> &chains() {
    static std::map<HWND, std::vector<WNDPROC>> m;
    return m;
}

// Install `proc` as the window's new top wndproc.
//
// Idempotent: a proc already in the chain is a no-op, so re-running show() /
// applyToolChrome() can never duplicate a level - and can never point a
// level at itself, which is the bug this file exists to prevent.
inline void install(HWND hwnd, WNDPROC proc) {
    if (!hwnd || !proc) return;
    std::vector<WNDPROC> &ch = chains()[hwnd];
    for (WNDPROC p : ch)
        if (p == proc) return;               // already hooked
    const WNDPROC top = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
    if (ch.empty()) {
        ch.push_back(top);                   // innermost = FLTK's own proc
    } else if (top != ch.back()) {
        // A foreign proc hooked on top of our chain since we last looked.
        // Keep it below the new one so nothing is bypassed.
        ch.push_back(top);
    }
    ch.push_back(proc);
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)proc);
}

// Forward a message to the level BELOW `self` (DefWindowProc if self is the
// innermost). Every hooked proc must pass its own function here.
inline LRESULT forward(HWND hwnd, WNDPROC self, UINT msg, WPARAM wp,
                       LPARAM lp) {
    const auto it = chains().find(hwnd);
    if (it != chains().end()) {
        const std::vector<WNDPROC> &ch = it->second;
        for (size_t i = ch.size(); i-- > 1;) {
            if (ch[i] == self)
                return CallWindowProcW(ch[i - 1], hwnd, msg, wp, lp);
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Remove `proc` from the chain (ui/ImeInPlace.cpp unhooks when its last host
// goes away). Only a top-level entry can be removed cleanly; a deeper one
// stays hooked (its forward path is still correct) until the window dies and
// detachAll() drops the whole chain.
inline void uninstall(HWND hwnd, WNDPROC proc) {
    const auto it = chains().find(hwnd);
    if (it == chains().end()) return;
    std::vector<WNDPROC> &ch = it->second;
    for (size_t i = ch.size(); i-- > 1;) {
        if (ch[i] != proc) continue;
        if (i == ch.size() - 1) {
            ch.pop_back();
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)ch.back());
        }
        break;
    }
    if (ch.size() <= 1) chains().erase(it);
}

// The window is going away: drop its chain. Call from WM_DESTROY /
// WM_NCDESTROY next to windowShadow::detachHwnd().
inline void detachAll(HWND hwnd) { chains().erase(hwnd); }

} // namespace nativeSubclass

#endif // _WIN32
