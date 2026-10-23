#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <condition_variable>
#include <FL/Fl_Widget.H>
#include "container.h"   // MyContainer, ViewWidget
#include "preprocess.h"  // PreviewHeading

// ----------------------------------------------------------------------------
// PreviewPanel —— Markdown 预览面板（mdview 核心的 Pecia 内嵌形态）
//   文本 → md_to_html（preprocess.cpp）→ litehtml 渲染（MyContainer + ViewWidget）
//
//   异步渲染：常驻后台线程执行 md_to_html（ratex 进程调用与 mmdr FFI 渲染），
//   UI 线程不阻塞；结果回主线程做 litehtml 布局（FLTK 相关，必须主线程）。
//   覆盖式请求：连续触发只渲染最新一份；过期结果自动丢弃。
//   析构时停止线程（安全）。
// ----------------------------------------------------------------------------
class PreviewPanel {
public:
    PreviewPanel();
    ~PreviewPanel();

    ViewWidget *view() const { return m_view; }   // 预览 widget（嵌入布局）

    // 异步渲染 Markdown 文本。build_dir 为 exe 目录（公式/图表图片输出位置）。
    // 立即返回；渲染完成后内部调度到主线程更新视图。
    // doc_dir：文档所在目录（用于把本地图片拷贝进预览目录，供浏览器完整显示）
    // forceRemoteRefresh: manual Refresh only. Bypasses the persistent
    // remote-image cache and re-downloads images; the document itself is
    // rendered through the same path as always.
    void renderAsync(const std::string &markdown, const std::string &build_dir,
                     const std::string &doc_dir, bool forceRemoteRefresh = false);
    void clear();               // 清空预览并作废在途渲染
    bool hasContent() const;

    // 文档基础目录（解析相对图片路径，如 "p1.png" -> 目录/p1.png）
    void setBaseUrl(const std::string &dir);

    // 预览基准字体（编辑器 editor_font）。预览正文/代码跟随编辑器字体：
    // 该名字由 md_to_html 写入注入 CSS 的首位候选，同时交给容器做兜底 face。
    // UI 线程调用；下一次 renderAsync 生效（已渲染的内容需重新刷新）。
    void setBaseFont(const std::string &name) {
        m_baseFont = name;
        if (m_container) m_container->set_base_font_name(name);
    }

    // 标题列表回调（每次渲染完成时触发，参数为文档顺序的标题，供目录树重建）
    using headings_cb_t = std::function<void(const std::vector<PreviewHeading> &)>;
    void setHeadingsCallback(headings_cb_t cb) { m_headingsCb = std::move(cb); }

    // 每张远程图结束（成功/失败）时触发。MainWindow 用它复用已有的
    // scheduleTrimSoon() 做工作集回收；不参与重绘判定。
    using remote_settled_cb_t = std::function<void()>;
    void setRemoteSettledCallback(remote_settled_cb_t cb) { m_remoteSettledCb = std::move(cb); }

    // 滚动到文档内锚点（如 "toc-0"）——目录树点击跳转
    void scrollToAnchor(const std::string &id);

    // 预览文本缩放（Ctrl+滚轮）。缩放后重新布局（主线程）。
    void setFontScale(float scale);
    float fontScale() const { return m_fontScale; }

    // 主线程定时器驱动：取回后台渲染结果
    static void timerCb(void *data);
    void pump();

private:
    void applyResult(const std::string &html, std::vector<PreviewHeading> headings,
                     bool forceRemoteRefresh);
    void workerLoop();

    std::shared_ptr<MyContainer> m_container;   // 生命周期共享（远程下载 weak_ptr 引用）
    ViewWidget *m_view;
    float       m_fontScale = 1.0f;
    std::string m_baseFont;                      // 预览基准字体（UI 线程写）
    headings_cb_t m_headingsCb;                  // 渲染完成后的标题回调（主线程）
    remote_settled_cb_t m_remoteSettledCb;       // 远程图结束 → 工作集回收（主线程）

    std::mutex              m_mutex;
    std::condition_variable m_cv;
    bool                    m_stop = false;

    // 请求侧（覆盖式）
    std::string m_requestMd;
    std::string m_requestBuildDir;
    std::string m_requestDocDir;
    std::string m_requestFont;              // 请求侧字体副本（worker 线程读）
    bool        m_requestForceRemote = false;   // 手动刷新：本次请求强制重下远程图
    int         m_requestVersion = 0;
    bool        m_hasRequest = false;
    int         m_nextVersion = 0;

    // 结果侧
    std::string m_pendingHtml;
    std::vector<PreviewHeading> m_pendingHeadings;
    std::string m_pendingError;            // 非空 = 这次转换失败了（带原因）
    bool        m_pendingForceRemote = false;
    int         m_pendingVersion = 0;
    bool        m_workerBusy = false;   // worker 正在渲染（泵的 keep 依据）

    std::thread m_worker;               // 常驻后台线程
    bool        m_pumpArmed = false;    // pump 定时器已挂起
};

// ----------------------------------------------------------------------------
// PreviewDivider —— 两栏分隔条（4px 可拖动，比例限制 20%-80%，AGENTS.md 规范）
//   拖动时回调 onDrag(新比例)，由 MainWindow 更新 m_previewRatio 并重排布局。
//
//   左侧视图面板（SidePanel）的分隔条复用本控件，但可用范围不同：预览栏是
//   「编辑器|预览」对半，侧栏只有 160–280px，在大窗口上按整窗比例算会被
//   0.20 下限卡死（1920px 下拖到 x=100 仍返回 0.20*1920=384px，等于拖不动）。
//   因此用 setDragRange() 放开范围，具体钳制由调用方按像素做。
// ----------------------------------------------------------------------------
class PreviewDivider : public Fl_Widget {
public:
    using drag_cb_t = std::function<void(float)>;   // 参数：左栏占窗口的比例
    drag_cb_t onDrag;

    explicit PreviewDivider(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {
        box(FL_FLAT_BOX);
    }

    // 分隔条颜色（跟随编辑区边框：状态栏/行号列同色 bgChrome）
    void setDividerColor(Fl_Color c) { m_color = c; redraw(); }

    // onDrag 比例可用范围（默认 0.20~0.80）。调用方需自行按像素钳制。
    void setDragRange(float lo, float hi) {
        m_minRatio = lo;
        m_maxRatio = (hi > lo) ? hi : lo;
    }

    void draw() override {
        fl_color(m_color);
        fl_rectf(x(), y(), w(), h());
    }

    int handle(int event) override {
        switch (event) {
        case FL_ENTER:
            // 悬停分隔条：显示水平调整光标（与窗口边框一致）
            if (window()) window()->cursor(FL_CURSOR_WE);
            return 1;
        case FL_LEAVE:
            if (window()) window()->cursor(FL_CURSOR_DEFAULT);
            return 1;
        case FL_PUSH:
            return 1;
        case FL_DRAG: {
            if (window()) window()->cursor(FL_CURSOR_WE);
            if (onDrag) {
                Fl_Window *win = window();
                int total = win ? win->w() : 1000;
                float ratio = (float)(Fl::event_x_root() - (win ? win->x() : 0)) / (float)total;
                if (ratio < m_minRatio) ratio = m_minRatio;
                if (ratio > m_maxRatio) ratio = m_maxRatio;
                onDrag(ratio);
            }
            return 1;
        }
        default:
            return 0;
        }
    }

private:
    Fl_Color m_color = FL_DARK2;   // 默认深灰；创建后由 MainWindow 设为主题 bgChrome
    float    m_minRatio = 0.20f;
    float    m_maxRatio = 0.80f;
};
