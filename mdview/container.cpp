#include "container.h"
#include "preprocess.h"
#include "editor/Editor.h"   // FontUtils::findFontByName（CSS 字体名 → 已安装字体）
#include "core/PathUtils.h"  // pathutil::exeDir()（便携：缓存写 exe 同级 temp/）

#include <FL/Fl_Image_Surface.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>
#include "mmdr_ffi.h"

// Forward declaration: process-level singleton placeholder used for images
// that fail to load. Defined later in this file.
static Fl_Image* unsupportedImage();

// stb_image：跨平台图片解码（单头文件，零依赖）
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#ifdef _WIN32
#include <shellapi.h>
#include <urlmon.h>  // URLDownloadToFileW
#pragma comment(lib, "urlmon.lib")
// UTF-8 → UTF-16 转换
static std::wstring widen(const std::string& s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], len);
    return w;
}
// 用 GDI 获取精确的字体度量
static void get_gdi_metrics(const std::string& family, int pixel_size, int weight,
                            bool italic, int& ascent, int& descent, int& height, int& x_height)
{
    HDC hdc = CreateCompatibleDC(nullptr);
    if (!hdc) { ascent = (int)(pixel_size * 0.8f); descent = pixel_size - ascent; height = pixel_size; x_height = pixel_size / 2; return; }

    // 尝试创建指定字体
    std::wstring wfamily = widen(family);
    HFONT hf = CreateFontW(-pixel_size, 0, 0, 0,
        (DWORD)weight, (DWORD)italic, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        wfamily.c_str());
    if (!hf) {
        // 回退到 Arial
        hf = CreateFontW(-pixel_size, 0, 0, 0,
            (DWORD)weight, (DWORD)italic, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
    }
    if (!hf) {
        DeleteDC(hdc);
        ascent = (int)(pixel_size * 0.8f); descent = pixel_size - ascent; height = pixel_size; x_height = pixel_size / 2;
        return;
    }

    SelectObject(hdc, hf);
    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);

    ascent   = tm.tmAscent;
    descent  = tm.tmDescent;
    height   = tm.tmHeight;   // 不含 external leading
    x_height = tm.tmAveCharWidth; // 近似

    SelectObject(hdc, GetStockObject(SYSTEM_FONT));
    DeleteObject(hf);
    DeleteDC(hdc);
}
#endif

// ==================== 工具函数 ====================

