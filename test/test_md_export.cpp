// test_md_export.cpp - "Open in browser" export must be self-contained.
//
// The preview panel caches remote images as <exe>/temp/ricia_img_%08x.img
// (FNV-1a of the URL, see mdview/container.cpp). localize_remote_images()
// copies those bytes into the preview dir and rewrites <img src> to a local
// relative file, so index.html renders with no network round-trip at all:
// gitee's raw endpoint answers with a 302 to a ~13-minute signed URL on
// raw.giteeusercontent.com, and any network that cannot follow that hop
// shows a broken image even though the in-app preview displays it fine.
//
// Covered: cache hit -> rewrite + copy with the correct extension, cache miss
// -> remote URL kept verbatim, HTML-escaped "&" resolving to the same cache
// file, unknown extension -> kept remote, repeated use of one picture.
#include "test_assert.h"
#include "mdview/image_export.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace fs = std::filesystem;

static fs::path g_build;

// 不再模拟"temp\*.img 缓存"—— 那套缓存已经不存在了：远程图现在直接落在
// 产物目录里、用 URL 原始文件名（见 remote_image_file_name）。测试要做的
// 是在 g_build 里预置一个该名字的文件，模拟"渲染时已下载好"，然后断言
// localize_remote_images 复用它、不触网、也不改名。
static std::string dest_path(const std::string& url)
{
    const std::string name = remote_image_file_name(url);
    return name.empty() ? std::string()
                        : (g_build / name).string();
}

// 不再复刻被测代码的命名算法 —— 那等于把实现抄一遍，改了实现测试就跟着
// 一起错，守不住任何东西。改为按契约断言：输出目录里恰好多出一个指定扩展名
// 的文件，且 HTML 里引用的就是那个文件名。
//
// 命名契约本身（沿用 URL 原名，不用哈希）由 test_export_e2e 断言。
static std::string only_ext_in(const fs::path& dir, const char* ext)
{
    std::string found;
    int n = 0;
    const size_t eLen = std::strlen(ext);
    for (const auto& e : fs::directory_iterator(dir)) {
        const std::string fn = e.path().filename().string();
        if (fn.size() > eLen &&
            fn.compare(fn.size() - eLen, eLen, ext) == 0) {
            found = fn;
            ++n;
        }
    }
    return n == 1 ? found : std::string();
}

static std::string only_png_in(const fs::path& dir)
{
    return only_ext_in(dir, ".png");
}

// 在产物目录里预置一张"渲染时已下载好"的图。必须在 make_build_dir()【之后】
// 调用 —— 后者会 remove_all 整个 g_build，顺序反了预置的文件会被自己删掉。
static void write_cache(const std::string& url, const std::string& bytes)
{
    const std::string p = dest_path(url);
    if (p.empty()) return;
    std::ofstream f(fs::path(p), std::ios::binary);
    f << bytes;
}

