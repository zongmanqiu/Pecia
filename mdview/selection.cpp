#include "selection.h"

#include "core/SearchCore.h"   // rawSearchForward/Backward + countRawRange（与编辑区同一套语义）

#include <algorithm>
#include <climits>
#include <cmath>
#include <functional>
#include <utility>

namespace pecia {

// ---------- 文本索引 ----------

void TextSelection::build(litehtml::document* doc, litehtml::document_container* container)
{
    clear();
    if (!doc || !container) return;
    m_container = container;
    m_doc = doc->weak_from_this();

    auto root = doc->root();
    if (!root) return;

    // 只遍历 <body>：<head> 里的 <style>/<title> 文本用户看不见，也不该被
    // 选中或复制。
    litehtml::element::ptr body;
    if (root->is_body()) {
        body = root;
    } else {
        for (const auto& c : root->children()) {
            if (c && c->is_body()) { body = c; break; }
        }
    }
    if (!body) body = root;

    // 递归深度保护：正常 markdown 生成的 HTML 深度 < 30，这里只是防止异常
    // 输入把栈打穿（嵌套 200 层以上的列表/引用）。
    // 先声明、后赋值：lambda 要递归就得先能看到自己（默认捕获模式不会带上
    // 正在定义的变量）。
    // 第三个参数是 block 编号：每进入一个块级元素就换新号，text() 按编号变化
    // 补换行。不能改用「行间距大于行高」判块边界——preprocess 已把 p+p /
    // li+li 的外边距清零（见 preprocess.cpp 的 p + p, li + li 规则），段间距
    // 与行距完全相等，那个判据永远不成立。
    int nextBlock = 0;
    std::function<void(const litehtml::element::ptr&, int, int)> walk;
    walk = [this, &walk, &nextBlock](const litehtml::element::ptr& el, int depth,
                                     int block) {
            if (!el || depth > 200) return;
            if (el->css().get_display() == litehtml::display_none) return;

            // <br> 是 html_tag 而不是文本元素，但复制出来的文字必须有换行。
            if (el->is_break() && !el->is_text()) {
                Run r;
                r.text  = "\n";
                r.brk   = true;
                r.block = block;
                m_runs.push_back(std::move(r));
                return;
            }

            if (el->is_text()) {
                Run r;
                el->get_text(r.text);
                if (r.text.empty()) return;
                r.font  = el->css().get_font();
                r.space = el->is_space();
                r.brk   = el->is_break();
                r.block = block;
                // white-space 是继承属性：<pre><code> 里的空白必须原样保留，
                // 否则复制代码时行首缩进和连续空格全被压成一个空格。
                const litehtml::white_space ws = el->css().get_white_space();
                r.pre = (ws == litehtml::white_space_pre ||
                         ws == litehtml::white_space_pre_line ||
                         ws == litehtml::white_space_pre_wrap);
                if (r.brk) {
                    // white-space: pre* 里的换行：没有可绘制的框，只参与复制。
                    r.text = "\n";
                    m_runs.push_back(std::move(r));
                    return;
                }
                r.box = el->get_placement();
                // 框为 0 说明这个文本没有真正排版（display:none / 尚未布局），
                // 既选不中也没必要留着占索引。
                if (r.box.width < 0.5f || r.box.height < 0.5f) return;
                if (r.space && !r.pre) {
                    // 折叠后的空白：litehtml 画的是 m_transformed_text（单个
                    // 空格），框宽就是这一个空格的宽度。用 " " 作为文本，复制
                    // 与量宽都和实际绘制一致。pre* 里的空白保持原文。
                    r.text = " ";
                }
                m_runs.push_back(std::move(r));
                return;
            }

            int childBlock = block;
            switch (el->css().get_display()) {
            case litehtml::display_block:
            case litehtml::display_list_item:
            case litehtml::display_table:
            case litehtml::display_table_cell:
            case litehtml::display_table_row:
            case litehtml::display_table_row_group:
            case litehtml::display_flex:
                childBlock = ++nextBlock;
                break;
            default:
                break;
            }
            for (const auto& c : el->children()) walk(c, depth + 1, childBlock);
        };

    walk(body, 0, 0);
}

void TextSelection::clear()
{
    m_runs.clear();
    m_doc.reset();
    m_container = nullptr;
    m_start = Pos{};
    m_end = Pos{};
    m_valid_range = false;
}

// ---------- 量宽 / 命中 ----------

int TextSelection::prefix_width(const Run& run, int byte_off) const
{
    if (byte_off <= 0 || !m_container) return 0;
    if (byte_off >= (int)run.text.size()) return (int)std::lround(run.box.width);
    return (int)m_container->text_width(run.text.substr(0, (size_t)byte_off).c_str(), run.font);
}

int TextSelection::offset_at_x(const Run& run, int x) const
{
    const int local = x - (int)run.box.left();
    if (local <= 0) return 0;
    if (local >= (int)run.box.width) return (int)run.text.size();

    // run 都很短（word 最多 8 个字符、space 只有 1 个、CJK 逐字），逐字符
    // 量前缀即可，不需要二分。
    int best = 0;
    size_t i = 0;
    while (i < run.text.size()) {
        size_t next = i + 1;
        while (next < run.text.size() &&
               ((unsigned char)run.text[next] & 0xC0) == 0x80) {
            ++next;   // 跳过 UTF-8 续字节
        }
        if ((int)m_container->text_width(run.text.substr(0, next).c_str(), run.font) > local)
            break;
        i = next;
        best = (int)i;
    }
    return best;
}

bool TextSelection::hit(litehtml::pixel_t x, litehtml::pixel_t y, Pos& out) const
{
    // 1) 先按垂直距离找到"所在行"：离点击 y 最近的 run 给出了最小垂直距离。
    int bestV = INT_MAX;
    for (const Run& r : m_runs) {
        if (r.brk) continue;
        const int v = (y < r.box.top())    ? (int)(r.box.top() - y)
                    : (y > r.box.bottom()) ? (int)(y - r.box.bottom())
                                           : 0;
        bestV = std::min(bestV, v);
    }
    if (bestV == INT_MAX) return false;

    // 2) 该行内按水平距离选最近的 run（同距离保留文档序靠前的）。容差 +1
    //    是为了让紧挨着的两行都能算作"这一行"，边界上不会突然跳一整行。
    int bestH = INT_MAX, bestIdx = -1, bestOff = 0;
    for (int i = 0; i < (int)m_runs.size(); ++i) {
        const Run& r = m_runs[(size_t)i];
        if (r.brk) continue;
        const int v = (y < r.box.top())    ? (int)(r.box.top() - y)
                    : (y > r.box.bottom()) ? (int)(y - r.box.bottom())
                                           : 0;
        if (v > bestV + 1) continue;
        const int h = (x < r.box.left())     ? (int)(r.box.left() - x)
                    : (x >= r.box.right())   ? (int)(x - r.box.right())
                                             : 0;
        if (h < bestH) {
            bestH = h;
            bestIdx = i;
            bestOff = r.space ? (x >= (int)(r.box.left() + r.box.width / 2.0f)
                                     ? (int)r.text.size() : 0)
                              : offset_at_x(r, (int)x);
            if (bestH == 0) break;
        }
    }
    if (bestIdx < 0) return false;
    out.run = bestIdx;
    out.offset = bestOff;
    return true;
}

bool TextSelection::hit_text(litehtml::pixel_t x, litehtml::pixel_t y, Pos& out) const
{
    const int kTolX = 2;   // 词与词之间是空格 run，正常不会漏；2px 容差只吸收边框舍入
    const int kTolY = 1;
    int bestH = INT_MAX, bestIdx = -1, bestOff = 0;
    for (int i = 0; i < (int)m_runs.size(); ++i) {
        const Run& r = m_runs[(size_t)i];
        if (r.brk) continue;
        if (y < r.box.top() - kTolY || y > r.box.bottom() + kTolY) continue;
        if (x < r.box.left() - kTolX || x > r.box.right() + kTolX) continue;
        const int h = (x < r.box.left())     ? (int)(r.box.left() - x)
                    : (x >= r.box.right())   ? (int)(x - r.box.right())
                                             : 0;
        if (h < bestH) {
            bestH = h;
            bestIdx = i;
            bestOff = r.space ? (x >= (int)(r.box.left() + r.box.width / 2.0f)
                                     ? (int)r.text.size() : 0)
                              : offset_at_x(r, (int)x);
            if (bestH == 0) break;
        }
    }
    if (bestIdx < 0) return false;
    out.run = bestIdx;
    out.offset = bestOff;
    return true;
}

// ---------- 选区状态 ----------

void TextSelection::begin(const Pos& p)
{
    m_start = p;
    m_end = p;
    m_valid_range = false;
}

void TextSelection::extend(const Pos& p)
{
    if (m_start.run < 0) return;
    m_end = p;
    m_valid_range = !(m_start.run == m_end.run && m_start.offset == m_end.offset);
}

void TextSelection::select_all()
{
    if (m_runs.empty()) return;
    m_start.run = 0;
    m_start.offset = 0;
    m_end.run = (int)m_runs.size() - 1;
    m_end.offset = (int)m_runs.back().text.size();
    m_valid_range = !(m_start.run == m_end.run && m_start.offset == m_end.offset);
}

void TextSelection::set_range(const Pos& a, const Pos& b)
{
    if (a.run < 0 || b.run < 0) return;
    m_start = a;
    m_end = b;
    m_valid_range = !(a.run == b.run && a.offset == b.offset);
}

// ---------- 预览内查找 ----------

// run 列表按文档序拼成一条连续字符串，就是用户看到的文字（run 之间本来
// 就有空白 run，换行由 brk run 的 "\n" 补齐），所以直接在拼接串上查找即可：
// 不需要把源文件偏移映射到渲染结果（那条"反向链路"仍未建）。
// 拼接串与各 run.text 逐字节一致，所以字节偏移可用 run 起始表二分换算回
// (run, offset)，不必再建逐字节索引表（200 KB 文档就是 1.6 MB 额外内存）。
bool TextSelection::find(const std::string& needle, bool caseSensitive,
                         bool wholeWord, bool forward, const Pos& from,
                         bool wrap, Pos& outStart, Pos& outEnd,
                         int* index, int* total, bool* wrapped) const
{
    outStart.run = -1;
    outStart.offset = 0;
    outEnd.run = -1;
    outEnd.offset = 0;
    if (index) *index = 0;
    if (total) *total = 0;
    if (wrapped) *wrapped = false;
    if (needle.empty() || m_runs.empty()) return false;

    std::string flat;
    std::vector<int> starts(m_runs.size() + 1);
    for (size_t i = 0; i < m_runs.size(); ++i) {
        starts[i] = (int)flat.size();
        flat += m_runs[i].text;
    }
    starts[m_runs.size()] = (int)flat.size();
    const int len = (int)flat.size();
    if (len <= 0) return false;

    // 字节偏移 → (run, offset)。off 落在最后一个 run 之后时钳到文末。
    // atEnd = 这是命中**终点**：off 正好落在某个 run 的起始处时归给前一个
    // run 的末尾，这样终点总落在实际命中的最后一个 run 里。否则终点会变成
    // "下一个 run 的 0 偏移"（例如 "Hello world" 的终点落到 <br> 的起点），
    // 取文/高亮仍碰巧正确，但一旦按偏移做比较就会错一位。
    auto posAt = [this, &starts](int off, bool atEnd) {
        Pos p;
        const int n = (int)m_runs.size();
        if (off <= 0) {
            p.run = 0;
            p.offset = 0;
            return p;
        }
        if (off >= starts[(size_t)n]) {
            p.run = n - 1;
            p.offset = (int)m_runs[(size_t)(n - 1)].text.size();
            return p;
        }
        // 起点：starts[run] <= off 的最后一个 run（落在边界上就取该 run 的 0）。
        // 终点：starts[run] < off 的最后一个 run（落在边界上取前一个的末尾）。
        int lo = 0, hi = n - 1;
        while (lo < hi) {
            const int mid = (lo + hi + 1) / 2;
            const bool fits = atEnd ? (starts[(size_t)mid] < off)
                                    : (starts[(size_t)mid] <= off);
            if (fits) lo = mid;
            else hi = mid - 1;
        }
        p.run = lo;
        p.offset = off - starts[(size_t)lo];
        return p;
    };

    int fromFlat = forward ? 0 : len;
    if (from.run >= 0 && from.run < (int)m_runs.size()) {
        const Run& r = m_runs[(size_t)from.run];
        fromFlat = starts[(size_t)from.run] +
                   std::min(from.offset, (int)r.text.size());
    }

    const char* nd = needle.c_str();
    const int ndLen = (int)needle.size();
    int found = forward
        ? rawSearchForward(flat.c_str(), len, fromFlat, nd, ndLen,
                           caseSensitive, wholeWord)
        : rawSearchBackward(flat.c_str(), len, fromFlat, nd, ndLen,
                            caseSensitive, wholeWord);
    if (found < 0 && wrap) {
        found = forward
            ? rawSearchForward(flat.c_str(), len, 0, nd, ndLen,
                               caseSensitive, wholeWord)
            : rawSearchBackward(flat.c_str(), len, len, nd, ndLen,
                                caseSensitive, wholeWord);
        if (found >= 0 && wrapped) *wrapped = true;
    }
    if (found < 0) return true;   // 可搜但未命中

    outStart = posAt(found, false);
    outEnd = posAt(found + ndLen, true);

    if (index || total) {
        long long cnt = 0;
        int idx = 0;
        countRawRange(flat.c_str(), len, 0, len, nd, ndLen, caseSensitive,
                      wholeWord, found, cnt, idx);
        if (total) *total = (int)cnt;
        if (index) *index = idx;
    }
    return true;
}

// ---------- 输出 ----------

void TextSelection::rects(std::vector<litehtml::position>& out) const
{
    if (!m_valid_range || m_start.run < 0 || m_end.run < 0) return;

    int a = m_start.run, ao = m_start.offset;
    int b = m_end.run, bo = m_end.offset;
    if (a > b || (a == b && ao > bo)) {
        std::swap(a, b);
        std::swap(ao, bo);
    }

    for (int i = a; i <= b && i < (int)m_runs.size(); ++i) {
        const Run& r = m_runs[(size_t)i];
        if (r.brk) continue;
        const int from = (i == a) ? ao : 0;
        const int to   = (i == b) ? bo : (int)r.text.size();
        if (to <= from) continue;

        // 空白的字形就是它整个框，按字符偏移切会让高亮比实际空格窄。
        const int x0 = (int)r.box.left() + (r.space ? 0 : prefix_width(r, from));
        const int x1 = (int)r.box.left() + (r.space ? (int)r.box.width : prefix_width(r, to));
        if (x1 <= x0) continue;

        litehtml::position p;
        p.x      = (litehtml::pixel_t)x0;
        p.y      = (litehtml::pixel_t)(int)r.box.top();
        p.width  = (litehtml::pixel_t)(x1 - x0);
        p.height = (litehtml::pixel_t)(int)r.box.height;
        out.push_back(p);
    }
}

std::string TextSelection::text() const
{
    if (!m_valid_range || m_start.run < 0 || m_end.run < 0) return {};

    int a = m_start.run, ao = m_start.offset;
    int b = m_end.run, bo = m_end.offset;
    if (a > b || (a == b && ao > bo)) {
        std::swap(a, b);
        std::swap(ao, bo);
    }

    std::string out;
    int prevBlock = -1;
    for (int i = a; i <= b && i < (int)m_runs.size(); ++i) {
        const Run& r = m_runs[(size_t)i];
        const int from = (i == a) ? ao : 0;
        const int to   = (i == b) ? bo : (int)r.text.size();
        if (to <= from) continue;

        if (r.brk) {
            if (!out.empty() && out.back() != '\n') out += '\n';
            prevBlock = r.block;
            continue;
        }

        // 换块 → 补换行；行内软换行不补，和浏览器复制段落一致（复制出来的
        // 是连贯的一句话，不是按屏幕换行断开）。块边界由 build() 记录的编号
        // 判断，不依赖像素间距（见 build() 里的说明）。
        if (r.block >= 0 && prevBlock >= 0 && r.block != prevBlock) {
            while (!out.empty() && out.back() == ' ') out.pop_back();
            if (!out.empty() && out.back() != '\n') out += '\n';
        }

        if (r.space) {
            if (r.pre) {
                // code block / pre：空白就是内容，逐字节保留（缩进、连续空格）。
                out.append(r.text, (size_t)from, (size_t)(to - from));
            } else if (!out.empty() && out.back() != ' ' && out.back() != '\n') {
                out += ' ';
            }
        } else {
            out.append(r.text, (size_t)from, (size_t)(to - from));
        }
        prevBlock = r.block;
    }

    // 收尾：只裁掉末尾的空格与换行。pre 块内的空白已在上面原样保留，不能
    // 再走「行首空格全删」那套折叠规则。
    while (!out.empty() && (out.back() == ' ' || out.back() == '\n'))
        out.pop_back();
    return out;
}

}  // namespace pecia
