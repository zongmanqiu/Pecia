// MainWindow_preview.cpp - Markdown preview integration.
// Two-pane layout: editor (m_tabs) | PreviewDivider | PreviewPanel.
// Ratio clamped to 20%-80% (AGENTS.md); auto-refresh is debounced by the
// configured interval (0 = manual only) and follows the active tab.
#include "MainWindow.h"
#include "core/Config.h"
#include "core/I18n.h"
#include "editor/Document.h"
#include "editor/Editor.h"
#include "mdview/mmdr_ffi.h"
#include "mdview/preview_panel.h"
#include "ui/HoverMenuBar.h"
#include "ui/Layout.h"   // MENU_H
#include "ui/ThemeWidgets.h"  // styleToolScrollbar / drawToolScrollbar（统一滚动条）
#include <FL/Fl.H>
#include <FL/Fl_Browser.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Tabs.H>
#include <FL/fl_draw.H>
#include <FL/fl_utf8.h>
#include <FL/platform.H>
#include <shellapi.h>

// --------------------------------------------------------------------------
// 预览临时目录管理（exe 同级 temp/<文档名>-<PID>/）：
//   - PID 后缀保证多进程同名文档天然隔离，无需锁
//   - 启动时清理：扫描 temp/*，进程已不存在的残留目录删除（崩溃兜底）
//   - 退出时清理：删除自己进程的预览目录（正常退出）
// --------------------------------------------------------------------------

static std::string previewTempRoot()
{
    wchar_t wbuf[MAX_PATH];
    if (!GetModuleFileNameW(NULL, wbuf, MAX_PATH)) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string dir(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, &dir[0], n, nullptr, nullptr);
    auto pos = dir.find_last_of("/\\");
    if (pos != std::string::npos) dir = dir.substr(0, pos);
    return dir + "\\temp";
}

