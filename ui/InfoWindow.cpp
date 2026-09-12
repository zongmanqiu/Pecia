// InfoWindow.cpp - reusable info dialog, built on DialogBase.
#include "ui/InfoWindow.h"
#include "ui/DialogBase.h"
#include "ui/SmokeTest.h"
#include "core/Theme.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

// A simple full-window info dialog: title bar (close-only) + a stack of
// label/value rows. No bottom button bar (the close button covers it).
// Derived from DialogBase so it shares the exact same shell (TitleBar,
// border, colors) as every other window.
class InfoDialogBase : public DialogBase {
public:
    InfoDialogBase(const char *title, const std::vector<InfoRow> &rows,
                   const Theme *theme, int uiFontSize, int width)
        : DialogBase(width, 0, title, theme, uiFontSize, ModalDialog) {
        begin();
        initShell(title);

        const int ROW_H = 28;
        const int PAD = 20;
        int W = width;
        int H = TITLE_H + 16 + (int)rows.size() * ROW_H + PAD;
        size(W, H);

        int y = TITLE_H + 16;
        for (const auto &r : rows) {
            Fl_Box *lb = new Fl_Box(PAD, y, (W - 2 * PAD) / 2, 24);
            lb->box(FL_NO_BOX);
            lb->labelsize(uiFontSize);
            lb->labelcolor(theme ? theme->colors().text1 : FL_BLACK);
            lb->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
            lb->copy_label(r.label.c_str());

            Fl_Box *vb = new Fl_Box(W - PAD - 140, y, 140, 24);
            vb->box(FL_NO_BOX);
            vb->labelsize(uiFontSize);
            vb->labelcolor(theme ? theme->colors().text1 : FL_BLACK);
            vb->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
            vb->copy_label(r.value.c_str());

            y += ROW_H;
        }

        end();
        finalizeShell();
    }
};

} // namespace

InfoWindow::InfoWindow() = default;
InfoWindow::~InfoWindow() = default;

void InfoWindow::setTheme(const Theme *theme, int uiFontSize) {
    m_theme = theme;
    m_uiFontSize = uiFontSize;
}

void InfoWindow::show() {
    int W = m_width;
    InfoDialogBase dlg(m_title.c_str(), m_rows, m_theme, m_uiFontSize, W);
    dlg.position((Fl::w() - dlg.w()) / 2, (Fl::h() - dlg.h()) / 2);
    dlg.show();
    if (ui::g_smokeMode) { dlg.hide(); return; }   // smoke: build+show only
    while (dlg.shown()) Fl::wait();
}
