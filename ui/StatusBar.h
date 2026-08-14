// StatusBar.h - Custom status bar widget
#pragma once

#include <FL/Fl_Output.H>

// StatusBar
//   Fl_Output subclass that draws its text with a fixed 6px left padding
//   instead of the default box-dx padding. Rejects all mouse events
//   (display-only, not interactive).
class StatusBar : public Fl_Output {
public:
    StatusBar(int X, int Y, int W, int H, const char *l = nullptr)
        : Fl_Output(X, Y, W, H, l) {}

    void draw() FL_OVERRIDE {
        Fl_Boxtype b = box();
        if (damage() & FL_DAMAGE_ALL) draw_box(b, color());
        const int LEFT_PAD = 6;
        Fl_Input_::drawtext(x() + LEFT_PAD,
                            y() + Fl::box_dy(b),
                            w() - LEFT_PAD - Fl::box_dx(b),
                            h() - Fl::box_dh(b));
    }

    int handle(int event) FL_OVERRIDE {
        if (event == FL_FOCUS || event == FL_UNFOCUS) return 0;
        // Display-only widget: never show the text I-beam. Fl_Input_
        // sets FL_CURSOR_INSERT on enter/move; override with the default
        // arrow (FL_LEAVE falls through to Fl_Input_, which restores it).
        if (event == FL_ENTER || event == FL_MOVE ||
            event == FL_PUSH || event == FL_RELEASE ||
            event == FL_DRAG) {
            if (window()) window()->cursor(FL_CURSOR_DEFAULT);
            return 0;
        }
        return Fl_Output::handle(event);
    }
};
