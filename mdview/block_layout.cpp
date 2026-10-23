#include "block_layout.h"

// gumbo.h 无条件 #define _CRT_SECURE_NO_WARNINGS，而 Pecia 目标在命令行也定义了
// 同一个宏 —— 两者相遇就是 C4005（宏重定义）。先撤掉命令行那份，让 gumbo 自己
// 定义即可；本文件不调用任何需要该宏的 CRT 函数。
#ifdef _CRT_SECURE_NO_WARNINGS
#undef _CRT_SECURE_NO_WARNINGS
#endif
#include <gumbo.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace pecia {
namespace {

// 单块最低高度：极小的块（比如只有一行空白的 <hr>）按字节估会得到 0 px，
// 0 高度会让 offset 重叠、block_at() 的二分失去意义。
constexpr int kMinBlockHeight = 16;

// 块首占位元素。
//
// 预览 CSS 用 `body > * + * { margin-top: 8px }` 统一顶层块间距，而分块后每块
// 的第一个元素在它自己的 document 里没有前兄弟 —— 于是每个块边界都会少 8px。
// 42 块就是 42 处「段落贴得太近」。塞一个零高、零边距的空 div 当首子元素，
// 真实的首元素就重新获得 8px margin-top，与全量路径逐像素一致。
// 它自己必须 height/margin/padding/line-height 全 0：只要占了 1px，边界就会多出来。
constexpr const char* kGapPlaceholder =
    "<div style=\"height:0;margin:0;padding:0;border:0;line-height:0;font-size:0\"></div>\n";

// 顶层元素在源码里的字节区间。
struct Span {
    size_t begin = 0;
    size_t end = 0;
    // 可选外壳：块 HTML 里把原文 [begin,end) 包在 open … close 之间。
    // 只有「被切开的 <pre>」用它 —— 每一片都要自带 <pre><code> 包裹，
    // 否则切出来的只是裸文本行，没有代码块背景与等宽字体。
    std::string open;
    std::string close;
    // 预算判定用的字节数：包装也算成本（每片都重复一份开始标签）。
    size_t cost() const { return (end - begin) + open.size() + close.size(); }
};

// void 元素（<br> <hr> <img>）没有结束标签，gumbo 的 end_pos 为空；此时从
// 起始位置向后找闭合本标签的 '>'（要跳过引号内的 '>'，例如 alt="a>b"）。
// 不处理这一条会静默丢掉整篇文档里的所有 <br>/<img> —— 原型阶段实测丢了 1081 个。
size_t element_end_offset(const std::string& html, const GumboNode* n)
{
    unsigned start = n->v.element.start_pos.offset;
    unsigned end = n->v.element.end_pos.offset;
    size_t tail = n->v.element.original_end_tag.length;
    if (start == 0 && end == 0) return 0;   // parser-inserted node: 源码里不存在
    if (end > start) return (size_t)end + tail;

    size_t i = start;
    char quote = 0;
    while (i < html.size())
    {
        char c = html[i];
        if (quote) { if (c == quote) quote = 0; }
        else if (c == '"' || c == '\'') quote = c;
        else if (c == '>') return i + 1;
        ++i;
    }
    return 0;
}

// 给开始标签追加内联 style。已带 style 属性就并到原值末尾（内联样式优先级
// 高于样式表里的 `pre { ... }`，这正是覆盖 padding/border 所需要的）。
std::string with_style(const std::string& tag, const std::string& extra)
{
    if (tag.empty() || extra.empty()) return tag;
    std::string out = tag;
    size_t gt = out.rfind('>');
    if (gt == std::string::npos) return out;
    for (const char* mark : {"style=\"", "style='"})
    {
        size_t at = out.find(mark);
        if (at == std::string::npos || at >= gt) continue;
        const size_t valueAt = at + 7;
        size_t q = out.find(out[valueAt - 1], valueAt);
        if (q == std::string::npos || q >= gt) break;
        out.insert(q, ";" + extra);
        return out;
    }
    out.insert(gt, " style=\"" + extra + "\"");
    return out;
}

// 把一个超预算的 <pre> 按行切成若干片，每片自带 <pre><code> 外壳。
//
// 为什么必须切：3000 行的代码块是一个顶层元素，只在顶层断刀的话它永远独占
// 一块 —— 实测 405 KB 的 md 打开后 RSS 仍 235 MB，分块等于没生效。
//
// 为什么按行切是安全的：<pre> 内行高固定、不参与流式布局反馈，切点落在 '\n'
// 上不会改变任何一行的排版结果；唯一的副作用是每片会重复一份 pre 的
// padding(16px)/border(1px)/圆角，所以按首/中/末三种形态用内联样式把它们压掉，
// 让 N 片拼起来与原来的一整块逐像素一致。
bool split_pre_element(const std::string& html, size_t preBegin, size_t preEnd,
                       size_t budget, std::vector<Span>& out)
{
    // 开始标签 [preBegin, preTagEnd)
    size_t preTagEnd = preBegin;
    {
        char quote = 0;
        while (preTagEnd < preEnd)
        {
            char c = html[preTagEnd];
            if (quote) { if (c == quote) quote = 0; }
            else if (c == '"' || c == '\'') quote = c;
            else if (c == '>') { ++preTagEnd; break; }
            ++preTagEnd;
        }
    }
    if (preTagEnd >= preEnd) return false;
    const std::string preOpen = html.substr(preBegin, preTagEnd - preBegin);

    // 内容必须在 <code> 里：切完补回 <code> 才能保持字体与背景规则不变。
    size_t codeAt = html.find("<code", preTagEnd);
    if (codeAt == std::string::npos || codeAt >= preEnd) return false;
    size_t codeTagEnd = html.find('>', codeAt);
    if (codeTagEnd == std::string::npos || codeTagEnd >= preEnd) return false;
    ++codeTagEnd;
    size_t codeClose = html.rfind("</code>", preEnd);
    if (codeClose == std::string::npos || codeClose <= codeTagEnd) return false;
    const std::string codeOpen = html.substr(codeAt, codeTagEnd - codeAt);

    // 按行切分（每行含行尾 '\n'，最后一行可能没有）
    std::vector<std::pair<size_t, size_t>> lines;
    for (size_t p = codeTagEnd; p < codeClose;)
    {
        size_t nl = html.find('\n', p);
        if (nl == std::string::npos || nl + 1 > codeClose)
        {
            lines.push_back({p, codeClose});
            break;
        }
        lines.push_back({p, nl + 1});
        p = nl + 1;
    }
    // 少于两行、或切开后并不比原来小，都不值得切。
    if (lines.size() < 8) return false;

    // 贪心按预算分行成段。单行就超预算时让它独占一段（无法再切）。
    std::vector<std::pair<size_t, size_t>> parts;   // 每段的行索引区间 [lo, hi)
    for (size_t i = 0; i < lines.size();)
    {
        size_t j = i, acc = 0;
        while (j < lines.size())
        {
            const size_t len = lines[j].second - lines[j].first;
            if (j > i && acc + len > budget) break;
            acc += len;
            ++j;
        }
        parts.push_back({i, j});
        i = j;
    }
    if (parts.size() < 2) return false;

    const size_t n = parts.size();
    for (size_t k = 0; k < n; ++k)
    {
        // 首片保上边、末片保下边，中间片两边都压成 0；圆角同理。
        std::string extra;
        if (k == 0)
            extra = "margin-bottom:0;padding-bottom:0;border-bottom-width:0;"
                    "border-bottom-left-radius:0;border-bottom-right-radius:0";
        else if (k + 1 == n)
            extra = "margin-top:0;padding-top:0;border-top-width:0;"
                    "border-top-left-radius:0;border-top-right-radius:0";
        else
            extra = "margin-top:0;margin-bottom:0;padding-top:0;padding-bottom:0;"
                    "border-top-width:0;border-bottom-width:0;border-radius:0";

        Span s;
        s.begin = lines[parts[k].first].first;
        s.end = lines[parts[k].second - 1].second;
        s.open = with_style(preOpen, extra) + codeOpen;
        s.close = "</code></pre>";
        out.push_back(std::move(s));
    }
    return true;
}

// litehtml 自带的精简版 gumbo 没有 gumbo_get_elements_by_tag()，自己走一遍树。
const GumboNode* find_tag(const GumboNode* n, GumboTag tag)
{
    if (!n) return nullptr;
    if (n->type != GUMBO_NODE_ELEMENT && n->type != GUMBO_NODE_DOCUMENT) return nullptr;
    const GumboVector* kids = (n->type == GUMBO_NODE_DOCUMENT) ? &n->v.document.children
                                                               : &n->v.element.children;
    if (n->type == GUMBO_NODE_ELEMENT && n->v.element.tag == tag) return n;
    for (unsigned i = 0; i < kids->length; ++i)
        if (const GumboNode* hit = find_tag(static_cast<const GumboNode*>(kids->data[i]), tag))
            return hit;
    return nullptr;
}

const GumboNode* find_body(const GumboOutput* out)
{
    if (!out || !out->root) return nullptr;
    const GumboNode* root = out->root;
    if (root->type == GUMBO_NODE_ELEMENT) return find_tag(root, GUMBO_TAG_BODY);
    if (root->type == GUMBO_NODE_DOCUMENT)
    {
        for (unsigned i = 0; i < root->v.document.children.length; ++i)
            if (const GumboNode* hit = find_tag(
                    static_cast<const GumboNode*>(root->v.document.children.data[i]), GUMBO_TAG_BODY))
                return hit;
    }
    return nullptr;
}

}  // namespace

