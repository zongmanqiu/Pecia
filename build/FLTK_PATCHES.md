# FLTK Patches（本项目对 FLTK 源码的修改记录）

本文件记录 Pecia 项目对第三方依赖 FLTK 源码的所有修改。
**目的**：将来若升级/替换 FLTK 版本，可按本文件逐条重新应用这些修改。

- 适用 FLTK 版本：**1.4.5**
- 库源码位置：`.thirdparty/fltk-1.4.5/fltk-1.4.5/`
- 库构建目录：`.thirdparty/fltk-1.4.5/fltk-1.4.5/build/`（修改源码后需在此重编库，再运行 `main\build\build.bat`）
- **修改后完整文件归档**：`main/patches/fltk-1.4.5/src/Fl_Menu.cxx`
  与 `main/patches/fltk-1.4.5/src/Fl_Text_Display.cxx`、
  `main/patches/fltk-1.4.5/src/Fl_Text_Buffer.cxx`、
  `main/patches/fltk-1.4.5/FL/Fl_Text_Buffer.H`
  （本项目对 FLTK 的修改分布在这些文件内；
  构建前将归档文件复制覆盖到库源码对应位置即可，无需手动逐条打补丁）

---

## Patch 1：弹出菜单窗口改为双缓冲（消除菜单高亮切换闪烁）

### 现象
主程序菜单（`HoverMenuBar` 弹出的 FLTK 菜单）在鼠标于选项间移动时，
高亮块切换有明显闪烁。帧捕获证实：菜单窗口单缓冲
（`Fl_Menu_Window` 继承 `Fl_Single_Window`），高亮切换按
"先擦除旧项、再绘制新项"两步重绘，中间状态会被屏幕刷新捕捉到。

### 修改文件
`src/Fl_Menu.cxx`（源码根目录下的 `src/`，非 `build/`）

### 修改 1a：新增头文件包含
**位置**：文件头部现有
```cpp
#include <FL/Fl_Menu_Window.H>
```
（约第 26 行）之后**新增一行**：
```cpp
#include <FL/Fl_Double_Window.H>
```

### 修改 1b：菜单窗口基类改双缓冲
**位置**：`class window_with_items` 声明（原约第 112 行）及构造初始化列表（约第 115 行）。

**修改前**：
```cpp
class window_with_items : public Fl_Menu_Window {
protected:
  window_with_items(int X, int Y, int W, int H, const Fl_Menu_Item *m) :
    Fl_Menu_Window(X, Y, W, H, 0) {
      menu = m;
      set_menu_window();
      Fl_Window_Driver::driver(this)->set_popup_window();
      end();
      set_modal();
      clear_border();
```

**修改后**：
```cpp
class window_with_items : public Fl_Double_Window {
protected:
  window_with_items(int X, int Y, int W, int H, const Fl_Menu_Item *m) :
    Fl_Double_Window(X, Y, W, H, 0) {
      menu = m;
      set_menu_window();
      Fl_Window_Driver::driver(this)->set_popup_window();
      end();
      set_modal();
      clear_border();
```

仅替换基类名与构造初始化项；函数体内的
`set_menu_window()` / `set_popup_window()` / `set_modal()` / `clear_border()`
均为 `Fl_Window` 成员，保持不变。
该改动同时影响 `menuwindow`（下拉菜单）与 `menutitle`（菜单栏标题窗），
两者都是 `window_with_items` 的子类。

### 修改 1c：menuwindow::position 的基类调用
**位置**：`menuwindow::position(int X, int Y)` 函数体（原约第 494 行）。
基类不再是 `Fl_Menu_Window`，原显式调用需同步改指，否则编译报
C2352。

**修改前**：
```cpp
void menuwindow::position(int X, int Y) {
  if (title) {title->position(X, title->y()+Y-y());}
  Fl_Menu_Window::position(X, Y);
```

**修改后**：
```cpp
void menuwindow::position(int X, int Y) {
  if (title) {title->position(X, title->y()+Y-y());}
  Fl_Double_Window::position(X, Y);
```

