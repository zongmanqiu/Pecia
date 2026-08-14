#include "preview_panel.h"
#include "preprocess.h"
#include <FL/Fl.H>

// ----------------------------------------------------------------------------
// 异步渲染：常驻后台线程 + 覆盖式请求。
//   renderAsync() → 记录最新请求（覆盖旧请求），worker 空闲时立即消费
//   worker 循环   → 取最新请求 → md_to_html（ratex 进程/ mmdr FFI 均在线程内）
//   pump()        → 主线程定时器；有结果且版本最新 → applyResult（litehtml 布局）
//   UI 线程永不阻塞（不 join 渲染线程）。
// ----------------------------------------------------------------------------

PreviewPanel::PreviewPanel()
    : m_container(std::make_shared<MyContainer>())
    , m_view(new ViewWidget(0, 0, 100, 100, m_container.get()))
    , m_stop(false)
{
    m_container->set_viewport_size(100, 100);
    // 远程图片下载完成 → 重绘预览（下载线程回主线程后触发）
    m_container->set_remote_done_callback([this]() {
        if (m_view) {
            m_view->redraw();
            if (m_view->has_content()) {
                // 重新布局以纳入新图片尺寸
                m_view->load_and_render(m_view->current_html());
            }
        }
    });
    m_worker = std::thread([this]() { workerLoop(); });
}

PreviewPanel::~PreviewPanel()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
        m_cv.notify_all();
    }
    if (m_worker.joinable()) m_worker.join();
    if (m_pumpArmed) {
        m_pumpArmed = false;
        Fl::remove_timeout(timerCb, this);
    }
    delete m_view;
}

void PreviewPanel::workerLoop()
{
    for (;;) {
        std::string markdown, build_dir, doc_dir;
        int version = 0;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]() { return m_stop || m_hasRequest; });
            if (m_stop) return;
            markdown = m_requestMd;
            build_dir = m_requestBuildDir;
            doc_dir = m_requestDocDir;
            version = m_requestVersion;
            m_hasRequest = false;              // 只处理最新请求（覆盖式）
        }
        std::vector<PreviewHeading> headings;
        std::string html = md_to_html(markdown, build_dir, doc_dir, &headings);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (version == m_nextVersion) {    // 仍是最新请求 → 交付结果
                m_pendingHtml = std::move(html);
                m_pendingHeadings = std::move(headings);
                m_pendingVersion = version;
            }
            m_workerBusy = false;              // 渲染完成（无论是否交付）
        }
    }
}

void PreviewPanel::clear()
{
    m_view->set_rendering(false);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_nextVersion;                       // 作废所有在途结果
        m_pendingVersion = 0;
        m_pendingHtml.clear();
        m_hasRequest = false;
    }
    m_view->clear();
}

bool PreviewPanel::hasContent() const { return m_view->has_content(); }

void PreviewPanel::setBaseUrl(const std::string &dir)
{
    m_container->set_base_url(dir.c_str());
}

void PreviewPanel::renderAsync(const std::string &markdown, const std::string &build_dir,
                               const std::string &doc_dir)
{
    m_view->set_rendering(true);   // 空内容时显示 Rendering...
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_nextVersion;
        m_requestMd = markdown;
        m_requestBuildDir = build_dir;
        m_requestDocDir = doc_dir;
        m_requestVersion = m_nextVersion;
        m_hasRequest = true;
        m_workerBusy = true;                   // 泵 keep 依据：请求已发或 worker 渲染中
        m_cv.notify_all();
    }
    // 挂起主线程 pump 定时器（每 30ms 检查一次结果）
    if (!m_pumpArmed) {
        m_pumpArmed = true;
        Fl::add_timeout(0.03, timerCb, this);
    }
}

void PreviewPanel::pump()
{
    std::string html;
    std::vector<PreviewHeading> headings;
    bool hasResult = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_pendingVersion > 0) {
            html = m_pendingHtml;
            headings = std::move(m_pendingHeadings);
            m_pendingHtml.clear();
            m_pendingVersion = 0;
            hasResult = true;
        }
    }
    if (hasResult) applyResult(html, std::move(headings));
}

void PreviewPanel::timerCb(void *data)
{
    auto *self = static_cast<PreviewPanel *>(data);
    if (!self) return;
    self->pump();
    // 请求未发完、worker 渲染中或在途结果未取完 → 泵继续；全部空闲才停
    bool keep = false;
    {
        std::lock_guard<std::mutex> lock(self->m_mutex);
        keep = self->m_hasRequest || self->m_workerBusy || self->m_pendingVersion > 0;
        if (!keep) self->m_pumpArmed = false;
    }
    if (keep) {
        Fl::add_timeout(0.03, timerCb, self);
    }
}

void PreviewPanel::applyResult(const std::string &html)
{
    applyResult(html, {});
}

void PreviewPanel::applyResult(const std::string &html, std::vector<PreviewHeading> headings)
{
    m_view->set_rendering(false);
    // 自动刷新后保持垂直滚动百分比位置
    float ratio = 0.0f;
    if (m_view->has_content() && m_view->content_height() > 0)
        ratio = (float)m_view->scroll_y() / (float)m_view->content_height();
    m_view->load_and_render(html);
    if (ratio > 0.0f) m_view->scroll_to((int)(ratio * m_view->content_height()));
    m_view->redraw();
    if (m_headingsCb) m_headingsCb(headings);
}

void PreviewPanel::scrollToAnchor(const std::string &id)
{
    if (m_container && m_view->has_content())
        m_container->on_anchor_click(id.c_str(), nullptr);
}

void PreviewPanel::setFontScale(float scale)
{
    if (scale < 0.6f) scale = 0.6f;
    if (scale > 2.5f) scale = 2.5f;
    m_fontScale = scale;
    // 重新渲染当前 HTML（保持滚动比例），字体缩放由 MyContainer 应用
    float ratio = (m_view->content_height() > 0)
                  ? (float)m_view->scroll_y() / (float)m_view->content_height() : 0.0f;
    if (m_view->has_content()) {
        m_view->load_and_render(m_view->current_html());
        if (ratio > 0.0f) m_view->scroll_to((int)(ratio * m_view->content_height()));
    }
    m_view->redraw();
}