bool BlockLayout::split(const std::string& html)
{
    // 保留上一次的密度系数（px/KB）。
    //
    // 为什么：split() 第一行是 clear()，而 clear() 会把系数退回默认值
    // （260 px/KB）。默认值与真实密度可以差 2~3 倍（实测 5 MB 文档真实约
    // 674 px/KB），于是所有块的高度都估偏了，offset 累加出上千像素的
    // "名义空洞"——视口正好落进空洞里，那里没有任何内容可画，面板就是一片
    // 白（实测：5 MB 文档改一个字后预览空白 14 秒，打点显示目标块起点比
    // 视口低 3713 px，而视口只有 504 px 高）。
    //
    // 同一个文档改一个字，密度不会变；上次实测出来的系数正是最好的初值。
    // 换了别的文档也只是初值不够准，等实测样本一多就会被重新校准。
    const double prevPxPerKb = m_pxPerKb;
    clear();
    if (html.size() < kContentBudget * 2) return false;

    // <head>（含样式表）原样复制给每一块：块内样式必须与全量路径逐字节一致，
    // 否则分块后字号/行高/边距会莫名漂移。
    size_t bodyAt = html.find("<body");
    if (bodyAt == std::string::npos) return false;
    size_t bodyEnd = html.find('>', bodyAt);
    if (bodyEnd == std::string::npos) return false;
    m_prefix = html.substr(0, bodyEnd + 1);
    m_suffix = "\n</body></html>\n";

    GumboOutput* out = gumbo_parse_with_options(&kGumboDefaultOptions, html.data(),
                                               (int)html.size());
    if (!out) return false;
    const GumboNode* body = find_body(out);
    if (!body)
    {
        gumbo_destroy_output(&kGumboDefaultOptions, out);
        return false;
    }

    std::vector<Span> spans;
    const GumboVector& kids = body->v.element.children;
    for (unsigned i = 0; i < kids.length; ++i)
    {
        const GumboNode* n = static_cast<const GumboNode*>(kids.data[i]);
        if (!n || n->type != GUMBO_NODE_ELEMENT) continue;   // 空白/裸文本/注释跳过
        unsigned start = n->v.element.start_pos.offset;
        size_t end = element_end_offset(html, n);
        if (start == 0 || end <= start || end > html.size()) continue;
        // 超预算的 <pre>（几千行的代码块）在顶层断刀切不开，必须按行内部切。
        // 否则整篇「一个代码块」的文档永远只有 2 个 span，连分块门槛都过不去。
        if (n->v.element.tag == GUMBO_TAG_PRE &&
            end - start > kContentBudget &&
            split_pre_element(html, (size_t)start, end, kContentBudget, spans))
            continue;
        Span s;
        s.begin = (size_t)start;
        s.end = end;
        spans.push_back(std::move(s));
    }
    gumbo_destroy_output(&kGumboDefaultOptions, out);

    if (spans.size() < 4) return false;

    // 覆盖度自检：顶层元素必须盖住绝大部分源码区间。覆盖度过低说明 gumbo 的
    // 位置信息不可信（或被内联 HTML 搞乱了结构），此时分块会丢内容 —— 宁可
    // 退回全量路径，也不能静默少渲染一段。
    {
        size_t covered = 0;
        for (const Span& s : spans) covered += s.end - s.begin;
        size_t lo = spans.front().begin;
        size_t hi = spans.back().end;
        if (hi <= lo) return false;
        if (100.0 * (double)covered / (double)(hi - lo) < 80.0) return false;
    }

    // 贪心打包：只在顶层元素之间断块。
    std::string cur;
    int curN = 0;
    size_t curBytes = 0;
    for (const Span& s : spans)
    {
        const size_t fragBytes = s.cost();
        // 单个超大元素（巨型表格、未切开的 <pre>）不能切开，只能让它独占一块；
        // 因此判定条件是「已有内容且再加就超预算」，而不是「frag 本身超预算」。
        if (curN > 0 && curBytes + fragBytes > kContentBudget)
        {
            m_blocks.push_back(Block{});
            Block& b = m_blocks.back();
            b.html = m_prefix + "\n" + cur + m_suffix;
            b.bytes = curBytes;
            b.nchild = curN;
            cur.clear();
            curN = 0;
            curBytes = 0;
        }
        // 非首块的第一个元素在它自己的 document 里没有前兄弟，会丢掉
        // `body > * + *` 的 8 px 上边距 —— 用零高占位把「前兄弟」补回来。
        if (curN == 0 && !m_blocks.empty()) cur += kGapPlaceholder;
        if (!s.open.empty()) cur += s.open;
        cur.append(html, s.begin, s.end - s.begin);
        if (!s.close.empty()) cur += s.close;
        cur.push_back('\n');
        ++curN;
        curBytes += fragBytes + 1;
    }
    if (curN > 0)
    {
        m_blocks.push_back(Block{});
        Block& b = m_blocks.back();
        b.html = m_prefix + "\n" + cur + m_suffix;
        b.bytes = curBytes;
        b.nchild = curN;
    }
    if (m_blocks.size() < 2) { clear(); return false; }

    m_active = true;
    m_width = 800;
    m_pxPerKb = prevPxPerKb;      // 不再退回默认值（见函数开头的说明）
    refresh_height_model();
    rebuild_geometry();
    return true;
}

