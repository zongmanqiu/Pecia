// ToolChrome.h - Custom window chrome for the standalone tools.
// Mirrors the main window's native frame handling (edge resize border,
// taskbar presence, app icon, Win11 corners) so the tool windows look
// and behave exactly like the main Pecia window.
#pragma once

#include <FL/Fl_Window.H>

class Theme;

// Install the native window chrome on a border(0) FLTK window:
//   - WS_EX_APPWINDOW (taskbar presence)
//   - WM_SETICON from the exe resource (IDI_PECIA)
//   - WS_THICKFRAME + non-client 6px resize border (left/right/bottom)
//   - Win11 right-angle corners + border color
// Call once after the window has been shown (fl_xid valid).
void setupToolChrome(Fl_Window *win, const Theme *theme);
