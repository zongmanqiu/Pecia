// TitleBar.h - Custom title bar with window buttons and tab rendering
#pragma once

#include <FL/Fl_Widget.H>
#include <functional>
#include <string>
#include <vector>

class Theme;

// TitleBar
//   Custom-drawn title bar that replaces the OS title bar.
//   Draws the app icon, multi-tab row (or window title in single-tab mode),
//   and four caption buttons (Pin, Minimize, Maximize/Restore, Close).
//   Supports window dragging, double-click to maximize, and tab interaction.
//
//   Communication with the parent MainWindow is purely through setters
//   (for data) and callbacks (for actions). No direct MainWindow dependency.
class TitleBar : public Fl_Widget {
public:
    // Tab information needed for rendering
    struct TabInfo {
        std::string label;  // tab display text (may include "*" for dirty)
    };

    // Button identifiers
    enum Btn { BTN_NONE = -1, BTN_PIN = 0, BTN_MIN, BTN_MAX, BTN_CLOSE, BTN_COUNT };
    // Hit-test result codes for tab interactions
    enum { HIT_TAB_BASE = 100, HIT_CLOSE_BASE = 200, HIT_PLUS = 300 };

    TitleBar(int X, int Y, int W, int H);
    ~TitleBar();

    // ---- Data setters (called by MainWindow to keep us in sync) ----
    void setTabData(const std::vector<TabInfo> &tabs, int activeIndex);
    void setMultiTab(bool on) { m_multiTab = on; }
    void setMaximized(bool on) { m_maximized = on; }
    void setPinned(bool on) { m_pinned = on; }

    // ---- Action callbacks (set by MainWindow) ----
    void setOnNewFile(std::function<void()> f) { m_onNewFile = std::move(f); }
    void setOnCloseTab(std::function<void(int)> f) { m_onCloseTab = std::move(f); }
    void setOnSwitchToTab(std::function<void(int)> f) { m_onSwitchToTab = std::move(f); }
    void setOnTogglePin(std::function<void()> f) { m_onTogglePin = std::move(f); }
    void setOnMinimize(std::function<void()> f) { m_onMinimize = std::move(f); }
    void setOnToggleMaximize(std::function<void()> f) { m_onToggleMaximize = std::move(f); }
    void setOnCloseWindow(std::function<void()> f) { m_onCloseWindow = std::move(f); }

    // Refresh / redraw
    void refresh() { redraw(); }

    // Set the custom-drawn chrome font size.
    void setFontSize(int sz) { m_chromeFontSize = sz; refresh(); }

    // Set theme colors for title bar drawing.
    void setTheme(const Theme *theme) { m_theme = theme; refresh(); }

    // Public event handler (called by MainWindow for event forwarding).
    int handle(int event) FL_OVERRIDE;

protected:
    void draw() FL_OVERRIDE;

private:
    // Hit-test the given screen coordinate. Returns a Btn value, a
    // HIT_TAB_BASE/HIT_CLOSE_BASE offset, HIT_PLUS, or BTN_NONE.
    int hitTest(int mx, int my) const;

    // Draw the multi-tab row (called from draw() when m_multiTab is true)
    void drawTabs();

    // Execute the action associated with a hit-test result.
    void doAction(int hit);

    // Draw a single caption button at the given position.
    void drawButton(int bx, int by, int bw, int bh, Btn btn);

    // Button state tracking
    int  m_btnW[BTN_COUNT];       // width of each caption button
    int  m_hover = BTN_NONE;      // currently hovered button
    int  m_pressed = BTN_NONE;    // currently pressed button
    bool m_dragging = false;      // window drag in progress
    int  m_dragStartX = 0, m_dragStartY = 0;
    int  m_winX = 0, m_winY = 0;

#if defined(_WIN32)
    void *m_hIcon = nullptr;      // HICON for the app icon (16x16)
#endif

    // Tab layout cache (recalculated in drawTabs(), used by hitTest())
    mutable int m_tabX[128];
    mutable int m_tabW[128];
    mutable int m_tabCount = 0;
    mutable int m_plusX = 0, m_plusW = 0;

    // Cached state from MainWindow
    std::vector<TabInfo> m_tabsInfo;
    int  m_activeIndex = 0;
    bool m_multiTab = false;
    bool m_maximized = false;
    bool m_pinned = false;
    int  m_chromeFontSize = 16;   // font size for chrome text
    const Theme *m_theme = nullptr;

    // Callbacks
    std::function<void()>           m_onNewFile;
    std::function<void(int)>        m_onCloseTab;
    std::function<void(int)>        m_onSwitchToTab;
    std::function<void()>           m_onTogglePin;
    std::function<void()>           m_onMinimize;
    std::function<void()>           m_onToggleMaximize;
    std::function<void()>           m_onCloseWindow;
};
