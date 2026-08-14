// AIChatSettingsDialog.h - modal AI configuration dialog for the AI Chat
// tool. Endpoint / Model / API Key only; writes to settings.ini (ai_* keys)
// so the AI Chat tool can be configured standalone, without Pecia.
// Built on DialogBase (same title bar / border / theme as every other
// dialog); bottom chrome button bar with HoverButtons; 1px outer border is
// drawn by DialogBase.
#pragma once

#include "ui/DialogBase.h"

class Fl_Input;
class Config;
class Theme;

class AIChatSettingsDialog : public DialogBase {
public:
    AIChatSettingsDialog(Config *appCfg, const Theme *theme, int uiFontSize);

    // Pre-fill inputs from config, show modal, save on OK. Returns true
    // if the user clicked OK (values written to settings.ini).
    bool runModal();

protected:
    int handle(int event) FL_OVERRIDE;   // OK shortcut

private:
    static void cbOk(Fl_Widget *, void *);
    static void cbGLM(Fl_Widget *, void *);

    Config     *m_cfg;
    Fl_Input   *m_endpoint = nullptr;
    Fl_Input   *m_model = nullptr;
    Fl_Input   *m_key = nullptr;
    bool         m_ok = false;
};
