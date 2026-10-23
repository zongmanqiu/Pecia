#include "container.h"
#include "preprocess.h"
#include "core/I18n.h"        // localized render-failure messages
#include "editor/Editor.h"   // FontUtils::findFontByName（CSS 字体名 → 已安装字体）
#include "core/PathUtils.h"  // pathutil::exeDir()（便携：缓存写 exe 同级 temp/）
#include "image_export.h"    // remote_image_file_name（远程图命名，与导出一致）

#include <FL/Fl.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>      // floorf()：边框像素量化
#include <cstdio>
#include <cstdlib>    // getenv：PECIA_PROFILE
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>
#include "mmdr_ffi.h"

// Forward declaration: process-level singleton placeholder used for images
// that fail to load. Defined later in this file.
static Fl_Image* unsupportedImage();

// ---------- 阶段耗时诊断 ----------
// 只在 PECIA_PROFILE=1 时写文件；关闭时不碰磁盘、不取系统时间以外的成本。
// 输出位置选 exe 同级（与 drafts/ 一致），避免污染 main/。
static bool mdv_prof_on()
{
    static const bool on = [] {
        const char* v = getenv("PECIA_PROFILE");
        return v && *v && *v != '0';
    }();
    return on;
}

void mdv_prof_ms(const char* tag, double ms)
{
    if (!mdv_prof_on()) return;
    static FILE* fp = [] {
        std::string p = pathutil::exeDir() + "/preview_profile.log";
        return fopen(p.c_str(), "a");
    }();
    if (!fp) return;
    fprintf(fp, "[prof] %-28s %9.1f ms\n", tag, ms);
    fflush(fp);
}

void mdv_prof_int(const char* tag, long long value)
{
    if (!mdv_prof_on()) return;
    static FILE* fp = [] {
        std::string p = pathutil::exeDir() + "/preview_profile.log";
        return fopen(p.c_str(), "a");
    }();
    if (!fp) return;
    fprintf(fp, "[prof] %-28s %9lld\n", tag, value);
    fflush(fp);
}

void mdv_prof_str(const char* tag, const std::string& text)
{
    if (!mdv_prof_on()) return;
    static FILE* fp = [] {
        std::string p = pathutil::exeDir() + "/preview_profile.log";
        return fopen(p.c_str(), "a");
    }();
    if (!fp) return;
    fprintf(fp, "[prof] %-20s %s\n", tag, text.c_str());
    fflush(fp);
}

// 文本测量统计（PECIA_PROFILE 下才累加）：parse 阶段的耗时几乎全在这里，
// 只报一个总数无法区分「缓存没命中」和「命中但 lookup 本身慢」。
static size_t g_tw_calls = 0;
static size_t g_tw_hits  = 0;
static double g_tw_miss_ms = 0.0;

void mdv_prof_tw_reset()
{
    g_tw_calls = g_tw_hits = 0;
    g_tw_miss_ms = 0.0;
}

void mdv_prof_tw_report(const char* phase)
{
    if (!mdv_prof_on()) return;
    char buf[160];
    snprintf(buf, sizeof(buf), "text_width[%s] calls=%zu hits=%zu miss=%.1fms",
             phase, g_tw_calls, g_tw_hits, g_tw_miss_ms);
    mdv_prof_ms(buf, 0.0);
}


// 两个矩形是否相交。用于视口裁剪的「完全不相交就跳过」判断。
// 边界相接（a.right()==b.x）按不相交处理——面积为零，画了也看不见。
static inline bool rects_intersect(const litehtml::position& a, const litehtml::position& b)
{
    return a.x < b.right() && b.x < a.right() &&
           a.y < b.bottom() && b.y < a.bottom();
}

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

    // 清除缩放缓存（走 clear_scaled 保持 LRU 链与字节计数一致）
    clear_scaled();

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
    if (!fi || !text || !*text) return litehtml::pixel_t(0);

    // 键用 (文本, face, size) 而不是 FontInfo* —— FontInfo 会在 delete_font
    // 后释放，地址可能被下一个字体复用（见 text_width_cache.h 的说明）。
    //
    // 收益口径注意：这条缓存优化的是**打开文档**，不是重排。实测 166 KB 的 md
    // 在 parse 阶段调用 text_width 33,839 次（873 ms 里的 732 ms），而去重后
    // 只有 4,478 个；render / re-render 阶段调用次数为 0。
    // 加缓存后 parse 873 → 216 ms（4.0x），命中率 86.8%。
    float w = 0.0f;
    if (m_textWidthCache.lookup(text, (int)fi->face, fi->size, w))
    {
        if (mdv_prof_on()) { ++g_tw_calls; ++g_tw_hits; }
        return litehtml::pixel_t(w);
    }

    auto tw0 = std::chrono::steady_clock::now();
    fl_font(fi->face, fi->size);
    w = (float)fl_width(text);
    if (mdv_prof_on()) {
        ++g_tw_calls;
        g_tw_miss_ms += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - tw0).count();
    }
    m_textWidthCache.store(text, (int)fi->face, fi->size, w);
    return litehtml::pixel_t(w);
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
        if (self->m_pendingRemoteDownloads > 0) --self->m_pendingRemoteDownloads;
        if (self->m_pendingRemoteDownloads == 0 && self->m_remote_settled_cb)
            self->m_remote_settled_cb();
    }
    delete task;
}