### 原因
`Fl_Menu_Window` 继承 `Fl_Single_Window`（单缓冲，直接上屏）。
改为 `Fl_Double_Window` 后，菜单窗口的每次重绘先画到离屏缓冲、
再一次性合成上屏，消除高亮切换的中间帧可见性。
菜单窗口很小（几十个选项），双缓冲性能开销可忽略。

### 如何应用到新版本 FLTK
1. 打开新版本的 `src/Fl_Menu.cxx`
2. 在 `#include <FL/Fl_Menu_Window.H>` 后加 `#include <FL/Fl_Double_Window.H>`
3. 将 `class window_with_items : public Fl_Menu_Window` 改为
   `class window_with_items : public Fl_Double_Window`
4. 将其构造初始化列表 `Fl_Menu_Window(X, Y, W, H, 0)` 改为
   `Fl_Double_Window(X, Y, W, H, 0)`
5. 将 `menuwindow::position` 函数体内的 `Fl_Menu_Window::position(X, Y)` 改为
   `Fl_Double_Window::position(X, Y)`
6. 重新编译 FLTK 库，再构建 Pecia

### 验证
- 打开任意菜单，鼠标在选项间移动：高亮切换不再闪烁
- 菜单弹出、子菜单、快捷键、键盘导航功能不受影响

---

## Patch 2：禁用菜单栏按钮的按下覆盖窗口（消除点击时按钮文字偏移）

### 现象
点击菜单栏按钮（弹出菜单瞬间），按钮文字会向左偏移 2px。
帧对比实证：正常绘制时“文件”文字在项内居中（x 14..44），
按下覆盖窗口显示后变为左对齐 +3px（x 6..36）。

### 根因（两处机制，均需处理）
1. **菜单窗口的 menubar_title 机制**（主要）：主循环为菜单栏下拉创建菜单窗口时
   （`Fl_Menu.cxx` 约 1131-1135 行），调用参数 `menubar_title` 被置为
   menubar 值（1），且 `title` 参数仍指向当前菜单项——
   `menuwindow` 构造的 `menubar_title` 分支因此创建一个
   `menutitle` 覆盖窗口（尺寸 = 菜单项尺寸，位置 = 按钮位置）。
   该窗口用 `Fl_Menu_Item::draw(..., selected=2)` 绘制：
   文字左对齐并偏移 3px，与自定义菜单栏 HoverMenuBar 的居中文字不一致，
   导致点击瞬间按钮文字跳动 2px。
2. **fakemenu 机制**（次要，kludge）：菜单项无子菜单时创建的
   “按下”效果窗口，同样绘制左对齐文字。

Pecia 的 HoverMenuBar 在菜单打开时自行绘制按下高亮（m_highlight），
这两个覆盖窗口均冗余，因此禁用。

### 修改文件
`src/Fl_Menu.cxx`（与 Patch 1 同一文件）

### 修改 2a：menuwindow 构造跳过菜单栏按钮覆盖窗口
**位置**：`menuwindow::menuwindow` 构造函数中 `if (t)` 的
`menubar_title` 分支（原约第 473-476 行）。

**修改前**：
```cpp
if (t) {
  if (menubar_title) {
    int dy = Fl::box_dy(button->box())+1;
    int ht = button->h()-dy*2;
    title = new menutitle(tx, ty-ht-dy, Wtitle, ht, t, true);
  } else {
    int dy = 2;
    int ht = Htitle+2*BW+3;
    title = new menutitle(X, Y-ht-dy, Wtitle, ht, t);
  }
} else {
  title = 0;
}
```

**修改后**（menubar_title 分支不再创建覆盖窗口，置 title=0；
else 分支（子菜单标题）保持不变）：
```cpp
if (t) {
  if (menubar_title) {
    // PATCHED by Pecia: no menubar-button overlay window (see above)
    title = 0;
  } else {
    int dy = 2;
    int ht = Htitle+2*BW+3;
    title = new menutitle(X, Y-ht-dy, Wtitle, ht, t);
  }
} else {
  title = 0;
}
```

