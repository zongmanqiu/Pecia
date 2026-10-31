// MainWindow_sidepanel.cpp - 左侧视图面板（文件树 / 大纲）的接线层
//
// 面板本身（绘制、扫描、命中）在 ui/SidePanel.cpp；这里只做四件事：
//   1. 开关与宽度策略（宽度可拖动，有最小/最大限制，窄窗口不挤死编辑区）
//   2. 数据同步：当前文档所在目录 → 文件树；预览标题 → 大纲
//   3. 与主布局的耦合：contentLeft() 让编辑器/预览整体右移
//   4. 分隔条拖动 → 像素宽度 → settings.ini（跨会话记住）
//
// 布局形态与「编辑器|预览」完全一致：
//     [SidePanel | Divider(4px) | 内容区(编辑器|预览)]
// Divider 复用 mdview 的 PreviewDivider（同一条 4px 竖线、同一种拖动光标），
// 只是 onDrag 的语义从"比例"换成"像素"——侧栏是 160–280px 的窄栏，按整窗
// 比例钳制在大窗口上会拖不动（详见 PreviewDivider::setDragRange 注释）。

#include "MainWindow.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Editor.H>

#include "Config.h"
#include "Document.h"
#include "I18n.h"
#include "SidePanel.h"
#include "Theme.h"
#include "editor/Editor.h"     // Tab::editor 是 Editor*（继承 Fl_Text_Editor，光有前向声明转不了）
#include "mdview/preprocess.h"   // extractOutlineHeadings：预览关闭时现提大纲
#include "mdview/preview_panel.h"

namespace {
// 面板宽度策略。velotype 用视口 15%/240–360，但它是单栏编辑器；Pecia 是
// 「编辑器 | 预览」双栏 + 侧栏第三栏，必须更克制：默认取窗口 14%，可拖范围
// 160–280px（低于 160 放不下"目录名 + 缩进"，高于 280 会明显吃掉正文）。
constexpr int kSideWMin   = 160;
constexpr int kSideWMax   = 280;
constexpr float kSideWRatio = 0.14f;
// 内容区（编辑器+预览）剩余宽度下限。低于这个值宁可把面板缩到这个边界，
// 也不让正文/预览被挤成一条。
constexpr int kMinContentW = 240;

// 行号校验交给 isHeadingSourceLine（mdview/preprocess，纯字符串逻辑，有单测）。
// 这里只是把缓冲区的某一行取出来喂给它。
bool isHeadingLine(Fl_Text_Buffer *buf, int line, const std::string &label)
{
    if (!buf || line < 1) return false;
    // FLTK 1.4 改成了蛇形命名：Fl_Text_Buffer::line_text（不是 lineText）
    char *raw = buf->line_text(buf->skip_lines(0, line - 1));
    if (!raw) return false;
    std::string line_(raw);
    free(raw);

    // 顺带取下一行：setext 标题（正文在下划线上面一行）要靠它确认。
    std::string next;
    char *rawNext = buf->line_text(buf->skip_lines(0, line));
    if (rawNext) { next = rawNext; free(rawNext); }

    return isHeadingSourceLine(line_, label, next);
}
} // namespace

int MainWindow::sidePanelWidth() const
{
    if (!m_sidePanelOpen) return 0;
    int wTarget = (m_sidePanelW > 0) ? m_sidePanelW
                                     : static_cast<int>(w() * kSideWRatio);
    wTarget = std::max(kSideWMin, std::min(kSideWMax, wTarget));
    // 极端窄窗口（<400px）下宁可关掉面板，也不要挤到只剩几十像素
    if (w() - wTarget < kMinContentW) return 0;
    return wTarget;
}

int MainWindow::contentLeft() const
{
    const int sideW = sidePanelWidth();
    return (sideW > 0) ? (sideW + kSideDividerW) : 0;
}

