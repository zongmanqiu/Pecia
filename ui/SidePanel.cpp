#include "SidePanel.h"

#include <FL/Fl_Scrollbar.H>
#include <FL/Fl_Tooltip.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <utility>   // std::pair（大纲折叠的祖先栈）

#include "core/I18n.h"
#include "core/PathUtils.h"
#include "ui/Layout.h"         // MENU_H：工具条高度与菜单栏一致
#include "ui/OutlineRows.h"
#include "ui/ThemeWidgets.h"   // styleToolScrollbar/drawToolScrollbar：滚动条与主编辑器同款

namespace {

constexpr int kIndent   = 14;   // 每级缩进
constexpr int kChevronW = 14;   // 行首箭头列宽（所有行恒定占位，见 rowTextLeft）
constexpr int kPadX     = 8;    // 行首内边距
constexpr int kMaxDepth = 6;    // 目录递归深度上限
constexpr int kMaxNodes = 800;  // 扫描条目总上限（防止 giant 目录卡死 UI）
constexpr size_t kMaxElideCache = 4096;  // 截断结果缓存条数上限（见 elidedLabel）
constexpr int kTabGap   = 4;

// 不进入的目录：隐藏目录（.git/.cache…）与常见构建产物/依赖目录。
// velotype 没有这层过滤，遇到 node_modules 会递归上万项 —— 这里必须加。
bool skipDirectory(const std::string &name) {
    if (name.empty()) return true;
    if (name[0] == '.') return true;
    static const char *kBanned[] = {
        "node_modules", "target", "build", "dist", "bin", "obj",
        "__pycache__", "venv", "env", "vendor",
    };
    for (const char *b : kBanned)
        if (name.size() == std::strlen(b) && _stricmp(name.c_str(), b) == 0)
            return true;
    return false;
}

bool lessName(const std::string &a, const std::string &b) {
    std::string la = a, lb = b;
    for (char &c : la) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    for (char &c : lb) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return la < lb;
}

// 画三角箭头（展开 ▾ / 折叠 ▸）。用折线而不是字符：等宽/比例字体对
// U+25B8 的字形覆盖不一致，画出来会大小不一。
void drawChevron(int cx, int cy, bool expanded, Fl_Color c) {
    fl_color(c);
    if (expanded) {
        fl_polygon(cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3);
    } else {
        fl_polygon(cx - 2, cy - 4, cx - 2, cy + 4, cx + 3, cy);
    }
}

// 工具条图标：两个三角（朝下 = 展开全部，朝上 = 折叠全部）。
// 和箭头一样用手画，不用字符：字体覆盖不全时 U+25BC / U+21BB 会变成方块，
// 本项目已经在 U+2026 上踩过一次（见 drawEllipsisDots 的注释）。
void drawToolChevrons(int cx, int cy, bool down, Fl_Color c) {
    fl_color(c);
    // 描边人字而不是实心三角：20px 按钮里两个实心三角会挤成一坨，两道折线
    // 才看得出是"两下"。
    //
    // ★ 线宽 1px，与标题栏四个按钮的描边对齐（TitleBar::drawButton 全是
    //   1px 描边）。早先这里是 2px，同一窗口里侧栏图标明显比标题栏重
    //   （用户 2026-10-31 反馈"线条宽度更宽"）。
    fl_line_style(FL_SOLID, 1);
    for (int k = 0; k < 2; ++k) {
        const int yy = cy + (k == 0 ? -4 : 2);
        fl_begin_line();
        if (down) {
            fl_vertex(cx - 4, yy);
            fl_vertex(cx, yy + 3);
            fl_vertex(cx + 4, yy);
        } else {
            fl_vertex(cx - 4, yy + 3);
            fl_vertex(cx, yy);
            fl_vertex(cx + 4, yy + 3);
        }
        fl_end_line();
    }
    fl_line_style(0);
}

// ---- 工具条图标：刷新 -----------------------------------------------------
//
// 一个圆弧 + 末端的实心箭头。手画，不用字体字形（缺字会画成方块，U+2026
// 已经踩过）。之前试过双弧、方角矩形等画法，20px 下都不如这一版干净：
// 元素越少，锯齿越少。
//
// ★ fl_arc(x,y,w,h,a1,a2) 的 a2 是**终止角**，不是跨度，且要求 a2 >= a1。
//   改弧长只动 a2，别把跨度当 a2 传。
//
// ★ 缺口的两个端点定在**正右（0°/3 点钟）和正下（270°/6 点钟）**，弧从
//   0° 逆时针绕 270° 到 270°，缺口 90° 落在右下象限（用户 2026-10-31 的
//   定案画法："从正下方开始，顺时针，到正右方，留出 90 度的缺口"）。
//
//   这样摆的好处是**箭头必然是正的**：端点落在 0° 的坐标轴上，该处切线
//   是纯垂直的 (0,1)，三角形的对称轴也就垂直、底边水平，四个顶点全落在
//   整数或半整数像素上（底边 ±2.5 → ±3，尖 +4.33 → +4），不会出现斜边
//   在像素格子里左右各偏一半、看起来"被拧了"的问题。
//
// ★ 箭头挂在 0° 端（缺口上端），方向取"角度减小"一侧的切线 = 屏幕正下，
//   也就是顺着顺时针方向转入缺口 —— 和整枚图标表达的"顺时针刷新"一致。
//
// ★ 线宽 1px：与标题栏按钮的描边同档（用户 2026-10-31 反馈 2px 太宽）。
void drawToolRefresh(int cxIn, int cy, Fl_Color c) {
    constexpr int r = 6;
    constexpr double a1 = 0.0;     // 起始角：正右，箭头挂在这里
    constexpr double a2 = 270.0;   // 终止角：正下，缺口 = 90°（右下）
    // 光学居中：箭头朝右侧展开，包围盒比圆环宽，不左移一格整体会偏右。
    const int cx = cxIn - 1;

    fl_color(c);
    fl_line_style(FL_SOLID, 1);
    fl_begin_line();
    fl_arc(cx - r, cy - r, 2 * r, 2 * r, a1, a2);
    fl_end_line();
    fl_line_style(0);

    // 箭头挂在弧端点上，三角形必须**关于端点严格左右对称**。
    //
    // ★ 全部用整数坐标算，不碰三角函数：a1=0 时端点就是 (cx + r, cy)，本来就
    //   是整数；底边两端取 ex ± kHalf，kHalf 是整数，尖取 (ex, ey + kLen)。
    //   这样弧端点的水平坐标 = 三角形竖直中线的水平坐标 = 尖端水平坐标，
    //   三个数一致，肉眼看到的才是"弧插在底边正中间"。
    //
    // ★ 之前翻车的原因就在这里：kHalf 取 2.5（半像素），两端是 px±2.5，
    //   lround 会把两个半像素**同时朝正方向**取整，底边整体右移半格，弧端点
    //   于是从"底边中点"滑到了偏左 —— 用户 2026-10-31："三角形要往左一点，
    //   让圆弧刚好插入三角形那条边的中间"。半像素在 5px 的底边上就是 20%。
    //
    // ★ 等边约束：高 = 边长·√3/2。底边 2·kHalf = 6px → 高 5.2，像素上取 5。
    //   kHalf 取 2 时底边只有 4px，三角形缩成一个看不出方向的小楔子，所以
    //   取 3；再宽就会盖住缺口。
    // ★ 整枚三角形再左移 1px（`- 1`）：数学上底边中点就在圆周 (cx+r) 上，
    //   但 1px 线宽的弧在右端占的是 cx+r-1 ~ cx+r 两个像素列，视觉重心偏左
    //   半格，三角形贴在圆周上时看上去仍偏右（用户 2026-10-31："还要往左
    //   1px"）。左移一格后三角形中线压住弧的末端像素列，才对得上。
    const int ex = cx + r - 1;
    const int ey = cy;
    constexpr int kHalf = 3;             // 底边半宽（整数，保证严格对称）
    constexpr int kLen  = 5;             // 高：6 × √3/2 ≈ 5.2，像素上取 5
    fl_polygon(ex, ey + kLen,            // 尖（正下方）
               ex + kHalf, ey,           // 底边右端
               ex - kHalf, ey);          // 底边左端
}

std::string toLowerCopy(const std::string &s) {
    std::string t = s;
    for (char &c : t) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return t;
}

// 画省略号：三个 2x2 的方点。为什么不用 U+2026（"…"）：本机实测这个字形被
// FLTK/GDI 画成 5x1 的死点（还带 ClearType 彩边），看起来像一条下划线。
// 手画方点在任何字体、任何 DPI 下都一致，也不受字体回退影响。
void drawEllipsisDots(int x, int cy, Fl_Color c) {
    fl_color(c);
    fl_rectf(x,     cy - 1, 2, 2);
    fl_rectf(x + 4, cy - 1, 2, 2);
    fl_rectf(x + 8, cy - 1, 2, 2);
}

// 把超宽文本截断到「放得下 + 留出省略号位置」。为什么不做水平滚动条：
//   ① 侧栏 160–280px，加一条水平滚动条再吃掉 10px 纵向空间，而文件名/标题
//      滚动是低频操作，不值得常驻控件；
//   ② Fl_Scroll 的水平滚动会让树行命中与缩进都多乘一个 scroll_x，复杂度
//      与收益不成比例。
// 截断后的全名通过 tooltip 呈现（SidePanel::updateRowTooltip）。
constexpr int kEllipsisW = 10;   // 3 个 2px 方点 + 2 个 2px 间隔

std::string elideToWidth(const std::string &s, int avail, int fontSize,
                         bool bold, bool *elided) {
    if (elided) *elided = false;
    if (avail <= 0) return std::string();
    fl_font(bold ? FL_HELVETICA_BOLD : FL_HELVETICA, fontSize);
    if (static_cast<int>(fl_width(s.c_str())) <= avail) return s;
    if (elided) *elided = true;

    const int limit = avail - kEllipsisW;
    if (limit <= 0) return std::string();
    // 按 UTF-8 字符边界从尾部回退（截半个中文会画出乱码方块）
    size_t end = s.size();
    while (end > 0) {
        size_t start = end - 1;
        while (start > 0 && (static_cast<unsigned char>(s[start]) & 0xC0) == 0x80)
            --start;
        const std::string sub = s.substr(0, start);
        if (static_cast<int>(fl_width(sub.c_str())) <= limit) return sub;
        end = start;
    }
    return std::string();
}

// ---- 面板里收录哪些文件 ----------------------------------------------------
//
// 早先只收录 .md / .markdown。用户反馈"一些 txt 都找不到，太离谱了"——编辑
// 器本来就能打开任意文本文件，侧栏没有理由把它们藏起来。现在分三档：
//   ① 已知文本扩展名 → 收录（Markdown/纯文本/配置/日志/各编程语言）
//   ② 已知二进制扩展名 → 跳过（exe/png/obj…，不靠猜）
//   ③ 未知或没有扩展名 → 读开头 2KB，出现 NUL 或控制字符过多就判为二进制
// 这样 .py/.cpp/.json/.ini/.log 都在树里，而 .exe/.png/.o 不会污染目录。
const char *const kTextExts[] = {
    ".md", ".markdown", ".txt", ".text", ".log", ".rst", ".adoc", ".org",
    ".ini", ".cfg", ".conf", ".config", ".properties", ".env", ".toml",
    ".json", ".jsonc", ".json5", ".yaml", ".yml", ".xml", ".csv", ".tsv",
    ".html", ".htm", ".xhtml", ".css", ".scss", ".sass", ".less",
    ".js", ".mjs", ".cjs", ".ts", ".tsx", ".jsx", ".vue", ".svelte",
    ".py", ".pyw", ".rb", ".php", ".pl", ".pm", ".lua", ".c", ".h",
    ".cc", ".cpp", ".cxx", ".hpp", ".hxx", ".hh", ".inl",
    ".m", ".mm", ".cs", ".java", ".kt", ".kts", ".go", ".rs", ".swift",
    ".scala", ".sh", ".bash", ".zsh", ".fish", ".bat", ".cmd", ".ps1",
    ".sql", ".r", ".jl", ".dart", ".ex", ".exs", ".erl", ".hs", ".clj",
    ".lisp", ".el", ".vim", ".tex", ".proto", ".graphql", ".gql",
    ".diff", ".patch", ".cmake", ".gradle", ".mk",
};

const char *const kBinaryExts[] = {
    ".exe", ".dll", ".lib", ".obj", ".o", ".a", ".so", ".dylib", ".pdb",
    ".ilk", ".exp", ".bin", ".dat", ".db", ".sqlite", ".sqlite3", ".class",
    ".pyc", ".pyo", ".zip", ".7z", ".rar", ".gz", ".tar", ".bz2", ".xz",
    ".zst", ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".ico", ".icns",
    ".webp", ".tif", ".tiff", ".psd", ".pdf", ".doc", ".docx", ".xls",
    ".xlsx", ".ppt", ".pptx", ".odt", ".ods", ".mp3", ".mp4", ".wav",
    ".flac", ".ogg", ".avi", ".mkv", ".mov", ".webm", ".ttf", ".otf",
    ".woff", ".woff2", ".eot", ".lnk", ".iso", ".msi", ".nupkg",
};

// 只看最后一段扩展名（含点，小写）。目录部分先跳过，免得 "C:\a.b\notes" 被
// 当成 .b 文件——与 pathutil::isMarkdownPath 同一套 ASCII 规则。
std::string lowerExtension(const std::string &path) {
    const auto sep = path.find_last_of("\\/");
    const size_t base = (sep == std::string::npos) ? 0 : sep + 1;
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos || dot < base || dot + 1 >= path.size())
        return std::string();
    return toLowerCopy(path.substr(dot));
}

