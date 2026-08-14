// InfoWindow.cpp - reusable info dialog.
#include "ui/InfoWindow.h"
#include "core/Theme.h"
#include "core/I18n.h"

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Box.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>

#if defined(_WIN32)
#include <windows.h>
#endif

class InfoTitleBar : public Fl_Widget {
    bool m_closeHover = false;
    bool m_closePressed = false;
    const char *m_title;
    const Theme *m_theme = nullptr;
    int m_fontSize = 16;

public:
    InfoTitleBar(int X, int Y, int W, int H, const char *title)
        : Fl_Widget(X, Y, W, H), m_title(title) {
        box(FL_FLAT_BOX);
        color(FL_BACKGROUND2_COLOR);
    }
    void setTheme(const Theme *t, int sz) { m_theme = t; m_fontSize = sz; }

    void draw() FL_OVERRIDE {
        Fl_Color bg = m_theme ? m_theme->colors().bgChrome : color();
        fl_draw_box(FL_FLAT_BOX, x(), y(), w(), h(), bg);

        fl_font(FL_HELVETICA, m_fontSize);
        Fl_Color txtCol = m_theme ? m_theme->colors().textPrimary : fl_contrast(FL_FOREGROUND_COLOR, bg);
        fl_color(txtCol);
        fl_draw(m_title, x() + 12, y(), w() - 58, h(),
                FL_ALIGN_LEFT | FL_ALIGN_CENTER);

        int cx = x() + w() - 46;
        int cy = y();
        int cw = 46;
        int ch = h();

        bool hot = m_closeHover;
        Fl_Color closeBg = hot
            ? (m_theme ? m_theme->colors().hoverClose : fl_rgb_color(232, 17, 35))
            : bg;
        fl_draw_box(FL_FLAT_BOX, cx, cy, cw, ch, closeBg);

        Fl_Color ic = hot
            ? FL_WHITE
            : (m_theme ? m_theme->colors().textPrimary : fl_contrast(FL_FOREGROUND_COLOR, bg));
        int bx = cx + cw / 2;
        int by = cy + ch / 2;
        fl_color(ic);
        fl_begin_line();
        fl_vertex(bx - 5, by - 5); fl_vertex(bx + 5, by + 5);
        fl_end_line();
        fl_begin_line();
        fl_vertex(bx + 5, by - 5); fl_vertex(bx - 5, by + 5);
        fl_end_line();

        // Title bar border lines are omitted — the dialog-level border
        // (SettingsDialog::draw() or BorderOverlay) covers left, right,
        // and bottom.  A top border is unnecessary since the window has
        // no OS frame above the title bar.
    }

    int handle(int event) FL_OVERRIDE {
        bool inClose = (Fl::event_x() >= x() + w() - 46 && Fl::event_x() < x() + w() &&
                        Fl::event_y() >= y() && Fl::event_y() < y() + h());
        switch (event) {
        case FL_ENTER: return 1;
        case FL_MOVE:
            if (inClose != m_closeHover) { m_closeHover = inClose; redraw(); }
            return 1;
        case FL_LEAVE:
            m_closeHover = false; m_closePressed = false; redraw();
            return 1;
        case FL_PUSH:
            if (inClose) { m_closePressed = true; redraw(); return 1; }
            if (Fl::event_button() == FL_LEFT_MOUSE) {
                Fl_Window *win = window();
                if (win) {
                    ReleaseCapture();
                    SendMessage((HWND)fl_xid(win), WM_NCLBUTTONDOWN, HTCAPTION, 0);
                }
            }
            return 1;
        case FL_RELEASE:
            if (m_closePressed && inClose) window()->hide();
            m_closePressed = false; redraw();
            return 1;
        default: return 0;
        }
    }
};

class BorderOverlay : public Fl_Widget {
    const Theme *m_theme = nullptr;
public:
    BorderOverlay(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) {}
    void setTheme(const Theme *t) { m_theme = t; }
    void draw() FL_OVERRIDE {
        Fl_Color c = m_theme ? m_theme->colors().borderColor : fl_rgb_color(127, 127, 127);
        ::fl_color(c);
        // Draw the border on all four sides of the WINDOW. The widget
        // itself is offset below the title bar (see InfoWindow::show)
        // so the event dispatch never routes hover events through it.
        int W = window() ? window()->w() : w();
        int H = window() ? window()->h() : h();
        ::fl_rectf(0, 0, W, 1);           // top
        ::fl_rectf(0, H - 1, W, 1);       // bottom
        ::fl_rectf(0, 0, 1, H);           // left
        ::fl_rectf(W - 1, 0, 1, H);       // right
    }
    int handle(int) FL_OVERRIDE { return 0; }
};

