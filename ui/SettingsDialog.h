// SettingsDialog.h - modal dialog for application preferences
#pragma once

#include "core/Theme.h"
#include "Layout.h"
#include "ui/DialogBase.h"

#include <string>
#include <vector>

class Fl_Choice;
class Fl_Check_Button;
class Fl_Button;
class Fl_Int_Input;
class Fl_Input;
class Fl_Group;
class SettingsNav;
class Config;
class Theme;

// SettingsDialog
//   Modal window shown via Settings > Options... Lets the user
//   change tab width, auto indent, round corners, multi tab and UI
//   language. Font, theme, line numbers and word wrap are intentionally
//   NOT here - they live in the View menu where they can be toggled at
//   any time without a modal dialog. On OK, writes everything back to
//   Config and signals the caller (MainWindow) via applySettings() to
//   refresh open editors.
//
//   The dialog uses a custom-drawn title bar (border(0) + self-drawn
//   1px gray border) matching the main window / InfoWindow style, and
//   supports rounded vs. right-angle corners (Win11 DWM / Win10 region).
class SettingsDialog : public DialogBase {
public:
    SettingsDialog(int w, int h, const char *title, const Theme *theme = nullptr,
                    int uiFontSize = 16, int pad = 16);
    ~SettingsDialog();

    // Populate the controls from the given Config. The dialog does not
    // keep a pointer to Config - it copies values in/out by value.
    void loadFrom(const Config &cfg);

    // Write the current control values back to Config. Returns true if
    // anything changed (so the caller can refresh open editors).
    bool saveTo(Config &cfg) const;

    // Run the dialog modally. Returns true if the user clicked OK,
    // false if they cancelled.
    bool runModal();

    // Test/Debug: show the "add to Open with" ExtensionsDialog (build +
    // display only; never touches the registry). Used by the Help > Test
    // Dialogs menu to inspect the window's UI and translations.
    void openExtensionsForTest();

    // m040 S3: the page gate walks the switcher and the page groups; the
    // widgets themselves stay private.
    int pageCount() const { return (int)m_pages.size(); }
    Fl_Group *pageAt(int i) const {
        return (i >= 0 && i < (int)m_pages.size()) ? m_pages[i] : nullptr;
    }
    Fl_Group *contentGroup() const { return m_content; }
    class SettingsNav *nav() const { return m_nav; }



private:
    // Test seam: the UI smoke test reads/writes the controls and drives the
    // commit path (saveTo) without showing the dialog.
    friend struct UiSmokeAccess;

    Fl_Choice       *m_tabWidthChoice;   // 2 / 4 / 8 (spaces)
    Fl_Check_Button *m_autoIndentChk;

    Fl_Check_Button *m_multiTabChk;
    Fl_Check_Button *m_fixedStartupDocChk;  // 固定启动文档（无其它 Pecia 时打开 Test.txt）
    Fl_Choice       *m_autoPreviewChoice;   // 打开 .md 时：无操作/分屏预览/仅预览
    Fl_Check_Button *m_trimTrailingChk;
    Fl_Check_Button *m_trimLeadingChk;
    Fl_Check_Button *m_trimEndingChk;
    Fl_Check_Button *m_expandTabsChk;
    Fl_Choice       *m_autoSaveChoice;   // Off / 30s / 60s / 120s / 300s
    Fl_Check_Button *m_integNewTxt;      // System: New > Text Document
    bool             m_initialNewTxt = false;  // registry state when the dialog opened
    Fl_Button       *m_chooseExtsBtn;    // Opens ExtensionsDialog (add to "Open with")
    Fl_Check_Button *m_detectUrlsChk;    // Editor: URL detection
    Fl_Check_Button *m_pasteMediaChk;    // 粘贴图片/文件时插入路径（默认关）
    Fl_Check_Button *m_cleanupTempChk;   // Exit: clean 7-day-old temp files
    Fl_Int_Input    *m_longLineInput;    // 0 = off, >0 = column
    Fl_Input        *m_searchUrlInput;   // 搜索引擎 URL 模板（%s = 选中内容）
    // m040: the AI endpoint / model / key live here and nowhere else. The
    // chat pane used to open AIChatSettingsDialog for the same three keys.
    Fl_Input        *m_aiEndpoint = nullptr;
    Fl_Input        *m_aiModel = nullptr;
    Fl_Input        *m_aiKey = nullptr;
    Fl_Int_Input    *m_aiContext = nullptr;   // context turns (int, clamped)
    // m040 S3: vertical page switcher. The controls are built ONCE and live
    // in their page group; switching a page only shows/hides, so loadFrom /
    // saveTo never learn about pages and cannot lose a value on a switch.
    class SettingsNav *m_nav = nullptr;
    Fl_Group        *m_content = nullptr;
    std::vector<Fl_Group *> m_pages;
    Fl_Button       *m_okBtn;
    Fl_Button       *m_cancelBtn;
    const Theme    *m_theme = nullptr;
    int             m_uiFontSize = 16;
    int             m_pad = 16;   // dialog content padding (px), from settings.ini

    // Currently selected extensions for the "Open with" menu, stored as
    // individual entries (e.g. L".txt", L".md"). Edited via the
    // ExtensionsDialog sub-dialog (m_chooseExtsBtn callback). Deliberately
    // NOT persisted: the dialog always starts from an empty selection and
    // the registry is written only when that dialog is confirmed.
    std::vector<std::wstring> m_openWithExts;

    int handle(int event) FL_OVERRIDE;   // OK shortcut support

    static void cbOk(Fl_Widget *w, void *data);
    static void cbCancel(Fl_Widget *w, void *data);
    static void cbChooseExts(Fl_Widget *w, void *data);

    // Show one page and hide the rest (also syncs the switcher's active cell).
    void showPage(int i);
};