bool extInList(const std::string &ext, const char *const *list, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (ext == list[i]) return true;
    return false;
}

// 读开头 2KB 猜二进制。为什么不能只看扩展名：用户会把笔记存成 .note 或者
// 干脆没有扩展名，这类文件只有内容能说明问题。
bool looksBinary(const std::string &path) {
    std::ifstream f(pathutil::fromUtf8(path), std::ios::binary);
    if (!f) return true;                       // 打不开（权限/被占用）就不列
    char buf[2048];
    f.read(buf, sizeof(buf));
    const std::streamsize n = f.gcount();
    if (n <= 0) return false;                  // 空文件：算文本，可以打开来编辑
    int ctrl = 0;
    for (std::streamsize i = 0; i < n; ++i) {
        const unsigned char b = static_cast<unsigned char>(buf[i]);
        if (b == 0) return true;               // NUL 基本只有二进制/UTF-16 才有
        if (b < 0x20 && b != '\t' && b != '\r' && b != '\n' &&
            b != '\f' && b != '\v')
            ++ctrl;
    }
    return static_cast<long long>(ctrl) * 20 > n;   // 控制字符 > 5% 视为二进制
}

bool isPanelTextFile(const std::string &path) {
    const std::string ext = lowerExtension(path);
    if (!ext.empty()) {
        if (extInList(ext, kTextExts, sizeof(kTextExts) / sizeof(kTextExts[0])))
            return true;
        if (extInList(ext, kBinaryExts, sizeof(kBinaryExts) / sizeof(kBinaryExts[0])))
            return false;
    }
    return !looksBinary(path);
}

} // namespace

