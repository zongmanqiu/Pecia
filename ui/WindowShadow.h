// WindowShadow.h - the window's companion LAYER: the 1px border ring with real
// anti-aliasing, plus the soft gradient that fades out just OUTSIDE the window.
//
// Why a second window at all: a window cannot paint beyond its own rectangle,
// and more importantly it cannot paint PARTIALLY there - SetWindowRgn clips
// with a 1-bit mask, so an arc rasterised inside the window is always a hard
// staircase. A layered window is the only surface in our stack that carries
// per-pixel alpha, so the visible outline (the outer 1px) is drawn here and the
// window region is inset by that one pixel to let it show.
//
// Measured on this machine (temp/probe_shadow*.cpp, temp/probe_corner_aa.py):
//   * CS_DROPSHADOW produces no shadow at all on our WS_POPUP windows;
//   * DWM rounded corners add no gradient of their own outside the 1px border;
//   * a system shadow, even when present, has a fixed colour/width/curve.
//
// The layer window is:
//   WS_EX_LAYERED    - per-pixel alpha via UpdateLayeredWindow
//   WS_EX_TRANSPARENT- clicks fall through to whatever is underneath
//   WS_EX_NOACTIVATE - never steals focus, never shows up in Alt+Tab
//   WS_EX_TOOLWINDOW - no taskbar button
//
// The ring must exist even when the glow is switched off (View > Window
// Shadow): it is the border, not decoration. setEnabled() only drops the glow
// half of the bitmap.
#pragma once

class Fl_Window;

namespace windowShadow {

// Process-wide switch (View > Window Shadow), published by the main window
// (from Config) and by the tool processes (DialogBase(Config*)). Windows opt
// in via attach() whatever the switch says, so turning it back on later can
// still find them.
void setEnabled(bool on);
bool enabled();

// Create the shadow window for `win` and lay it under it. Requires a live
// native handle (call after the window has been shown); no-op otherwise, so
// it is safe to call from GUI-less smoke tests.
//
// Popup menus (Fl_Window::menu_window()) participate too: attach() hooks
// their native HWND so the glow follows moves/DPI changes and is destroyed
// with the menu. They use the SAME kRampPx/kAlphaMax/kSigmaPx profile as every
// other window - never add per-menu width/opacity overrides.
//
// Marks the window as a shadow candidate even when the switch is off, then
// returns without creating anything - that is what makes toggle-on work for
// windows that were shown while the glow was disabled.
void attach(Fl_Window *win);

// Re-evaluate every window that opted in: create the glow when the switch
// is on, tear it down when it is off. Used by the View > Window Shadow
// toggle, both in this process and in the tool processes (after the
// broadcast).
void applyAll();

// Re-render every attached glow. Needed when something the ramp follows
// changed without a geometry change (corner radius toggled by View > Rounded
// Corners).
void syncAll();

// Re-position / re-render after a move, resize, show, hide, minimize,
// maximize, DPI or rounded-corner change. Cheap no-op when nothing changed.
void sync(Fl_Window *win);

// Destroy the shadow window (call before the real window goes away).
void detach(Fl_Window *win);

// Native-handle version (Win32: pass the HWND). Needed from the teardown path:
// by WM_DESTROY/NCDESTROY, fl_find(hwnd) can already return null because FLTK
// has removed the window from its table, and a Fl_Window-based detach then
// returns early and leaves the shadow on screen after the window is gone.
void detachHwnd(void *nativeHandle);

// --- Cross-process sync (main window -> tool processes) -------------------
// Registered window message used by the broadcast (0 on non-Windows).
unsigned int shadowMessage();

// Ask the other Pecia processes (Lua console / AI chat) to apply the same
// on/off state.
void broadcastEnabled(bool on);

}  // namespace windowShadow
