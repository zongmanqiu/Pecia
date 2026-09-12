// FindReplace.cpp - inline find/replace bar implementation
#include "FindReplace.h"
#include "ui/MatchHighlight.h"
#include "ui/ThemeWidgets.h"
#include "core/Theme.h"
#include "editor/Editor.h"
#include "editor/Document.h"

#include <FL/Fl_Input.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
#include <string.h>

#include "core/I18n.h"
#include "core/SearchCore.h"
#include "ui/Layout.h"

// Fl_Input subclass that does NOT consume Ctrl+H / Ctrl+F / Ctrl+G
// internally, so those shortcuts can reach MainWindow's dispatchShortcut.
// Fl_Input_ normally interprets Ctrl+H as backspace (ASCII 0x08) on
// some platforms; we override handle() to let those key events through.
// (Shared implementation lives in PassThruInput.h - see GoTo/FindReplace.)
#include "PassThruInput.h"

namespace {

// The ONE width rule for every Pecia button: HoverButton::fitWidth()
// (label + 2*kPadX=24, clamped to kMinW=48). Find/Prev/All/Replace buttons
// must use this too so they match the toolbar, GoTo and Lua/AI buttons —
// no local fl_width()+padding formulas.
int hbFit(Fl_Button *b) {
    return static_cast<HoverButton *>(b)->fitWidth();
}

}  // namespace

static const int LINE_H = gBarH;   // one bar row (unified height)
static const int GAP = 6;
static const int FIELD_H = 22;
static constexpr int kInputWidth = 120;  // shared by Find/Replace/GoTo

// CenteredOutput - an Fl_Output subclass that draws its text value
// horizontally centered without a box or margins.
class CenteredOutput : public Fl_Output {
public:
    CenteredOutput(int X, int Y, int W, int H, const char *l = nullptr)
        : Fl_Output(X, Y, W, H, l) {}

    void draw() FL_OVERRIDE {
        // Clear the background with the widget's color to erase any previous text.
        fl_color(color());
        fl_rectf(x(), y(), w(), h());

        // Center the text value by computing an offset from the widget width.
        Fl_Boxtype b = box();
        int dx = Fl::box_dx(b);
        int dy = Fl::box_dy(b);
        int dw = Fl::box_dw(b);
        int dh = Fl::box_dh(b);

        const char *txt = value() ? value() : "";
        fl_font(textfont(), textsize());
        int tw = (int)fl_width(txt);
        int tx = x() + dx + (w() - dw - tw) / 2;
        if (tx < x() + dx) tx = x() + dx;

        int ty = y() + dy + (h() - dh - fl_height()) / 2;
        fl_color(textcolor());
        fl_draw(txt, tx, ty, tw, fl_height(), FL_ALIGN_LEFT);
    }

    int handle(int event) FL_OVERRIDE {
        // Read-only display; do not take focus or show a cursor.
        if (event == FL_FOCUS || event == FL_UNFOCUS)
            return 0;
        // Never show the text I-beam (Fl_Input_ sets FL_CURSOR_INSERT on
        // enter/move); force the default arrow.
        if (event == FL_ENTER || event == FL_MOVE ||
            event == FL_PUSH || event == FL_RELEASE ||
            event == FL_DRAG) {
            if (window()) window()->cursor(FL_CURSOR_DEFAULT);
            return 0;
        }
        return Fl_Output::handle(event);
    }
};

static int labelOffset(int fontSize) {
    // Approximate pixel width of the "Find:" / "Replace:" label so we
    // can align the input box with the buttons to its right.
    fl_font(FL_HELVETICA, fontSize);
    int w1 = (int)fl_width("Find:");
    int w2 = (int)fl_width("Replace:");
    return (w1 > w2 ? w1 : w2) + 6;
}

