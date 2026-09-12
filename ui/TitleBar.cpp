// TitleBar.cpp - Custom title bar implementation
#include "TitleBar.h"
#include "core/Theme.h"

#include <FL/fl_draw.H>
#include <FL/Fl_Window.H>
#include <FL/x.H>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <string.h>
#include <algorithm>
#include "resource.h"

TitleBar::TitleBar(int X, int Y, int W, int H)
    : Fl_Widget(X, Y, W, H, nullptr) {
    box(FL_FLAT_BOX);
    m_btnW[BTN_PIN]   = 36;
    m_btnW[BTN_MIN]   = 36;
    m_btnW[BTN_MAX]   = 36;
    m_btnW[BTN_CLOSE] = 36;
#if defined(_WIN32)
    m_hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr),
                                MAKEINTRESOURCEW(IDI_PECIA),
                                IMAGE_ICON, 16, 16, 0);
#endif
}

TitleBar::~TitleBar() {
#if defined(_WIN32)
    if (m_hIcon) DestroyIcon((HICON)m_hIcon);
#endif
}

void TitleBar::setTabData(const std::vector<TabInfo> &tabs, int activeIndex) {
    m_tabsInfo = tabs;
    m_activeIndex = activeIndex;
}

// ---- Drawing -------------------------------------------------------------

void TitleBar::draw() {
    fl_draw_box(FL_FLAT_BOX, x(), y(), w(), h(), color());

    const int ICON_SZ = 16;
    const int ICON_MARGIN = 6;
    int iconX = x() + ICON_MARGIN;
    int iconY = y() + (h() - ICON_SZ) / 2;
#if defined(_WIN32)
    if (m_hIcon) {
        DrawIconEx(fl_gc, iconX, iconY, (HICON)m_hIcon, ICON_SZ, ICON_SZ, 0, nullptr, DI_NORMAL);
    }
#endif

    int buttonsTotalW = enabledButtonsTotalW();
    int tabAreaX = x() + ICON_MARGIN + ICON_SZ + 8;
    int tabAreaW = (x() + w() - buttonsTotalW - 6) - tabAreaX;

    if (m_multiTab) {
        drawTabs();
    } else {
        Fl_Window *win = window();
        const char *title = win ? win->label() : "Pecia";
        if (!title) title = "Pecia";
        fl_font(FL_HELVETICA, m_chromeFontSize);
        fl_color(fl_contrast(FL_FOREGROUND_COLOR, color()));
        fl_push_clip(tabAreaX, y(), tabAreaW, h());
        fl_draw(title, tabAreaX, y(), tabAreaW, h(), FL_ALIGN_LEFT | FL_ALIGN_CLIP);
        fl_pop_clip();
    }

    // Draw the caption buttons right-to-left, skipping any hidden by the
    // enabled-button mask (dialogs show only the close button).
    int bx = x() + w();
    for (int b = BTN_COUNT - 1; b >= 0; --b) {
        Btn btn = (Btn)b;
        if (!buttonEnabled(btn)) continue;
        bx -= m_btnW[btn];
        drawButton(bx, y(), m_btnW[btn], h(), btn);
    }
}

