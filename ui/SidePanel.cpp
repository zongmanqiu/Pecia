#include "SidePanel.h"

#include <FL/Fl_Scrollbar.H>
#include <FL/Fl_Tooltip.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <functional>

#include "core/I18n.h"
#include "core/PathUtils.h"
#include "ui/ThemeWidgets.h"   // styleToolScrollbar/drawToolScrollbar：滚动条与主编辑器同款

namespace {

constexpr int kIndent   = 14;   // 每级缩进
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

std::string toLowerCopy(const std::string &s) {
    std::string t = s;
    for (char &c : t) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return t;
}

// 画省略号：三个 2×2 的方点。
//
// 为什么不用 U+2026（"…"）：本机实测这个字形被 FLTK/GDI 画成 5×1 的死点
// （还带 ClearType 彩边），看起来像一条下划线 —— 用户直接反馈"没用省略号"。
// 手画方点在任何字体、任何 DPI 下都一致，也不受字体回退影响。
// cy 取基线（方点压在基线上，与西文句点的位置一致）。
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
        if (r.isDir) {
            drawChevron(left + 5, ry + rowH / 2, r.expanded,
                        tc.text2);
        }
        const int textLeft = o->rowTextLeft(r);
        const int avail = o->rowTextAvail(r);

        const bool bold = r.isHeading && r.level == 1;
        fl_font(bold ? FL_HELVETICA_BOLD : FL_HELVETICA, o->m_uiFontSize);
        Fl_Color fg = tc.text1;
        if (r.isDir) fg = tc.text1;
        else if (r.isHeading && r.level >= 3) fg = tc.text2;
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
            if (row >= 0) o->rowClicked(row);
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
    m_scroll = new Fl_Scroll(x, y + m_tabH, w, h - m_tabH);
    m_scroll->type(Fl_Scroll::VERTICAL);
    m_scroll->box(FL_NO_BOX);
    // 滚动条与主编辑器同款：10px、扁平、轨道 background1、滑块 background2
    // （ThemeWidgets.h 的 styleToolScrollbar；FLTK 默认滚动条带凹槽和边框，
    //  与编辑区摆在一起明显不是一套样式）。
    m_scroll->scrollbar_size(10);
    styleToolScrollbar(&m_scroll->scrollbar, FL_VERT_SLIDER, m_tc);
    m_scroll->color(m_tc.background1);
    add(m_scroll);
    m_tree = new SideTree(x, y + m_tabH, w - 2, 1, this);
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
        m_tabH = fs + 14;
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
        // 只列文本类文件（见 isPanelTextFile 的三档判断）
        if (!isDir && !isPanelTextFile(full)) continue;

        FsNode n;
        n.name = name;
        n.path = full;
        n.isDir = isDir;
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
    m_currentPath = path;
    m_selected = -1;
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        if (!m_rows[i].path.empty() && m_rows[i].path == path) { m_selected = i; break; }
    }
    if (m_tree) m_tree->redraw();
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
        if (m_outlineLabels.empty()) {
            SideRow r;
            r.label = I18n::get("preview.toc.empty");
            m_rows.push_back(r);
        } else {
            for (size_t i = 0; i < m_outlineLabels.size(); ++i) {
                SideRow r;
                r.label = m_outlineLabels[i];
                r.isHeading = true;
                r.level = (i < m_outlineLevels.size() && m_outlineLevels[i] > 0)
                              ? m_outlineLevels[i] : 1;
                r.depth = std::min(r.level - 1, 4);
                r.anchor = (i < m_outlineIds.size()) ? m_outlineIds[i] : std::string();
                m_rows.push_back(r);
            }
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
    // 内容区 = 面板高 - 顶部 Tab 条
    const int bodyH = std::max(1, h() - m_tabH);
    m_scroll->resize(x(), y() + m_tabH, w(), bodyH);
    // 宽度变了，行的截断状态可能翻转（截断↔完整），tooltip 需重新判定
    m_tooltipRow = -1;
    m_tree->tooltip(nullptr);
    syncTreeGeometry();
}

int SidePanel::rowAt(int py) const {
    if (!m_tree) return -1;
    const int rel = py - m_tree->y();
    if (rel < 0) return -1;
    const int row = rel / m_rowH;
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return -1;
    return row;
}

void SidePanel::rowClicked(int row) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return;
    const SideRow &r = m_rows[row];
    if (r.isHeading) {
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
        if (active) {
            fl_color(m_tc.highlight1);
            fl_rectf(m_tabX[i], y() + 3, tabW, m_tabH - 6);
        }
        // 两个 Tab 都用主文字色：选中态已经有 highlight1 底色区分，未选中项
        // 再用注释色就像"禁用"，与状态栏/工具栏的用色规则也不一致。
        fl_color(active ? fl_contrast(m_tc.text1, m_tc.highlight1) : m_tc.text1);
        const char *label = (i == 0) ? I18n::get("panel.files")
                                     : I18n::get("panel.outline");
        fl_draw(label, m_tabX[i], y() + 3, tabW, m_tabH - 6,
                FL_ALIGN_CENTER);
    }

    // 右缘不再画分隔线：面板右侧紧挨着 4px 的 divider（background2），
    // 再画一条 borderLine 深色竖线只会变成"大纲右侧一条多余的黑线"
    // （用户 2026-10-20 反馈）。分层交给 divider 一件事就够了。

    Fl_Group::draw();

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
    if (!colour || y < 0 || y >= m_tabH) return false;
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
    return base + kPadX + r.depth * kIndent + (r.isDir ? 14 : 0);
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
    const bool bold = (r.isHeading && r.level == 1);
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
    case FL_LEAVE:
        // 落在 Tab 条上时让树的 hover 失效
        if (m_hover != -1) { m_hover = -1; if (m_tree) m_tree->redraw(); }
        break;
    default:
        break;
    }
    return Fl_Group::handle(event);
}