FindReplace::FindReplace(int x, int y, int w, int h)
    : Fl_Group(x, y, w, h)
    , m_findInput(nullptr)
    , m_replaceInput(nullptr)
    , m_btnNext(nullptr)
    , m_btnPrev(nullptr)
    , m_btnFindAll(nullptr)
    , m_btnReplace(nullptr)
    , m_btnReplaceAll(nullptr)
    , m_chkCase(nullptr)
    , m_chkWholeWord(nullptr)
    , m_chkWrap(nullptr)
    , m_matchCount(nullptr)
    , m_btnClose(nullptr)
    , m_editor(nullptr)
    , m_lastPos(-1)
    , m_totalCount(0)
    , m_currentIndex(0)
    , m_highlight(new MatchHighlight()) {
    begin();

    int line1Y = y;
    int line2Y = y + LINE_H;

    const int labelW = labelOffset(m_fontSize);
    const int replaceInputW = kInputWidth;
    const int findInputFullW = kInputWidth;

    // Measure button/checkbox label widths at the label font/size.
    fl_font(FL_HELVETICA, 11);

    int cx = x + GAP;

    // --- Line 1: Find ---
    m_findInput = new PassThruInput(cx, line1Y + (LINE_H - FIELD_H) / 2,
                                findInputFullW, FIELD_H);
    m_findInput->textsize(12);
    m_findInput->box(FL_BORDER_BOX);
    m_findInput->selection_color(m_theme ? m_theme->colors().highlight2 : FL_SELECTION_COLOR);
    m_findInput->callback(cbFindNext, this);
    m_findInput->when(FL_WHEN_ENTER_KEY_CHANGED);
    // Place the label as an inline note inside the input.
    cx += findInputFullW + GAP;

    int fixedRight = 24;  // close button width + a little margin
    m_btnClose = new HoverButton(x + w - fixedRight,
                                line1Y + (LINE_H - FIELD_H) / 2,
                                20, FIELD_H, "X");
    m_btnClose->labelsize(11);
    m_btnClose->callback(cbClose, this);
    m_btnClose->tooltip("Close find bar (Esc)");

    // Buttons sized to their label text + padding; checkboxes sized to
    // checkbox glyph (~14px) + label text + padding. All left-aligned.
    const int BTN_PAD = m_fontSize + 4;   // proportional to font size
    const int CHK_PAD = 24;   // glyph + text

    const char* lblNext = I18n::get("find.next");
    int wNext = (int)fl_width(lblNext) + BTN_PAD;
    m_btnNext = new HoverButton(cx, line1Y + (LINE_H - FIELD_H) / 2,
                              wNext, FIELD_H, lblNext);
    m_btnNext->labelsize(11);
    m_btnNext->callback(cbFindNext, this);
    cx += wNext + GAP;

    const char* lblPrev = I18n::get("find.prev");
    int wPrev = (int)fl_width(lblPrev) + BTN_PAD;
    m_btnPrev = new HoverButton(cx, line1Y + (LINE_H - FIELD_H) / 2,
                              wPrev, FIELD_H, lblPrev);
    m_btnPrev->labelsize(11);
    m_btnPrev->callback(cbFindPrev, this);
    cx += wPrev + GAP;

    const char* lblFindAll = I18n::get("find.all");
    int wFindAll = (int)fl_width(lblFindAll) + BTN_PAD;
    m_btnFindAll = new HoverButton(cx, line1Y + (LINE_H - FIELD_H) / 2,
                                 wFindAll, FIELD_H, lblFindAll);
    m_btnFindAll->labelsize(11);
    m_btnFindAll->callback(cbFindAll, this);
    cx += wFindAll + GAP + 6;

    const char* lblCase = I18n::get("find.matchcase");
    int wCase = (int)fl_width(lblCase) + CHK_PAD;
    m_chkCase = new Fl_Check_Button(cx, line1Y + (LINE_H - FIELD_H) / 2,
                                    wCase, FIELD_H, lblCase);
    m_chkCase->labelsize(11);
    cx += wCase + GAP;

    const char* lblWhole = I18n::get("find.wholeword");
    int wWhole = (int)fl_width(lblWhole) + CHK_PAD;
    m_chkWholeWord = new Fl_Check_Button(cx, line1Y + (LINE_H - FIELD_H) / 2,
                                          wWhole, FIELD_H, lblWhole);
    m_chkWholeWord->labelsize(11);
    cx += wWhole + GAP;

    const char* lblWrap = I18n::get("find.wrap");
    int wWrap = (int)fl_width(lblWrap) + CHK_PAD;
    m_chkWrap = new Fl_Check_Button(cx, line1Y + (LINE_H - FIELD_H) / 2,
                                    wWrap, FIELD_H, lblWrap);
    m_chkWrap->labelsize(11);
    m_chkWrap->value(1);
    cx += wWrap + GAP;

    // Match-count display fills the remaining space between the last
    // checkbox and the close button on the right. We compute its
    // geometry at the end (after the close button is placed) and store
    // the starting x so we can size it after the constructor is done.
    m_countStartX = cx;

    // Match-count display: transparent box, centered text, same gray
    // color as the input hint labels. It fills the remaining horizontal
    // space but does not look like an editable field.
    m_matchCount = new CenteredOutput(m_countStartX,
                                       line1Y + (LINE_H - FIELD_H) / 2,
                                       1, FIELD_H, "");
    m_matchCount->box(FL_NO_BOX);
    m_matchCount->labelsize(11);
    m_matchCount->textsize(11);
    m_matchCount->textcolor(m_theme ? m_theme->colors().text1 : FL_BLACK);
    m_matchCount->color(fl_rgb_color(200, 200, 200));
    m_matchCount->value("0/0");

    // Place inline "Find:" / "Replace:" notes inside the input boxes.
    // We do this by setting the input's value to a leading "Find:  "
    // prefix (light gray) and letting the user type after it. Simpler
    // approach: draw the note text on top of the input via a small
    // label widget placed just inside the input's left edge.

    // --- Line 2: Replace ---
    cx = x + GAP;
    m_replaceInput = new PassThruInput(cx, line2Y + (LINE_H - FIELD_H) / 2,
                                   replaceInputW + labelW, FIELD_H);
    m_replaceInput->textsize(12);
    m_replaceInput->box(FL_BORDER_BOX);
    cx += replaceInputW + labelW + GAP;

    const char* lblReplace = I18n::get("find.replace");
    int wReplace = (int)fl_width(lblReplace) + BTN_PAD;
    m_btnReplace = new HoverButton(cx, line2Y + (LINE_H - FIELD_H) / 2,
                                  wReplace, FIELD_H, lblReplace);
    m_btnReplace->labelsize(11);
    m_btnReplace->callback(cbReplace, this);
    cx += wReplace + GAP;

    const char* lblReplaceAll = I18n::get("find.replaceall");
    int wReplaceAll = (int)fl_width(lblReplaceAll) + BTN_PAD;
    m_btnReplaceAll = new HoverButton(cx, line2Y + (LINE_H - FIELD_H) / 2,
                                    wReplaceAll, FIELD_H, lblReplaceAll);
    m_btnReplaceAll->labelsize(11);
    m_btnReplaceAll->callback(cbReplaceAll, this);

    // --- Inline "Find:" / "Replace:" notes drawn on top of inputs ---
    // Use lightweight Fl_Box widgets with a gray color so the user
    // sees a hint label inside each empty input.
    m_findLabel = new Fl_Box(m_findInput->x() + 4,
                              m_findInput->y(),
                              labelW - 4, FIELD_H, I18n::get("find.findhint"));
    m_findLabel->labelsize(11);
    m_findLabel->labelcolor(fl_rgb_color(140, 140, 140));
    m_findLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    m_findLabel->box(FL_NO_BOX);

    m_replaceLabel = new Fl_Box(m_replaceInput->x() + 4,
                                 m_replaceInput->y(),
                                 labelW - 4, FIELD_H, I18n::get("find.replacehint"));
    m_replaceLabel->labelsize(11);
    m_replaceLabel->labelcolor(fl_rgb_color(140, 140, 140));
    m_replaceLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    m_replaceLabel->box(FL_NO_BOX);

    end();
    box(FL_FLAT_BOX);
    color(fl_rgb_color(200, 200, 200));
    // Set the match-count widget to its correct width immediately so
    // the centered text doesn't overflow into neighbouring widgets
    // (which would look like two overlapping count displays).
    layout(w);
    hide();
}

FindReplace::~FindReplace() {
    // Cancel any pending async match-count chunk before the object dies.
    Fl::remove_timeout(countChunkCb, this);
    if (m_countText) { free(m_countText); m_countText = nullptr; }
    delete m_highlight;
}

void FindReplace::setEditor(Fl_Text_Editor *e) {
    // The find bar follows the active tab; any in-flight chunked count
    // belongs to the previous tab's buffer and must be discarded, or the
    // old file's match numbers would show up on the new tab.
    if (e != m_editor && (m_countChunkActive || !m_countDone))
        cancelCount();
    m_editor = e;
    updateButtonStates();
}

// Read-only documents (large >100 MB files, and any file the user marked
// read-only) cannot be modified or highlighted, so Find All / Replace /
// Replace All are disabled (greyed out) instead of silently no-opping.
// Next/Prev and Go To keep working - they only read the buffer.
bool FindReplace::editorReadOnly() const {
    if (auto *ed = dynamic_cast<Editor *>(m_editor))
        return ed->doc() && ed->doc()->isReadOnly();
    return false;
}

