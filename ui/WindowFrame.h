// WindowFrame.h - Custom window frame management
#pragma once

#if defined(_WIN32)
#include <windows.h>
#endif

class Fl_Window;

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
