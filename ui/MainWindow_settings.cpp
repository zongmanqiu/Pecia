// MainWindow_settings.cpp - MainWindow settings persistence and
// application (saveSettings, syncFromConfig, applySettings, scheme,
// menu keys/language). Moved here from MainWindow.cpp so the main window
// file stays manageable. Member functions defined in this translation
// unit are part of the same MainWindow class declared in ui/MainWindow.h.
#include "ui/MainWindow.h"

#include "editor/Document.h"
#include "editor/Editor.h"
#include "ui/FindReplace.h"
#include "ui/GoTo.h"
#include "ui/SettingsDialog.h"
#include "ui/ShortcutDialog.h"
#include "ui/ConfirmDialog.h"
#include "ui/TitleBar.h"
#include "ui/HoverMenuBar.h"
#include "core/Config.h"
#include "core/Theme.h"
#include "core/I18n.h"
#include "core/UiBridge.h"
#include "script/ScriptManager.h"
#include "ui/Layout.h"
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Output.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
#include <FL/fl_string_functions.h>
#include <FL/Fl_Tabs.H>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

void MainWindow::applyScheme(const char *name) {
    if (name && *name) {
        Fl::scheme(name);
    }
}

// Build the (menu item pointer -> I18n key) mapping table by looking
// up each item via its original English path. Must be called *before*
// applyLanguageToMenu() so find_item() can still match the English
// labels in g_menu. After this runs we hold raw pointers into the
// menu array, which stay valid for the lifetime of m_menu.
void MainWindow::buildMenuKeys() {
    struct Entry { const char *path; const char *key; };
    static const Entry entries[] = {
        { "File",                          "menu.file" },
        { "File/New",                "menu.file.new" },
        { "File/New Window",         "menu.file.newwindow" },
        { "File/Open...",             "menu.file.open" },
        { "File/Save",                "menu.file.save" },
        { "File/Save As...",          "menu.file.saveas" },
        { "File/Recent Files",        "menu.file.recent" },
        { "File/Clear Recent Files",   "menu.file.clearrecent" },
        { "File/Exit",                "menu.file.exit" },

        { "Edit",                          "menu.edit" },
        { "Edit/Undo",               "menu.edit.undo" },
        { "Edit/Redo",               "menu.edit.redo" },
        { "Edit/Cut",                "menu.edit.cut" },
        { "Edit/Copy",               "menu.edit.copy" },
        { "Edit/Paste",              "menu.edit.paste" },
        { "Edit/Delete",             "menu.edit.delete" },
        { "Edit/Find...",            "menu.edit.find" },
        { "Edit/Find Next",          "menu.edit.findnext" },
        { "Edit/Find Previous",      "menu.edit.findprev" },
        { "Edit/Replace...",         "menu.edit.replace" },
        { "Edit/Go To...",           "menu.edit.goto" },
        { "Edit/Select All",         "menu.edit.selectall" },
        { "Edit/Time/Date",           "menu.edit.timedate" },
        { "Edit/Search Selection",    "menu.edit.search" },
        { "Edit/Send to AI",          "menu.edit.sendai" },

        { "Markdown",                      "menu.markdown" },
        { "Markdown/Preview",              "menu.markdown.preview" },
        { "Markdown/Scroll Sync",          "menu.markdown.scrollsync" },
        { "Markdown/Refresh",              "menu.markdown.refresh" },
        { "Markdown/Auto Refresh",         "menu.markdown.autorefresh" },
        { "Markdown/Auto Refresh/Off",     "menu.markdown.off" },
        { "Markdown/Open in Browser",      "menu.markdown.openinbrowser" },

        { "View",                          "menu.view" },
        { "View/Script Bar",                 "menu.view.scriptbar" },
        { "View/Status Bar",              "menu.view.statusbar" },
        { "View/Line Numbers",            "menu.view.linenumbers" },
        { "View/Highlight Current Line",  "menu.view.highlightline" },
        { "View/Show Space Symbol",       "menu.view.showwhitespace" },
        { "View/Word Wrap",               "menu.view.wrap" },
        { "View/Font",                "menu.view.font" },
        { "View/Statistics...",       "menu.view.statistics" },
        { "View/Zoom",                "menu.view.zoom" },
        { "View/Zoom/        Zoom Out",    "menu.view.zoomout" },
        { "View/Zoom/        Zoom In",     "menu.view.zoomin" },
        { "View/Zoom/        Reset Zoom",  "menu.view.zoomreset" },
        { "View/Theme",            "menu.view.theme" },
        { "View/Multi Tab",               "menu.view.multitab" },

        { "Tools",                         "menu.tools" },
        { "Tools/Lua Script...",           "menu.tools.lua" },
        { "Tools/AI Chat...",              "menu.tools.aichat" },
        { "Tools/Reload Scripts",          "menu.tools.reloadscripts" },

        { "Settings",                      "menu.settings" },
        { "Settings/Options...",  "menu.settings.options" },
        { "Settings/Shortcuts...",    "menu.settings.shortcuts" },
        { "Settings/Open Script Folder...", "menu.settings.openscriptfolder" },
        { "Settings/Reload Scripts",  "menu.settings.reloadscripts" },
        { "Settings/Language",        "menu.settings.language" },

        { "Help",                          "menu.help" },
        { "Help/Check for Updates",   "menu.help.checkupdates" },
        { "Help/About",              "menu.help.about" },
    };
    m_menuKeys.clear();
    for (const auto &e : entries) {
        Fl_Menu_Item *item = (Fl_Menu_Item *)m_menu->find_item(e.path);
        if (item) {
            // A top-level menu bar entry has no "/" in its path
            // (e.g. "File", "View"). Nested items use "View/...".
            bool top = (strchr(e.path, '/') == nullptr);
            // Built-in default shortcut, captured BEFORE applyShortcuts()
            // overwrites the items with user config.
            m_menuKeys.push_back({ item, e.key, top, item->shortcut() });
        }
    }
}

