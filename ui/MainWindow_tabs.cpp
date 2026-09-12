// MainWindow_tabs.cpp - MainWindow tab management (add/switch/close tabs,
// tab labels, title bar sync). Moved here from MainWindow.cpp so the main
// window file stays manageable. Member functions defined in this
// translation unit are part of the same MainWindow class declared in
// ui/MainWindow.h.
#include "ui/MainWindow.h"

#include "editor/Document.h"
#include "editor/Editor.h"
#include "ui/TitleBar.h"
#include "ui/FindReplace.h"
#include "ui/GoTo.h"
#include "core/Config.h"
#include "core/Theme.h"
#include "core/I18n.h"
#include "core/OpLog.h"
#include "ui/Layout.h"
#include "ui/HoverMenuBar.h"
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/filename.H>
#include <FL/fl_string_functions.h>
#include <FL/fl_draw.H>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

// --------------------------------------------------------------------------

int MainWindow::findTabByPath(const char *path) const {
    if (!path || !path[0]) return -1;
    for (int i = 0; i < (int)m_tabsList.size(); ++i) {
        const char *p = m_tabsList[i].doc->filePath();
        if (p && strcmp(p, path) == 0) return i;
    }
    return -1;
}

// --------------------------------------------------------------------------
// Tab right-click context menu (see MainWindow::handle FL_RIGHT_MOUSE).
// --------------------------------------------------------------------------

void MainWindow::showTabContextMenu(int tabIndex) {
    if (tabIndex < 0 || tabIndex >= (int)m_tabsList.size()) return;
    bool unnamed = !m_tabsList[tabIndex].doc->filePath()[0];

    Fl_Menu_Item items[] = {
        {I18n::get("tabs.closeothers"), 0, nullptr, nullptr, FL_MENU_DIVIDER},
        {I18n::get("tabs.copypath"), 0, nullptr, nullptr,
         unnamed ? FL_MENU_INACTIVE : 0},
        {nullptr}
    };
    const Fl_Menu_Item *picked =
        items->popup(Fl::event_x(), Fl::event_y());
    if (!picked) return;
    if (picked == &items[0]) closeOtherTabs(tabIndex);
    else if (picked == &items[1]) copyTabPath(tabIndex);
}

// Close every tab except keepIndex (the tab under the right-click).
// Tabs are saved/prompted through checkSaveBeforeClose, so a Cancel
// stops the operation with the remaining tabs untouched.
void MainWindow::closeOtherTabs(int keepIndex) {
    if (keepIndex < 0 || keepIndex >= (int)m_tabsList.size()) return;
    if ((int)m_tabsList.size() <= 1) return;

    // Close high indices first so the earlier indices (and keepIndex)
    // stay valid while erasing.
    for (int i = (int)m_tabsList.size() - 1; i >= 0; --i) {
        if (i == keepIndex) continue;
        switchToTab(i);
        if (!checkSaveBeforeClose(i)) break;   // user cancelled
        Tab t = m_tabsList[i];
        m_tabsList.erase(m_tabsList.begin() + i);
        // Same detach + destroy-page-before-doc ordering as closeTab.
        t.editor->buffer(nullptr);
        m_tabs->remove(t.page);
        delete t.page;
        delete t.doc;
    }

    switchToTab(keepIndex);
    layoutTabs();
    syncTitleBar();
    redraw();
}

void MainWindow::copyTabPath(int tabIndex) {
    if (tabIndex < 0 || tabIndex >= (int)m_tabsList.size()) return;
    const char *path = m_tabsList[tabIndex].doc->filePath();
    if (!path || !path[0]) return;   // unnamed tab: nothing to copy
    Fl::copy(path, (int)strlen(path), 1);
}

int MainWindow::activeTabIndex() const {
    Fl_Widget *current = m_tabs->value();
    for (int i = 0; i < (int)m_tabsList.size(); ++i) {
        if (m_tabsList[i].page == current) return i;
    }
    return -1;
}

MainWindow::Tab *MainWindow::activeTab() {
    int i = activeTabIndex();
    if (i < 0) return nullptr;
    return &m_tabsList[i];
}