// ---------------------------------------------------------------------------
// SideTree
// ---------------------------------------------------------------------------

SideTree::SideTree(int x, int y, int w, int h, SidePanel *owner)
    : Fl_Widget(x, y, w, h), m_owner(owner)
{
    box(FL_NO_BOX);
}

void SideTree::draw() {
    SidePanel *o = m_owner;
    if (!o) return;
    const ThemeColors &tc = o->m_tc;

    // 可见行范围：Fl_Scroll 在 draw_clip 里已经把裁剪区设成可视窗口，直接
    // 问它就行。
    //
    // ★ 旧实现用 `ry = y() + i*rowH` 和 `ry + rowH <= y()` / `ry >= y()+h()`
    //   判视口，而行本身就是按 y() 排布的——两个判断恒不成立，等于每帧把
    //   **所有**行都过一遍。5MB 文档的大纲有 5498 行，而单次 fl_width 在本机
    //   实测约 65us，一帧文本测量就是 0.6~1.6 秒（滚动、点大纲都卡死）。
    const int rowH = o->m_rowH;
    const int nRows = static_cast<int>(o->m_rows.size());
    int vx = x(), vy = y(), vw = w(), vh = h();
    int first = 0, last = nRows - 1;
    switch (fl_clip_box(x(), y(), w(), h(), vx, vy, vw, vh)) {
    case 2: last = -1; break;                       // 与本控件不相交
    case 1:                                         // 部分相交 → 换算成行号
        first = std::max(0, (vy - y()) / rowH);
        last  = std::min(nRows - 1, (vy + vh - 1 - y()) / rowH);
        break;
    default: break;                                 // 0 = 完全可见，全画
    }
    // 内容区底色与文本编辑区一致（background1）；Tab 条用 background2
    // （与状态栏同参），视觉分层交给 Tab 条，不靠加深内容区。
    // 只填可见区：大文档的大纲内容高度可达十几万像素，按控件全高填没有意义。
    if (nRows == 0 || last >= first) {
        fl_color(tc.background1);
        fl_rectf(vx, vy, vw, vh);
    }

    for (int i = first; i <= last; ++i) {
        const SideRow &r = o->m_rows[i];
        const int ry = y() + i * rowH;

        const bool selected = (i == o->m_selected);
        const bool hovered  = (i == o->m_hover);
        if (selected || hovered) {
            fl_color(selected ? tc.highlight2 : tc.highlight1);
            fl_rectf(x(), ry, w(), rowH);
        }

        const int left = x() + kPadX + r.depth * kIndent;
        // 目录行与「有子标题」的大纲行都画箭头（大纲折叠，2026-10-31）。
        if (r.isDir || r.hasChildren) {
            drawChevron(left + 5, ry + rowH / 2, r.expanded,
                        tc.text2);
        }
        const int textLeft = o->rowTextLeft(r);
        const int avail = o->rowTextAvail(r);

        // 大纲标题一律不加粗（2026-10-31 用户）：H1 单独加粗在列表里像"选中
        // 态"，而且别的编辑器的大纲也都不加粗。层级已经由缩进表达，够用。
        const bool bold = false;
        fl_font(bold ? FL_HELVETICA_BOLD : FL_HELVETICA, o->m_uiFontSize);
        Fl_Color fg = tc.text1;
        if (r.isDir) fg = tc.text1;
        else if (r.isHeading && r.level >= 3) fg = tc.text2;
        // 二进制文件：灰显（点了也不打开，见 rowClicked）
        else if (!r.openable) fg = tc.text2;
        if (selected || hovered) fg = fl_contrast(fg, selected ? tc.highlight2 : tc.highlight1);
        fl_color(fg);
        // 超宽行截断 + 手画省略号；全名见 tooltip（updateRowTooltip）。
        // 截断结果走缓存（elidedLabel）：文本测量是这条路上最贵的一步。
        bool elided = false;
        int  textW = 0;
        const std::string &shown = o->elidedLabel(r.label, avail, bold, &elided, &textW);
        fl_draw(shown.c_str(), textLeft, ry, avail, rowH, FL_ALIGN_LEFT | FL_ALIGN_CLIP);
        if (elided) {
            // 与 fl_draw(框版) 的垂直居中规则一致：文字顶 = ry + (行高 - 字高)/2
            const int baseline = ry + (rowH - fl_height()) / 2 + fl_height() - fl_descent();
            drawEllipsisDots(textLeft + textW + 3, baseline, fg);
        }
    }
}

