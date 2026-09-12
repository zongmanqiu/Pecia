// HoverMenuBar.h - Fl_Menu_Bar subclass with hover highlighting and a
// custom item layout that EXACTLY matches FLTK's internal menu geometry.
//
// Why the layout matters (this fixed a visible offset bug):
//   - Our draw()/itemAt() previously used "x()+6, measure+12" per item.
//   - FLTK's menu popup machinery (Fl_Menu_::pulldown, menuwindow::
//     find_selected/titlex, and the "fakemenu" pressed-state window)
//     computes item positions with "x()+3, measure+16".
//   - Clicking a menu-bar button therefore opened the popup and drew the
//     pressed highlight offset from the button by (4*index - 3) px.
//   - Fix: draw with the SAME math FLTK uses internally (start 3, +16),
//     so the popup, the pressed look, and hover hit-testing all line up
//     exactly with the drawn items. Text stays centered within the item.
//
// The class is shared by the main menu bar and the script toolbar (the
// toolbar is also a HoverMenuBar, giving it menu-bar hover/switch
// behavior: clicking one item pops its menu, then hovering other items
// switches menus - stock Fl_Menu_Button::popup() cannot do this).
//
// While a submenu is open FLTK's modal pulldown loop grabs the mouse, so
// this widget never receives FL_MOVE events of its own and its hover
// highlight would freeze on the clicked item. A 30ms timeout polls the
// real mouse position (Fl::event_x_root()) while the menu is open and
// re-draws the highlight to track the item the popup switched to.
#pragma once

#include <FL/Fl_Menu_Bar.H>

class HoverMenuBar : public Fl_Menu_Bar {
public:
    HoverMenuBar(int X, int Y, int W, int H, const char *l = nullptr)
        : Fl_Menu_Bar(X, Y, W, H, l), m_pressColor(0) {}

    // Pressed-state fill (theme highlight2). 0 = derive via fl_darker().
    void setPressColor(Fl_Color c) { m_pressColor = c; }

    void draw() override {
        draw_box();
        if (!menu() || !menu()->text) return;
        // Item layout: X starts at x()+3, each item is measure()+16 wide
        // (matching FLTK's internal menuwindow math - see header comment).
        // Text is center-aligned so it is always equidistant from the
        // item borders regardless of language.
        int hl = m_menuOpen ? m_highlight : hoverIndex();
        int X = x() + 3;
        int i = 0;
        for (const Fl_Menu_Item *m = menu()->first(); m->text; m = m->next()) {
            int W = m->measure(0, this) + 16;
            if (i == hl) {
                Fl_Color hc = selection_color();
                // When a menu is actually open on this item (pressed), use
                // m_pressColor when the caller set one (tied to theme
                // highlight2), else darken the fill so the active menu
                // reads as pressed vs a plain hover.
                if (m_menuOpen && i == m_highlight && i == hl) {
                    if (m_pressColor != 0)
                        hc = m_pressColor;
                    else
                        hc = fl_darker(hc);
                }
                fl_draw_box(box(), X, y(), W, h(), hc);
            }
            // Draw the label centered within the item's full width (W).
            // We bypass m->draw() because it left-aligns with a 3px offset.
            Fl_Label l;
            l.value   = m->text;
            l.image   = nullptr;
            l.deimage = nullptr;
            l.type    = m->labeltype_ ? m->labeltype_ : FL_NORMAL_LABEL;
            l.font    = m->labelsize_ || m->labelfont_ ? m->labelfont_ : textfont();
            l.size    = m->labelsize_ ? m->labelsize_ : textsize();
            l.color   = m->labelcolor_ ? m->labelcolor_ : textcolor();
            l.h_margin_ = l.v_margin_ = l.spacing = 0;
            if (!m->active()) l.color = fl_inactive((Fl_Color)l.color);
            l.draw(X, y(), W, h(), FL_ALIGN_CENTER);
            X += W;
            ++i;
        }
    }

