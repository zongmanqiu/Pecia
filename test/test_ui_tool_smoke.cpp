// test_ui_tool_smoke.cpp - construction-level smoke test for the two
// standalone tool windows (AI chat, Lua console).
//
// Constructs each tool window through its real code path (builds the shared
// DialogBase shell, title bar, tool chrome, and the tool's content panes)
// and destroys it. ui::gSmokeMode is set so the Lua console skips its
// detached pipe-listener thread (which would otherwise leak a background
// thread referencing the destroyed window). A crash aborts the process, so
// ctest fails automatically if construction regresses.
#include "test_assert.h"

#include "core/Config.h"
#include "core/Theme.h"
#include "core/I18n.h"
#include "ui/Layout.h"
#include "ui/SmokeTest.h"
#include "LuaTool/LuaToolWindow.h"
#include "AIChat/AIChatWindow.h"

#include <FL/Fl.H>

#include <cstdio>
#include <string>

int main() {
    Fl::visual(FL_DOUBLE | FL_RGB);
    Fl::scheme("gtk+");
    ui::g_smokeMode = true;   // no detached threads / blocking modal loops

    Config cfg;
    initLayoutHeights(cfg);
    if (!I18n::load("en")) I18n::load("en");

    // ── Lua console ──
    {
        LuaToolWindow win(&cfg, "\\\\.\\pipe\\pecia-lua-smoke", 800, 600,
                          "smoke-lua");
    }
    CHECK(true);

    // ── AI chat ──
    {
        AIChatWindow win(&cfg, "\\\\.\\pipe\\pecia-lua-smoke", 800, 600,
                         "smoke-ai");
    }
    CHECK(true);

    if (test::failCount() == 0) {
        printf("test_ui_tool_smoke: tool windows constructed without crashing\n");
        return 0;
    }
    fprintf(stderr, "test_ui_tool_smoke: %d/%d checks FAILED\n",
            test::failCount(), test::checkCount());
    return 1;
}