InfoWindow::InfoWindow() = default;
InfoWindow::~InfoWindow() = default;

void InfoWindow::setTheme(const Theme *theme, int uiFontSize) {
    m_theme = theme;
    m_uiFontSize = uiFontSize;
}

// A thin wrapper around Fl_Double_Window that draws the outer border
// last, on top of all children (same approach as SettingsDialog).
class InfoDialog : public Fl_Double_Window {
    const Theme *m_theme;
public:
    InfoDialog(int W, int H, const Theme *theme)
        : Fl_Double_Window(W, H), m_theme(theme) {}
    void draw() FL_OVERRIDE {
        Fl_Double_Window::draw();
        Fl_Color bc = m_theme ? m_theme->colors().borderColor : fl_rgb_color(127, 127, 127);
        ::fl_color(bc);
        ::fl_rectf(0, 0, w(), 1);
        ::fl_rectf(0, h() - 1, w(), 1);
        ::fl_rectf(0, 0, 1, h());
        ::fl_rectf(w() - 1, 0, 1, h());
    }
};

void InfoWindow::show() {
    const int TITLE_H = 32;
    const int ROW_H   = 28;
    const int PAD     = 20;
    const int ROWS    = (int)m_rows.size();
    int W = m_width;
    int H = TITLE_H + 16 + ROWS * ROW_H + PAD;

    InfoDialog dlg(W, H, m_theme);
    dlg.border(0);
    dlg.box(FL_FLAT_BOX);
    dlg.color(m_theme ? m_theme->colors().bgEditor : FL_WHITE);

    Fl_Group *content = new Fl_Group(0, TITLE_H, W, H - TITLE_H);
    content->box(FL_FLAT_BOX);
    content->color(m_theme ? m_theme->colors().bgEditor : FL_WHITE);

    int y = TITLE_H + 16;
    for (const auto &r : m_rows) {
        Fl_Box *lb = new Fl_Box(PAD, y, (W - 2 * PAD) / 2, 24);
        lb->box(FL_NO_BOX);
        lb->labelsize(m_uiFontSize);
        lb->labelcolor(m_theme ? m_theme->colors().textPrimary : FL_BLACK);
        lb->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        lb->label(r.label.c_str());

        Fl_Box *vb = new Fl_Box(W - PAD - 140, y, 140, 24);
        vb->box(FL_NO_BOX);
        vb->labelsize(m_uiFontSize);
        vb->labelcolor(m_theme ? m_theme->colors().textPrimary : FL_BLACK);
        vb->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
        vb->label(r.value.c_str());

        y += ROW_H;
    }
    content->end();

    InfoTitleBar *tb = new InfoTitleBar(0, 0, W, TITLE_H, m_title.c_str());
    tb->setTheme(m_theme, m_uiFontSize);

    BorderOverlay *bo = new BorderOverlay(0, TITLE_H, W, H - TITLE_H);
    bo->setTheme(m_theme);

    dlg.end();
    dlg.position((Fl::w() - W) / 2, (Fl::h() - H) / 2);
    dlg.show();

#if defined(_WIN32)
    HWND hwnd = fl_xid(&dlg);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_APPWINDOW);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);

#endif

    while (dlg.shown()) Fl::wait();
}

InfoTitleBar *createInfoTitleBar(int X, int Y, int W, int H, const char *title, const Theme *theme, int fontSize) {
    InfoTitleBar *tb = new InfoTitleBar(X, Y, W, H, title);
    tb->setTheme(theme, fontSize);
    return tb;
}

BorderOverlay *createBorderOverlay(int X, int Y, int W, int H, const Theme *theme) {
    BorderOverlay *bo = new BorderOverlay(X, Y, W, H);
    bo->setTheme(theme);
    return bo;
}