void FindReplace::updateButtonStates() {
    bool ro = editorReadOnly();
    if (m_btnFindAll) { if (ro) m_btnFindAll->deactivate(); else m_btnFindAll->activate(); }
    if (m_btnReplace) { if (ro) m_btnReplace->deactivate(); else m_btnReplace->activate(); }
    if (m_btnReplaceAll) { if (ro) m_btnReplaceAll->deactivate(); else m_btnReplaceAll->activate(); }
}

void FindReplace::layout(int w) {
    if (!m_matchCount) return;

    const int CHK_PAD = 24;

    fl_font(FL_HELVETICA, 11);

    // Close button: always at the far right
    int closeW = 20;
    m_btnClose->resize(x() + w - closeW - GAP, m_btnClose->y(), closeW, m_btnClose->h());

    // Max available width for left content (excluding close button)
    int maxLeftW = w - closeW - GAP * 2;

    fl_font(FL_HELVETICA, m_fontSize);
    const int labelW = labelOffset(m_fontSize);
    const int replaceInputW = kInputWidth;

    // Button widths: use the SAME fitWidth() rule as every other Pecia
    // button (HoverButton: label + 2*kPadX, clamped to kMinW=48), so the
    // find/prev/all/replace buttons match the toolbar, GoTo and Lua/AI
    // dialogs exactly instead of a local fl_width+BTN_PAD formula.
    int wNext = static_cast<HoverButton *>(m_btnNext)->fitWidth();
    int wPrev = static_cast<HoverButton *>(m_btnPrev)->fitWidth();
    int wFindAll = static_cast<HoverButton *>(m_btnFindAll)->fitWidth();
    int wCase = (int)fl_width(I18n::get("find.matchcase")) + CHK_PAD;
    int wWhole = (int)fl_width(I18n::get("find.wholeword")) + CHK_PAD;
    int wWrap = (int)fl_width(I18n::get("find.wrap")) + CHK_PAD;

    // Line 1: Find input -> Next -> Prev -> All -> Match Case -> Whole Word -> Wrap -> count
    const int findInputW = kInputWidth;
    int cx = x() + GAP;
    m_findInput->resize(cx, m_findInput->y(), findInputW, m_findInput->h());
    m_findLabel->resize(cx + 4, m_findLabel->y(), labelW - 4, m_findLabel->h());
    cx += findInputW + GAP;

    m_btnNext->resize(cx, m_btnNext->y(), wNext, m_btnNext->h());
    cx += wNext + GAP;

    m_btnPrev->resize(cx, m_btnPrev->y(), wPrev, m_btnPrev->h());
    cx += wPrev + GAP;

    m_btnFindAll->resize(cx, m_btnFindAll->y(), wFindAll, m_btnFindAll->h());
    cx += wFindAll + GAP + 6;

    // Show/hide checkboxes based on available width
    int chkStartX = cx;
    int chkTotal = wCase + wWhole + wWrap + GAP * 2;

    if (chkStartX + chkTotal <= maxLeftW - 40) {
        m_chkCase->show();
        m_chkCase->resize(cx, m_chkCase->y(), wCase, m_chkCase->h());
        cx += wCase + GAP;

        m_chkWholeWord->show();
        m_chkWholeWord->resize(cx, m_chkWholeWord->y(), wWhole, m_chkWholeWord->h());
        cx += wWhole + GAP;

        m_chkWrap->show();
        m_chkWrap->resize(cx, m_chkWrap->y(), wWrap, m_chkWrap->h());
        cx += wWrap + GAP;
    } else if (chkStartX + wCase + wWhole + GAP <= maxLeftW - 40) {
        m_chkCase->show();
        m_chkCase->resize(cx, m_chkCase->y(), wCase, m_chkCase->h());
        cx += wCase + GAP;

        m_chkWholeWord->show();
        m_chkWholeWord->resize(cx, m_chkWholeWord->y(), wWhole, m_chkWholeWord->h());
        cx += wWhole + GAP;

        m_chkWrap->hide();
    } else if (chkStartX + wCase + GAP <= maxLeftW - 40) {
        m_chkCase->show();
        m_chkCase->resize(cx, m_chkCase->y(), wCase, m_chkCase->h());
        cx += wCase + GAP;

        m_chkWholeWord->hide();
        m_chkWrap->hide();
    } else {
        m_chkCase->hide();
        m_chkWholeWord->hide();
        m_chkWrap->hide();
    }

    m_countStartX = cx;
    int countRight = m_btnClose->x() - GAP;
    int cw = countRight - cx;
    if (cw < 0) cw = 0;
    m_matchCount->resize(cx, m_matchCount->y(), cw, m_matchCount->h());

    // Line 2: Replace input -> Replace -> All
    cx = x() + GAP;
    m_replaceInput->resize(cx, m_replaceInput->y(), replaceInputW, m_replaceInput->h());
    m_replaceLabel->resize(cx + 4, m_replaceLabel->y(), labelW - 4, m_replaceLabel->h());
    cx += replaceInputW + GAP;

    int wReplace = hbFit(m_btnReplace);
    int wReplaceAll = hbFit(m_btnReplaceAll);

    if (cx + wReplace + wReplaceAll + GAP <= maxLeftW) {
        m_btnReplace->show();
        m_btnReplace->resize(cx, m_btnReplace->y(), wReplace, m_btnReplace->h());
        cx += wReplace + GAP;

        m_btnReplaceAll->show();
        m_btnReplaceAll->resize(cx, m_btnReplaceAll->y(), wReplaceAll, m_btnReplaceAll->h());
    } else if (cx + wReplace <= maxLeftW) {
        m_btnReplace->show();
        m_btnReplace->resize(cx, m_btnReplace->y(), wReplace, m_btnReplace->h());
        m_btnReplaceAll->hide();
    } else {
        m_btnReplace->hide();
        m_btnReplaceAll->hide();
    }

    updateHintVisibility();
}

void FindReplace::activate(const char *initialText, bool focusReplace) {
    show();
    clearMatchHighlight();
    if (initialText && *initialText) {
        m_findInput->value(initialText);
    }
    if (focusReplace) {
        Fl::focus(m_replaceInput);
        m_replaceInput->insert_position(0, m_replaceInput->size());
        if (m_replaceLabel) {
            if (initialText && *initialText) m_replaceLabel->hide();
            else m_replaceLabel->show();
        }
        // Hide the find hint when we're focused on replace
        if (m_findLabel) m_findLabel->hide();
    } else {
        Fl::focus(m_findInput);
        m_findInput->insert_position(0, m_findInput->size());
        if (m_findLabel) {
            if (initialText && *initialText) m_findLabel->hide();
            else m_findLabel->show();
        }
    }
    // Reset match count state
    m_totalCount = 0;
    m_currentIndex = 0;
    m_lastPos = -1;
    updateButtonStates();
    // Run a layout pass so the count display reaches its final width
    // before the first redraw.
    layout(w());
    updateMatchCount();
}

