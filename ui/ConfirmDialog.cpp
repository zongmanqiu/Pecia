// ConfirmDialog.cpp - custom modal confirmation dialog
#include "ui/ConfirmDialog.h"
#include "ui/InfoWindow.h"   // createInfoTitleBar, createBorderOverlay
#include "ui/ThemeWidgets.h"
#include "ui/Layout.h"
#include "core/ShortcutCore.h"
#include "core/Theme.h"
#include <FL/fl_draw.H>

static const int BTN_H = gBtnH;   // unified button height
static const int MARGIN = 16;
static const int GAP = 8;
static const int BTN_W = 90;

ConfirmDialog::ConfirmDialog(const char *title, const char *message,
                             const char *b0, const char *b1, const char *b2,
                             const Theme *theme, int uiFontSize)
    : Fl_Double_Window(420, 0, title), m_theme(theme)  // width 420, height computed below
{
    set_modal();
    border(0);
    box(FL_FLAT_BOX);
    color(theme ? theme->colors().bgEditor : FL_BACKGROUND2_COLOR);

    // --- Message text height ---
    fl_font(uiFontSize ? FL_HELVETICA : FL_HELVETICA, uiFontSize ? uiFontSize : 14);
    int textW = w() - 2 * MARGIN;
    int textH = 0;
    if (message) {
        // Approximate wrapped text height
        const char *p = message;
        int lines = 1;
        while (*p) { if (*p++ == '\n') ++lines; }
        // Also check word-wrap by measuring line width
        int maxW = 0;
        const char *start = message;
        const char *end;
        while ((end = strchr(start, '\n')) != nullptr) {
            int lw = (int)fl_width(start, (int)(end - start));
            if (lw > maxW) maxW = lw;
            start = end + 1;
        }
        int lw = (int)fl_width(start);
        if (lw > maxW) maxW = lw;

        if (maxW > textW) {
            // Rough: add extra lines for wrapping
            lines += maxW / textW;
            if (maxW % textW == 0) lines--;
        }
        if (lines < 1) lines = 1;
        textH = lines * (uiFontSize + 4) + 8;
        if (textH < 40) textH = 40;
    }

    int winW = 420;
    int winH = TITLE_H + MARGIN + textH + MARGIN + gBarH;
    size(winW, winH);

    // Title bar (top)
    m_titleBar = createInfoTitleBar(0, 0, winW, TITLE_H, title, theme, uiFontSize);
    if (theme) ((Fl_Widget*)m_titleBar)->color(theme->colors().bgChrome);

    // Message
    int msgY = TITLE_H + MARGIN;
    m_message = new Fl_Box(MARGIN, msgY, winW - 2 * MARGIN, textH, message);
    m_message->box(FL_NO_BOX);
    m_message->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    m_message->labelsize(uiFontSize ? uiFontSize : 14);
    m_message->labelcolor(theme ? theme->colors().textPrimary : FL_BLACK);

    // Bottom button bar (matches SettingsDialog style)
    int btnBarY = TITLE_H + MARGIN + textH + MARGIN;
    Fl_Color chromeCol = theme ? theme->colors().bgChrome : FL_BACKGROUND2_COLOR;
    Fl_Group *btnBar = new Fl_Group(0, btnBarY, winW, gBarH);
    btnBar->box(FL_FLAT_BOX);
    btnBar->color(chromeCol);

    Fl_Color btnFg = theme ? theme->colors().textPrimary : FL_BLACK;
    const char *labels[] = { b0, b1, b2 };
    int btnW[3] = { 0, 0, 0 };
    for (int i = 0; i < 3; ++i) {
        if (!labels[i]) { m_btn[i] = nullptr; continue; }
        int bw = 0;
        fl_font(FL_HELVETICA, uiFontSize ? uiFontSize : 14);
        bw = (int)fl_width(labels[i]) + 24;
        if (bw < 60) bw = 60;
        btnW[i] = bw;
    }
    int totalW = 0;
    for (int i = 0; i < 3; ++i) {
        if (!labels[i]) continue;
        if (totalW > 0) totalW += GAP;
        totalW += btnW[i];
    }
    int btnX = winW - MARGIN - totalW;
    int btnY = btnBarY + (gBarH - BTN_H) / 2;

    for (int i = 0; i < 3; ++i) {
        if (!labels[i]) continue;
        m_btn[i] = new HoverButton(btnX, btnY, btnW[i], BTN_H, labels[i]);
        m_btn[i]->color(chromeCol);
        m_btn[i]->selection_color(theme ? theme->colors().accentSelection : FL_SELECTION_COLOR);
        m_btn[i]->labelcolor(btnFg);
        m_btn[i]->labelsize(uiFontSize);
        m_btn[i]->callback(cbBtn, this);
        btnX += btnW[i] + GAP;
    }
    btnBar->resizable(nullptr);
    btnBar->end();

    // Border overlay (fallback; draw() below overdraws for reliability)
    m_border = createBorderOverlay(0, TITLE_H, winW, winH - TITLE_H, theme);

    end();
    resizable(nullptr);
}

void ConfirmDialog::draw() {
    Fl_Double_Window::draw();
    // Draw outer border last, on top of everything (like SettingsDialog).
    Fl_Color bc = m_theme ? m_theme->colors().borderColor : fl_rgb_color(127, 127, 127);
    ::fl_color(bc);
    ::fl_rectf(0, 0, w(), 1);
    ::fl_rectf(0, h() - 1, w(), 1);
    ::fl_rectf(0, 0, 1, h());
    ::fl_rectf(w() - 1, 0, 1, h());
}

ConfirmDialog::~ConfirmDialog() = default;

int ConfirmDialog::handle(int event) {
    // OK shortcut (Window > Button > OK): triggers the first button.
    if (event == FL_KEYDOWN || event == FL_SHORTCUT) {
        if (dialogOkShortcutMatches(Fl::event_key(), Fl::event_state())) {
            if (m_btn[0]) {
                m_result = 0;
                hide();
                return 1;
            }
        }
    }
    return Fl_Double_Window::handle(event);
}

int ConfirmDialog::run() {
    // Center on screen
    position((Fl::w() - w()) / 2, (Fl::h() - h()) / 2);
    m_result = -1;
    show();
    while (shown()) Fl::wait();
    return m_result;
}

void ConfirmDialog::cbBtn(Fl_Widget *w, void *data) {
    auto *self = static_cast<ConfirmDialog *>(data);
    for (int i = 0; i < 3; ++i) {
        if (self->m_btn[i] == w) {
            self->m_result = i;
            break;
        }
    }
    self->hide();
}

