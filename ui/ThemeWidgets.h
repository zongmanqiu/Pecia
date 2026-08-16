// ThemeWidgets.h - Themed widget classes for the standalone tools.
// Mirrors the main window's widget styling: flat editors/displays,
// 10px theme-colored scrollbars drawn the same way as the main
// editor's custom scrollbars (Editor::drawCustomScrollbar), bordered
// inputs and the accent selection color on buttons.
#pragma once

#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Scrollbar.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input.H>
#include <FL/fl_draw.H>
#include <initializer_list>
#include <string>

#include "core/Theme.h"

#if defined(_WIN32)
#include "ui/ImeInPlace.h"
#endif

namespace {

// Apply the main window's scrollbar style (10px, no arrows, flat,
// theme thumb/track) to one scrollbar.
inline void styleToolScrollbar(Fl_Scrollbar *sb, uchar type,
                               const ThemeColors &tc) {
    if (!sb) return;
    sb->type(type);
    sb->box(FL_FLAT_BOX);
    sb->color(tc.scrollbarThumb);
    sb->selection_color(tc.scrollbarTrack);
}

// Draw one scrollbar exactly like the main editor does
// (Editor::drawCustomScrollbar): plain track + thumb rectangles in
// the theme colors, no FLTK native grooves/slider details.
inline void drawToolScrollbar(Fl_Scrollbar *sb, bool horizontal,
                              const ThemeColors &tc) {
    if (!sb || !sb->visible_r()) return;

    int sx = sb->x();
    int sy = sb->y();
    int sw = sb->w();
    int sh = sb->h();

    double minv = sb->minimum();
    double maxv = sb->maximum();
    double val  = sb->value();
    float  sl   = sb->slider_size();

    fl_color(tc.scrollbarTrack);
    fl_rectf(sx, sy, sw, sh);

    if (horizontal) {
        int tw = (int)(sw * sl);
        if (tw < 20) tw = 20;
        double range = maxv - minv;
        int tx = sx;
        if (range > 0.0)
            tx = sx + (int)((val - minv) * (sw - tw) / range);
        fl_color(tc.scrollbarThumb);
        fl_rectf(tx, sy, tw, sh);
    } else {
        int th = (int)(sh * sl);
        if (th < 20) th = 20;
        double range = maxv - minv;
        int ty = sy;
        if (range > 0.0)
            ty = sy + (int)((val - minv) * (sh - th) / range);
        fl_color(tc.scrollbarThumb);
        fl_rectf(sx, ty, sw, th);
    }
}

} // namespace

// Flat button that fills with selection_color() on hover / while pressed,
// mirroring the menu bar's item highlight (HoverMenuBar) and the title
// bar buttons. Callers style it like any Fl_Button: set color() for the
// resting background, selection_color() for the hover/pressed fill, and
// labelcolor() for the text. A 1px border (FL_BORDER_BOX) keeps the
// button look distinct from plain chrome background.
class HoverButton : public Fl_Button {
public:
    // Horizontal padding on each side of the label (total = 2x). Used by
    // fit() so every auto-fitted button shares the exact same width rule.
    static constexpr int kPadX = 12;
    // Minimum auto-fitted width, so a short label still yields a tappable
    // button (all windows agree on this one value).
    static constexpr int kMinW = 48;

    HoverButton(int X, int Y, int W, int H, const char *l = nullptr)
        : Fl_Button(X, Y, W, H, l) {
        box(FL_BORDER_BOX);
    }

    // Width that fits the current label (text width + 2*padX, clamped to
    // kMinW). Works with any label/font already set on this button.
    int fitWidth() const {
        Fl_Button *me = const_cast<HoverButton *>(this);
        int tw = 0, th = 0;
        me->measure_label(tw, th);
        int w = tw + 2 * kPadX;
        if (w < kMinW) w = kMinW;
        return w;
    }

    // Resize to fit the label (height unchanged). Convenient for buttons
    // whose final x is computed by the caller (right-aligned rows).
    void fit() {
        resize(x(), y(), fitWidth(), h());
    }

    void draw() FL_OVERRIDE {
        Fl_Color c = color();
        if (Fl::belowmouse() == this || Fl::pushed() == this) {
            c = selection_color();
            // Distinct press feedback: while actually held down, darken the
            // fill (fl_darker = 67% base + 33% black) so a click reads as a
            // deeper press than a plain hover.
            if (Fl::pushed() == this)
                c = fl_darker(c);
        }
        draw_box(box(), c);
        draw_label();
        if (Fl::focus() == this) draw_focus();
    }