// Re-translate every menu item via the currently-loaded I18n table.
// Toggle items (FL_MENU_TOGGLE) get the bare translated label (no
// leading space - the checkbox slot occupies the indent). All other
// items - normal items, FL_MENU_RADIO children, submenu headers - get
// the 4-space leading indent so their text aligns with toggle items'
// checkbox slot, matching the layout scheme documented in g_menu.
//
// Each translated label is stored in m_menuTexts (a std::vector<std::string>
// member) and the menu item's text pointer is pointed at the c_str().
// The vector is reserve()d up front so push_back doesn't relocate
// its std::string elements, keeping the c_str() pointers obtained in
// the second pass stable until the next call to this method.
void MainWindow::applyLanguageToMenu() {
    m_menuTexts.clear();
    m_menuTexts.reserve(m_menuKeys.size());
    // First pass: render each translated label into m_menuTexts.
    // We must not touch e.item->text yet - push_back may relocate the
    // std::string objects if the vector reallocates, invalidating any
    // c_str() pointers we already handed out.
    for (size_t i = 0; i < m_menuKeys.size(); ++i) {
        const MenuKeyEntry &e = m_menuKeys[i];
        const char *t = I18n::get(e.key);
        // Defensive fallback: if I18n returns empty (corrupted/missing
        // lang file, or I18n not yet loaded), use the key itself so the
        // menu item never ends up with a dangling text pointer after
        // the m_menuTexts.clear() above invalidated the previous one.
        if (!t || !*t) t = e.key;
        // Leading-space rules:
        //   - Top-level menu bar entries (File, Edit, View, ...):
        //     no leading spaces. Identified by isTopLevel (path has
        //     no "/"), NOT by FL_SUBMENU - because nested submenu
        //     headers like "Zoom" also use FL_SUBMENU.
        //   - FL_MENU_TOGGLE / FL_MENU_RADIO items reserve their own
        //     checkbox/radio slot. No leading spaces.
        //   - All items get the bare translated label.
        char buf[160];
        fl_strlcpy(buf, t, sizeof(buf));
        m_menuTexts.push_back(std::string(buf));
    }
    // Second pass: now that all std::string objects live at stable
    // addresses (vector won't grow further), hand each menu item a
    // pointer into its corresponding string's buffer. We assign
    // unconditionally (no empty-skip) so every item points at valid
    // memory after this call - the previous m_menuTexts.clear() above
    // already invalidated whatever the items were pointing at.
    for (size_t i = 0; i < m_menuKeys.size(); ++i) {
        m_menuKeys[i].item->text = m_menuTexts[i].c_str();
    }
    m_menu->redraw();
    // Re-translate toolbar button labels too (File, Edit, View, ...).
    updateScriptBarLabels();
}

