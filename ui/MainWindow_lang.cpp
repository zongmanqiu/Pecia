// MainWindow_lang.cpp - MainWindow language switching and Recent Files
// menu. Moved here from MainWindow.cpp so the main window file stays
// manageable. Member functions defined in this translation unit are part
// of the same MainWindow class declared in ui/MainWindow.h.
#include "ui/MainWindow.h"

#include "editor/Document.h"
#include "editor/Editor.h"
#include "core/Config.h"
#include "core/Theme.h"
#include "core/I18n.h"
#include "ui/Layout.h"
#include "ui/GoTo.h"
#include "ui/FindReplace.h"
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/fl_draw.H>
#include <FL/filename.H>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

// blending with the title-bar strip.
// all languages, making them a stable identifier.
void MainWindow::cbSetLanguage(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self) return;
    Fl_Menu_Item *item = (Fl_Menu_Item *)self->m_menu->mvalue();
    if (!item || !item->text) return;
    // Identify the picked language by matching the label text. strstr()
    // is used so the leading 4-space indent (added by g_menu for
    // alignment) doesn't get in the way.
    const char *code = nullptr;
    if (strstr(item->text, "English") != nullptr) {
        code = "en";
    } else if (strstr(item->text, "中文") != nullptr) {
        code = "zh-CN";
    }
    if (!code) return;
    // Defer the actual reload + retranslation: FLTK's pulldown menu
    // window caches Fl_Menu_Item pointers and may still touch item->text
    // after our callback returns. If we rewrite text now we risk
    // invalidating those cached pointers and crashing. Schedule a
    // zero-timeout callback instead so the work runs on the next event
    // loop tick, after FLTK has finished tearing down the menu window.
    self->m_pendingLangCode = code;
    Fl::remove_timeout(s_applyLangDeferred, self);
    Fl::add_timeout(0.0, s_applyLangDeferred, self);
}

void MainWindow::s_applyLangDeferred(void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self || !self->m_pendingLangCode) return;
    const char *code = self->m_pendingLangCode;
    self->m_pendingLangCode = nullptr;
    if (!I18n::load(code)) {
        // If the requested language file is missing, fall back to English
        // so the UI stays usable rather than showing raw keys.
        I18n::load("en");
    }
    self->m_cfg->setLang(code);
    // Re-translate the menu bar so the new language takes effect
    // immediately - without this, only the radio check mark moves and
    // the labels stay in the previous language.
    self->applyLanguageToMenu();
    self->updateStatusBar();
    // Refresh the find/replace bar labels as well.
    if (self->m_findBar) self->m_findBar->refreshLabels();
    if (self->m_goToBar) self->m_goToBar->refreshLabels();
    // Rebuild the script bar: script display names come from each
    // script's ==Meta== block (@name.<current language>), which depends
    // on the new language. The rebuild also refreshes the script
    // shortcut registry.
    self->rebuildScriptBar();
}

// Rebuild the Recent Files submenu from Config. Allocates a new
// Fl_Menu_Item array (m_recentMenu) sized to recentCount + 2 (one for
// each entry plus the trailing nullptr terminator, or a single disabled
// "(empty)" entry + terminator when there are no recent files).
//
// The FL_SUBMENU_POINTER flag on the parent "Recent Files" item tells
// FLTK to read its user_data() as a pointer to this submenu array.
void MainWindow::refreshRecentMenu() {
    // Free the previous array and its _strdup'd strings (if any).
    if (m_recentMenu) {
        for (int i = 0; i < m_recentMenuSize; ++i) {
            if (m_recentMenu[i].text) free((void *)m_recentMenu[i].text);
        }
        delete[] m_recentMenu;
        m_recentMenu = nullptr;
    }
    m_recentMenuSize = 0;

    int n = m_cfg->recentCount();
    if (n <= 0) {
        // Single disabled "(empty)" entry + nullptr terminator
        m_recentMenuSize = 2;
        m_recentMenu = new Fl_Menu_Item[m_recentMenuSize];
        memset(m_recentMenu, 0, sizeof(Fl_Menu_Item) * m_recentMenuSize);
        m_recentMenu[0].text = _strdup("(empty)");
        m_recentMenu[0].flags = FL_MENU_INACTIVE;
    } else {
        m_recentMenuSize = n + 1;  // entries + nullptr terminator
        m_recentMenu = new Fl_Menu_Item[m_recentMenuSize];
        memset(m_recentMenu, 0, sizeof(Fl_Menu_Item) * m_recentMenuSize);
        for (int i = 0; i < n; ++i) {
            char path[FL_PATH_MAX];
            m_cfg->recentGet(i, path, sizeof(path));
            if (!path[0]) continue;
            const char *base = fl_filename_name(path);
            char label[FL_PATH_MAX + 64];
            // 5-space gap between index, filename, and path.
            if (i < 9) {
                snprintf(label, sizeof(label),
                         "%d     %s     %s",
                         i + 1, base ? base : path, path);
            } else {
                snprintf(label, sizeof(label),
                         "  %s     %s",
                         base ? base : path, path);
            }
            m_recentMenu[i].text = _strdup(label);
            m_recentMenu[i].callback_ = cbOpenRecent;
            m_recentMenu[i].user_data_ = reinterpret_cast<void *>((intptr_t)i);
        }
    }

    // Point the parent "Recent Files" entry's user_data at the new
    // array. We use the cached pointer from m_recentMenuItem (captured
    // in the constructor while the menu was still in English) instead
    // of find_item() - the English path lookup would fail once the
    // menu has been translated to Chinese, leaving the parent pointing
    // at freed memory (use-after-free crash on the next submenu open).
    if (m_recentMenuItem) {
        m_recentMenuItem->user_data(m_recentMenu);
    }
    m_menu->redraw();
}

void MainWindow::cbOpenRecent(Fl_Widget *w, void *data) {
    // `w` is the Fl_Menu_Bar that fired the callback; its parent is the
    // MainWindow. `data` is the per-item user_data we stored in
    // refreshRecentMenu() - the recent-files index (cast through intptr_t).
    Fl_Menu_Bar *mb = dynamic_cast<Fl_Menu_Bar *>(w);
    if (!mb) return;
    MainWindow *mw = dynamic_cast<MainWindow *>(mb->parent());
    if (!mw) return;
    intptr_t idx = reinterpret_cast<intptr_t>(data);
    if (idx < 0 || idx >= mw->m_cfg->recentCount()) return;
    char path[FL_PATH_MAX];
    mw->m_cfg->recentGet((int)idx, path, sizeof(path));
    if (!path[0]) return;
    mw->openFile(path);
}

// File > Clear Recent Files - wipe the recent-files list and the
// backing "recent file.ini" on disk, then refresh the submenu so the
// change is visible immediately.
void MainWindow::cbClearRecent(Fl_Widget *w, void * /*data*/) {
    Fl_Menu_Bar *mb = dynamic_cast<Fl_Menu_Bar *>(w);
    if (!mb) return;
    MainWindow *mw = dynamic_cast<MainWindow *>(mb->parent());
    if (!mw) return;
    mw->m_cfg->recentClear();
    mw->refreshRecentMenu();
}

