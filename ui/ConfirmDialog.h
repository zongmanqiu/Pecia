// ConfirmDialog.h - custom modal confirmation dialog matching Pecia's style
#pragma once

#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>

class Theme;
class InfoTitleBar;
class BorderOverlay;

// Modal confirmation dialog with custom title bar and border,
// styled consistently with SettingsDialog ("Options").
// Mirrors the FLTK fl_choice() API but with Pecia's look.
class ConfirmDialog : public Fl_Double_Window {
public:
    // Buttons are indexed 0..2 matching fl_choice(b0, b1, b2).
    // run() returns the clicked button index, or -1 if closed via X.
    ConfirmDialog(const char *title, const char *message,
                  const char *b0, const char *b1, const char *b2,
                  const Theme *theme = nullptr, int uiFontSize = 16);
    ~ConfirmDialog();
    int run();

protected:
    void draw() FL_OVERRIDE;
    int handle(int event) FL_OVERRIDE;   // OK shortcut (button 0)

private:
    InfoTitleBar  *m_titleBar;
    BorderOverlay *m_border;
    const Theme   *m_theme = nullptr;
    Fl_Box        *m_message;
    Fl_Button     *m_btn[3];
    int            m_result = -1;

    static void cbBtn(Fl_Widget *w, void *data);
};