    int handle(int event) FL_OVERRIDE {
        // Redraw on enter/leave so the hover fill tracks the mouse
        // immediately (Fl_Button only repaints on press by default).
        if (event == FL_ENTER || event == FL_LEAVE) redraw();
        return Fl_Button::handle(event);
    }
};

// Layout a right-aligned row of buttons across [barX, rightEdge] (the
// chrome bottom button bar of a dialog). Each button is first sized to its
// label (HoverButton::fit), then placed right-to-left with `gap` between
// them, ending `margin` from rightEdge, vertically centered at `barCenterY`.
// Accepts Fl_Button* so callers can pass members typed as the base class;
// every button must actually be a HoverButton (they all are in Pecia).
inline void fitButtonRow(std::initializer_list<Fl_Button *> btns,
                         int rightEdge, int barCenterY, int margin, int gap) {
    // Size every button to its label first.
    for (Fl_Button *b : btns)
        if (b) static_cast<HoverButton *>(b)->fit();
    // Place right-to-left.
    int x = rightEdge - margin;
    // Walk the list in reverse to lay out from right to left.
    const auto *vec = btns.begin();
    for (size_t i = btns.size(); i-- > 0;) {
        Fl_Button *b = vec[i];
        if (!b) continue;
        int w = b->w();
        b->position(x - w, barCenterY - b->h() / 2);
        x -= w + gap;
    }
}

// Dropdown styled like the Options dialog's Tab width / Auto save
// choices: rectangular FL_BORDER_BOX with a divider line and arrow.
// Fl_Choice would hardcode its own box type otherwise.
class SettingsChoice : public Fl_Choice {
public:
    SettingsChoice(int X, int Y, int W, int H, const char *L = nullptr)
        : Fl_Choice(X, Y, W, H, L) {}

    void draw() FL_OVERRIDE {
        // Draw the widget box using our own box type (not Fl_Choice's
        // hardcoded one).
        draw_box(box(), color());

        // Arrow area
        int dx = Fl::box_dx(box());
        int dy = Fl::box_dy(box());
        int H = h() - 2 * dy;
        int W = 20;
        int X = x() + w() - W - dx;
        int Y = y() + dy;
        int active = active_r();
        Fl_Color arrow_color = active ? labelcolor() : fl_inactive(labelcolor());

        // Arrow divider: vertical line
        int x1 = X;
        int y1 = y() + dy;
        int y2 = y() + h() - dy;
        fl_color(fl_darker(color()));
        fl_yxline(x1, y1, y2);

        // Arrow
        Fl_Rect ab(X, Y, W, H);
        ab.inset(2, 0, 2, 0);
        fl_draw_arrow(ab, FL_ARROW_CHOICE, FL_ORIENT_NONE, arrow_color);

        // Selected text
        W += 2 * dx;
        if (mvalue()) {
            Fl_Menu_Item m = *mvalue();
            if (active) m.activate(); else m.deactivate();
            int xx = x() + dx, yy = y() + dy + 1, ww = w() - W, hh = H - 2;
            fl_push_clip(xx, yy, ww, hh);
            fl_draw_shortcut = 2;
            m.draw(xx, yy, ww, hh, this, Fl::focus() == this);
            fl_draw_shortcut = 0;
            fl_pop_clip();
        }

        draw_label();
    }
};

// Single-line input styled like the main window's editor/inputs: bordered,
// theme background/text and a cursor colour matching the main window's
// textPrimary. Replaces bare Fl_Input so every dialog input looks (and has
// the same caret colour as) the main editor.
class ThemedInput : public Fl_Input {
public:
    ThemedInput(int x, int y, int w, int h, const ThemeColors &tc,
                const char *lbl = nullptr)
        : Fl_Input(x, y, w, h, lbl) {
        box(FL_BORDER_BOX);
        color(tc.bgEditor);
        textcolor(tc.textPrimary);
        cursor_color(tc.textPrimary);
        selection_color(tc.accentSelection);
    }

    // Re-apply colours from a (possibly newly reloaded) theme.
    void themify(const ThemeColors &tc) {
        color(tc.bgEditor);
        textcolor(tc.textPrimary);
        cursor_color(tc.textPrimary);
        selection_color(tc.accentSelection);
    }
};

// Text display styled like the main window's editor panes, including
// custom-drawn scrollbars identical to Editor::drawCustomScrollbar.
class ThemedTextDisplay : public Fl_Text_Display {
public:
    ThemedTextDisplay(int x, int y, int w, int h, const ThemeColors &tc)
        : Fl_Text_Display(x, y, w, h), m_tc(tc) {
        box(FL_FLAT_BOX);
        color(tc.bgEditor);
        textcolor(tc.textPrimary);
        selection_color(tc.accentSelection);   // same selection color as Pecia
        scrollbar_size(10);
        styleToolScrollbar(mVScrollBar, FL_VERT_SLIDER, tc);
        styleToolScrollbar(mHScrollBar, FL_HOR_SLIDER, tc);
    }

