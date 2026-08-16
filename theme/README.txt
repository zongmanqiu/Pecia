# Pecia 主题说明（theme/ 文件夹）

本目录存放颜色主题文件。每个 `.txt` 文件 = 一套完整配色；文件内部末尾
附带每个颜色键（参数）的作用说明。settings.ini 只记录当前选中的主题名
（theme.name = <文件名不带后缀>），颜色值都从这个文件读取。

## 内置主题
| 文件名        | 风格           | 说明 |
| ------------- | -------------- | ---- |
| light.txt     | 亮色（默认）   | 浅色、白底、原始终结 |
| dark.txt      | 暗色           | 深灰 + 蓝强调，护眼 |
| green.txt     | 绿意           | 墨绿主色（源自 lx-music） |
| blue2.txt     | 靛蓝           | 深蓝主色 |
| orange.txt    | 橙             | 暖橙主色 |
| red.txt       | 红             | 热情红主色 |
| purple.txt    | 紫             | 紫色主色 |
| ming.txt      | 青蓝           | 青绿主色 |

## 怎么用
1. 在程序里：`View(视图) → Theme(主题)` 选择。
2. 手动：改 `theme/<名字>.txt` 里的某个颜色，重启或切到该主题即生效。
   （运行中的程序改了文件不会热刷，需重启或重新切一次。）

## 新增主题
1. 复制 `light.txt` → 改名为 `theme/我的主题.txt`。
2. 编辑里面的颜色值。
3. 在程序里选它（需在 View>Theme 菜单加对应项，或后续做动态扫描）。

## 每个颜色参数控制哪些元素
值格式 `#RRGGBB` 或 `R,G,B`；缺失键用内置亮色兜底。

| 键                | 控制元素 |
| ----------------- | -------- |
| bg_chrome         | 标题栏/菜单/工具栏/状态栏/行号栏背景（主色） |
| scrollbar_thumb   | 滚动条滑块 |
| scrollbar_track   | 滚动条轨道背景 |
| bg_panel          | 菜单/搜索/替换/跳转面板背景 |
| bg_editor         | 编辑器/输入框背景（工作区） |
| text_primary      | 正文/菜单文字（主文字） |
| text_secondary    | 次要文字（注释/提示） |
| accent_selection  | 选中高亮 + 激活 tab 下划线 + 按钮/菜单选中（强调色） |
| search_highlight  | 查找全部命中高亮 |
| line_highlight    | 当前行高亮 |
| link_hover        | 超链接颜色 |
| hover_btn         | 标题栏按钮悬停背景 |
| hover_close       | 关闭按钮悬停背景 |
| border_color      | 窗口 1px 外边框线 |
