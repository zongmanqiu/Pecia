// WindowFrame.h - Custom window frame management
#pragma once

#if defined(_WIN32)
#include <windows.h>
#endif

class Fl_Window;

// True when the window's outer rectangle covers its monitor's work area,
// i.e. the window is in the maximized / full-screen state.
//
// Deliberately geometry-based instead of relying on IsZoomed(): a window can
// be flagged WS_MAXIMIZE yet float off the work area (after being moved), and
// the tool windows maximize by re-applying geometry without ever setting
// WS_MAXIMIZE. Both cases must be classified by actual size, so that the
// caption icon, the 1px border and the edge-resize hit-test all agree.
bool windowCoversWorkArea(const Fl_Window *win);

// WindowFrame
//   Manages custom window frame behavior: edge resizing, maximize/restore,
//   rounded corners (Windows 11 DWM), and taskbar integration.
//   Encapsulates the Win32-specific window message handling.
class WindowFrame {
public:
    WindowFrame();

    // Edge resize constants
    enum { NC_PAD = 6 };  // non-client resize border width (px)

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