// Re-apply the active theme colors to the whole main window: chrome, menus,
// status bar, tabs, and every open editor. Called after a preset switch.
void MainWindow::applyThemeColors() {
    m_theme.load(*m_cfg);   // re-read theme.name + any ini overrides

    // Window root / chrome
    color(m_theme.colors().bgChrome);
    m_titleBar->color(m_theme.colors().bgChrome);
    m_titleBar->setTheme(&m_theme);
    m_menu->color(m_theme.colors().bgPanel);
    m_menu->textcolor(m_theme.colors().textPrimary);
    m_menu->selection_color(m_theme.colors().accentSelection);
    for (int i = 0; i < m_menu->size(); ++i) {
        Fl_Menu_Item *item = (Fl_Menu_Item *)m_menu->menu() + i;
        if (item->text) item->labelcolor(m_theme.colors().textPrimary);
    }
    m_menu->redraw();
    m_status->color(m_theme.colors().bgChrome);
    m_status->textcolor(m_theme.colors().textPrimary);
    m_status->redraw();
    if (m_scriptBar) {
        m_scriptBar->color(m_theme.colors().bgChrome);
        m_scriptBar->selection_color(m_theme.colors().accentSelection);
        m_scriptBar->textcolor(m_theme.colors().textPrimary);
        // Re-colour every script-bar menu item label too, then force a
        // redraw so the toolbar updates immediately (not just on relaunch).
        for (int i = 0; i < m_scriptBar->size(); ++i) {
            Fl_Menu_Item *item = (Fl_Menu_Item *)m_scriptBar->menu() + i;
            if (item->text) item->labelcolor(m_theme.colors().textPrimary);
        }
        m_scriptBar->redraw();
    }
    // In-editor bars pick up the new palette too.
    if (m_findBar) m_findBar->setTheme(&m_theme);
    if (m_goToBar) m_goToBar->setTheme(&m_theme);
    // Tabs background should match the editor so the selection border blends in.
    m_tabs->color(m_theme.colors().bgEditor);
    m_tabs->selection_color(m_theme.colors().bgEditor);

    // Every open editor: theme colors + rebuilt highlight style table.
    for (auto &t : m_tabsList) {
        if (t.page) t.page->color(m_theme.colors().bgEditor);   // tab page
        t.editor->setTheme(&m_theme);
        t.editor->color(m_theme.colors().bgEditor);
        t.editor->reapplyHighlightData();   // also re-applies line highlight
        t.editor->redraw();
    }

    redraw();
}

// View > Theme - switch the color theme. Each radio item's label IS the
// theme file stem (theme name), so pick it directly. The switch is deferred
// to the next loop tick so FLTK has torn down the pulldown menu first.
void MainWindow::cbSetTheme(Fl_Widget * /*w*/, void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self || !self->m_cfg) return;
    Fl_Menu_Item *item = (Fl_Menu_Item *)self->m_menu->mvalue();
    if (!item || !item->text || !*item->text) return;
    std::string name;
    for (const char *s = item->text; *s; ++s)   // trim indent / keep stem
        if (*s != ' ') name += *s;
    if (name.empty()) return;
    self->m_pendingTheme = name;
    Fl::remove_timeout(s_applyThemeDeferred, self);
    Fl::add_timeout(0.0, s_applyThemeDeferred, self);
}

void MainWindow::s_applyThemeDeferred(void *data) {
    MainWindow *self = static_cast<MainWindow *>(data);
    if (!self || self->m_pendingTheme.empty()) return;
    const std::string name = self->m_pendingTheme;
    self->m_pendingTheme.clear();
    self->m_theme.setPreset(name.c_str(), *self->m_cfg);   // persist theme.name
    self->m_theme.save(*self->m_cfg);                      // persist the palette
    self->applyThemeColors();
}

// --------------------------------------------------------------------------
// Find helpers
// ---------------------------------------------------------------------------

void MainWindow::showFindBar(bool withReplace) {
    Tab *t = activeTab();
    if (!t) return;
    m_findBar->setEditor(t->editor);

    if (m_goToBar && m_goToBar->visible()) {
        m_goToBar->hide();
    }

    const char *initial = nullptr;
    Fl_Text_Buffer *buf = t->doc->buffer();
    int start, end;
    if (buf->selection_position(&start, &end) && end - start < 256) {
        char *sel = buf->selection_text();
        if (sel && *sel) {
            initial = sel;
        } else if (sel) {
            ::free(sel);
        }
    }
    m_findBar->activate(initial, withReplace);
    if (initial) ::free((void *)initial);
    layoutTabs();
}