注意：不要通过清空菜单栏分支的 `title` 变量（调用侧）来实现，
那会改变菜单窗口的定位参数（`right_edge`/`Wp`），导致菜单窗口
定位异常（实测菜单不弹出）。

### 修改 2b：禁用 fakemenu 创建
**位置**：主循环 `else { // !m->submenu():` 分支中的
“kludge so menubar buttons turn on”代码块（原约第 1142-1148 行）。
创建语句整体移除（保留注释）。

**修改前**：
```cpp
if (!pp.menu_number && pp.menubar) {
  // kludge so "menubar buttons" turn "on" by using menu title:
  pp.fakemenu = new menuwindow(0,
                            cw.x()+cw.titlex(pp.item_number),
                            cw.y()+cw.h(), 0, 0,
                            0, m, 0, 1);
  pp.fakemenu->title->show();
}
```

**修改后**：if 块体为空（仅保留说明注释）。
后续 `delete pp.fakemenu;` 保持不动（空指针删除安全）。

### 副作用说明
标准 FL_Menu_Bar（非 HoverMenuBar）的菜单栏按钮将失去“按下”
覆盖效果。Pecia 的菜单栏与工具栏均为 HoverMenuBar
（自绘按下高亮），不受影响。

### 如何应用到新版本 FLTK
1. 打开新版本的 `src/Fl_Menu.cxx`
2. 在 `menuwindow` 构造函数中，将 `if (t) { if (menubar_title) { ... }`
   分支中的 `menutitle` 创建语句替换为 `title = 0;`
   （仅 menubar_title 分支；子菜单标题分支保持不变）
3. 在 `else { // !m->submenu():` 分支中找到
   `pp.fakemenu->title->show();`（连同其上方 fakemenu 创建语句），
   将整个 if 块体留空
4. 重新编译 FLTK 库，再构建 Pecia

### 验证
- 点击菜单栏按钮：按钮文字不再偏移（帧对比 A/B/C 三态一致）
- 按下高亮仍由 HoverMenuBar 自绘显示

---

## Patch 3：菜单快捷键统一列对齐（消除长键名右对齐错位）

### 现象
弹出菜单中，短键名（F3、Esc、Home…）的快捷键文本在"修饰列右对齐 + 键列
左对齐"布局中左缘对齐；而长键名（Delete、Backspace、Page_Up…）被整体
右对齐到菜单右缘，左缘不在同一列，与短键错位。

### 根因
`Fl_Menu.cxx` 的 `menutitle::draw()` 快捷键绘制分支按键名长度分两种布局：
- 键名 ≤4 字符：修饰键右对齐 + 键部分左对齐（两列布局）；
- 键名 >4 字符：整个快捷键文本右对齐到菜单右缘。
`Delete` 显示名 6 字符，落入第二种，与 `F3` 等不对齐。

### 修改文件
`src/Fl_Menu.cxx`（与 Patch 1/2 同一文件）

### 修改内容
**共两处，必须同时修改**（测量与绘制阈值一致，否则键列宽按短键算、
长键名会被截断，如 Delete 只剩 "Del"）：

**位置 1（测量）**：菜单宽度测量循环中的快捷键宽度分支（原约第 417 行）。

**修改前**：
```cpp
if (fl_utf_nb_char((const unsigned char*)k, (int) strlen(k))<=4) {
```

**修改后**：
```cpp
// Pecia patch (2026-08-09, see main/build/FLTK_PATCHES.md):
// measurement must use the same threshold as the drawing code, or
// long key names ("Delete") would be drawn in a too-narrow key
// column and get truncated ("Del").
if (fl_utf_nb_char((const unsigned char*)k, (int) strlen(k))<=999) {
```

**位置 2（绘制）**：`menutitle::draw()` 快捷键绘制分支的长度判断（原约第 563 行）。