    void draw() FL_OVERRIDE {
        Fl_Text_Display::draw();
        drawToolScrollbar(mVScrollBar, false, m_tc);
        drawToolScrollbar(mHScrollBar, true, m_tc);
    }

private:
    const ThemeColors &m_tc;
};

// Text editor styled like the main window's editor panes.
class ThemedTextEditor : public Fl_Text_Editor {
public:
    ThemedTextEditor(int x, int y, int w, int h, const ThemeColors &tc,
                     bool lineHighlight = true)
        : Fl_Text_Editor(x, y, w, h), m_tc(tc), m_lineHighlightOn(lineHighlight) {
        box(FL_FLAT_BOX);
        color(tc.bgEditor);
        textcolor(tc.textPrimary);
        selection_color(tc.accentSelection);   // same selection color as Pecia
        cursor_color(tc.textPrimary);          // same cursor color as Pecia
        scrollbar_size(10);
        styleToolScrollbar(mVScrollBar, FL_VERT_SLIDER, tc);
        styleToolScrollbar(mHScrollBar, FL_HOR_SLIDER, tc);
    }

    // ---- Current-line highlight via style buffer (same as Editor.cpp) ----

    // (Re)build the style buffer: 'A' = normal, 'B' = current line.
    void applyHighlight() {
        if (!buffer() || !m_lineHighlightOn) return;
        int len = buffer()->length();
        if (len <= 0) { m_lastLineStart = -1; return; }
        if (!m_styleBuf) m_styleBuf = new Fl_Text_Buffer(len > 0 ? len : 1);
        std::string fill(len, 'A');
        m_styleBuf->text(fill.c_str());
        m_styleTable[0].color   = textcolor();
        m_styleTable[0].font    = textfont();
        m_styleTable[0].size    = textsize();
        m_styleTable[0].attr    = 0;
        m_styleTable[0].bgcolor = color();
        m_styleTable[1].color   = textcolor();
        m_styleTable[1].font    = textfont();
        m_styleTable[1].size    = textsize();
        m_styleTable[1].attr    = Fl_Text_Display::ATTR_BGCOLOR;
        m_styleTable[1].bgcolor = m_tc.lineHighlight;
        if (!m_hlDataSet) {
            highlight_data(m_styleBuf, m_styleTable, 2, 'A', 0, 0);
            m_hlDataSet = true;
        }
        // Mark the current line 'B'.
        int pos = insert_position();
        if (pos < 0) pos = 0;
        if (pos > len) pos = len;
        int lineStart = buffer()->line_start(pos);
        int lineEnd   = buffer()->line_end(pos);
        if (lineEnd > len) lineEnd = len;
        m_lastLineStart = lineStart;
        if (lineEnd > lineStart) {
            std::string hl(lineEnd - lineStart, 'B');
            m_styleBuf->replace(lineStart, lineEnd, hl.c_str());
        }
        redraw();
    }

    // Rebuild the style buffer to match the current text length (call
    // when the text changes size). Same as Editor::syncLineHighlightBuffer.
    void syncHighlightBuffer() {
        if (!m_lineHighlightOn || !buffer()) return;
        m_lastLineStart = -1;
        applyHighlight();
    }

    // Mark the line under the cursor 'B' (call when the cursor moves to
    // another line). Same as Editor::updateLineHighlight.
    void refreshHighlight() {
        if (!m_lineHighlightOn || !buffer() || !m_styleBuf) return;
        int pos = insert_position();
        int len = buffer()->length();
        if (pos < 0) pos = 0;
        if (pos > len) pos = len;
        int curLineStart = buffer()->line_start(pos);
        if (curLineStart == m_lastLineStart) return;
        // Revert the previous line to 'A'.
        if (m_lastLineStart >= 0 && m_lastLineStart < len) {
            int oldEnd = buffer()->line_end(m_lastLineStart);
            if (oldEnd > len) oldEnd = len;
            if (oldEnd > m_lastLineStart) {
                std::string a(oldEnd - m_lastLineStart, 'A');
                m_styleBuf->replace(m_lastLineStart, oldEnd, a.c_str());
            }
        }
        // Mark the new current line 'B'.
        int lineEnd = buffer()->line_end(pos);
        if (lineEnd > len) lineEnd = len;
        m_lastLineStart = curLineStart;
        if (lineEnd > curLineStart) {
            std::string b(lineEnd - curLineStart, 'B');
            m_styleBuf->replace(curLineStart, lineEnd, b.c_str());
        }
        redraw();
    }