// 行文本截断（带缓存）。返回值指向缓存里的字符串，引用在下一次同 key 重算
// 前保持有效（unordered_map 的引用不因 rehash 失效）。
const std::string &SidePanel::elidedLabel(const std::string &label, int avail,
                                          bool bold, bool *elidedOut, int *widthOut) {
    auto it = m_elideCache.find(label);
    if (it != m_elideCache.end()) {
        ElideEntry &e = it->second;
        if (e.avail == avail && e.fontPx == m_uiFontSize && e.bold == bold) {
            if (elidedOut) *elidedOut = e.elided;
            if (widthOut)  *widthOut = e.textW;
            return e.text;
        }
    }
    // 大文档大纲可能见过几千种标题，但真正同时需要的只有一屏；超量就整体
    // 丢弃（比维护 LRU 简单，代价是下次重算一屏）。
    if (m_elideCache.size() > kMaxElideCache) m_elideCache.clear();

    ElideEntry e;
    e.avail = avail;
    e.fontPx = m_uiFontSize;
    e.bold = bold;
    e.text = elideToWidth(label, avail, m_uiFontSize, bold, &e.elided);
    // 省略号的落点需要截断文本的像素宽；这里量一次跟着缓存走，
    // 否则每帧每行都要再花一次 fl_width（本机约 65us）。
    if (e.elided) e.textW = static_cast<int>(fl_width(e.text.c_str()));
    auto res = m_elideCache.insert_or_assign(label, std::move(e));
    const ElideEntry &stored = res.first->second;
    if (elidedOut) *elidedOut = stored.elided;
    if (widthOut)  *widthOut = stored.textW;
    return stored.text;
}

int SideTree::handle(int event) {
    SidePanel *o = m_owner;
    if (!o) return 0;
    switch (event) {
    // FL_ENTER 必须返回 1：Fl_Group::handle 对 FL_MOVE 的分发逻辑是
    //   「belowmouse 已经是这个子控件 → 送 FL_MOVE；否则先送 FL_ENTER」，
    //   只有 ENTER 被接住（返回 1）belowmouse 才会落到树上，之后的 FL_MOVE
    //   才进得来。不接 FL_ENTER 的话 hover 高亮与长文件名 tooltip 全部失效
    //   （表现为"鼠标划过侧栏毫无反应"）。
    case FL_ENTER: {
        int row = o->rowAt(Fl::event_y());
        if (row != o->m_hover) { o->m_hover = row; redraw(); }
        o->updateRowTooltip(row);
        return 1;
    }
    case FL_MOVE: {
        int row = o->rowAt(Fl::event_y());
        if (row != o->m_hover) { o->m_hover = row; redraw(); }
        o->updateRowTooltip(row);
        return 1;
    }
    case FL_LEAVE:
        if (o->m_hover != -1) { o->m_hover = -1; redraw(); }
        o->updateRowTooltip(-1);
        return 1;
    case FL_PUSH:
        if (Fl::event_button() == FL_LEFT_MOUSE) {
            int row = o->rowAt(Fl::event_y());
            if (row >= 0) o->rowClicked(row, Fl::event_x());
            return 1;
        }
        break;
    default:
        break;
    }
    return Fl_Widget::handle(event);
}

// ---------------------------------------------------------------------------
// SidePanel
// ---------------------------------------------------------------------------

SidePanel::SidePanel(int x, int y, int w, int h)
    : Fl_Group(x, y, w, h)
{
    box(FL_NO_BOX);
    // 面板顶部两条横栏（Tab 条、工具条）与菜单栏/状态栏同高：gTextBarH 是
    // 全程序"无边框文本条"的统一高度，三者一致才不会显得 Tab 条突兀。
    m_tabH = MENU_H;
    m_toolH = MENU_H;
    m_scroll = new Fl_Scroll(x, y + m_tabH + m_toolH, w, h - m_tabH - m_toolH);
    m_scroll->type(Fl_Scroll::VERTICAL);
    m_scroll->box(FL_NO_BOX);
    // 滚动条与主编辑器同款：10px、扁平、轨道 background1、滑块 background2
    // （ThemeWidgets.h 的 styleToolScrollbar；FLTK 默认滚动条带凹槽和边框，
    //  与编辑区摆在一起明显不是一套样式）。
    m_scroll->scrollbar_size(10);
    styleToolScrollbar(&m_scroll->scrollbar, FL_VERT_SLIDER, m_tc);
    m_scroll->color(m_tc.background1);
    add(m_scroll);
    m_tree = new SideTree(x, y + m_tabH + m_toolH, w - 2, 1, this);
    m_scroll->add(m_tree);
    m_scroll->end();
    end();
    relayout();
}

void SidePanel::setColors(const ThemeColors &tc, int uiFontSize) {
    m_tc = tc;
    const int fs = (uiFontSize > 0) ? uiFontSize : 16;
    if (fs != m_uiFontSize) {
        m_uiFontSize = fs;
        m_rowH = fs + 10;
        // Tab 条高度不在这里算：它和工具条一样跟菜单栏等高（见 relayout），
        // 三条横栏统一高度才不会"文件/大纲比别的栏高一截"。
    }
    if (m_scroll) {
        m_scroll->color(m_tc.background1);
        styleToolScrollbar(&m_scroll->scrollbar, FL_VERT_SLIDER, m_tc);
    }
    relayout();
}

void SidePanel::setTab(Tab t) {
    if (m_tab == t) return;
    m_tab = t;
    rebuildRows();
    redraw();
    // The Tab strip is painted by this widget, not by a child, so the
    // non-client band beside it samples it through bandColourAt - which means
    // the band has to be repainted when the active tab changes, or it keeps
    // the previous tab's colour.
    if (Fl_Window *win = window()) windowChrome::refreshFrameBand(win);
}

void SidePanel::setRoot(const std::string &dir) {
    // 目录没变：不重扫硬盘（切标签页会频繁触发），滚动位置与展开状态都保留
    if (dir == m_rootDir && m_hasRoot) return;

    m_rootDir = dir;
    m_root = FsNode();
    m_hasRoot = !dir.empty();
    m_error.clear();
    // 换了文档：上一个文件的折叠状态在这里作废。不清的话标题链 key 会在
    // 两个文档之间撞车（同名章节），折叠状态串到别的文件上。
    m_outlineCollapsed.clear();
    if (!m_hasRoot) {
        // Do NOT cache a translated string here: the row label must follow
        // runtime language switches (rebuildRows resolves it live).
        rebuildRows();
        return;
    }
    m_root.name = std::filesystem::path(pathutil::fromUtf8(dir))
                      .filename().u8string();
    if (m_root.name.empty()) m_root.name = dir;
    m_root.path = dir;
    m_root.isDir = true;
    int budget = kMaxNodes;
    scanInto(m_root, 1, budget);
    // 根目录默认展开（否则第一眼是空面板）
    m_expanded.insert(dir);
    rebuildRows();
}