void TitleBar::drawTabs() {
    int n = (int)m_tabsInfo.size();
    if (n > 128) n = 128;
    m_tabCount = n;

    const int ICON_MARGIN = 6, ICON_SZ = 16;
    int buttonsTotalW = enabledButtonsTotalW();
    int areaX = x() + ICON_MARGIN + ICON_SZ + 8;
    int areaW = (x() + w() - buttonsTotalW - 6) - areaX;

    int availW = areaW - h();
    int totalW = 0;
    int textW[128];
    fl_font(FL_HELVETICA, m_chromeFontSize);
    for (int i = 0; i < n; ++i) {
        const char *label = m_tabsInfo[i].label.c_str();
        textW[i] = (int)fl_width(label) + 28;
        totalW += textW[i];
    }

    float scale = (totalW > availW && totalW > 0) ? (float)availW / totalW : 1.0f;

    int cx = areaX;
    Fl_Color bg = color();
    Fl_Color activeBg = m_theme ? m_theme->colors().highlight1
                                : fl_color_average(FL_WHITE, bg, 0.6f);
    Fl_Color hoverBg = m_theme ? m_theme->colors().highlight1
                               : fl_color_average(FL_WHITE, bg, 0.3f);
    Fl_Color textCol = fl_contrast(FL_FOREGROUND_COLOR, bg);

    for (int i = 0; i < n; ++i) {
        int tw = (int)(textW[i] * scale);
        if (tw < 60) tw = 60;
        if (cx + tw > areaX + availW) tw = areaX + availW - cx;
        if (tw < 0) break;

        m_tabX[i] = cx;
        m_tabW[i] = tw;

        bool active = (i == m_activeIndex);
        bool hover = (m_hover == HIT_TAB_BASE + i || m_hover == HIT_CLOSE_BASE + i);

        Fl_Color tabBg = active ? activeBg : (hover ? hoverBg : bg);
        fl_draw_box(FL_FLAT_BOX, cx, y(), tw, h(), tabBg);

        if (i + 1 < n) {
            fl_color(fl_color_average(FL_BLACK, bg, 0.6f));
            fl_rectf(cx + tw - 1, y() + 6, 1, h() - 12);
        }

        const char *label = m_tabsInfo[i].label.c_str();
        fl_font(FL_HELVETICA, m_chromeFontSize);
        fl_color(textCol);
        int labelW = tw - 24;
        fl_push_clip(cx + 4, y(), labelW, h());
        fl_draw(label, cx + 4, y(), labelW, h(), FL_ALIGN_LEFT | FL_ALIGN_CLIP);
        fl_pop_clip();

        // Close button on tab
        {
            int closeSz = 14;
            int closeX = cx + tw - closeSz - 3;
            int closeY = y() + (h() - closeSz) / 2;
            bool closeHover = (m_hover == HIT_CLOSE_BASE + i);
            if (closeHover) {
                fl_draw_box(FL_FLAT_BOX, closeX, closeY, closeSz, closeSz,
                            m_theme ? m_theme->colors().highlight3 : fl_rgb_color(232, 17, 35));
                fl_color(FL_WHITE);
            } else {
                // Tab close glyph follows text1 (primary text colour).
                fl_color(m_theme ? m_theme->colors().text1
                                 : fl_color_average(FL_BLACK, bg, 0.5f));
            }
            int mx = closeX + closeSz / 2;
            int my = closeY + closeSz / 2;
            fl_begin_line();
            fl_vertex(mx - 3, my - 3); fl_vertex(mx + 3, my + 3);
            fl_end_line();
            fl_begin_line();
            fl_vertex(mx + 3, my - 3); fl_vertex(mx - 3, my + 3);
            fl_end_line();
        }

        cx += tw;
    }

    // + button
    m_plusX = cx;
    m_plusW = h();
    Fl_Color plusBg = (m_hover == HIT_PLUS)
                        ? m_theme ? m_theme->colors().highlight1 : fl_rgb_color(229, 229, 229)
                        : fl_color_average(FL_WHITE, bg, 0.15f);
    fl_draw_box(FL_FLAT_BOX, m_plusX, y(), m_plusW, h(), plusBg);
    fl_color(textCol);
    int px = m_plusX + m_plusW / 2;
    int py = y() + h() / 2;
    fl_begin_line();
    fl_vertex(px - 5, py); fl_vertex(px + 5, py);
    fl_end_line();
    fl_begin_line();
    fl_vertex(px, py - 5); fl_vertex(px, py + 5);
    fl_end_line();
}

// ---- Hit testing ---------------------------------------------------------

int TitleBar::enabledButtonsTotalW() const {
    int total = 0;
    for (int b = 0; b < BTN_COUNT; ++b)
        if (buttonEnabled((Btn)b)) total += m_btnW[b];
    return total;
}

