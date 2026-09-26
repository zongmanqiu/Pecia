// MainWindow_view.cpp - MainWindow view-state toggles and dialogs
// (status bar, toolbar visibility, line highlight, whitespace, font
// selection, statistics, Go To Line). Moved here from MainWindow.cpp so
// the main window file stays manageable. Member functions defined in this
// translation unit are part of the same MainWindow class declared in
// ui/MainWindow.h.
#include "ui/MainWindow.h"

#include "editor/Document.h"
#include "editor/Editor.h"
#include "ui/FindReplace.h"
#include "ui/GoTo.h"
#include "ui/SettingsDialog.h"
#include "ui/ConfirmDialog.h"
#include "ui/InfoWindow.h"
#include "core/Config.h"
#include "core/Theme.h"
#include "core/I18n.h"
#include "core/UiBridge.h"
#include "ui/Layout.h"
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/fl_ask.H>
#include <FL/filename.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

// based on its buffer's line count. The width is derived from the
// number of digits in the maximum line number, measured with the
// editor's current font. Only updates when the width actually changes
// to avoid unnecessary redraws on every keystroke.
void MainWindow::s_updateLinenumberWidthCb(void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (self) self->updateLinenumberWidth();
}

void MainWindow::updateLinenumberWidth() {
    if (!m_cfg->getLineNumbers()) return;
    for (auto &t : m_tabsList) {
        if (!t.editor || !t.doc) continue;
        Fl_Text_Buffer *buf = t.doc->buffer();
        if (!buf) continue;
        int lines = t.doc->lineCount();   // cached: avoids a full-buffer scan per font change
        if (lines < 1) lines = 1;
        // Count digits in the line number
        int digits = 1;
        for (int n = lines; n >= 10; n /= 10) digits++;
        // Measure width of a string of that many '9' digits (widest digit)
        fl_font(t.editor->linenumber_font(), t.editor->linenumber_size());
        char sample[16];
        memset(sample, '9', digits);
        sample[digits] = '\0';
        double textW = fl_width(sample);
        // 3px padding on each side (matching FLTK's internal layout) + 2 extra
        int newWidth = (int)textW + 8;
        int curWidth = t.editor->linenumber_width();
        if (curWidth != newWidth) {
            t.editor->linenumber_width(newWidth);
            t.editor->redraw();
        }
    }
}

// View > Status Bar - toggle the bottom status bar visibility.
// Called from both the main menu bar and the right-click context menu.
// We always read the current config value and toggle it, because the
// FL_MENU_VALUE flag on popup menu items is unreliable (FLTK's internal
// popup copy is not the same array as menuBar()->menu()).
void MainWindow::cbToggleStatusbar(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self) return;
    bool on = !self->m_cfg->getShowStatusbar();
    self->m_showStatusbar = on;
    self->m_cfg->setShowStatusbar(on);
    // Sync the main menu bar checkmark
    if (self->m_statusbarItem) { if (on) self->m_statusbarItem->set(); else self->m_statusbarItem->clear(); }
    self->layoutTabs();
}

// View > Menu Bar - toggle the menu bar visibility.
void MainWindow::cbToggleMenuBar(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self) return;
    bool on = !self->m_cfg->getShowMenuBar();
    self->m_showMenuBar = on;
    self->m_cfg->setShowMenuBar(on);
    if (self->m_menuBarItem) { if (on) self->m_menuBarItem->set(); else self->m_menuBarItem->clear(); }
    self->layoutTabs();
}

// Query the current always-on-top state from config. The state is now
// controlled solely by the title bar pin button (the View menu item was
// removed), so we read it from Config instead of a menu toggle item.
bool MainWindow::alwaysOnTop() const {
    return m_cfg->getAlwaysOnTop();
}

// Apply always-on-top: persist to config, set the Win32 HWND_TOPMOST
// flag, and refresh the title bar pin button so it reflects the new
// state. Called from the title bar pin button.
void MainWindow::setAlwaysOnTop(bool on) {
    m_cfg->setAlwaysOnTop(on);
#if defined(_WIN32)
    HWND hwnd = fl_xid(this);
    if (hwnd) {
        SetWindowPos(hwnd,
                     on ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
#endif
    syncTitleBar();
}

// View > Highlight Current Line - toggle current line highlight.
void MainWindow::cbToggleHighlightLine(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self) return;
    bool on = !self->m_cfg->getHighlightCurrentLine();
    self->m_cfg->setHighlightCurrentLine(on);
    if (self->m_highlightLineItem) { if (on) self->m_highlightLineItem->set(); else self->m_highlightLineItem->clear(); }
    for (auto &t : self->m_tabsList) {
        t.editor->setLineHighlight(on);
    }
}

// View > Show Space Symbol - toggle space-symbol dots.
void MainWindow::cbToggleShowWhitespace(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self) return;
    bool on = !self->m_cfg->getShowWhitespace();
    self->m_cfg->setShowWhitespace(on);
    if (self->m_showWhitespaceItem) { if (on) self->m_showWhitespaceItem->set(); else self->m_showWhitespaceItem->clear(); }
    for (auto &t : self->m_tabsList) {
        t.editor->setShowSpaceSymbols(on);
    }
}

