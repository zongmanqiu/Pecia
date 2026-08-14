// test_shortcut_core.cpp - unit tests for the shortcut configuration
// core (parse/serialize/validate/registry). Pure logic, no GUI: links
// ShortcutCore.cpp and FLTK (for the key-code constants).
#include "test_assert.h"
#include "core/ShortcutCore.h"

#include <FL/Fl.H>

#include <cstdio>
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// parse / serialize
// ---------------------------------------------------------------------------

static void test_parse_empty() {
    ShortcutCombo c;
    CHECK(shortcutParse("", &c));
    CHECK(c.empty());
    CHECK(shortcutToString(c) == "");
}

static void test_parse_modifier_letters() {
    ShortcutCombo c;
    // Ctrl+F
    CHECK(shortcutParse("Ctrl+F", &c));
    CHECK(c.key == 'f');
    CHECK(c.mods == FL_CTRL);
    CHECK(shortcutToString(c) == "Ctrl+F");
    // Alt+F5
    CHECK(shortcutParse("Alt+F5", &c));
    CHECK(c.key == FL_F + 5);
    CHECK(c.mods == FL_ALT);
    CHECK(shortcutToString(c) == "Alt+F5");
    // Ctrl+Alt+Y (modifier order normalized: Ctrl, Alt, Shift)
    CHECK(shortcutParse("Ctrl+Alt+Y", &c));
    CHECK(c.key == 'y');
    CHECK(c.mods == (FL_CTRL | FL_ALT));
    CHECK(shortcutToString(c) == "Ctrl+Alt+Y");
    // Shift+S (uppercase key implies Shift)
    CHECK(shortcutParse("Shift+S", &c));
    CHECK(c.key == 's');
    CHECK(c.mods == FL_SHIFT);
    CHECK(shortcutToString(c) == "Shift+S");
    // Lowercase key, no shift
    CHECK(shortcutParse("Ctrl+s", &c));
    CHECK(c.key == 's');
    CHECK(c.mods == FL_CTRL);
    CHECK(shortcutToString(c) == "Ctrl+S");
}

static void test_parse_function_keys() {
    ShortcutCombo c;
    CHECK(shortcutParse("F1", &c));
    CHECK(c.key == FL_F + 1);
    CHECK(c.mods == 0);
    CHECK(shortcutToString(c) == "F1");
    CHECK(shortcutParse("Ctrl+F12", &c));
    CHECK(c.key == FL_F + 12);
    CHECK(c.mods == FL_CTRL);
    CHECK(shortcutToString(c) == "Ctrl+F12");
    CHECK(shortcutParse("Shift+F3", &c));
    CHECK(c.key == FL_F + 3);
    CHECK(c.mods == FL_SHIFT);
    CHECK(shortcutToString(c) == "Shift+F3");
}

static void test_parse_special_keys() {
    ShortcutCombo c;
    CHECK(shortcutParse("Esc", &c));
    CHECK(c.key == FL_Escape);
    CHECK(shortcutToString(c) == "Esc");
    CHECK(shortcutParse("Delete", &c));
    CHECK(c.key == FL_Delete);
    CHECK(shortcutToString(c) == "Delete");
    CHECK(shortcutParse("Ctrl+PageDown", &c));
    CHECK(c.key == FL_Page_Down);
    CHECK(c.mods == FL_CTRL);
    CHECK(shortcutToString(c) == "Ctrl+PageDown");
    CHECK(shortcutParse("Ctrl+Plus", &c));
    CHECK(c.key == '+');
    CHECK(shortcutToString(c) == "Ctrl+Plus");
    // Digit key
    CHECK(shortcutParse("Ctrl+0", &c));
    CHECK(c.key == '0');
    CHECK(shortcutToString(c) == "Ctrl+0");
}

static void test_parse_invalid() {
    ShortcutCombo c;
    CHECK(!shortcutParse("Garbage", &c));        // unknown name
    CHECK(!shortcutParse("Ctrl+Foo", &c));       // unknown modifier
    CHECK(!shortcutParse("Ctrl+F+X", &c));       // two keys
    CHECK(!shortcutParse("Ctrl++", &c));         // trailing '+'
    CHECK(!shortcutParse("++F1", &c));           // empty modifier
    // Bare "S" parses as Shift+S, which validation rejects.
    CHECK(shortcutParse("S", &c));
    CHECK(c.key == 's');
    CHECK(c.mods == FL_SHIFT);
    CHECK(shortcutValidate(c) != nullptr);
}

// ---------------------------------------------------------------------------
// validate
// ---------------------------------------------------------------------------

