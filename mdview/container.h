#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Window.H>
#include <FL/fl_draw.H>
#include <memory>
#include <map>
#include <list>
#include <unordered_map>
#include <string>
#include <utility>
#include <vector>
#include <functional>
#include "litehtml.h"
#include "text_width_cache.h"
#include "block_layout.h"
#include <FL/Fl_Shared_Image.H>
#include <FL/Fl_SVG_Image.H>
#include <FL/Fl_JPEG_Image.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_Image.H>

// 预览阶段耗时诊断：仅在 PECIA_PROFILE=1 时把 (阶段名, 毫秒) 追加到
// exe 同级 preview_profile.log。默认完全关闭（一次 getenv + 分支）。
// 存在的理由：大文档打开慢必须能回答「慢在预处理 / 建 DOM / 布局 / 绘制
// 哪一段」，靠外部采样只能看到一个总数（见 temp/预览内存优化方案.md）。
void mdv_prof_ms(const char* tag, double ms);

// 同一条诊断通道（PECIA_PROFILE=1 → exe 同级 preview_profile.log），但记的是
// 整数量：块数、总高 px、实测块数。分块渲染的效果只能靠这几个数判断
// （块切了多少、估算模型收敛到多少 px/KB、有没有实例化整篇）。
void mdv_prof_int(const char* tag, long long value);

// 同上，记一行自由文本（分块窗口的块级快照：字节/估算高度/实测高度/偏移）。
void mdv_prof_str(const char* tag, const std::string& text);

class MyContainer;

struct FontInfo { Fl_Font face; int size; int ascent; int descent; int height; int x_height; int decoration = 0; };

// Result of resolving one CSS font-family value: the FLTK face actually drawn
// with, plus the Windows face name it corresponds to (used for GDI metrics, so
// measurement and drawing agree).
struct ResolvedFont { Fl_Font face; std::string name; };

struct GifAnim {
    unsigned char* pixels; int w, h, ch;
    int frame_count; int* delays; int current_frame = 0;
    Fl_RGB_Image* image = nullptr;
    std::string cache_key; MyContainer* container = nullptr;
};

// 文本测量缓存见 text_width_cache.h —— 抽成独立类型是为了能用纯单测覆盖
// 「键的每个维度（文本 / face / size）都必须参与比较」这条契约。

