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

- 新增 `undo_begin()` / `undo_end()` 方法；
- 内部用 `mUndoGrouping` 嵌套计数跟踪事务深度，仅在最外层事务结束时落盘一条
  undo 记录；
- `insert_()` / `remove_()` 在事务进行中时，将本次编辑事件标记为同一 `group`，
  撤销/重做时整组一起回放，避免历史被海量细碎事件污染；
- 暴露 `mUndoGrouping` 供 Pecia 的 buffer 锁定逻辑读取事务状态。

**修改文件：** `src/Fl_Text_Buffer.cxx`、`FL/Fl_Text_Buffer.H`

## Fl_Text_Display.cxx — 垂直滚动量程封顶 (2026-10-05, 2026-11-25 修正)

`update_v_scrollbar()` 与 `scroll_()` 共用**同一个**下限值：`scroll_()` 计算
并记录在本实例成员 `mVScrollAppliedLimit` 里，`update_v_scrollbar()` 直接复用，
消除三处独立计算导致的「拖到底仍差 1~6 像素」与长文档滑块死区。

2026-11-25 两处修正：
1. 该记录值原本是**文件级 static**，同进程多个 `Fl_Text_Display`
   （主编辑器 / Lua 工具两个窗格 / AI 聊天两个窗格）互相覆盖，某个窗口的
   量程会按别的窗口的缓冲区行数计算。现改为实例成员（声明在补丁后的
   `FL/Fl_Text_Display.H`）。
2. 末行空行回拉循环的守卫 `mTopLineNum > bottom_line` 经实测确认**恒假**
   （`scroll_()` 把顶行钳制到同一 limit），上游的回拉启发式因此实际不执行。
   尝试把它改成 `<` 会同时破坏 `test_vscroll_reach` 的两条契约（拖动过程
   失去单调推进、短文档带尾随空行时不再停在 top==1）——该启发式与
   PECIA_BLANK_ROWS_PAST_END 的语义本就冲突，**维持现状（回拉保持关闭）是
   被测过的出货行为**，只记录不修改。
3. 同轮把 `update_v_scrollbar()` 的量程改为**按本实例当前行数即时重算**
   （`vscroll_bottom_line(mNBufferLines, mNVisibleLines)`，下限 1）。原实现
   读进程级 static 缓存：新窗口会继承上一个窗口拖到底时留下的大 limit
   （test_vscroll_reach 契约 5 过去正是骑在这份"串窗污染"上才到 max=372），
   而全新实例若不继承又会被 `scroll_()` 的钳制值卡住量程。即时重算让量程
   与 `scroll_()` 用同一条公式（文档中"两处必须同一个 limit"的不变量由
   公式而非缓存保证），`mVScrollAppliedLimit` 仅保留为 `scroll_()` 的记录值
   供诊断，不再参与量程计算。

**修改文件：** `FL/Fl_Text_Display.H`、`src/Fl_Text_Display.cxx`

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

Pecia（含 AI 对话窗）与两个 UI 冒烟测试链接 ui/WindowShadow.cpp，
提供真实实现；其他只链接 ltk.lib 的目标由 /alternatename 落到
Fl_Menu.cxx 内的空桩，第三方库仍可独立链接。

**修改文件：** src/Fl_Menu.cxx

## Fl_Text_Display.cxx — 中英混排 / 超长 token 换行（2026-10-24）

在原有“像素贪心 + CJK 断点 + 标点禁则”基础上做最小增强，不做 K-P、不做全局回看：

- 新增断点：CJK ↔ 拉丁/数字边界（UAX #14 允许；解决“中文English”整段不可断）。
- 单 token 前瞻：破行前先看下一个不可断 token 的宽度；若 token 本身比整行还宽
  （URL、长路径、无空格长串），不再把它整段挪到下一行，而是在当前行内继续填充。
- 弱断点：仅在超长 token 内部、或整行没有任何强断点时，允许在 `- _ / \ ? & = #`
  之后断开；且只有几乎不损失填充（≥ 90% 行宽）才采用，避免 URL 被切得七零八落。
- 禁则兜底：字符级回退时向前回退，避免下一行以 `。，、；：！？）》】` 开头；
  并把漏掉的起始括号 `〈《〖〘〚〝`（U+3008/300A/3016/3018/301A/301D）
  补进“禁止在行尾断开”的列表（旧表只有 「『【〔，导致出现“行尾留下《”）。
