// test_stubs.cpp - minimal stub for symbols required by fltk.lib
// but not present in test-only link lines (tests don't link Theme.cpp).
typedef unsigned int Fl_Color;
Fl_Color g_popupCheckColor = 0;
// patched FLTK 的 Fl_Menu 分隔线补丁同样引用这个符号(产品里由 core/Theme.cpp
// 定义,主题 text1);本测试不链 Theme.cpp,自带一份。
Fl_Color g_borderColour = 0;
