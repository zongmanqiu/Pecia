// PassThruInput.h - Fl_Input subclass that does NOT consume Ctrl+H /
// Ctrl+F / Ctrl+G internally, so those shortcuts can reach MainWindow's
// dispatchShortcut. Fl_Input_ normally interprets Ctrl+H as backspace
// (ASCII 0x08) on some platforms; this override lets those key events
// through to the parent. Shared by FindReplace and GoTo.
//
// 基类是 BlinkInput（而非裸 Fl_Input）：这样所有用它的对话框搜索框/参数框
// 都自动获得与主编辑器一致的 500ms 光标闪烁（见 ui/CaretBlink.h）。
#pragma once

#include "ui/BlinkInput.h"

#include <FL/Fl_Input.H>

class PassThruInput : public BlinkInput {
public:
    PassThruInput(int x, int y, int w, int h, const char *lbl = nullptr)
        : BlinkInput(x, y, w, h, lbl) {}
    int handle(int ev) FL_OVERRIDE {
        if (ev == FL_KEYDOWN || ev == FL_SHORTCUT) {
            int k = Fl::event_key();
            int s = Fl::event_state();
            if ((s & FL_COMMAND) && (k == 'f' || k == 'h' || k == 'g'))
                return 0;  // let parent handle it
            if (k == FL_Enter || k == FL_KP_Enter)
                return 0;
        }
        // Keep BlinkInput's focus/IME bookkeeping (attach/detach, blink reset)
        // while still letting Ctrl+H/F/G and Enter bubble to MainWindow.
        return BlinkInput::handle(ev);
    }
};