// 当前文档路径（空 = 未保存的新文档）
void MainWindow::toggleSidePanel()
{
    if (!m_sidePanel) {
        m_sidePanel = new SidePanel(0, 0, kSideWMin, 100);
        m_sidePanel->setPickFile([this](const std::string &path) {
            // 走现有 openFile：已开标签复用、未保存有确认、单/多标签策略一致
            openFile(path.c_str());
        });
        m_sidePanel->setPickHeading([this](const std::string &anchor) {
            // 预览滚动保持不变（无预览时 scrollToAnchor 自己早退）
            if (m_preview) m_preview->scrollToAnchor(anchor);
            jumpToHeadingLine(anchor);
        });
        // 底部工具条的「刷新」＝菜单 Markdown > Refresh（Ctrl+R）：同一个入口，
        // 文件页重扫磁盘、大纲页回这里重提标题（标题数据在窗口这边）。
        m_sidePanel->setRefresh([this]() { cbRefreshPreview(nullptr, this); });
        add(m_sidePanel);
        m_sidePanel->hide();
    }
    if (!m_sideDivider) {
        m_sideDivider = new PreviewDivider(0, 0, kSideDividerW, 100);
        m_sideDivider->setDividerColor(m_theme.colors().background2);
        // 侧栏是窄栏：比例范围放到 5%~60%，真正的钳制（160–280px、内容区
        // 下限）在下面的 onDrag 里按像素做。
        m_sideDivider->setDragRange(0.05f, 0.60f);
        m_sideDivider->hide();
        // add 顺序：divider 在面板之后加入（绘制层级无重叠，先后无关紧要，
        // 但事件命中上 divider 必须是独立 hit 区域，不能与面板重叠）。
        add(m_sideDivider);
        m_sideDivider->onDrag = [this](float ratio) {
            int px = static_cast<int>(ratio * w() + 0.5f);
            if (px < kSideWMin) px = kSideWMin;
            if (px > kSideWMax) px = kSideWMax;
            // 内容区保底：不够就缩到"刚好留够"的位置（仍不小于 kSideWMin）
            if (w() - px < kMinContentW) px = w() - kMinContentW;
            if (px < kSideWMin) px = kSideWMin;
            if (px == m_sidePanelW) return;   // 没变化不重排（拖动时每像素都回调）
            m_sidePanelW = px;
            if (m_cfg) m_cfg->setSidePanelWidth(px);
            layoutTabs();
        };
    }

    m_sidePanelOpen = !m_sidePanelOpen;
    if (m_sidePanelOpen) {
        // 首次打开：恢复上次拖动得到的宽度（0 = 用默认比例）
        if (m_sidePanelW == 0 && m_cfg) m_sidePanelW = m_cfg->getSidePanelWidth();
        m_sidePanel->setColors(m_theme.colors(),
                               m_cfg ? m_cfg->getUiFontSize() : 16);
        syncSidePanel();
        m_sidePanel->show();
    } else {
        m_sidePanel->hide();
    }
    layoutTabs();
    if (m_cfg) m_cfg->setSidePanel(m_sidePanelOpen);   // 跨会话记住开关

    // 菜单勾选状态（FL_MENU_TOGGLE 只翻自己的 flag，跨入口时要手动对齐）。
    // 用构造期缓存的英文路径指针，避免菜单翻译后 find_item("View/Side Panel")
    // 找不到。
    if (m_sidePanelItem) {
        if (m_sidePanelOpen) m_sidePanelItem->set();
        else                 m_sidePanelItem->clear();
        if (m_menu) m_menu->redraw();
    }
}

// 把「当前标签的文档路径 / 目录 / 预览标题」灌进面板。
void MainWindow::syncSidePanel()
{
    if (!m_sidePanel || !m_sidePanelOpen) return;

    std::string path;
    int i = activeTabIndex();
    if (i >= 0 && i < static_cast<int>(m_tabsList.size()) && m_tabsList[i].doc) {
        const char *p = m_tabsList[i].doc->filePath();
        if (p && *p) path = p;
    }

    std::string dir;
    if (!path.empty()) {
        const size_t sep = path.find_last_of("\\/");
        if (sep != std::string::npos && sep > 0) dir = path.substr(0, sep);
    }

    m_sidePanel->setCurrentPath(path);
    m_sidePanel->setRoot(dir);                       // 目录未变时不重扫（见 setRoot）
    // 大纲：预览开启时用渲染回调维护的那份（零额外成本）；预览关闭时现提
    // 一份，大纲页不开天窗。两条路径的锚点 id 规则一致（extractOutlineHeadings
    // 的注释），所以预览随后打开时看到的还是同一批锚点。
    if (m_previewActive) {
        // 预览这条路的标题来自渲染回调，里面没有源码行号 → 给空lines。
        // 用户点大纲时 jumpToHeadingLine 会按需补提一次。
        m_outlineLines.clear();
        m_sidePanel->setOutline(m_tocLabels, m_tocLevels, m_tocIds, m_outlineLines);
    } else {
        refreshOutline();
    }
}

