// ImeInPlace.h - Shared Windows IME in-place composition for Pecia text
// editors.
//
// FLTK does not consume WM_IME_* messages, so by default the IME reverts to
// its floating composition window, which draws its own caret next to the
// editor's caret -> "two carets" during pinyin input. The main editor
// already fixed this by consuming WM_IME_* and rendering the composition
// in place. This component makes that behaviour a reusable helper any
// Fl_Text_Editor can use, so the Lua/AI windows and their inputs match the
// main window.
//
// Usage: a Fl_Text_Editor subclass calls ImeInPlace::attach(this) from its
// handle() on FL_FOCUS/FL_KEYBOARD (after the HWND exists), and
// ImeInPlace::detach(this) on FL_UNFOCUS. attach() installs a window-proc
// hook on the editor's top window (once per window) that routes
// WM_IME_STARTCOMPOSITION / WM_IME_COMPOSITION / WM_IME_ENDCOMPOSITION to
// the currently-attached editor whose window owns that HWND.
#pragma once

// Minimal header: no FLTK or windows.h includes, so including this into
// ThemeWidgets.h (used by many TUs) does not leak Win32 macros.
#if defined(_WIN32)
class Fl_Text_Editor;   // forward-declared; only passed as an opaque pointer
#endif

namespace ImeInPlace {

// Install the IME hook for this editor's top window (if not already) and
// mark `ed` as the current in-place composition target. Afterwards any
// WM_IME_* destined for that window is consumed and rendered in place.
void attach(Fl_Text_Editor *ed);

// Clear the current composition state; safe to call repeatedly.
void detach(Fl_Text_Editor *ed);

// True when an in-place composition is currently active on `ed`.
bool compositionActive(Fl_Text_Editor *ed);

}  // namespace ImeInPlace
