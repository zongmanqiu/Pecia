// PassThruInput.h - Fl_Input subclass that does NOT consume Ctrl+H /
// Ctrl+F / Ctrl+G internally, so those shortcuts can reach MainWindow's
// dispatchShortcut. Fl_Input_ normally interprets Ctrl+H as backspace
// (ASCII 0x08) on some platforms; this override lets those key events
// through to the parent. Shared by FindReplace and GoTo.
#pragma once

#include <FL/Fl_Input.H>

class PassThruInput : public Fl_Input {
public:
    PassThruInput(int x, int y, int w, int h, const char *lbl = nullptr)
        : Fl_Input(x, y, w, h, lbl) {}
    int handle(int ev) FL_OVERRIDE {
        if (ev == FL_KEYDOWN || ev == FL_SHORTCUT) {
            int k = Fl::event_key();
            int s = Fl::event_state();
            if ((s & FL_COMMAND) && (k == 'f' || k == 'h' || k == 'g'))
                return 0;  // let parent handle it
            if (k == FL_Enter || k == FL_KP_Enter)
                return 0;
        }
        return Fl_Input::handle(ev);
    }
};