static std::string read_all(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void make_build_dir()
{
    g_build = fs::temp_directory_path() / "pecia_md_export_test";
    fs::remove_all(g_build);
    fs::create_directories(g_build);
}

static std::string page(const std::string& body)
{
    return "<!DOCTYPE html><html><body>" + body + "</body></html>\n";
}

static void test_cache_hit_is_localized()
{
    const std::string url = "https://md-export-test.invalid/pic.png";
    const std::string payload = "FAKE-PNG-BYTES";
    // 先建目录再预置文件：make_build_dir() 会 remove_all，顺序反了缓存会被
    // 自己删掉，测试就成了"缓存未命中"（曾经真的踩过）。
    make_build_dir();
    write_cache(url, payload);

    std::string html = page("<img src=\"" + url + "\" alt=\"p\">");
    localize_remote_images(html, g_build.string());

    std::string local = only_png_in(g_build);
    CHECK(!local.empty());
    CHECK(html.find(local) != std::string::npos);
    CHECK(html.find(url) == std::string::npos);
    CHECK(fs::exists(g_build / local));
    CHECK(read_all(g_build / local) == payload);

    fs::remove_all(g_build);
}

// A cache miss must NOT reach the network on the render path (fetch omitted),
// otherwise every keystroke-triggered re-render would download images.
static void test_cache_miss_keeps_url()
{
    const std::string url = "https://md-export-test.invalid/missing.png";
    make_build_dir();

    std::string html = page("<img src=\"" + url + "\">");
    localize_remote_images(html, g_build.string());

    CHECK(html.find(url) != std::string::npos);
    CHECK(fs::directory_iterator(g_build) == fs::directory_iterator());

    fs::remove_all(g_build);
}

// "Open in browser" path: the snapshot on disk was written while the images
// were still downloading, so a missing cache is fetched on the spot and the
// src is localized after all.
static bool fake_fetch(const std::string& url, const std::string& dest)
{
    if (url.rfind("https://ok.invalid/", 0) != 0) return false;
    std::ofstream f(dest, std::ios::binary);
    f << "FETCHED";
    return true;
}

static void test_fetch_fills_missing_cache()
{
    const std::string url = "https://ok.invalid/late.png";
    make_build_dir();

    std::string html = page("<img src=\"" + url + "\">");
    localize_remote_images(html, g_build.string(), &fake_fetch);

    std::string local = only_png_in(g_build);
    CHECK(!local.empty());
    CHECK(html.find(local) != std::string::npos);
    CHECK(fs::exists(g_build / local));
    CHECK(read_all(g_build / local) == std::string("FETCHED"));

    fs::remove_all(g_build);
}

// A fetch that fails (offline, 404, blocked host) keeps the remote URL rather
// than writing a broken local file.
static void test_failed_fetch_keeps_url()
{
    const std::string url = "https://blocked.invalid/x.png";
    make_build_dir();

    std::string html = page("<img src=\"" + url + "\">");
    localize_remote_images(html, g_build.string(), &fake_fetch);

    CHECK(html.find(url) != std::string::npos);
    CHECK(!fs::exists(dest_path(url)));
    CHECK(fs::directory_iterator(g_build) == fs::directory_iterator());

    fs::remove_all(g_build);
}

// "&" in the URL reaches the attribute HTML-escaped as "&amp;"; the cache
// name must be derived from the decoded URL. Also covers extension recovery
// for SVG, which browsers refuse to decode from an extension-less file.
static void test_escaped_url_and_svg_extension()
{
    const std::string url = "https://md-export-test.invalid/b.svg?x=1&y=2";
    make_build_dir();
    write_cache(url, "<svg/>");

    std::string html = page("<img src=\"https://md-export-test.invalid/b.svg?x=1&amp;y=2\">");
    localize_remote_images(html, g_build.string());

    std::string local = only_ext_in(g_build, ".svg");
    CHECK(!local.empty());
    CHECK(html.find(local) != std::string::npos);
    CHECK(html.find("&amp;y=2") == std::string::npos);
    CHECK(fs::exists(g_build / local));

    fs::remove_all(g_build);
}

// A cached hit whose URL carries no usable extension keeps the remote URL:
// guessing a suffix would hand the browser bytes under the wrong decoder.
static void test_unknown_extension_keeps_url()
{
    const std::string url = "https://md-export-test.invalid/image";
    make_build_dir();
    write_cache(url, "WHATEVER");

    std::string html = page("<img src=\"" + url + "\">");
    localize_remote_images(html, g_build.string());

    CHECK(html.find(url) != std::string::npos);

    fs::remove_all(g_build);
}

// The same picture used twice must be rewritten at every occurrence.
static void test_repeated_image_rewritten()
{
    const std::string url = "https://md-export-test.invalid/twice.png";
    make_build_dir();
    write_cache(url, "PNG2");

    std::string html = page("<img src=\"" + url + "\"><p>x</p><img src=\"" + url + "\">");
    localize_remote_images(html, g_build.string());

    std::string local = only_png_in(g_build);
    CHECK(!local.empty());
    size_t first = html.find(local);
    CHECK(first != std::string::npos);
    CHECK(html.find(local, first + 1) != std::string::npos);
    CHECK(html.find(url) == std::string::npos);

    fs::remove_all(g_build);
}

// Local paths, data: URIs and formula PNGs (absolute Windows paths) must be
// left alone - they are handled by the local-copy pass in preprocess.cpp.
static void test_non_remote_sources_untouched()
{
    make_build_dir();
    std::string html = page(
        "<img src=\"local.png\">"
        "<img src=\"data:image/png;base64,iVBORw0KGgo=\">"
        "<img src=\"C:\\\\tmp\\\\formula.png\">");
    localize_remote_images(html, g_build.string());

    CHECK(html.find("\"local.png\"") != std::string::npos);
    CHECK(html.find("data:image/png;base64") != std::string::npos);
    CHECK(html.find("formula.png") != std::string::npos);

    fs::remove_all(g_build);
}

// The export dialog reports how many remote src survived localization, so the
// user learns about missing pictures instead of opening a page with holes.
// Counter semantics must match what localize_remote_images actually left
// behind: distinct URLs, de-duplicated, local/data: sources never counted.
static void test_count_remote_image_srcs()
{
    CHECK(count_remote_image_srcs("") == 0);
    CHECK(count_remote_image_srcs(page("<p>no images at all</p>")) == 0);
    CHECK(count_remote_image_srcs(page("<img src=\"a.png\"><img src=\"b.jpg\">")) == 0);

    const std::string a = "https://md-export-test.invalid/a.png";
    const std::string b = "http://md-export-test.invalid/b.svg";
    // a appears twice -> one distinct source, not two.
    std::string html = page("<img src=\"" + a + "\"><img src=\"" + b +
                            "\"><img src=\"" + a + "\">");
    CHECK(count_remote_image_srcs(html) == 2);

    // And it must agree with the post-localization state of the document.
    make_build_dir();
    write_cache(a, "AAA");
    localize_remote_images(html, g_build.string());
    CHECK(count_remote_image_srcs(html) == 1);   // only b (no cache) remains

    fs::remove_all(g_build);
}

int main()
{
    std::fflush(stderr);
    test_cache_hit_is_localized();
    test_cache_miss_keeps_url();
    test_fetch_fills_missing_cache();
    test_failed_fetch_keeps_url();
    test_escaped_url_and_svg_extension();
    test_unknown_extension_keeps_url();
    test_repeated_image_rewritten();
    test_non_remote_sources_untouched();
    test_count_remote_image_srcs();
    int fails = test::failCount();
    std::fprintf(stderr, "%d checks, %d failures\n", test::checkCount(), fails);
    return fails ? 1 : 0;
}
