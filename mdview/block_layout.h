#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <memory>
#include <string>
#include <vector>

#include "litehtml.h"

namespace pecia {

// 分块（虚拟化）预览布局。
//
// 为什么要有它：litehtml 为一篇文档建出来的 DOM 是「节点数 × 每节点约 1.3 KB」。
// 实测 1.4 MB 的 HTML（1 MB markdown）稳态 436 MB、峰值 594 MB —— 这个数字由
// 节点数量决定，P0/P1/P2 那几轮的修正动不了它。要让它与视口成正比而不是与
// 文档成正比，只能把一个 document 拆成许多个小 document，只保视口附近几块
// 存活。原型实测（temp/probe_p3_blocks.cpp）：594 MB → 38 MB，且滚动到文档
// 末尾时 RSS 不再增长（probe 的 plateau 判据），单块 parse 约 10 ms。
//
// 切分口径：
//   - 切点只落在 <body> 的顶层子元素之间，`<ul>/<ol>/<table>/<pre>` 永远不会被劈开。
//   - 位置来自 gumbo 记录的源码偏移，不靠字符串猜标签嵌套。
//   - 每块都套同一份 <head>（样式表），所以块内样式与全量路径完全一致。
//
// 高度模型：
//   冷块没有真实高度，只能按「内容字节 × 每 KB 像素」估算。这个系数每次实例化
//   一块就用实测值刷新（加权），于是越滚越准。实测 12 KB 内容预算下，用均值
//   估算的累计漂移 ≤ 1.5 屏（占文档总高 1%），而滚动是相对位移，漂移不会自激。
class BlockLayout {
public:
    // 单块内容字节预算。实测取舍（temp/p3_real16k.log 等）：
    //   12 KB → 单块 parse 11.5 ms、漂移 1.3 屏   ← 选它
    //    8 KB → 单块 parse  8.8 ms、漂移 1.6 屏（块更多，块内方差更大）
    //   24 KB → 单块 parse 19.8 ms、漂移 2.8 屏（已超过一帧 16.7 ms）
    static constexpr size_t kContentBudget = 12 * 1024;

    // 视口上下各多保 1 块，滚轮一滚就能立刻出内容，不用等实例化。
    static constexpr int kPrefetchBlocks = 1;

    // 每 KB 内容的初始像素高度（16 px 正文 / 800 px 宽 / 行高 1.625 的实测均值）。
    // 只是起点：第一次实例化后就会被真实值替换。
    static constexpr double kDefaultPxPerKb = 260.0;

    struct Block {
        std::string html;             // 完整小块 HTML（含 <head>）
        size_t bytes = 0;             // 内容字节（不含 wrapper），高度估算的唯一依据
        int nchild = 0;               // 顶层子元素数（诊断/测试用）
        int estHeight = 0;            // 估算高度
        int realHeight = -1;          // 实测高度；-1 = 从未实例化
        int offset = 0;               // 距文档顶部偏移（由高度累加）
        int renderWidth = 0;          // realHeight 对应的排版宽度，换宽后失效
        bool failed = false;          // 该块解析/排版失败（高度退化为估算值）
        litehtml::document::ptr doc;  // 已实例化则非空
    };

    // 把整篇 HTML 切成块。返回 false 表示这块 HTML 不适合分块（没有 <body>、
    // 没有可用顶层元素、覆盖度过低），调用方应退回全量路径。
    bool split(const std::string& html);

    bool active() const { return m_active; }
    size_t count() const { return m_blocks.size(); }
    const Block& block(size_t i) const { return m_blocks[i]; }

    int total_height() const { return m_totalHeight; }
    int width() const { return m_width; }
    // 换宽：块的实测高度全部作废（要重排），但保留实测量作为估算依据的样本。
    void set_width(int w);

    // y 所在的块索引。越界时钳到首/末块（空文档返回 -1）。
    int block_at(int y) const;
    int offset_of(size_t i) const { return i < m_blocks.size() ? m_blocks[i].offset : 0; }
    int height_of(size_t i) const;
    int inner_y(size_t i, int y) const { return y - offset_of(i); }
    // 块内锚点的绝对 y（块偏移 + 元素在块文档内的 y）。
    // 返回 -1 表示该块尚未实例化、或块里没有这个 id —— 调用方据此退回块首。
    // 只在块已经实例化后调用（未实例化时没有 DOM 可查）。
    int anchor_y(size_t i, const std::string& id) const;

    // 让 [first, last] 内的块持有 document，窗口外的释放。容器只用来排版，
    // 取 document_container* 而不是 MyContainer*，这样门禁测试可以喂自己的
    // 桩容器（不需要把 GUI 那套一起链进来）。
    // 返回 true 表示这次调用改变了存活集合（调用方据此决定是否重绘）。
    bool ensure_window(int first, int last, litehtml::document_container* container);

    void release_all();
    void clear();

    // 诊断：当前存活的 document 数、已实测的块数、当前像素/千字节系数。
    int live_count() const;
    size_t measured_count() const;

    // Number of blocks whose layout FAILED (parse error or exception).
    // A failed block has no document, so the draw path skips it and the
    // panel shows the background with nothing on it. Counted over the whole
    // table, not just the visible window: a block that failed stays failed
    // (it is not retried), so the count only grows.
    size_t failed_count() const;
    double px_per_kb() const { return m_pxPerKb; }

private:
    std::vector<Block> m_blocks;
    std::string m_prefix;          // <!DOCTYPE ...> + <head>...</head> + <body>
    std::string m_suffix;          // </body></html>
    double m_pxPerKb = kDefaultPxPerKb;
    int m_width = 800;
    int m_totalHeight = 0;
    bool m_active = false;

    // 重算 estHeight / offset / m_totalHeight。任何高度变化后都要调用。
    void rebuild_geometry();
    // 用已实测的块刷新 m_pxPerKb（按字节加权），再重算未实测块的 estHeight。
    void refresh_height_model();
};

}  // namespace pecia
