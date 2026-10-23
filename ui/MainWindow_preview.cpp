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
#include "mdview/image_export.h"
#include "mdview/preview_panel.h"
#include "ui/Layout.h"   // MENU_H（仅注释引用；按钮栏删除后本文件不再用它）
#include "ui/SidePanel.h"     // 左侧面板（layoutPreviewPanes 需要完整类型）
#include <FL/Fl.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_Tabs.H>
#include <FL/fl_draw.H>
#include <FL/fl_utf8.h>
#include <FL/filename.h>   // fl_filename_name / fl_filename_ext
#include <FL/platform.H>
#include <shellapi.h>
#include <shobjidl.h>   // IFileSaveDialog（另存为 HTML 的路径选择）

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
        // 远程图全部落盘后复用已有的工作集回收（2s 去抖；大文档由
        // trimSoonCb 内部跳过）。只回收 working set，不改变缓存/文件/布局。
        m_preview->setRemoteSettledCallback([this]() { scheduleTrimSoon(); });
        m_previewDivider = new PreviewDivider(0, 0, kDividerW, 100);
        // 分隔条颜色与编辑区边框统一（状态栏/行号列同色 bgChrome）
        m_previewDivider->setDividerColor(m_theme.colors().background2);
        m_previewDivider->hide();
        // 预览滚动条颜色与编辑区统一：轨道 background1，滑块 background2
        m_preview->view()->setScrollbarColors(
            m_theme.colors().background1,
            m_theme.colors().background2);
        m_previewDivider->onDrag = [this](float ratio) {
            // 分隔条按整窗宽度算比例；有侧栏时换算成"内容区（侧栏右侧）"的比例
            const int cl = contentLeft();
            if (cl > 0) {
                const float cw = (float)(w() - cl);
                if (cw > 1.0f) ratio = (ratio * (float)w() - (float)cl) / cw;
            }
            if (ratio < 0.20f) ratio = 0.20f;
            if (ratio > 0.80f) ratio = 0.80f;
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

        // 预览顶部按钮栏已删除（2026-10-20 用户反馈：一行按钮太占地，而
        // 三个功能全都有更顺手的入口）。功能去向：
        //   · 刷新  → Ctrl+R（Markdown/Refresh，同时刷新左侧面板当前页）
        //   · 大纲  → Markdown/Outline（打开左侧面板并切到大纲页）
        //   · 面板  → 视图/侧面板 或 Ctrl+B（原本就与工具栏重复）
        // 预览视图因此从内容区顶缘开始，少占一行 MENU_H。
    }

    m_previewActive = true;
    m_preview->view()->show();
    m_previewDivider->show();
    m_previewGate.invalidate();   // 强制首次渲染
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
    if (!win) return;
    // "Refresh" 是预览与左侧面板共用的唯一手动入口（菜单 Markdown > Refresh /
    // Ctrl+R）：面板文件页重扫磁盘，大纲页重新提取标题；预览关闭时
    // refreshPreview() 自己会早退，所以只开面板时快捷键也有效。
    if (win->m_sidePanel && win->m_sidePanelOpen) {
        if (win->m_sidePanel->tab() == SidePanel::Files)
            win->m_sidePanel->rescan();
        else
            win->refreshOutline();
    }
    win->refreshPreview(true);   // 手动刷新：无条件重渲染
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