**修改前**：
```cpp
if (fl_utf_nb_char((const unsigned char*)k, (int) strlen(k))<=4) {
```

**修改后**：
```cpp
// Pecia patch (2026-08-09, see main/build/FLTK_PATCHES.md):
if (fl_utf_nb_char((const unsigned char*)k, (int) strlen(k))<=999) {
```

阈值放宽后所有快捷键统一走两列布局（修饰右对齐 + 键左对齐），
`Delete`/`F3`/`Ctrl+F` 左缘同列。

### 副作用说明
快捷键显示布局全局统一为列对齐（修饰列右对齐、键列左对齐）；
快捷键触发逻辑（`Fl::test_shortcut`）不受影响。

### 如何应用到新版本 FLTK
1. 打开新版本的 `src/Fl_Menu.cxx`
2. 在 `menutitle::draw()` 的快捷键绘制分支，将长度判断阈值
   `<=4` 改为 `<=999`
3. 重新编译 FLTK 库，再构建 Pecia

### 验证
- 弹出菜单：Delete、F3、Ctrl+F 的快捷键文本左缘对齐同一列
- 快捷键触发、菜单功能不受影响

### 备份
修改前原文件备份于 `.bf.c++/0809-2004 FLTK源码修改/Fl_Menu.cxx`

---

## Patch 4：禁用编辑区内置右键菜单（2026-08-10）

### 现象
FLTK 1.4 的 `Fl_Text_Display` 内置右键上下文菜单（Cut/Copy/Paste/Select All，
src 内 `rmb_menu` + `handle_rmb()`）。Pecia 是极简编辑器，不需要该菜单。

### 修改文件
`src/Fl_Text_Display.cxx`

### 修改内容
**位置**：`handle()` 的 `FL_PUSH` 分支中 `if (Fl::event_button() == FL_RIGHT_MOUSE)`
块（原约 4243 行）。

**修改前**（弹右键菜单 + 处理返回动作）：
```cpp
if (Fl::event_button() == FL_RIGHT_MOUSE) {
  switch (handle_rmb(1)) { ... }
  return 1;
}
```

**修改后**（仅吃掉右键事件，不弹菜单）：
```cpp
if (Fl::event_button() == FL_RIGHT_MOUSE) {
  // PATCHED by Pecia (see main/build/FLTK_PATCHES.md):
  // disable the built-in right-click context menu.
  return 1;
}
```

### 如何应用到新版本 FLTK
1. 打开新版本 `src/Fl_Text_Display.cxx`
2. 在 `FL_PUSH` 分支找到 `if (Fl::event_button() == FL_RIGHT_MOUSE)` 块
3. 删除 `handle_rmb(1)` 的 switch，仅保留 `return 1;`
4. 重新编译 FLTK 库，再构建 Pecia

### 验证
- 编辑区鼠标右键：无菜单弹出，无其他副作用（复制选择仍可用 Ctrl+C）

### 修改后完整文件归档
`main/patches/fltk-1.4.5/src/Fl_Text_Display.cxx`

---

## Patch 5：菜单高亮切换强制走双缓冲合成（2026-08-11）

### 现象
Patch 1 把菜单窗口改为 `Fl_Double_Window` 后，鼠标在弹出菜单项间
移动时高亮切换**仍有明显闪烁**（比 Patch 1 前弱）。

### 根因
高亮切换入口 `menuwindow::set_selected()` 用 `damage(FL_DAMAGE_CHILD)`
标记重绘，而 `Fl_Double_Window::flush()` 对 `FL_DAMAGE_CHILD` 有特判：
**直接走单缓冲路径（`Fl_Window::flush()`）把内容画到屏幕，绕过离屏
合成**。因此高亮切换的“擦除旧项 + 绘制新项”两步仍在屏幕上直接发生，
中间帧可见 → 闪烁。

Patch 1 消除的是整窗重绘（菜单弹出/关闭、尺寸变化）的闪烁，
高亮切换路径不受影响。