void MainWindow::showGoToBar() {
    Tab *t = activeTab();
    if (!t) {
        // Fallback: use the first tab if activeTab is temporarily null.
        if (!m_tabsList.empty()) {
            t = &m_tabsList[0];
            m_tabs->value(t->page);
        }
    }
    if (!t) return;

    if (m_findBar && m_findBar->visible()) {
        m_findBar->clearMatchHighlight();
        m_findBar->hide();
    }

    m_goToBar->setEditor(t->editor);
    // show() BEFORE layoutTabs so the bar is visible and gets
    // positioned at its correct y (above status bar), rather than
    // keeping a stale position from construction.
    m_goToBar->show();
    layoutTabs();
    m_goToBar->activate();
}

// --------------------------------------------------------------------------
// Drag & drop, shortcuts, wheel zoom

void MainWindow::syncFromConfig() {
    if (!m_cfg) return;

    // Scheme
    char scheme[32];
    m_cfg->getScheme(scheme, sizeof(scheme), "gtk+");
    applyScheme(scheme);

    // Word Wrap
    bool wrap = m_cfg->getWrap();
    if (m_wrapItem && (m_wrapItem->flags & FL_MENU_VALUE) != (wrap ? FL_MENU_VALUE : 0)) {
        if (wrap) m_wrapItem->set(); else m_wrapItem->clear();
        for (auto &t : m_tabsList) {
            t.editor->wrap_mode(wrap ? Fl_Text_Display::WRAP_AT_BOUNDS
                                      : Fl_Text_Display::WRAP_NONE, 0);
        }
    }

    // Line Numbers
    bool ln = m_cfg->getLineNumbers();
    if (m_lineNumbersItem && (m_lineNumbersItem->flags & FL_MENU_VALUE) != (ln ? FL_MENU_VALUE : 0)) {
        if (ln) m_lineNumbersItem->set(); else m_lineNumbersItem->clear();
        for (auto &t : m_tabsList) {
            t.editor->linenumber_width(0);  // will be updated by updateLinenumberWidth()
        }
        if (ln) updateLinenumberWidth();
    }

    // Long line marker
    int llm = m_cfg->getLongLineMarker();
    for (auto &t : m_tabsList) {
        t.editor->setLongLineMarker(llm);
    }

    // Status Bar
    bool sb = m_cfg->getShowStatusbar();
    if (sb != m_showStatusbar) {
        m_showStatusbar = sb;
        if (m_statusbarItem) {
            if (sb) m_statusbarItem->set(); else m_statusbarItem->clear();
        }
        layoutTabs();
    }

    // Tools Bar
    bool tb = m_cfg->getShowToolbar();
    if (tb != m_showScriptBar) {
        m_showScriptBar = tb;
        if (m_scriptBarItem) {
            if (tb) m_scriptBarItem->set(); else m_scriptBarItem->clear();
        }
        layoutTabs();
    }

    // Multi Tab - setting now lives in the Settings dialog; the only
    // sync we need is to re-layout the tab strip in case another window
    // changed the value.
    layoutTabs();

    // Highlight Current Line
    bool hl = m_cfg->getHighlightCurrentLine();
    if (m_highlightLineItem && (m_highlightLineItem->flags & FL_MENU_VALUE) != (hl ? FL_MENU_VALUE : 0)) {
        if (hl) m_highlightLineItem->set(); else m_highlightLineItem->clear();
    }

    // Show Space Symbol
    bool sw = m_cfg->getShowWhitespace();
    if (m_showWhitespaceItem && (m_showWhitespaceItem->flags & FL_MENU_VALUE) != (sw ? FL_MENU_VALUE : 0)) {
        if (sw) m_showWhitespaceItem->set(); else m_showWhitespaceItem->clear();
    }

    // Editor font - sync radio selection and apply to editors
    char cfgFont[64];
    m_cfg->getEditorFont(cfgFont, sizeof(cfgFont));
    // 旧配置可能引用已从字体列表移除的字体（如 Cascadia Mono）:
    // 启动/同步时自动迁移回默认 Consolas，避免静默落到 Courier 回退。
    if (cfgFont[0]) {
        bool known = false;
        for (int i = 0; i < FontUtils::FONT_COUNT; ++i) {
            if (FontUtils::strCaseCmp(FontUtils::FONT_LIST[i], cfgFont) == 0) {
                known = true;
                break;
            }
        }
        if (!known) {
            m_cfg->setEditorFont("Consolas");
            snprintf(cfgFont, sizeof(cfgFont), "%s", "Consolas");
        }
    }
    for (auto &t : m_tabsList) {
        const char *curName = Fl::get_font_name(t.editor->textfont(), nullptr);
        if (!curName || FontUtils::strCaseCmp(curName, cfgFont) != 0) {
            t.editor->setFontFace(cfgFont);
        }
    }
    // Update font radio selection using cached first item pointer
    if (m_fontFirstItem) {
        for (int i = 0; i < FontUtils::FONT_COUNT; ++i) {
            Fl_Menu_Item *fi = m_fontFirstItem + i;
            if (!fi->label()) break;
            const char *label = fi->label();
            while (*label == ' ') ++label;
            if (FontUtils::strCaseCmp(label, cfgFont) == 0) {
                if (!(fi->flags & FL_MENU_VALUE)) {
                    m_menu->setonly(fi);
                }
                break;
            }
        }
    }

    // Always on Top
    bool aot = m_cfg->getAlwaysOnTop();
    if (alwaysOnTop() != aot) {
        setAlwaysOnTop(aot);
    }

    // Language
    char newLang[64];
    m_cfg->getLang(newLang, sizeof(newLang), "en");
    if (strcmp(I18n::currentCode(), newLang) != 0) {
        I18n::load(newLang);
        applyLanguageToMenu();
        updateTitle();
        updateStatusBar();
        if (m_findBar) m_findBar->refreshLabels();
    }

    // Shortcuts may have changed on disk (another Pecia process edited
    // them): rebuild menu labels + the dispatch registry, and rebuild
    // the script bar so script shortcut labels stay in sync.
    applyShortcuts();
    rebuildScriptBar();

    // settings.ini is the source of truth for colors, so re-apply the theme
    // whenever it changes on disk (e.g. another process switched themes or a
    // user hand-edited theme.* keys). Cheap: reload + recolour.
    applyThemeColors();

    redraw();
}

