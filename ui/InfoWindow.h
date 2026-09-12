// InfoWindow.h - reusable info dialog, built on DialogBase (shared title
// bar / border).
#pragma once

#include <string>
#include <vector>

class Theme;

struct InfoRow {
    std::string label;
    std::string value;
};

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