int MainWindow::addTab(Document *doc, const char * /*label*/) {
    bool multi = m_cfg->getMultiTab();
    int toolbarH = m_showScriptBar ? SCRIPTBAR_H : 0;
    int menuBarH = m_showMenuBar ? MENU_H : 0;
    int contentTop = TITLE_H + menuBarH + toolbarH;
    int statusH = m_showStatusbar ? STATUS_H : 0;
    int statusY = h() - statusH;
    int tabsH = statusY - contentTop;

    // Page Y: same formula as layoutTabs().
    // - multi-tab:  pageY = contentTop + TABS_H → tab_height = TABS_H
    // - single-tab: pageY = contentTop          → tab_height = 0
    int pageY = contentTop + (multi ? TABS_H : 0);
    int pageH = tabsH - (multi ? TABS_H : 0);
    pageH = (pageH > 0) ? pageH : 1;

    // Create a tab page group that fills the available area.
    // Position is in WINDOW coords because Fl_Tabs children use
    // window-absolute coordinates.
    // IMPORTANT: never rely on Fl_Group::current() here - the
    // constructor explicitly sets current to the MainWindow after
    // m_tabs->end(), so `new Fl_Group(page)` would otherwise attach
    // the page to MainWindow and the Editor to whatever is current,
    // corrupting the tab child arrays -> Fl_Tabs::draw() dereferences
    // null and crashes (0xC0000005, intermittent). Set current to
    // m_tabs so the page lands directly in the tabs group, then to
    // page so the Editor lands in the page.
    Fl_Group *savedCurrent = Fl_Group::current();
    Fl_Group::current(m_tabs);
    Fl_Group *page = new Fl_Group(0, pageY, w(), pageH);
    page->box(FL_FLAT_BOX);     // no inner border
    page->color(m_tabs->color()); // match m_tabs color so Fl_Tabs::draw() doesn't draw a selection border
    Fl_Group::current(page);
    // Editor fills the entire page (coordinates are in WINDOW space,
    // same as the page itself, so use pageY/pageH here too).
    Editor *ed = new Editor(0, pageY, w(), pageH, m_cfg);
    ed->setCursorCallback([this]() { updateStatusBar(); });
    ed->setTheme(&m_theme);
    ed->box(FL_FLAT_BOX);       // remove the default sunken editor border
    ed->color(m_theme.colors().background1);  // theme background
    // Apply per-document settings from Config
    doc->setTrimTrailing(m_cfg->getTrimTrailingWhitespace());
    doc->setSaveFormat(m_cfg->getTrimLeadingBlank(),
                       m_cfg->getTrimEndingBlank(),
                       m_cfg->getExpandTabsOnSave(),
                       m_cfg->getTabWidth());
    ed->setDoc(doc);

    ed->buffer(doc->buffer());
    int fontId, fontSize;
    m_cfg->getFont(fontId, fontSize);
    fontSize = m_cfg->getUiFontSize();  // unify with rest of UI
    ed->textfont(fontId);
    ed->textsize(fontSize);
    ed->linenumber_size(fontSize);
    // Word wrap: the wrap layout measures every character through GDI and
    // takes seconds for multi-megabyte files, so large buffers are opened
    // with wrapping forced off (the user can still toggle it on manually).
    bool wrap = m_cfg->getWrap();
    if (wrap && doc->buffer()->length() > kWrapLimitBytes)
        wrap = false;
    ed->wrap_mode(wrap ? Fl_Text_Display::WRAP_AT_BOUNDS
                       : Fl_Text_Display::WRAP_NONE, 0);
    ed->buffer()->tab_distance(m_cfg->getTabWidth());
    ed->setLongLineMarker(m_cfg->getLongLineMarker());
    ed->linenumber_width(0);  // will be set by updateLinenumberWidth() below
    page->resizable(ed);
    page->end();
    // page was already added to m_tabs by the explicit current(m_tabs)
    // above; no further add() needed.
    Fl_Group::current(savedCurrent);   // restore caller's current

    // When flag: NEVER in single-tab mode, CHANGED in multi-tab mode.
    // FL_WHEN_CLOSED enables the built-in close button on each tab.
    page->when(multi ? (FL_WHEN_CHANGED | FL_WHEN_CLOSED) : FL_WHEN_NEVER);
    page->callback(cbTabPageClose, this);

    Tab t;
    t.doc = doc;
    t.page = page;
    t.editor = ed;
    m_tabsList.push_back(t);

    // Hook the buffer's modify callback so the title's * mark stays in sync
    doc->buffer()->add_modify_callback(cbModify, this);

    updateTabLabel((int)m_tabsList.size() - 1);

    // Switch to the new tab
    m_tabs->value(page);
    Fl::focus(ed);

    // openFile may have just destroyed the previously-active tab while
    // FindBar/GoToBar still hold a pointer to its editor - hide them so the
    // next Ctrl+F / Ctrl+G binds to this fresh tab (see switchToTab).
    bool barHidden = false;
    if (m_findBar && m_findBar->visible()) {
        m_findBar->clearMatchHighlight();
        m_findBar->hide();
        barHidden = true;
    }
    if (m_goToBar && m_goToBar->visible()) {
        m_goToBar->hide();
        barHidden = true;
    }
    if (barHidden) layoutTabs();

    // In multi-tab mode, make sure Fl_Tabs fires its callback on tab
    // changes. The close button is not available in FLTK 1.4.5, but
    // middle-click to close is handled in cbTabClose.
    if (multi) m_tabs->when(FL_WHEN_CHANGED);
    else       m_tabs->when(FL_WHEN_NEVER);

    // Enable line highlight if configured
    if (m_cfg->getHighlightCurrentLine()) {
        ed->setLineHighlight(true);
    }

    // Enable space symbols if configured
    if (m_cfg->getShowWhitespace()) {
        ed->setShowSpaceSymbols(true);
    }

    // Set editor font face
    char fontName[64];
    m_cfg->getEditorFont(fontName, sizeof(fontName));
    ed->setFontFace(fontName);

    // Set the correct line number width based on the buffer's line count
    updateLinenumberWidth();

    return (int)m_tabsList.size() - 1;
}