void BlockLayout::clear()
{
    m_blocks.clear();
    m_prefix.clear();
    m_suffix.clear();
    m_active = false;
    m_totalHeight = 0;
    m_pxPerKb = kDefaultPxPerKb;
}

int BlockLayout::height_of(size_t i) const
{
    if (i >= m_blocks.size()) return 0;
    const Block& b = m_blocks[i];
    // 实测高度只对「排版时用的那个宽度」有效；换宽后必须退回估算值，否则
    // 滚动条会按旧宽度的总高画，拖到底却看不到文档末尾。
    if (b.realHeight >= 0 && b.renderWidth == m_width) return b.realHeight;
    return b.estHeight > 0 ? b.estHeight : kMinBlockHeight;
}

namespace {

// 在块文档里找 id 对应元素的 y（块文档坐标系）。litehtml 0.10 没有
// get_element_by_anchor()，只能递归比对 get_attr("id")；块内元素少，够快。
int find_anchor_y(const litehtml::element::ptr& el, const std::string& id)
{
    if (!el) return -1;
    const char* aid = el->get_attr("id");
    if (aid && id == aid) return (int)el->get_placement().y;
    for (const auto& ch : el->children())
    {
        int y = find_anchor_y(ch, id);
        if (y >= 0) return y;
    }
    return -1;
}

}  // namespace

