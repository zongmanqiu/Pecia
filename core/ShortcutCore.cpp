// ShortcutCore.cpp - shared shortcut configuration logic.
#include "core/ShortcutCore.h"

#include <FL/Fl.H>
#include "core/Config.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace {

struct KeyName {
    const char *name;
    int         code;
};

// Function / navigation keys. Bare (no-modifier) combos are only legal
// for keys in this set (see shortcutValidate); the same set doubles as
// the display-name table for special keys.
const KeyName kSpecialKeys[] = {
    { "F1", FL_F + 1 },    { "F2", FL_F + 2 },    { "F3", FL_F + 3 },
    { "F4", FL_F + 4 },    { "F5", FL_F + 5 },    { "F6", FL_F + 6 },
    { "F7", FL_F + 7 },    { "F8", FL_F + 8 },    { "F9", FL_F + 9 },
    { "F10", FL_F + 10 },  { "F11", FL_F + 11 },  { "F12", FL_F + 12 },
    { "F13", FL_F + 13 },  { "F14", FL_F + 14 },  { "F15", FL_F + 15 },
    { "F16", FL_F + 16 },  { "F17", FL_F + 17 },  { "F18", FL_F + 18 },
    { "F19", FL_F + 19 },  { "F20", FL_F + 20 },  { "F21", FL_F + 21 },
    { "F22", FL_F + 22 },  { "F23", FL_F + 23 },  { "F24", FL_F + 24 },
    { "Esc", FL_Escape },
    { "Delete", FL_Delete },
    { "Insert", FL_Insert },
    { "Home", FL_Home },
    { "End", FL_End },
    { "PageUp", FL_Page_Up },
    { "PageDown", FL_Page_Down },
    { "Up", FL_Up },
    { "Down", FL_Down },
    { "Left", FL_Left },
    { "Right", FL_Right },
    { "Print", FL_Print },
    { "Pause", FL_Pause },
    { "ScrollLock", FL_Scroll_Lock },
    { "CapsLock", FL_Caps_Lock },
    { "NumLock", FL_Num_Lock },
    // Named keys that must NOT be used bare (they would break typing /
    // navigation), but are valid with a modifier:
    { "Backspace", FL_BackSpace },
    { "Tab", FL_Tab },
    { "Enter", FL_Enter },
    { "Space", ' ' },
};

bool isBareSafe(int key) {
    // Function keys + navigation keys may be used without modifiers.
    if (key >= FL_F && key <= FL_F + 24) return true;
    switch (key) {
    case FL_Escape: case FL_Delete: case FL_Insert:
    case FL_Home: case FL_End:
    case FL_Page_Up: case FL_Page_Down:
    case FL_Up: case FL_Down: case FL_Left: case FL_Right:
    case FL_Print: case FL_Pause:
    case FL_Scroll_Lock: case FL_Caps_Lock: case FL_Num_Lock:
        return true;
    default:
        return false;
    }
}

} // namespace

const char *shortcutKeyName(int key) {
    for (const auto &k : kSpecialKeys)
        if (key == k.code) return k.name;
    if (key >= 'a' && key <= 'z') {
        static char buf[2] = { 0, 0 };
        buf[0] = (char)(key - 'a' + 'A');
        return buf;
    }
    if (key >= '0' && key <= '9') {
        static char buf[2] = { 0, 0 };
        buf[0] = (char)key;
        return buf;
    }
    if (key == '+') return "Plus";
    if (key >= 0x21 && key <= 0x7e) {
        static char buf[2] = { 0, 0 };
        buf[0] = (char)key;
        return buf;
    }
    return "?";
}

int shortcutKeyFromName(const char *name) {
    if (!name || !*name) return 0;
    for (const auto &k : kSpecialKeys)
        if (strcmp(name, k.name) == 0) return k.code;
    if (strcmp(name, "Plus") == 0) return '+';
    if (name[0] >= 'A' && name[0] <= 'Z' && name[1] == 0)
        return name[0] - 'A' + 'a';
    if (name[0] >= 'a' && name[0] <= 'z' && name[1] == 0)
        return name[0];
    if (name[0] >= '0' && name[0] <= '9' && name[1] == 0)
        return name[0];
    if (name[1] == 0 && name[0] >= 0x21 && name[0] <= 0x7e)
        return name[0];
    return 0;
}

