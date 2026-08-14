// InfoWindow.h - reusable info dialog with custom title bar and close button.
#pragma once

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/fl_draw.H>
#include <string>
#include <vector>

class Theme;

struct InfoRow {
    std::string label;
    std::string value;
};

class InfoTitleBar;
class BorderOverlay;

// Factory functions (declared in InfoWindow.cpp)
InfoTitleBar *createInfoTitleBar(int X, int Y, int W, int H, const char *title, const Theme *theme = nullptr, int fontSize = 16);
BorderOverlay *createBorderOverlay(int X, int Y, int W, int H, const Theme *theme = nullptr);

class InfoWindow {
public:
    InfoWindow();
    ~InfoWindow();

    void setTitle(const char *t) { m_title = t ? t : ""; }
    void setRows(const std::vector<InfoRow> &rows) { m_rows = rows; }
    void setWidth(int w) { m_width = w; }
    // Apply theme colors. MUST be called before show().
    void setTheme(const Theme *theme, int uiFontSize);

    void show();

private:
    std::string m_title;
    std::vector<InfoRow> m_rows;
    int m_width = 360;

    const Theme *m_theme = nullptr;
    int m_uiFontSize = 16;
};