static std::string read_file(const char* path)
{
    HANDLE hFile = CreateFileW(widen(path).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return {};

    DWORD size = GetFileSize(hFile, NULL);
    if (size == INVALID_FILE_SIZE || size == 0) {
        CloseHandle(hFile);
        return {};
    }

    std::string buf((size_t)size, '\0');
    DWORD read = 0;
    ReadFile(hFile, &buf[0], size, &read, NULL);
    CloseHandle(hFile);
    return buf;
}

// stb 的磁盘接口走 ANSI fopen，中文路径会失败：用宽字符读入内存，
// 再交给 stbi_load_from_memory 解码。返回 stbi_image_free 可释放的指针。
static unsigned char* stbi_load_utf8(const char* path, int* x, int* y, int* comp, int req)
{
    FILE* f = _wfopen(widen(path).c_str(), L"rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return nullptr; }
    auto* buf = static_cast<unsigned char*>(malloc((size_t)size));
    if (!buf) { fclose(f); return nullptr; }
    size_t rd = fread(buf, 1, (size_t)size, f);
    fclose(f);
    unsigned char* data = stbi_load_from_memory(buf, (int)rd, x, y, comp, req);
    free(buf);
    return data;
}

// ==================== MyContainer ====================

MyContainer::MyContainer() = default;

void MyContainer::set_document(const std::shared_ptr<litehtml::document>& doc)
{
    m_doc = doc;
}

void MyContainer::clear_images()
{
    // 清除图片缓存：GIF 帧指针与 m_gifs 的 gif->image 是同一指针，
    // 必须跳过（否则双删崩溃），由下方 m_gifs 循环统一删除。
    // 注意：此循环必须在 m_gifs 删除/clear 之前执行——若先清 m_gifs，
    // m_gifs.count(path) 恒为 0，GIF 帧指针会被 delete 两次（double-free）。
    // 占位图（unsupportedImage()）是进程级静态单例，属 m_images 外部的
    // 自有生命周期，不能 delete（否则函数静态指针悬垂 → 后续 UAF/double-free）。
    for (auto& [path, img] : m_images) {
        if (m_gifs.count(path)) continue;
        if (img == unsupportedImage()) continue;   // 单例占位图不释放
        delete img;
    }
    m_images.clear();

    // 清除 GIF 动画和定时器（在 m_images 循环之后执行）
    for (auto& [k, gif] : m_gifs) {
        if (gif) {
            Fl::remove_timeout(gif_timer_cb, gif);
            delete gif->image;
            if (gif->pixels) stbi_image_free(gif->pixels);
            if (gif->delays) stbi_image_free(gif->delays);
            delete gif;
        }
    }
    m_gifs.clear();

    // 清除缩放缓存
    for (auto& [k, img] : m_scaledImages) {
        delete img;
    }
    m_scaledImages.clear();

    // 清除自定义像素数据（按分配器区分释放方式）
    for (auto& [p, fromStbi] : m_owned_pixels) {
        if (fromStbi) stbi_image_free(p);
        else delete[] p;
    }
    m_owned_pixels.clear();
}

void MyContainer::set_viewport_size(int w, int h)
{
    m_viewport_w = w;
    m_viewport_h = h;
}

// 去掉 CSS 字体名两侧的空白与引号（litehtml 的 parse_font_family 已剥掉引号，
// 但手写 HTML / 未走该解析路径时可能残留，这里统一处理）。
static std::string trim_family_token(const std::string& s)
{
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    size_t e = s.find_last_not_of(" \t\r\n");
    std::string t = s.substr(b, e - b + 1);
    if (t.size() >= 2 && ((t.front() == '\'' && t.back() == '\'') ||
                          (t.front() == '"'  && t.back() == '"')))
        t = t.substr(1, t.size() - 2);
    return t;
}

static std::string to_lower_ascii(std::string s)
{
    for (char& c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

// CSS 通用字体族关键字（sans-serif / serif / monospace ...）：它们不是真实
// 字体名，不能拿去查系统字体表。
static bool is_generic_family(const std::string& lower)
{
    static const char* kGeneric[] = {
        "serif", "sans-serif", "monospace", "cursive", "fantasy",
        "system-ui", "ui-serif", "ui-sans-serif", "ui-monospace", "ui-rounded",
        "-apple-system", "emoji", "math", "fangsong",
    };
    for (const char* g : kGeneric)
        if (lower == g) return true;
    return false;
}

void MyContainer::set_base_font_name(const std::string& name)
{
    if (name == m_base_font_name) return;
    m_base_font_name = name;
    m_base_face = name.empty() ? -1 : FontUtils::findFontByName(name.c_str());
    m_font_map.clear();   // 兜底 face 变了，已缓存的解析结果作废
}

ResolvedFont MyContainer::resolve_family(const std::string& family)
{
    auto it = m_font_map.find(family);
    if (it != m_font_map.end()) return it->second;

    // CSS font-family 是逗号分隔的候选列表，按顺序取第一个系统真正安装的
    // 字体（与 litehtml 官方 win32 container 的做法一致）。旧实现把整串当
    // 关键字匹配 mono/serif/sans，真实字体名（Consolas / 微软雅黑 …）被全部
    // 丢弃，预览因此永远画不出编辑器里选定的字体。
    ResolvedFont r{ -1, std::string() };
    litehtml::string_vector fonts;
    litehtml::split_string(family, fonts, ",");
    for (const auto& raw : fonts) {
        std::string name = trim_family_token(raw);
        if (name.empty() || is_generic_family(to_lower_ascii(name))) continue;
        Fl_Font id = FontUtils::findFontByName(name.c_str());
        if (id >= 0) { r.face = id; r.name = name; break; }
    }

    if (r.face < 0) {
        // 候选全部不可用：退回 FLTK 逻辑字体。Windows 上这三个分别是
        // Courier New / Times New Roman / Microsoft Sans Serif。
        std::string lower = to_lower_ascii(family);
        if (lower.find("mono") != std::string::npos ||
            lower.find("courier") != std::string::npos ||
            lower.find("console") != std::string::npos ||
            lower.find("code") != std::string::npos) {
            r.face = FL_COURIER;
            r.name = "Courier New";
        } else if (lower.find("serif") != std::string::npos ||
                   lower.find("times") != std::string::npos) {
            r.face = FL_TIMES;
            r.name = "Times New Roman";
        } else if (m_base_face >= 0) {
            r.face = m_base_face;
            r.name = m_base_font_name;
        } else {
            r.face = FL_HELVETICA;
            r.name = "Microsoft Sans Serif";
        }
    }

    m_font_map[family] = r;
    return r;
}

// ---------- 字体 ----------

litehtml::uint_ptr MyContainer::create_font(const litehtml::font_description& descr,
                                            const litehtml::document*, litehtml::font_metrics* fm)
{
    ResolvedFont rf = resolve_family(descr.family);
    Fl_Font base = rf.face;
    int off = 0;
    if (descr.weight >= 700) off += 1;
    if (descr.style == litehtml::font_style_italic) off += 2;
    Fl_Font face = (Fl_Font)(base + off);
    // 预览字体缩放：布局尺寸与绘制尺寸统一在此应用（px/pt/em 全部生效）
    int     size = (int)(descr.size * m_font_scale);
    if (size < 1) size = 1;

    auto* fi = new FontInfo();
    fi->face  = face;
    fi->size  = size;
    fi->decoration = descr.decoration_line;

    // 用 GDI 精确测量字体。必须用解析出的真实字体名（rf.name）而不是整串
    // CSS 候选列表：CreateFontW 只接受单个字体名，传逗号列表会落到某个默认
    // 字体上，量出来的行高/基线和实际绘制的 face 不一致。
    {
        int w = (descr.weight >= 700) ? FW_BOLD : FW_NORMAL;
        bool it = (descr.style == litehtml::font_style_italic);
        int a = 0, d = 0, h = 0, xh = 0;
        get_gdi_metrics(rf.name, size, w, it, a, d, h, xh);
        fi->ascent   = a;
        fi->descent  = d;
        fi->height   = h;
        fi->x_height = xh;
    }

    if (fm) {
        fm->ascent     = (float)fi->ascent;
        fm->descent    = (float)fi->descent;
        fm->height     = (float)fi->height;
        fm->x_height   = (float)fi->x_height;
        fm->draw_spaces = true;
        // 上下标偏移（行高的固定比例）
        fm->super_shift  = (float)(fi->height / 3);
        fm->sub_shift    = (float)(fi->height / 6);
    }
    return (litehtml::uint_ptr)fi;
}

void MyContainer::delete_font(litehtml::uint_ptr hFont)
{
    delete (FontInfo*)hFont;
}

litehtml::pixel_t MyContainer::text_width(const char* text, litehtml::uint_ptr hFont)
{
    auto* fi = (FontInfo*)hFont;
    fl_font(fi->face, fi->size);
    return litehtml::pixel_t((float)fl_width(text));
}

void MyContainer::draw_text(litehtml::uint_ptr, const char* text,
                            litehtml::uint_ptr hFont, litehtml::web_color color,
                            const litehtml::position& pos)
{
    auto* fi = (FontInfo*)hFont;
    fl_font(fi->face, fi->size);   // 尺寸已在 create_font 应用缩放
    fl_color(fl_rgb_color(color.red, color.green, color.blue));
    fl_draw(text, (int)pos.x, (int)(pos.y + fi->ascent));

    // 文本装饰线（下划线/删除线/上划线）：litehtml 把 decoration 传给
    // create_font（font_description::decoration_line），绘制由容器负责。
    if (fi->decoration != 0) {
        int tw = (int)fl_width(text);
        int x1 = (int)pos.x;
        int x2 = x1 + tw;
        fl_line_style(FL_SOLID, 1);
        if (fi->decoration & litehtml::text_decoration_line_underline) {
            int y = (int)(pos.y + fi->ascent + 1);
            fl_line(x1, y, x2, y);
        }
        if (fi->decoration & litehtml::text_decoration_line_line_through) {
            int y = (int)(pos.y + fi->ascent - fi->x_height / 2);
            fl_line(x1, y, x2, y);
        }
        if (fi->decoration & litehtml::text_decoration_line_overline) {
            int y = (int)(pos.y + fi->ascent - fi->x_height);
            fl_line(x1, y, x2, y);
        }
        fl_line_style(FL_SOLID, 0);
    }
}

// ---------- 单位转换 ----------

litehtml::pixel_t MyContainer::pt_to_px(float pt) const
{
    return litehtml::pixel_t((float)(pt * m_screen_dpi / 72.0f + 0.5f));
}

litehtml::pixel_t MyContainer::get_default_font_size() const
{
    return litehtml::pixel_t(16.0f);   // 缩放统一在 create_font 应用
}

const char* MyContainer::get_default_font_name() const
{
    return "Microsoft YaHei, sans-serif";
}

// ---------- 列表 ----------

void MyContainer::draw_list_marker(litehtml::uint_ptr, const litehtml::list_marker& marker)
{
    if (!marker.image.empty()) return;
    fl_color(fl_rgb_color(marker.color.red, marker.color.green, marker.color.blue));

    if (marker.marker_type >= litehtml::list_style_type_armenian &&
        marker.marker_type <= litehtml::list_style_type_lower_alpha) {
        int sz = (int)(marker.pos.height);
        if (marker.font) {
            auto* fi = (FontInfo*)marker.font;
            fl_font(fi->face, std::min(fi->size, sz));
        } else fl_font(FL_HELVETICA, sz);
        std::string num = std::to_string(marker.index) + ".";
        fl_draw(num.c_str(), (int)marker.pos.x,
                (int)(marker.pos.y + marker.pos.height - 3));
    } else {
        int cx = (int)(marker.pos.x + marker.pos.width / 2);
        int cy = (int)(marker.pos.y + marker.pos.height / 2);
        int r  = std::max(2, (int)(std::min(marker.pos.width, marker.pos.height) / 3));
        // 区分几何 marker：disc 实心圆 / circle 空心圆 / square 实心方块
        switch (marker.marker_type) {
        case litehtml::list_style_type_square:
            fl_rectf(cx - r, cy - r, r * 2, r * 2);
            break;
        case litehtml::list_style_type_circle:
            fl_arc(cx - r, cy - r, r * 2, r * 2, 0, 360);
            break;
        default:   // disc
            fl_pie(cx - r, cy - r, r * 2, r * 2, 0, 360);
            break;
        }
    }
}

// ---------- 图片 ----------

MyContainer::~MyContainer()
{
    clear_images();
    m_doc.reset();
}

static bool is_remote_url(const std::string& url)
{
    return url.find("http://") == 0 || url.find("https://") == 0 || url.find("ftp://") == 0;
}

static bool is_svg_path(const std::string& path)
{
    auto pos = path.rfind('.');
    if (pos == std::string::npos) return false;
    std::string ext;
    for (size_t i = pos; i < path.size(); i++)
        ext.push_back((char)std::tolower((unsigned char)path[i]));
    return ext == ".svg";
}

static bool is_gif_path(const std::string& path)
{
    auto pos = path.rfind('.');
    if (pos == std::string::npos) return false;
    std::string ext;
    for (size_t i = pos; i < path.size(); i++)
        ext.push_back((char)std::tolower((unsigned char)path[i]));
    return ext == ".gif";
}

// 图片加载失败的占位图：灰底 + 细边框 + 居中 ×（纯符号，无语言问题）。
// 共享单例（不按 path 缓存、不可被 clear_images delete——失败分支提前返回，
// 不进入 m_images）。
static Fl_Image* unsupportedImage()
{
    static Fl_Image* img = nullptr;
    if (img) return img;
    const int W = 40, H = 40;
    Fl_Image_Surface surf(W, H);
    Fl_Surface_Device::push_current(&surf);
    fl_color(0xE8, 0xE8, 0xE8);        // 浅灰底
    fl_rectf(0, 0, W, H);
    fl_color(0x9A, 0x9A, 0x9A);        // 边框
    fl_rect(0, 0, W, H);
    fl_color(0x88, 0x88, 0x88);        // × 符号
    fl_font(FL_HELVETICA, 20);
    fl_draw("\xC3\x97", 0, 0, W, H, FL_ALIGN_CENTER);   // × (U+00D7)
    Fl_Surface_Device::pop_current();
    img = surf.image();
    return img;
}

// 远程图片异步下载：线程完成后经 Fl::awake 回主线程
struct MyContainer::RemoteDone {
    std::weak_ptr<MyContainer> wc;
    std::string url;        // 原始 URL（缓存 key）
    std::string cache_file; // 下载到的本地文件
};

// 远程下载失败（URLDownloadToFile 失败）→ 主线程显示占位
struct MyContainer::RemoteFail {
    std::weak_ptr<MyContainer> wc;
    std::string url;
};

void MyContainer::remote_download_fail_cb(void* data)
{
    auto* task = static_cast<RemoteFail*>(data);
    if (!task) return;
    std::shared_ptr<MyContainer> self = task->wc.lock();
    if (self) {
        self->m_images[task->url] = unsupportedImage();
        if (self->m_remote_done_cb) self->m_remote_done_cb();
    }
    delete task;
}

void MyContainer::remote_download_done_cb(void* data)
{
    auto* task = static_cast<RemoteDone*>(data);
    if (!task) return;
    std::shared_ptr<MyContainer> self = task->wc.lock();
    if (self) {
        // 按类型加载缓存文件：SVG 用 resvg 渲染，GIF 走多帧动画，其余按位图（stb）
        Fl_Image* img = nullptr;
        if (is_svg_path(task->url)) {
            std::string svg_text = read_file(task->cache_file.c_str());
            if (!svg_text.empty()) img = self->load_svg_text(svg_text);
        } else if (is_gif_path(task->url)) {
            // 远程 GIF 与本地一致地注册动画；key 用 URL（= draw_image 的 full）
            img = self->load_gif_file(task->cache_file, task->url);
        } else {
            img = self->load_bitmap_file(task->cache_file);
        }
        if (img) self->m_images[task->url] = img;
        else self->m_images[task->url] = unsupportedImage();   // 下载成功但解析失败 → 占位
        if (self->m_remote_done_cb) self->m_remote_done_cb();
    }
    delete task;
}

// mmdr FFI（resvg）渲染 SVG 文本为位图（自动使用系统字体）；超过 200px 等比缩到 200px 内
Fl_Image* MyContainer::load_svg_text(const std::string& svg_text)
{
    MmdrBitmap bmp = {0};
    if (mmdr_svg_to_png(svg_text.c_str(), 0, 0, &bmp) != 0 ||
        !bmp.pixels || bmp.width <= 0 || bmp.height <= 0) {
        mmdr_bitmap_free(&bmp);
        return nullptr;
    }
    int dw = bmp.width, dh = bmp.height;
    int rw = dw, rh = dh;
    int longer = std::max(dw, dh);
    if (longer > 200) {
        float sc = 200.0f / longer;
        rw = std::max(1, (int)(dw * sc));
        rh = std::max(1, (int)(dh * sc));
    }
    if (rw != dw || rh != dh) {
        // 需要缩放：释放自然尺寸位图，按目标尺寸重渲
        mmdr_bitmap_free(&bmp);
        if (mmdr_svg_to_png(svg_text.c_str(), rw, rh, &bmp) != 0 ||
            !bmp.pixels || bmp.width <= 0 || bmp.height <= 0) {
            mmdr_bitmap_free(&bmp);
            return nullptr;
        }
    }
    int stride = bmp.stride;
    int bw = bmp.width, bh = bmp.height;
    size_t alloc = (size_t)bh * (size_t)stride;
    auto* pixels = new unsigned char[alloc];
    memcpy(pixels, bmp.pixels, alloc);
    m_owned_pixels.emplace_back(pixels, false);   // new[] -> delete[]
    Fl_Image* img = new Fl_RGB_Image(pixels, bw, bh, 4, stride);
    mmdr_bitmap_free(&bmp);
    return img;
}

// 用 stb_image 加载位图（本地文件/远程缓存），超过 200px 等比缩到 200px 内
Fl_Image* MyContainer::load_bitmap_file(const std::string& file)
{
    int rw = 0, rh = 0, rch = 0;
    unsigned char* rdata = stbi_load_utf8(file.c_str(), &rw, &rh, &rch, 4);
    if (!rdata) return nullptr;
    rch = 4;
    int longer = std::max(rw, rh);
    bool fromStbi = true;
    if (longer > 200) {
        float sc = 200.0f / longer;
        int nw = std::max(1, (int)(rw * sc));
        int nh = std::max(1, (int)(rh * sc));
        auto* dst = new unsigned char[(size_t)nw * nh * rch]();
        for (int y = 0; y < nh; y++) {
            int sy = y * rh / nh;
            for (int x = 0; x < nw; x++) {
                int sx = x * rw / nw;
                memcpy(dst + (y * nw + x) * rch, rdata + (sy * rw + sx) * rch, rch);
            }
        }
        stbi_image_free(rdata);
        rdata = dst;
        rw = nw; rh = nh;
        fromStbi = false;               // now a new[] buffer
    }
    m_owned_pixels.emplace_back(rdata, fromStbi);
    return new Fl_RGB_Image(rdata, rw, rh, rch);
}

// 用 stb_image 加载 GIF 的所有帧（宽字符打开，中文路径安全），返回首帧；
// 同时注册到 m_gifs 以支持动画。本地文件路径与远程下载缓存文件都走这里。
// file：磁盘上的 GIF 文件；key：缓存键（本地为路径，远程为 URL），须与
// draw_image / get_image_size 中 resolve_image_path 的结果一致，动画才能被找到。
Fl_Image* MyContainer::load_gif_file(const std::string& file, const std::string& key)
{
    FILE* f = _wfopen(widen(file).c_str(), L"rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0) { fclose(f); return nullptr; }
    auto* buf = new unsigned char[(size_t)fsize];
    size_t rd = fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    if (rd != (size_t)fsize) { delete[] buf; return nullptr; }

    int w = 0, h = 0, frames = 0, ch = 0;
    int* delays = nullptr;
    unsigned char* pixels = stbi_load_gif_from_memory(buf, (int)fsize, &delays, &w, &h, &frames, &ch, 4);
    delete[] buf;

    if (pixels && frames > 0) {
        ch = 4;
        auto* gif = new GifAnim();
        gif->pixels = pixels;  // stbi_malloc，不用 delete[]
        gif->w = w; gif->h = h; gif->ch = ch;
        gif->frame_count = frames;
        gif->delays = delays;  // stbi_malloc，clear_images 里 stbi_image_free
        gif->current_frame = 0;
        gif->cache_key = key;
        gif->container = this;
        // 第一帧作为初始 Fl_RGB_Image（像素数据已包含在 pixels 中）
        gif->image = new Fl_RGB_Image(pixels, w, h, ch);
        m_gifs[key] = gif;
        return gif->image;     // 第一帧
    }
    if (pixels) stbi_image_free(pixels);
    return nullptr;
}

std::string MyContainer::resolve_image_path(const std::string& url, const std::string& base_url) const
{
    // 远程 URL 直接返回
    if (is_remote_url(url)) return url;

    // 绝对路径直接返回
    if (!url.empty() && url[0] == '/') return url;
    if (url.size() > 2 && url[1] == ':') return url; // e.g. C:\...

    // 相对路径：base_url + url
    if (!base_url.empty()) {
        // base_url 可能是目录（以 / 结尾）或文件
        std::string base = base_url;
        if (base.back() != '/' && base.back() != '\\')
            base = base.substr(0, base.find_last_of("/\\") + 1);
        return base + url;
    }
    return url;
}

Fl_Image* MyContainer::load_image_file(const std::string& path)
{
    // 检查缓存
    auto it = m_images.find(path);
    if (it != m_images.end()) return it->second;

    Fl_Image* img = nullptr;

    if (is_remote_url(path))
    {
        // 远程图片：异步下载（后台线程），下载完成回主线程加载并重绘。
        // 同步 URLDownloadToFileW 会阻塞 UI（慢网下可卡死十几秒）。
        // 便携：缓存一律落在 exe 同级 temp/，绝不写 exe 之外（%TEMP% 会污染
        // 系统临时目录，与"便携程序"目标冲突）。exe 目录解析失败则放弃下载。
        std::string cache_dir = pathutil::exeDir();
        if (!cache_dir.empty()) {
            cache_dir += "\\temp";
            CreateDirectoryW(widen(cache_dir).c_str(), NULL);
            // 缓存文件名 = URL 的简单哈希 + 扩展名
            unsigned h = 2166136261u;
            for (unsigned char c : path) h = (h ^ c) * 16777619u;
            char name[64];
            snprintf(name, sizeof(name), "pecia_img_%08x.img", h);
            std::string cache_file = cache_dir + "\\" + name;

            if (GetFileAttributesW(widen(cache_file).c_str()) != INVALID_FILE_ATTRIBUTES) {
                // 缓存已存在 → 直接加载（GIF 走多帧动画分支，key 用 URL）
                img = is_gif_path(path) ? load_gif_file(cache_file, path)
                                        : load_bitmap_file(cache_file);
            } else {
                // 后台下载：任务持 weak_ptr（容器析构后回调为空操作）
                auto wp = shared_from_this();
                std::thread([wp, path, cache_file]() {
                    HRESULT hr = URLDownloadToFileW(nullptr, widen(path).c_str(),
                                                    widen(cache_file).c_str(), 0, nullptr);
                    if (SUCCEEDED(hr)) {
                        auto task = new RemoteDone{wp, path, cache_file};
                        Fl::awake(remote_download_done_cb, task);
                    } else {
                        // 下载失败 → 主线程显示占位（不重试，刷新预览可重新触发）
                        auto task = new RemoteFail{wp, path};
                        Fl::awake(remote_download_fail_cb, task);
                    }
                }).detach();
            }
        }
    }
    else
    {
        // 本地文件
        if (is_svg_path(path))
        {
            // 用 mmdr FFI（resvg 0.47）直接渲染 SVG 为位图（自动使用系统字体）
            std::string svg_text = read_file(path.c_str());
            if (!svg_text.empty()) {
                img = load_svg_text(svg_text);
            }
            if (!img) return unsupportedImage();   // 加载失败 → 占位
        }
        else if (is_gif_path(path))
        {
            // GIF：加载所有帧并注册动画（远程缓存文件也复用同一逻辑）
            img = load_gif_file(path, path);
            if (!img) return unsupportedImage();   // GIF 加载失败 → 占位
        }
        else
        {
            // 位图用 stb_image 加载 + 手动缩放
            // 注意：必须 else，否则 SVG/GIF 分支成功加载的图片也会被此块
            // 用 stbi_load 重新解码并覆盖（SVG 永远显示占位、GIF 动画失效）。
            int iw = 0, ih = 0, ich = 0;
            unsigned char* data = stbi_load_utf8(path.c_str(), &iw, &ih, &ich, 4);
            if (data) {
                ich = 4;
                int longer = std::max(iw, ih);
                int nw = iw, nh = ih;
                // 公式和 mermaid 不缩放
                bool no_scale = (path.find("formulas") != std::string::npos)
                             || (path.find("mermaid") != std::string::npos);
                bool fromStbi = true;
                if (!no_scale && longer > 200) {
                    float sc = 200.0f / longer;
                    nw = std::max(1, (int)(iw * sc));
                    nh = std::max(1, (int)(ih * sc));
                    auto* dst = new unsigned char[nw * nh * ich]();
                    for (int y = 0; y < nh; y++) {
                        int sy = y * ih / nh;
                        for (int x = 0; x < nw; x++) {
                            int sx = x * iw / nw;
                            memcpy(dst + (y * nw + x) * ich,
                                   data + (sy * iw + sx) * ich, ich);
                        }
                    }
                    stbi_image_free(data);
                    data = dst;
                    iw = nw; ih = nh;
                    fromStbi = false;               // now a new[] buffer
                }
                m_owned_pixels.emplace_back(data, fromStbi);
                img = new Fl_RGB_Image(data, iw, ih, ich);
            } else {
                return unsupportedImage();   // 解码失败（格式不支持/损坏）→ 占位
            }
        }
    }

    // 存入缓存
    m_images[path] = img;
    return img;
}

void MyContainer::load_image(const char* src, const char* baseurl, bool /*redraw_on_ready*/)
{
    if (!src || !*src) return;
    std::string full = resolve_image_path(src, baseurl ? baseurl : m_base_url);
    load_image_file(full);
}

void MyContainer::get_image_size(const char* src, const char* baseurl, litehtml::size& sz)
{
    sz.width = litehtml::pixel_t(0);
    sz.height = litehtml::pixel_t(0);
    if (!src || !*src) return;

    std::string full = resolve_image_path(src, baseurl ? baseurl : m_base_url);
    auto it = m_images.find(full);
    Fl_Image* img = nullptr;
    if (it != m_images.end()) {
        img = it->second;
    } else {
        // 尝试加载
        img = load_image_file(full);
    }

    if (img && img->w() > 0 && img->h() > 0) {
        sz.width = litehtml::pixel_t((float)img->w());
        sz.height = litehtml::pixel_t((float)img->h());
    }
}

void MyContainer::draw_image(litehtml::uint_ptr /*hdc*/, const litehtml::background_layer& layer,
                             const std::string& url, const std::string& base_url)
{
    std::string full = resolve_image_path(url, base_url.empty() ? m_base_url : base_url);
    auto it = m_images.find(full);
    Fl_Image* img = nullptr;
    if (it != m_images.end()) {
        img = it->second;
    } else {
        img = load_image_file(full);
    }

    if (!img || img->w() <= 0 || img->h() <= 0) return;

    // 如果是 GIF 且尚未开始播放，启动动画
    auto git = m_gifs.find(full);
    if (git != m_gifs.end() && git->second && git->second->frame_count > 1) {
        GifAnim* gif = git->second;
        if (!Fl::has_timeout(gif_timer_cb, gif)) {
            int delay = std::max(1, gif->delays[0]);
            Fl::add_timeout(delay / 1000.0, gif_timer_cb, gif);
        }
    }

    auto& box = layer.border_box;
    // 布局框与图片源尺寸不同时，按布局框缩放绘制。
    // Fl_Image::draw 的 W/H 参数是裁剪语义（源图大于目标时只画左上角），
    // 因此先 copy 出目标尺寸（BILINEAR 缩放）再 1:1 绘制，结果按目标尺寸缓存。
    Fl_Image* draw_img = img;
    int bw = (int)box.width, bh = (int)box.height;
    if (bw > 0 && bh > 0 && (bw != img->w() || bh != img->h())) {
        std::string key = full + "@" + std::to_string(bw) + "x" + std::to_string(bh);
        auto sit = m_scaledImages.find(key);
        if (sit != m_scaledImages.end()) {
            draw_img = sit->second;
        } else {
            if (m_scaledImages.size() >= 16) {   // 防无限增长
                for (auto& [k, si] : m_scaledImages) delete si;
                m_scaledImages.clear();
            }
            draw_img = img->copy(bw, bh);
            m_scaledImages[key] = draw_img;
        }
    }
    draw_img->draw((int)box.x, (int)box.y);

    // 1px 黑色边框
    fl_color(fl_rgb_color(0, 0, 0));
    fl_rect((int)box.x, (int)box.y,
            (int)box.width, (int)box.height);
}

// ---------- GIF 动画 ----------

void MyContainer::gif_timer_cb(void* data)
{
    auto* gif = (GifAnim*)data;
    if (!gif || !gif->pixels || gif->frame_count <= 1) return;

    // 更新到下一帧
    gif->current_frame = (gif->current_frame + 1) % gif->frame_count;

    // 更新 Fl_RGB_Image 指向当前帧数据
    int frame_size = gif->w * gif->h * gif->ch;
    const uchar* frame_data = gif->pixels + gif->current_frame * frame_size;
    delete gif->image;
    gif->image = new Fl_RGB_Image(frame_data, gif->w, gif->h, gif->ch);

    // 更新 m_images 缓存
    if (gif->container && !gif->cache_key.empty()) {
        auto& imgs = gif->container->m_images;
        auto kit2 = imgs.find(gif->cache_key);
        if (kit2 != imgs.end()) {
            kit2->second = gif->image;
        }
        // 帧更新后旧缩放缓存失效（key 前缀 = 路径@）
        auto& scaled = gif->container->m_scaledImages;
        std::string prefix = gif->cache_key + "@";
        for (auto it = scaled.begin(); it != scaled.end();) {
            if (it->first.compare(0, prefix.size(), prefix) == 0) {
                delete it->second;
                it = scaled.erase(it);
            } else {
                ++it;
            }
        }
    }

    // 安排下一帧
    int delay = std::max(1, gif->delays[gif->current_frame]);
    Fl::add_timeout(delay / 1000.0, gif_timer_cb, gif);

    // 触发重绘（通过全局 FLTK 窗口）
    Fl_Window* win = Fl::first_window();
    if (win) win->redraw();
}

// ---------- 背景填充 ----------

void MyContainer::draw_solid_fill(litehtml::uint_ptr, const litehtml::background_layer& layer,
                                  const litehtml::web_color& color)
{
    auto& p = layer.border_box;
    if (p.width <= 0 || p.height <= 0) return;
    fl_color(fl_rgb_color(color.red, color.green, color.blue));
    // 内缩 1px 避免与边框绘制区域重叠产生视觉伪影
    float bw = 1.0f;
    fl_rectf((int)(p.x + bw), (int)(p.y + bw),
             (int)(p.width - bw * 2), (int)(p.height - bw * 2));
}

void MyContainer::draw_linear_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                                       const litehtml::background_layer::linear_gradient& g)
{
    if (!g.color_points.empty()) draw_solid_fill(hdc, layer, g.color_points.front().color);
}
void MyContainer::draw_radial_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                                       const litehtml::background_layer::radial_gradient& g)
{
    if (!g.color_points.empty()) draw_solid_fill(hdc, layer, g.color_points.front().color);
}
void MyContainer::draw_conic_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                                       const litehtml::background_layer::conic_gradient& g)
{
    if (!g.color_points.empty()) draw_solid_fill(hdc, layer, g.color_points.front().color);
}

// ---------- 边框 ----------

void MyContainer::draw_borders(litehtml::uint_ptr, const litehtml::borders& borders,
                               const litehtml::position& draw_pos, bool)
{
    auto visible = [](const litehtml::border& b) {
        return b.width > 0.0f
            && b.style != litehtml::border_style_none
            && b.style != litehtml::border_style_hidden;
    };

    int x = (int)draw_pos.x;
    int y = (int)draw_pos.y;
    int w = (int)draw_pos.width;
    int h = (int)draw_pos.height;

    if (visible(borders.top)) {
        fl_color(fl_rgb_color(borders.top.color.red, borders.top.color.green, borders.top.color.blue));
        fl_rectf(x, y, w, (int)borders.top.width);
    }
    if (visible(borders.bottom)) {
        fl_color(fl_rgb_color(borders.bottom.color.red, borders.bottom.color.green, borders.bottom.color.blue));
        fl_rectf(x, y + h - (int)borders.bottom.width, w, (int)borders.bottom.width);
    }
    if (visible(borders.left)) {
        fl_color(fl_rgb_color(borders.left.color.red, borders.left.color.green, borders.left.color.blue));
        fl_rectf(x, y + (int)borders.top.width, (int)borders.left.width,
                 h - (int)borders.top.width - (int)borders.bottom.width);
    }
    if (visible(borders.right)) {
        fl_color(fl_rgb_color(borders.right.color.red, borders.right.color.green, borders.right.color.blue));
        fl_rectf(x + w - (int)borders.right.width, y + (int)borders.top.width,
                 (int)borders.right.width,
                 h - (int)borders.top.width - (int)borders.bottom.width);
    }
}

// ---------- 杂项 ----------

void MyContainer::set_caption(const char*) {}
void MyContainer::set_base_url(const char* base_url)
{
    if (base_url) m_base_url = base_url;
    else m_base_url.clear();
}
void MyContainer::link(const std::shared_ptr<litehtml::document>&, const litehtml::element::ptr&) {}
void MyContainer::on_anchor_click(const char* url, const litehtml::element::ptr&)
{
    if (!url || !*url) return;
    if (url[0] == '#' && m_doc && m_scroll_cb) {
        std::string target = url + 1;
        // 从文档树中查找 id 匹配的元素
        std::function<litehtml::element::ptr(const litehtml::element::ptr&, const std::string&)> find;
        find = [&find](const litehtml::element::ptr& el, const std::string& id) -> litehtml::element::ptr {
            const char* attr = el->get_attr("id");
            if (attr && id == attr) return el;
            for (auto& child : el->children()) {
                auto found = find(child, id);
                if (found) return found;
            }
            return nullptr;
        };
        auto el = find(m_doc->root(), target);
        if (el) {
            litehtml::position pos = el->get_placement();
            m_scroll_cb((int)pos.y);
        }
    }
}
void MyContainer::on_mouse_event(const litehtml::element::ptr&, litehtml::mouse_event) {}
void MyContainer::set_cursor(const char*) {}
void MyContainer::transform_text(std::string&, litehtml::text_transform) {}
void MyContainer::import_css(std::string&, const std::string&, std::string&) {}

void MyContainer::set_clip(const litehtml::position& pos, const litehtml::border_radiuses&)
{
    fl_push_clip((int)pos.x, (int)pos.y,
                 (int)pos.width, (int)pos.height);
}
void MyContainer::del_clip() { fl_pop_clip(); }

void MyContainer::get_viewport(litehtml::position& viewport) const
{
    viewport.x      = litehtml::pixel_t(0);
    viewport.y      = litehtml::pixel_t(0);
    viewport.width  = litehtml::pixel_t((float)m_viewport_w);
    viewport.height = litehtml::pixel_t((float)m_viewport_h);
}

litehtml::element::ptr MyContainer::create_element(const char*, const litehtml::string_map&,
                                                   const std::shared_ptr<litehtml::document>&)
{
    return nullptr;
}

void MyContainer::get_media_features(litehtml::media_features& media) const
{
    memset(&media, 0, sizeof(media));
    media.type          = litehtml::media_type_screen;
    media.width         = (float)m_viewport_w;
    media.height        = (float)m_viewport_h;
    media.device_width  = (float)m_viewport_w;
    media.device_height = (float)m_viewport_h;
    media.color         = 8;
    media.monochrome    = 0;
    media.color_index   = 256;
    media.resolution    = (float)(int)m_screen_dpi;
}

void MyContainer::get_language(std::string& language, std::string& culture) const
{
    language = "zh";
    culture  = "zh-CN";
}

// ==================== ViewWidget ====================

ViewWidget::ViewWidget(int x, int y, int w, int h, MyContainer* container)
    : Fl_Widget(x, y, w, h), m_container(container)
{
    // 使用 FLAT_BOX + 白色背景，让 FLTK 在 draw() 前自动清空背景，
    // 避免分割线拖动后旧内容残留在新分配的区域内。
    box(FL_FLAT_BOX);
    color(FL_WHITE);
}

void ViewWidget::load_and_render(const std::string& html)
{
    if (!m_container) return;
    m_current_html = html;
    auto doc = litehtml::document::createFromString(html.c_str(), m_container);
    if (!doc) return;
    m_container->set_document(doc);
    m_scroll_y = 0;
    m_scroll_x = 0;
    doc->render(litehtml::pixel_t((float)w()));
    redraw();
}

void ViewWidget::clear()
{
    m_current_html.clear();
    if (m_container) m_container->set_document(nullptr);
    m_scroll_y = 0;
    m_scroll_x = 0;
    redraw();
}

bool ViewWidget::has_content() const
{
    return m_container && m_container->document() != nullptr;
}

int ViewWidget::content_height() const
{
    if (!m_container || !m_container->document()) return 0;
    return (int)m_container->document()->height();
}

int ViewWidget::content_width() const
{
    if (!m_container || !m_container->document()) return 0;
    return (int)m_container->document()->width();
}

void ViewWidget::scroll_to(int y)
{
    int max_y = std::max(0, content_height() - h());
    m_scroll_y = std::max(0, std::min(y, max_y));
    redraw();
}

void ViewWidget::scroll_to_x(int x)
{
    int max_x = std::max(0, content_width() - w());
    m_scroll_x = std::max(0, std::min(x, max_x));
    redraw();
}

void ViewWidget::draw()
{
    // 背景
    fl_color(FL_WHITE);
    fl_rectf(x(), y(), w(), h());

    if (has_content())
    {
        auto doc = m_container->document();

        // 是否需要垂直 / 水平滚动条
        const int sb_w = 10;   // 垂直滚动条宽度
        const int sb_h = 10;   // 水平滚动条高度
        int total_h = content_height();
        int total_w = content_width();
        bool need_v = total_h > h();
        bool need_h = total_w > w();

        // 视图内容区（扣除另一方向滚动条后的可见矩形）
        int content_w = need_v ? w() - sb_w : w();
        int content_h = need_h ? h() - sb_h : h();

        // litehtml 的 draw(hdc, x, y, clip) 把文档原点偏移到屏幕坐标 (x, y)，
        // clip 也是屏幕坐标系下的可见矩形。
        litehtml::position clip;
        clip.x      = litehtml::pixel_t((float)x());
        clip.y      = litehtml::pixel_t((float)y());
        clip.width  = litehtml::pixel_t((float)content_w);
        clip.height = litehtml::pixel_t((float)content_h);

        fl_push_clip(x(), y(), content_w, content_h);
        doc->draw((litehtml::uint_ptr)0,
                  (float)(x() - m_scroll_x),
                  (float)(y() - m_scroll_y),
                  &clip);
        fl_pop_clip();

        // 垂直滚动条
        if (need_v)
        {
            int sb_x = x() + w() - sb_w;
            int vis_h = content_h;   // 扣除水平滚动条后的可视高度
            int thumb_h = std::max(20, vis_h * vis_h / total_h);
            int max_y = std::max(1, total_h - vis_h);
            int thumb_y = y() + m_scroll_y * (vis_h - thumb_h) / max_y;

            fl_color(m_trackColor);
            fl_rectf(sb_x, y(), sb_w, vis_h);
            fl_color(m_thumbColor);
            fl_rectf(sb_x, thumb_y, sb_w, thumb_h);
        }

        // 水平滚动条
        if (need_h)
        {
            int sb_y = y() + h() - sb_h;
            int vis_w = content_w;   // 扣除垂直滚动条后的可视宽度
            int thumb_w = std::max(20, vis_w * vis_w / total_w);
            int max_x = std::max(1, total_w - vis_w);
            int thumb_x = x() + m_scroll_x * (vis_w - thumb_w) / max_x;

            fl_color(m_trackColor);
            fl_rectf(x(), sb_y, vis_w, sb_h);
            fl_color(m_thumbColor);
            fl_rectf(thumb_x, sb_y, thumb_w, sb_h);
        }

        // 右下角两滚动条交汇处的小方块
        if (need_v && need_h)
        {
            fl_color(m_trackColor);
            fl_rectf(x() + w() - sb_w, y() + h() - sb_h, sb_w, sb_h);
        }
    }
    else
    {
        // 空内容：渲染中显示提示；否则纯白（无导航界面）
        if (m_rendering) {
            const char* msg = "Rendering...";
            fl_font(FL_HELVETICA, 18);
            fl_color(fl_rgb_color(160, 160, 160));
            int cx = x() + w() / 2;
            int cy = y() + h() / 2;
            fl_draw(msg, cx - (int)fl_width(msg) / 2, cy);
        }
    }
}

void ViewWidget::resize(int X, int Y, int W, int H)
{
    int old_w = w();
    Fl_Widget::resize(X, Y, W, H);
    if (!m_container) return;

    // 同步容器视口尺寸
    m_container->set_viewport_size(W, H);

    // 自动换行模式：视图宽度变化时让文档按新宽度重新排版，
    // 文字到边界就换行，不会出现水平滚动条。
    if (m_wrap && m_container->document() && W != old_w) {
        m_container->document()->render(litehtml::pixel_t((float)W));
        m_scroll_x = 0;   // 重排后水平滚动重置
    } else {
        // 固定宽度模式：文档不重排，仅裁剪滚动位置避免越界
        int max_x = std::max(0, content_width() - W);
        if (m_scroll_x > max_x) m_scroll_x = max_x;
    }
    // 同理裁剪垂直滚动位置
    int max_y = std::max(0, content_height() - H);
    if (m_scroll_y > max_y) m_scroll_y = max_y;

    // 只重绘自身（不再整窗 damage：手动布局下每次分隔条拖动会
    // 触发全窗口重绘导致菜单栏闪烁；自身新旧区域由 FLTK 处理）
    redraw();
}

int ViewWidget::handle(int event)
{
    switch (event)
    {
    case FL_PUSH:
    {
        m_drag_start_x = Fl::event_x();
        m_drag_start_y = Fl::event_y();
        m_drag_start_scroll_x = m_scroll_x;
        m_drag_start_scroll_y = m_scroll_y;

        const int sb_w = 10;
        const int sb_h = 10;
        int mx = Fl::event_x();
        int my = Fl::event_y();
        bool in_vscroll = (mx >= x() + w() - sb_w);
        bool in_hscroll = (my >= y() + h() - sb_h);

        if (in_vscroll && has_content()) {
            int total_h = content_height();
            int vis_h = (content_width() > w()) ? h() - sb_h : h();
            int thumb_h = std::max(20, vis_h * vis_h / total_h);
            int max_y_scroll = std::max(1, total_h - vis_h);
            int thumb_y = y() + m_scroll_y * (vis_h - thumb_h) / max_y_scroll;
            if (my >= thumb_y && my <= thumb_y + thumb_h) {
                m_dragging_vscroll = true;
                return 1;
            }
        }
        if (in_hscroll && has_content()) {
            int total_w = content_width();
            int vis_w = (content_height() > h()) ? w() - sb_w : w();
            int thumb_w = std::max(20, vis_w * vis_w / total_w);
            int max_x_scroll = std::max(1, total_w - vis_w);
            int thumb_x = x() + m_scroll_x * (vis_w - thumb_w) / max_x_scroll;
            if (mx >= thumb_x && mx <= thumb_x + thumb_w) {
                m_dragging_hscroll = true;
                return 1;
            }
        }
    }
    break;

    case FL_DRAG:
    {
        if (!has_content()) break;

        // 垂直滚动条拖动
        if (m_dragging_vscroll) {
            int total_h = content_height();
            int sb_h = 10;
            int vis_h = (content_width() > w()) ? h() - sb_h : h();
            int thumb_h = std::max(20, vis_h * vis_h / total_h);
            int max_y_scroll = std::max(1, total_h - vis_h);
            int dy = Fl::event_y() - m_drag_start_y;
            int delta_scroll = dy * max_y_scroll / (vis_h - thumb_h);
            scroll_to(m_drag_start_scroll_y + delta_scroll);
            return 1;
        }

        // 水平滚动条拖动
        if (m_dragging_hscroll) {
            int total_w = content_width();
            int sb_w = 10;
            int vis_w = (content_height() > h()) ? w() - sb_w : w();
            int thumb_w = std::max(20, vis_w * vis_w / total_w);
            int max_x_scroll = std::max(1, total_w - vis_w);
            int dx = Fl::event_x() - m_drag_start_x;
            int delta_scroll = dx * max_x_scroll / (vis_w - thumb_w);
            scroll_to_x(m_drag_start_scroll_x + delta_scroll);
            return 1;
        }

        // 内容区域触摸拖动
        const int sb_w = 10;
        const int sb_h = 10;
        int mx = Fl::event_x();
        int my = Fl::event_y();
        bool in_vscroll = (mx >= x() + w() - sb_w);
        bool in_hscroll = (my >= y() + h() - sb_h);
        if (in_vscroll || in_hscroll) {
            break;
        }
        int dx = Fl::event_x() - m_drag_start_x;
        int dy = Fl::event_y() - m_drag_start_y;
        scroll_to(m_drag_start_scroll_y - dy);
        scroll_to_x(m_drag_start_scroll_x - dx);
        return 1;
    }

    case FL_RELEASE:
        m_dragging_vscroll = false;
        m_dragging_hscroll = false;
        break;

    case FL_MOUSEWHEEL:
    {
        // Ctrl+滚轮 → 字体缩放（预览）
        if (Fl::event_ctrl()) {
            if (m_zoom_cb) {
                float delta = (Fl::event_dy() < 0) ? 1.1f : (1.0f / 1.1f);
                m_zoom_cb(delta);
            }
            return 1;
        }
        // Shift+滚轮 或 触控板水平滚轮（event_dx != 0）→ 水平滚动
        int dx = Fl::event_dx();
        if (Fl::event_shift() || dx != 0) {
            int step = (dx != 0) ? dx * 40 : Fl::event_dy() * 40;
            scroll_to_x(m_scroll_x + step);
        } else {
            scroll_to(m_scroll_y + Fl::event_dy() * 40);
        }
        return 1;
    }

    case FL_DND_ENTER:
    case FL_DND_DRAG:
        return 1;
    case FL_DND_RELEASE:
        return 1;
    case FL_PASTE:
    {
        const char* fname = Fl::event_text();
        if (fname && *fname) {
            if (m_open_cb) m_open_cb(fname);
            return 1;
        }
        break;
    }
    }
    return Fl_Widget::handle(event);
}

// ==================== MainWindow ====================