class MyContainer : public litehtml::document_container,
                   public std::enable_shared_from_this<MyContainer> {
public:
    MyContainer();
    ~MyContainer() override;
    void set_document(const std::shared_ptr<litehtml::document>& doc);
    void clear_images();

    // 视口裁剪矩形（屏幕坐标）。由 ViewWidget::draw() 在每次绘制前设置。
    //
    // 为什么需要：litehtml 的 render_item::draw_children() 只用 is_visible()
    // （display / visibility）过滤，**不看 clip**；clip 只被透传给 set_clip()
    // 做 CSS overflow 裁剪。于是一篇 100 张图的文档，每帧都会把 100 张图
    // 全过一遍 draw_image()，屏幕外的图照样创建按布局框尺寸的缩放副本
    // （760x570x4 ≈ 1.65 MB/张）。这不是假设，是 render_item.cpp:841 的
    // 直读结论。
    //
    // 这里只做「完全不相交就跳过」的快速排除：部分可见的图照常绘制，
    // 所以不影响正确性，只省成本。
    void set_view_clip(const litehtml::position& clip)
    {
        m_view_clip = clip;
        m_has_view_clip = true;
    }
    void clear_view_clip() { m_has_view_clip = false; }

    // 供测试/诊断查询：当前缩放缓存占用的字节数与条目数。
    size_t scaled_cache_bytes() const { return m_scaledBytes; }
    size_t scaled_cache_count() const { return m_scaledImages.size(); }
    size_t image_cache_count() const { return m_images.size(); }
    size_t text_width_cache_size() const { return m_textWidthCache.size(); }
    size_t draw_image_calls() const { return m_draw_image_calls; }
    size_t draw_image_skipped() const { return m_draw_image_skipped; }
    void reset_draw_counters() { m_draw_image_calls = m_draw_image_skipped = 0; }

    // 换文档时清空文本测量缓存。生命周期跟文档走：同一份 HTML 反复重排
    // （拖分隔条 / 缩放）时测量结果依然有效，换文件则整批作废。
    void clear_text_width_cache() { m_textWidthCache.clear(); }

    std::shared_ptr<litehtml::document> document() const { return m_doc; }
    void set_viewport_size(int w, int h);
    void set_font_scale(float s) { m_font_scale = s; }
    float font_scale() const { return m_font_scale; }

    // 预览基准字体（编辑器当前字体名，来自 View > Font 的字体对话框）。预览的正文/
    // 代码都跟随它：md_to_html 把它写在注入 CSS 的第一个候选，这里再据此解析
    // 兜底 face（CSS 候选全部不可用时使用）。UI 线程调用。
    void set_base_font_name(const std::string& name);

    using scroll_to_cb_t = std::function<void(int)>;
    void set_scroll_callback(scroll_to_cb_t cb) { m_scroll_cb = std::move(cb); }

    // 分块模式下的锚点跳转：那时没有单一 document，无法按元素定位，改由视图
    // 在块表里查 id（见 ViewWidget::scroll_to_anchor）。
    using anchor_cb_t = std::function<void(const std::string&)>;
    void set_anchor_callback(anchor_cb_t cb) { m_anchor_cb = std::move(cb); }

    // 远程图片下载完成回调（主线程触发；用于通知视图重绘）
    using remote_done_cb_t = std::function<void()>;
    void set_remote_done_callback(remote_done_cb_t cb) { m_remote_done_cb = std::move(cb); }

    // 单个远程图片结束（成功或失败）后的通知。用于把「最后一笔下载完成」
    // 交给已有的 scheduleTrimSoon() 做工作集回收；不改变重绘/占位逻辑。
    using remote_settled_cb_t = std::function<void()>;
    void set_remote_settled_callback(remote_settled_cb_t cb) { m_remote_settled_cb = std::move(cb); }

    // litehtml 接口
    litehtml::uint_ptr create_font(const litehtml::font_description&, const litehtml::document*, litehtml::font_metrics*) override;
    void delete_font(litehtml::uint_ptr) override;
    litehtml::pixel_t text_width(const char*, litehtml::uint_ptr) override;
    void draw_text(litehtml::uint_ptr, const char*, litehtml::uint_ptr, litehtml::web_color, const litehtml::position&) override;
    litehtml::pixel_t pt_to_px(float) const override;
    litehtml::pixel_t get_default_font_size() const override;
    const char* get_default_font_name() const override;
    void draw_list_marker(litehtml::uint_ptr, const litehtml::list_marker&) override;
    void load_image(const char*, const char*, bool) override;
    void get_image_size(const char*, const char*, litehtml::size&) override;
    void draw_image(litehtml::uint_ptr, const litehtml::background_layer&, const std::string&, const std::string&) override;
    void draw_solid_fill(litehtml::uint_ptr, const litehtml::background_layer&, const litehtml::web_color&) override;
    void draw_linear_gradient(litehtml::uint_ptr, const litehtml::background_layer&, const litehtml::background_layer::linear_gradient&) override;
    void draw_radial_gradient(litehtml::uint_ptr, const litehtml::background_layer&, const litehtml::background_layer::radial_gradient&) override;
    void draw_conic_gradient(litehtml::uint_ptr, const litehtml::background_layer&, const litehtml::background_layer::conic_gradient&) override;
    void draw_borders(litehtml::uint_ptr, const litehtml::borders&, const litehtml::position&, bool) override;
    void set_caption(const char*) override;
    void set_base_url(const char*) override;
    // 预览产物目录（exe 同级 temp/<文档名>-<PID>/）。远程图片直接下载到这里，
    // 用URL 里的原始文件名与真实扩展名 —— 于是这个目录本身就是一份完整
    // 自包含的网页资源：浏览器直接打开 index.html 即可，导出到别处也只是
    // 把整个文件夹复制过去。不再另设 temp\*.img 缓存（同一张图会存两份，
    // 且哈希名既不可读也打不开）。
    void set_asset_dir(const std::string& dir) { m_asset_dir = dir; }

    // Manual refresh only: ignore the persistent remote-image cache and the
    // per-run asset files, so the next layout downloads fresh bytes. The
    // caller must clear m_images first (PreviewPanel::applyResult does), so
    // replacing the files cannot leave an old decoded image behind.
    void set_force_remote_refresh(bool on) { m_force_remote_refresh = on; }
    void link(const std::shared_ptr<litehtml::document>&, const litehtml::element::ptr&) override;
    void on_anchor_click(const char*, const litehtml::element::ptr&) override;
    void on_mouse_event(const litehtml::element::ptr&, litehtml::mouse_event) override;
    void set_cursor(const char*) override;
    void transform_text(std::string&, litehtml::text_transform) override;
    void import_css(std::string&, const std::string&, std::string&) override;
    void set_clip(const litehtml::position&, const litehtml::border_radiuses&) override;
    void del_clip() override;
    // 当前 FLTK 裁剪栈深度（只统计 litehtml 的 set_clip/del_clip 配对）。
    // 分块绘制时每块都会重启一次 draw，若某一块的 set_clip 没有配对 del_clip，
    // 后续块的全部绘制都会被裁在错误区域里 —— 表现就是「大片空白 + 内容错位」。
    int clip_depth() const { return m_clip_depth; }
    void get_viewport(litehtml::position&) const override;
    litehtml::element::ptr create_element(const char*, const litehtml::string_map&, const std::shared_ptr<litehtml::document>&) override;
    void get_media_features(litehtml::media_features&) const override;
    void get_language(std::string&, std::string&) const override;

    static void gif_timer_cb(void* data);

private:
    struct RemoteDone;                       // 远程图片下载任务（cpp 内定义）
    struct RemoteFail;                       // 远程下载失败任务（cpp 内定义）
    static void remote_download_done_cb(void* data);  // 主线程回调（成功）
    static void remote_download_fail_cb(void* data);  // 主线程回调（失败 → 占位）
    std::shared_ptr<litehtml::document> m_doc;
    int m_viewport_w = 800, m_viewport_h = 600;
    float m_screen_dpi = 96.0f;
    float m_font_scale = 1.0f;   // 预览字体缩放（Ctrl+滚轮）
    std::string m_base_url;
    std::string m_asset_dir;
    bool m_force_remote_refresh = false;   // one render only (see setter)
    std::map<std::string, Fl_Image*> m_images;
    // 按 路径@WxH 键的缩放缓存。存的是「按布局框尺寸」的副本（1.65 MB/张量级），
    // 是图片内存的大头，所以用真 LRU（按字节淘汰单条）而不是「满 N 条全清」——
    // 后者在图片数超过上限的文档里会退化成每帧重建全部（见方案 6.2.2）。
    std::map<std::string, Fl_Image*> m_scaledImages;
    std::list<std::string> m_scaledLru;                     // 头部 = LRU，尾部 = MRU
    std::unordered_map<std::string, std::list<std::string>::iterator> m_scaledLruIndex;
    size_t m_scaledBytes = 0;
    // 缩放缓存上限。32 MiB ≈ 19 张 1.65 MB 的副本；条目上限防小图无限增长。
    static constexpr size_t kScaledCacheMaxBytes = 32u * 1024u * 1024u;
    static constexpr size_t kScaledCacheMaxCount = 256;

    litehtml::position m_view_clip;
    bool m_has_view_clip = false;
    int m_clip_depth = 0;               // litehtml set_clip/del_clip 的配对数
    // 文本测量缓存。**不设容量上限**：它是文档级缓存，随换文档清空，
    // 单文档的 unique 片段数由文档本身决定（实测 166 KB 的 md 只有 4,478 个）。
    // 给它加上限只会让「文本种类多于上限」的文档反复重测，收益为负。
    pecia::TextWidthCache m_textWidthCache;
    // 绘制计数（只读诊断用，零成本：两个自增）
    size_t m_draw_image_calls = 0;
    size_t m_draw_image_skipped = 0;
    std::map<std::string, GifAnim*> m_gifs;
    // Self-owned image pixel buffers and how they were allocated.
    // pair <ptr, fromStbi>: fromStbi==true means stbi_image_free, false means
    // delete[]. stbi_load/stbi_load_gif return stbi_malloc'd memory while the
    // scaled copies are new unsigned char[] - the two MUST NOT be freed the
    // same way (delete[] on a malloc'd block is undefined behavior).
    std::vector<std::pair<unsigned char*, bool>> m_owned_pixels;
    scroll_to_cb_t m_scroll_cb;
    anchor_cb_t m_anchor_cb;             // 分块模式的锚点跳转（见 set_anchor_callback）
    remote_done_cb_t m_remote_done_cb;   // 远程图片下载完成（主线程）
    remote_settled_cb_t m_remote_settled_cb;   // 全部远程图结束（成功/失败）
    int m_pendingRemoteDownloads = 0;          // 仅 UI 线程读写
    std::map<std::string, ResolvedFont> m_font_map;   // CSS font-family 值 → 解析结果
    std::string m_base_font_name;                     // 预览基准字体名（编辑器字体）
    Fl_Font     m_base_face = -1;                     // 基准字体解析结果（-1=未安装）
    ResolvedFont resolve_family(const std::string&);
    Fl_Image* load_image_file(const std::string&);
    Fl_Image* load_bitmap_file(const std::string&);   // stb_image 加载位图（本地/缓存）
    Fl_Image* load_gif_file(const std::string& file, const std::string& key);  // GIF 多帧加载（本地/远程缓存），注册动画，返回首帧
    Fl_Image* load_svg_text(const std::string&);      // mmdr FFI 渲染 SVG 文本为位图
    // 按 URL 类型分派加载缓存文件（SVG/GIF/位图）。远程"下载完成"与
    // "缓存已存在"两条路径必须共用它，否则会首次显示、之后变占位。
    Fl_Image* load_cached_image(const std::string& cache_file, const std::string& url);
    std::string resolve_image_path(const std::string&, const std::string&) const;

    // 缩放缓存：真 LRU（命中提到 MRU；插入前按字节/条目淘汰最久未用的单条）
    Fl_Image* find_scaled(const std::string& key);
    void insert_scaled(const std::string& key, Fl_Image* img);
    void drop_scaled(const std::string& key);
    void clear_scaled();
};