void SidePanel::rescan() {
    if (m_rootDir.empty()) { rebuildRows(); return; }
    // setRoot 对"目录没变"会短路返回（切标签页时省一次全盘扫描），但手动
    // 刷新要的就是重新读盘：清掉缓存目录骗过短路。展开状态保留（m_expanded
    // 不动），刷新后用户看到的还是原来展开的那几层。
    const std::string dir = m_rootDir;
    m_rootDir.clear();
    setRoot(dir);
}

void SidePanel::refreshLanguage() {
    // 不重扫磁盘：只重建可见行，让合成行（未打开/空文件夹/无标题）改用
    // 新语言的 I18n 文本。m_rows 里的文件名不受语言影响。
    rebuildRows();
    redraw();
}

void SidePanel::scanInto(FsNode &parent, int depth, int &budget) {
    if (depth > kMaxDepth || budget <= 0) return;
    std::error_code ec;
    std::filesystem::directory_iterator it(pathutil::fromUtf8(parent.path), ec);
    if (ec) return;

    std::vector<FsNode> dirs, files;
    for (const auto &entry : it) {
        if (budget <= 0) break;
        std::error_code ec2;
        const bool isDir = entry.is_directory(ec2);
        if (ec2) continue;
        std::error_code ec3;
        const bool isFile = entry.is_regular_file(ec3);
        if (!isDir && (ec3 || !isFile)) continue;

        std::string name = entry.path().filename().u8string();
        if (name.empty()) continue;
        if (isDir && skipDirectory(name)) continue;

        std::string full = entry.path().u8string();

        FsNode n;
        n.name = name;
        n.path = full;
        n.isDir = isDir;
        // 二进制文件也列出来（用户 2026-10-31：图片全不见了，看着像"文件列表缺失"），
        // 但标成不可打开 —— 点开会在编辑器里显示乱码。灰显 + 不响应点击。
        if (!isDir) n.openable = isPanelTextFile(full);
        (isDir ? dirs : files).push_back(std::move(n));
        --budget;
    }

    auto byName = [](const FsNode &a, const FsNode &b) {
        return lessName(a.name, b.name);
    };
    std::sort(dirs.begin(), dirs.end(), byName);
    std::sort(files.begin(), files.end(), byName);

    for (auto &d : dirs) {
        scanInto(d, depth + 1, budget);
        parent.children.push_back(std::move(d));
    }
    for (auto &f : files) parent.children.push_back(std::move(f));
}

void SidePanel::setOutline(const std::vector<std::string> &labels,
                           const std::vector<int> &levels,
                           const std::vector<std::string> &ids,
                           const std::vector<int> &lines) {
    m_outlineLabels = labels;
    m_outlineLevels = levels;
    m_outlineIds = ids;
    m_outlineLines = lines;
    rebuildRows();
    redraw();
}

void SidePanel::setCurrentPath(const std::string &path) {
    // 换了文档：上一个文件的折叠状态在这里作废。setRoot() 那边也清一次，但
    // 它按"目录没变"短路——同目录里换文件（最常见的场景）根本走不到。
    // key 虽然是标题链文本，两篇文档出现同名章节时仍会串状态。
    if (path != m_currentPath) m_outlineCollapsed.clear();
    m_currentPath = path;
    m_selected = -1;
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        if (!m_rows[i].path.empty() && m_rows[i].path == path) { m_selected = i; break; }
    }
    if (m_tree) m_tree->redraw();
}

// --- 大纲折叠 --------------------------------------------------------------
//
// 可见行计算本身在 ui/OutlineRows.cpp（纯函数、可单测）。这里只做两件事：
// 把结果灌进 m_rows；把"箭头挡在文字前面"的命中判断与折叠集合的增删。

// 箭头画在「行文字左缘往左 14px」这一格（对应 rowTextLeft 的 14 预留），
// 命中区就取这一整格：点箭头 = 折叠/展开，点文字 = 跳转。
//
// ★ 必须分热区：把「点行任意位置=折叠」和「点标题=跳转」叠在同一行上，
//   两者是互斥的——只能二选一。所以箭头单独占一格，而不是整行响应。
bool SidePanel::chevronHit(const SideRow &r, int px) const {
    if (!r.isDir && !r.hasChildren) return false;
    const int left = rowTextLeft(r) - kChevronW;
    return px >= left && px < left + kChevronW;
}

void SidePanel::toggleHeadingCollapse(const SideRow &r) {
    if (r.key.empty()) return;
    auto it = m_outlineCollapsed.find(r.key);
    if (it != m_outlineCollapsed.end())
        m_outlineCollapsed.erase(it);
    else
        m_outlineCollapsed.insert(r.key);
    rebuildRows();
    redraw();
}

void SidePanel::rebuildRows() {
    m_rows.clear();
    m_hover = -1;
    m_selected = -1;

    if (m_tab == Files) {
        if (!m_hasRoot) {
            SideRow r;
            r.label = m_error.empty() ? I18n::get("panel.noopen") : m_error;
            m_rows.push_back(r);
        } else {
            std::function<void(const FsNode &, int)> walk =
                [&](const FsNode &n, int depth) {
                    SideRow r;
                    r.label = n.name;
                    r.path = n.path;
                    r.depth = depth;
                    r.isDir = n.isDir;
                    r.openable = n.openable;
                    if (n.isDir) {
                        r.hasChildren = !n.children.empty();
                        r.expanded = m_expanded.count(n.path) > 0;
                    }
                    m_rows.push_back(r);
                    if (n.isDir && r.expanded)
                        for (const auto &c : n.children) walk(c, depth + 1);
                };
            walk(m_root, 0);   // 根目录自身作为首行（让用户看到在哪个目录）
            if (m_rows.size() == 1 && m_root.children.empty()) {
                SideRow r;
                r.label = I18n::get("panel.empty");
                r.depth = 1;
                m_rows.push_back(r);
            }
        }
    } else {
        // 大纲：标题数据 + 折叠集合 → 可见行（纯计算，见 ui/OutlineRows.cpp）
        m_rows = outline_rows::build(m_outlineLabels, m_outlineLevels,
                                     m_outlineIds, m_outlineCollapsed);
        if (m_rows.empty()) {
            // 合成行要查 I18n（纯函数刻意不碰全局状态，见 OutlineRows.h）
            SideRow r;
            r.label = I18n::get("preview.toc.empty");
            m_rows.push_back(r);
        }
    }

    if (!m_currentPath.empty()) {
        for (int i = 0; i < static_cast<int>(m_rows.size()); ++i)
            if (!m_rows[i].path.empty() && m_rows[i].path == m_currentPath) {
                m_selected = i;
                break;
            }
    }
    syncTreeGeometry();
}

