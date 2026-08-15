#include "GoTo.h"
#include "core/Theme.h"
#include "ui/ThemeWidgets.h"

#include <FL/Fl_Input.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/fl_draw.H>
#include <string.h>

#include "core/I18n.h"
#include "ui/Layout.h"

// Fl_Input subclass that does NOT consume Ctrl+H / Ctrl+F / Ctrl+G
// internally, so those shortcuts can reach MainWindow's dispatchShortcut.
class PassThruInput : public Fl_Input {
public:
    PassThruInput(int x, int y, int w, int h, const char *lbl = nullptr)
        : Fl_Input(x, y, w, h, lbl) {}
    int handle(int ev) FL_OVERRIDE {
        if (ev == FL_KEYDOWN || ev == FL_SHORTCUT) {
            int k = Fl::event_key();
            int s = Fl::event_state();
            // Let Ctrl+F/H/G and Enter pass through to the parent
            // so MainWindow's dispatchShortcut / GoTo::handle() can
            // process them.
            if ((s & FL_COMMAND) && (k == 'f' || k == 'h' || k == 'g'))
                return 0;
            if (k == FL_Enter || k == FL_KP_Enter)
                return 0;
        }
        return Fl_Input::handle(ev);
    }
};

static const int LINE_H = gBarH;   // one bar row (unified height)
static const int GAP = 6;
static const int FIELD_H = 22;
static constexpr int kInputWidth = 120;  // shared by Find/Replace/GoTo

// Measure the label text ("Line:" / "琛岋細") width at labelsize 12 so the
// label overlay fits neatly inside the left edge of the input box.
static int gotoLabelWidth() {
    fl_font(FL_HELVETICA, 12);
    return (int)fl_width(I18n::get("gotoline.label")) + 6;
}

GoTo::GoTo(int x, int y, int w, int h)
    : Fl_Group(x, y, w, h)
    , m_lineInput(nullptr)
    , m_btnGo(nullptr)
    , m_btnClose(nullptr)
    , m_lineLabel(nullptr)
    , m_editor(nullptr) {
    begin();

    int line1Y = y;
    const int labelW = gotoLabelWidth();

    // Input box 鈥?initial width is just a placeholder; layout()
    // computes the real width dynamically from available space.
    m_lineInput = new PassThruInput(x + GAP, line1Y + (LINE_H - FIELD_H) / 2,
                                labelW + 80, FIELD_H);
    m_lineInput->labelsize(11);
    m_lineInput->textsize(11);
    m_lineInput->box(FL_BORDER_BOX);
    m_lineInput->color(fl_rgb_color(255, 255, 255));
    m_lineInput->textcolor(FL_BLACK);
    m_lineInput->cursor_color(FL_BLACK);

    // Inline label overlaid on the left side of the input box (gray,
    // no box, shown when the input is empty and unfocused).
    m_lineLabel = new Fl_Box(m_lineInput->x() + 4,
                              m_lineInput->y(),
                              labelW - 4, FIELD_H,
                              I18n::get("gotoline.label"));
    m_lineLabel->labelsize(11);
    m_lineLabel->labelcolor(fl_rgb_color(140, 140, 140));
    m_lineLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    m_lineLabel->box(FL_NO_BOX);

    // Go button 鈥?positioned after the input box (layout adjusts).
    const char *goLbl = I18n::get("gotoline.go");
    int goW = (int)fl_width(goLbl) + 10;
    m_btnGo = new HoverButton(m_lineInput->x() + m_lineInput->w() + GAP,
                             line1Y + (LINE_H - FIELD_H) / 2,
                             goW, FIELD_H, goLbl);
    m_btnGo->labelsize(11);
    m_btnGo->callback(cbGo, this);

    // Close button 鈥?"X" at the far right, same style as FindReplace.
    m_btnClose = new HoverButton(x + w - 20 - GAP,
                                line1Y + (LINE_H - FIELD_H) / 2,
                                20, FIELD_H, "X");
    m_btnClose->labelsize(11);
    m_btnClose->callback(cbClose, this);
    m_btnClose->tooltip(I18n::get("find.close"));

    end();

    color(fl_rgb_color(200, 200, 200));
    box(FL_FLAT_BOX);

    layout(w);
    hide();
}

GoTo::~GoTo() = default;

void GoTo::setFontSize(int sz) {
    m_fontSize = sz;
    m_lineInput->textsize(sz);
    if (m_lineLabel) m_lineLabel->labelsize(sz);
    if (m_btnGo) m_btnGo->labelsize(sz);
    if (m_btnClose) m_btnClose->labelsize(sz);
    layout(w());
    redraw();
}

