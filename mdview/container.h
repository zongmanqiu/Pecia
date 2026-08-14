#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Tile.H>
#include <FL/fl_draw.H>
#include <memory>
#include <map>
#include <string>
#include <vector>
#include <functional>
#include "litehtml.h"
#include <FL/Fl_Shared_Image.H>
#include <FL/Fl_SVG_Image.H>
#include <FL/Fl_JPEG_Image.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_Image.H>

class MyContainer;

// Fl_Tile 子类：分割线拖动结束后强制重绘整个 Tile 区域（消除残影），
// 并把分割比例钳制在 20%-80% 之间（AGENTS.md 规范）。
class MyTile : public Fl_Tile {
public:
    using Fl_Tile::Fl_Tile;
    void move_intersection(int oldx, int oldy, int newx, int newy) override;
};

struct FontInfo { Fl_Font face; int size; int ascent; int descent; int height; int x_height; int decoration = 0; };

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
    std::map<std::string, Fl_Image*> m_images;
    std::map<std::string, Fl_Image*> m_scaledImages; // 按 路径@WxH 键的缩放缓存
    std::map<std::string, GifAnim*> m_gifs;
    std::vector<unsigned char*> m_owned_pixels;
    scroll_to_cb_t m_scroll_cb;
    remote_done_cb_t m_remote_done_cb;   // 远程图片下载完成（主线程）
    std::map<std::string, Fl_Font> m_font_map;
    Fl_Font get_font_face(const std::string&);
    Fl_Image* load_image_file(const std::string&);
    Fl_Image* load_bitmap_file(const std::string&);   // stb_image 加载位图（本地/缓存）
    Fl_Image* load_svg_text(const std::string&);      // mmdr FFI 渲染 SVG 文本为位图
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
    std::string m_current_html;       // 当前渲染的 HTML 内容（用于导出）
    std::function<void(const std::string&)> m_open_cb;  // 拖放打开路径回调
    std::function<void(float)> m_zoom_cb;               // Ctrl+滚轮缩放回调
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
    bool wrap_mode() const { return m_wrap; }
    void set_wrap_mode(bool wrap);
    const std::string& current_html() const { return m_current_html; }
    void set_open_callback(std::function<void(const std::string&)> cb) { m_open_cb = std::move(cb); }
    void set_rendering(bool r) { m_rendering = r; redraw(); }
    void set_zoom_callback(std::function<void(float)> cb) { m_zoom_cb = std::move(cb); }
    void draw() override;
    void resize(int X, int Y, int W, int H) override;
    int handle(int event) override;
};
