# FLTK 源码修改记录

此文件记录对 `.thirdparty/fltk-*` 源码的所有修改。
升级 FLTK 时按此文件重放修改。

## Fl_Menu_.H — check_color() 属性 (2026-09-07)

在 `Fl_Menu_` 类中新增 `check_color_` 成员和 `check_color()` / `check_color(Fl_Color)` 方法。
用于分离菜单勾选框填充色与行悬停高亮色：行悬停仍用 `selection_color()`，
勾选框填充用 `check_color()`（Pecia 设为 highlight2）。
默认值 0 表示回退到 `selection_color()`。

**修改文件：** `FL/Fl_Menu_.H`

## Fl_Menu.cxx — 勾选框填充使用 check_color() (2026-09-07)

`Fl_Menu_Item::draw()` 中，FL_MENU_RADIO 和 FL_MENU_TOGGLE 的勾选框填充色
从 `m->selection_color()` 改为 `m->check_color()`（为 0 时回退到 `selection_color()`）。

**修改文件：** `src/Fl_Menu.cxx`

## Fl_Menu.cxx — g_popupCheckColor 回退 (2026-09-10)

`Fl_Menu_Item::draw()` 中，勾选框填充色新增 `g_popupCheckColor` 作为中间回退：
`check_color()` > `g_popupCheckColor` > `selection_color()` > `FL_SELECTION_COLOR`。

原因：右键弹出菜单使用静态 `button` 变量获取 `check_color()`，但当 View 子菜单
弹出时 `pulldown()` 递归调用会覆盖 `button`，导致后续绘制使用错误的菜单引用。
`g_popupCheckColor` 由 Editor 在 popup() 前设置为 theme highlight2，popup() 后重置为 0。

**修改文件：** `src/Fl_Menu.cxx`

## Fl_Text_Display.cxx — 下划线厚度 1px (2026-09-10)

`draw_string()` 中 `ATTR_UNDERLINE` 分支的下划线厚度从 `pitch = fsize/7`（16px 字体
下为 2px）改为固定 `pitch = 1`（1px）。使 URL 等下划线标记更纤细精致。

**修改文件：** `src/Fl_Text_Display.cxx`

## Fl_Menu.cxx — 禁用内部 fakemenu 冗余窗口 (legacy Patch 2)

为子菜单/多级菜单绘制时，FLTK 会在父菜单之上额外创建一个 `Fl_Menu` 窗口
（fakemenu）用于承载子项。Pecia 改为**不创建该冗余窗口**（沿用父菜单绘制），
并清理 `\title` 等内部状态时一并处理。避免多级菜单下的双层覆盖窗口与额外
重绘开销。

**修改文件：** `src/Fl_Menu.cxx`

## Fl_Menu.cxx — 菜单高亮切换强制双缓冲 (legacy Patch 5)

菜单项高亮（hover / 当前项）切换时，强制走双缓冲重绘路径，避免高亮残影/闪烁。

**修改文件：** `src/Fl_Menu.cxx`

## Fl_Text_Buffer(.H/.cxx) — 撤销事务分组 (legacy Patch 6)

为 `Fl_Text_Buffer` 增加**撤销事务（Undo Transaction）**机制，使一次程序化多步
编辑（如脚本替换选区、批量插入）在撤销栈中合并为**单个 undo 步骤**：

- 新增 `begin_undo_transaction()` / `end_undo_transaction()` 方法；
- 内部用 `mUndoGrouping` 嵌套计数跟踪事务深度，仅在最外层事务结束时落盘一条
  undo 记录；
- `insert_()` / `remove_()` 在事务进行中时，将本次编辑事件标记为同一 `group`，
  撤销/重做时整组一起回放，避免历史被海量细碎事件污染；
- 暴露 `mUndoGrouping` 供 Pecia 的 buffer 锁定逻辑读取事务状态。

**修改文件：** `src/Fl_Text_Buffer.cxx`、`FL/Fl_Text_Buffer.H`

## Fl_Text_Display.cxx — 垂直滚动量程封顶 (2026-10-05)

`update_v_scrollbar()` 把滚动条量程封顶到 `mTopLineNum`，消除三处独立计算的
滚动上限不一致导致的「拖到底仍差 1~6 像素」与长文档滑块死区。封顶守卫只在
真边界（文档末行）生效，中途状态不受影响。

**修改文件：** `src/Fl_Text_Display.cxx`

## Fl_Scrollbar.cxx — 滚动条单一整轨道（去箭头区）(2026-10-13)

上游 `Fl_Scrollbar::handle()` 与 `draw()` 在 `H >= 3*W`（竖向）时把轨道两端各
抠掉一个 W 像素的箭头区再做命中/绘制。Pecia 的滚动条箭头由自绘主题覆盖
（不可见、不可用），这 20px 死区导致「可见滑块」与「命中区」错位约 10px
（滑块在上半部要按下方才抓得住）。现移除两处箭头区裁剪：滚动条 = 一条完整
轨道，`handle()` 命中、`draw()` 绘制与 Pecia 自绘几何（`scrollbarThumbGeometry`）
三方对齐；滑块仍可贴到轨道最底。

**修改文件：** `src/Fl_Scrollbar.cxx`

## Fl_Menu.cxx — 弹出菜单分隔线扁平化（2026-10-22）

`Fl_Menu_Item::draw()` 中，`FL_MENU_DIVIDER` 分隔线由上游的
`FL_DARK3` + `FL_LIGHT3` 双色立体线改为**一条 1px 纯色扁平线**，颜色取
`extern Fl_Color g_borderColour`（定义在 Pecia 的 `core/Theme.cpp`，
= 主题 text1：亮色主题≈纯黑，暗色主题自动浅色，与窗口边框同色系）。
所有弹层（菜单栏下拉、脚本条、编辑器右键菜单、Fl_Choice 下拉）共用
同一条绘制路径，因此分隔线全项目自动统一，无需各处单独设置。
新增全局引用与既有 `g_popupCheckColor` 同模式（FLTK 工具 fluid/
fltk-options 不链接 Theme.cpp，不引用该符号则不受影响）。

**修改文件：** `src/Fl_Menu.cxx`

## Fl_Menu.cxx — 弹出菜单外发光钩子（2026-10-22）

menuwindow::show() / hide() 调用 Pecia 的 peciaMenuShadowShown/Hidden，
把 ui/WindowShadow.cpp 的同一套外发光（同一组全局参数：宽度/透明度/
Gamma 曲线）附着到所有弹出菜单：菜单栏下拉、右键菜单、Fl_Choice 下拉、
多级子菜单。关闭菜单时先销毁阴影再隐藏原生窗口，避免残留。

Pecia / PeciaLua / PeciaAIChat / 两个 UI 冒烟测试链接 ui/WindowShadow.cpp，
提供真实实现；其他只链接 ltk.lib 的目标由 /alternatename 落到
Fl_Menu.cxx 内的空桩，第三方库仍可独立链接。

**修改文件：** src/Fl_Menu.cxx