void MainWindow::refreshPreview(bool force)
{
    if (!m_preview || !m_previewActive) return;
    // 内容变化判据：O(1) 的 (文档, 版本号)。切标签页 → 指针不同 → 必渲染；
    // 同一文档内编辑 → contentRev 变 → 必渲染；都没变 → 跳过，
    // 省掉一次全量取文 + 解析 + litehtml 布局。手动刷新走 force。
    // 旧实现留着一份全文 m_lastRenderedMd 做逐字节比较，每次 tick 要付
    // 三次全量分配+拷贝；这里曾因此对 >4 MB 的文档直接跳过预览（静默空白）。
    // 现在任何能打开的文档都走真实渲染，慢是用户自己的选择。
    // force 由 shouldRender() 自行消费（内部置 m_force），无需先 invalidate。
    Tab *t = activeTab();
    const Document *doc = (t && t->doc) ? t->doc : nullptr;
    const unsigned long long rev = doc ? doc->contentRev() : 0;
    if (!m_previewGate.shouldRender(doc, rev, force))
        return;
    std::string md = activeDocumentText();
    // 设置文档基础目录（解析相对图片路径；未命名标签则为空）
    std::string base;
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
    // 预览跟随编辑器字体：把 editor_font 交给预览（写入注入 CSS 首位候选 +
    // 容器兜底 face）。每次渲染前同步，View > Font 切换后由 cbSelectFont 触发刷新。
    {
        char previewFont[64] = {0};
        if (m_cfg) m_cfg->getEditorFont(previewFont, sizeof(previewFont));
        m_preview->setBaseFont(previewFont);
    }
    // force 只在手动刷新（Ctrl+R / Refresh）时为 true：绕过远程图缓存并
    // 重新下载；定时刷新和打开文件仍复用缓存。
    m_preview->renderAsync(md, buildDir, base, force);
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

// ---------------------------------------------------------------------------
// 自包含页面输出 —— "导出 HTML" 与 "在浏览器中打开" 的【唯一】实现。
//
// 为什么必须只有一份：这两条功能以前各写一套（一个重新渲染并落盘、一个
// 读回 index.html 做局部改写），结果就是两边对"本地图要不要拷、远程图要不要
// 下载、文件叫什么"的判断逐渐分叉 —— 用户看到的正是"导出正常，但直接打开
// 那个 index.html 不行"。凡是要落到磁盘上给人看的 HTML，都必须走这里。
//
// outDir 已存在且可写；本函数在其中产出：
//   index.html        —— 页面本体
//   <原文件名>        —— 文档里的本地相对图片（保持原名与相对结构）
//   <URL 原文件名>    —— 远程图片，命中预览缓存或当场下载
//   formulas/ mermaid/—— 公式与图表位图
// 返回 false 表示连 index.html 都没写成；*remoteRemaining > 0 表示页面里仍有
// 远程地址（离线打开会缺图），调用方应如实告知用户而不是假装成功。
static bool writeStandaloneHtml(const std::string& md,
                                const std::string& outDir,
                                const std::string& srcPath,
                                const std::string& baseFont,
                                int* remoteRemaining)
{
    if (remoteRemaining) *remoteRemaining = 0;

    // 相对图片以源文档目录为解析基准。md_to_html 的 doc_dir 形参接受
    // "文件全路径"或"目录"两种形态（第 7 步会自行判别），这里给文件全路径。
    std::string docBase = srcPath;

    std::string html;
    try {
        html = md_to_html(md, outDir, docBase, nullptr, baseFont);
    } catch (...) {
        html.clear();
    }
    if (html.empty()) return false;

    // 远程图片：命中预览缓存就复用，缺的当场下载（WinHTTP + 重试 + 2xx 校验）。
    // 预览缓存是容器异步写的，渲染完成时图往往还没到，所以这里必须真下载。
    localize_remote_images(html, outDir, default_remote_fetch());

    if (remoteRemaining) *remoteRemaining = count_remote_image_srcs(html);

    const std::string indexPath = outDir + "\\index.html";
    FILE* f = fl_fopen(indexPath.c_str(), "wb");
    if (!f) return false;
    std::fwrite(html.data(), 1, html.size(), f);
    std::fclose(f);
    return true;
}

// 基准【字体名】（如 "Consolas"），不是字号。md_to_html 的 base_font 形参只
// 被写进 CSS font-family 的首位候选，不参与 font-size。原先这里传的是
// "%dpx" 字号串，于是导出页里出现 font-family: '15px' 这种无效声明 ——
// 浏览器忽略它，而字号其实也没跟上（body 字号由 CSS 固定），等于两个目的
// 都没达成。预览侧的字号缩放走 MyContainer::m_font_scale，与本参数无关。
static std::string editorFontName(const Config* cfg)
{
    char fb[64] = {0};
    if (cfg) cfg->getEditorFont(fb, sizeof(fb));
    return fb;
}

void MainWindow::openPreviewInBrowser()
{
    if (!m_preview || !m_previewActive || !m_preview->hasContent()) return;

    Tab* tab = activeTab();
    const std::string md = activeDocumentText();
    if (md.empty()) return;
    const std::string srcPath = (tab && tab->doc) ? tab->doc->filePath() : "";

    // 与导出走同一个函数、同一套判定：预览目录里的 index.html 从此就是
    // 完整自包含页面，用户事后直接双击该文件也能看到全部图片，不依赖这次
    // 是否点过菜单。
    std::string outDir = m_previewDir;
    if (outDir.empty()) {
        outDir = previewTempRoot();
        if (outDir.empty()) return;
        outDir += "\\preview";
        CreateDirectoryW(widen(outDir).c_str(), NULL);
    }

    int remoteLeft = 0;
    if (!writeStandaloneHtml(md, outDir, srcPath, editorFontName(m_cfg),
                             &remoteLeft))
        return;

    const std::string path = outDir + "\\index.html";
    ShellExecuteW(NULL, L"open", widen(path).c_str(), NULL, NULL, SW_SHOWNORMAL);

    if (remoteLeft > 0) {
        char msg[512];
        std::snprintf(msg, sizeof(msg), "%s\n\n%d",
                      I18n::get("export.partial"), remoteLeft);
        MessageBoxW(NULL, widen(msg).c_str(), L"Pecia", MB_OK | MB_ICONWARNING);
    }
}

void MainWindow::cbOpenPreviewInBrowser(Fl_Widget *w, void *data)
{
    auto *win = static_cast<MainWindow *>(data ? data : w->window());
    if (win) win->openPreviewInBrowser();
}

// 另存为 HTML：与"在浏览器打开"共用同一条渲染链路，区别只在落点。
//
// 为什么是"选文件夹、导出一个文件夹"而不是"选一个 .html"：
//   自包含的 markdown 导出必然产生一串旁证文件（图片、KaTeX 公式 SVG、
//   Mermaid 图）。让用户只挑一个 .html，浏览器打开后照样看不到图 —— 页
//   面的相对引用在孤立文件里全部断链。用户真正要的是"一个能整份发给别
//   人的东西"，那它就必须是文件夹：选个位置，落一个以文档命名的子目录，
//   index.html 与所有资源都在里面。
// 因此这里重新走一遍 md_to_html（而不是拷预览目录的 index.html：那份
// 引用是相对预览目录的，搬走就断），输出目录直接就是用户选的位置。
void MainWindow::exportHtmlToFile()
{
    Tab *tab = activeTab();
    if (!tab || !tab->doc || !tab->doc->buffer()) return;

    std::string md = activeDocumentText();
    if (md.empty()) return;

    std::string srcPath = tab->doc->filePath();

    // 输出子目录名 = 文档名去扩展名；未保存的新文档用 pecia。
    std::string folderName = "pecia";
    if (srcPath[0]) {
        const char *name = fl_filename_name(srcPath.c_str());
        const char *dot = fl_filename_ext(name);
        size_t stem = dot && dot[0] ? (size_t)(dot - name) : strlen(name);
        if (stem > 0) folderName.assign(name, stem);
    }
    // 文件系统不允许这些字符出现在目录名里（源文件名合法时一般不会命中，
    // 但文档名可能来自网络下载/临时文件，仍要兜住）。
    for (char &c : folderName)
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    while (!folderName.empty() && (folderName.back() == ' ' || folderName.back() == '.'))
        folderName.pop_back();
    if (folderName.empty()) folderName = "pecia";

    // 起始目录：源文档所在目录优先，否则当前工作目录。
    std::string startDir = ".";
    if (srcPath[0]) {
        const char *slash = strrchr(srcPath.c_str(), '\\');
        if (slash) startDir.assign(srcPath.c_str(), slash - srcPath.c_str());
    }
    WCHAR startDirW[FL_PATH_MAX];
    MultiByteToWideChar(CP_UTF8, 0, startDir.c_str(), -1, startDirW, FL_PATH_MAX);
    WCHAR folderNameW[FL_PATH_MAX];
    MultiByteToWideChar(CP_UTF8, 0, folderName.c_str(), -1, folderNameW, FL_PATH_MAX);

    // 文件夹选择器：让用户选"放在哪"，子目录名由我们定。
    IFileOpenDialog *pfd = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IFileOpenDialog, (void**)&pfd);
    if (FAILED(hr) || !pfd) return;

    FILEOPENDIALOGOPTIONS opts = 0;
    pfd->GetOptions(&opts);
    pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    pfd->SetTitle(widen(I18n::get("export.folder.title")).c_str());
    pfd->SetFileName(folderNameW);

    IShellItem *psiFolder = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(startDirW, nullptr, IID_IShellItem,
                                              (void**)&psiFolder)) && psiFolder) {
        pfd->SetFolder(psiFolder);
        psiFolder->Release();
    }

    hr = pfd->Show(nullptr);
    if (FAILED(hr)) { pfd->Release(); return; }

    IShellItem *psiResult = nullptr;
    hr = pfd->GetResult(&psiResult);
    if (FAILED(hr) || !psiResult) { pfd->Release(); return; }

    PWSTR pszPath = nullptr;
    bool done = false;
    bool remoteFailed = false;
    int remoteTotal = 0;
    char chosen[FL_PATH_MAX] = { 0 };
    if (SUCCEEDED(psiResult->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)) && pszPath) {
        WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, chosen, FL_PATH_MAX, nullptr, nullptr);
        CoTaskMemFree(pszPath);

        // 选中的目录 + 文档名 = 最终输出目录。不覆盖已存在的同名目录里的
        // 旧内容：整个目录本来就是这次导出的产物，重复导出直接覆盖同名
        // 文件即可（CopyFileW / fopen "wb" 语义），残留的旧图片无害。
        std::string outDir(chosen);
        if (!outDir.empty() && outDir.back() != '\\' && outDir.back() != '/') outDir += '\\';
        outDir += folderName;
        CreateDirectoryW(widen(outDir).c_str(), NULL);

        // 与"在浏览器中打开"共用 writeStandaloneHtml()：两条路径产出的目录
        // 结构、文件命名、缺图判定必须逐字一致，否则又会各坏各的。
        int left = 0;
        if (writeStandaloneHtml(md, outDir, srcPath, editorFontName(m_cfg),
                                &left)) {
            remoteTotal = left;
            remoteFailed = (remoteTotal > 0);
            done = true;
        }
    }
    psiResult->Release();
    pfd->Release();

    if (done) {
        std::string indexPath = std::string(chosen) + "\\" + folderName + "\\index.html";
        if (remoteFailed) {
            // 有图没抓到：明说，别让用户对着缺图的页面猜。用 i18n 的失败
            // 提示 + 数量，路径放在同一条消息里。
            char msg[1024];
            std::snprintf(msg, sizeof(msg), "%s\n\n%d",
                          I18n::get("export.partial"), remoteTotal);
            MessageBoxW(NULL, widen(msg).c_str(), widen(indexPath).c_str(),
                        MB_OK | MB_ICONWARNING);
        } else {
            // 全部自包含：只报路径。"导出"的语义是存文件，不代劳打开浏览器。
            MessageBoxW(NULL, widen(indexPath).c_str(), L"Pecia",
                        MB_OK | MB_ICONINFORMATION);
        }
    }
}