### 修改文件
`src/Fl_Menu.cxx`（与 Patch 1/2/3 同一文件）

### 修改内容
**位置**：`menuwindow::set_selected()`（原约第 602-604 行）。

**修改前**：
```cpp
void menuwindow::set_selected(int n) {
  if (n != selected) {selected = n; damage(FL_DAMAGE_CHILD);}
}
```

**修改后**：
```cpp
void menuwindow::set_selected(int n) {
  // PATCHED by Pecia (see main/build/FLTK_PATCHES.md Patch 5)
  if (n != selected) {selected = n; damage(FL_DAMAGE_ALL);}
}
```

### 原因
`FL_DAMAGE_ALL` 使 `Fl_Double_Window::flush()` 走双缓冲合成路径：
内容先画到离屏缓冲，再一次性上屏。`menuwindow::draw()` 内部本身
就有增量逻辑（`drawn_selected != selected` 时只重绘新旧两个高亮项），
其余项保留在缓冲中，整窗重绘开销可忽略（菜单窗口仅几十项）。

### 如何应用到新版本 FLTK
1. 打开新版本的 `src/Fl_Menu.cxx`
2. 将 `menuwindow::set_selected()` 中的 `damage(FL_DAMAGE_CHILD)`
   改为 `damage(FL_DAMAGE_ALL)`
3. 重新编译 FLTK 库，再构建 Pecia

### 验证
- 鼠标在弹出菜单项间快速移动：高亮切换不再闪烁
- 菜单弹出/关闭/子菜单/键盘导航不受影响

---

---


---

## Patch 6：撤销事务（Undo Transaction）— Fl_Text_Buffer（2026-08-12）

**文件**：`src/Fl_Text_Buffer.cxx`、`FL/Fl_Text_Buffer.H`
**归档**：`main/patches/fltk-1.4.5/src/Fl_Text_Buffer.cxx`、
`main/patches/fltk-1.4.5/FL/Fl_Text_Buffer.H`

### 背景
Lua 脚本 / AI 插入会逐字符/逐操作产生几十条独立撤销记录，一次 Ctrl+Z
只能回滚一步。需要在 API 层显式分组（事务）——模式 4（事务/API 显式分组型）。

### 修改内容
1. `Fl_Text_Undo_Action` 新增 `bool group;`（事件属于事务）。
2. `Fl_Text_Buffer` 新增 `int mUndoGrouping;`（嵌套计数器）+ 公开 API
   `undo_begin()` / `undo_end()`（引用计数，支持嵌套事务）。
3. `insert_` / `remove_` 生成**新**撤销事件时标记 `group = (mUndoGrouping > 0)`
   （合并续接的事件保持原标志不变）。
4. `undo()`：撤销当前事件后，若该事件属于事务，连续撤销后续所有
   `group=true` 的事件——一次 undo 回滚整个事务，不越界吞掉事务前的
   普通事件。
5. `redo()`：同理，一次 redo 重做整个事务；重做期间临时设置
   `mUndoGrouping`，使 redo 生成的撤销事件**继承事务标志**（redo 后再
   undo 仍按整组回滚）；redo 时把 undo() 弹出的历史事件 push 回
   `mUndoList`（与 undo() 对称，保证后续 undo 仍能继续回溯）。
6. **根因修复（历史 replace 栈错位 bug）**：`insert_` / `remove_` 在
   `apply_undo` 执行期间（`mRedoList->locked()`，`Fl_Text_Undo_Action_List`
   新增公开 `locked()` 访问器）**不再向 mUndoList push 中间事件**（改为
   释放空 action）；`undo()` 的弹出逻辑相应简化为单次 pop。此前
   replace() 撤销事件内部 remove_+insert_ 各 push 一次，导致 `undo()`
   弹错事件——下一次撤销会静默吞掉历史或错乱（Pecia 曾用 `text()`
   清空撤销栈规避，代价是脚本结果完全不可撤销）。