    // Find the menu item at window-relative coordinates (mx, my), or
    // nullptr if the point is not on any item. Layout must match draw()
    // AND FLTK's internal menuwindow math (3 + measure + 16 per item):
    // FLTK uses the same math in titlex()/find_selected() to position
    // the popup, the pressed "fakemenu" look, and hover switching.
    const Fl_Menu_Item *itemAt(int mx, int my) {
        if (my < y() || my >= y() + h()) return nullptr;
        int X = x() + 3;
        for (const Fl_Menu_Item *m = menu()->first(); m->text; m = m->next()) {
            int W = m->measure(0, this) + 16;
            if (mx >= X && mx < X + W) return m;
            X += W;
        }
        return nullptr;
    }

    int handle(int event) override {
        // Redraw on mouse motion so the hover highlight tracks the cursor.
        if (event == FL_ENTER || event == FL_LEAVE || event == FL_MOVE) {
            redraw();
        }
        if (event == FL_PUSH && menu() && menu()->text) {
            const Fl_Menu_Item *v = itemAt(Fl::event_x(), Fl::event_y());
            if (v) {
                m_highlight = itemIndexAt(Fl::event_x(), Fl::event_y());
                m_menuOpen = true;
                redraw();   // paint the pressed highlight immediately
                // While the pulldown's modal grab is active this widget gets
                // no mouse events, so a repeat timer tracks the real cursor
                // and re-highlights the item the popup switched to.
                Fl::repeat_timeout(0.03, s_trackHoverCb, this);
                // pulldown with initial_item = v pops up v's submenu
                // immediately, matching Fl_Menu_Bar's click behaviour.
                v = menu()->pulldown(x(), y(), w(), h(), v, this, 0, 1);
                picked(v);
                m_menuOpen = false;
                Fl::remove_timeout(s_trackHoverCb, this);
                m_highlight = -1;
                redraw();
            }
            return 1;
        }
        return Fl_Menu_Bar::handle(event);
    }

private:
    // Index of the item under (mx, my) in window coordinates, or -1.
    int itemIndexAt(int mx, int my) const {
        if (my < y() || my >= y() + h()) return -1;
        int X = x() + 3;
        int i = 0;
        for (const Fl_Menu_Item *m = menu()->first(); m->text; m = m->next()) {
            int W = m->measure(0, this) + 16;
            if (mx >= X && mx < X + W) return i;
            X += W;
            ++i;
        }
        return -1;
    }

    // Index of the item under the cursor while the mouse is over this
    // widget in the normal (non-modal) case, or -1.
    int hoverIndex() const {
        if (Fl::belowmouse() != this) return -1;
        int mx = Fl::event_x();
        int my = Fl::event_y();
        if (mx < x() || mx >= x() + w() ||
            my < y() || my >= y() + h()) return -1;
        return itemIndexAt(mx, my);
    }

    // Re-run while a submenu is open: translate the real cursor position
    // (root coordinates) into this widget's window coordinates and update
    // the pressed highlight to the item the popup is showing. On the bar
    // but between items -> no highlight; over the popup itself -> keep
    // the open item.
    static void s_trackHoverCb(void *data) {
        HoverMenuBar *self = static_cast<HoverMenuBar *>(data);
        if (!self->m_menuOpen) return;
        // Window-relative cursor position: itemAt()/itemIndexAt() expect
        // coordinates in the parent window's space (they add x()/y()
        // internally), so only the window origin is subtracted here.
        int wx = 0, wy = 0;
        for (Fl_Window *w = self->window(); w; w = w->window()) {
            wx += w->x();
            wy += w->y();
        }
        int rx = Fl::event_x_root();
        int ry = Fl::event_y_root();
        int idx = self->itemIndexAt(rx - wx, ry - wy);
        int hl;
        if (idx >= 0) {
            hl = idx;
        } else if (ry >= wy + self->y() && ry < wy + self->y() + self->h()) {
            hl = -1;                  // on the bar but between items
        } else {
            hl = self->m_highlight;   // over the popup -> keep open item
        }
        if (hl != self->m_highlight) {
            self->m_highlight = hl;
            self->redraw();
        }
        Fl::repeat_timeout(0.03, s_trackHoverCb, self);
    }

    int  m_highlight = -1;   // item index highlighted while a menu is open
    bool m_menuOpen = false;
    Fl_Color m_pressColor = 0;   // pressed fill (theme highlight2); 0=derive
};
