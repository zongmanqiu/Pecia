#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <memory>
#include <string>
#include <vector>

#include "litehtml.h"

namespace pecia {

// 预览文档的文本选区。
//
// 为什么自研：litehtml 0.10 没有选区 API —— element/document 头文件里只有命中
// 测试（on_lbutton_down/up）和布局数据，没有"选了哪些字"的概念；参考实现的
// litebrowser 也只在它自己的单行编辑控件里有选区，HTML 视图同样没有。所以
// 选区只能按"文本 run"建模：
//
//   litehtml 解析时就把每个文本节点按空白切成 word / space 元素（见
//   document.cpp + document_container::split_text），每个元素都有自己的布局框
//   （element::get_placement()）。于是「文档顺序的 run 列表 + 每段的框」就是
//   一张现成的行级索引，不需要再改 litehtml。
//
// 选区端点 = (run 下标, run 内字节偏移)。高亮矩形 = run 的框按前缀宽度切出的
// 子区间；前缀宽度用容器的 text_width 量 —— 与 litehtml 排版用的是同一个函数，
// 所以高亮边界和真实字形对得上。
//
// 只依赖 litehtml + document_container，不碰 FLTK：命中/提取/矩形都能用纯单测
// 覆盖（test/test_preview_selection.cpp 用 8px/字节的桩容器断言）。
class TextSelection {
public:
    struct Pos {
        int run = -1;    // run 下标；-1 = 未定位
        int offset = 0;  // run.text 内的字节偏移（落在 UTF-8 字符边界上）
    };

    // 为一份**已经 render 完**的 document 建索引。
    // container 只用来量文字宽度，生命周期须长于本对象的使用。
    void build(litehtml::document* doc, litehtml::document_container* container);
    void clear();

    bool valid() const { return !m_runs.empty(); }
    bool has_range() const { return m_valid_range; }

    // 建索引时那份 document 是否还活着（分块预览里块会被释放，调用方据此
    // 丢掉已经失效的选区，而不是去碰悬垂的几何数据）。
    bool document_alive() const { return !m_doc.expired(); }
    const litehtml::document* document() const { return m_doc.lock().get(); }

    // 文档坐标 → 文本位置。总会返回最靠近的 run（拖到文字外时选区继续到
    // 最近一行，和浏览器一致）；文档里没有任何文字时返回 false。
    bool hit(litehtml::pixel_t x, litehtml::pixel_t y, Pos& out) const;

    // 仅当坐标落在一段文字（word / 空格）的框内（含 2px / 1px 容差）才返回
    // true。用来决定「按下左键」是开始选字，还是保持原来的拖拽滚动 —— 页边
    // 空白、段落之间的空隙都不该抢走拖拽。
    bool hit_text(litehtml::pixel_t x, litehtml::pixel_t y, Pos& out) const;

    void begin(const Pos& p);     // 设置锚点（并清空到锚点的空区间）
    void extend(const Pos& p);    // 拖到当前位置：锚点 → 当前 = 选区
    void select_all();            // 全文（首 run 开头 → 末 run 结尾）
    void collapse() { m_valid_range = false; }

    // 直接设置选区（预览内查找命中时用）。a/b 顺序任意（矩形/取文都按文档序）。
    void set_range(const Pos& a, const Pos& b);

    // 在**渲染后的文字**里查找（预览内查找）。预览只有渲染文本、没有源文件
    // 偏移，所以这条路不需要"预览 → 文档"的反向链路（AGENTS 红线 7 里仍
    // 属后续计划的那一项），命中天然落在用户看得见的位置。
    //   from：上次命中的终点（forward）或起点（backward）；run < 0 = 从头/尾
    //   wrap：到边界后是否回绕
    //   返回 false = 没有可搜的文字（未渲染/全空）；未命中时 outStart.run = -1
    //   *index/*total：命中的序号（1 基）与总数
    bool find(const std::string& needle, bool caseSensitive, bool wholeWord,
              bool forward, const Pos& from, bool wrap,
              Pos& outStart, Pos& outEnd, int* index, int* total,
              bool* wrapped) const;

    // 选区的矩形（文档坐标，一 run 一段）。零宽的段不输出。
    void rects(std::vector<litehtml::position>& out) const;

    // 选区内的文字：块之间补换行，行内软换行不补（与浏览器复制一致）。
    std::string text() const;

private:
    struct Run {
        std::string text;
        litehtml::position box;
        litehtml::uint_ptr font = 0;   // 量宽度用的字体句柄（容器持有）
        bool space = false;            // 空白元素（el_space）
        bool brk = false;              // white-space: pre* 里的换行
    };

    // run 内前 byte_off 个字节的宽度（相对 run 框左缘）。
    int prefix_width(const Run& run, int byte_off) const;
    // run 框内的 x → 字节偏移（落在字符边界上）。
    int offset_at_x(const Run& run, int x) const;

    std::vector<Run> m_runs;
    std::weak_ptr<litehtml::document> m_doc;
    litehtml::document_container* m_container = nullptr;
    Pos m_start;
    Pos m_end;
    bool m_valid_range = false;
};

}  // namespace pecia