class ViewWidget : public Fl_Widget {
    MyContainer* m_container;
    // 分块（虚拟化）布局：只有超过 kChunkThreshold 的文档才启用，普通文档仍走
    // 「一个 document」的全量路径 —— 后者已经足够快（166 KB 的 md 只要 175 ms），
    // 而且全量路径下搜索/选中/锚点都是天然可用的。分块是为了让 1 MB 级文档的
    // 内存与视口成正比（实测 594 MB → 38 MB），代价是多块之间的交互要单独处理。
    std::unique_ptr<pecia::BlockLayout> m_blocks;
    int m_scroll_y = 0;
    int m_scroll_x = 0;
    bool m_wrap = true;
    int m_drag_start_x = 0;
    int m_drag_start_y = 0;
    int m_drag_start_scroll_x = 0;
    int m_drag_start_scroll_y = 0;
    bool m_dragging_vscroll = false;
    bool m_dragging_hscroll = false;
    bool m_rendering = false;             // 后台渲染进行中（空内容时显示 Rendering...）
    // 渲染失败提示（空内容时显示在中间，替代 "Rendering..."）。
    // 预览没有大小上限了，超大文档走到 litehtml 布局阶段可能 oom；
    // 那时必须有一句可读的话，而不是让异常逃出事件循环把整个进程带走。
    std::string m_renderError;
    std::string m_current_html;       // 当前渲染的 HTML 内容（用于导出）
    std::function<void(const std::string&)> m_open_cb;  // 拖放打开路径回调
    std::function<void(float)> m_zoom_cb;               // Ctrl+滚轮缩放回调
    // Scrollbar colors (theme-driven; defaults match old hardcoded values
    // so the widget still works before setScrollbarColors is called).
    Fl_Color m_trackColor = fl_rgb_color(240, 240, 240);
    Fl_Color m_thumbColor = fl_rgb_color(180, 180, 180);
    // 防抖重排：wrap 模式下宽度变化时不立即 re-render（大文档 ~50ms/次，
    // 拖动分隔条时连续触发会阻塞 UI）。延迟到拖动停止后才重排。
    int m_pending_render_width = -1;   // 待重排宽度，-1 = 无待处理
    bool m_debounce_active = false;    // FLTK 定时器是否已注册
public:
    // 超过这个 HTML 体积就改用分块渲染。256 KB 是本机实测的分界：低于它的
    // 文档全量 parse 只要 ~200 ms、稳态内存不到 100 MB，走全量路径既快又没
    // 有虚拟化带来的边界问题（块间距、锚点、选中都要单独照顾）。
    static constexpr size_t kChunkThreshold = 256 * 1024;

