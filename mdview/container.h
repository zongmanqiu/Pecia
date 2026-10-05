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
#include <string>
#include <utility>
#include <vector>
#include <functional>
#include "litehtml.h"
#include <FL/Fl_Shared_Image.H>
#include <FL/Fl_SVG_Image.H>
#include <FL/Fl_JPEG_Image.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_Image.H>

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

class MyContainer : public litehtml::document_container,
                   public std::enable_shared_from_this<MyContainer> {
public:
    MyContainer();
    ~MyContainer() override;
    void set_document(const std::shared_ptr<litehtml::document>& doc);
    void clear_images();
    std::shared_ptr<litehtml::document> document() const { return m_doc; }
    void set_viewport_size(int w, int h);
    void set_font_scale(float s) { m_font_scale = s; }
    float font_scale() const { return m_font_scale; }

    // 预览基准字体（编辑器当前字体名，见 FontUtils::FONT_LIST）。预览的正文/
    // 代码都跟随它：md_to_html 把它写在注入 CSS 的第一个候选，这里再据此解析
    // 兜底 face（CSS 候选全部不可用时使用）。UI 线程调用。
    void set_base_font_name(const std::string& name);

    using scroll_to_cb_t = std::function<void(int)>;
    void set_scroll_callback(scroll_to_cb_t cb) { m_scroll_cb = std::move(cb); }

    // 远程图片下载完成回调（主线程触发；用于通知视图重绘）
    using remote_done_cb_t = std::function<void()>;
    void set_remote_done_callback(remote_done_cb_t cb) { m_remote_done_cb = std::move(cb); }

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
    void link(const std::shared_ptr<litehtml::document>&, const litehtml::element::ptr&) override;
    void on_anchor_click(const char*, const litehtml::element::ptr&) override;
    void on_mouse_event(const litehtml::element::ptr&, litehtml::mouse_event) override;
    void set_cursor(const char*) override;
    void transform_text(std::string&, litehtml::text_transform) override;
    void import_css(std::string&, const std::string&, std::string&) override;
    void set_clip(const litehtml::position&, const litehtml::border_radiuses&) override;
    void del_clip() override;
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
    std::map<std::string, Fl_Image*> m_images;
    std::map<std::string, Fl_Image*> m_scaledImages; // 按 路径@WxH 键的缩放缓存
    std::map<std::string, GifAnim*> m_gifs;
    // Self-owned image pixel buffers and how they were allocated.
    // pair <ptr, fromStbi>: fromStbi==true means stbi_image_free, false means
    // delete[]. stbi_load/stbi_load_gif return stbi_malloc'd memory while the
    // scaled copies are new unsigned char[] - the two MUST NOT be freed the
    // same way (delete[] on a malloc'd block is undefined behavior).
    std::vector<std::pair<unsigned char*, bool>> m_owned_pixels;
    scroll_to_cb_t m_scroll_cb;
    remote_done_cb_t m_remote_done_cb;   // 远程图片下载完成（主线程）
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
};

class ViewWidget : public Fl_Widget {
    MyContainer* m_container;
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
public:
    ViewWidget(int x, int y, int w, int h, MyContainer*);
    void load_and_render(const std::string& html);
    void clear();
    bool has_content() const;
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
};