void FindReplace::refreshLabels() {
    // Refresh button/checkbox labels after a language change.
    // Also resize them based on the new label width.
    fl_font(FL_HELVETICA, 11);
    const int CHK_PAD = 24;

    int y1 = m_btnPrev->y();
    int y2 = m_btnReplace->y();

    // Line 1: Next, Prev, All, Match Case, Whole Word, Wrap
    int cx = m_findInput->x() + m_findInput->w() + GAP;

    const char* lblNext = I18n::get("find.next");
    m_btnNext->label(lblNext);
    int wNext = hbFit(m_btnNext);
    m_btnNext->resize(cx, y1, wNext, m_btnNext->h());
    cx += wNext + GAP;

    const char* lblPrev = I18n::get("find.prev");
    m_btnPrev->label(lblPrev);
    int wPrev = hbFit(m_btnPrev);
    m_btnPrev->resize(cx, y1, wPrev, m_btnPrev->h());
    cx += wPrev + GAP;

    const char* lblFindAll = I18n::get("find.all");
    m_btnFindAll->label(lblFindAll);
    int wFindAll = hbFit(m_btnFindAll);
    m_btnFindAll->resize(cx, y1, wFindAll, m_btnFindAll->h());
    cx += wFindAll + GAP + 6;

    const char* lblCase = I18n::get("find.matchcase");
    int wCase = (int)fl_width(lblCase) + CHK_PAD;
    m_chkCase->label(lblCase);
    m_chkCase->resize(cx, y1, wCase, m_chkCase->h());
    cx += wCase + GAP;

    const char* lblWhole = I18n::get("find.wholeword");
    int wWhole = (int)fl_width(lblWhole) + CHK_PAD;
    m_chkWholeWord->label(lblWhole);
    m_chkWholeWord->resize(cx, y1, wWhole, m_chkWholeWord->h());
    cx += wWhole + GAP;

    const char* lblWrap = I18n::get("find.wrap");
    int wWrap = (int)fl_width(lblWrap) + CHK_PAD;
    m_chkWrap->label(lblWrap);
    m_chkWrap->resize(cx, y1, wWrap, m_chkWrap->h());
    cx += wWrap + GAP;

    // Update the match-count start X so layout() can recompute its width.
    m_countStartX = cx;

    // Line 2: Replace, All
    cx = m_replaceInput->x() + m_replaceInput->w() + GAP;

    const char* lblReplace = I18n::get("find.replace");
    m_btnReplace->label(lblReplace);
    int wReplace = hbFit(m_btnReplace);
    m_btnReplace->resize(cx, y2, wReplace, m_btnReplace->h());
    cx += wReplace + GAP;

    const char* lblReplaceAll = I18n::get("find.replaceall");
    m_btnReplaceAll->label(lblReplaceAll);
    int wReplaceAll = hbFit(m_btnReplaceAll);
    m_btnReplaceAll->resize(cx, y2, wReplaceAll, m_btnReplaceAll->h());

    // Update the hint labels inside the input boxes.
    m_findLabel->label(I18n::get("find.findhint"));
    m_replaceLabel->label(I18n::get("find.replacehint"));

    // Re-layout to fix match-count width and redraw.
    layout(w());
    redraw();
}

int FindReplace::handle(int event) {
    // Let FLTK process the event first so focus changes take effect.
    int ret = Fl_Group::handle(event);
    // After focus/unfocus events, update the hint label visibility.
    // Also update when the user types into the input (FL_KEYBOARD).
    if (event == FL_FOCUS || event == FL_UNFOCUS || event == FL_KEYUP) {
        updateHintVisibility();
    }
    return ret;
}

// Show/hide one placeholder-hint label depending on whether its input is
// empty and not focused.
static void updateHintLabel(Fl_Widget *label, Fl_Input *input) {
    if (!label || !input) return;
    const char *v = input->value();
    if (!v || !*v)
        (Fl::focus() == input) ? label->hide() : label->show();
    else
        label->hide();
}

void FindReplace::updateHintVisibility() {
    updateHintLabel(m_findLabel, m_findInput);
    updateHintLabel(m_replaceLabel, m_replaceInput);
}

bool FindReplace::caseSensitive() const {
    return m_chkCase->value() != 0;
}

bool FindReplace::wholeWord() const {
    return m_chkWholeWord->value() != 0;
}

// Whole-word boundary check shared by forward/backward searches: the
// bytes immediately before and after the match must not be word chars.
bool FindReplace::wholeWordOk(Fl_Text_Buffer *buf, int found, int needleLen) {
    const char *t = buf->text();
    bool ok = ::wholeWordOk(t, buf->length(), found, needleLen);
    free((void *)t);
    return ok;
}

int FindReplace::searchForward(Fl_Text_Buffer *buf, int start,
                               const char *needle, int needleLen,
                               int matchCase, int *foundPos) {
    int pos = start;
    int found;
    while (buf->search_forward(pos, needle, &found, matchCase) != 0) {
        if (!wholeWord()) {
            *foundPos = found;
            return 1;
        }
        if (wholeWordOk(buf, found, needleLen)) {
            *foundPos = found;
            return 1;
        }
        pos = found + needleLen;
        if (pos >= buf->length()) break;
    }
    return 0;
}

int FindReplace::searchBackward(Fl_Text_Buffer *buf, int start,
                                const char *needle, int needleLen,
                                int matchCase, int *foundPos) {
    int pos = start;
    int found;
    while (buf->search_backward(pos, needle, &found, matchCase) != 0) {
        if (!wholeWord()) {
            *foundPos = found;
            return 1;
        }
        if (wholeWordOk(buf, found, needleLen)) {
            *foundPos = found;
            return 1;
        }
        if (found == 0) break;
        pos = found - 1;
    }
    return 0;
}

int FindReplace::countMatches() {
    if (!m_editor) return 0;
    Fl_Text_Buffer *buf = m_editor->buffer();
    const char *needle = m_findInput->value();
    if (!needle || !*needle) return 0;

    const int matchCase = caseSensitive() ? 1 : 0;
    const int needleLen = (int)strlen(needle);

    int pos = 0;
    int found = 0;
    int count = 0;

    while (searchForward(buf, pos, needle, needleLen, matchCase, &found) != 0) {
        ++count;
        pos = found + needleLen;
        if (pos >= buf->length()) break;
    }
    return count;
}