static void test_validate_rules() {
    // Empty is always OK.
    ShortcutCombo c;
    CHECK(shortcutValidate(c) == nullptr);

    // Bare letters / digits / punctuation rejected.
    c.key = 'a'; c.mods = 0;
    CHECK(shortcutValidate(c) != nullptr);
    c.key = '5'; c.mods = 0;
    CHECK(shortcutValidate(c) != nullptr);
    c.key = ','; c.mods = 0;
    CHECK(shortcutValidate(c) != nullptr);

    // Bare function/navigation keys OK.
    c.key = FL_F + 5; c.mods = 0;
    CHECK(shortcutValidate(c) == nullptr);
    c.key = FL_Escape; c.mods = 0;
    CHECK(shortcutValidate(c) == nullptr);
    c.key = FL_Delete; c.mods = 0;
    CHECK(shortcutValidate(c) == nullptr);
    c.key = FL_Left; c.mods = 0;
    CHECK(shortcutValidate(c) == nullptr);

    // Modifier combos with letters OK.
    c.key = 'f'; c.mods = FL_CTRL;
    CHECK(shortcutValidate(c) == nullptr);
    c.key = 'y'; c.mods = FL_CTRL | FL_ALT;
    CHECK(shortcutValidate(c) == nullptr);

    // Shift+letter / Shift+digit rejected.
    c.key = 's'; c.mods = FL_SHIFT;
    CHECK(shortcutValidate(c) != nullptr);
    c.key = '1'; c.mods = FL_SHIFT;
    CHECK(shortcutValidate(c) != nullptr);

    // Shift+F-key / Shift+arrow OK.
    c.key = FL_F + 3; c.mods = FL_SHIFT;
    CHECK(shortcutValidate(c) == nullptr);
    c.key = FL_Up; c.mods = FL_SHIFT;
    CHECK(shortcutValidate(c) == nullptr);

    // Ctrl+Alt+Del rejected.
    c.key = FL_Delete; c.mods = FL_CTRL | FL_ALT;
    CHECK(shortcutValidate(c) != nullptr);
    // Ctrl+Alt+F1 OK.
    c.key = FL_F + 1; c.mods = FL_CTRL | FL_ALT;
    CHECK(shortcutValidate(c) == nullptr);
}

// ---------------------------------------------------------------------------
// registry
// ---------------------------------------------------------------------------

static void test_registry_bind_find() {
    ShortcutRegistry reg;
    reg.set("a", 'f', FL_CTRL);
    reg.set("b", FL_F + 3, FL_SHIFT);
    reg.set("empty", 0, 0);   // key 0 removes

    const std::string *id = reg.find('f', FL_CTRL);
    CHECK(id && *id == "a");
    id = reg.find(FL_F + 3, FL_SHIFT);
    CHECK(id && *id == "b");

    // No match: different key / different mods.
    CHECK(reg.find('f', 0) == nullptr);
    CHECK(reg.find('f', FL_CTRL | FL_SHIFT) == nullptr);
    CHECK(reg.find(FL_F + 4, FL_SHIFT) == nullptr);
    CHECK(reg.find('f', FL_CTRL) != nullptr);   // "empty" removed nothing

    // Overwrite a's binding; old combo now free.
    reg.set("a", 'g', FL_CTRL);
    CHECK(reg.find('f', FL_CTRL) == nullptr);
    CHECK(reg.find('g', FL_CTRL) != nullptr);
}

static void test_registry_conflicts() {
    ShortcutRegistry reg;
    reg.set("x", 'f', FL_CTRL);
    reg.set("y", 'g', FL_CTRL);

    CHECK(reg.conflictWith('f', FL_CTRL, "") == "x");
    CHECK(reg.conflictWith('f', FL_CTRL, "x") == "");   // self excluded
    CHECK(reg.conflictWith('h', FL_CTRL, "") == "");    // free combo
    CHECK(reg.conflictOf("x") == "");                   // no other with same
    reg.set("z", 'f', FL_CTRL);
    CHECK(reg.conflictOf("x") == "z");
    CHECK(reg.conflictOf("z") == "x");
}

// ---------------------------------------------------------------------------
// static action table
// ---------------------------------------------------------------------------

static void test_static_actions() {
    CHECK(g_shortcutActionCount > 0);
    // Ids are unique and every labelKey/locKey is non-empty.
    for (int i = 0; i < g_shortcutActionCount; ++i) {
        const ShortcutAction &a = g_shortcutActions[i];
        CHECK(a.id && *a.id);
        CHECK(a.labelKey && *a.labelKey);
        CHECK(a.locKey && *a.locKey);
        for (int j = i + 1; j < g_shortcutActionCount; ++j)
            CHECK(strcmp(a.id, g_shortcutActions[j].id) != 0);
    }
    // Lookup works.
    CHECK(shortcutFindAction("win.pin") != nullptr);
    CHECK(shortcutFindAction("win.button.ok") != nullptr);
    CHECK(shortcutFindAction("nope") == nullptr);
    // Removed actions no longer exist.
    CHECK(shortcutFindAction("win.cancel") == nullptr);
    CHECK(shortcutFindAction("lua.run") == nullptr);
    CHECK(shortcutFindAction("chat.send") == nullptr);
    // Defaults of the window ops that exist today.
    const ShortcutAction *next = shortcutFindAction("win.nexttab");
    CHECK(next && next->defKey == FL_Tab && (next->defMods & FL_CTRL));
    // Every action belongs to the single Window group.
    for (int i = 0; i < g_shortcutActionCount; ++i) {
        CHECK(g_shortcutActions[i].group == SG_WINDOW);
        CHECK(shortcutActionInGroup(g_shortcutActions[i], SG_WINDOW));
    }
}

// ---------------------------------------------------------------------------

int main() {
    test_parse_empty();
    test_parse_modifier_letters();
    test_parse_function_keys();
    test_parse_special_keys();
    test_parse_invalid();
    test_validate_rules();
    test_registry_bind_find();
    test_registry_conflicts();
    test_static_actions();
    if (test::failCount() == 0) {
        printf("test_shortcut_core: all checks passed\n");
        return 0;
    }
    fprintf(stderr, "test_shortcut_core: %d/%d checks FAILED\n",
            test::failCount(), test::checkCount());
    return 1;
}