int SidePanel::contentHeight() const {
    return std::max(1, static_cast<int>(m_rows.size()) * m_rowH);
}

void SidePanel::syncTreeGeometry() {
    if (!m_scroll || !m_tree) return;
    // 右侧留出滚动条宽度；行文本另有 avail 裁剪，不会被滑块压住
    const int sw = std::max(1, w() - 16);
    const int ch = contentHeight();
    // 只在尺寸真的变化时 resize：Fl_Scroll 的滚动偏移挂在子控件 y 上，
    // 频繁 resize 会把侧栏滚动位置打回顶部（拖分隔条时会表现为"跳动"）。
    if (m_tree->w() == sw && m_tree->h() == ch) return;
    m_tree->resize(m_tree->x(), m_tree->y(), sw, ch);
    m_tree->redraw();
    m_scroll->redraw();
}

void SidePanel::relayout() {
    if (!m_scroll || !m_tree) return;
    // 顶部两条横栏与菜单栏/状态栏统一高度（gTextBarH）。MENU_H 随"文本条
    // 高度"设置变，所以每次重排都同步，不能只在构造里取。
    m_tabH = MENU_H;
    m_toolH = MENU_H;
    // 内容区 = 面板高 - 顶部 Tab 条 - 工具条（工具条贴在 Tab 条下面）
    const int bodyH = std::max(1, h() - m_tabH - m_toolH);
    m_scroll->resize(x(), y() + m_tabH + m_toolH, w(), bodyH);
    // 宽度变了，行的截断状态可能翻转（截断↔完整），tooltip 需重新判定
    m_tooltipRow = -1;
    m_tree->tooltip(nullptr);
    syncTreeGeometry();
}

// ---------------------------------------------------------------------------
// 工具条（Tab 条下面一行图标按钮：全部展开/折叠 · 刷新）
// ---------------------------------------------------------------------------
//
// 配色与交互全部套菜单栏的参数（用户 2026-10-31 指定）：底色 background3、
// 图标色 text1、悬停 highlight1（= 菜单栏 selection_color）、按下 highlight2
// （= HoverMenuBar pressColor）。高度也跟 MENU_H 一致，看上去是同一套控件。

void SidePanel::toolRects(int rx[kToolSlots], int rw[kToolSlots], int &ry, int &rh) const {
    // 按钮 = 铺满工具条高度的方格，右对齐（刷新最右，展开/折叠在它左边）。
    //
    // 高度取整条 m_toolH 而不是"高度 - 4"：菜单栏的悬停高亮是铺满整条高度
    // 的，工具条若画成 20×20 悬浮方块，同一窗口里就有两套悬停观感。铺满之后
    // 两者是同一套规则，顺带把点击区从 20×20 放大到 24×24（用户 2026-10-31
    // 问"要不要和标题栏按钮统一宽度"——标题栏那 32×24 是窗口外观件，不必照
    // 抄宽度；该统一的是这条"色块铺满条高"的规则）。
    //
    // 预览格（i >= 2）往左排，与功能格紧邻且同样铺满条高，看起来是同一排。
    const int bs = std::max(16, m_toolH);
    rh = bs;
    ry = y() + m_tabH;
    const int xr = x() + w() - 4 - bs;
    rx[1] = xr;
    rw[1] = bs;
    rx[0] = xr - bs;   // 两格紧邻，中间不留缝（菜单项也是紧邻的）
    rw[0] = bs;
}

void SidePanel::drawToolBar() {
    const int ty = y() + m_tabH;
    // 只有这一块 background3，下缘不再画任何分隔线：用户 2026-10-31 明确
    // 反馈"工具条底部留了一条和文件/大纲同色的细线，不能接受"。分层靠
    // background3 与内容区 background1 的明度差本身，够了。
    fl_color(m_tc.background3);                 // 与菜单栏同一个主题参数
    fl_rectf(x(), ty, w(), m_toolH);

    int rx[kToolSlots], rw[kToolSlots], ry, rh;
    toolRects(rx, rw, ry, rh);

    for (int i = 0; i < kToolSlots; ++i) {
        const int cx = rx[i] + rw[i] / 2;
        const int cy = ry + rh / 2;
        Fl_Color bg = m_tc.background3;
        if (m_toolPressed == i)      bg = m_tc.highlight2;
        else if (m_toolHover == i)   bg = m_tc.highlight1;
        if (bg != m_tc.background3) {
            fl_color(bg);
            fl_rectf(rx[i], ry, rw[i], rh);
        }
        // 按钮表示"下一步做什么"：已全展开 → 画向上（折叠）；否则画向下（展开）
        if (i == 0) drawToolChevrons(cx, cy, !allExpanded(), m_tc.text1);
        else        drawToolRefresh(cx, cy, m_tc.text1);
    }
}

int SidePanel::toolAt(int px, int py) const {
    if (py < y() + m_tabH || py >= y() + m_tabH + m_toolH) return -1;
    int rx[kToolSlots], rw[kToolSlots], ry, rh;
    toolRects(rx, rw, ry, rh);
    // 只认前 2 格（功能格）：预览格如果以后再加，不能参与命中
    for (int i = 0; i < kToolSlots; ++i)
        if (px >= rx[i] && px < rx[i] + rw[i] && py >= ry && py < ry + rh)
            return i;
    return -1;
}

void SidePanel::triggerTool(int which) {
    if (which == 0) {
        if (allExpanded()) collapseAll();
        else               expandAll();
        return;
    }
    if (which != 1) return;
    // 刷新必须走 MainWindow 注入的那条路：大纲数据在窗口那边（预览开/关
    // 两条来源），面板自己重排是刷新不出新标题的。
    if (m_refresh) m_refresh();
    else if (m_tab == Files) rescan();
}