// View > Font - radio submenu, select editor font face.
void MainWindow::cbSelectFont(Fl_Widget *w, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self) return;
    Fl_Menu_Item *item = menuClickedItem(w);
    if (!item || !item->label()) return;
    // Skip leading spaces in the label (submenu items use 8-space indent)
    const char *fontName = item->label();
    while (*fontName == ' ') ++fontName;
    self->m_cfg->setEditorFont(fontName);
    for (auto &t : self->m_tabsList) {
        t.editor->setFontFace(fontName);
    }
    // 预览跟随编辑器字体：字体名写进了注入 CSS，文档内容没变但样式变了，
    // 必须先清掉"内容未变则跳过"的记忆再刷新（与 togglePreview 同法）。
    if (self->m_preview && self->m_previewActive) {
        self->m_lastRenderedMd.clear();
        self->refreshPreview();
    }
}

// View > Statistics... - shows detailed document statistics.
void MainWindow::cbStatistics(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self || !self->activeTab()) return;
    Fl_Text_Buffer *buf = self->activeTab()->doc->buffer();
    const char *text = buf->text();
    int len = buf->length();

    int totalChars = len;
    int charsNoSpace = 0;
    int cjkChars = 0;
    int words = 0;
    int paragraphs = 0;
    int lines = 0;
    int emptyLines = 0;

    bool inWord = false;
    bool inParagraph = false;
    bool prevWasNewline = false;
    bool lineHasContent = false;   // current line has any non-whitespace

    int i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)text[i];

        if (c == '\n') {
            lines++;
            if (!lineHasContent) emptyLines++;   // blank line (empty or whitespace-only)
            if (inParagraph && prevWasNewline) {
                paragraphs++;
                inParagraph = false;
            }
            prevWasNewline = true;
            lineHasContent = false;
            i++;
            continue;
        }
        prevWasNewline = false;

        if (c != ' ' && c != '\t' && c != '\r') {
            charsNoSpace++;
            inParagraph = true;
            lineHasContent = true;
        }

        if (c >= 0x80) {
            int seqLen = 1;
            if ((c & 0xE0) == 0xC0) seqLen = 2;
            else if ((c & 0xF0) == 0xE0) seqLen = 3;
            else if ((c & 0xF8) == 0xF0) seqLen = 4;

            if (seqLen >= 3 && i + seqLen <= len) {
                unsigned int cp = 0;
                if (seqLen == 3) {
                    cp = ((c & 0x0F) << 12) |
                         (((unsigned char)text[i+1] & 0x3F) << 6) |
                         ((unsigned char)text[i+2] & 0x3F);
                } else if (seqLen == 4) {
                    cp = ((c & 0x07) << 18) |
                         (((unsigned char)text[i+1] & 0x3F) << 12) |
                         (((unsigned char)text[i+2] & 0x3F) << 6) |
                         ((unsigned char)text[i+3] & 0x3F);
                }
                if ((cp >= 0x4E00 && cp <= 0x9FFF) ||
                    (cp >= 0x3400 && cp <= 0x4DBF) ||
                    (cp >= 0x20000 && cp <= 0x2A6DF) ||
                    (cp >= 0x3000 && cp <= 0x303F) ||
                    (cp >= 0xFF00 && cp <= 0xFFEF) ||
                    (cp >= 0x2E80 && cp <= 0x2EFF) ||
                    (cp >= 0x31C0 && cp <= 0x31EF) ||
                    (cp >= 0xF900 && cp <= 0xFAFF) ||
                    (cp >= 0xAC00 && cp <= 0xD7AF) ||
                    (cp >= 0x3040 && cp <= 0x309F) ||
                    (cp >= 0x30A0 && cp <= 0x30FF)) {
                    cjkChars++;
                }
            }
            i += seqLen;
            if (inWord) inWord = false;
            continue;
        }

        bool isWordChar = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '\'');
        if (isWordChar) {
            if (!inWord) {
                inWord = true;
                words++;
            }
        } else {
            inWord = false;
        }
        i++;
    }
    if (len > 0 && text[len-1] != '\n') {
        lines++;
        if (!lineHasContent) emptyLines++;
    }
    if (inParagraph) paragraphs++;
    free((void *)text);

    InfoWindow iw;
    iw.setTheme(&self->m_theme, self->m_cfg->getUiFontSize());
    iw.setTitle(I18n::get("statistics.title"));
    iw.setWidth(320);
    iw.setRows({
        { I18n::get("statistics.characters"),        std::to_string(totalChars) },
        { I18n::get("statistics.wordchars"),         std::to_string(charsNoSpace) },
        { I18n::get("statistics.cjk"),               std::to_string(cjkChars) },
        { I18n::get("statistics.words"),             std::to_string(words) },
        { I18n::get("statistics.paragraphs"),        std::to_string(paragraphs) },
        { I18n::get("statistics.emptylines"),        std::to_string(emptyLines) },
        { I18n::get("statistics.lines"),             std::to_string(lines) }
    });
    iw.show();
}

// Edit > Go To... - shows the Go To bar
void MainWindow::cbGotoLine(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self) return;
    Tab *t = self->activeTab();
    if (!t) return;

    if (self->m_findBar && self->m_findBar->visible()) {
        self->m_findBar->clearMatchHighlight();
        self->m_findBar->hide();
    }

    self->m_goToBar->setEditor(t->editor);
    self->m_goToBar->activate();
    self->layoutTabs();
}

// Tools > Lua Script... - lazily create and show the script dialog.
