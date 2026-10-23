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

## css_properties 共享 — P0 预览内存优化 (2026-10-20)

### 动机

`element::m_css` 原本是**值成员**（992 B），40,247 个 DOM 节点各持一份 →
`38.1 MB` 纯粹用于存 CSS 计算结果，占当时 86.8 MB 峰值的 44%。而 markdown
产出的 HTML 里样式高度同质（`td` 3,657 个、`tr` 943 个，同一父亲下的文本节点
样式完全相同），绝大多数副本内容逐字节一致。

### 改动清单

| 文件 | 改动 |
|---|---|
| `include/litehtml/element.h` | `m_css` 改为 `std::shared_ptr<css_properties>`；`css()` 语义不变（仍返回引用）；`css_w()` 改为**写时复制**（`use_count() > 1` 时克隆）；新增 `intern_styles()` |
| `src/element.cpp` | 构造函数预分配实例；实现 `intern_styles()`（计算签名 → 查文档级样式池 → 命中则换用共享实例） |
| `include/litehtml/document.h`、`src/document.cpp` | 新增 `m_css_pool` 与 `intern_css()` / `clear_css_pool()`；`render()` 开头清池，避免池把已克隆出去的实例钉住 |
| `include/litehtml/css_properties.h`、`src/css_properties.cpp` | 新增 `operator==`（逐字段，用宏 `LH_STYLE_CMP` 展开以免漏写） |
| `css_length.h` / `css_margins.h` / `css_offsets.h` / `css_position.h` / `borders.h` / `background.h` / `types.h` | 给参与比较的子类型补 `operator==` |
| `src/html_tag.cpp`、`src/el_text.cpp`、`src/el_style.cpp` | `compute_styles()` 末尾调用 `intern_styles()` |
| `src/el_image.cpp` | 连带改动：`m_css` 变成 `shared_ptr` 后，`m_css.set_display(...)` 必须写成 `m_css->set_display(...)`。漏了这一处链接会失败 |
| `src/html_tag.cpp`、`src/document.cpp` | 删除前期排查遗留的 `fprintf(stderr, "[DBG] ...")`（4 处，其中 `get_font` 在绘制热路径上，每文档数万次） |

### 两个必须守住的不变量

1. **签名必须包含父元素的样式实例地址**。`color`、`font-*`、`text-align`、
   `white-space` 等是**继承属性**，子元素取值来自父实例。若签名只由
   tag/class/id/style 组成，"红色 div 下的 span" 与 "绿色 div 下的 span"
   会被判为同一份样式而合并 → 静默串味。
2. **签名只用于分桶，不等于相等**。哈希相同的两个样式仍要逐个字段深比较
   （`operator==`）才能共享，否则哈希碰撞会导致错误合并。

写入语义（`css_w()`）走写时复制：先断开共享再改，所以布局阶段（如
`table.cpp` 给每个 `td` 回填计算宽度）不会污染共享伙伴。

### 为什么 `background` 里的渐变不参与相等比较

`background::m_image` 的元素类型是 `litehtml::image`（`gradient.h`），内含完整
`gradient` 对象。为它写深比较要连带 `gradient`、`color_stop` 一路补下去，成本
远高于收益：markdown 预览不产生 CSS 渐变背景，且**比较失败只会少共享、不会错
共享**。故 `image::type_gradient` 一律返回不相等。此权衡已在代码注释中标明。

### 回归保护

`main/test/test_litehtml_css_pool.cpp`（ctest 项 `litehtml_css_pool`）覆盖四种情况：

- 每个 setter 单独改动都必须打破相等（漏字段 → 该测试变红）
- 重复结构文档确实发生共享
- 同 tag 不同父色**不得**合并
- 写时复制后，共享伙伴取值不变

`test_preview_mem.cpp` 中原有的一条断言 `v_el >= v_css`（"css 确实是内联的"）
是这条改动的**反向门禁**，已按新事实翻转为 `v_el < v_css`，并说明原因。

## 深嵌套输入的栈溢出防护 — P1 (2026-10-20)

### 动机（实测，不是推测）

litehtml 与它内嵌的 gumbo 有多处"跟着输入深度递归"的代码，且**都没有上限**。
在 MSVC 构建（即 Pecia 实际分发的那份）上实测，栈只有 1 MB，崩溃点低得危险：

| 输入形态 | 崩溃深度 | 退出码 | 输入体积 |
|---|---|---|---|
| `<div>` 嵌套 | **1000** | `0xC00000FD` | 约 11 KB |
| 未闭合 `<div><span>` | **1000** | `0xC00000FD` | 约 11 KB |
| `var(--a,var(--a,...))` | **2000** | `0xC00000FD` | 约 18 KB |
| `calc(calc(...))` | ~2000 | `0xC00000FD` | — |

`0xC00000FD` 是 `STATUS_STACK_OVERFLOW`，**不是可捕获的异常**：进程直接消失，
用户未保存的编辑内容一起没了。而 `md4c` 会把 Markdown 里的裸 HTML 原样透传，
所以这不是理论输入——粘贴一段手写 HTML 就可能触发。

同一个探针用 MinGW 构建时崩溃点高一倍（2000 / 5000），因为 MinGW 默认栈更大。
**只测 MinGW 会严重低估风险**，这是本次必须先在产品实际构建上复测的原因。

### 三处改动

