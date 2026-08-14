// ParamDialog.h - adaptive modal parameter-input dialog.
// Used by Lua scripts with --!param declarations, and by the future Lua
// window API. Layout is fully content-driven:
//   - width  = longest label + input field + margins (clamped)
//   - height = title bar + rows + symmetric dialog_pad gaps + button bar
// Top: title bar with the given title and a close (X) button; the X
// counts as cancel. Bottom: chrome button bar with an OK button only.
// Parameter rows stack top-down, one input per parameter.
#pragma once

#include <string>
#include <vector>

#include "script/LuaParamParser.h"
#include "ui/ThemeWidgets.h"
#include "ui/DialogBase.h"

class Theme;
class Fl_Input;
class Fl_Check_Button;
class Fl_Button;

// Modal dialog collecting values for declared script parameters.
// run() returns true on OK (values in results()) or false on cancel
// (X / Esc-close).
class ParamDialog : public DialogBase {
public:
    ParamDialog(const char *title,
                const std::vector<LuaParam> &params,
                const Theme *theme = nullptr, int uiFontSize = 16);
    ~ParamDialog();

    bool run();

    // Value per parameter, in declaration order ("" for empty input).
    const std::vector<std::string> &results() const { return m_values; }

protected:
    int handle(int event) FL_OVERRIDE;   // OK shortcut

private:
    // Test seam: the UI smoke test sets control values and drives the commit
    // path (collectAndOk) without showing the dialog.
    friend struct UiSmokeAccess;

    static void cbBtn(Fl_Widget *w, void *data);
    static void cbKey(Fl_Widget *w, void *data);
    // Collect all values (checkbox/choice/input, in declaration order) into
    // m_values and confirm the dialog. Shared by the OK button and the
    // Enter-in-last-input shortcut so both walk m_params identically.
    static void collectAndOk(ParamDialog *self);

    const Theme        *m_theme;
    std::vector<LuaParam> m_params;      // declaration order (drives result order)
    std::vector<Fl_Input *> m_inputs;    // text rows (parallel to m_params)
    std::vector<SettingsChoice *> m_choices;  // dropdown rows (parallel to m_params)
    std::vector<Fl_Check_Button *> m_checks;  // checkbox rows (parallel to m_params)
    std::vector<std::string> m_values;
    Fl_Button        *m_ok = nullptr;
    int               m_result = 0;
};
