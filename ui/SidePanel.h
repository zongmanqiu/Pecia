// SidePanel.h - 左侧视图面板（文件树 / 文档大纲，顶部两个 Tab 切换）
//
// 设计参照 velotype 的 workspace drawer：
//   · 面板挂在内容区最左，纵跨编辑器与预览（不是塞进预览区，预览本来就窄）
//   · 顶部一条 Tab 条，"Files / Outline" 二选一，省掉两个独立面板
//   · 顶部 Tab 条下面还有一条工具条（高度 = MENU_H，底色用菜单栏的
//     background3）：右边两个图标按钮——「全部展开/全部折叠」双状态切换
//     （按当前页=目录或标题）与「刷新」（= Markdown > Refresh 的同一入口）。
//     配色/悬停/按下全部套菜单栏参数：文字色 text1、悬停 highlight1、
//     按下 highlight2（与 HoverMenuBar 一致）。图标与按钮都是面板自绘
//   · 树行自绘（FLTK 无现成树控件），目录可展开/折叠，只列文本类文件
//     （Markdown / 纯文本 / 配置 / 各编程语言源码；二进制扩展名与内容判为
//      二进制的文件不进树，判定见 SidePanel.cpp 的 isPanelTextFile）
//
// 数据流：MainWindow 在「切换文档 / 切 Tab」时调用 setRoot() 重扫磁盘；手动刷新
// （Markdown > Refresh / Ctrl+R）文件页调用 rescan()、大纲页调用 refreshOutline()；
// 大纲数据与预览目录树同源（预览开启时来自渲染回调，预览关闭时由
// extractOutlineHeadings() 现提；锚点 id 两侧一致，复用 toc-N）。
#pragma once

#include <FL/Fl_Group.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Widget.H>

#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Theme.h"
#include "ui/SideRow.h"       // SideRow（行数据，不含 FLTK 依赖）
#include "ui/WindowFrame.h"   // windowChrome::BandColourSource

class SidePanel;

// 面板内容区：自绘行、命中、滚动（放在 Fl_Scroll 里）。
class SideTree : public Fl_Widget {
public:
    SideTree(int x, int y, int w, int h, SidePanel *owner);
    void draw() override;
    int  handle(int event) override;

private:
    SidePanel *m_owner;
};

// 左侧视图面板。
class SidePanel : public Fl_Group, public windowChrome::BandColourSource {
public:
    enum Tab { Files = 0, Outline = 1 };
    using pick_file_cb_t    = std::function<void(const std::string &path)>;
    using pick_heading_cb_t = std::function<void(const std::string &anchor)>;

    SidePanel(int x, int y, int w, int h);

    // 主题与 UI 字号（切换主题/字号后调用，会重算行高并重排）。
    void setColors(const ThemeColors &tc, int uiFontSize);

    void setTab(Tab t);
    Tab  tab() const { return m_tab; }

    // 文件树：以 dir（当前文档所在目录）为根重扫磁盘。空串 = 清空。
    void setRoot(const std::string &dir);

    // 大纲：与预览目录树同源（labels/levels/ids/lines 四数组平行）。
    // lines = 每条标题在源码里的行号（1-based），供点击后把光标送过去；
    // 预览那条路产不出行号，给它传一个空数组（行号缺失时调用方自行重提，
    // 见 MainWindow::jumpToHeadingLine 的"先校验、不对就重提"策略）。
    void setOutline(const std::vector<std::string> &labels,
                    const std::vector<int> &levels,
                    const std::vector<std::string> &ids,
                    const std::vector<int> &lines);

    // 当前显示的大纲标题文本（与 setOutline 的 labels 同序）——跳源码时
    // 要拿它跟目标行比对，防止行号过期跳错位置。
    const std::vector<std::string> &outlineLabels() const { return m_outlineLabels; }

    // 大纲可见行的计算在 ui/OutlineRows.h（纯函数，可离线单测）；这里只
    // 负责把结果灌进 m_rows，并在"文档没有标题"时补一行合成提示。

    // 强制重扫磁盘（菜单/快捷键刷新时，文件页用）。setRoot 对"目录没变"
    // 会短路，这里清掉缓存目录骗过短路；已展开的目录状态保留。
    void rescan();

    // 运行时切换语言后调用：重建行（"未打开文件 / 空文件夹 / 无标题"
    // 这些合成行在 rebuildRows 里实时查 I18n，不缓存旧语言文本）。
    void refreshLanguage();

    void setPickFile(pick_file_cb_t cb) { m_pickFile = std::move(cb); }
    void setPickHeading(pick_heading_cb_t cb) { m_pickHeading = std::move(cb); }

    // 底部「刷新」按钮做的事 = 菜单 Markdown > Refresh（Ctrl+R）。文件页必须
    // 重扫磁盘、大纲页必须回到 MainWindow 重新提标题（标题数据在窗口那边），
    // 所以面板不自己实现，由 MainWindow 注入。
    void setRefresh(std::function<void()> cb) { m_refresh = std::move(cb); }

    // 底部「全部展开 / 全部折叠」：作用于**当前页**——文件页是目录，大纲页
    // 是标题。allExpanded() 决定按钮显示哪个文案（全展开 → 显示"全部折叠"）。
    bool allExpanded() const;
    void expandAll();
    void collapseAll();

    // 当前文档路径：用于在文件树里高亮选中行。
    void setCurrentPath(const std::string &path);

    // 主题/字号/宽度变化后重排内部控件。
    void relayout();
    void draw() override;
    int  handle(int event) override;