void GoTo::setTheme(const Theme *theme) {
    m_theme = theme;
    if (m_theme) {
        color(m_theme->colors().bgPanel);
        if (m_lineLabel) m_lineLabel->labelcolor(m_theme->colors().textSecondary);
        if (m_lineInput) { m_lineInput->color(m_theme->colors().bgEditor); m_lineInput->box(FL_BORDER_BOX); m_lineInput->cursor_color(m_theme->colors().textPrimary); m_lineInput->selection_color(m_theme->colors().accentSelection); }
        if (m_btnGo) { m_btnGo->color(m_theme->colors().bgPanel); m_btnGo->selection_color(m_theme->colors().accentSelection); m_btnGo->labelcolor(m_theme->colors().textPrimary); m_btnGo->labelsize(m_fontSize); }
        if (m_btnClose) { m_btnClose->color(m_theme->colors().bgPanel); m_btnClose->selection_color(m_theme->colors().accentSelection); m_btnClose->labelcolor(m_theme->colors().textPrimary); m_btnClose->labelsize(m_fontSize); }
    }
    redraw();
}

void GoTo::setEditor(Fl_Text_Editor *e) {
    m_editor = e;
}

void GoTo::activate() {
    show();
    m_lineInput->value("");
    Fl::focus(m_lineInput);
    updateHintVisibility();
    layout(w());
}

void GoTo::layout(int w) {
    const int labelW = gotoLabelWidth();

    // Go button width - use the same fit() sizing as every other Pecia
    // button (kPadX padding + kMinW floor) so it matches the toolbar/
    // Lua/AI buttons instead of a cramped label+"10".
    int goW = m_btnGo ? static_cast<HoverButton *>(m_btnGo)->fitWidth() : 48;

    // Same fixed width as FindReplace 鈥?all three input boxes
    // (Find, Replace, GoTo) are identical.
    int inputW = kInputWidth;
    if (inputW < 60) inputW = 60;

    int cx = x() + GAP;

    // Input box.
    m_lineInput->resize(cx, m_lineInput->y(), inputW, m_lineInput->h());
    cx += inputW;

    // Inline label overlaid on the left edge of the input.
    m_lineLabel->resize(m_lineInput->x() + 4, m_lineLabel->y(),
                         labelW - 4, m_lineLabel->h());

    // Go button
    cx += GAP;
    m_btnGo->resize(cx, m_btnGo->y(), goW, m_btnGo->h());

    // Close button: always at the far right, 20 px wide "X"
    m_btnClose->resize(x() + w - 20 - GAP, m_btnClose->y(), 20, m_btnClose->h());

    updateHintVisibility();
}

void GoTo::gotoLine() {
    const char *val = m_lineInput->value();
    if (!val || !*val) return;

    char *endptr = nullptr;
    long lineNum = strtol(val, &endptr, 10);
    if (endptr == val || lineNum <= 0) return;

    if (m_onGo) m_onGo(lineNum);
}

void GoTo::refreshLabels() {
    m_lineLabel->label(I18n::get("gotoline.label"));
    m_btnGo->label(I18n::get("gotoline.go"));
    // Close button stays "X" 鈥?not translated

    int goW = (int)fl_width(m_btnGo->label()) + 10;
    m_btnGo->resize(m_btnGo->x(), m_btnGo->y(), goW, m_btnGo->h());
}

void GoTo::updateHintVisibility() {
    if (!m_lineLabel || !m_lineInput) return;

    const char *v = m_lineInput->value();
    bool focused = (Fl::focus() == m_lineInput);
    if (!v || !*v) {
        if (!focused) m_lineLabel->show();
        else m_lineLabel->hide();
    } else {
        m_lineLabel->hide();
    }
}

int GoTo::handle(int event) {
    updateHintVisibility();
    if (event == FL_KEYDOWN && (Fl::event_key() == FL_Enter || Fl::event_key() == FL_KP_Enter)) {
        gotoLine();
        return 1;
    }
    return Fl_Group::handle(event);
}

void GoTo::cbGo(Fl_Widget * /*w*/, void *data) {
    GoTo *self = static_cast<GoTo *>(data);
    if (self) self->gotoLine();
}

void GoTo::cbClose(Fl_Widget * /*w*/, void *data) {
    GoTo *self = static_cast<GoTo *>(data);
    if (!self) return;
    self->hide();
    if (self->m_onClose) self->m_onClose();
    if (self->m_editor) Fl::focus(self->m_editor);
}