void FindReplace::updateMatchCount() {
    // Use pre-computed values from findNext/findPrev
    // If no search has been performed yet, count total matches (only for
    // regular-sized buffers - counting a >100 MB file freezes the UI).
    if (m_totalCount == 0 && m_editor && m_editor->buffer() &&
        m_editor->buffer()->length() <= kLargeFindHighlightLimitBytes) {
        m_totalCount = countMatches();
    }
    char buf[32];
    bool big = m_editor && m_editor->buffer() &&
               m_editor->buffer()->length() > kLargeFindHighlightLimitBytes;
    if (big) {
        // Large buffers (>100 MB): the count runs once per needle
        // asynchronously; show "index/total" when both are known.
        if (m_totalCount < 0)
            snprintf(buf, sizeof(buf), ".../...");
        else if (!m_countIdxKnown)
            snprintf(buf, sizeof(buf), "?/%d", m_totalCount);
        else
            snprintf(buf, sizeof(buf), "%d/%d", m_currentIndex, m_totalCount);
    } else if (m_totalCount < 0) {
        snprintf(buf, sizeof(buf), "%d/-", m_currentIndex);
    } else {
        snprintf(buf, sizeof(buf), "%d/%d", m_currentIndex, m_totalCount);
    }
    m_matchCount->value(buf);
}

// Large buffers (>100 MB, always read-only so the text cannot change):
// count all matches asynchronously in bounded chunks driven by timeouts.
// Each tick scans up to 128 MB, so the UI stays responsive. The count
// runs ONCE per needle - later Next/Prev presses do not re-scan.
void FindReplace::startAsyncCount() {
    if (!m_editor || !m_editor->buffer()) return;
    Fl_Text_Buffer *buf = m_editor->buffer();
    if (buf->length() <= kLargeFindHighlightLimitBytes) return;
    const char *needle = m_findInput->value();
    if (!needle) needle = "";
    // Already counted (or counting) this needle? Nothing to do.
    if ((m_countDone || m_countChunkActive) && m_countNeedle == needle &&
        m_countOwner == buf) return;
    m_countNeedle = needle;
    m_countOwner = buf;
    m_countChunkPos = 0;
    m_countChunkTotal = 0;
    m_countChunkActive = true;
    m_countDone = false;
    m_countIdxKnown = false;
    m_countIndexOnly = false;
    m_currentIndex = 0;
    m_totalCount = -1;
    if (m_countText) { free(m_countText); m_countText = nullptr; }
    m_matchCount->value(".../...");
    Fl::add_timeout(0.0, countChunkCb, this);
}

// Abort an in-flight chunked count. Used when the tab (buffer) changes
// mid-scan: the count callback must not feed a stale snapshot of the old
// file into the newly selected tab's match display.
void FindReplace::cancelCount() {
    Fl::remove_timeout(countChunkCb, this);
    m_countChunkActive = false;
    m_countChunkPos = 0;
    m_countChunkTotal = 0;
    if (m_countText) { free(m_countText); m_countText = nullptr; }
    m_countTextLen = 0;
    m_totalCount = -1;
    m_currentIndex = 0;
    m_countIdxKnown = false;
    m_countIndexOnly = false;
    m_countDone = false;
    m_countNeedle.clear();
    m_countOwner = nullptr;
    updateMatchCount();
}

// The cursor jumped to an arbitrary position (scroll + click, etc.), so
// the current match's sequence number cannot be derived incrementally.
// Scan from the start in chunks, stopping as soon as the index of the
// current match (m_lastPos) is captured. The total count (if already
// known) is preserved.
void FindReplace::startIndexScan() {
    if (!m_editor || !m_editor->buffer()) return;
    Fl_Text_Buffer *buf = m_editor->buffer();
    if (buf->length() <= kLargeFindHighlightLimitBytes) return;
    const char *needle = m_findInput->value();
    if (!needle) needle = "";
    if (m_countChunkActive && m_countIndexOnly && m_countNeedle == needle) return;
    m_countNeedle = needle;
    m_countOwner = buf;
    m_countChunkPos = 0;
    m_countChunkTotal = 0;
    m_countChunkActive = true;
    m_countIdxKnown = false;
    m_countIndexOnly = true;
    m_currentIndex = 0;
    if (m_countText) { free(m_countText); m_countText = nullptr; }
    m_matchCount->value(".../...");
    Fl::add_timeout(0.0, countChunkCb, this);
}

void FindReplace::countChunkCb(void *data) {
    FindReplace *self = static_cast<FindReplace *>(data);
    if (!self) return;
    self->m_countChunkActive = false;   // re-armed below if not done
    Fl_Text_Buffer *buf = self->m_editor ? self->m_editor->buffer() : nullptr;
    if (!buf) return;

    // The user switched tabs mid-count: this snapshot belongs to the
    // previous tab's buffer. Discard it so the old file's match numbers
    // never appear on the new tab.
    if (buf != self->m_countOwner) {
        self->cancelCount();
        return;
    }

    const char *needle = self->m_findInput->value();
    if (!needle) needle = "";
    if (self->m_countNeedle != needle) {
        // Needle changed mid-count: restart from scratch (full count).
        self->m_countNeedle = needle;
        self->m_countChunkPos = 0;
        self->m_countChunkTotal = 0;
        self->m_countDone = false;
        self->m_countIdxKnown = false;
        self->m_countIndexOnly = false;
        self->m_currentIndex = 0;
        if (self->m_countText) { free(self->m_countText); self->m_countText = nullptr; }
        if (!*needle) { self->m_totalCount = 0; self->updateMatchCount(); return; }
    }

    // Snapshot the text once per count (large files are read-only, so the
    // copy cannot go stale). The raw scan is much faster than repeated
    // search_forward() calls on dense needles.
    if (!self->m_countText) {
        self->m_countText = buf->text();
        self->m_countTextLen = buf->length();
    }
    const int len = self->m_countTextLen;
    const int needleLen = (int)strlen(needle);
    if (needleLen == 0 || self->m_countChunkPos >= len) {
        if (!self->m_countIndexOnly) {
            self->m_totalCount = (int)self->m_countChunkTotal;
            self->m_countDone = true;
        }
        self->m_countIndexOnly = false;
        self->m_countChunkActive = false;
        self->updateMatchCount();
        return;
    }

    // Count matches within the next ~128 MB chunk (bounded work per tick).
    int start = self->m_countChunkPos;
    int end = start + 128 * 1024 * 1024;
    if (end > len) end = len;
    bool ascii = true;
    for (int k = 0; k < needleLen; ++k)
        if ((unsigned char)needle[k] & 0x80) { ascii = false; break; }
    if (ascii) {
        countRawRange(self->m_countText, len, start, end, needle, needleLen,
                      self->caseSensitive(), self->wholeWord(), self->m_lastPos,
                      self->m_countChunkTotal, self->m_currentIndex);
        if (!self->m_countIdxKnown && self->m_currentIndex != 0)
            self->m_countIdxKnown = true;
        if (self->m_countIndexOnly && self->m_countIdxKnown) end = len; // stop now
    } else {
        // Non-ASCII needle: fall back to the searchForward-based loop.
        const int matchCase = self->caseSensitive() ? 1 : 0;
        int pos = start;
        int found = 0;
        int matchesThisTick = 0;
        while (pos < end && matchesThisTick < 200000) {
            if (self->searchForward(buf, pos, needle, needleLen, matchCase, &found) == 0) {
                pos = len;
                break;
            }
            ++self->m_countChunkTotal;
            if (!self->m_countIdxKnown && found == self->m_lastPos) {
                self->m_countIdxKnown = true;
                self->m_currentIndex = (int)self->m_countChunkTotal;
            }
            ++matchesThisTick;
            pos = found + needleLen;
            if (self->m_countIndexOnly && self->m_countIdxKnown) { pos = len; break; }
        }
        self->m_countChunkPos = pos;
        end = pos;
    }
    self->m_countChunkPos = end;

    if (end >= len || (self->m_countIndexOnly && self->m_countIdxKnown)) {
        if (!self->m_countIndexOnly) {
            self->m_totalCount = (int)self->m_countChunkTotal;
            self->m_countDone = true;
        }
        self->m_countIndexOnly = false;
        self->m_countChunkActive = false;
        if (self->m_countText) { free(self->m_countText); self->m_countText = nullptr; }
        self->updateMatchCount();
        return;
    }
    // Show progress: "idx/..." once the current match's index is known.
    {
        char run[32];
        if (self->m_countIdxKnown)
            snprintf(run, sizeof(run), "%d/...", self->m_currentIndex);
        else
            snprintf(run, sizeof(run), ".../...");
        self->m_matchCount->value(run);
    }
    self->m_countChunkActive = true;
    Fl::add_timeout(0.0, countChunkCb, self);
}