    // windowChrome::BandColourSource：顶部 Tab 条是在 draw() 里自绘的，
    // 控件树里没有对应子控件，非客户区边条必须直接问这里，否则会退回到
    // 窗口底色（background1），在 Tab 行左侧留下一块比 Tab 条更浅的色块。
    bool bandColourAt(int x, int y, unsigned int *colour) const override;

    // 供 SideTree 回调使用（friend 语义：只暴露它需要的两件事）。
    // px = 点击处的窗口 x（用来区分"点箭头"与"点标题文字"）。
    void rowClicked(int row, int px);
    // 悬停行变化时更新 tooltip：文本被省略号截断的行显示全名。
    void updateRowTooltip(int row);

private:
    friend class SideTree;   // 内容区的绘制/命中要读行数据与主题

    // 磁盘扫描结果（只在 setRoot/rescan 时重建）。
    struct FsNode {
        std::string name;
        std::string path;
        bool isDir = false;
        bool openable = true;   // false = 二进制（灰显、不响应点击）
        std::vector<FsNode> children;
    };

    void scanInto(FsNode &parent, int depth, int &budget);
    void rebuildRows();                       // 扫描树 + 展开集合 → m_rows
    // 行文本的"截断到可用宽度"结果缓存（键 = 原始文本）。为什么必须缓存：
    // fl_width 在本机实测约 65us/次（GDI 量 CJK），而截断要对每个候选前缀
    // 各量一次 —— 大文档大纲有几千行，不缓存时一次重绘就要几百毫秒到几秒。
    // 值里带参数快照，面板宽度/字号/粗细变了就重算，不需要额外失效逻辑。
    const std::string &elidedLabel(const std::string &label, int avail, bool bold,
                                   bool *elidedOut = nullptr, int *widthOut = nullptr);
    int  rowAt(int py) const;                 // 行命中（-1 = 无）
    int  rowTextLeft(const SideRow &r) const; // 行内文字的窗口绝对左缘
    int  rowTextAvail(const SideRow &r) const;// 行内文字可用宽度（截断/提示共用）

    // 点击是否落在某个可折叠行的箭头热区内（箭头占行文字左边 14px 一格）。
    bool chevronHit(const SideRow &r, int px) const;
    void toggleHeadingCollapse(const SideRow &r);
    int  contentHeight() const;
    void syncTreeGeometry();

    // --- 工具条（Tab 条下方一行图标按钮）--------------------------------
    // 自绘（与顶部 Tab 条同一套做法）：没有子控件，所以几何必须只有一个
    // 来源 —— toolRects() 同时喂给 draw() 和命中判定，否则"画的"和"点的"
    // 各算一遍，迟早出现"按钮看着在左、点下去要往右 3px"。
    static constexpr int kToolSlots = 2;
    void toolRects(int rx[kToolSlots], int rw[kToolSlots], int &ry, int &rh) const;
    void drawToolBar();
    int  toolAt(int px, int py) const;   // 0 = 展开/折叠，1 = 刷新，-1 = 无
    void triggerTool(int which);
    bool dirsAllExpanded(const FsNode &n) const;   // 文件页：目录是否全展开
    void expandAllDirs(const FsNode &n);
    std::set<std::string> allOutlineCollapseKeys() const;

    ThemeColors m_tc;
    int  m_uiFontSize = 16;
    int  m_rowH = 26;
    int  m_tabH = 30;
    int  m_toolH = 24;            // 工具条高度（= MENU_H，与菜单栏同高）
    int  m_toolHover = -1;        // 悬停的工具条按钮（-1 = 无）
    int  m_toolPressed = -1;      // 按下的工具条按钮（-1 = 无）
    Tab  m_tab = Files;

    FsNode m_root;
    bool   m_hasRoot = false;
    std::string m_rootDir;
    std::string m_error;          // 真实扫描失败原因（空 = 未打开文件时用 I18n）
    std::set<std::string> m_expanded;   // 已展开目录的路径

    std::vector<SideRow> m_rows;  // 当前可见行
    int m_hover = -1;
    int m_selected = -1;
    int m_tooltipRow = -1;        // 已设置 tooltip 的行（-1 = 无，避免重复 copy）

    // 截断结果缓存（见 elidedLabel 的注释）
    struct ElideEntry {
        int  avail  = -1;
        int  fontPx = 0;
        bool bold   = false;
        bool elided = false;
        int  textW  = -1;    // 截断文本的像素宽（只在需要画省略号时才量）
        std::string text;
    };
    std::unordered_map<std::string, ElideEntry> m_elideCache;

    // 大纲数据（与预览目录树同源，rebuildRows 时展开成 m_rows）
    std::vector<std::string> m_outlineLabels;
    std::vector<int>         m_outlineLevels;
    std::vector<std::string> m_outlineIds;
    std::vector<int>         m_outlineLines;   // 大纲行号（可为空数组）
    // 大纲折叠：被折叠标题的 key 集合。key = 从根到该标题的各级标题文本用
    // '\x01' 连接 —— 不用锚点 id（toc-N 按文档顺序生成，在上面插一个标题
    // 就整体位移），也不用行号（改一行就失效）。切文档时必须清空，否则
    // 上一个文件的折叠状态会串到下一个文件（见 setRoot）。
    std::set<std::string>   m_outlineCollapsed;
    std::string m_currentPath;    // 当前文档路径（文件树高亮）

    Fl_Scroll *m_scroll = nullptr;
    SideTree  *m_tree = nullptr;

    pick_file_cb_t    m_pickFile;
    pick_heading_cb_t m_pickHeading;
    std::function<void()> m_refresh;   // 底部「刷新」（由 MainWindow 注入）

    int  m_tabX[2] = {0, 0};      // 两个 Tab 的命中矩形（relayout 时算）
    int  m_tabW[2] = {0, 0};
};