// Reposition every tab page (and its editor) based on the current
// status-bar visibility and multi-tab setting.
//
// NOTE: Fl_Tabs child widget coordinates are in WINDOW space (not
// relative to the Fl_Tabs). This is why tab_height() computes the gap
// as o->y() - y() — it subtracts the Fl_Tabs' own window y from the
// child's window y to get the child's offset inside the tab widget.
//
//   - single-tab mode: page top = contentTop (same as m_tabs top)
//     -> tab_height() = 0 -> no tab strip
//   - multi-tab mode:  page top = contentTop + TABS_H
//     -> tab_height() = TABS_H -> tab strip drawn at the top
void MainWindow::layoutTabs() {
    int toolbarH = m_showScriptBar ? SCRIPTBAR_H : 0;
    int menuBarH = m_showMenuBar ? MENU_H : 0;
    int contentTop = TITLE_H + menuBarH + toolbarH;  // m_tabs->y() in window coords
    int statusH = m_showStatusbar ? STATUS_H : 0;
    int statusY = h() - statusH;
    bool multi = m_cfg->getMultiTab();

    int findBarH = (m_findBar && m_findBar->visible()) ? FINDBAR_H : 0;
    int goToBarH = (m_goToBar && m_goToBar->visible()) ? GOTOBAR_H : 0;
    int tabsH = statusY - contentTop - findBarH - goToBarH;
    // 预览模式：两栏（编辑 | 分隔条 | 预览）；否则单栏全宽
    layoutPreviewPanes(contentTop, tabsH);

    // Tell FLTK to forget the old "initial sizes" — this prevents
    // Fl_Tabs::draw() from re-positioning children using stale bounds
    // that could conflict with the tab page positioning below.
    //
    // We use init_sizes() AFTER the resize, not before, so it captures
    // the current positions as the new reference frame. Without this,
    // Fl_Group::resize() (called transitively by drawing the widget)
    // might distribute size "differences" to children and leave gaps.
    m_tabs->init_sizes();

    // ---- Tab page geometry ----
    // Fl_Tabs computes tab_height() as the minimum (child->y() - this->y())
    // across all children.  We set each page's y to
    //   contentTop + (multi ? TABS_H : 0)
    // so Fl_Tabs sees tab_height = TABS_H when multi-tab is on, and 0
    // when it's off (single-tab = no visible tab strip).
    int pageY = contentTop + (multi ? TABS_H : 0);

    // The page height is everything from that Y down to the status bar.
    // We do NOT subtract TABS_H again — Fl_Tabs itself draws the tab
    // strip ON TOP of the child area (it clips its own drawing to the
    // tab strip region and paints the page in the remaining space).
    // The child (page) is expected to fill the FULL Fl_Tabs area,
    // starting at pageY.  Fl_Tabs' draw will clip the top TABS_H pixels
    // to paint the tab strip, and the bottom part shows the page.
    int pageH = tabsH - (multi ? TABS_H : 0);
    pageH = (pageH > 0) ? pageH : 1;

    for (auto &t : m_tabsList) {
        // Page fills the full editor area; position in WINDOW coords
        // because Fl_Tabs children use window-absolute coordinates.
        // Width follows m_tabs (narrower in preview mode).
        t.page->resize(0, pageY, m_tabs->w(), pageH);
        // Editor fills the full editor area too. Coordinates are in
        // WINDOW space (same convention as the page), so the editor
        // shares the same origin — no gap between tab strip and editor.
        t.editor->resize(0, pageY, m_tabs->w(), pageH);
        // Toggle the built-in close button along with multi-tab mode.
        if (multi) {
            t.page->when((uchar)(t.page->when() | FL_WHEN_CLOSED));
        } else {
            t.page->when((uchar)(t.page->when() & ~FL_WHEN_CLOSED));
        }
    }

    // ---- Bottom chrome (find bar + go-to bar + status bar) ----
    if (m_status) {
        if (m_showStatusbar) {
            m_status->show();
            m_status->resize(0, statusY, w(), STATUS_H);
        } else {
            m_status->hide();
        }
    }

    // Position visible bars at their correct y; hidden bars are left
    // untouched so they keep their last visible position and activate()
    // finds them at the right spot.
    if (m_goToBar && m_goToBar->visible()) {
        const int goToY = statusY - GOTOBAR_H;
        m_goToBar->resize(0, goToY, w(), GOTOBAR_H);
        m_goToBar->layout(w());
    }

    if (m_findBar && m_findBar->visible()) {
        // Find sits above GoTo when GoTo is visible, or directly above
        // the status bar when GoTo is hidden.
        const int findY = (m_goToBar && m_goToBar->visible())
                         ? statusY - GOTOBAR_H - FINDBAR_H
                         : statusY - FINDBAR_H;
        m_findBar->resize(0, findY, w(), FINDBAR_H);
        m_findBar->layout(w());
    }

    // ---- Top chrome (title bar + menu bar + tools bar) ----
    if (m_titleBar) m_titleBar->resize(0, 0, w(), TITLE_H);
    if (m_menu) {
        if (m_showMenuBar) {
            m_menu->show();
            m_menu->resize(0, TITLE_H, w(), MENU_H);
        } else {
            m_menu->hide();
        }
    }
    if (m_scriptBar) {
        if (m_showScriptBar) {
            m_scriptBar->show();
            m_scriptBar->resize(0, TITLE_H + menuBarH, w(), SCRIPTBAR_H);
        } else {
            m_scriptBar->hide();
        }
    }

    // Force Fl_Tabs to recompute its tab layout for the new geometry.
    m_tabs->redraw();
    // Full window redraw: find bar / status bar / go-to bar are direct
    // children of the window (not of m_tabs), so m_tabs->redraw() alone
    // does not invalidate their old/new positions after a structural
    // change (e.g. toggling the status bar while the find bar is open).
    // Without this, the find bar can be partially obscured by the
    // re-shown status bar until the next hover-triggered局部 redraw.
    redraw();
}