void FindReplace::highlightAllMatches() {
    if (!m_editor) return;
    Fl_Text_Buffer *buf = m_editor->buffer();
    int len = buf->length();
    if (len <= 0) return;

    // Large files: no highlight AND no counting (both require full-file
    // work: an equal-size style buffer, or a full scan per match - either
    // freezes the UI for seconds on hundreds of MB). Find Next/Prev still
    // work normally. The Find All button is disabled for read-only docs
    // (see updateButtonStates), but guard here too so nothing touches the
    // match-count state even if this is reached another way.
    if (len > kLargeFindHighlightLimitBytes) {
        return;
    }

    // Delegate the whole match scan to the MatchHighlight manager; the
    // Editor paints the ranges in its own unified style buffer.
    const char *needle = m_findInput->value();
    const int matchCase = caseSensitive() ? 1 : 0;
    const int needleLen = needle ? (int)strlen(needle) : 0;
    Editor *ed = dynamic_cast<Editor *>(m_editor);
    if (ed && m_highlight->highlightAll(ed, needle, needleLen,
                                        matchCase != 0)) {
        m_highlight->setActive(true);
        if (m_onHighlightChange) m_onHighlightChange(true);
    }
}

void FindReplace::clearMatchHighlight() {
    if (!m_editor) return;
    if (auto *ed = dynamic_cast<Editor *>(m_editor))
        m_highlight->clear(ed);
    m_highlight->setActive(false);
    if (m_onHighlightChange) m_onHighlightChange(false);
}

void FindReplace::findNext() {
    if (!m_editor) return;
    clearMatchHighlight();
    Fl_Text_Buffer *buf = m_editor->buffer();
    if (!buf) return;   // editor detached (e.g. tab is in mid-open)
    const char *needle = m_findInput->value();
    if (!needle || !*needle) return;

    int start = m_editor->insert_position();
    int found = -1;
    const int matchCase = caseSensitive() ? 1 : 0;
    int matchLen = (int)strlen(needle);
    bool big = buf->length() > kLargeFindHighlightLimitBytes;
    bool ascii = true;
    for (int k = 0; k < matchLen; ++k)
        if ((unsigned char)needle[k] & 0x80) { ascii = false; break; }

    if (big && ascii) {
        // Fast memchr-based search (FLTK's search is a naive per-byte loop
        // that takes seconds on a hundreds-of-MB file).
        char *raw = buf->text();
        found = rawSearchForward(raw, buf->length(), start, needle, matchLen,
                                 caseSensitive(), wholeWord());
        if (found < 0 && m_chkWrap->value())
            found = rawSearchForward(raw, buf->length(), 0, needle, matchLen,
                                     caseSensitive(), wholeWord());
        free(raw);
    } else {
        if (searchForward(buf, start, needle, matchLen, matchCase, &found) == 0) {
            if (m_chkWrap->value())
                searchForward(buf, 0, needle, matchLen, matchCase, &found);
        }
    }
    if (found < 0) {
        fl_beep();
        m_lastPos = -1;
        if (big) { startAsyncCount(); updateMatchCount(); return; }
        m_totalCount = 0;
        m_currentIndex = 0;
        updateMatchCount();
        return;
    }
    buf->select(found, found + matchLen);
    m_editor->insert_position(found + matchLen);
    m_editor->show_insert_position();
    int oldLast = m_lastPos;
    m_lastPos = found;

    // Large buffers: the index is only incrementable when this search
    // continued directly from the previous match - the found match must
    // actually lie AFTER it. A wrap-around (found before the old match)
    // resets to the first match instead of incrementing.
    updateMatchIndex(buf, needle, matchLen, matchCase, found, oldLast, start, true);

    // Notify the editor that the cursor moved so it can update
    // the current-line highlight (highlight1) for the new position.
    if (auto *ed = dynamic_cast<Editor *>(m_editor))
        ed->notifyCursorMoved();
}

// Shared tail of findNext/findPrev: on large (read-only) buffers update
// the match index incrementally via the SearchCore step helpers; on
// regular buffers recompute index and total by scanning from the start.
// `forward` selects nextIndexStep(+WrapFirst) vs prevIndexStep(+WrapLast).
void FindReplace::updateMatchIndex(Fl_Text_Buffer *buf, const char *needle,
                                   int matchLen, int matchCase,
                                   int found, int oldLast, int start,
                                   bool forward) {
    if (buf->length() > kLargeFindHighlightLimitBytes) {
        if (m_countNeedle != needle || !m_countDone) {
            startAsyncCount();
        } else {
            IndexStep step = forward
                ? nextIndexStep(oldLast, found, start, matchLen,
                                m_countIdxKnown, m_currentIndex, m_totalCount)
                : prevIndexStep(oldLast, found, start,
                                m_countIdxKnown, m_currentIndex, m_totalCount);
            switch (step) {
            case IndexStep::Advance:
            case IndexStep::WrapFirst:
            case IndexStep::WrapLast:
                break;   // index updated in place
            case IndexStep::Rescan:
            default:
                startIndexScan();
                break;
            }
        }
        updateMatchCount();   // refresh the "idx/N" display
        return;
    }

    // Compute current index and total count
    m_totalCount = countMatches();
    if (m_totalCount > 0) {
        int pos = 0;
        int idx = 0;
        int searchFound = 0;
        while (searchForward(buf, pos, needle, matchLen, matchCase, &searchFound) != 0) {
            ++idx;
            if (searchFound == found) {
                m_currentIndex = idx;
                break;
            }
            pos = searchFound + matchLen;
            if (pos >= buf->length()) break;
        }
    } else {
        m_currentIndex = 0;
    }
    updateMatchCount();
}

