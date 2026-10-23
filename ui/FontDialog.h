// FontDialog.h - system font picker (View > Font)
//
// Replaces the old hard-coded 9-entry View > Font radio submenu. That list
// was wrong in practice: SimHei / NSimSun / Gulim are not installed on most
// machines, and Noto Sans SC - even when installed - is invisible to FLTK's
// default (ANSI-only) font enumeration. Picking any of them silently fell
// back to Courier, so the user never saw the font they chose.
//
// This dialog enumerates what is actually installed instead:
//
//   - search box (substring, case-insensitive)
//   - "monospace only" filter
//   - the list renders every row IN ITS OWN FONT, so it doubles as a preview
//   - a larger sample area below shows the current selection
//
// Deliberately absent: font size and bold/italic. Size is the zoom feature
// (Ctrl +/-, persisted as editor_zoom_size); bold/italic mean nothing for a
// plain-text editor, and FLTK's bold/italic variants (attr != 0) are filtered
// out of the enumeration anyway.
#pragma once

#include "ui/DialogBase.h"
#include <FL/Fl_Input.H>          // search field member type
#include <FL/Fl_Check_Button.H>   // "monospace only" member type
#include <string>
#include <vector>

class Theme;
class Fl_Box;
class SearchInput;   // ui/ThemeWidgets.h(搜索框实际类型,见 .cpp 构造)

// Test seam for test_ui_smoke.cpp (same pattern as SettingsDialog/ParamDialog).
struct UiSmokeAccess;

class FontDialog : public DialogBase {
public:
    // `currentFont` is the font name currently stored in the config; it is
    // pre-selected (and scrolled into view) when the dialog opens.
    FontDialog(const Theme *theme, int uiFontSize, const char *currentFont);
    ~FontDialog() override;

    // Shows the dialog and runs a modal loop. Returns true when the user
    // pressed OK (chosen() then holds the new font name).
    bool runModal();

    // Font name the user accepted; empty after a cancel.
    const std::string &chosen() const { return m_chosen; }

    friend struct UiSmokeAccess;

protected:
    int handle(int event) FL_OVERRIDE;   // Esc = cancel

private:
    class FontBrowser;    // self-drawn list (each row in its own face)
    class PreviewBox;     // sample text area

    void rebuildList();       // re-filter from search text + monospace switch
    void updateSearchHint();  // 提示字显隐(与 ShortcutDialog 同一规则)
    void syncSelection();     // browser value -> preview
    void selectLine(int line);// select a row (1-based) + refresh preview
    bool isMono(size_t idx);  // lazily computed + cached monospace flag

    static void cbSearch(Fl_Widget *w, void *data);
    static void cbMono(Fl_Widget *w, void *data);
    static void cbList(Fl_Widget *w, void *data);
    static void cbOk(Fl_Widget *w, void *data);
    static void cbCancel(Fl_Widget *w, void *data);

    std::vector<std::string> m_all;     // every regular face, sorted
    std::vector<std::string> m_shown;   // what the browser currently holds
    std::vector<char>        m_mono;    // -1 unknown, 0/1 = isMono(m_all[i])
    std::string m_current;
    std::string m_chosen;
    SearchInput     *m_search  = nullptr;      // 共享搜索框(ThemeWidgets.h)
    Fl_Box          *m_searchHint = nullptr;   // 输入内浅色提示字(共用搜索框)
    Fl_Check_Button *m_monoBox = nullptr;
    FontBrowser     *m_list    = nullptr;
    PreviewBox      *m_preview = nullptr;
    int              m_itemSize = 14;
};
