// ImeInPlace.h - Shared Windows IME in-place composition for Pecia text
// widgets.
//
// FLTK does not consume WM_IME_* messages, so by default the IME reverts to
// its floating composition window, which draws its own caret next to the
// widget's caret -> "two carets" during pinyin input, and that floating
// window does not follow the insertion point. The main editor fixed this by
// consuming WM_IME_* and rendering the composition in place. This component
// makes that behaviour a reusable, single implementation for every text
// widget in the project (Fl_Text_Editor and Fl_Input families alike), so the
// main editor, the tool windows and every dialog input share the exact same
// caret and composition path.
//
// Usage:
//   * a text widget derives from ImeInPlace::Host and forwards its focus
//     events: attach(this) on FL_FOCUS/FL_KEYBOARD, detach(this) on
//     FL_UNFOCUS, release(this) in the destructor;
//   * the Host owns its composition state and commits the string itself
//     (buffer or value), because the storage differs per widget family;
//   * draw() calls drawComposition() while its own composition state is
//     active, so the composition text and the caret are painted by the same
//     code everywhere (same colour, width, blink phase and fl_set_spot
//     anchor).
//
// attach() installs a window-proc hook on the widget's top window (once per
// window) that routes WM_IME_STARTCOMPOSITION / WM_IME_COMPOSITION /
// WM_IME_ENDCOMPOSITION to the focused Host of that window and hides the
// IME's default composition window (ISC_SHOWUICOMPOSITIONWINDOW), which is
// what removes the second caret. Candidate lists keep working.
#pragma once

#include <FL/Fl.H>
#include <FL/Fl_Widget.H>

class Fl_Window;

namespace ImeInPlace {

class Host {
public:
    virtual ~Host() = default;

    // The widget that receives focus and whose top window owns the IME.
    virtual Fl_Widget *imeWidget() = 0;

    // Composition lifecycle. `utf8` is UTF-8; `cursorBytes` is a byte offset
    // into it (GCS_CURSORPOS converted from UTF-16 code units).
    virtual void imeCompositionStart() = 0;
    virtual void imeCompositionUpdate(const char *utf8, int cursorBytes) = 0;
    virtual void imeCompositionCommit(const char *utf8) = 0;
    virtual void imeCompositionEnd() = 0;
};

void attach(Host *host);
void detach(Host *host);
void release(Host *host);

void drawComposition(const char *text, int cursorBytes,
                     int x, int top, int lineH,
                     Fl_Color textColor, Fl_Color caretColor, bool caretVisible,
                     Fl_Font font, int fontSize, Fl_Window *win,
                     int clipX, int clipY, int clipW, int clipH);

}  // namespace ImeInPlace
