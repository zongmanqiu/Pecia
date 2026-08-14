// ShortcutDialog.h - modal dialog for customizing keyboard shortcuts.
#pragma once

#include "core/ShortcutCore.h"
#include "ui/DialogBase.h"

#include <string>
#include <vector>

class Config;
class Theme;
class Fl_Box;
class Fl_Button;
class Fl_Input;

// One configurable action shown in the dialog.
struct ShortcutDialogRow {
    std::string id;        // config key suffix ("win.cancel", "menu.file.save",
                           // "script.Formatting/trim", ...)
    std::string name;      // display name (already translated)
    int         group = 0; // ShortcutGroup (dialog section)
    int         defKey = 0;    // default combo when the config value is empty
    unsigned    defMods = 0;
};

// ShortcutDialog
//   Modal dialog opened via Settings > Shortcuts.... Lists every
//   configurable action (main-window menu items, toolbar scripts,
//   window operations, Lua console + AI chat actions) grouped into
//   sections. Double-click a row to record a new key combo; click the
//   [Clear] button to make the shortcut empty. Conflicts are flagged
//   red and invalid keys (bare letters, Shift+letter, Ctrl+Alt+Del)
//   are rejected inline.
//
//   On OK, every row's effective combo is written to Config
//   ("shortcut.<id>" in the shared settings.ini). The calling window is
//   responsible for applying the changes (rebuilding menus / handlers).
class ShortcutDialog : public DialogBase {
public:
    ShortcutDialog(int w, int h, const char *title, Config *cfg,
                   const Theme *theme = nullptr, int uiFontSize = 16);
    ~ShortcutDialog() override;

    // Install the action list (call before runModal).
    void setRows(const std::vector<ShortcutDialogRow> &rows);

    // Run modally. Returns true when the user pressed OK (config has
    // been written); false on cancel.
    bool runModal();

private:
    class RowWidget;
    class SettingsScroll;

    struct RowState {
        ShortcutDialogRow def;
        ShortcutCombo    combo;   // current in-memory combo
        bool             conflict = false;
    };

    void layoutRows();
    void applyFilter();
    void updateHintVisibility();
    void repositionRows();
    void recomputeConflicts();
    void rebuildComboLabels();
    std::string comboText(const ShortcutCombo &combo) const;
    void onRowComboClicked(RowWidget *w);
    void onRowBlur(RowWidget *w);
    void stopRecording();
    void onRowComboChanged(RowWidget *w, const ShortcutCombo &combo);
    void setError(const char *text);
    void updateHintText();
    void finishOk();
    void resetToDefaults();

    static void cbOk(Fl_Widget *w, void *data);
    static void cbCancel(Fl_Widget *w, void *data);
    static void cbReset(Fl_Widget *w, void *data);
    static void cbFilter(Fl_Widget *w, void *data);
    bool rowVisible(size_t i) const;

    Config          *m_cfg = nullptr;
    const Theme     *m_theme = nullptr;
    int              m_uiFontSize = 16;
    int              m_pad = 16;   // content gap above/below the rows
                                   // (dialog_pad config key, shared with
                                   // the Options dialog)
    std::vector<RowState> m_rows;
    std::vector<RowWidget *> m_widgets;
    RowWidget       *m_recording = nullptr;
    class SettingsScroll *m_scroll = nullptr;   // the rows' scroll container
    class Fl_Input  *m_filterInput = nullptr;
    Fl_Box          *m_hintLabel = nullptr;
    Fl_Box          *m_bottomSpacer = nullptr;
    Fl_Box          *m_topSpacer = nullptr;
    std::string      m_filterText;   // lowercase filter query
    std::string      m_errText;      // current error ("" = none)
    class Fl_Button *m_okBtn = nullptr;
    class Fl_Button *m_cancelBtn = nullptr;
    class Fl_Button *m_resetBtn = nullptr;

    int handle(int event) FL_OVERRIDE;
};