// --------------------------------------------------------------------------
// Keyboard shortcut configuration (Settings > Shortcuts...)
// --------------------------------------------------------------------------

// Effective combo for an action: an explicit shortcut.<id> value wins
// (an EMPTY value is a real "no shortcut" - cleared by the user), only
// a MISSING key falls back to the built-in default.
ShortcutCombo MainWindow::shortcutComboFor(const char *id, int defKey,
                                           unsigned defMods) {
    ShortcutCombo combo;
    char key[160];
    snprintf(key, sizeof(key), "shortcut.%s", id ? id : "");
    std::string text = m_cfg ? m_cfg->getShortcut(id) : "";
    if (m_cfg && m_cfg->hasKey(key)) {
        if (!text.empty()) shortcutParse(text, &combo);
    } else {
        combo.key = defKey;
        combo.mods = defMods;
    }
    return combo;
}

// (Re)build everything shortcut-related from the shortcut.* config keys:
//   - static handler actions (window ops)
//   - menu item shortcuts (popup labels + FLTK-native dispatch)
//   - toolbar script registry entries (dispatched via dispatchShortcut;
//     the toolbar menu item labels are applied by rebuildScriptBar())
void MainWindow::applyShortcuts() {
    m_shortcutRegistry.clear();
    m_shortcutHandlers.clear();

    auto bind = [this](const char *id, int defKey, unsigned defMods,
                       std::function<void()> fn) {
        ShortcutCombo combo = shortcutComboFor(id, defKey, defMods);
        if (combo.empty()) return;
        m_shortcutRegistry.set(id, combo.key, combo.mods);
        m_shortcutHandlers[id] = std::move(fn);
    };

    // Window-level operations (shared ids with the tool windows).
    bind("win.pin", 0, 0, [this]() { togglePin(); });
    bind("win.minimize", 0, 0, [this]() { minimizeWindow(); });
    bind("win.maximize", 0, 0, [this]() { toggleMaximize(); });
    bind("win.close", 0, 0, [this]() { cbWindowClose(nullptr, this); });
    bind("win.nexttab", FL_Tab, FL_CTRL, [this]() { cycleTab(+1); });
    bind("win.prevtab", FL_Tab, FL_CTRL | FL_SHIFT, [this]() { cycleTab(-1); });
    bind("win.closetab", 'w', FL_CTRL, [this]() { closeCurrentTab(); });

    // Menu actions: update the copied menu items' shortcuts. FLTK then
    // displays the new combo in every popup and dispatches it natively.
    for (const auto &e : m_menuKeys) {
        if (e.isTopLevel) continue;
        Fl_Menu_Item *item = e.item;
        if (!item->callback_) continue;             // headers / submenu-only
        if (item->flags & FL_MENU_RADIO) continue;  // font/language radios
        int defS = item->shortcut();
        ShortcutCombo combo =
            shortcutComboFor(e.key, defS & 0xffff, defS & 0x7fff0000);
        item->shortcut(combo.key | combo.mods);
        // Go To keeps its toggle-bar behavior as a handler action (the
        // menu callback only shows the bar).
        if (strcmp(e.key, "menu.edit.goto") == 0) {
            bind("menu.edit.goto", combo.key, combo.mods,
                 [this]() { toggleGoToBar(); });
        }
    }

    // Toolbar scripts: register a dispatch entry per script; the toolbar
    // menu items' shortcut labels are applied by rebuildScriptBar().
    ScriptCatalog cat = scriptManagerScan();
    if (!cat.rootPath.empty()) {
        auto addScript = [&](const std::string &rel) {
            std::string id = "script." + rel;
            ShortcutCombo combo = shortcutComboFor(id.c_str(), 0, 0);
            if (combo.empty()) return;
            m_shortcutRegistry.set(id, combo.key, combo.mods);
            m_shortcutHandlers[id] =
                [this, rel]() { cbRunScript(m_scriptBar, (void *)rel.c_str()); };
        };
        for (const auto &n : cat.rootScripts) addScript(n);
        for (const auto &g : cat.groups) {
            for (const auto &s : g.scripts) addScript(g.name + "/" + s);
            for (const auto &sg : g.subGroups)
                for (const auto &s : sg.scripts)
                    addScript(g.name + "/" + sg.name + "/" + s);
        }
    }

    if (m_menu) m_menu->redraw();
}