7. **合并边界**：`insert_` / `remove_` 的事件合并条件改为
   `group == (mUndoGrouping > 0)`——事务内不续接事务前的普通事件，
   事务结束后普通输入也不续接组事件（事务保持原子、可独立回滚）。

### 验证
- 构建：13/13 单测通过（build.bat 全量）。
- 独立测试程序（`temp/undo_transaction_test.cpp`）26/26 通过：事务
  原子回滚/重做、嵌套事务、事务与普通输入交错、事务内 replace、
  空事务、replace 后连续 undo/redo 不丢事件。

### 如何应用到新版本 FLTK
1. 打开新版 `src/Fl_Text_Buffer.cxx` 与 `FL/Fl_Text_Buffer.H`
2. 按上述 7 点移植（头文件声明 + 成员 + 事件标记 + undo/redo 循环 +
   apply 期间不记录历史 + 合并边界条件）
3. 重新编译 FLTK 库，再重建 Pecia

---

## Patch 7：菜单复选项改为 Fill 式方框（对齐 Find 栏 Match Case）（2026-08）

### 现象
下拉菜单中的复选项（如“状态栏”“Word Wrap”等）勾框是
“边框 + 打勾”样式；而 Find 栏的 Match Case 复选框是
**选中时方框整体填满高亮色、未选时只有空边框透明**，视觉语言不一致。

### 修改文件
`src/Fl_Menu.cxx`（与 Patch 1/2/3/5 同一文件）

### 修改内容
**位置**：`Fl_Menu_Item::draw()` 中 `FL_MENU_TOGGLE && !FL_MENU_RADIO` 分支
（原约第 331-339 行）。

**修改前**（方框 + 打勾）：
```cpp
} else { // FL_MENU_TOGGLE && ! FL_MENU_RADIO
  // Flat bordered checkbox + check (chosen style: flat square box).
  fl_draw_box(FL_BORDER_BOX, x+2, y+d, W, W, FL_BACKGROUND2_COLOR);
  if (value()) {
    fl_draw_check(Fl_Rect(x+3, y+d+1, W-2, W-2), check_color);
  }
  x += W + 3;
  w -= W + 3;
}
```

**修改后**（选中填色、未选空框，匹配 Fl_Check_Button + FL_BORDER_BOX）：
```cpp
} else { // FL_MENU_TOGGLE && ! FL_MENU_RADIO
  // Filled square box (matches the find bar "Match Case" check box):
  // unchecked = hollow border, checked = the box is filled with the
  // menu's selection (highlight) colour. No checkmark is drawn.
  Fl_Color cb_col = value() ? (m ? m->selection_color() : FL_SELECTION_COLOR)
                            : FL_BACKGROUND2_COLOR;
  fl_draw_box(FL_BORDER_BOX, x+2, y+d, W, W, cb_col);
  x += W + 3;
  w -= W + 3;
}
```

### 原因
`Fl_Check_Button`（Find 栏 Match Case）在 `down_box(FL_BORDER_BOX)` 时，
`Fl_Light_Button::draw()` 走 default 分支：
`draw_box(FL_BORDER_BOX, ..., value() ? selection_color() : color())`——
选中＝方框填 `selection_color`，未选＝`color()`（透明空框），**不画勾**。
菜单复选项改为同一套逻辑，勾框样式与 Find 栏完全一致。

`FL_MENU_RADIO`（单选）保持原“方框 + 圆点”画法，未改动。

### 如何应用到新版本 FLTK
1. 打开新版本的 `src/Fl_Menu.cxx`
2. 将 `FL_MENU_TOGGLE && !FL_MENU_RADIO` 分支改为上述“选中填色、未选空框”逻辑
3. 重新编译 FLTK 库，再构建 Pecia

### 验证
- 构建：15/15 单测通过（build.bat 全量）。
- 打开任意包含复选项的下拉菜单：选中项方框填高亮色、未选项空框，
  与 Find 栏 Match Case 视觉效果一致。