// 文件页：以「非空目录（含根目录自己）全部展开」为全展开。
//
// ★ 必须把"自己"算进来：文件树的第一行就是根目录（见 rebuildRows 的
//   walk(m_root, 0)），而 setRoot 初始就把根目录塞进 m_expanded。早先这里
//   只看子目录，于是 collapseAll 把根目录也塌掉之后再点"全部展开"，
//   子目录全插回去了、根目录却还塌着 → 面板只剩一行，看起来"展开无效"。
bool SidePanel::dirsAllExpanded(const FsNode &n) const {
    if (!n.children.empty() && m_expanded.count(n.path) == 0) return false;
    for (const auto &c : n.children) {
        if (!c.isDir) continue;
        if (!dirsAllExpanded(c)) return false;
    }
    return true;
}

void SidePanel::expandAllDirs(const FsNode &n) {
    if (!n.children.empty()) m_expanded.insert(n.path);   // 自己也展开
    for (const auto &c : n.children) {
        if (!c.isDir) continue;
        expandAllDirs(c);
    }
}

std::set<std::string> SidePanel::allOutlineCollapseKeys() const {
    std::set<std::string> keys;
    // 空折叠集合下构建一次 = 拿到全文所有标题的 key，挑出"有子标题"的那些。
    for (const auto &r : outline_rows::build(m_outlineLabels, m_outlineLevels,
                                             m_outlineIds, {}))
        if (r.hasChildren) keys.insert(r.key);
    return keys;
}

bool SidePanel::allExpanded() const {
    if (m_tab == Files)
        return !m_hasRoot || dirsAllExpanded(m_root);
    for (const auto &r : outline_rows::build(m_outlineLabels, m_outlineLevels,
                                             m_outlineIds, {}))
        if (r.hasChildren && m_outlineCollapsed.count(r.key)) return false;
    return true;
}

void SidePanel::expandAll() {
    if (m_tab == Files) {
        if (m_hasRoot) expandAllDirs(m_root);
    } else {
        m_outlineCollapsed.clear();
    }
    rebuildRows();
    redraw();
}

void SidePanel::collapseAll() {
    if (m_tab == Files)
        m_expanded.clear();
    else
        m_outlineCollapsed = allOutlineCollapseKeys();
    rebuildRows();
    redraw();
}

int SidePanel::rowAt(int py) const {
    if (!m_tree) return -1;
    // py 是窗口坐标；m_tree->y() 与它在同一条坐标轴上（= 面板顶 + Tab 高，
    // Fl_Scroll 滚动时随 scroll_to 一起平移），所以直接相减就够。
    // ★ 实测数据（2026-10-31，探针记录）：未滚动时 tree.y=102，滚 80px 后
    //   tree.y=22，滚 306px 后 tree.y=-204 —— 三者与行绘制用的 y() 完全一致。
    //   不要"顺手"再减一次 m_scroll->yposition()：那样会重复计算，点击整体
    //   错位若干行，表现正是"点了 A 却折叠了上面的 B"。
    const int rel = py - m_tree->y();
    if (rel < 0) return -1;
    const int row = rel / m_rowH;
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return -1;
    return row;
}

void SidePanel::rowClicked(int row, int px) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return;
    const SideRow &r = m_rows[row];
    if (r.isHeading) {
        // 箭头热区 → 折叠/展开本节；否则 → 跳到该标题。
        if (r.hasChildren && chevronHit(r, px)) {
            toggleHeadingCollapse(r);
            return;
        }
        if (!r.anchor.empty() && m_pickHeading) m_pickHeading(r.anchor);
        m_selected = row;
        if (m_tree) m_tree->redraw();
        return;
    }
    if (r.path.empty()) return;
    if (r.isDir) {
        if (m_expanded.count(r.path)) m_expanded.erase(r.path);
        else m_expanded.insert(r.path);
        rebuildRows();
        redraw();
        return;
    }
    // 二进制文件只做展示：点开会在编辑器里显示乱码（Document 不做二进制识别）
    if (!r.openable) return;
    if (m_pickFile) m_pickFile(r.path);
}

void SidePanel::draw() {
    // Tab 条：底 = background2（与状态栏同参），选中项 highlight1。
    fl_color(m_tc.background2);
    fl_rectf(x(), y(), w(), m_tabH);

    fl_font(FL_HELVETICA, m_uiFontSize);
    const int gap = kTabGap;
    const int tabW = (w() - gap * 3) / 2;
    for (int i = 0; i < 2; ++i) {
        m_tabX[i] = x() + gap + i * (tabW + gap);
        m_tabW[i] = tabW;
        const bool active = (static_cast<int>(m_tab) == i);
        // 高度统一成 MENU_H（24）后内缩从 3 收到 2，否则 16px 字只剩 18px
        // 可用高，CJK 会被切掉一点点。
        if (active) {
            fl_color(m_tc.highlight1);
            fl_rectf(m_tabX[i], y() + 2, tabW, m_tabH - 4);
        }
        // 两个 Tab 都用主文字色：选中态已经有 highlight1 底色区分，未选中项
        // 再用注释色就像"禁用"，与状态栏/工具栏的用色规则也不一致。
        fl_color(active ? fl_contrast(m_tc.text1, m_tc.highlight1) : m_tc.text1);
        const char *label = (i == 0) ? I18n::get("panel.files")
                                     : I18n::get("panel.outline");
        fl_draw(label, m_tabX[i], y() + 2, tabW, m_tabH - 4,
                FL_ALIGN_CENTER);
    }

    // 右缘不再画分隔线：面板右侧紧挨着 4px 的 divider（background2），
    // 再画一条 borderLine 深色竖线只会变成"大纲右侧一条多余的黑线"
    // （用户 2026-10-20 反馈）。分层交给 divider 一件事就够了。

    Fl_Group::draw();

    // 工具条（Tab 条下面一行图标按钮）：自绘，不是子控件
    drawToolBar();

    // 滚动条最后画：Fl_Scroll 已按 FL_FLAT_BOX + 主题色画过一次，这里用与
    // 主编辑器逐像素相同的画法（ThemeWidgets.h drawToolScrollbar）覆盖，
    // 保证两个区域的滚动条看起来是同一套控件。
    if (m_scroll) drawToolScrollbar(&m_scroll->scrollbar, false, m_tc);
}