| 文件 | 改动 |
|---|---|
| `src/document.cpp`、`include/litehtml/document.h` | `create_node()` 增加 `depth` 参数，入口处 `kMaxDomDepth = 200` 守卫。**只挡这一处就能保护整条下游链**：`create_render_item` / `init` / `compute_styles(true)` / 递归析构都走同一棵 DOM 树，树深有界则它们全部有界 |
| `src/css_parser.cpp`、`include/litehtml/css_parser.h` | `consume_component_value()` 在 `kMaxCssNesting = 100` 处停止展开；新增 `m_nesting` 成员，由 `consume_function` / `consume_simple_block` 里的 RAII `NestingScope` 维护（这两个函数各有多个 `return` 点，手工递减必漏） |
| `src/gumbo/parser.c` | `destroy_node()` 由递归改写为**迭代**：前序收集进 `GumboVector`，再逆序释放。逆序前序 = 后序，子节点仍先于父节点释放，与原语义等价。额外内存 8 B/节点 |

### 为什么连 gumbo 也要改（关键定位过程）

只加了前两处守卫后，崩溃点从 1000 抬到 16000–32000 层，但**没有根除**。用阶段性
标记定位：

```
STAGE parse_begin html=352040
STAGE parse_done          <- 解析成功
（没有再出现 STAGE destroy_done，进程已死）
```

崩溃发生在 `gumbo_destroy_output()` → `destroy_node()` 的递归析构里，即**解析成功
之后**。这也是它格外隐蔽的原因：崩溃点离"读文档"的代码很远，看起来像随机数据丢失。

注意一个容易误判的细节：`<div>` 嵌套在 32000 层崩、未闭合 `<div><span>` 在 16000
层崩——数字不同，但两者都在 **32000 个标签**时崩，说明瓶颈是"标签数"（树深）而非
字符体积。

### 修复后实测（同一探针、同一 MSVC 构建）

四个变体在 200/1000/2000/4000/8000/16000/32000/**64000** 层全部 `ok`，崩溃计数 0。
从 1000 层提到 >64000 层，且 CSS 侧彻底不再随深度退化（此前 4000 层 `var()` 嵌套
耗时 830 ms，代价还是超线性的）。

### 阈值取值理由

- `kMaxDomDepth = 200`：真实文档嵌套极少超过几十层，浏览器自身也在 512 量级；
  200 距离实测崩溃点 1000 有 5 倍余量，可吸收不同编译器/平台每帧栈大小的差异。
- `kMaxCssNesting = 100`：真实样式表连两层 `calc()` 都少见，距离崩溃点 2000 有 20 倍余量。

超限后的行为是**内容被截断/平铺而非嵌套**。这类输入本身已超出可用范围，取舍明确：
宁可少渲染一小段，也不能让整个进程消失。`m_index` 仍单调前进，因此各消费循环
一定终止，不会死循环。

### 回归保护

`main/test/test_nest_depth.cpp`（ctest 项 `nest_depth`）对 4 种形态各跑 5000 层，
每个用例在**独立子进程**中执行（栈溢出无法在进程内恢复，`__try` 也救不回来；
子进程还顺带避开 MSVC C2712"含对象展开的函数不能用 `__try`"）。

**门禁已做双向验证**：把两个常量临时改成 1000000（等于关掉守卫）后重编，
4 个用例全部 `STACK_OVERFLOW` 且退出码为 1；恢复后重新变绿。一个不会变红的门禁
等于没有门禁。

> 注意：`3_patch.bat` 原先对 litehtml 只平铺同步 `src/*` 与 `include/litehtml/*`，
> **不会递归到 `src/gumbo/`**。若漏掉这段，`parser.c` 的补丁会被归档却永不生效。
> 已补上 `src/gumbo/*` 的同步，并用 mtime 变化实测确认复制真的发生。


## 行内盒按索引整体平移 — 分块渲染支撑 (2026-10-23)

### 动机

分块布局（`main/mdview/block_layout.*`）会把一次完整排版切成多块分别定位，
块高先估算、实测后再校正。校正时该块内的**行内盒**要跟着整体下移，而上 API
只有 `y_shift()`——它作用于整个 render_item，无法只移动该 item 的某一个行内盒。
于是补一条"按索引平移单个行内盒"的通路。

### 改动清单

| 文件 | 改动 |
|---|---|
| `include/litehtml/render_item.h` | 基类新增两个虚接口（默认空实现，不影响其他派生类）：`shift_inline_box(size_t index, pixel_t shift)`、`get_inline_boxes_count()` |
| `include/litehtml/render_inline.h` | 实现这两个接口：`shift_inline_box()` 只把 `m_boxes[index].y` 加 `shift`（越界则不动）；`get_inline_boxes_count()` 返回 `m_boxes.size()` |
| `include/litehtml/line_box.h` | `line_box_item` 新增 `m_box_index`（默认 -1）与 `set_box_index()` / `get_box_index()` |
| `src/line_box.cpp` | ① `line_box_item::y_shift()`：`m_box_index >= 0` 时走 `shift_inline_box(m_box_index, shift)`，否则退回原来的 `m_element->y_shift(shift)`；② `new_box()` 内联分支里，清空行内盒后调用 `set_box_index(该 el 当前行内盒数量)` 登记索引；③ 顺手修正一处 `}` 缩进 |

### 不变量

- `m_box_index` 只在 `new_box()` 的内联分支里赋值；其余路径保持 -1，因此
  非内联元素的行为与上游**逐字一致**。
- `shift_inline_box()` 越界直接返回，不抛异常、不改容器大小。
- 这两个新接口是新增虚函数，不改已有签名，升级上游时冲突面只有这 4 个文件。

> 这 4 个文件 + 上面 `src/el_image.cpp` 共 5 个文件此前只躺在 `patches/` 里被
> `3_patch.bat` 应用，本文档没有记录（2026-10-24 对照上游 `litehtml-0.10.tar.gz`
> 逐文件 diff 时才发现）。已按实际差异补齐本节。
