#pragma once

#include <FL/Fl_Group.H>
#include <functional>

class Fl_Input;
class Fl_Button;
class Fl_Box;
class Fl_Text_Editor;

class Theme;

class GoTo : public Fl_Group {
public:
    GoTo(int x, int y, int w, int h);
    ~GoTo();

    void setEditor(Fl_Text_Editor *e);

    void activate();

    void layout(int w);

    void gotoLine();

    void refreshLabels();

    void setCloseCallback(std::function<void()> cb) { m_onClose = std::move(cb); }
    void setGoCallback(std::function<void(long)> cb) { m_onGo = std::move(cb); }

    // Set theme for color values and immediately apply them.
    void setTheme(const Theme *theme);

    // Set the UI chrome font size.
    void setFontSize(int sz);

protected:
    int handle(int event) FL_OVERRIDE;

private:
    Fl_Input        *m_lineInput;
    Fl_Button       *m_btnGo;
    Fl_Button       *m_btnClose;
    Fl_Box          *m_lineLabel;

    Fl_Text_Editor  *m_editor;

    std::function<void()> m_onClose;
    std::function<void(long)> m_onGo;

    const Theme *m_theme = nullptr;
    int          m_fontSize = 16;

    void updateHintVisibility();

    static void cbGo(Fl_Widget *w, void *data);
    static void cbClose(Fl_Widget *w, void *data);
};