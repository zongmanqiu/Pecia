// MainWindow_toolbar.cpp - MainWindow toolbar implementation.
// The tools bar is a HoverMenuBar: every top-level item is one script
// button - a "Scripts" (Misc) item for loose .lua files at the root of
// the exe-adjacent script/ folder, and one item per level-1 subfolder
// whose popup lists that folder's scripts (level-2 folders become
// nested submenus). Being a real menu bar gives the toolbar the same
// hover/click behaviour as the main menu bar: clicking one item pops
// its menu, and moving the mouse to another item switches menus.
// Member functions defined in this translation unit are part of the
// same MainWindow class declared in ui/MainWindow.h.
#include "ui/MainWindow.h"

#include "editor/Document.h"
#include "editor/Editor.h"
#include "core/Theme.h"
#include "core/Config.h"
#include "core/I18n.h"
#include "core/OpLog.h"
#include "ui/Layout.h"
#include "ui/HoverMenuBar.h"
#include "script/ScriptManager.h"
#include <FL/fl_draw.H>
#include <string.h>

Fl_Menu_Item *MainWindow::menuClickedItem(Fl_Widget *w) {
    Fl_Menu_ *m = dynamic_cast<Fl_Menu_ *>(w);
    return m ? (Fl_Menu_Item *)m->mvalue() : nullptr;
}

// Build the script toolbar from the script/ folder scan. The toolbar is
// a HoverMenuBar; its menu array is one FL_SUBMENU item per button:
//   - "Scripts" (Misc, i18n scriptbar.scripts) - root .lua files. When the
//     root has no scripts the submenu shows a single disabled
//     "(no scripts)" entry so the item is never empty.
//   - one item per level-1 subfolder - that folder's scripts, with
//     level-2 subfolders as nested FL_SUBMENUs (each with its own null
//     terminator so FLTK's submenu walk never overruns the array).
// The old array is freed, then the new one is attached via menu() -
// no widgets are created or destroyed here (deleting/recreating
// Fl_Menu_Button widgets during construction corrupts the heap,
// 0xC0000005), so rebuilds at any time are safe.
void MainWindow::rebuildScriptBar() {
    if (!m_scriptBar) return;

    if (m_scriptBarMenu) {
        // m_scriptBarMenu contains nested FL_SUBMENUs, each ended by an
        // FLTK null entry - so the first null `.text` is NOT the array end.
        // Walk the full recorded element count to free every _strdup'd item.
        for (int i = 0; i < m_scriptBarMenuCount; ++i) {
            free((void *)m_scriptBarMenu[i].text);
            if (m_scriptBarMenu[i].user_data_) free(m_scriptBarMenu[i].user_data_);
        }
        delete[] m_scriptBarMenu;
        m_scriptBarMenu = nullptr;
        m_scriptBarMenuCount = 0;
    }

    ScriptCatalog cat = scriptManagerScan();
    if (cat.rootPath.empty()) {
        m_scriptBar->menu(nullptr);
        return;   // no script/ folder -> no toolbar items
    }

    const Theme &theme = m_theme;
    int fontSize = m_cfg->getUiFontSize();

    // Count items: 1 "Scripts" header + root scripts (or 1 placeholder)
    // + null, then per group (1 header + scripts + null, plus per
    // level-2 submenu (1 header + scripts + null)).
    int itemCount = 0;
    // "Scripts" submenu: header + (root scripts or 1 placeholder) + null
    itemCount += 1 + (cat.rootScripts.empty() ? 1 : (int)cat.rootScripts.size()) + 1;
    for (const auto &g : cat.groups) {
        itemCount += 1 + (int)g.scripts.size() + 1;
        for (const auto &sg : g.subGroups)
            itemCount += 1 + (int)sg.scripts.size() + 1;
    }
    ++itemCount;   // final null terminator

    Fl_Menu_Item *menu = new Fl_Menu_Item[itemCount]();
    int mi = 0;

    // "Scripts" (Misc) item: always present as the default collector for
    // loose .lua files at the script/ folder root.
    const char *scriptsLabel = I18n::get("scriptbar.scripts");
    if (!scriptsLabel || !*scriptsLabel) scriptsLabel = "Scripts";
    menu[mi].text = _strdup(scriptsLabel);
    menu[mi].flags = FL_SUBMENU;
    ++mi;
    if (cat.rootScripts.empty()) {
        menu[mi].text = _strdup(I18n::get("scriptbar.scripts.none"));
        menu[mi].flags = FL_MENU_INACTIVE;
        ++mi;
    } else {
        for (const auto &name : cat.rootScripts) {
            std::string disp = scriptManagerDisplayName(name);
            menu[mi].text = _strdup(disp.c_str());
            menu[mi].callback(cbRunScript, _strdup(name.c_str()));
            applyScriptItemShortcut(&menu[mi], name.c_str());
            ++mi;
        }
    }
    ++mi;   // null terminator closes the "Scripts" submenu

    // One item per level-1 subfolder: submenu of the folder's scripts,
    // with level-2 subfolders as nested submenus.
    for (const auto &g : cat.groups) {
        menu[mi].text = _strdup((g.displayName.empty() ? g.name : g.displayName).c_str());
        menu[mi].flags = FL_SUBMENU;
        ++mi;
        for (const auto &s : g.scripts) {
            std::string rel = g.name + "/" + s;
            std::string disp = scriptManagerDisplayName(rel);
            menu[mi].text = _strdup(disp.c_str());
            menu[mi].callback(cbRunScript, _strdup(rel.c_str()));
            applyScriptItemShortcut(&menu[mi], rel.c_str());
            ++mi;
        }
        for (const auto &sg : g.subGroups) {
            menu[mi].text = _strdup((sg.displayName.empty() ? sg.name : sg.displayName).c_str());
            menu[mi].flags = FL_SUBMENU;
            ++mi;
            for (const auto &s : sg.scripts) {
                std::string rel = g.name + "/" + sg.name + "/" + s;
                std::string disp = scriptManagerDisplayName(rel);
                menu[mi].text = _strdup(disp.c_str());
                menu[mi].callback(cbRunScript, _strdup(rel.c_str()));
                applyScriptItemShortcut(&menu[mi], rel.c_str());
                ++mi;
            }
            ++mi;   // level-2 submenu null terminator
        }
        ++mi;   // per-group null terminator closes this submenu
    }
    // final null terminator already zero-initialised

    for (int i = 0; i < itemCount; ++i) {
        if (!menu[i].text) continue;
        menu[i].labeltype(FL_NORMAL_LABEL);
        menu[i].labelfont(FL_HELVETICA);
        menu[i].labelsize(fontSize);
        menu[i].labelcolor(theme.colors().textPrimary);
    }

    m_scriptBarMenu = menu;
    m_scriptBarMenuCount = itemCount;
    m_scriptBar->menu(menu);
    m_scriptBar->redraw();

    // Refresh the script shortcut dispatch registry (a new script may
    // have been added by the Save As -> reload pipe flow).
    applyShortcuts();
}