bool shortcutParse(const std::string &text, ShortcutCombo *out) {
    if (!out) return false;
    out->key = 0;
    out->mods = 0;
    if (text.empty()) return true;

    // Split on '+'; the last part is the key, the rest must be modifiers.
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t plus = text.find('+', pos);
        if (plus == std::string::npos) {
            parts.push_back(text.substr(pos));
            break;
        }
        parts.push_back(text.substr(pos, plus - pos));
        pos = plus + 1;
    }
    if (parts.empty()) return false;

    unsigned mods = 0;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        const std::string &m = parts[i];
        if (m == "Ctrl" || m == "ctrl" || m == "Control" || m == "control")
            mods |= FL_CTRL;
        else if (m == "Alt" || m == "alt")
            mods |= FL_ALT;
        else if (m == "Shift" || m == "shift")
            mods |= FL_SHIFT;
        else
            return false;
    }

    const std::string &key = parts.back();
    if (key.empty()) return false;

    // Named keys first.
    for (const auto &k : kSpecialKeys)
        if (key == k.name) {
            out->key = k.code;
            out->mods = mods;
            return true;
        }
    if (key == "Plus") {
        out->key = '+';
        out->mods = mods;
        return true;
    }

    if (key.size() == 1) {
        char c = key[0];
        if (c >= 'a' && c <= 'z') {
            out->key = c;
            out->mods = mods;
            return true;
        }
        if (c >= 'A' && c <= 'Z') {
            out->key = c - 'A' + 'a';
            // Uppercase implies Shift only for a bare key: "S" means
            // Shift+S, but "Ctrl+S" means Ctrl+s (the Shift is written
            // explicitly, matching the canonical serializer).
            if (mods == 0) mods = FL_SHIFT;
            out->mods = mods;
            return true;
        }
        if (c >= '0' && c <= '9') {
            out->key = c;
            out->mods = mods;
            return true;
        }
        // Printable punctuation.
        if (c >= 0x21 && c <= 0x7e && c != '+' && c != '&') {
            out->key = c;
            out->mods = mods;
            return true;
        }
    }
    return false;
}

std::string shortcutToString(const ShortcutCombo &combo) {
    if (combo.empty()) return "";
    std::string s;
    if (combo.mods & FL_CTRL) s += "Ctrl+";
    if (combo.mods & FL_ALT) s += "Alt+";
    if (combo.mods & FL_SHIFT) s += "Shift+";
    s += shortcutKeyName(combo.key);
    return s;
}