int TitleBar::hitTest(int mx, int my) const {
    if (my < y() || my > y() + h()) return BTN_NONE;
    int lx = x() + w();
    for (int b = BTN_COUNT - 1; b >= 0; --b) {
        Btn btn = (Btn)b;
        if (!buttonEnabled(btn)) continue;
        lx -= m_btnW[btn];
        if (mx >= lx && mx < lx + m_btnW[btn]) return btn;
    }

    if (m_multiTab) {
        const int ICON_MARGIN = 6, ICON_SZ = 16;
        int buttonsTotalW = enabledButtonsTotalW();
        int tabAreaX = x() + ICON_MARGIN + ICON_SZ + 8;
        int tabAreaW = (x() + w() - buttonsTotalW - 6) - tabAreaX;
        int availW = tabAreaW - h();

        int n = (int)m_tabsInfo.size();
        if (n > 128) n = 128;

        int totalW = 0;
        int textW[128];
        fl_font(FL_HELVETICA, m_chromeFontSize);
        for (int i = 0; i < n; ++i) {
            const char *label = m_tabsInfo[i].label.c_str();
            textW[i] = (int)fl_width(label) + 28;
            totalW += textW[i];
        }

        float scale = (totalW > availW && totalW > 0) ? (float)availW / totalW : 1.0f;

        int cx = tabAreaX;
        for (int i = 0; i < n; ++i) {
            int tw = (int)(textW[i] * scale);
            if (tw < 60) tw = 60;
            if (cx + tw > tabAreaX + availW) tw = tabAreaX + availW - cx;
            if (tw < 0) break;

            if (mx >= cx + tw - 17 && mx < cx + tw)
                return HIT_CLOSE_BASE + i;
            if (mx >= cx && mx < cx + tw - 17)
                return HIT_TAB_BASE + i;

            cx += tw;
        }

        int plusX = cx;
        int plusW = h();
        if (mx >= plusX && mx < plusX + plusW) {
            return HIT_PLUS;
        }
    }

    return BTN_NONE;
}

// ---- Action dispatch -----------------------------------------------------

void TitleBar::doAction(int hit) {
    if (hit == HIT_PLUS) {
        if (m_onNewFile) m_onNewFile();
        return;
    }
    if (hit >= HIT_CLOSE_BASE) {
        int idx = hit - HIT_CLOSE_BASE;
        if (idx >= 0 && idx < (int)m_tabsInfo.size()) {
            if (m_onCloseTab) m_onCloseTab(idx);
        }
        return;
    }
    if (hit >= HIT_TAB_BASE) {
        int idx = hit - HIT_TAB_BASE;
        if (idx >= 0 && idx < (int)m_tabsInfo.size()) {
            if (m_onSwitchToTab) m_onSwitchToTab(idx);
        }
        return;
    }
    Btn b = (Btn)hit;
    switch (b) {
    case BTN_PIN:   if (m_onTogglePin) m_onTogglePin(); break;
    case BTN_MIN:   if (m_onMinimize) m_onMinimize(); break;
    case BTN_MAX:   if (m_onToggleMaximize) m_onToggleMaximize(); break;
    case BTN_CLOSE: if (m_onCloseWindow) m_onCloseWindow(); break;
    default: break;
    }
}

// ---- Button drawing ------------------------------------------------------