// Set a toolbar script item's shortcut from the shortcut.script.<rel>
// config key (empty when unset). The Lua-bar popup then displays the
// combo next to the script name.
void MainWindow::applyScriptItemShortcut(Fl_Menu_Item *item,
                                         const char *relPath) {
    if (!item || !relPath || !*relPath) return;
    std::string id = "script.";
    id += relPath;
    ShortcutCombo combo = shortcutComboFor(id.c_str(), 0, 0);
    item->shortcut(combo.key | combo.mods);
}

// Re-scan the exe-adjacent script/ folder and rebuild the script bar
// buttons (public wrapper around rebuildScriptBar for the pipe layer).
void MainWindow::reloadScriptBar() {
    rebuildScriptBar();
}

// Build the script bar. The bar is a HoverMenuBar widget created HERE
// (empty menu); rebuildScriptBar() later attaches the script menu
// array. Creating the widget once and only swapping menu() arrays on
// rebuild avoids the heap corruption (0xC0000005) that came from
// deleting/recreating Fl_Menu_Button widgets during construction.
void MainWindow::buildScriptBar() {
    int toolbarY = TITLE_H + MENU_H;
    m_scriptBar = new HoverMenuBar(0, toolbarY, w(), SCRIPTBAR_H);
    m_scriptBar->box(FL_FLAT_BOX);
    m_scriptBar->color(m_theme.colors().bgChrome);   // same as title bar
    m_scriptBar->selection_color(m_theme.colors().accentSelection);
    m_scriptBar->textcolor(m_theme.colors().textPrimary);
    m_scriptBar->textsize(m_cfg->getUiFontSize());
}

// Re-translate toolbar item labels after a language change. Only the
// "Scripts" (Misc) item carries a translated label (script items are
// file/folder names). The menu bar lays items out itself, so no
// re-flowing is needed.
void MainWindow::updateScriptBarLabels() {
    if (!m_scriptBar || !m_scriptBarMenu) return;
    const char *scriptsLabel = I18n::get("scriptbar.scripts");
    if (scriptsLabel && *scriptsLabel) {
        free((void *)m_scriptBarMenu[0].text);
        m_scriptBarMenu[0].text = _strdup(scriptsLabel);
    }
    m_scriptBar->redraw();
}

// View > Tools Bar - toggle the toolbar visibility.
// layoutTabs() recomputes the editor's top edge so hiding the toolbar
// reclaims its SCRIPTBAR_H pixels for the editor.
void MainWindow::cbToggleScriptBar(Fl_Widget *w, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self) return;
    Fl_Menu_Item *item = menuClickedItem(w);
    bool on = (item && (item->flags & FL_MENU_VALUE));
    self->m_showScriptBar = on;
    self->m_cfg->setShowToolbar(on);
    self->layoutTabs();
}