int BlockLayout::anchor_y(size_t i, const std::string& id) const
{
    if (i >= m_blocks.size() || id.empty()) return -1;
    const Block& b = m_blocks[i];
    if (!b.doc) return -1;   // 未实例化：没有 DOM 可查，调用方退回块首
    int inner = find_anchor_y(b.doc->root(), id);
    if (inner < 0) return -1;
    return offset_of(i) + inner;
}

void BlockLayout::rebuild_geometry()
{
    int off = 0;
    for (size_t i = 0; i < m_blocks.size(); ++i)
    {
        m_blocks[i].offset = off;
        off += height_of(i);
    }
    m_totalHeight = off;
}

void BlockLayout::refresh_height_model()
{
    double sumPx = 0.0, sumKb = 0.0;
    for (const Block& b : m_blocks)
    {
        if (b.realHeight > 0 && b.renderWidth == m_width && b.bytes > 0)
        {
            sumPx += (double)b.realHeight;
            sumKb += (double)b.bytes / 1024.0;
        }
    }
    // 只有实测样本才改系数。加权（按字节）而不是取平均 px/KB：大块更接近
    // 「正文密度」，它决定绝大多数页面的高度。
    //
    // 门槛是一块半（16 KB）而不是 100 KB：编辑一个大文档会重新 split，
    // 此时全部实测样本都作废，只有视口附近的一两块是新的实测值。如果
    // 门槛定得高，系数永远校准不了，估算与真实高度可以差 2~3 倍，
    // 于是所有块之间出现上千像素的"名义空洞"，视口落进去就是一片空白
    // （见 split() 里的说明）。一块半的样本已经足以把系数拉回正轨，
    // 后面实测样本变多时还会继续精调。
    if (sumKb > 16.0 && sumPx > 0.0) m_pxPerKb = sumPx / sumKb;

    for (Block& b : m_blocks)
    {
        int h = (int)((double)b.bytes / 1024.0 * m_pxPerKb + 0.5);
        b.estHeight = h < kMinBlockHeight ? kMinBlockHeight : h;
    }
}

