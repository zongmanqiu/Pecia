// ShortcutCore.h - shared shortcut configuration logic (pure, testable).
//
// One source of truth for every keyboard shortcut the application can
// customize:
//   - canonical combo text format ("Ctrl+F", "Alt+F5", "Esc", "")
//   - parse / serialize / validation rules
//   - a conflict-checking registry (id <-> key combo)
//   - the static action table (window ops + the two tool windows)
//
// Menu actions and toolbar script actions are built dynamically by each
// window (menu table / script folder scan) and are NOT part of the
// static table.
#pragma once

#include <string>
#include <vector>

// A keyboard shortcut: FLTK key code + modifier bits.
//   key == 0  -> no shortcut (empty). Letters are stored lowercase.
//   mods      -> FL_CTRL | FL_SHIFT | FL_ALT (FLTK constants).
struct ShortcutCombo {
    int      key = 0;
    unsigned mods = 0;
    bool     empty() const { return key == 0; }
    bool operator==(const ShortcutCombo &o) const {
        return key == o.key && mods == o.mods;
    }
};

// Parse canonical text into a combo. Accepts "" (-> empty combo).
// Formats: "Ctrl+Alt+Shift+F1", "F5", "Esc", "Ctrl++" is invalid;
// use "Ctrl+Plus". Returns false on malformed input.
bool shortcutParse(const std::string &text, ShortcutCombo *out);

// Serialize a combo to canonical text. Empty combo -> "".
// Letters are rendered uppercase ("Ctrl+F"), '+' -> "Plus".
std::string shortcutToString(const ShortcutCombo &combo);

// Display name for a single FLTK key code ("F1", "Esc", "Delete", "A").
// Returns "?" for unknown keys.
const char *shortcutKeyName(int key);

// Reverse of shortcutKeyName: "F1" -> FL_F+1, "A" -> 'a', "Plus" -> '+'.
// Returns 0 for unknown names.
int shortcutKeyFromName(const char *name);

// Validation: returns nullptr if the combo is assignable, otherwise a
// static error string (English; the dialog may localize via I18n).
// Rules:
//   - bare letter / digit / punctuation keys are rejected (they would
//     swallow text input)
//   - Shift+letter and Shift+digit are rejected (uppercase typing)
//   - Ctrl+Alt+Del is rejected (reserved by the system)
//   - bare function/navigation keys (F1-F24, Esc, Delete, Insert,
//     Home/End, PageUp/Down, arrows, Print, Pause, lock keys) are OK
//   - empty combo is always OK
const char *shortcutValidate(const ShortcutCombo &combo);

// Conflict-free key registry: maps action ids to key combos and answers
// "which id is bound to this key?" / "would this key collide?".
class ShortcutRegistry {
public:
    struct Entry {
        std::string id;
        int         key;
        unsigned    mods;
    };

    // Bind `id` to (key, mods); key == 0 removes the entry.
    void set(const std::string &id, int key, unsigned mods);

    // Id currently bound to (key, mods), or nullptr.
    const std::string *find(int key, unsigned mods) const;

    // Any id (other than `exceptId`) bound to (key, mods), or "".
    std::string conflictWith(int key, unsigned mods,
                             const std::string &exceptId = "") const;

    // The id bound to the same combo as `id` (or "" if none).
    std::string conflictOf(const std::string &id) const;

    void clear() { m_entries.clear(); }
    const std::vector<Entry> &entries() const { return m_entries; }

private:
    std::vector<Entry> m_entries;
};

// Dialog sections (order of display).
enum ShortcutGroup {
    SG_MENU = 0,   // main window menu actions
    SG_SCRIPT,     // toolbar scripts (dynamic)
    SG_WINDOW,     // window-level operations: title bar, tabs, buttons
                  // (tools are NOT separate groups; their actions live
                  // here and each window binds them to its own controls)
};

struct ShortcutAction {
    const char *id;        // config key suffix: "shortcut.<id>"
    const char *locKey;    // I18n key: location prefix ("Window > Title Bar")
    const char *labelKey;  // I18n key: action word ("Minimize")
    int         defKey;    // default FLTK key (0 = none)
    unsigned    defMods;   // default modifiers
    int         group;     // ShortcutGroup
};

// Static actions: window operations + the two standalone tools.
// Menu actions (dynamic) and script actions (dynamic) are added by
// MainWindow when building the dialog row list.
extern const ShortcutAction g_shortcutActions[];
extern const int g_shortcutActionCount;

// Look up a static action by id; returns nullptr if not found.
const ShortcutAction *shortcutFindAction(const char *id);

// Dialog-level shortcut: does (key, mods) match the configured
// "shortcut.win.button.ok" combo (settings.ini)? Defaults to Ctrl+Enter.
// Config is read once and cached; used by modal dialogs so the OK button
// can be triggered by a user-assignable shortcut.
bool dialogOkShortcutMatches(int key, unsigned mods);

// Whether a shortcut change for this action can be applied by a
// non-main window (tool windows handle win.* and their own group only).
inline bool shortcutActionInGroup(const ShortcutAction &a, int group) {
    return (a.group == group) || (a.group == SG_WINDOW);
}