void FindReplace::findPrev() {
    if (!m_editor) return;
    clearMatchHighlight();
    Fl_Text_Buffer *buf = m_editor->buffer();
    if (!buf) return;   // editor detached (e.g. tab is in mid-open)
    const char *needle = m_findInput->value();
    if (!needle || !*needle) return;

    int start = m_editor->insert_position();
    if (start > 0) --start;
    int found = -1;
    const int matchCase = caseSensitive() ? 1 : 0;
    int matchLen = (int)strlen(needle);
    bool big = buf->length() > kLargeFindHighlightLimitBytes;
    bool ascii = true;
    for (int k = 0; k < matchLen; ++k)
        if ((unsigned char)needle[k] & 0x80) { ascii = false; break; }

    if (big && ascii) {
        // Fast memchr-based backward search (single pass, last match before
        // the cursor).
        char *raw = buf->text();
        found = rawSearchBackward(raw, buf->length(), start + 1, needle, matchLen,
                                  caseSensitive(), wholeWord());
        if (found < 0 && m_chkWrap->value())
            found = rawSearchBackward(raw, buf->length(), buf->length(), needle, matchLen,
                                      caseSensitive(), wholeWord());
        free(raw);
    } else {
        if (searchBackward(buf, start, needle, matchLen, matchCase, &found) == 0) {
            if (m_chkWrap->value())
                searchBackward(buf, buf->length(), needle, matchLen, matchCase, &found);
        }
    }
    if (found < 0) {
        fl_beep();
        m_lastPos = -1;
        if (big) { startAsyncCount(); updateMatchCount(); return; }
        m_totalCount = 0;
        m_currentIndex = 0;
        updateMatchCount();
        return;
    }
    buf->select(found, found + matchLen);
    m_editor->insert_position(found);
    m_editor->show_insert_position();
    int oldLast = m_lastPos;
    m_lastPos = found;

    // Large buffers: same jump-detection as findNext - the index is only
    // adjusted when this search continued from the previous match (the
    // found match must lie BEFORE it; a backward wrap resets to the last).
    updateMatchIndex(buf, needle, matchLen, matchCase, found, oldLast, start, false);

    // Notify the editor that the cursor moved so it can update
    // the current-line highlight (highlight1) for the new position.
    if (auto *ed = dynamic_cast<Editor *>(m_editor))
        ed->notifyCursorMoved();
}

void FindReplace::replaceOne() {
    if (!m_editor) return;
    // Read-only documents (e.g. >100 MB large files) reject replacements.
    if (editorReadOnly()) return;
    Fl_Text_Buffer *buf = m_editor->buffer();
    const char *repl = m_replaceInput->value();
    if (!repl) repl = "";

    int start, end;
    if (!buf->selection_position(&start, &end)) {
        findNext();
        if (!buf->selection_position(&start, &end)) return;
    }
    // One undo step per replace click; also prevents merging with the
    // surrounding typed text (position-contiguous merge would otherwise
    // swallow consecutive replaces into one event).
    buf->undo_begin();
    buf->replace_selection(repl);
    buf->undo_end();
    m_editor->insert_position(start + (int)strlen(repl));
    m_editor->show_insert_position();
    findNext();
}

void FindReplace::replaceAll() {
    if (!m_editor) return;
    // Read-only documents (e.g. >100 MB large files) reject replacements.
    if (editorReadOnly()) return;
    clearMatchHighlight();
    Fl_Text_Buffer *buf = m_editor->buffer();
    const char *needle = m_findInput->value();
    const char *repl = m_replaceInput->value();
    if (!needle || !*needle) return;
    if (!repl) repl = "";

    const int matchCase = caseSensitive() ? 1 : 0;
    const bool whole = wholeWord();
    const int needleLen = (int)strlen(needle);
    const int replLen   = (int)strlen(repl);

    // Work on a plain copy; the document buffer is touched exactly once
    // (single undo transaction + single modify callback, so undo/redo and
    // UI refresh stay O(1)). replaceAllForward keeps the same semantics
    // as the old per-replace loop (ASCII case folding + whole-word via
    // SearchCore) and handles NUL bytes in the document.
    char *full = buf->text();
    std::string text(full, buf->length());
    ::free(full);

    int count = replaceAllForward(text, needle, needleLen, repl, replLen,
                                  matchCase != 0, whole);

    if (count > 0) {
        buf->undo_begin();
        buf->replace(0, buf->length(), text.c_str());
        buf->undo_end();
    }
    m_editor->redraw();
    m_totalCount = 0;
    m_currentIndex = 0;
    m_lastPos = -1;
    updateMatchCount();
}

void FindReplace::cbFindNext(Fl_Widget * /*w*/, void *data) {
    FindReplace *self = static_cast<FindReplace *>(data);
    if (self) self->findNext();
}

void FindReplace::cbFindPrev(Fl_Widget * /*w*/, void *data) {
    FindReplace *self = static_cast<FindReplace *>(data);
    if (self) self->findPrev();
}

void FindReplace::cbFindAll(Fl_Widget * /*w*/, void *data) {
    FindReplace *self = static_cast<FindReplace *>(data);
    if (!self || !self->m_editor) return;
    if (self->m_highlight->active()) {
        self->clearMatchHighlight();
        return;
    }

    const char *needle = self->m_findInput->value();
    if (!needle || !*needle) return;

    // Highlight all matches in the editor.
    self->highlightAllMatches();
}

void FindReplace::cbReplace(Fl_Widget * /*w*/, void *data) {
    FindReplace *self = static_cast<FindReplace *>(data);
    if (self) self->replaceOne();
}

void FindReplace::cbReplaceAll(Fl_Widget * /*w*/, void *data) {
    FindReplace *self = static_cast<FindReplace *>(data);
    if (self) self->replaceAll();
}