void MainWindow::cbExportHtml(Fl_Widget *w, void *data)
{
    auto *win = static_cast<MainWindow *>(data ? data : w->window());
    if (win) win->exportHtmlToFile();
}

// 在 layoutTabs() 末尾调用：按预览模式排布两栏（编辑区 | 分隔条 | 预览）。
// 非预览模式时保持原行为。预览列 = 预览内容（顶部按钮栏已删除）
// 左侧视图面板（若开启）占用内容区最左的一条，编辑器与预览整体右移。
//
// contentH 与 tabsH 的区别是关键：contentH = 内容区全高（到状态栏上缘），
// tabsH = 再扣掉 find/go-to 栏后的可视高度。侧栏与分隔条用 contentH——
// 它们不属于编辑器列，不该被查找栏挤短；否则栏位呼出时面板下方会露出
// 窗口默认灰底（用户 2026-10-20 反馈的"左侧一块深色"）。
void MainWindow::layoutPreviewPanes(int contentTop, int contentH, int tabsH)
{
    const int sideW = sidePanelWidth();
    if (m_sidePanel) {
        if (sideW > 0) {
            m_sidePanel->resize(0, contentTop, sideW, contentH);
            m_sidePanel->setColors(m_theme.colors(),
                                   m_cfg ? m_cfg->getUiFontSize() : 16);
            m_sidePanel->relayout();
            m_sidePanel->show();
        } else {
            m_sidePanel->hide();
        }
    }
    // 侧栏分隔条：与预览分隔条同宽同色，悬停同为 FL_CURSOR_WE。
    // 面板因窄窗口被 sidePanelWidth() 判定为 0 时，分隔条一并隐藏。
    if (m_sideDivider) {
        if (sideW > 0) {
            m_sideDivider->resize(sideW, contentTop, kSideDividerW, contentH);
            m_sideDivider->show();
        } else {
            m_sideDivider->hide();
        }
    }
    const int contentX = contentLeft();
    int contentW = w() - contentX;
    if (contentW < 1) contentW = 1;

    if (m_previewActive && m_preview && m_previewDivider) {
        int editorW = (int)(contentW * m_previewRatio);
        if (editorW < (int)(contentW * 0.20f)) editorW = (int)(contentW * 0.20f);
        if (editorW > (int)(contentW * 0.80f)) editorW = (int)(contentW * 0.80f);
        int previewW = contentW - editorW - kDividerW;
        if (previewW < 1) previewW = 1;

        const int divX = contentX + editorW;
        const int px = divX + kDividerW;

        m_tabs->resize(contentX, contentTop, editorW, tabsH);
        m_previewDivider->resize(divX, contentTop, kDividerW, tabsH);
        // 顶部按钮栏已删除：预览视图从内容区顶缘开始，吃满 tabsH
        m_preview->view()->resize(px, contentTop, previewW, tabsH);
        m_preview->view()->show();
        m_previewDivider->show();
        m_previewDivider->redraw();
    } else {
        m_tabs->resize(contentX, contentTop, contentW, tabsH);
        if (m_previewDivider) m_previewDivider->hide();
        if (m_preview) m_preview->view()->hide();
    }
}