// --------------------------------------------------------------------------
// Tab management
void MainWindow::switchToTab(int index) {
    if (index < 0 || index >= (int)m_tabsList.size()) return;
    m_tabs->value(m_tabsList[index].page);
    Fl::focus(m_tabsList[index].editor);
    // Start the new tab's cursor in the visible phase.
    m_cursorVisible = true;
    m_tabsList[index].editor->show_cursor(1);

    // Closing the tab we came from may have left FindBar's m_editor / its
    // captured m_countText pointing at an already-deleted editor/buffer.
    // Rather than re-syncing them to the new tab (which would silently carry
    // over a search started on a different buffer), hide the bars; the
    // user reopens them via Ctrl+F / Ctrl+G and they bind to this tab.
    bool barHidden = false;
    if (m_findBar && m_findBar->visible()) {
        m_findBar->clearMatchHighlight();
        m_findBar->hide();
        barHidden = true;
    }
    if (m_goToBar && m_goToBar->visible()) {
        m_goToBar->hide();
        barHidden = true;
    }
    if (barHidden) layoutTabs();

    // Markdown 预览跟随当前标签：立即刷新
    if (m_previewActive) refreshPreview();

    updateTitle();
    updateStatusBar();
    syncTitleBar();
}

void MainWindow::updateTabLabel(int index) {
    if (index < 0 || index >= (int)m_tabsList.size()) return;
    Document *doc = m_tabsList[index].doc;
    char buf[FL_PATH_MAX + 8];
    const char *path = doc->filePath();
    const char *name = (path && path[0]) ? fl_filename_name(path) : "Unnamed";
    if (doc->isDirty())
        snprintf(buf, sizeof(buf), "*%s", name);
    else
        snprintf(buf, sizeof(buf), "%s", name);
    m_tabsList[index].page->copy_label(buf);
    // Redraw the title bar tab strip (since tabs are drawn by TitleBar now).
    syncTitleBar();
}

void MainWindow::syncTitleBar() {
    if (!m_titleBar) return;
    std::vector<TitleBar::TabInfo> tabs;
    tabs.reserve(m_tabsList.size());
    for (auto &t : m_tabsList) {
        const char *label = t.page->label();
        TitleBar::TabInfo info;
        info.label = label ? label : "Unnamed";
        tabs.push_back(std::move(info));
    }
    m_titleBar->setTabData(tabs, activeTabIndex());
    m_titleBar->setMultiTab(m_cfg->getMultiTab());
    m_titleBar->setMaximized(m_maximized);
    m_titleBar->setPinned(alwaysOnTop());
    m_titleBar->redraw();
}