void MyContainer::remote_download_done_cb(void* data)
{
    auto* task = static_cast<RemoteDone*>(data);
    if (!task) return;
    std::shared_ptr<MyContainer> self = task->wc.lock();
    if (self) {
        // 按类型加载缓存文件（与"缓存已存在"路径共用同一分派函数）
        Fl_Image* img = self->load_cached_image(task->cache_file, task->url);
        if (img) self->m_images[task->url] = img;
        else self->m_images[task->url] = unsupportedImage();   // 下载成功但解析失败 → 占位
        if (self->m_remote_done_cb) self->m_remote_done_cb();
        // 放在重绘/重排之后：只有最后一张下载结束才通知，scheduleTrimSoon
        // 再做 2s 去抖。慢网下不会在图片还在陆续到达时提前把页面踢出工作集。
        if (self->m_pendingRemoteDownloads > 0) --self->m_pendingRemoteDownloads;
        if (self->m_pendingRemoteDownloads == 0 && self->m_remote_settled_cb)
            self->m_remote_settled_cb();
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

// URL 百分号解码：md4c-html 对 src/href 里所有非 ASCII 字节（中文、空格、
// 反斜杠等）做 %XX 编码（md4c-html.c: NEED_URL_ESC），而磁盘文件名是解码后的
// 原文 —— 不解码，_wfopen 永远找不到带中文/空格的本地图片。
// 只在出现合法 %XX（两位十六进制）时解码；孤立 '%'（如 "100%.png"）原样保留。
static std::string url_decode(const std::string& s)
{
    if (s.find('%') == std::string::npos) return s;
    auto hexval = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int hi = hexval(s[i + 1]), lo = hexval(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

std::string MyContainer::resolve_image_path(const std::string& url, const std::string& base_url) const
{
    // 远程 URL 直接返回（浏览器语义：URL 保持编码形态，下载后按原名落盘）
    if (is_remote_url(url)) return url;

    // 本地路径：先做 URL 百分号解码（md4c-html 对 src 里的中文/空格等
    // 非 ASCII 字节输出 %XX，磁盘文件名是解码后的原文，见上方 url_decode）。
    std::string u = url_decode(url);

    // 绝对路径直接返回
    if (!u.empty() && u[0] == '/') return u;
    if (u.size() > 2 && u[1] == ':') return u; // e.g. C:\...

    // 相对路径：base_url + url
    if (!base_url.empty()) {
        // base_url 可能是目录（以 / 结尾）或文件
        std::string base = base_url;
        if (base.back() != '/' && base.back() != '\\')
            base = base.substr(0, base.find_last_of("/\\") + 1);
        return base + u;
    }
    return u;
}

// 按 URL 类型分派加载缓存文件：SVG 用 resvg 渲染，GIF 走多帧动画，
// 其余按位图（stb）。远程"下载完成"与"缓存已存在"两条路径共用本函数——
// 若两条路径各自分派，一旦漏掉 SVG 就会出现"首次显示、再次打开变占位"
// （stb 无法解析 SVG 文本）。
Fl_Image* MyContainer::load_cached_image(const std::string& cache_file,
                                         const std::string& url)
{
    if (is_svg_path(url)) {
        std::string svg_text = read_file(cache_file.c_str());
        return svg_text.empty() ? nullptr : load_svg_text(svg_text);
    }
    if (is_gif_path(url)) return load_gif_file(cache_file, url);
    return load_bitmap_file(cache_file);
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
        //
        // 下载目标仍然是【预览产物目录本身】（temp/<文档名>-<PID>/），文件名
        // 用 URL 原始名字与真实扩展名。这个目录同时是浏览器/导出的资源目录，
        // 所以下游契约（可读名、每张图一份、自包含导出）完全不变。
        //
        // 唯一新增的是【取字节】的来源：先查跨启动的 temp/remote-cache。
        // 命中时把缓存文件 hardlink/copy 成资产目录里的同名文件，后续仍走
        // load_cached_image()；未命中才真正下载。缓存只存字节，不改变预览、
        // 导出、浏览器打开的任何判断。手动刷新（Ctrl+R）会跳过缓存并重新下载。
        //
        // 便携：目录一律在 exe 之下，绝不写 exe 之外（%TEMP% 会污染系统临时
        // 目录，与"便携程序"目标冲突）。拿不到目录则放弃下载。
        std::string cache_dir = m_asset_dir;
        if (cache_dir.empty()) cache_dir = pathutil::exeDir() + "\\temp";
        while (!cache_dir.empty() &&
               (cache_dir.back() == '\\' || cache_dir.back() == '/'))
            cache_dir.pop_back();

        const std::string local_name = remote_image_file_name(path);
        if (!cache_dir.empty() && !local_name.empty()) {
            CreateDirectoryW(widen(cache_dir).c_str(), NULL);
            std::string cache_file = cache_dir + "\\" + local_name;
            const bool force = m_force_remote_refresh;

            if (!force &&
                GetFileAttributesW(widen(cache_file).c_str()) != INVALID_FILE_ATTRIBUTES) {
                // 本次运行已下载/物化过 → 直接加载。必须走统一分派：
                // 这里若只判 GIF，SVG 会被误送进 stb_image 而变成占位图
                //（首次能显示、之后不能）。
                img = load_cached_image(cache_file, path);
            } else if (!force &&
                       copy_remote_image_from_cache(path, cache_file,
                                                   kRemoteImageCacheTtlSeconds)) {
                // 跨启动缓存命中：文件已经物化成资产目录里的正常名字。
                img = load_cached_image(cache_file, path);
            } else {
                if (force) DeleteFileW(widen(cache_file).c_str());

                // 后台下载：任务持 weak_ptr（容器析构后回调为空操作）。
                // 有可信扩展名时先落到 remote-cache 的 staging 文件，成功后
                // 原子发布并物化；没有扩展名时保持旧行为，直接下到资产目录。
                ++m_pendingRemoteDownloads;
                auto wp = shared_from_this();
                std::thread([wp, path, cache_file]() {
                    std::string staging = remote_image_cache_file(path);
                    const bool cached = !staging.empty();
                    if (cached) {
                        ensure_remote_image_cache_dir();
                        staging += ".download-" +
                                   std::to_string(GetCurrentProcessId()) + "-" +
                                   std::to_string(GetCurrentThreadId()) + ".tmp";
                    } else {
                        staging = cache_file;
                    }
                    DeleteFileW(widen(staging).c_str());

                    HRESULT hr = URLDownloadToFileW(nullptr, widen(path).c_str(),
                                                    widen(staging).c_str(), 0, nullptr);
                    if (SUCCEEDED(hr)) {
                        if (cached) {
                            commit_remote_image_cache(path, staging, cache_file);
                            DeleteFileW(widen(staging).c_str());   // best-effort cleanup
                        }
                        auto task = new RemoteDone{wp, path, cache_file};
                        Fl::awake(remote_download_done_cb, task);
                    } else {
                        DeleteFileW(widen(staging).c_str());
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

namespace {

// Fl_Image 的像素占用（字节）。d() 是每像素字节数，RGB/RGBA 图通常为 3 或 4；
// 返回 0 时按 4 估（宁可多算，让 LRU 保守一点）。
inline size_t image_bytes(const Fl_Image* img)
{
    if (!img || img->w() <= 0 || img->h() <= 0) return 0;
    int d = img->d() > 0 ? img->d() : 4;
    return static_cast<size_t>(img->w()) * static_cast<size_t>(img->h()) *
           static_cast<size_t>(d);
}

}  // namespace

Fl_Image* MyContainer::find_scaled(const std::string& key)
{
    auto it = m_scaledImages.find(key);
    if (it == m_scaledImages.end()) return nullptr;

    // 命中 → 提到 MRU。std::list::splice 不使迭代器失效，所以索引不必更新。
    auto iit = m_scaledLruIndex.find(key);
    if (iit != m_scaledLruIndex.end() && iit->second != std::prev(m_scaledLru.end()))
        m_scaledLru.splice(m_scaledLru.end(), m_scaledLru, iit->second);
    return it->second;
}

void MyContainer::drop_scaled(const std::string& key)
{
    auto it = m_scaledImages.find(key);
    if (it != m_scaledImages.end()) {
        m_scaledBytes -= image_bytes(it->second);
        delete it->second;
        m_scaledImages.erase(it);
    }
    auto iit = m_scaledLruIndex.find(key);
    if (iit != m_scaledLruIndex.end()) {
        m_scaledLru.erase(iit->second);
        m_scaledLruIndex.erase(iit);
    }
}

void MyContainer::insert_scaled(const std::string& key, Fl_Image* img)
{
    // 已存在同 key（GIF 换帧等）→ 先删旧的，避免计数错乱
    if (m_scaledImages.count(key)) drop_scaled(key);

    const size_t need = image_bytes(img);
    // 淘汰最久未用的单条，直到能放下。注意是「淘汰一条」而不是「全清」：
    // 全清会让图片数刚超过上限的文档进入「每帧重建全部」的抖动。
    while (!m_scaledLru.empty() &&
           (m_scaledBytes + need > kScaledCacheMaxBytes ||
            m_scaledLru.size() + 1 > kScaledCacheMaxCount))
    {
        drop_scaled(m_scaledLru.front());
    }

    m_scaledImages[key] = img;
    m_scaledLru.push_back(key);
    m_scaledLruIndex[key] = std::prev(m_scaledLru.end());
    m_scaledBytes += need;
}

void MyContainer::clear_scaled()
{
    for (auto& [k, img] : m_scaledImages) delete img;
    m_scaledImages.clear();
    m_scaledLru.clear();
    m_scaledLruIndex.clear();
    m_scaledBytes = 0;
}

void MyContainer::draw_image(litehtml::uint_ptr /*hdc*/, const litehtml::background_layer& layer,
                             const std::string& url, const std::string& base_url)
{
    // 视口裁剪：完全落在可见矩形外的图直接跳过 —— 不解码、不缩放、不绘制。
    //
    // litehtml 的 draw_children() 只按 is_visible()（display/visibility）过滤，
    // 不做视口裁剪，整篇文档的每个元素每帧都会走到这里。没有这道门，一篇 100 张
    // 图的文档滚动时，屏幕外的图也会每帧被复制成 1.65 MB 的缩放副本。
    //
    // 只排除「完全不相交」的情况：跨视口边界的图照常绘制，不会缺一块。
    const litehtml::position& box = layer.border_box;
    ++m_draw_image_calls;
    if (m_has_view_clip && box.width > 0 && box.height > 0 &&
        !rects_intersect(box, m_view_clip))
    {
        ++m_draw_image_skipped;
        return;
    }

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

    // 布局框与图片源尺寸不同时，按布局框缩放绘制。
    // Fl_Image::draw 的 W/H 参数是裁剪语义（源图大于目标时只画左上角），
    // 因此先 copy 出目标尺寸（BILINEAR 缩放）再 1:1 绘制，结果按目标尺寸缓存。
    Fl_Image* draw_img = img;
    int bw = (int)box.width, bh = (int)box.height;
    if (bw > 0 && bh > 0 && (bw != img->w() || bh != img->h())) {
        std::string key = full + "@" + std::to_string(bw) + "x" + std::to_string(bh);
        if (Fl_Image* cached = find_scaled(key)) {
            draw_img = cached;
        } else {
            draw_img = img->copy(bw, bh);
            insert_scaled(key, draw_img);
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
        // 帧更新后旧缩放缓存失效（key 前缀 = 路径@）。
        // 必须走 drop_scaled() 而不是直接 erase —— LRU 链与字节计数要同步，
        // 否则 m_scaledBytes 会虚高，淘汰逻辑提前触发。
        std::string prefix = gif->cache_key + "@";
        std::vector<std::string> stale;
        for (const auto& [k, si] : gif->container->m_scaledImages) {
            if (k.compare(0, prefix.size(), prefix) == 0) stale.push_back(k);
        }
        for (const auto& k : stale) gif->container->drop_scaled(k);
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

    // 像素量化：坐标四舍五入，边框宽度最小 1px。
    // 原来一律 (int) 直接截断：CSS 的 1px 边框在字体缩放(0.6~2.5)后
    // 常落在 x.5 这类位置上，截断使相邻单元格的同一条网格线错开一个
    // 像素（或各画一条、叠加成 2px），于是表格线看起来粗细不一。
    // 统一用同一个量化函数，保证同一条网格线落在同一像素行/列上。
    auto px   = [](float v) { return (int)floorf(v + 0.5f); };
    auto bwid = [](float v) { int w = (int)floorf(v + 0.5f); return w < 1 ? 1 : w; };
    auto setcol = [](const litehtml::border& b) {
        fl_color(fl_rgb_color(b.color.red, b.color.green, b.color.blue));
    };

    int x = px(draw_pos.x);
    int y = px(draw_pos.y);
    int w = px(draw_pos.width);
    int h = px(draw_pos.height);
    if (w <= 0 || h <= 0) return;

    int bt = visible(borders.top)    ? bwid(borders.top.width)    : 0;
    int bb = visible(borders.bottom) ? bwid(borders.bottom.width) : 0;
    int bl = visible(borders.left)   ? bwid(borders.left.width)   : 0;
    int br = visible(borders.right)  ? bwid(borders.right.width)  : 0;

    if (bt) {
        if (bt > h) bt = h;
        setcol(borders.top);
        fl_rectf(x, y, w, bt);
    }
    if (bb && h > bt) {
        int yy = y + h - bb;
        int hh = bb;
        // 高度不足时退让，避免 bottom 反盖住 top 造成角点加深
        if (yy < y + bt) { yy = y + bt; hh = h - bt; }
        if (hh > 0) { setcol(borders.bottom); fl_rectf(x, yy, w, hh); }
    }

    // 左右边框夹在上下边框之间，避免四角被画两次（叠加处颜色加深）
    int innerY = y + bt;
    int innerH = h - bt - bb;
    if (innerH <= 0) return;
    if (bl) {
        if (bl > w) bl = w;
        setcol(borders.left);
        fl_rectf(x, innerY, bl, innerH);
    }
    if (br && w > bl) {
        int xx = x + w - br;
        int ww = br;
        if (xx < x + bl) { xx = x + bl; ww = w - bl; }
        if (ww > 0) { setcol(borders.right); fl_rectf(xx, innerY, ww, innerH); }
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
    if (url[0] == '#' && m_anchor_cb) {
        // 分块模式：没有单一 document，交给视图在块表里找（见 BlockLayout）。
        // 全量模式下 m_doc 非空，走下面的元素级精确定位更好，所以这条在前、
        // 但不 return —— 由下面的分支决定是否还需要做像素级定位。
        if (!m_doc) {
            m_anchor_cb(std::string(url + 1));
            return;
        }
    }
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
    ++m_clip_depth;
    fl_push_clip((int)pos.x, (int)pos.y,
                 (int)pos.width, (int)pos.height);
}
void MyContainer::del_clip()
{
    // 没有配对的 set_clip 时**绝不能** fl_pop_clip()：FLTK 的裁剪栈是全局的，
    // 多弹一次就会把调用方（ViewWidget::draw 里那层「只画面板可视区」的
    // push_clip）弹掉，之后的绘制要么画到面板外，要么落进一个错误的裁剪区。
    // 表现就是分块预览里「整块内容全是空白」。计数与弹栈必须同进同退。
    if (m_clip_depth <= 0) return;
    --m_clip_depth;
    fl_pop_clip();
}

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
    // 分块模式（大文档）下正文里的 #anchor 链接由视图自己在块表里定位。
    if (m_container) {
        m_container->set_anchor_callback([this](const std::string& id) {
            if (scroll_to_anchor(id)) redraw();
        });
    }
}

void ViewWidget::load_and_render(const std::string& html)
{
    if (!m_container) return;
    // 取消可能挂起的防抖重排——新文档已加载，旧宽度的重排无意义。
    cancel_deferred_render();
    // Litehtml parsing and layout allocate per-node DOM structures: a huge
    // document can throw bad_alloc here. The preview no longer has a size
    // limit (see MainWindow.h), so this path is reachable — and an escaping
    // exception would unwind straight out of the FLTK event loop and take the
    // whole process (with every other tab's unsaved work) down with it.
    // Report it in the panel instead.
    try {
        // 内容变了 = 换文档：上一个文档的图片解码结果与文本测量结果全部作废。
        // 内容没变只是重排（拖分隔条 / 字体缩放）时**不能清** —— 清了就得把
        // 所有图片重新解码、所有文本重新测量一遍，滚动和拖拽会卡一下。
        if (m_current_html != html) {
            m_container->clear_images();
            m_container->clear_text_width_cache();
        }
        m_current_html = html;
        mdv_prof_tw_reset();

        // 大文档先试分块。一个 document 的内存是「节点数 × 约 1.3 KB」，1 MB 的
        // markdown 会到 436 MB；分块后只保视口附近几块（原型实测 38 MB）。
        // 分块失败（结构异常 / 覆盖度不足）时静默退回全量路径，行为与从前一致。
        if (html.size() >= kChunkThreshold && render_chunked(html))
        {
            m_renderError.clear();
            redraw();
            return;
        }

        // 全量路径：清掉上一次可能存在的分块状态。否则 has_content()/draw()
        // 会继续按分块分支走，而 content_height() 用的是旧的块表。
        if (m_blocks && m_blocks->active()) m_blocks->clear();

        auto tp0 = std::chrono::steady_clock::now();
        auto doc = litehtml::document::createFromString(html.c_str(), m_container);
        auto tp1 = std::chrono::steady_clock::now();
        mdv_prof_tw_report("parse");
        mdv_prof_ms("parse(createFromString)", std::chrono::duration<double, std::milli>(tp1 - tp0).count());
        if (!doc) return;
        m_container->set_document(doc);
        m_scroll_y = 0;
        m_scroll_x = 0;
        doc->render(litehtml::pixel_t((float)w()));
        auto tp2 = std::chrono::steady_clock::now();
        mdv_prof_ms("layout(render)", std::chrono::duration<double, std::milli>(tp2 - tp1).count());
        m_renderError.clear();
        redraw();
    } catch (const std::bad_alloc &) {
        m_current_html.clear();
        if (m_container) m_container->set_document(nullptr);
        m_scroll_y = 0;
        m_scroll_x = 0;
        set_render_error(I18n::get("preview.fail.oom"));
    } catch (...) {
        m_current_html.clear();
        if (m_container) m_container->set_document(nullptr);
        m_scroll_y = 0;
        m_scroll_x = 0;
        set_render_error(I18n::get("preview.fail.render"));
    }
}

bool ViewWidget::render_chunked(const std::string& html)
{
    if (!m_container) return false;
    if (!m_blocks) m_blocks = std::make_unique<pecia::BlockLayout>();

    auto t0 = std::chrono::steady_clock::now();
    if (!m_blocks->split(html))
    {
        mdv_prof_int("chunk(split failed -> full path)", 1);
        m_blocks->clear();
        return false;
    }
    m_blocks->set_width(w());
    mdv_prof_ms("chunk(split)", std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - t0).count());
    mdv_prof_int("chunk(blocks)", (long long)m_blocks->count());
    mdv_prof_int("chunk(total px, estimated)", (long long)m_blocks->total_height());
    mdv_prof_int("chunk(px per Kb x1000)", (long long)(m_blocks->px_per_kb() * 1000.0));

    // 分块模式下没有「当前 document」这个概念：锚点、搜索、选中的定位全部
    // 由 BlockLayout 负责。把容器的 m_doc 清空，避免任何残留的悬垂引用。
    m_container->set_document(nullptr);
    m_scroll_y = 0;
    m_scroll_x = 0;

    auto t1 = std::chrono::steady_clock::now();
    ensure_visible_blocks(h());
    mdv_prof_ms("chunk(first window)", std::chrono::duration<double, std::milli>(
                                          std::chrono::steady_clock::now() - t1).count());
    mdv_prof_int("chunk(measured blocks)", (long long)m_blocks->measured_count());
    mdv_prof_int("chunk(total px, 1st window)", (long long)m_blocks->total_height());
    return true;
}

void ViewWidget::ensure_visible_blocks(int content_h)
{
    if (!m_blocks || !m_blocks->active() || !m_container) return;
    int first = m_blocks->block_at(m_scroll_y);
    int last = m_blocks->block_at(m_scroll_y + content_h);
    if (first < 0 || last < 0) return;
    m_blocks->ensure_window(first - pecia::BlockLayout::kPrefetchBlocks,
                            last + pecia::BlockLayout::kPrefetchBlocks, m_container);
    // 实例化/释放会刷新实测高度 → 总高变化 → 滚动位置可能已经越界。
    int max_y = std::max(0, content_height() - h());
    if (m_scroll_y > max_y) m_scroll_y = max_y;
}

void ViewWidget::draw_chunked(int content_w, int content_h, const litehtml::position& clip)
{
    if (!m_blocks || !m_container) return;

    // 定位 → 实例化 → 重新定位，直到范围稳定。
    //
    // 为什么需要这一轮：块高在实测前是估算值（px/KB 系数），估算与真实
    // 高度可能差 2~3 倍。编辑一个大文档后所有块都退回估算，block_at()
    // 定位到的块，其真实起点可能在视口下方几千像素处 —— 那里什么都没有，
    // 面板就是一片白（实测 5 MB 文档改一个字后空白 14 秒，打点显示目标块
    // 起点比视口低 3713 px，而视口只有 504 px 高）。
    //
    // 实例化一轮会把这些块测出来，系数随之校准、offset 修正，再定位就准了。
    // 三轮足够：每轮都会用实测值修正系数，偏差单调收敛。
    int first = -1, last = -1;
    for (int pass = 0; pass < 3; ++pass)
    {
        const int f = m_blocks->block_at(m_scroll_y);
        const int l = m_blocks->block_at(m_scroll_y + content_h);
        if (f < 0 || l < 0) return;
        if (pass > 0 && f == first && l == last) break;   // 范围稳定了
        first = f;
        last = l;
        const int lo = std::max(0, f - pecia::BlockLayout::kPrefetchBlocks);
        const int hi = std::min((int)m_blocks->count() - 1, l + pecia::BlockLayout::kPrefetchBlocks);
        m_blocks->ensure_window(lo, hi, m_container);
    }
    if (first < 0 || last < 0) return;

    // 先确保窗口（会实例化本屏 + 前后各一块），再逐块绘制。
    const int lo = std::max(0, first - pecia::BlockLayout::kPrefetchBlocks);
    const int hi = std::min((int)m_blocks->count() - 1, last + pecia::BlockLayout::kPrefetchBlocks);
    m_blocks->ensure_window(lo, hi, m_container);

    // 图片的视口裁剪同样按屏幕矩形生效；逐块设置一次即可。
    m_container->set_view_clip(clip);
    for (int i = first; i <= last; ++i)
    {
        const pecia::BlockLayout::Block& b = m_blocks->block((size_t)i);
        if (!b.doc) continue;
        // litehtml 的 draw(hdc, x, y, clip) 把「该块文档的原点」放到屏幕 (x, y)：
        // 每块有自己的坐标原点，所以 y 要减去滚动量再加上块自身的累计偏移。
        float top = (float)(y() - m_scroll_y + m_blocks->offset_of((size_t)i));
        // 每块自己 push 一层裁剪，而不是只靠 draw() 外层那一层。
        // 为什么必须这样（实测，不是保险起见）：litehtml 的绘制路径会把调用方
        // 的裁剪区弹掉一层 —— 只依赖外层 push 时，块内容全部画不出来（面板上
        // 只有空白），而每块自建裁剪就完全正常。按块裁剪同时也切掉了相邻块
        // 之间的越界绘制。
        int btop = (int)top;
        int bh = m_blocks->height_of((size_t)i);
        int cy0 = std::max(y(), btop);
        int cy1 = std::min(y() + content_h, btop + bh);
        fl_push_clip(x(), cy0, content_w, std::max(0, cy1 - cy0));
        b.doc->draw((litehtml::uint_ptr)0, (float)(x() - m_scroll_x), top, &clip);
        fl_pop_clip();
        if (m_container->clip_depth() != 0)
        {
            // set_clip 没配对 del_clip：FLTK 的裁剪栈已经歪了，继续画下去会把
            // 后面所有块都裁进错误区域。这里把多出来的层弹掉再继续。
            for (int guard = 0; guard < 8 && m_container->clip_depth() > 0; ++guard)
                m_container->del_clip();
        }
    }
    m_container->clear_view_clip();
}

bool ViewWidget::scroll_to_anchor(const std::string& id)
{
    if (!m_blocks || !m_blocks->active() || id.empty()) return false;
    // 块级定位：先扫块 HTML 找出 id 落在哪一块（避免为全文实例化 document），
    // 再只实例化那一块并做**块内精确偏移**。
    // 只跳到块首是不够的：一块 12 KB 内容实测约 2600–3100 px（≈3 屏），标题若
    // 在块尾部，块首的视口里根本看不到目标 —— 那等于跳错地方。
    const std::string dq = "id=\"" + id + "\"";
    const std::string sq = "id='" + id + "'";
    const auto tp0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < m_blocks->count(); ++i)
    {
        const std::string& h = m_blocks->block(i).html;
        if (h.find(dq) == std::string::npos && h.find(sq) == std::string::npos) continue;
        const auto tp1 = std::chrono::steady_clock::now();
        mdv_prof_ms("anchor(scan blocks)", std::chrono::duration<double, std::milli>(tp1 - tp0).count());

        // 实例化这一块：几何会随实测高度刷新，所以偏移必须在 ensure_window
        // 之后取，否则用的是估算值。
        m_blocks->ensure_window((int)i, (int)i, m_container);
        int y = m_blocks->anchor_y(i, id);
        if (y < 0) y = m_blocks->offset_of(i);   // 查不到元素就退回块首
        scroll_to(y);
        const auto tp2 = std::chrono::steady_clock::now();
        mdv_prof_ms("anchor(layout+find)", std::chrono::duration<double, std::milli>(tp2 - tp1).count());
        mdv_prof_ms("anchor(total)", std::chrono::duration<double, std::milli>(tp2 - tp0).count());
        return true;
    }
    return false;
}

void ViewWidget::clear()
{
    cancel_deferred_render();
    if (m_blocks) m_blocks->clear();
    m_current_html.clear();
    if (m_container) m_container->set_document(nullptr);
    m_scroll_y = 0;
    m_scroll_x = 0;
    redraw();
}

bool ViewWidget::has_content() const
{
    if (m_blocks && m_blocks->active()) return true;
    return m_container && m_container->document() != nullptr;
}

int ViewWidget::content_height() const
{
    if (m_blocks && m_blocks->active()) return m_blocks->total_height();
    if (!m_container || !m_container->document()) return 0;
    return (int)m_container->document()->height();
}

int ViewWidget::content_width() const
{
    // 分块时每块都按视口宽度排版，没有水平滚动（与 wrap 模式一致）。
    if (m_blocks && m_blocks->active()) return w();
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
        const bool chunked = m_blocks && m_blocks->active();

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
        auto dp0 = std::chrono::steady_clock::now();
        if (chunked)
        {
            // 分块：只画可见块（前后各预取一块已由 draw_chunked 实例化）。
            draw_chunked(content_w, content_h, clip);
        }
        else
        {
            auto doc = m_container->document();
            // 把这个可见矩形交给容器：draw_image() 用它做视口裁剪，跳过屏幕外的图。
            // litehtml 自己不做这一步（draw_children 只按 display/visibility 过滤），
            // 没有它整篇文档的图每帧都会被绘制/缩放一遍。
            m_container->set_view_clip(clip);
            doc->draw((litehtml::uint_ptr)0,
                      (float)(x() - m_scroll_x),
                      (float)(y() - m_scroll_y),
                      &clip);
            m_container->clear_view_clip();
        }
        auto dp1 = std::chrono::steady_clock::now();
        mdv_prof_ms("draw(doc->draw)", std::chrono::duration<double, std::milli>(dp1 - dp0).count());
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
        // 空内容：渲染中显示提示，失败时显示原因；否则纯白（无导航界面）
        if (!m_renderError.empty()) {
            // 预览无大小上限，超大文档可能在这一步（litehtml 布局）耗尽内存。
            // 必须给出可读提示 —— 静默空白用户无从判断是"太大"还是"写错了"。
            fl_font(FL_HELVETICA, 15);
            fl_color(fl_rgb_color(190, 60, 60));
            int cx = x() + w() / 2;
            int cy = y() + h() / 2;
            fl_draw(m_renderError.c_str(), cx - (int)fl_width(m_renderError.c_str()) / 2, cy);
        }
        else if (m_rendering) {
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
    // 大文档 re-render 约 50ms，拖动分隔条时连续触发会阻塞 UI 线程。
    // 防抖：延迟 120ms 后重排，拖动期间只做裁剪 + redraw（用旧布局绘制）。
    // 分块模式：宽度变化要重排每一块（每块都按视口宽布局），但只在防抖后做
    // 一次 —— 拖分隔条时每帧重排 3 块就已经是几十毫秒。
    if (m_blocks && m_blocks->active()) {
        if (W != old_w) {
            m_pending_render_width = W;
            if (!m_debounce_active) {
                Fl::add_timeout(0.12, debounce_timer_cb, this);
                m_debounce_active = true;
            }
        }
        m_scroll_x = 0;
    } else if (m_wrap && m_container->document() && W != old_w) {
        m_pending_render_width = W;
        if (!m_debounce_active) {
            Fl::add_timeout(0.12, debounce_timer_cb, this);
            m_debounce_active = true;
        }
        m_scroll_x = 0;   // 重排后水平滚动重置（立即生效，避免越界）
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

void ViewWidget::debounce_timer_cb(void* data)
{
    auto* self = static_cast<ViewWidget*>(data);
    self->m_debounce_active = false;
    self->do_deferred_render();
}

void ViewWidget::do_deferred_render()
{
    if (m_pending_render_width < 0 || !m_container)
    {
        m_pending_render_width = -1;
        return;
    }
    if (m_blocks && m_blocks->active())
    {
        // 重排前后总高会变（行数变了），按比例保住阅读位置，否则拖一下分隔条
        // 就会把用户弹回文档顶部。
        int W = m_pending_render_width;
        m_pending_render_width = -1;
        int oldTotal = m_blocks->total_height();
        float ratio = oldTotal > 0 ? (float)m_scroll_y / (float)oldTotal : 0.0f;
        m_blocks->set_width(W);
        ensure_visible_blocks(h());
        if (ratio > 0.0f) m_scroll_y = (int)(ratio * (float)content_height());
        redraw();
        return;
    }
    if (!m_container->document())
    {
        m_pending_render_width = -1;
        return;
    }
    int W = m_pending_render_width;
    m_pending_render_width = -1;
    try {
        m_container->document()->render(litehtml::pixel_t((float)W));
    } catch (const std::bad_alloc &) {
        set_render_error(I18n::get("preview.fail.oom"));
    } catch (...) {
        set_render_error(I18n::get("preview.fail.render"));
    }
    redraw();
}

void ViewWidget::cancel_deferred_render()
{
    if (m_debounce_active) {
        Fl::remove_timeout(debounce_timer_cb, this);
        m_debounce_active = false;
    }
    m_pending_render_width = -1;
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