const char *shortcutValidate(const ShortcutCombo &combo) {
    if (combo.empty()) return nullptr;
    const unsigned mods = combo.mods;

    // Ctrl+Alt+Del is reserved by the system (secure attention sequence).
    if ((mods & FL_CTRL) && (mods & FL_ALT) && combo.key == FL_Delete)
        return "Ctrl+Alt+Del is reserved by the system";

    const bool hasModifier = (mods & (FL_CTRL | FL_ALT | FL_SHIFT)) != 0;
    if (!hasModifier) {
        // Bare keys: only function/navigation keys are safe.
        if (isBareSafe(combo.key)) return nullptr;
        return "A plain letter or number key would interfere with typing";
    }

    // Shift alone (no Ctrl/Alt) on a letter or digit types that character.
    if (mods == FL_SHIFT) {
        if (combo.key >= 'a' && combo.key <= 'z')
            return "Shift+letter would interfere with typing";
        if (combo.key >= '0' && combo.key <= '9')
            return "Shift+digit would interfere with typing";
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// ShortcutRegistry
// ---------------------------------------------------------------------------

void ShortcutRegistry::set(const std::string &id, int key, unsigned mods) {
    m_entries.erase(
        std::remove_if(m_entries.begin(), m_entries.end(),
                       [&id](const Entry &e) { return e.id == id; }),
        m_entries.end());
    if (key) m_entries.push_back({ id, key, mods });
}

const std::string *ShortcutRegistry::find(int key, unsigned mods) const {
    const unsigned mask = FL_CTRL | FL_SHIFT | FL_ALT | FL_META;
    const unsigned want = mods & mask;
    for (const auto &e : m_entries) {
        if (e.key == key && (e.mods & mask) == want) return &e.id;
    }
    return nullptr;
}

std::string ShortcutRegistry::conflictWith(int key, unsigned mods,
                                           const std::string &exceptId) const {
    const unsigned mask = FL_CTRL | FL_SHIFT | FL_ALT | FL_META;
    const unsigned want = mods & mask;
    for (const auto &e : m_entries) {
        if (e.id == exceptId) continue;
        if (e.key == key && (e.mods & mask) == want) return e.id;
    }
    return "";
}

std::string ShortcutRegistry::conflictOf(const std::string &id) const {
    for (const auto &e : m_entries) {
        if (e.id == id) {
            return conflictWith(e.key, e.mods, id);
        }
    }
    return "";
}

// ---------------------------------------------------------------------------
// Static action table
// ---------------------------------------------------------------------------

const ShortcutAction g_shortcutActions[] = {
    // Window-level operations: ONE config key per action; the main
    // window and the tool windows each bind it to their own control.
    { "win.pin",      "shortcut.loc.titlebar", "shortcut.win.pin",      0, 0, SG_WINDOW },
    { "win.minimize", "shortcut.loc.titlebar", "shortcut.win.minimize", 0, 0, SG_WINDOW },
    { "win.maximize", "shortcut.loc.titlebar", "shortcut.win.maximize", 0, 0, SG_WINDOW },
    { "win.close",    "shortcut.loc.titlebar", "shortcut.win.close",    0, 0, SG_WINDOW },
    { "win.nexttab",  "shortcut.loc.tabs",     "shortcut.win.nexttab",  FL_Tab, FL_CTRL, SG_WINDOW },
    { "win.prevtab",  "shortcut.loc.tabs",     "shortcut.win.prevtab",  FL_Tab, FL_CTRL | FL_SHIFT, SG_WINDOW },
    { "win.closetab", "shortcut.loc.tabs",     "shortcut.win.closetab", 'w', FL_CTRL, SG_WINDOW },

    // Tool window buttons (bound by the Lua / AI processes; the main
    // window ignores them):
    { "win.button.run",   "shortcut.loc.button", "shortcut.button.run",   FL_Enter, FL_CTRL, SG_WINDOW },
    { "win.button.clear", "shortcut.loc.button", "shortcut.button.clear", 0, 0, SG_WINDOW },
    { "win.button.ok",    "shortcut.loc.button", "shortcut.button.ok",    0, 0, SG_WINDOW },
    { "win.button.todoc", "shortcut.loc.button", "shortcut.button.todoc", 0, 0, SG_WINDOW },
    { "win.button.tolua", "shortcut.loc.button", "shortcut.button.tolua", 0, 0, SG_WINDOW },
    { "win.button.toai",  "shortcut.loc.button", "shortcut.button.toai",  0, 0, SG_WINDOW },
    { "win.button.saveas","shortcut.loc.button", "shortcut.button.saveas",0, 0, SG_WINDOW },
    { "win.button.help",  "shortcut.loc.button", "shortcut.button.help",  0, 0, SG_WINDOW },
    { "win.button.settings","shortcut.loc.button", "shortcut.button.settings", 0, 0, SG_WINDOW },
};

const int g_shortcutActionCount =
    (int)(sizeof(g_shortcutActions) / sizeof(g_shortcutActions[0]));

// ---------------------------------------------------------------------------
// Dialog-level OK shortcut (win.button.ok)
// ---------------------------------------------------------------------------

// Lazily loaded "shortcut.win.button.ok" combo (default Ctrl+Enter).
// Cached: dialog sessions are short and the config rarely changes while
// a modal dialog is open.
static ShortcutCombo s_dialogOk;
static bool s_dialogOkLoaded = false;

static const ShortcutCombo &dialogOkCombo() {
    if (!s_dialogOkLoaded) {
        s_dialogOkLoaded = true;
        Config cfg;
        std::string text = cfg.getShortcut("win.button.ok");
        if (!text.empty()) {
            ShortcutCombo c;
            if (shortcutParse(text, &c)) {
                s_dialogOk = c;
            } else {
                s_dialogOk.key = FL_Enter;
                s_dialogOk.mods = FL_CTRL;
            }
        } else {
            s_dialogOk.key = FL_Enter;
            s_dialogOk.mods = FL_CTRL;
        }
    }
    return s_dialogOk;
}

bool dialogOkShortcutMatches(int key, unsigned mods) {
    const ShortcutCombo &c = dialogOkCombo();
    if (c.empty()) return false;
    return c.key == key &&
           c.mods == (mods & (FL_CTRL | FL_SHIFT | FL_ALT));
}

// ---------------------------------------------------------------------------
// Helpers used by the dialogs / windows
// ---------------------------------------------------------------------------

// Look up a static action by id; returns nullptr if not found.
const ShortcutAction *shortcutFindAction(const char *id) {
    for (int i = 0; i < g_shortcutActionCount; ++i) {
        if (strcmp(g_shortcutActions[i].id, id) == 0)
            return &g_shortcutActions[i];
    }
    return nullptr;
}
