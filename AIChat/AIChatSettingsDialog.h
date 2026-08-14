// AIChatSettingsDialog.h - modal AI configuration dialog for the AI Chat
// tool. Endpoint / Model / API Key only; writes to settings.ini (ai_* keys)
// so the AI Chat tool can be configured standalone, without Pecia.
// Visual style mirrors SettingsDialog: custom title bar (createInfoTitleBar),
// bottom button bar with chrome background, and a 1px outer border overlay.
#pragma once

#include <FL/Fl_Double_Window.H>

class Fl_Input;
class InfoTitleBar;
class BorderOverlay;
class Config;
class Theme;

class AIChatSettingsDialog : public Fl_Double_Window {
public:
    AIChatSettingsDialog(Config *appCfg, const Theme *theme, int uiFontSize);

    // Pre-fill inputs from config, show modal, save on OK. Returns true
    // if the user clicked OK (values written to settings.ini).
    bool runModal();

    void draw() FL_OVERRIDE;   // redraw outer border on top (like SettingsDialog)
    int handle(int event) FL_OVERRIDE;   // OK shortcut

private:
    static void cbOk(Fl_Widget *, void *);
    static void cbGLM(Fl_Widget *, void *);

    Config     *m_cfg;
    const Theme *m_theme;
    Fl_Input   *m_endpoint = nullptr;
    Fl_Input   *m_model = nullptr;
    Fl_Input   *m_key = nullptr;
    InfoTitleBar  *m_titleBar = nullptr;
    BorderOverlay *m_border = nullptr;
    bool         m_ok = false;
};