void TitleBar::drawButton(int bx, int by, int bw, int bh, Btn btn) {
    bool hot = (m_hover == (int)btn);
    bool pressed = (m_pressed == (int)btn && m_hover == (int)btn);
    Fl_Color bg;
    if (btn == BTN_CLOSE && hot)
        bg = m_theme ? m_theme->colors().highlight3 : fl_rgb_color(232, 17, 35);
    else if (hot)
        bg = m_theme ? m_theme->colors().highlight1
                     : fl_rgb_color(229, 229, 229);
    else
        bg = color();

    // Pressed state: only applied while the mouse is held down on the
    // button (never on release-triggered action).
    if (pressed) {
        bg = m_theme ? m_theme->colors().highlight2 : bg;
    }

    fl_draw_box(FL_FLAT_BOX, bx, by, bw, bh, bg);

    Fl_Color ic = (btn == BTN_CLOSE && hot) ? FL_WHITE
                   : fl_contrast(FL_FOREGROUND_COLOR, color());

    int cx = bx + bw / 2;
    int cy = by + bh / 2;
    fl_color(ic);

    switch (btn) {
    case BTN_PIN: {
        if (m_pinned) fl_line_style(FL_SOLID, 2);
        fl_color(ic);
        fl_begin_line();
        fl_vertex(cx - 5, cy - 7); fl_vertex(cx + 5, cy - 7);
        fl_end_line();
        fl_begin_line();
        fl_vertex(cx - 5, cy - 1); fl_vertex(cx, cy - 6); fl_vertex(cx + 5, cy - 1);
        fl_end_line();
        fl_begin_line();
        fl_vertex(cx, cy - 6); fl_vertex(cx, cy + 7);
        fl_end_line();
        fl_line_style(0);
        break;
    }
    case BTN_MIN: {
        fl_begin_line();
        fl_vertex(cx - 5, cy); fl_vertex(cx + 5, cy);
        fl_end_line();
        break;
    }
    case BTN_MAX: {
        if (!m_maximized) {
            int s = 10;
            fl_rect(cx - s / 2, cy - s / 2, s, s, ic);
        } else {
            int s = 8, off = 3;
            fl_rect(cx - s / 2 + off, cy - s / 2, s, s, ic);
            fl_rect(cx - s / 2, cy - s / 2 + off, s, s, ic);
        }
        break;
    }
    case BTN_CLOSE: {
        fl_begin_line();
        fl_vertex(cx - 5, cy - 5); fl_vertex(cx + 5, cy + 5);
        fl_end_line();
        fl_begin_line();
        fl_vertex(cx + 5, cy - 5); fl_vertex(cx - 5, cy + 5);
        fl_end_line();
        break;
    }
    default: break;
    }
}

// ---- Event handling ------------------------------------------------------

int TitleBar::handle(int event) {
    switch (event) {
    case FL_MOVE: {
        int prev = m_hover;
        m_hover = hitTest(Fl::event_x(), Fl::event_y());
        if (m_hover != prev) redraw();
        return 1;
    }
      case FL_LEAVE:
          if (m_hover != BTN_NONE || m_pressed != BTN_NONE) {
              m_hover = BTN_NONE;
              m_pressed = BTN_NONE;
              redraw();
          }
          return 1;
    case FL_PUSH: {
        m_hover = hitTest(Fl::event_x(), Fl::event_y());
        m_pressed = m_hover;
        if (m_pressed != BTN_NONE) {
            redraw();
            return 1;
        }
        if (Fl::event_button() == FL_LEFT_MOUSE) {
            if (Fl::event_clicks() > 0) {
                Fl::event_clicks(0);
                if (m_onToggleMaximize) m_onToggleMaximize();
                return 1;
            }
            m_dragging = true;
            m_dragStartX = Fl::event_x_root();
            m_dragStartY = Fl::event_y_root();
            Fl_Window *win = window();
            if (win) { m_winX = win->x(); m_winY = win->y(); }
            return 1;
        }
        return 0;
    }
    case FL_DRAG: {
        if (m_dragging) {
            int dx = Fl::event_x_root() - m_dragStartX;
            int dy = Fl::event_y_root() - m_dragStartY;
            Fl_Window *win = window();
            if (win) win->position(m_winX + dx, m_winY + dy);
            return 1;
        }
        return 0;
    }
    case FL_RELEASE: {
        if (m_pressed != BTN_NONE) {
            int btn = m_pressed;
            m_pressed = BTN_NONE;
            if (btn == m_hover) doAction(btn);
            redraw();
            return 1;
        }
        m_dragging = false;
        return 1;
    }
    }
    return 0;
}
