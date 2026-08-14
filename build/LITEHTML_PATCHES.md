# LITEHTML Patches（本项目对 litehtml 源码的修改记录）

- 适用版本：**0.10（deps.txt 固定）**
- 库源码位置：`.thirdparty/litehtml-0.10/`
- **修改后完整文件归档**：`main/patches/litehtml-0.10/src/document_container.cpp`
  （构建前由 build.bat 从归档复制覆盖）

---

## Patch 1：超长非 CJK 词按字符拆词（长 URL / 长英文断行）

### 现象/动机
litehtml 的文本分词（`split_text`）只按空格切词（汉字 CJK 已逐字分词）。
**长 URL / 长英文**（无空格、非 CJK）整段作为一个"词"——行放不下时
整词换行仍超宽 → 撑宽文档 → 水平滚动（垂直分屏下体验差）。
litehtml 不支持 CSS `word-break`/`overflow-wrap`（搜索零结果）。

### 修改文件
`src/document_container.cpp` — `split_text()` 的 `else` 分支（非空格、非 CJK 字符）。

**修改前**：
```cpp
else
{
    str += c;
}
```

**修改后**：
```cpp
else
{
    str += c;
    // PATCHED by Pecia (see build/LITEHTML_PATCHES.md):
    // 超长非 CJK 词（长 URL / 长英文——无空格）按 8 字符拆成多个词，
    // 让行内布局能断行。否则整段一个词，放不下时整词换行仍超宽，
    // 撑宽文档导致水平滚动。汉字（CJK）已逐字分词，不受影响。
    if (str.size() >= 8)
    {
        on_word(utf32_to_utf8(str));
        str.clear();
    }
}
```

### 原理
`split_text` 的 `on_word` 回调为每个词创建独立 `el_text` 元素——拆词后
每个子词是独立元素，行内布局（`line_box::can_hold`）逐词判断，超宽时
在词边界换行。拆词粒度 8 字符（行尾最多留 8 字符空白，与之前
HTML span 方案粒度一致）。

### 为什么不用 HTML 层 span 方案
span 方案（在生成的 HTML 中插入 `<span>`）有几个结构性弱点：
- 只对超长段生效，需维护"跳过 pre / 实体"等边界
- 污染 index.html（输出产物不干净）
- 未来新元素需逐一考虑
split_text 拆词是**渲染引擎机制级**（与空格断行同等地位）——任何 HTML、
任何元素自动覆盖；index.html 保持干净（浏览器端 word-break CSS 已能换行）。

### 如何应用到新版本 litehtml
1. 打开新版 `src/document_container.cpp`
2. 在 `split_text()` 的 `else` 分支（非空格、非 CJK 字符累积处）：
   `str` 累积到 8 字符时调用 `on_word()` 并清空
3. 重新编译 litehtml（主构建全量重建会自动重编）

### 验证
- 长 URL / 长英文段落：按行宽自动断行，无水平滚动
- 汉字长句：逐字分词（原有行为）不受影响
- 代码块（pre-wrap）：按空格断行（原有）+ 超长行按 8 字符拆
- 公式 / mermaid（img 元素）：不受影响（拆分只作用于文本分词）
