// ScriptDialogs.h - the two generic dialogs a Lua script can ask the host
// to show (m025): one text prompt, one list choice.
//
// They exist so scripts get Pecia-styled, themed UI instead of raw Win32
// boxes, and so that UI is MEASURABLE: both register with WidgetRegistry,
// which is what the ui.* host ops read. Headless runs never construct them
// (the engine answers from the captured/default values instead).
#pragma once

#include <string>
#include <vector>

#include "ui/DialogBase.h"

class Fl_Input;
class Fl_Return_Button;
class Fl_Button;
class Fl_Group;
class Theme;

// Single-line prompt: returns the text, or "" when cancelled. result()
// distinguishes "empty text accepted" from "cancelled" via ok().
class PromptDialog : public DialogBase {
public:
    PromptDialog(const char *title, const char *label, const char *dflt,
                 const Theme *theme, int uiFontSize);
    ~PromptDialog() override;

    // Modal: runs a private event loop (same pattern as ConfirmDialog).
    // In smoke-test mode it returns the default without blocking.
    bool run();

    bool ok() const { return m_ok; }
    const std::string &value() const { return m_value; }

private:
    static void cbOk(Fl_Widget *, void *);
    static void cbCancel(Fl_Widget *, void *);
    int handle(int event) FL_OVERRIDE;

    Fl_Input   *m_input = nullptr;
    Fl_Button  *m_okBtn = nullptr;
    Fl_Button  *m_cancelBtn = nullptr;
    bool              m_ok = false;
    std::string       m_value;
};

// List choice: pre is the 1-based initially highlighted row. Returns the
// chosen index (0-based) or -1 when cancelled.
class PickDialog : public DialogBase {
public:
    PickDialog(const char *title, const char *label,
               const std::vector<std::string> &items, int pre,
               const Theme *theme, int uiFontSize);
    ~PickDialog() override;

    bool run();
    int  choice() const { return m_choice; }

private:
    static void cbItem(Fl_Widget *, void *);
    static void cbCancel(Fl_Widget *, void *);
    int handle(int event) FL_OVERRIDE;

    Fl_Group  *m_list = nullptr;
    Fl_Button *m_cancelBtn = nullptr;
    int        m_default = -1;   // 0-based default from the 1-based `pre`
    int        m_choice = -1;
};
