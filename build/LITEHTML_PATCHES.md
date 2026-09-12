# litehtml 源码修改记录

此文件记录对 `.thirdparty/litehtml-*` 源码的所有修改。升级 litehtml 时按此文件重放修改。

## src/document_container.cpp — 超长非 CJK 词拆词断行 (2026-09)

`word_break` 分词逻辑中，对**不含空格的超长非 CJK 词**（如长 URL、长英文串）
按 **每 8 个字符**切分为多个词，使其能在预览区内正常换行，避免单个超长词把
文档撑宽、产生水平滚动条。CJK 汉字本就逐字分词，不受此改动影响。

**修改文件：** `src/document_container.cpp`
（代码注释见 `// PATCHED by Pecia (see build/LITEHTML_PATCHES.md):`）