void BlockLayout::set_width(int w)
{
    if (w <= 0 || w == m_width) return;
    m_width = w;
    // 换宽后所有实测高度都作废（行数变了）。释放 document 省内存；实测样本
    // 保留在 realHeight 里但被 renderWidth 判定为失效，等视口块重排后再采信。
    for (Block& b : m_blocks) b.doc.reset();
    rebuild_geometry();
}

int BlockLayout::block_at(int y) const
{
    if (m_blocks.empty()) return -1;
    if (y <= 0) return 0;
    if (y >= m_totalHeight) return (int)m_blocks.size() - 1;
    // offset 单调递增，二分。
    size_t lo = 0, hi = m_blocks.size() - 1;
    while (lo < hi)
    {
        size_t mid = lo + (hi - lo + 1) / 2;
        if (m_blocks[mid].offset <= y) lo = mid;
        else hi = mid - 1;
    }
    return (int)lo;
}

bool BlockLayout::ensure_window(int first, int last, litehtml::document_container* container)
{
    if (m_blocks.empty()) return false;
    if (first < 0) first = 0;
    if (last >= (int)m_blocks.size()) last = (int)m_blocks.size() - 1;
    if (last < first) return false;

    bool changed = false;

    // 先释放窗口外：先放再建，峰值只按窗口算。
    for (size_t i = 0; i < m_blocks.size(); ++i)
    {
        Block& b = m_blocks[i];
        if (b.doc && ((int)i < first || (int)i > last))
        {
            b.doc.reset();
            changed = true;
        }
    }

    for (int i = first; i <= last; ++i)
    {
        Block& b = m_blocks[(size_t)i];
        if (b.doc || b.failed) continue;
        if (!container) continue;
        try
        {
            auto doc = litehtml::document::createFromString(b.html.c_str(), container);
            if (!doc)
            {
                // 解析失败：标死，避免每次 draw 都重试同一块（那会变成每帧
                // 一次的阻塞）。高度退化为估算值，视觉上留一段空白。
                b.failed = true;
                continue;
            }
            doc->render(litehtml::pixel_t((float)m_width));
            b.doc = doc;
            b.realHeight = (int)doc->height();
            b.renderWidth = m_width;
            changed = true;
        }
        catch (...)
        {
            // 单块 OOM 不应带走整个预览：与全量路径不同，这里还有其余的块可看。
            b.failed = true;
            changed = true;
        }
    }

    if (changed)
    {
        // 实测高度变了 → 系数与偏移全部重算（O(n)，n = 块数，通常几十到几百）。
        refresh_height_model();
        rebuild_geometry();
    }
    return changed;
}

void BlockLayout::release_all()
{
    for (Block& b : m_blocks) b.doc.reset();
}

int BlockLayout::live_count() const
{
    int n = 0;
    for (const Block& b : m_blocks) if (b.doc) ++n;
    return n;
}

size_t BlockLayout::measured_count() const
{
    size_t n = 0;
    for (const Block& b : m_blocks)
        if (b.realHeight > 0 && b.renderWidth == m_width) ++n;
    return n;
}

size_t BlockLayout::failed_count() const
{
    size_t n = 0;
    for (const Block& b : m_blocks)
        if (b.failed) ++n;
    return n;
}

}  // namespace pecia