// Build the row list for the Shortcuts dialog: menu actions (from the
// captured menu-key table), toolbar scripts (from the folder scan) and
// the static actions (window ops + Lua console + AI chat).

namespace {

// "menu.file.save" -> "Menu > File > Save" (every segment translated
// via its own cumulative I18n key: menu.file, menu.file.save, ...).
std::string menuActionPath(const char *key) {
    std::string s = I18n::get("shortcut.group.menu");
    const char *p = strchr(key, '.');
    if (!p) return s;
    ++p;   // skip the leading "menu."
    std::string cum = "menu";
    while (p && *p) {
        const char *dot = strchr(p, '.');
        std::string seg = dot ? std::string(p, dot - p) : std::string(p);
        cum += "." + seg;
        const char *label = I18n::get(cum.c_str());
        s += " > ";
        s += (label && *label) ? label : seg;
        if (!dot) break;
        p = dot + 1;
    }
    return s;
}

// "Formatting/trim" -> "Toolbar > Formatting > trim". The KEY stays
// the raw relative path (stable across languages); only the displayed
// name is localized: folder segments use folder.ini display names,
// the script segment uses its ==Meta== @name.
std::string scriptActionPath(const std::string &rel,
                             const std::string &folderDisplay) {
    std::string s = I18n::get("shortcut.scriptbar");
    if (!folderDisplay.empty()) {
        s += " > ";
        s += folderDisplay;
    }
    std::string disp = scriptManagerDisplayName(rel);
    s += " > ";
    s += disp.empty() ? rel : disp;
    return s;
}

} // namespace