// UTF-8 -> UTF-16（Win32 宽字符 API 用；路径源字符串均为本进程生成的 UTF-8）
static std::wstring widen(const std::string &utf8)
{
    if (utf8.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], n);
    while (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

// UTF-16 -> UTF-8（FindFirstFileW 等返回值转回项目统一编码）
static std::string narrow(const wchar_t *w)
{
    if (!w || !*w) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

static void removeDirTree(const std::string &dir)
{
    SHFILEOPSTRUCTW op = {};
    op.wFunc = FO_DELETE;
    std::wstring wdir = widen(dir);
    std::wstring from = wdir + L"\\\0";
    op.pFrom = from.c_str();
    op.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    SHFileOperationW(&op);
    RemoveDirectoryW(wdir.c_str());
}

void MainWindow::cleanupDeadPreviewDirs()
{
    std::string root = previewTempRoot();
    if (root.empty()) return;
    CreateDirectoryW(widen(root).c_str(), NULL);
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(widen(root + "\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        std::string name = narrow(fd.cFileName);
        if (name == "." || name == "..") continue;
        // 目录名尾部 -PID（非该格式的目录不碰，可能是用户自己的文件）
        auto dash = name.find_last_of('-');
        if (dash == std::string::npos || dash + 1 >= name.size()) continue;
        std::string pidStr = name.substr(dash + 1);
        bool digits = !pidStr.empty() &&
            std::all_of(pidStr.begin(), pidStr.end(), [](char c) { return c >= '0' && c <= '9'; });
        if (!digits) continue;
        DWORD pid = (DWORD)atol(pidStr.c_str());
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (h) { CloseHandle(h); continue; }   // 进程活着（含 PID 复用误判 → 少删，无害）
        removeDirTree(root + "\\" + name);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

void MainWindow::cleanupOwnPreviewDir()
{
    if (!m_previewDir.empty()) removeDirTree(m_previewDir);
}

// 退出时清理 7 天前的临时文件：遍历 exe 同级 temp/ 下的子目录和文件，
// 最后写入时间超过 7 天（预览缓存残留、ops 日志、崩溃 dmp/txt）则删除。
void MainWindow::cleanupOldTempDirs()
{
    if (m_cfg && !m_cfg->getCleanupTempOld()) return;
    std::string root = previewTempRoot();
    if (root.empty()) return;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULONGLONG now100ns = ((ULONGLONG)now.dwHighDateTime << 32) | now.dwLowDateTime;
    ULONGLONG sevenDays = (ULONGLONG)7 * 24 * 3600 * 10000000ULL;

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(widen(root + "\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        std::string name = narrow(fd.cFileName);
        if (name == "." || name == "..") continue;
        // 按最后写入时间清理（目录内文件变动会更新时间）：
        // 目录（预览缓存残留）整体删除；文件（ops 日志/崩溃 dmp/txt）直接删。
        ULONGLONG ft = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32)
                     | fd.ftLastWriteTime.dwLowDateTime;
        if (now100ns > ft + sevenDays) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                removeDirTree(root + "\\" + name);
            else
                DeleteFileW(widen(root + "\\" + name).c_str());
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

namespace {
constexpr int kPopupW = 340;  // 目录弹出层固定宽度：6 级缩进下正文仍可见足够多的中文字，
                              // 更长标题在 MenuList::draw 里省略号截断（不撑宽、不溢出）
} // namespace

// 菜单样式的目录列表控件：主题背景/文字/悬停、行 hover 高亮（原生 FLTK 菜单无滚动条/限高，
// 自绘控件在 Fl_Scroll 内即可两全：菜单观感 + 限高滚动）。点击行回调。
class MenuList : public Fl_Widget {
public:
    std::vector<std::string> items;
    std::vector<int> levels;          // 与 items 平行的标题层级（1..6），决定缩进/字重/主次色
    std::function<void(int)> onPick;   // 行号（0-based）
    int rowH = 22;

    MenuList(int x, int y, int w, int h, const ThemeColors &tc, int uiFontSize,
             const char *cap = nullptr)
        : Fl_Widget(x, y, w, h, cap), m_tc(tc), m_uiFontSize(uiFontSize) {}

    // 内容变化后重算自身高度（Fl_Scroll 据此出现滚动条）
    void itemsChanged() {
        size(w(), (int)items.size() * rowH);
        m_hover = -1;
        redraw();
    }

    // Update the UI font size (called when the user changes it mid-session).
    void setFontSize(int uiFontSize) {
        if (m_uiFontSize == uiFontSize) return;
        m_uiFontSize = uiFontSize;
        redraw();
    }

    void draw() override {
        Fl_Color bg = m_tc.background3;
        Fl_Color fg = m_tc.text1;
        Fl_Color fgSub = m_tc.text2;
        Fl_Color hover = m_tc.highlight1;
        // Whole area = the toc popup background (background3, the "transition"
        // surface also used by menu bar / find bar / line numbers).
        fl_color(bg);
        fl_rectf(x(), y(), w(), h());
        // Indent per level = 2 CJK-char widths, measured in the same UI face
        // used everywhere else (consistent with menus/dialogs). The popup font
        // is the shared UI font (FL_HELVETICA + ui font size).
        int kw = 0, kh = 0;
        fl_font(FL_HELVETICA, m_uiFontSize);
        fl_measure("\u6c49\u6c49", kw, kh);   // "汉汉" = 2 汉字宽
        if (kw < 4) kw = 4;                    // safety floor
        for (int i = 0; i < (int)items.size(); ++i) {
            int ry = y() + i * rowH;
            // Heading hierarchy WITHOUT adding theme colors: H1 stays bold
            // primary, H2 normal primary, H3+ steps down to secondary.
            int lvl = (i < (int)levels.size() && levels[i] >= 1) ? levels[i] : 1;
            const int indent = (lvl - 1) * kw;
            bool bold = (lvl == 1);
            bool sub = (lvl >= 3);
            fl_font(bold ? FL_HELVETICA_BOLD : FL_HELVETICA, m_uiFontSize);
            if (i == m_hover) {
                fl_color(hover);
                fl_rectf(x(), ry, w(), rowH);
                // keep the label legible over the hover fill
                fl_color(fl_contrast(sub ? fgSub : fg, hover));
            } else {
                fl_color(sub ? fgSub : fg);
            }
            // Right margin leaves room for the scrollbar; clip long headings
            // so the popup keeps a stable fixed width (see kPopupW).
            const int left  = x() + 8 + indent;
            int avail = x() + w() - 16 - left;
            if (avail < 8) avail = 8;   // extreme indent guard
            drawItemCaption(items[i], left, ry, avail, rowH);
        }
    }

    // Draw one caption, truncating with an ellipsis if it exceeds avail.
    static void drawItemCaption(const std::string &text, int x0, int y0,
                                int avail, int h0) {
        const char *beg = text.c_str();
        int tw = 0, th = 0;
        fl_measure(beg, tw, th);
        if (tw <= avail) {
            fl_draw(beg, x0, y0, avail, h0, FL_ALIGN_LEFT);
            return;
        }
        // Leave room for an ellipsis and walk UTF-8 chars forward.
        int ew = 0, eh = 0;
        fl_measure("\u2026", ew, eh);
        const int budget = avail - ew < 8 ? 8 : avail - ew;
        int acc = 0;
        int n = 0;                       // clipped byte length
        const char *p = beg;
        const char *end = beg + text.size();
        while (p < end) {
            int len = 1;
            fl_utf8decode(p, end, &len);   // length of this UTF-8 char
            std::string one(p, (size_t)len);
            int cw = 0, ch = 0;
            fl_measure(one.c_str(), cw, ch);
            if (acc + cw > budget) break;
            acc += cw;
            n += len;
            p += len;
        }
        std::string clipped(beg, n);
        fl_draw(clipped.c_str(), x0, y0, avail, h0, FL_ALIGN_LEFT);
        fl_draw("\u2026", x0 + acc, y0, avail - acc, h0, FL_ALIGN_LEFT);
    }

    int handle(int e) override {
        switch (e) {
        case FL_MOVE:
        case FL_ENTER: {
            int idx = (Fl::event_y() - y()) / rowH;
            if (idx != m_hover && idx >= 0 && idx < (int)items.size()) {
                m_hover = idx;
                redraw();
            }
            return 1;
        }
        case FL_LEAVE:
            if (m_hover >= 0) { m_hover = -1; redraw(); }
            return 1;
        case FL_PUSH: {
            int idx = (Fl::event_y() - y()) / rowH;
            if (idx >= 0 && idx < (int)items.size() && onPick) onPick(idx);
            return 1;
        }
        }
        return Fl_Widget::handle(e);
    }

private:
    const ThemeColors &m_tc;
    int m_uiFontSize;           // Pecia 统一界面字号（与菜单/对话框一致）
    int m_hover = -1;
};

// 目录弹出面板：Fl_Double_Window（双缓冲，避免鼠标移动时 hover 高亮重绘闪烁）
// + Fl_Scroll（限高+滚动条）+ MenuList（菜单样式列表）。
// 高度限制见 showTocPopup（最多 10 行）。背景用 background3，边框/滚动条与
// 其他 Pecia 面板一致。
// 注：不用 Fl_Menu_Window（= 单缓冲的 Fl_Single_Window），否则随鼠标移动的
// 高亮行整块重绘会闪屏；点击外部关闭由下方 handle(FL_PUSH) 自行实现。
class TocPopup : public Fl_Double_Window {
public:
    Fl_Scroll *scroll;
    MenuList *list;

    TocPopup(const ThemeColors &tc, int uiFontSize)
        : Fl_Double_Window(0, 0, kPopupW, 100), m_tc(tc), m_uiFontSize(uiFontSize) {
        // Force a true borderless popup (no title bar / native frame) so it
        // reads as a dropdown list attached to the preview toolbar, not as a
        // separate titled window.
        border(0);
        box(FL_NO_BOX);
        // Background uses background3 (same "transition" surface as the menu
        // bar / find bar / line numbers), so the panel reads as a dropdown
        // list separated from the preview toolbar behind it.
        color(tc.background3);
        scroll = new Fl_Scroll(0, 0, kPopupW, 100);
        scroll->type(Fl_Scroll::VERTICAL);   // 只允许垂直滚动，取消底部水平滚动条
        scroll->box(FL_FLAT_BOX);
        scroll->color(tc.background3);
        scroll->scrollbar_size(10);
        // Scrollbar uses the exact same shared styling + custom-drawn flat
        // look as every other Pecia scrollbar (ToolChrome, settings, etc.).
        styleToolScrollbar(&scroll->scrollbar, FL_VERT_SLIDER, tc);
        styleToolScrollbar(&scroll->hscrollbar, FL_HOR_SLIDER, tc);
        list = new MenuList(0, 0, kPopupW, 100, tc, uiFontSize);
        scroll->end();
    }

    // Safe teardown: never leave the global mouse grab held if the window is
    // destroyed while shown (e.g. preview teardown deletes us directly).
    ~TocPopup() {
        if (Fl::grab() == this) Fl::release();
    }

    // Draw children first, then overlay-draw the scrollbar in Pecia's flat
    // theme style (track + thumb) instead of FLTK's native slider groove.
    void draw() FL_OVERRIDE {
        Fl_Double_Window::draw();
        drawToolScrollbar(&scroll->scrollbar, false, m_tc);
        drawToolScrollbar(&scroll->hscrollbar, true, m_tc);
    }

    // Close the popup: release the global mouse grab (if any) then hide.
    // While shown we grab the mouse so a click anywhere else is routed here
    // and closes the popup instead of being lost to the window underneath.
    void close() {
        if (Fl::grab() == this) Fl::release();
        hide();
    }

    // 点击面板外部 → 关闭
    int handle(int e) override {
        if (e == FL_PUSH) {
            int mx = Fl::event_x_root() - x();
            int my = Fl::event_y_root() - y();
            if (mx < 0 || my < 0 || mx >= w() || my >= h()) { close(); return 1; }
        }
        return Fl_Double_Window::handle(e);
    }

private:
    const ThemeColors &m_tc;
    int m_uiFontSize;           // Pecia 统一界面字号（透传给 MenuList）
};

namespace {
constexpr int kDividerW = 4;        // 分隔条宽度
} // namespace

// MainWindow 成员实现：活动标签全文
std::string MainWindow::activeDocumentText() const
{
    if (m_tabsList.empty()) return {};
    int idx = activeTabIndex();
    if (idx < 0 || idx >= (int)m_tabsList.size()) return {};
    const Tab &t = m_tabsList[idx];
    if (!t.doc || !t.doc->buffer()) return {};
    const char *text = t.doc->buffer()->text();
    if (!text) return {};
    std::string s(text);
    free((void *)text);
    return s;
}

void MainWindow::togglePreview()
{
    if (m_previewActive) {
        // 关闭：先隐藏并转移焦点（避免 delete 时 FLTK 焦点/鼠标指针悬垂崩溃），
        // 面板延迟到下一事件循环销毁（届时无排队事件引用它）
        m_previewActive = false;
        stopAutoRefreshLoop();
        stopScrollSync();
        if (m_preview) {
            if (Fl::focus() == m_preview->view()) {
                Tab *t = activeTab();
                if (t && t->editor) Fl::focus(t->editor);
            }
            m_preview->view()->hide();
        }
        if (m_previewDivider) m_previewDivider->hide();
        layoutTabs();
        if (!m_previewDestroyPending) {
            m_previewDestroyPending = true;
            Fl::add_timeout(0.1, destroyPreviewCb, this);
        }
        // 更新菜单勾选（英文/翻译双语路径——启动时序 label 可能未翻译）
        if (m_menu) {
            std::string p = std::string(I18n::get("menu.markdown")) + "/" + I18n::get("menu.markdown.preview");
            const Fl_Menu_Item *item = m_menu->find_item("Markdown/Preview");
            if (!item) item = m_menu->find_item(p.c_str());
            if (item) ((Fl_Menu_Item *)item)->clear();
            m_menu->redraw();
        }
        return;
    }

    // 开启：首次创建面板
    // 若 0.1s 前的"关闭"销毁仍挂起，必须先取消，否则 destroyPreviewCb 到时
    // 会把刚重新激活的面板删掉，留下悬垂的 m_preview（use-after-free）。
    if (m_previewDestroyPending) {
        Fl::remove_timeout(destroyPreviewCb, this);
        m_previewDestroyPending = false;
    }
    if (!m_preview) {
        m_preview = new PreviewPanel();
        m_previewDivider = new PreviewDivider(0, 0, kDividerW, 100);
        // 分隔条颜色与编辑区边框统一（状态栏/行号列同色 bgChrome）
        m_previewDivider->setDividerColor(m_theme.colors().background2);
        m_previewDivider->hide();
        // 预览滚动条颜色与编辑区统一：轨道 background1，滑块 background2
        m_preview->view()->setScrollbarColors(
            m_theme.colors().background1,
            m_theme.colors().background2);
        m_previewDivider->onDrag = [this](float ratio) {
            m_previewRatio = ratio;
            layoutTabs();
        };
        // 拖放打开文件
        m_preview->view()->set_open_callback([this](const std::string &path) {
            openFile(path.c_str());
        });
        // Ctrl+滚轮缩放预览字体（全局 handler 亦处理，此处为焦点在预览时的兜底）
        m_preview->view()->set_zoom_callback([this](float delta) {
            if (m_preview) m_preview->setFontScale(m_preview->fontScale() * delta);
        });
        // 渲染完成 → 重建目录树
        m_preview->setHeadingsCallback([this](const std::vector<PreviewHeading> &hs) {
            rebuildPreviewToc(hs);
        });
        // 加入窗口（Fl_Group 层）
        m_preview->view()->hide();
        add(m_previewDivider);
        add(m_preview->view());

        // 预览顶部按钮栏：复用 HoverMenuBar（与主菜单同样式——同高/同按钮/同 hover），
        // 两个文字按钮：刷新、目录
        const Fl_Color chrome = m_theme.colors().background3;
        m_previewBarItems.clear();
        m_previewBarItems.push_back({ I18n::get("preview.refresh"), 0, cbPreviewRefreshBtn, this, 0 });
        m_previewBarItems.push_back({ I18n::get("preview.toc"), 0, cbPreviewTocBtn, this, 0 });
        m_previewBarItems.push_back({ nullptr });
        m_previewBar = new HoverMenuBar(0, 0, 100, MENU_H);
        m_previewBar->menu(m_previewBarItems.data());
        m_previewBar->box(FL_FLAT_BOX);
        m_previewBar->color(chrome);
        m_previewBar->textcolor(m_theme.colors().text1);
        m_previewBar->selection_color(m_theme.colors().highlight1);
        m_previewBar->check_color(m_theme.colors().highlight2);
        m_previewBar->setPressColor(m_theme.colors().highlight2);
        m_previewBar->textsize(m_cfg ? m_cfg->getUiFontSize() : 16);
        m_previewBar->hide();
        add(m_previewBar);
    }

    m_previewActive = true;
    m_preview->view()->show();
    m_previewDivider->show();
    m_lastRenderedMd.clear();   // 强制首次渲染
    layoutTabs();
    refreshPreview();   // 开启即渲染当前内容（含未保存的编辑）
    startAutoRefreshLoop();
    startScrollSync();

    if (m_menu) {
        std::string p = std::string(I18n::get("menu.markdown")) + "/" + I18n::get("menu.markdown.preview");
        const Fl_Menu_Item *item = m_menu->find_item("Markdown/Preview");
        if (!item) item = m_menu->find_item(p.c_str());
        if (item) ((Fl_Menu_Item *)item)->set();
        m_menu->redraw();
    }
}

void MainWindow::cbTogglePreview(Fl_Widget *w, void *data)
{
    auto *win = static_cast<MainWindow *>(data ? data : w->window());
    if (win) win->togglePreview();
}

// 关闭预览的延迟销毁：事件队列清空后再删除面板（避免 FLTK 悬垂指针崩溃）
void MainWindow::destroyPreviewCb(void *data)
{
    auto *win = static_cast<MainWindow *>(data);
    if (!win) return;
    win->m_previewDestroyPending = false;
    if (win->m_previewBar) { win->remove(win->m_previewBar); delete win->m_previewBar; win->m_previewBar = nullptr; }
    delete win->m_tocPopup;   // 未加入窗口，需手动释放
    win->m_tocPopup = nullptr;
    win->m_previewBarItems.clear();
    win->m_tocLabels.clear();
    win->m_tocLevels.clear();
    win->m_tocIds.clear();
    if (win->m_previewDivider) { win->remove(win->m_previewDivider); delete win->m_previewDivider; win->m_previewDivider = nullptr; }
    if (win->m_preview) { win->remove(win->m_preview->view()); delete win->m_preview; win->m_preview = nullptr; }
    // 释放 mmdr FFI 的全局字体索引缓存 + 内存精简
    mmdr_fontdb_clear();
    win->scheduleTrimSoon();
}

void MainWindow::cbRefreshPreview(Fl_Widget *w, void *data)
{
    auto *win = static_cast<MainWindow *>(data ? data : w->window());
    if (win) win->refreshPreview();
}

void MainWindow::setAutoRefresh(int ms)
{
    // 只接受合法档位（防垃圾值写坏配置）
    switch (ms) {
    case 0: case 1000: case 5000: case 10000: case 30000:
        break;
    default:
        return;
    }
    m_autoRefreshMs = ms;
    if (m_cfg) m_cfg->setPreviewAutoRefreshMs(ms);
    // 档位变化：重启循环定时器
    stopAutoRefreshLoop();
    if (m_previewActive) startAutoRefreshLoop();
}

void MainWindow::syncAutoRefreshMenu()
{
    if (!m_menu) return;
    // 路径用翻译后的 label 拼接（1s/5s 等数字档位不译）；
    // 启动时 label 可能尚未翻译（applyLanguageToMenu 在后），英文路径也试
    std::string label_en, label_i18n;
    if (m_autoRefreshMs == 0) {
        label_en = "Off";
        label_i18n = I18n::get("menu.markdown.off");
    } else {
        label_en = label_i18n = std::to_string(m_autoRefreshMs / 1000) + "s";
    }
    std::string en  = std::string("Markdown/Auto Refresh/") + label_en;
    std::string i18 = std::string(I18n::get("menu.markdown")) + "/" + I18n::get("menu.markdown.autorefresh") + "/" + label_i18n;
    Fl_Menu_Item *item = (Fl_Menu_Item *)m_menu->find_item(en.c_str());
    if (!item) item = (Fl_Menu_Item *)m_menu->find_item(i18.c_str());
    if (item) item->setonly();
    m_menu->redraw();
}

void MainWindow::cbSetAutoRefresh(Fl_Widget *w, void *data)
{
    auto *win = static_cast<MainWindow *>(data ? data : w->window());
    if (!win || !win->m_menu) return;
    // 档位值无法从 user_data 取：copy(g_menu, this) 会把所有有回调菜单项的
    // user_data 覆盖为窗口指针，且 FLTK 菜单回调的 data 是菜单 widget 的
    // user_data（nullptr）而非菜单项 user_data。因此直接从 mvalue()（最后
    // 选中的菜单项）的 label 解析：数字档位（1s/5s/10s/30s）不参与翻译。
    const Fl_Menu_Item *sel = win->m_menu->mvalue();
    if (!sel || !sel->text) return;
    int ms = -1;
    const char *t = sel->text;
    if (strcmp(t, "Off") == 0 ||
        strcmp(t, I18n::get("menu.markdown.off")) == 0) {
        ms = 0;
    } else {
        int n = 0;
        if (sscanf(t, "%ds", &n) == 1 && n > 0) ms = n * 1000;
    }
    if (ms < 0) return;
    win->setAutoRefresh(ms);
}

// --------------------------------------------------------------------------
// 固定间隔循环刷新：每 m_autoRefreshMs 无条件刷新一次，与是否输入内容、
// 是否正在输入、是否停顿输入完全无关。预览面板隐藏 / 窗口最小化时跳过
// （不可见时刷新无意义，仅省资源）。
// --------------------------------------------------------------------------
void MainWindow::startAutoRefreshLoop()
{
    if (m_autoRefreshArmed || m_autoRefreshMs <= 0) return;
    m_autoRefreshArmed = true;
    Fl::add_timeout(m_autoRefreshMs / 1000.0, autoRefreshLoopCb, this);
}

void MainWindow::stopAutoRefreshLoop()
{
    if (!m_autoRefreshArmed) return;
    m_autoRefreshArmed = false;
    Fl::remove_timeout(autoRefreshLoopCb, this);
}

void MainWindow::autoRefreshLoopCb(void *data)
{
    auto *win = static_cast<MainWindow *>(data);
    if (!win || !win->m_autoRefreshArmed) return;

    // 不可见时跳过本次刷新（仅续期）
    bool minimized = false;
#if defined(_WIN32)
    HWND hwnd = fl_xid(win);
    minimized = hwnd && IsIconic(hwnd);
#endif
    if (win->m_previewActive && win->m_preview &&
        win->m_preview->view()->visible() && !minimized) {
        win->refreshPreview();
    }

    // 固定间隔续期：与输入无关，永远按档位循环
    Fl::repeat_timeout(win->m_autoRefreshMs / 1000.0, autoRefreshLoopCb, win);
}

void MainWindow::refreshPreview()
{
    if (!m_preview || !m_previewActive) return;
    // Large-document guard: refreshPreview copies the WHOLE document text
    // (activeDocumentText below) and string-compares it against the last
    // render - with a 50 MB document that is a 50 MB alloc+copy on every
    // tab switch / refresh tick before any parsing even starts. Skip the
    // preview entirely for such files (same threshold as auto word wrap).
    {
        Tab *t0 = activeTab();
        if (t0 && t0->doc && t0->doc->buffer() &&
            t0->doc->buffer()->length() > kWrapLimitBytes) {
            m_preview->clear();
            m_lastRenderedMd.clear();   // also frees a stale 50 MB copy
            return;
        }
    }
    std::string md = activeDocumentText();
    // Skip when the document text is unchanged (fixes the auto-refresh timer
    // re-rendering the full document every tick even when nothing changed).
    // togglePreview() clears m_lastRenderedMd to force the first/initial render.
    if (!m_lastRenderedMd.empty() && md == m_lastRenderedMd) return;
    m_lastRenderedMd = md;   // 记忆（循环定时器据此跳过未变化的内容）
    // 设置文档基础目录（解析相对图片路径；未命名标签则为空）
    std::string base;
    Tab *t = activeTab();
    {
        if (t && t->doc && t->doc->filePath() && t->doc->filePath()[0]) {
            base = t->doc->filePath();
            auto pos = base.find_last_of("/\\");
            if (pos != std::string::npos) base = base.substr(0, pos + 1);
        }
        m_preview->setBaseUrl(base);
    }
    if (md.empty()) {
        m_preview->clear();
        return;
    }
    // build_dir：exe 同级 temp/<文档名>-<PID>/（多进程同名文档天然隔离；
    // 公式/meimaid/本地图片拷贝/HTML 的统一输出）
    std::string buildDir;
    wchar_t exePathW[MAX_PATH];
    if (GetModuleFileNameW(NULL, exePathW, MAX_PATH)) {
        int n = WideCharToMultiByte(CP_UTF8, 0, exePathW, -1, nullptr, 0, nullptr, nullptr);
        if (n > 0) {
            buildDir.assign(n - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, exePathW, -1, &buildDir[0], n, nullptr, nullptr);
            auto pos = buildDir.find_last_of("/\\");
            if (pos != std::string::npos) buildDir = buildDir.substr(0, pos);
            buildDir += "\\temp";
            // 文档名（未命名用 untitled），作为预览子目录前缀
            std::string docName = "untitled";
            if (t && t->doc && t->doc->filePath() && t->doc->filePath()[0]) {
                std::string fp = t->doc->filePath();
                auto slash = fp.find_last_of("/\\");
                if (slash != std::string::npos) fp = fp.substr(slash + 1);
                auto dot = fp.find_last_of('.');
                if (dot != std::string::npos) fp = fp.substr(0, dot);
                if (!fp.empty()) docName = fp;
            }
            buildDir += "\\" + docName + "-" + std::to_string(GetCurrentProcessId());
        }
    }
    m_previewDir = buildDir;
    // 创建目录层级：temp/ 与 temp/<文档名>-<PID>/（CreateDirectory 只建最末层）
    if (!buildDir.empty()) {
        std::string tempRoot = buildDir.substr(0, buildDir.find_last_of("/\\"));
        CreateDirectoryW(widen(tempRoot).c_str(), NULL);
        CreateDirectoryW(widen(buildDir).c_str(), NULL);
    }
    // 异步渲染：md_to_html 在后台线程（ratex/ mmdr 不阻塞 UI）
    // 注意：不再清图片缓存——公式/mermaid PNG 按内容哈希命名，内容变了
    // 文件名自然变，未变的图片命中容器缓存（load_image_file 的 m_images），
    // 避免每次刷新全量重新解码。
    m_preview->renderAsync(md, buildDir, base);
}

void MainWindow::scrollPreview(int dy)
{
    if (m_preview && m_preview->hasContent())
        m_preview->view()->scroll_to(m_preview->view()->scroll_y() + dy);
}

// --------------------------------------------------------------------------
// 滚动同步：编辑区滚动 → 预览按比例滚动（150ms 轮询编辑滚动条位置）
// 开关：Markdown > Scroll Sync（默认关，Config 持久化）
// --------------------------------------------------------------------------
void MainWindow::cbToggleScrollSync(Fl_Widget *w, void *data)
{
    auto *win = static_cast<MainWindow *>(data ? data : w->window());
    if (!win) return;
    win->m_scrollSyncEnabled = !win->m_scrollSyncEnabled;
    if (win->m_cfg) win->m_cfg->setPreviewScrollSync(win->m_scrollSyncEnabled);
    if (win->m_scrollSyncEnabled) {
        win->startScrollSync();
    } else {
        win->stopScrollSync();
    }
    // 菜单勾选状态由 FL_MENU_TOGGLE 自动维护
}

void MainWindow::startScrollSync()
{
    if (!m_scrollSyncEnabled || m_scrollSyncArmed) return;
    m_scrollSyncArmed = true;
    m_lastEditorScroll = -1;
    Fl::add_timeout(0.15, scrollSyncCb, this);
}

void MainWindow::stopScrollSync()
{
    if (!m_scrollSyncArmed) return;
    m_scrollSyncArmed = false;
    Fl::remove_timeout(scrollSyncCb, this);
}

void MainWindow::scrollSyncCb(void *data)
{
    auto *win = static_cast<MainWindow *>(data);
    if (!win || !win->m_scrollSyncArmed) return;

    // 编辑区滚动比例
    Tab *tab = win->activeTab();
    if (tab && tab->editor) {
        float ratio = tab->editor->verticalScrollRatio();
        int key = (int)(ratio * 10000.0f);
        if (key != win->m_lastEditorScroll) {
            win->m_lastEditorScroll = key;
            if (win->m_preview && win->m_preview->hasContent()) {
                int target = (int)(ratio * win->m_preview->view()->content_height());
                win->m_preview->view()->scroll_to(target);
            }
        }
    }
    if (win->m_scrollSyncArmed) {
        Fl::add_timeout(0.15, scrollSyncCb, win);
    }
}

void MainWindow::openPreviewInBrowser()
{
    if (!m_preview || !m_previewActive || !m_preview->hasContent()) return;
    // 直接打开预览目录里的 index.html（渲染时由后台线程写出）：
    // 目录内包含本地图片拷贝/公式/meimaid，浏览器可完整显示。
    std::string path = m_previewDir + "\\index.html";
    if (GetFileAttributesW(widen(path).c_str()) == INVALID_FILE_ATTRIBUTES) {
        // 目录未生成（如预览打开后未渲染过）：退回写临时文件
        wchar_t tmpPathW[MAX_PATH];
        if (!GetTempPathW(MAX_PATH, tmpPathW)) return;
        path = narrow(tmpPathW) + "pecia_preview.html";
        FILE *f = fl_fopen(path.c_str(), "wb");
        if (!f) return;
        const std::string &html = m_preview->view()->current_html();
        fwrite(html.data(), 1, html.size(), f);
        fclose(f);
    }
    ShellExecuteW(NULL, L"open", widen(path).c_str(), NULL, NULL, SW_SHOWNORMAL);
}

void MainWindow::cbOpenPreviewInBrowser(Fl_Widget *w, void *data)
{
    auto *win = static_cast<MainWindow *>(data ? data : w->window());
    if (win) win->openPreviewInBrowser();
}

// 在 layoutTabs() 末尾调用：按预览模式排布两栏（编辑区 | 分隔条 | 预览）。
// 非预览模式时保持原行为。预览列 = 顶部工具条(28px) + 预览内容
void MainWindow::layoutPreviewPanes(int contentTop, int tabsH)
{
    if (m_previewActive && m_preview && m_previewDivider) {
        int editorW = (int)(w() * m_previewRatio);
        if (editorW < (int)(w() * 0.20f)) editorW = (int)(w() * 0.20f);
        if (editorW > (int)(w() * 0.80f)) editorW = (int)(w() * 0.80f);
        int divX = editorW;
        int previewW = w() - divX - kDividerW;
        if (previewW < 1) previewW = 1;

        const int barH = MENU_H;   // 与主菜单栏同高
        const int px = divX + kDividerW;

        m_tabs->resize(0, contentTop, editorW, tabsH);
        m_previewDivider->resize(divX, contentTop, kDividerW, tabsH);
        if (m_previewBar) {
            m_previewBar->resize(px, contentTop, previewW, barH);
            m_previewBar->show();
            // 拖动分隔条改预览宽度后，强制完整重绘按钮栏区域（防残影）
            m_previewBar->damage(FL_DAMAGE_ALL);
        }
        m_preview->view()->resize(px, contentTop + barH, previewW, tabsH - barH);
        m_preview->view()->show();
        m_previewDivider->show();
        m_previewDivider->redraw();
    } else {
        m_tabs->resize(0, contentTop, w(), tabsH);
        if (m_previewDivider) m_previewDivider->hide();
        if (m_preview) m_preview->view()->hide();
    }
}

// 工具条：手动刷新（大文档关闭自动刷新时使用）
void MainWindow::cbPreviewRefreshBtn(Fl_Widget * /*w*/, void *data)
{
    auto *win = static_cast<MainWindow *>(data);
    if (win) win->refreshPreview();
}

// 工具条：目录按钮 → 弹出目录面板（按钮下方，限高+滚动条）
void MainWindow::cbPreviewTocBtn(Fl_Widget * /*w*/, void *data)
{
    auto *win = static_cast<MainWindow *>(data);
    if (win) win->showTocPopup();
}

// 弹出目录面板：填充标题行、限制高度（最多 10 行，超出出现滚动条）、
// 定位在目录按钮下方（root 坐标），点击行跳转、点击外部关闭。
void MainWindow::showTocPopup()
{
    if (!m_preview || !m_previewBar) return;
    // The popup shares the same UI font size as every other Pecia dialog /
    // menu bar (ui_font_size), not the code editor's face.
    const int uiFontSize = m_cfg ? m_cfg->getUiFontSize() : 16;
    if (!m_tocPopup) {
        m_tocPopup = new TocPopup(m_theme.colors(), uiFontSize);
        m_tocPopupFontSize = uiFontSize;
    } else if (uiFontSize != m_tocPopupFontSize) {  // user changed it mid-session
        m_tocPopupFontSize = uiFontSize;
        m_tocPopup->list->setFontSize(uiFontSize);
    }
    TocPopup *p = m_tocPopup;

    p->list->items.clear();
    if (m_tocLabels.empty()) {
        p->list->items.push_back(I18n::get("preview.toc.empty"));
        p->list->levels.clear();        // 单个占位行，draw 按默认层级 1（正常字重/主色）渲染
    } else {
        p->list->items = m_tocLabels;
        p->list->levels = m_tocLevels;
    }
    p->list->onPick = [this](int row) {
        if (row >= 0 && row < (int)m_tocIds.size())
            m_preview->scrollToAnchor(m_tocIds[row]);
        if (m_tocPopup) m_tocPopup->close();
    };

    // 宽固定（长标题在 MenuList::draw 里省略号截断，不撑宽）；右侧留滚动条空间。
    // 高 = min(项数,10) * 行高 + 边框。
    const int n = (int)p->list->items.size();
    const int shown = n <= 0 ? 1 : (n > 10 ? 10 : n);
    const int W = kPopupW;
    const int H = shown * p->list->rowH + 2;
    p->resize(0, 0, W, H);
    p->scroll->resize(0, 0, W, H);
    p->list->resize(0, 0, W, H);
    p->list->itemsChanged();

    // 定位在目录按钮（第二个菜单项）下方
    int barX = m_previewBar->window()->x() + m_previewBar->x();
    int barY = m_previewBar->window()->y() + m_previewBar->y();
    int w1 = 0, h1 = 0;
    fl_font(m_previewBar->textfont(), m_previewBar->textsize());
    fl_measure(I18n::get("preview.refresh"), w1, h1);
    p->position(barX + 3 + w1 + 16, barY + m_previewBar->h());

    p->show();
    // Grab the mouse so clicks anywhere outside are routed to us and close the
    // popup (rather than hitting the window below). Released in TocPopup::close().
    if (Fl::grab() != p) Fl::grab(p);
}

// 重建目录数据（渲染完成回调，主线程）：标题正文与层级分别存入 m_tocLabels /
// m_tocLevels，锚点 id 存 m_tocIds（showTocPopup 时按行号对应）。
// 不再往文本里塞空格缩进——层级改用像素左移（MenuList::draw 的 kIndentPx），
// 避免长缩进把弹窗宽度撑大。
void MainWindow::rebuildPreviewToc(const std::vector<PreviewHeading> &headings)
{
    m_tocLabels.clear();
    m_tocLevels.clear();
    m_tocIds.clear();
    m_tocLabels.reserve(headings.size());
    m_tocLevels.reserve(headings.size());
    m_tocIds.reserve(headings.size());
    for (const auto &h : headings) {
        int lv = h.level < 1 ? 1 : (h.level > 6 ? 6 : h.level);
        m_tocLabels.push_back(h.text);
        m_tocLevels.push_back(lv);
        m_tocIds.push_back(h.id);
    }
}
