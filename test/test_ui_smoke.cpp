// test_ui_smoke.cpp - construction-level UI smoke test.
//
// Purpose: catch "the window crashes when you open it" regressions without
// needing a human to click or a running GUI. Each window is OPENED through
// its real public path with a smoke-mode flag that prevents the internal
// modal `while(shown()) Fl::wait()` loop from blocking. Because a crash
// (access violation) aborts the process, reaching the CHECK after each
// window proves construction + layout + show ran without faulting.
//
// This would have caught the ShortcutDialog child(0) crash and the
// ParamDialog close-button overflow bug.
//
// Requires FLTK + the same sources the main target links, and the resources
// the dialogs read (lang files via exe-dir).
#include "test_assert.h"

#include "core/Config.h"
#include "core/Theme.h"
#include "core/I18n.h"
#include "core/ShortcutCore.h"
#include "script/LuaParamParser.h"
#include "ui/Layout.h"
#include "ui/SmokeTest.h"
#include "ui/AboutDialog.h"
#include "ui/InfoWindow.h"

#include "ui/SettingsDialog.h"
#include "ui/ConfirmDialog.h"
#include "ui/ParamDialog.h"
#include "ui/ShortcutDialog.h"

#include <FL/Fl.H>

#include <cstdio>
#include <vector>
#include <string>

namespace {

// One-time FLTK + app init before constructing any window (mirrors the
// startup sequence in main_*.cpp).
void runOpenTest() {
    Config cfg;
    initLayoutHeights(cfg);
    if (!I18n::load("en")) I18n::load("en");
    Theme theme;
    theme.load(cfg);
    const int fs = 16;

    // ── Settings / Options ──
    { SettingsDialog dlg(520, 480, "smoke-settings", &theme, fs, 16); }
    CHECK(true);

    // ── Confirm (3 / 2 / 1 buttons) ──
    { ConfirmDialog dlg("t", "msg", "Yes", "No", "Cancel", &theme, fs); }
    { ConfirmDialog dlg2("t", "msg", "OK", "Cancel", nullptr, &theme, fs); }
    { ConfirmDialog dlg3("t", "msg", "OK", nullptr, nullptr, &theme, fs); }
    CHECK(true);

    // ── Param (multi-field incl. text/checkbox/choice, and a single short
    //    field that exposed the close-button overflow) ──
    {
        std::vector<LuaParam> params;
        LuaParam p1; p1.name = "n"; p1.defValue = "10";
        p1.description = "Count";
        LuaParam p2; p2.name = "flag"; p2.isCheckbox = true;
        p2.choices = {"on", "off"}; p2.defValue = "on"; p2.description = "Enabled";
        LuaParam p3; p3.name = "mode"; p3.choices = {"a", "b", "c"};
        p3.defValue = "b"; p3.description = "Mode";
        ParamDialog dlg("smoke-param", {p1, p2, p3}, &theme, fs);
        ParamDialog dlg2("smoke-param2", {p1}, &theme, fs);
    }
    CHECK(true);

    // ── Shortcut (exercises layoutRows()/repositionRows()/fitButtonRow) ──
    {
        std::vector<ShortcutDialogRow> rows;
        ShortcutDialogRow a;
        a.id = "win.pin"; a.name = "Pin"; a.group = SG_WINDOW;
        a.defKey = 0; a.defMods = 0;
        ShortcutDialogRow b;
        b.id = "win.minimize"; b.name = "Minimize"; b.group = SG_WINDOW;
        b.defKey = FL_F + 9; b.defMods = FL_CTRL;
        ShortcutDialog dlg(520, 560, "smoke-shortcut", &cfg, &theme, fs);
        dlg.setRows(rows);
    }
    CHECK(true);

    // ── Info (doc statistics; smoke mode skips the blocking loop) ──
    {
        InfoWindow iw;
        iw.setTheme(&theme, fs);
        iw.setTitle("Stats");
        iw.setWidth(320);
        iw.setRows({{"Chars", "123"}, {"Words", "45"}, {"Lines", "6"}});
        iw.show();
    }
    CHECK(true);

    // ── About (smoke mode skips the blocking loop) ──
    { showAboutDialog(&theme, fs); }
    CHECK(true);
}

} // namespace

int main() {
    Fl::visual(FL_DOUBLE | FL_RGB);
    Fl::scheme("gtk+");
    ui::g_smokeMode = true;   // no blocking modal loops during the smoke test

    runOpenTest();

    if (test::failCount() == 0) {
        printf("test_ui_smoke: all windows opened without crashing\n");
        return 0;
    }
    fprintf(stderr, "test_ui_smoke: %d/%d checks FAILED\n",
            test::failCount(), test::checkCount());
    return 1;
}