- 右侧留白：原实现为防测量漂移预留“一个字宽”的 slack；旧 CJK 断点的
  retPos 偏移又把这一格吃掉，所以旧版看起来每行都少一个字。修好偏移后
  slack 收紧为 `textsize()/3`（约 1/3 em），右端与旧版视觉宽度对齐。
- 修正原有 CJK 断点的 retPos 偏移：之前 `retPos = next(b)` 会让下一行从
  断点字符之后开始（导致收尾标点跑到行首）；现在 `retPos` 与 `newLineStart` 一致，
  同时 `retLineEnd` 按“是否跳过断行空白”计算。旧算法在 ex4 用例里出现的
  14 处行首收尾标点违规降为 0。
- 性能：普通多行文档的 wrap 布局耗时与旧版基本持平（10 MB ≈ 10 ms）；
  病态“单行 10 MB”文件约 1.6×（0.52 s → 0.82 s），且 >10 MB 默认关闭 wrap。

**最终 A/B（ex4，排除段末最后一行）**：520/640/800/1000 px 的平均行尾空白
4.34/3.09/3.88/2.54% → 1.61/0.94/1.89/0.81%；行首行尾标点违规 14/7/8/5 → 0/0/0/0。
普通英文单词内部断开 0 处；仅超长 URL/路径/无空格串在必要时内部断开（26→11 处）。

**修改文件：** `src/Fl_Text_Display.cxx`
**验证：** `temp/ab_wrap/`（旧/新 exe、逐行 CSV、截图与对比图）；
`ex4.WrapTest/wrap-cases.txt` 的 16 组用例在 520/640/800/1000 px 下对比。

## Fl_Text_Display.cxx — 禁用内建右键菜单（2026-11-25 补记）

`handle()` 的 `FL_PUSH` 右键分支由「调用 `handle_rmb(1)` 弹内建
cut/copy/paste 菜单」改为直接 `return 1`（吃掉事件）。该改动自 1.0.0 起
存在于源码但本文档从未记录，且代码注释曾错误地指向本文档（"see
main/build/FLTK_PATCHES.md"）——现补上这条真实声明。

- 动机：主编辑器与预览区共用 Pecia 自建的右键菜单（`editor/Editor.cpp`
  的 buildContextMenu，编辑/预览完全等同是用户定案），内建菜单会与之冲突。
- 代价（明知并接受）：不走 `Editor` 类的显示件（Lua 工具窗脚本区/输出区、
  AI 聊天历史/输入框）右键变成无操作；`Fl_Input` 系单行控件有自己的
  `handle_rmb`，不受影响。若将来需要给工具窗补右键，走同一份
  Fl_Menu_Item 公共路径，不要恢复内建菜单。

**修改文件：** `src/Fl_Text_Display.cxx`

## Fl_Text_Display.cxx — 多实例滚动量程修复落地 (2026-11-27)

按上文「垂直滚动量程封顶」条目中 2026-11-25 修正第 3 点（即时重算）正式落地：

- 构造函数初始化本实例成员 `mVScrollAppliedLimit = 0`；
- `scroll_()` 把实际应用的 limit 记录到本实例成员 `mVScrollAppliedLimit`，仅供诊断；
- `recalc_display()` 与 `update_v_scrollbar()` 改为按**本实例当前行数**用
  `vscroll_bottom_line(mNBufferLines, mNVisibleLines)` 即时重算量程（下限 1），
  `mVScrollAppliedLimit` 不再参与量程计算；
- 原文件级 `static int vscroll_applied_limit` 已删除，仅在源码中保留历史注释，不再参与任何逻辑。

多个 `Fl_Text_Display` 实例不再互相覆盖量程。上文 71-72 行「`update_v_scrollbar()`
直接复用」为本次落地前的旧表述，最终行为以本条及 2026-11-25 修正第 3 点为准。
`FL/Fl_Text_Display.H` 早已声明 `mVScrollAppliedLimit`，本次无需改动该头文件。

**修改文件：** `src/Fl_Text_Display.cxx`
**待验证：** 由主审统一构建，并回归 `test_vscroll_reach` / `test_vscroll_geom`（本次未跑构建）。
