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

    // Hit-test window edges for resize cursor. Returns bitmask:
    // 1=left, 2=top, 4=right, 8=bottom, or 0 = not on any edge.
    int hitResizeEdge(int mx, int my, int winW, int winH) const;
    int hitResizeEdge(int mx, int my, Fl_Window *win) const;

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

    // Fix taskbar visibility after removing WS_EX_TOOLWINDOW
    static void fixTaskbar(HWND hwnd, Fl_Window *win);

private:
    bool m_maximized = false;
    int  m_restoreX = 0, m_restoreY = 0;
    int  m_restoreW = 0, m_restoreH = 0;
};