bool SidePanel::bandColourAt(int x, int y, unsigned int *colour) const {
    // x/y arrive in this widget's own coordinate space (the caller subtracts
    // our x()/y()). Only the Tab strip is answered here: below it the content
    // is SideTree's background1, which the normal widget-tree walk already
    // resolves through the window colour.
    if (!colour || y < 0 || y >= m_tabH) {
        // 工具条同理：它是自绘的 background3 色带，也要如实回答，
        // 否则窗口边条在那一行会退回窗口底色。
        if (colour && y >= m_tabH && y < m_tabH + m_toolH) {
            *colour = m_tc.background3;
            return true;
        }
        return false;
    }
    // Recompute the tab geometry with the same formula draw() uses. m_tabX /
    // m_tabW are absolute and only valid after a paint, so they cannot be
    // used from here.
    const int tabW = (w() - kTabGap * 3) / 2;
    const int t0 = kTabGap;
    const int t1 = t0 + tabW + kTabGap;
    if (x >= t0 && x < t0 + tabW) {
        *colour = (m_tab == Files) ? m_tc.highlight1 : m_tc.background2;
    } else if (x >= t1 && x < t1 + tabW) {
        *colour = (m_tab == Outline) ? m_tc.highlight1 : m_tc.background2;
    } else {
        *colour = m_tc.background2;   // gap / margins
    }
    return true;
}

int SidePanel::rowTextLeft(const SideRow &r) const {
    const int base = m_tree ? m_tree->x() : x();
    // 行首固定预留一格箭头位，有没有箭头都一样。
    //
    // ★ 这里曾经是"有子项才留 14px"，后果是灾难性的（2026-10-31 用户实测）：
    //   缩进步长 kIndent 也是 14px，于是"父行(有箭头) +14" 与 "子行(无箭头)
    //   +0" 正好抵消 —— 子行和父行左缘完全重合，看起来毫无层级（目录里的
    //   文件与目录同级）；更糟的是同级里"后面还有子标题的那一行"比"最后一
    //   行"右移 14px，出现"六级标题比五级标题更靠左"这种倒挂。
    //   箭头列必须对所有行恒定占位，缩进才是真的每级 +14。
    return base + kPadX + kChevronW + r.depth * kIndent;
}

int SidePanel::rowTextAvail(const SideRow &r) const {
    // 右界取树控件右缘再让 2px：树宽 = 面板宽 - 16，滚动条占面板最右 10px，
    // 所以文字不会跑到滑块底下（绘制与 tooltip 判定必须用同一个数）。
    const int right = (m_tree ? m_tree->x() + m_tree->w() : x() + w()) - 2;
    const int a = right - rowTextLeft(r);
    return (a < 8) ? 8 : a;
}

// 悬停某行：文本被省略号截断时弹 tooltip 显示全文（文件名与大纲标题同一套）。
// 未截断或离开面板时清掉，避免残留上一行的提示。
void SidePanel::updateRowTooltip(int row) {
    if (!m_tree) return;
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        if (m_tooltipRow != -1) {
            m_tree->copy_tooltip(nullptr);
            Fl_Tooltip::exit(m_tree);
            m_tooltipRow = -1;
        }
        return;
    }
    if (row == m_tooltipRow) return;   // 同一行不重复设置
    m_tooltipRow = row;

    const SideRow &r = m_rows[row];
    // 与 draw() 一致：标题不再加粗（见 draw 里的说明），否则 tooltip 的
    // 截断判定会用较粗的字体量宽度，和实际画出来的不一致。
    const bool bold = false;
    fl_font(bold ? FL_HELVETICA_BOLD : FL_HELVETICA, m_uiFontSize);
    // 与绘制同一条缓存：鼠标划过时不再重复做一遍文本测量
    bool elided = false;
    elidedLabel(r.label, rowTextAvail(r), bold, &elided);
    m_tree->copy_tooltip(elided ? r.label.c_str() : nullptr);

    if (!elided) {
        Fl_Tooltip::exit(m_tree);
        return;
    }

    // ★ 为什么不能只靠 copy_tooltip：Fl::handle 只在 belowmouse **发生变化**时
    //   调 Fl_Tooltip::enter()，而那一刻 SideTree::handle(FL_ENTER) 还没执行，
    //   控件上还没有 tooltip 文本，整条路径什么都不弹；换行时文本变了但控件
    //   没变，enter_() 里的 `tw == widget_` 又会直接 return。
    //   enter_area 是 FLTK 给"控件内部子区域"准备的入口，可以按行重设。
    //   传进去的是 copy_tooltip 刚分配的缓冲区（指针每次都变），因此换行一定
    //   会重新计时弹出。
    Fl_Tooltip::enter_area(m_tree, 0, row * m_rowH, m_tree->w(), m_rowH,
                           m_tree->tooltip());
}

int SidePanel::handle(int event) {
    switch (event) {
    case FL_PUSH:
        if (Fl::event_button() == FL_LEFT_MOUSE) {
            const int tb = toolAt(Fl::event_x(), Fl::event_y());
            if (tb >= 0) {
                // 按下先换色（highlight2，与菜单栏按下一色），等抬起再执行；
                // 抬起位置已经移出按钮就取消——这是按钮的常规手感。
                m_toolPressed = tb;
                redraw();
                return 1;
            }
            if (Fl::event_y() < y() + m_tabH) {
                const int ex = Fl::event_x();
                for (int i = 0; i < 2; ++i) {
                    if (ex >= m_tabX[i] && ex < m_tabX[i] + m_tabW[i]) {
                        setTab(static_cast<Tab>(i));
                        return 1;
                    }
                }
                return 1;
            }
        }
        break;
    case FL_RELEASE:
        if (m_toolPressed >= 0) {
            const int which = m_toolPressed;
            m_toolPressed = -1;
            m_toolHover = toolAt(Fl::event_x(), Fl::event_y());
            redraw();
            if (m_toolHover == which) triggerTool(which);
            return 1;
        }
        break;
    case FL_ENTER:
    case FL_MOVE: {
        // 工具条是自绘的，没有子控件去接 FL_ENTER/FL_MOVE，悬停与 tooltip
        // 都只能在这里更新。
        const int tb = toolAt(Fl::event_x(), Fl::event_y());
        if (tb != m_toolHover) {
            m_toolHover = tb;
            // 图标没有文字，靠 tooltip 说清它是干什么的
            if (tb == 0)
                copy_tooltip(I18n::get(allExpanded() ? "panel.collapseall"
                                                     : "panel.expandall"));
            else if (tb == 1)
                copy_tooltip(I18n::get("panel.refresh"));
            else
                copy_tooltip(nullptr);
            redraw();
        }
        break;
    }
    case FL_LEAVE:
        // 落在 Tab 条上时让树的 hover 失效
        if (m_hover != -1) { m_hover = -1; if (m_tree) m_tree->redraw(); }
        if (m_toolHover != -1 || m_toolPressed != -1) {
            m_toolHover = -1;
            if (m_toolPressed >= 0) m_toolPressed = -1;
            copy_tooltip(nullptr);
            redraw();
        }
        break;
    default:
        break;
    }
    return Fl_Group::handle(event);
}
