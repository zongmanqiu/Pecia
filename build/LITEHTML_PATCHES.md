# litehtml 源码修改记录

此文件记录对 `.thirdparty/litehtml-*` 源码的所有修改。升级 litehtml 时按此文件重放修改。

## src/document_container.cpp — 超长非 CJK 词拆词断行 (2026-09)

`word_break` 分词逻辑中，对**不含空格的超长非 CJK 词**（如长 URL、长英文串）
按 **每 8 个字符**切分为多个词，使其能在预览区内正常换行，避免单个超长词把
文档撑宽、产生水平滚动条。CJK 汉字本就逐字分词，不受此改动影响。

**修改文件：** `src/document_container.cpp`
（代码注释见 `// PATCHED by Pecia (see build/LITEHTML_PATCHES.md):`）

## include/litehtml/types.h — position::round() 改为边对齐取整 (2026-10)

`position::round()` 原来对 `x / y / width / height` **各自** `std::round`。这破坏
了「后一个元素紧接前一个元素结束」这一不变量：以行距 38.6px 为例，各行 y 取整成
20 / 59 / 97 / 136（间隔 39,38,39），而每个 height 都取整成 40。于是单元格的
**下边框**(y + height - bw) 与下一格**上边框**(y) 有时落在同一像素（线宽 1px）、
有时落在相邻像素（拼成 2px）—— 表现为「表格横线粗细不一致」，而浏览器打开同一份
HTML 完全正常（说明 CSS 没错，是 litehtml 取整的问题）。

改为**边对齐**取整，让相邻盒子的边界保持一致：

```cpp
pixel_t x2 = std::round(x + width);
pixel_t y2 = std::round(y + height);
x = std::round(x);
y = std::round(y);
width  = x2 - x;
height = y2 - y;
```

实测（离线探针 `temp/probe/litehtml_probe.cpp`）：改前同一表格的内部横线
1px/2px 交替，改后每条内部横线都落在同一像素、恒为 1px。

**修改文件：** `include/litehtml/types.h`
（代码注释见 `// PATCHED by Pecia (see build/LITEHTML_PATCHES.md):`）

> 注意：`3_patch.bat` 原先只同步 `patches/litehtml-0.10/src/*`，已补上
> `include/litehtml/*` 的同步，否则这个头文件补丁不会随补丁重放生效。