void FindReplace::cbClose(Fl_Widget * /*w*/, void *data) {
    FindReplace *self = static_cast<FindReplace *>(data);
    if (!self) return;
    self->clearMatchHighlight();
    self->hide();
    if (self->m_onClose) self->m_onClose();
    if (self->m_editor) Fl::focus(self->m_editor);
}

void FindReplace::setTheme(const Theme *theme) {
    m_theme = theme;
    if (m_theme) {
        color(m_theme->colors().background3);
        Fl_Color fg = m_theme->colors().text1;
        Fl_Color bg = m_theme->colors().background3;
        for (int i = 0; i < children(); ++i) {
            Fl_Widget *ch = child(i);
            ch->labelcolor(fg);
            ch->labelsize(m_fontSize);
            if (auto *btn = dynamic_cast<Fl_Button *>(ch)) {
                btn->color(bg);
                btn->labelcolor(fg);
                btn->selection_color(m_theme->colors().highlight1);
                btn->labelsize(m_fontSize);
                if (auto *hb = dynamic_cast<HoverButton *>(btn))
                    hb->setPressColor(m_theme->colors().highlight2);
            }
            if (auto *cb = dynamic_cast<Fl_Check_Button *>(ch)) {
                cb->box(FL_NO_BOX);       // no outer border on the text area
                cb->down_box(FL_BORDER_BOX);  // square checkbox glyph
                cb->selection_color(m_theme->colors().highlight2);  // checked fill
                cb->labelcolor(fg);
                cb->labelsize(m_fontSize);
            }
        }
        if (m_findLabel) m_findLabel->labelcolor(m_theme->colors().text2);
        if (m_replaceLabel) m_replaceLabel->labelcolor(m_theme->colors().text2);
        if (m_matchCount) {
            m_matchCount->color(bg);
            m_matchCount->textcolor(m_theme->colors().text1);
            m_matchCount->labelcolor(m_theme->colors().text1);
        }
        if (m_findInput) { m_findInput->color(m_theme->colors().background1); m_findInput->box(FL_BORDER_BOX); m_findInput->cursor_color(m_theme->colors().text1); m_findInput->selection_color(m_theme->colors().highlight2); }
        if (m_replaceInput) { m_replaceInput->color(m_theme->colors().background1); m_replaceInput->box(FL_BORDER_BOX); m_replaceInput->cursor_color(m_theme->colors().text1); m_replaceInput->selection_color(m_theme->colors().highlight2); }
    }
    redraw();
}

void FindReplace::setFontSize(int sz) {
    m_fontSize = sz;
    for (int i = 0; i < children(); ++i) {
        Fl_Widget *ch = child(i);
        ch->labelsize(sz);
        if (auto *btn = dynamic_cast<Fl_Button *>(ch)) btn->labelsize(sz);
        if (auto *cb = dynamic_cast<Fl_Check_Button *>(ch)) cb->labelsize(sz);
        if (auto *ip = dynamic_cast<Fl_Input *>(ch)) ip->textsize(sz);
    }
    if (m_findInput) m_findInput->textsize(sz);
    if (m_replaceInput) m_replaceInput->textsize(sz);
    if (m_matchCount) m_matchCount->textsize(sz);

    // Direct resize: recalculate widths at current font size
    fl_font(FL_HELVETICA, m_fontSize);
    const int CHK_PAD = m_fontSize + 14;
    int cx = x() + GAP;

    int fiw = m_btnClose->x() - GAP - cx;  // remaining width for everything
    if (fiw < 100) fiw = 100;

    m_findInput->resize(cx, m_findInput->y(), 120, m_findInput->h());
    cx += m_findInput->w() + GAP;

    // Next, Prev, All
    int wNext = hbFit(m_btnNext);
    int wPrev = hbFit(m_btnPrev);
    int wFindAll = hbFit(m_btnFindAll);
    m_btnNext->resize(cx, m_btnNext->y(), wNext, m_btnNext->h()); cx += wNext + GAP;
    m_btnPrev->resize(cx, m_btnPrev->y(), wPrev, m_btnPrev->h()); cx += wPrev + GAP;
    m_btnFindAll->resize(cx, m_btnFindAll->y(), wFindAll, m_btnFindAll->h()); cx += wFindAll + GAP;

    // Checkboxes (Match Case, Whole Word, Wrap)
    int wCase = (int)fl_width(I18n::get("find.matchcase")) + CHK_PAD;
    int wWhole = (int)fl_width(I18n::get("find.wholeword")) + CHK_PAD;
    int wWrap = (int)fl_width(I18n::get("find.wrap")) + CHK_PAD;
    int leftForChk = m_btnClose->x() - GAP - cx;
    if (wCase + wWhole + wWrap < leftForChk) {
        m_chkCase->show();    m_chkCase->resize(cx, m_chkCase->y(), wCase, m_chkCase->h());   cx += wCase + 2;
        m_chkWholeWord->show(); m_chkWholeWord->resize(cx, m_chkWholeWord->y(), wWhole, m_chkWholeWord->h()); cx += wWhole + 2;
        m_chkWrap->show();    m_chkWrap->resize(cx, m_chkWrap->y(), wWrap, m_chkWrap->h());   cx += wWrap;
    } else if (wCase + wWhole < leftForChk) {
        m_chkCase->show();    m_chkCase->resize(cx, m_chkCase->y(), wCase, m_chkCase->h());   cx += wCase + 2;
        m_chkWholeWord->show(); m_chkWholeWord->resize(cx, m_chkWholeWord->y(), wWhole, m_chkWholeWord->h()); cx += wWhole + 2;
        m_chkWrap->hide();
    } else if (wCase < leftForChk) {
        m_chkCase->show();    m_chkCase->resize(cx, m_chkCase->y(), wCase, m_chkCase->h());   cx += wCase;
        m_chkWholeWord->hide(); m_chkWrap->hide();
    } else {
        m_chkCase->hide(); m_chkWholeWord->hide(); m_chkWrap->hide();
    }

    // Line 2: Replace row
    cx = x() + GAP;
    m_replaceInput->resize(cx, m_replaceInput->y(), 120, m_replaceInput->h());
    cx += m_replaceInput->w() + GAP;
    int wReplace = hbFit(m_btnReplace);
    int wReplaceAll = hbFit(m_btnReplaceAll);
    m_btnReplace->resize(cx, m_btnReplace->y(), wReplace, m_btnReplace->h());   cx += wReplace + GAP;
    m_btnReplaceAll->resize(cx, m_btnReplaceAll->y(), wReplaceAll, m_btnReplaceAll->h()); cx += wReplaceAll;

    // Match count
    m_matchCount->resize(cx, m_matchCount->y(), m_btnClose->x() - GAP - cx, m_matchCount->h());

    redraw();
}