// 大纲刷新（Markdown > Refresh / Ctrl+R / 预览关闭时的 syncSidePanel）：从
// 当前文档文本重新提取标题。预览开启时预览渲染回调也会灌同一份数据（锚点
// 规则一致），这里覆盖它，保证手动刷新立刻见效。
void MainWindow::refreshOutline()
{
    if (!m_sidePanel || !m_sidePanelOpen) return;
    std::vector<PreviewHeading> hs;
    extractOutlineHeadings(activeDocumentText(), hs);
    std::vector<std::string> labels;
    std::vector<int>         levels;
    std::vector<std::string> ids;
    labels.reserve(hs.size());
    levels.reserve(hs.size());
    ids.reserve(hs.size());
    m_outlineLines.clear();
    m_outlineLines.reserve(hs.size());
    for (const auto &h : hs) {
        labels.push_back(h.text);
        levels.push_back(h.level < 1 ? 1 : (h.level > 6 ? 6 : h.level));
        ids.push_back(h.id);
        m_outlineLines.push_back(h.line);
    }
    m_sidePanel->setOutline(labels, levels, ids, m_outlineLines);
}

// 大纲点击 → 把编辑器光标送到源码里对应的标题行。
//
// 行号是"上次提取那一刻"的快照：之后用户增删行就会漂移，所以落地前必须先
// 校验收下是不是那个标题——不对就在附近找，还找不到就重新提取一次。宁可多
// 花一次解析，也不能把光标丢到一个看起来像、其实是别的行上去。
//
// 预览自身的滚动交给 scrollToAnchor（老行为，原样保留），这里只负责源码侧。
void MainWindow::jumpToHeadingLine(const std::string &anchor)
{
    Tab *t = activeTab();
    if (!t || !t->editor || !m_sidePanel) return;
    Fl_Text_Editor *ed = t->editor;
    Fl_Text_Buffer *buf = ed->buffer();
    if (!buf) return;

    // "toc-N" → N（id 由 extractOutlineHeadings / md_to_html 按文档顺序生成）
    int idx = -1;
    {
        const size_t pos = anchor.rfind('-');
        long n = (anchor.compare(0, 4, "toc-") == 0 && pos != std::string::npos)
                     ? std::strtol(anchor.c_str() + 4, nullptr, 10) : -1;
        if (n >= 0 && n < (long)m_sidePanel->outlineLabels().size()) idx = (int)n;
    }
    if (idx < 0) return;

    for (int attempt = 0; attempt < 2; ++attempt) {
        // 行号缺失（预览提供的那份没有行号）或与当前条目数不符 → 重提
        if ((size_t)idx >= m_outlineLines.size() ||
            m_outlineLines.size() != m_sidePanel->outlineLabels().size()) {
            refreshOutline();
        }
        if ((size_t)idx >= m_outlineLines.size()) return;

        const int totalLines = buf->count_lines(0, buf->length()) + 1;
        const int aim = m_outlineLines[idx];
        const std::string &label = m_sidePanel->outlineLabels()[idx];

        int found = 0;
        if (aim >= 1 && aim <= totalLines && isHeadingLine(buf, aim, label)) {
            found = aim;
        } else {
            // 邻域搜索：往前/往后各找 40 行，取最近的一条匹配
            int best = 0, bestDist = 1 << 30;
            for (int d = 1; d <= 40; ++d) {
                const int cand[2] = {aim - d, aim + d};
                for (int k = 0; k < 2; ++k) {
                    if (cand[k] < 1 || cand[k] > totalLines) continue;
                    if (isHeadingLine(buf, cand[k], label) && d < bestDist) {
                        bestDist = d;
                        best = cand[k];
                    }
                }
            }
            found = best;
        }

        if (found >= 1) {
            // 与 Go To 那条路完全一致的落地方式：行首、滚动到位、给焦点
            const int pos = buf->skip_lines(0, found - 1);
            ed->insert_position(pos);
            ed->show_insert_position();
            ed->take_focus();
            return;
        }
        // 没命中 → 重提一次再试（第二遍已经是最新快照，仍失败就放弃）
        m_outlineLines.clear();
        refreshOutline();
    }
}

void MainWindow::cbToggleSidePanel(Fl_Widget * /*w*/, void *data)
{
    auto *win = static_cast<MainWindow *>(data);
    if (win) win->toggleSidePanel();
}
