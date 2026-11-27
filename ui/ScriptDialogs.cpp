// ScriptDialogs.cpp - see ui/ScriptDialogs.h
#include "ui/ScriptDialogs.h"
#include "ui/ThemeWidgets.h"
#include "ui/WidgetRegistry.h"
#include "ui/Layout.h"
#include "ui/SmokeTest.h"
#include "core/Theme.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Scroll.H>
#include <FL/fl_draw.H>

#include <algorithm>

static const int MARGIN = 16;
static const int GAP = 8;
static const int BTN_H = gBtnH;
static const int ROW_H = 26;
// One list row: the item button plus the gap to the next one.
static const int ROW_STEP = ROW_H - 4;
// Inner padding of the list viewport (left/right and top/bottom).
static const int LIST_PAD = 4;

// ---------------------------------------------------------------------------
// PromptDialog
// ---------------------------------------------------------------------------

PromptDialog::PromptDialog(const char *title, const char *label,
                           const char *dflt, const Theme *theme, int uiFontSize)
    : DialogBase(420, 0, title, theme, uiFontSize, ModalDialog)
{
    begin();
    initShell(title);

    const int fs = uiFontSize ? uiFontSize : 14;
    const int labelH = ROW_H;
    const int winH = TITLE_H + MARGIN + labelH + GAP + ROW_H + MARGIN + gBarH;
    size(420, winH);

    const ThemeColors &tc = theme ? theme->colors() : ThemeColors{};
    int y = TITLE_H + MARGIN;

    Fl_Box *lbl = new Fl_Box(MARGIN, y, 420 - 2 * MARGIN, labelH, label);
    lbl->box(FL_NO_BOX);
    lbl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    lbl->labelcolor(tc.text1);
    lbl->labelsize(fs);
    y += labelH + GAP;

    m_input = new Fl_Input(MARGIN, y, 420 - 2 * MARGIN, ROW_H);
    m_input->value(dflt ? dflt : "");
    m_input->textsize(fs);
    m_input->color(tc.background1);
    m_input->textcolor(tc.text1);
    m_input->selection_color(tc.highlight2);
    m_input->when(FL_NO_EVENT);

    int barY = winH - gBarH;
    Fl_Group *bar = new Fl_Group(0, barY, 420, gBarH);
    bar->box(FL_FLAT_BOX);
    bar->color(tc.background2);
    m_cancelBtn = new HoverButton(0, barY + (gBarH - BTN_H) / 2, 0, BTN_H, "Cancel");
    m_cancelBtn->callback(cbCancel, this);
    m_okBtn = new HoverButton(0, barY + (gBarH - BTN_H) / 2, 0, BTN_H, "OK");
    m_okBtn->callback(cbOk, this);
    fitButtonRow({ m_okBtn, m_cancelBtn }, 420, barY + gBarH / 2, MARGIN, GAP);
    bar->end();
    end();
    finalizeShell();

    // Measurable on purpose: a script's dialog is the surface an external
    // agent checks after it changes a script (ui.inspect / ui.send).
    enableInspection("input");
}

PromptDialog::~PromptDialog() = default;

void PromptDialog::cbOk(Fl_Widget *, void *d)
{
    auto *self = static_cast<PromptDialog *>(d);
    self->m_value = self->m_input ? self->m_input->value() : "";
    self->m_ok = true;
    self->hide();
}

void PromptDialog::cbCancel(Fl_Widget *, void *d)
{
    auto *self = static_cast<PromptDialog *>(d);
    self->m_ok = false;
    self->hide();
}

int PromptDialog::handle(int event)
{
    if (event == FL_KEYDOWN || event == FL_SHORTCUT) {
        if (Fl::event_key() == FL_Enter) { cbOk(nullptr, this); return 1; }
        if (Fl::event_key() == FL_Escape) { cbCancel(nullptr, this); return 1; }
    }
    return DialogBase::handle(event);
}

bool PromptDialog::run()
{
    if (ui::g_smokeMode) {
        shown() ? void() : show();
        // No event loop in smoke mode: take the constructor's prefilled
        // default as the accepted value instead of leaving m_ok=false.
        m_value = m_input ? m_input->value() : "";
        m_ok = true;
        return m_ok;
    }
    position((Fl::w() - w()) / 2, (Fl::h() - h()) / 2);
    m_ok = false;
    show();
    if (m_input) { m_input->take_focus(); Fl::flush(); }
    while (shown()) Fl::wait();
    return m_ok;
}

// ---------------------------------------------------------------------------
// PickDialog
// ---------------------------------------------------------------------------