    int handle(int event) FL_OVERRIDE {
#if defined(_WIN32)
        // Install the in-place IME hook once the window HWND exists (focus,
        // any key press, or simply entering the widget) so pinyin composition
        // is drawn in place and the IME's floating window (its second caret)
        // stays hidden — matching the main editor. Drop any active
        // composition on unfocus.
        if (event == FL_FOCUS || event == FL_KEYBOARD || event == FL_ENTER)
            ImeInPlace::attach(this);
        else if (event == FL_UNFOCUS)
            ImeInPlace::detach(this);
#endif
        // Ctrl+Enter bubbles up to the owning window so its shortcut
        // dispatch (Window > Button > Run / Send, default Ctrl+Enter)
        // can fire. Fl_Text_Editor would otherwise insert a newline and
        // swallow the key before the window ever sees it.
        if (event == FL_KEYDOWN &&
            (Fl::event_key() == FL_Enter || Fl::event_key() == '\r') &&
            (Fl::event_state() &
             (FL_CTRL | FL_SHIFT | FL_ALT | FL_META)) == FL_CTRL)
            return 0;
        int r = Fl_Text_Editor::handle(event);
        if (m_lineHighlightOn && buffer()) {
            // Keep the style buffer in sync with the text length.
            if (m_styleBuf && m_styleBuf->length() != buffer()->length())
                syncHighlightBuffer();
            else if (!m_styleBuf && buffer()->length() > 0)
                applyHighlight();
            // Cursor moved via keyboard / mouse / selection.
            if (event == FL_KEYDOWN || event == FL_PUSH ||
                event == FL_DRAG || event == FL_RELEASE) {
                refreshHighlight();
            }
        }
        return r;
    }

    void draw() FL_OVERRIDE {
        Fl_Text_Editor::draw();

        // Current-line highlight - same two layers as the main Pecia
        // editor (Editor.cpp draw()):
        //  1. Character background via the style buffer ('B' style, set
        //     by refreshHighlight) - FLTK draws it natively, overlapping
        //     the text-selection highlight naturally.
        //  2. The line's trailing whitespace to the right edge: the style
        //     buffer cannot color empty space, so fill that strip here
        //     with a thin rect. It does NOT cover the characters.
        if (m_lineHighlightOn && buffer()) {
            int pos = insert_position();
            int blen = buffer()->length();
            if (pos < 0) pos = 0;
            if (pos > blen) pos = blen;
            int lineEnd   = buffer()->line_end(pos);
            int endX, endY;
            if (position_to_xy(lineEnd, &endX, &endY)) {
                fl_font(textfont(), textsize());
                int lineH = fl_height();
                Fl_Color hlColor = m_tc.lineHighlight;
                // Same geometry as Editor.cpp: from the end of the line's
                // text to the widget's right edge in WINDOW coordinates
                // (x()+w(), not w() alone - the tool editors sit at
                // x()=PAD so w() alone leaves a PAD-wide gap).
                int hlLeft = endX;
                int hlRight = x() + w();
                if (mVScrollBar && mVScrollBar->visible() &&
                    mVScrollBar->x() > hlLeft)
                    hlRight = mVScrollBar->x();
                if (hlRight > hlLeft) {
                    fl_color(hlColor);
                    fl_rectf(hlLeft, endY, hlRight - hlLeft, lineH);
                }
            }
        }

        // Cursor: a plain 3px vertical bar with no caps - same as the
        // main Pecia editor (Editor.cpp draw()).
        {
            int curX, curY;
            if (mCursorOn && Fl::focus() == this &&
                position_to_xy(insert_position(), &curX, &curY)) {
                fl_font(textfont(), textsize());
                int barH = mMaxsize ? mMaxsize : fl_height();
                fl_color(color());
                fl_rectf(curX - 2, curY, 5, barH);
                fl_color(cursor_color());
                fl_rectf(curX - 1, curY, 3, barH);
            }
        }

        // Scrollbars last, on top of the highlight strip.
        drawToolScrollbar(mVScrollBar, false, m_tc);
        drawToolScrollbar(mHScrollBar, true, m_tc);
    }

private:
    const ThemeColors &m_tc;
    bool               m_lineHighlightOn;
    Fl_Text_Buffer    *m_styleBuf = nullptr;
    Fl_Text_Display::Style_Table_Entry m_styleTable[2];
    int                m_lastLineStart = -1;
    bool               m_hlDataSet = false;
};
