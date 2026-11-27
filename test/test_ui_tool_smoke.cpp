// test_ui_tool_smoke.cpp - construction-level smoke test for the AI chat
// tool window (in-process since m025). The Lua console tool window was
// removed in m037; do not resurrect it here.
//
// Constructs the AI chat pane through its real code path and destroys it.
// ui::g_smokeMode is set for the whole suite. A crash aborts the process,
// so ctest fails automatically if construction regresses.
#include "test_assert.h"

#include "core/Config.h"
#include "core/Theme.h"
#include "core/I18n.h"
#include "ui/Layout.h"
#include "ui/SmokeTest.h"
#include "AIChat/AIChatPane.h"

#include <FL/Fl.H>
#include <FL/Fl_Preferences.H>

#include <cstdio>
#include <string>

int main() {
    // 便携：与 Pecia 入口一致，禁止 FLTK 核心在 exe 之外读写 prefs。
    // 构造控件会触发 Fl::option()，否则会在 %APPDATA%/%ProgramData% 下
    // 生成 fltk.org/fltk.prefs。必须在任何 FLTK 调用之前设置。
    Fl_Preferences::file_access(Fl_Preferences::APP_OK);
    Fl::visual(FL_DOUBLE | FL_RGB);

    // 启动序列必须与主窗口入口一致（见开发指南十节 §7）：
    // 少了这两步，工具窗会退回 FLTK 原始控件边框、中文字体也不枚举，
    // 测出来的"窗口"与用户看到的不再是同一个东西。两步都断言，防止空调用。
    Fl_Box_Draw_F *borderBefore = Fl::get_boxtype(FL_BORDER_BOX);
    const int fontCount = Fl::set_fonts("*");
    theme_registerBorderBox();
    CHECK(fontCount > 100);                                 // CJK 字体已枚举
    CHECK(Fl::get_boxtype(FL_BORDER_BOX) != borderBefore);  // 主题边框已安装

    Fl::scheme("gtk+");
    ui::g_smokeMode = true;   // no detached threads / blocking modal loops

    Config cfg;
    initLayoutHeights(cfg);
    I18n::load("en");         // 只加载一次：失败也不重试，避免掩盖语言包缺失

    // ── AI chat ──
    {
        Theme theme;
        theme.load(cfg);
        AIChatPane win(&cfg, &theme, 800, 600);
        CHECK(win.w() > 0 && win.h() > 0);
    }

    if (test::failCount() == 0) {
        printf("test_ui_tool_smoke: AI chat pane constructed without crashing\n");
        return 0;
    }
    fprintf(stderr, "test_ui_tool_smoke: %d/%d checks FAILED\n",
            test::failCount(), test::checkCount());
    return 1;
}