PickDialog::PickDialog(const char *title, const char *label,
                       const std::vector<std::string> &items, int pre,
                       const Theme *theme, int uiFontSize)
    : DialogBase(420, 0, title, theme, uiFontSize, ModalDialog)
{
    begin();
    initShell(title);

    const int fs = uiFontSize ? uiFontSize : 14;
    const ThemeColors &tc = theme ? theme->colors() : ThemeColors{};
    const int n = (int)items.size();
    m_default = pre - 1;
    const int visibleRows = std::min(8, std::max(1, n));
    const int listH = visibleRows * ROW_STEP;
    const int winH = TITLE_H + MARGIN + ROW_H + GAP + listH + MARGIN + gBarH;
    size(420, winH);

    int y = TITLE_H + MARGIN;
    Fl_Box *lbl = new Fl_Box(MARGIN, y, 420 - 2 * MARGIN, ROW_H, label);
    lbl->box(FL_NO_BOX);
    lbl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    lbl->labelcolor(tc.text1);
    lbl->labelsize(fs);
    y += ROW_H + GAP;

    // The list is a scroll viewport (the visible window onto the rows) with
    // one inner group holding the row buttons.
    //
    // FLTK places a group's children in WINDOW coordinates - there is NO
    // translation by the group's origin: Fl_Widget::x() is documented as
    // "the widget position in its window" (FL/Fl_Widget.H:358), and
    // Fl_Group::draw_children() pushes the clip at the group's own rect but
    // draws every child at its raw x()/y() (src/Fl_Group.cxx:920-928).
    // Fl_Scroll says the same in its own docs: its children are positioned
    // relative to the scroll's origin, i.e. in window coordinates
    // (src/Fl_Scroll.cxx:489-503). So the rows are placed at absolute
    // coordinates that land inside the viewport - the same rule
    // PromptDialog follows for its buttons.
    const int listTop = y;
    const int listW = 420 - 2 * MARGIN;              // the viewport width
    const int groupW = listW - 16;                   // leave room for the scrollbar
    // The scroll range comes from the DECLARED size of the scrolled group
    // (src/Fl_Scroll.cxx:220-242), so the group must be as tall as the whole
    // list - a fixed multiple of the viewport height would cut the tail off.
    const int groupH = n * ROW_STEP + 2 * LIST_PAD;

    Fl_Scroll *scroll = new Fl_Scroll(MARGIN, listTop, listW, listH);
    scroll->color(tc.background1);
    scroll->scrollbar.color(tc.background2);
    m_list = new Fl_Group(MARGIN, listTop, groupW, groupH);
    m_list->box(FL_NO_BOX);
    for (int i = 0; i < n; ++i) {
        auto *b = new HoverButton(MARGIN + LIST_PAD,
                                 listTop + LIST_PAD + i * ROW_STEP,
                                 groupW - 2 * LIST_PAD,
                                 ROW_H - 6, items[i].c_str());
        // The requested default sits first: it is what Enter/OK would take.
        b->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        b->labelcolor(tc.text1);
        b->labelsize(fs);
        b->color(i == pre - 1 ? tc.highlight1 : tc.background1);
        b->selection_color(tc.highlight2);
        b->setPressColor(tc.highlight2);
        b->callback(cbItem, this);
        b->user_data(const_cast<char *>(items[i].c_str()));
    }
    m_list->end();
    scroll->end();

    int barY = winH - gBarH;
    Fl_Group *bar = new Fl_Group(0, barY, 420, gBarH);
    bar->box(FL_FLAT_BOX);
    bar->color(tc.background2);
    m_cancelBtn = new HoverButton(0, barY + (gBarH - BTN_H) / 2, 0, BTN_H, "Cancel");
    m_cancelBtn->callback(cbCancel, this);
    fitButtonRow({ m_cancelBtn }, 420, barY + gBarH / 2, MARGIN, GAP);
    bar->end();
    end();
    finalizeShell();

    enableInspection("select");
}

PickDialog::~PickDialog() = default;

void PickDialog::cbItem(Fl_Widget *w, void *d)
{
    auto *self = static_cast<PickDialog *>(d);
    if (!self->m_list || !w) return;
    for (int i = 0; i < self->m_list->children(); ++i)
        if (self->m_list->child(i) == w) { self->m_choice = i; break; }
    self->hide();
}

void PickDialog::cbCancel(Fl_Widget *, void *d)
{
    auto *self = static_cast<PickDialog *>(d);
    self->m_choice = -1;
    self->hide();
}

int PickDialog::handle(int event)
{
    if (event == FL_KEYDOWN || event == FL_SHORTCUT) {
        if (Fl::event_key() == FL_Escape) { cbCancel(nullptr, this); return 1; }
    }
    return DialogBase::handle(event);
}

bool PickDialog::run()
{
    if (ui::g_smokeMode) {
        shown() ? void() : show();
        // Smoke mode has no click on a row: accept the requested default
        // (1-based pre -> 0-based index) rather than reporting a cancel.
        m_choice = m_default;
        return m_choice >= 0;
    }
    position((Fl::w() - w()) / 2, (Fl::h() - h()) / 2);
    m_choice = -1;
    show();
    Fl::flush();
    while (shown()) Fl::wait();
    return m_choice >= 0;
}