std::vector<ShortcutDialogRow> MainWindow::buildShortcutRows() {
    std::vector<ShortcutDialogRow> rows;
    rows.reserve(64);

    // Menu actions: id = the I18n menu key ("menu.file.save"); the
    // dialog stores them under "shortcut.<id>".
    for (const auto &e : m_menuKeys) {
        if (e.isTopLevel) continue;
        Fl_Menu_Item *item = e.item;
        if (!item->callback_) continue;
        if (item->flags & FL_MENU_RADIO) continue;
        // Excluded by design: Help menu (no shortcuts), Multi Tab (a
        // manual setting, not a shortcut target).
        if (strncmp(e.key, "menu.help.", 10) == 0) continue;
        if (strcmp(e.key, "menu.view.multitab") == 0) continue;
        ShortcutDialogRow r;
        r.id = e.key;
        r.name = menuActionPath(e.key);
        r.group = SG_MENU;
        // Factory default from g_menu (captured before applyShortcuts
        // applied user config), so "Restore Defaults" really restores
        // the built-in shortcuts.
        int s = e.defShortcut;
        r.defKey = s & 0xffff;
        r.defMods = s & 0x7fff0000;
        if (r.defKey >= 'A' && r.defKey <= 'Z') {
            r.defKey += 'a' - 'A';
            r.defMods |= FL_SHIFT;
        }
        rows.push_back(r);
    }

    // Toolbar scripts (dynamic): id = "script.<relative path>" so new
    // scripts appear here automatically after a save/reload.
    ScriptCatalog cat = scriptManagerScan();
    if (!cat.rootPath.empty()) {
        auto addScript = [&](const std::string &rel, const std::string &folderDisplay) {
            ShortcutDialogRow r;
            r.id = "script." + rel;   // key stays the raw path (stable)
            r.name = scriptActionPath(rel, folderDisplay);
            r.group = SG_SCRIPT;
            rows.push_back(r);
        };
        auto folderDisp = [](const ScriptGroup &g) {
            return g.displayName.empty() ? g.name : g.displayName;
        };
        for (const auto &n : cat.rootScripts) addScript(n, "");
        for (const auto &g : cat.groups) {
            std::string f1 = folderDisp(g);
            for (const auto &s : g.scripts) addScript(g.name + "/" + s, f1);
            for (const auto &sg : g.subGroups) {
                std::string f2 = f1 + " > " + folderDisp(sg);
                for (const auto &s : sg.scripts)
                    addScript(g.name + "/" + sg.name + "/" + s, f2);
            }
        }
    }

    // Static actions: window ops + Lua console + AI chat (one list,
    // names carry the location: "Window > Title Bar > Close").
    for (int i = 0; i < g_shortcutActionCount; ++i) {
        const ShortcutAction &a = g_shortcutActions[i];
        ShortcutDialogRow r;
        r.id = a.id;
        const char *loc = I18n::get(a.locKey);
        const char *label = I18n::get(a.labelKey);
        r.name = std::string(loc && *loc ? loc : a.locKey) + " > " +
                 (label && *label ? label : a.labelKey);
        r.group = a.group;
        r.defKey = a.defKey;
        r.defMods = a.defMods;
        rows.push_back(r);
    }
    return rows;
}

// --------------------------------------------------------------------------
// Custom frame: draw, edge hit-test, maximize/restore
// --------------------------------------------------------------------------

// Draw a thin 1px border around the window so the edge is visible

void MainWindow::saveSettings() {
    if (!m_cfg) return;
    // NOTE: Window geometry is intentionally NOT persisted (fixed 800x600
    // on every launch; the user can resize freely during the session).
    // Font size is intentionally NOT persisted here. The zoom level
    // (set via Ctrl+/-/0 or Ctrl+wheel) is per-session; saving it would
    // make the next startup show e.g. 107% instead of 100% (a saved size
    // of 14 compared against the hard-coded BASE_FONT of 13). The Font
    // dialog is the only path that persists the base font size.
    // Menu toggles - use cached pointers (saved before translation) so
    // this still works after the menu has been switched to Chinese.
    if (m_wrapItem && (m_wrapItem->flags & FL_MENU_TOGGLE))
        m_cfg->setWrap((m_wrapItem->flags & FL_MENU_VALUE) != 0);
    if (m_lineNumbersItem && (m_lineNumbersItem->flags & FL_MENU_TOGGLE))
        m_cfg->setLineNumbers((m_lineNumbersItem->flags & FL_MENU_VALUE) != 0);
    // Multi Tab is no longer a View menu toggle; its value is written
    // Settings are written immediately to settings.ini by Config;
    // no explicit flush is needed.
}

// --------------------------------------------------------------------------
// Menu callbacks
// --------------------------------------------------------------------------



// File > New - create a new empty document.
//   - In single-tab mode: replaces the current document, with a save
//     prompt if there are unsaved changes.