    ViewWidget(int x, int y, int w, int h, MyContainer*);
    void load_and_render(const std::string& html);
    void clear();
    bool has_content() const;
    // 是否处于分块模式（大文档）。诊断与测试用。
    bool block_mode() const { return m_blocks && m_blocks->active(); }
    pecia::BlockLayout* block_layout() const { return m_blocks.get(); }
    // 分块模式下按锚点 id 定位并滚动。返回 false 表示没找到（调用方退回全量路径）。
    bool scroll_to_anchor(const std::string& id);
    int content_height() const;
    int content_width() const;
    void scroll_to(int y);
    void scroll_to_x(int x);
    int scroll_y() const { return m_scroll_y; }
    int scroll_x() const { return m_scroll_x; }
    const std::string& current_html() const { return m_current_html; }
    void set_open_callback(std::function<void(const std::string&)> cb) { m_open_cb = std::move(cb); }
    void set_rendering(bool r) { m_rendering = r; if (r) m_renderError.clear(); redraw(); }
    // 布局失败（通常是 oom）：显示一行原因，替代 Rendering...。
    void set_render_error(const std::string& msg) { m_renderError = msg; redraw(); }
    void set_zoom_callback(std::function<void(float)> cb) { m_zoom_cb = std::move(cb); }
    void setScrollbarColors(Fl_Color track, Fl_Color thumb) {
        m_trackColor = track;
        m_thumbColor = thumb;
        redraw();
    }
    void draw() override;
    void resize(int X, int Y, int W, int H) override;
    int handle(int event) override;
private:
    // 分块路径：切分 + 首次实例化视口块。返回 false 表示这块 HTML 不适合分块
    // （结构异常 / 覆盖度不足），调用方必须退回全量路径。
    bool render_chunked(const std::string& html);
    // 分块绘制：只画可见块，并在前后各预取 1 块。
    void draw_chunked(int content_w, int content_h, const litehtml::position& clip);
    // 分块模式下把块实例化到「包含当前视口」的窗口。
    void ensure_visible_blocks(int content_h);
    // 防抖重排：resize 时注册 FLTK 定时器，延迟执行 re-render。
    static void debounce_timer_cb(void* data);
    void do_deferred_render();
    void cancel_deferred_render();
};