// 菜单：目录... → 打开左侧面板并切到大纲页；再点一次收起面板。
// 不再单独弹一个目录窗口：侧栏大纲与预览共用同一批锚点，两个入口只会
// 增加一套需要同步的 UI；没开侧栏时这里直接把它打开。
// 开关语义：面板已开且正处大纲 Tab 时，本次点击改为收起（与「侧面板」
// 开关一致）；面板开着但在文件树 Tab 时仍先切到大纲，不收起。
void MainWindow::cbShowOutlineInPanel(Fl_Widget * /*w*/, void *data)
{
    auto *win = static_cast<MainWindow *>(data);
    if (win) win->showOutlineInSidePanel();
}

void MainWindow::showOutlineInSidePanel()
{
    if (m_sidePanelOpen && m_sidePanel &&
        m_sidePanel->tab() == SidePanel::Outline) {
        toggleSidePanel();          // 已在大纲页：再点一次 = 收起
        return;
    }
    if (!m_sidePanelOpen) toggleSidePanel();
    if (!m_sidePanel) return;
    m_sidePanel->setTab(SidePanel::Outline);
    refreshOutline();   // 当前文档文本 → 标题（预览开/关都可用）
}

// 重建目录数据（渲染完成回调，主线程）：标题正文与层级分别存入 m_tocLabels /
// m_tocLevels，锚点 id 存 m_tocIds（侧栏大纲直接消费同一批数据）。
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
    // 侧栏大纲与预览同源：同一批标题、同一批锚点 id
    if (m_sidePanel && m_sidePanelOpen)
        // 渲染回调给的标题没有源码行号 → 行号数组留空；点击大纲时
    // MainWindow::jumpToHeadingLine 会按需重提一次（预览路径因此零额外成本）。
    m_sidePanel->setOutline(m_tocLabels, m_tocLevels, m_tocIds, std::vector<int>{});
}
