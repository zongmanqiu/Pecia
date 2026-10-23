// SidePanel.h - 左侧视图面板（文件树 / 文档大纲，顶部两个 Tab 切换）
//
// 设计参照 velotype 的 workspace drawer：
//   · 面板挂在内容区最左，纵跨编辑器与预览（不是塞进预览区，预览本来就窄）
//   · 顶部一条 Tab 条，"Files / Outline" 二选一，省掉两个独立面板
//   · 手动刷新统一走菜单/快捷键（Markdown > Refresh，Ctrl+R）：文件页重扫
//     磁盘，大纲页重新提取标题；面板内不再保留底部刷新条
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
#include "ui/WindowFrame.h"   // windowChrome::BandColourSource

// 面板中的一行（文件树节点或大纲标题）。树是"扫描树 + 展开集合"按需展开成
// 这个扁平列表来画的，行本身就是命中区域，不需要第二种坐标模型。
struct SideRow {
    std::string label;    // 显示文本
    std::string path;     // 文件/目录完整路径（大纲行为空）
    std::string anchor;   // 大纲锚点 id（文件行为空）
    int  depth = 0;       // 缩进层级（0 = 顶层）
    int  level = 1;       // 标题级别 1..6（文件行恒 1）
    bool isDir = false;   // 目录行（可展开/折叠）
    bool isHeading = false;
    bool expanded = false;
    bool hasChildren = false;
};

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

    // 强制重扫磁盘（菜单/快捷键刷新时，文件页用）。setRoot 对"目录没变"
    // 会短路，这里清掉缓存目录骗过短路；已展开的目录状态保留。
    void rescan();

    // 运行时切换语言后调用：重建行（"未打开文件 / 空文件夹 / 无标题"
    // 这些合成行在 rebuildRows 里实时查 I18n，不缓存旧语言文本）。
    void refreshLanguage();

    void setPickFile(pick_file_cb_t cb) { m_pickFile = std::move(cb); }
    void setPickHeading(pick_heading_cb_t cb) { m_pickHeading = std::move(cb); }

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
    void rowClicked(int row);
    // 悬停行变化时更新 tooltip：文本被省略号截断的行显示全名。
    void updateRowTooltip(int row);

private:
    friend class SideTree;   // 内容区的绘制/命中要读行数据与主题

    // 磁盘扫描结果（只在 setRoot/rescan 时重建）。
    struct FsNode {
        std::string name;
        std::string path;
        bool isDir = false;
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
    int  contentHeight() const;
    void syncTreeGeometry();

    ThemeColors m_tc;
    int  m_uiFontSize = 16;
    int  m_rowH = 26;
    int  m_tabH = 30;
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
    std::string m_currentPath;    // 当前文档路径（文件树高亮）

    Fl_Scroll *m_scroll = nullptr;
    SideTree  *m_tree = nullptr;

    pick_file_cb_t    m_pickFile;
    pick_heading_cb_t m_pickHeading;

    int  m_tabX[2] = {0, 0};      // 两个 Tab 的命中矩形（relayout 时算）
    int  m_tabW[2] = {0, 0};
};
