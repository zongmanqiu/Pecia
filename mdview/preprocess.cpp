#include "preprocess.h"
#include <md4c.h>
#include <md4c-html.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "mmdr_ffi.h"
#include "ratex_ffi.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

// 预处理阶段耗时诊断（PECIA_PROFILE=1）：追加到 %TEMP%/pecia_pp_profile.log。
// 与 container.cpp 的 mdv_prof_ms 分开实现，避免 preprocess.cpp（同时也被
// 导出/测试目标使用）依赖 FLTK。
struct PpProf {
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    void mark(const char* tag) const;
};
void PpProf::mark(const char* tag) const
{
    static const bool on = [] {
        const char* v = getenv("PECIA_PROFILE");
        return v && *v && *v != '0';
    }();
    if (!on) return;
    double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    const char* tmp = getenv("TEMP");
    std::string p = std::string(tmp ? tmp : ".") + "/pecia_pp_profile.log";
    if (FILE* f = fopen(p.c_str(), "a")) {
        fprintf(f, "[pp] %-30s %9.1f ms\n", tag, ms);
        fclose(f);
    }
};

/* md4c 回调 */static void collect_html(const MD_CHAR* chunk, MD_SIZE chunk_size, void* userdata)
{
    auto* out = static_cast<std::string*>(userdata);
    out->append(chunk, chunk_size);
}

/* Emoji 简码 → Unicode 映射 */
#include <unordered_map>

/* UTF-8 -> UTF-16：本文件内所有落盘/拷贝都经它转宽，路径源字符串
   （exe 目录、文档目录）在 UI 层已按 UTF-8 生成，中文路径安全。 */
static std::wstring widen(const std::string& utf8)
{
    if (utf8.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], n);
    while (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

// ---------------------------------------------------------------------------
// Browser-copy HTML sanitizer.
// Markdown may embed raw HTML (<script>, <iframe>, on* handlers, javascript:
// URLs). The inline litehtml preview never executes scripts, so it renders
// the markup verbatim - but the index.html written for "Open in browser"
// runs under file:// in a real browser, where those elements would execute
// with local-file privileges. Sanitize only the on-disk browser copy.
// ---------------------------------------------------------------------------
static bool ieq(const std::string& hay, size_t pos, const char* needle)
{
    size_t n = 0;
    while (needle[n]) {
        if (pos + n >= hay.size()) return false;
        if (toupper((unsigned char)hay[pos + n]) != toupper((unsigned char)needle[n]))
            return false;
        ++n;
    }
    return true;
}

// Remove one <tag ...> ... </tag> block (case-insensitive), including
// self-closing <tag .../> and unterminated opening tags.
static void stripHtmlBlock(std::string& html, const char* tag)
{
    const std::string open = "<";
    const std::string close = "</";
    size_t i = 0;
    while ((i = html.find(open, i)) != std::string::npos) {
        if (!ieq(html, i + 1, tag)) { i += 1; continue; }
        // Find the end of the opening tag.
        size_t gt = html.find('>', i);
        if (gt == std::string::npos) { html.erase(i); break; }
        // Self-closing <tag .../>.
        size_t scan = gt;
        while (scan > i && (html[scan - 1] == ' ' || html[scan - 1] == '\t')) --scan;
        if (scan > i && html[scan - 1] == '/') {
            html.erase(i, gt - i + 1);
            continue;
        }
        // Match the closing tag (case-insensitive).
        size_t ce = html.find(close, gt + 1);
        size_t cEnd = std::string::npos;
        while (ce != std::string::npos) {
            if (ieq(html, ce + 2, tag)) {
                size_t cgt = html.find('>', ce);
                cEnd = (cgt == std::string::npos) ? html.size() : cgt + 1;
                break;
            }
            ce = html.find(close, ce + 2);
        }
        // Strip the whole block; if unterminated, strip only the opening tag.
        if (cEnd != std::string::npos)
            html.erase(i, cEnd - i);
        else
            html.erase(i, gt - i + 1);
    }
}

// Remove on*="..." event handler attributes inside any tag.
static void stripEventHandlers(std::string& html)
{
    size_t i = 0;
    while (i < html.size()) {
        size_t lt = html.find('<', i);
        if (lt == std::string::npos) break;
        size_t gt = html.find('>', lt);
        if (gt == std::string::npos) break;
        // Work inside [lt, gt] only: " onxxx=..." attributes.
        //
        // The search for " on" must be bounded by gt. It used to be
        // html.find(" on", p) over the whole document, so for every tag it
        // scanned all remaining bytes until it either found an attribute or hit
        // the end of the document. A 1.5 MB document with no on* attributes at
        // all (the normal case) paid a full-document scan per tag — ~50k tags
        // meant ~6 s. A tag is tens of bytes, so compare byte by byte instead.
        size_t p = lt;
        bool changed = false;
        while (p + 3 < gt) {
            if (!(html[p] == ' ' && html[p + 1] == 'o' && html[p + 2] == 'n')) {
                ++p;
                continue;
            }
            size_t nameEnd = p + 3;
            while (nameEnd < gt && (isalnum((unsigned char)html[nameEnd]) ||
                                    html[nameEnd] == '_' || html[nameEnd] == '-'))
                ++nameEnd;
            if (nameEnd == p + 3) { p += 3; continue; }   // "on" not an attribute name
            size_t eq = nameEnd;
            while (eq < gt && (html[eq] == ' ' || html[eq] == '\t')) ++eq;
            if (eq >= gt || html[eq] != '=') { p = nameEnd; continue; }
            size_t v = eq + 1;
            while (v < gt && (html[v] == ' ' || html[v] == '\t')) ++v;
            if (v >= gt) break;
            char quote = (html[v] == '"' || html[v] == '\'') ? html[v] : 0;
            size_t end;
            if (quote) {
                end = html.find(quote, v + 1);
                if (end == std::string::npos || end > gt) break;
            } else {
                // Unquoted value: ends at whitespace or '>'.
                end = v;
                while (end < gt && html[end] != ' ' && html[end] != '\t') ++end;
            }
            html.erase(p, end - p + (quote ? 1 : 0));
            changed = true;
            break;
        }
        if (changed) continue;      // positions shifted; re-scan from this tag
        i = gt + 1;
    }
}

// Neutralize javascript: URLs in href/src (case-insensitive).
static void neutralizeScriptUrls(std::string& html)
{
    const char needle[] = "javascript:";
    const size_t nlen = sizeof(needle) - 1;
    size_t i = 0;
    while ((i = html.find('j', i)) != std::string::npos) {
        if (ieq(html, i, needle)) {
            html.replace(i, nlen, "about:blank");
            i += 11;
        } else {
            i += 1;
        }
    }
}

// Reject relative references with ".." path segments: they could escape
// the document folder and copy arbitrary local files into the build dir.
static bool isSafeRelative(const std::string &rel)
{
    size_t segStart = 0;
    while (segStart <= rel.size()) {
        size_t segEnd = rel.find_first_of("/\\", segStart);
        std::string seg = rel.substr(segStart,
                                     segEnd == std::string::npos
                                         ? rel.size() - segStart
                                         : segEnd - segStart);
        if (seg == "..") return false;
        if (segEnd == std::string::npos) break;
        segStart = segEnd + 1;
    }
    return true;
}

/* URL 百分号解码（与 container.cpp 的 url_decode 同语义）：
   md4c-html 对 src 里的非 ASCII 字节输出 %XX，磁盘文件名是解码后的原文。 */
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

static void sanitizeBrowserHtml(std::string& html)
{
    stripHtmlBlock(html, "script");
    stripHtmlBlock(html, "iframe");
    stripHtmlBlock(html, "object");
    stripHtmlBlock(html, "embed");
    stripHtmlBlock(html, "frame");
    stripEventHandlers(html);
    neutralizeScriptUrls(html);
}

static const std::unordered_map<std::string, std::string>& emoji_map()
{
    static const std::unordered_map<std::string, std::string> m = {
        {":smile:", "\xF0\x9F\x98\x8A"}, {":smiley:", "\xF0\x9F\x98\x83"},
        {":laughing:", "\xF0\x9F\x98\x86"}, {":wink:", "\xF0\x9F\x98\x89"},
        {":blush:", "\xF0\x9F\x98\x8A"}, {":heart_eyes:", "\xF0\x9F\x98\x8D"},
        {":kissing_heart:", "\xF0\x9F\x98\x98"}, {":sob:", "\xF0\x9F\x98\xAD"},
        {":cry:", "\xF0\x9F\x98\xA2"}, {":angry:", "\xF0\x9F\x98\xA0"},
        {":joy:", "\xF0\x9F\x98\x82"}, {":sweat:", "\xF0\x9F\x98\x85"},
        {":disappointed:", "\xF0\x9F\x98\x9E"}, {":sleeping:", "\xF0\x9F\x98\xB4"},
        {":worried:", "\xF0\x9F\x98\x9F"}, {":frowning:", "\xF0\x9F\x98\xA6"},
        {":expressionless:", "\xF0\x9F\x98\x91"}, {":sweat_smile:", "\xF0\x9F\x98\x85"},
        {":stuck_out_tongue:", "\xF0\x9F\x98\x9B"}, {":sunglasses:", "\xF0\x9F\x98\x8E"},
        {":heart:", "\xE2\x9D\xA4"}, {":yellow_heart:", "\xF0\x9F\x92\x9B"},
        {":blue_heart:", "\xF0\x9F\x92\x99"}, {":purple_heart:", "\xF0\x9F\x92\x9C"},
        {":green_heart:", "\xF0\x9F\x92\x9A"}, {":broken_heart:", "\xF0\x9F\x92\x94"},
        {":two_hearts:", "\xF0\x9F\x92\x95"}, {":sparkles:", "\xE2\x9C\xA8"},
        {":star:", "\xE2\xAD\x90"}, {":star2:", "\xF0\x9F\x8C\x9F"},
        {":fire:", "\xF0\x9F\x94\xA5"}, {":zap:", "\xE2\x9A\xA1"},
        {":+1:", "\xF0\x9F\x91\x8D"}, {":-1:", "\xF0\x9F\x91\x8E"},
        {":ok_hand:", "\xF0\x9F\x91\x8C"}, {":wave:", "\xF0\x9F\x91\x8B"},
        {":clap:", "\xF0\x9F\x91\x8F"}, {":muscle:", "\xF0\x9F\x92\xAA"},
        {":pray:", "\xF0\x9F\x99\x8F"}, {":point_up:", "\xF0\x9F\x91\x86"},
        {":point_down:", "\xF0\x9F\x91\x87"}, {":thumbsup:", "\xF0\x9F\x91\x8D"},
        {":thumbsdown:", "\xF0\x9F\x91\x8E"}, {":tada:", "\xF0\x9F\x8E\x89"},
        {":confetti:", "\xF0\x9F\x8E\x8A"}, {":balloon:", "\xF0\x9F\x8E\x88"},
        {":gift:", "\xF0\x9F\x8E\x81"}, {":cake:", "\xF0\x9F\x8E\x82"},
        {":beers:", "\xF0\x9F\x8D\xBB"}, {":beer:", "\xF0\x9F\x8D\xBA"},
        {":coffee:", "\xE2\x98\x95"}, {":tea:", "\xF0\x9F\x8D\xB5"},
        {":pizza:", "\xF0\x9F\x8D\x95"}, {":hamburger:", "\xF0\x9F\x8D\x94"},
        {":fries:", "\xF0\x9F\x8D\x9F"}, {":apple:", "\xF0\x9F\x8D\x8E"},
        {":rocket:", "\xF0\x9F\x9A\x80"}, {":airplane:", "\xE2\x9C\x88"},
        {":car:", "\xF0\x9F\x9A\x97"}, {":bus:", "\xF0\x9F\x9A\x8C"},
        {":train:", "\xF0\x9F\x9A\x84"}, {":bike:", "\xF0\x9F\x9A\xB2"},
        {":check:", "\xE2\x9C\x85"}, {":x:", "\xE2\x9D\x8C"},
        {":warning:", "\xE2\x9A\xA0"}, {":no_entry:", "\xF0\x9F\x9A\xAB"},
        {":100:", "\xF0\x9F\x92\xAF"}, {":chart:", "\xF0\x9F\x93\x8A"},
        {":book:", "\xF0\x9F\x93\x96"}, {":email:", "\xE2\x9C\x89"},
        {":phone:", "\xE2\x98\x8E"}, {":computer:", "\xF0\x9F\x92\xBB"},
        {":clock:", "\xF0\x9F\x95\x90"}, {":calendar:", "\xF0\x9F\x93\x85"},
        {":memo:", "\xF0\x9F\x93\x9D"}, {":pushpin:", "\xF0\x9F\x93\x8C"},
        {":link:", "\xF0\x9F\x94\x97"}, {":mag:", "\xF0\x9F\x94\x8D"},
        {":bulb:", "\xF0\x9F\x92\xA1"}, {":wrench:", "\xF0\x9F\x94\xA7"},
        {":lock:", "\xF0\x9F\x94\x92"}, {":unlock:", "\xF0\x9F\x94\x93"},
        {":key:", "\xF0\x9F\x94\x91"}, {":bell:", "\xF0\x9F\x94\x94"},
        {":dog:", "\xF0\x9F\x90\xB6"}, {":cat:", "\xF0\x9F\x90\xB1"},
        {":mouse:", "\xF0\x9F\x90\xAD"}, {":rabbit:", "\xF0\x9F\x90\xB0"},
        {":frog:", "\xF0\x9F\x90\xB8"}, {":panda:", "\xF0\x9F\x90\xBC"},
        {":monkey:", "\xF0\x9F\x90\xB5"}, {":snake:", "\xF0\x9F\x90\x8D"},
        {":sunny:", "\xE2\x98\x80"}, {":rainbow:", "\xF0\x9F\x8C\x88"},
        {":cloud:", "\xE2\x98\x81"}, {":snowflake:", "\xE2\x9D\x84"},
        {":moon:", "\xF0\x9F\x8C\x99"}, {":earth:", "\xF0\x9F\x8C\x8D"},
        {":jp:", "\xF0\x9F\x87\xAF\xF0\x9F\x87\xB5"}, {":us:", "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8"},
        {":cn:", "\xF0\x9F\x87\xA8\xF0\x9F\x87\xB3"}, {":kr:", "\xF0\x9F\x87\xB0\xF0\x9F\x87\xB7"},
    };
    return m;
}

// True if pos is at the start of a line (only whitespace between the
// preceding newline and pos, or pos == 0). Markdown code fences must
// start at the beginning of a line — a "```" inside a table cell or
// inline code span is not a fence and must not be treated as one.
static bool is_at_line_start(const std::string& text, size_t pos)
{
    size_t p = pos;
    while (p > 0) {
        char c = text[p - 1];
        if (c == '\n') return true;
        if (c != ' ' && c != '\t') return false;
        --p;
    }
    return true;
}

// Fenced code-block ranges ("```" ... "```", including the fences). Content
// inside these must be treated as literal: emoji shorthand and $$ formulas
// should NOT be substituted there (it would corrupt code samples).
// Only fences at line start count (CommonMark: up to 3 spaces indent).
static std::vector<std::pair<size_t, size_t>> find_fenced_ranges(const std::string& text)
{
    std::vector<std::pair<size_t, size_t>> ranges;
    size_t i = 0;
    while (i < text.size()) {
        size_t open = text.find("```", i);
        if (open == std::string::npos) break;
        if (!is_at_line_start(text, open)) { i = open + 3; continue; }
        // closing fence must also be at line start
        size_t close = std::string::npos;
        size_t j = open + 3;
        while ((j = text.find("```", j)) != std::string::npos) {
            if (is_at_line_start(text, j)) { close = j; break; }
            j += 3;
        }
        if (close == std::string::npos) break;   // unterminated fence: ignore rest
        ranges.push_back({open, close + 3});     // inclusive of closing fence
        i = close + 3;
    }
    return ranges;
}

// True if pos falls inside any fenced code-range (or before i's next range).
//
// Ranges are produced in ascending, non-overlapping order by
// find_fenced_ranges(), so this is a binary search. It used to be a linear
// scan, and both call sites invoke it once per scanned position (every ':'
// candidate, every "$$"): with ~1800 fenced blocks in a 1 MB document that is
// ~10^9 range comparisons, which alone accounted for 6 s of the preview's
// preprocess stage.
static bool in_any_fence(const std::vector<std::pair<size_t, size_t>>& r,
                         size_t pos)
{
    size_t lo = 0, hi = r.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (pos < r[mid].first)      hi = mid;
        else if (pos >= r[mid].second) lo = mid + 1;
        else                         return true;
    }
    return false;
}

static std::string replace_emoji(const std::string& text,
                                 const std::vector<std::pair<size_t, size_t>>& fences)
{
    // 单遍扫描：遇到 ':' 才尝试匹配简码（原实现 100 个简码逐个 find+replace
    // 全文，O(100×N)；改为 O(N) 单遍，长文档明显更快）。
    const auto& m = emoji_map();
    std::string result;
    result.reserve(text.size() + 16);
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == ':' && !in_any_fence(fences, i)) {
            size_t j = text.find(':', i + 1);
            if (j != std::string::npos && j - i <= 32) {
                auto it = m.find(text.substr(i, j - i + 1));
                if (it != m.end()) {
                    result += it->second;
                    i = j + 1;
                    continue;
                }
            }
        }
        result += text[i];
        ++i;
    }
    return result;
}
/* 把 RGBA 位图写入 PNG 文件（stb_image_write，stride 须为 w*4）。
   stb 内部走 ANSI fopen，中文路径会失败：改用宽字符 FILE* 打开，
   stbi_write_png_to_func 只负责 PNG 编码。 */
static bool write_png_file(const char* path, const unsigned char* pixels, int w, int h, int stride)
{
    std::wstring wpath = widen(path);
    if (wpath.empty()) return false;
    FILE* fp = _wfopen(wpath.c_str(), L"wb");
    if (!fp) return false;
    struct Ctx { FILE* fp; bool ok; } ctx{fp, true};
    auto writer = [](void *context, void *data, int size) {
        Ctx *c = static_cast<Ctx*>(context);
        if (c->ok && fwrite(data, 1, size, c->fp) != (size_t)size) c->ok = false;
    };
    bool ok = stbi_write_png_to_func(writer, &ctx, w, h, 4, pixels, stride) != 0 && ctx.ok;
    fclose(fp);
    return ok;
}

/* FNV-1a 64：公式/mermaid 内容哈希 → PNG 缓存文件名 */
static uint64_t fnv64(const std::string& s)
{
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}

/* 并行执行渲染任务：FFI（ratex/mmdr）线程安全（各自 lib.rs 头注释声明
   无全局可变状态），最多 4 线程并发；每任务独立输出文件，无共享写。 */
template <typename Fn>
static void run_parallel(size_t n, Fn&& fn)
{
    if (n == 0) return;
    size_t nt = std::min<size_t>(n, 4);
    std::atomic<size_t> next{0};
    std::vector<std::thread> pool;
    pool.reserve(nt);
    for (size_t t = 0; t < nt; t++) {
        pool.emplace_back([&]() {
            for (;;) {
                size_t idx = next.fetch_add(1);
                if (idx >= n) break;
                fn(idx);
            }
        });
    }
    for (auto& th : pool) th.join();
}

struct FormulaItem { std::string text; bool display; };   // display=true 块级大图 / false 行内小图

static bool render_formulas(const std::vector<FormulaItem>& formulas,
                            const std::string& out_dir,
                            std::vector<uint64_t>& hashes)
{
    if (formulas.empty()) return false;

    CreateDirectoryW(widen(out_dir).c_str(), NULL);

    // 哈希命名缓存：内容未变的公式命中 <hash>.png 直接跳过 FFI 渲染；
    // 渲染失败写 <hash>.fail（内容变了哈希变 → 自然重试）。
    // 哈希含模式前缀（d:/i:）：同一公式块级/行内渲染参数不同，缓存须区分。
    struct Task { std::string key; bool display; uint64_t hash; };
    std::vector<Task> tasks;
    hashes.resize(formulas.size());
    for (size_t i = 0; i < formulas.size(); i++) {
        std::string single_line = formulas[i].text;
        size_t p = 0;
        while ((p = single_line.find('\n', p)) != std::string::npos)
            single_line.replace(p, 1, " ");
        p = 0;
        while ((p = single_line.find('\r', p)) != std::string::npos)
            single_line.replace(p, 1, " ");
        std::string hkey = (formulas[i].display ? "d:" : "i:") + single_line;
        uint64_t h = fnv64(hkey);
        hashes[i] = h;
        char out_path[MAX_PATH], fail_path[MAX_PATH];
        snprintf(out_path, sizeof(out_path), "%s\\%016llx.png", out_dir.c_str(), (unsigned long long)h);
        if (GetFileAttributesW(widen(out_path).c_str()) != INVALID_FILE_ATTRIBUTES) continue;
        snprintf(fail_path, sizeof(fail_path), "%s\\%016llx.fail", out_dir.c_str(), (unsigned long long)h);
        if (GetFileAttributesW(widen(fail_path).c_str()) != INVALID_FILE_ATTRIBUTES) continue;
        tasks.push_back({std::move(single_line), formulas[i].display, h});
    }
    if (tasks.empty()) return true;

    // 进程内并行渲染：ratex_ffi（LaTeX → SVG）→ mmdr_svg_to_png（resvg → PNG）
    // 块级字号 16 自然尺寸；行内字号 14 控制高度（与文字行协调）。
    run_parallel(tasks.size(), [&](size_t idx) {
        const Task& tk = tasks[idx];
        char* svg = nullptr;
        bool ok = false;
        // 块级/行内统一字号 16（与预览正文同号，行内公式与正文看不出大小差别）；
        // 行内模式（display_mode=0）为 Text style：嵌套分数逐层缩小属 KaTeX 固有行为
        if (pecia_ratex_svg(tk.key.c_str(), tk.display ? 1 : 0, 16.0, &svg) != 0 || !svg)
            goto fail;
        {
            MmdrBitmap bmp = {0};
            if (mmdr_svg_to_png(svg, 0, 0, &bmp) == 0 &&
                bmp.pixels && bmp.width > 0 && bmp.height > 0) {
                char out_path[MAX_PATH];
                snprintf(out_path, sizeof(out_path), "%s\\%016llx.png", out_dir.c_str(), (unsigned long long)tk.hash);
                ok = write_png_file(out_path, bmp.pixels, bmp.width, bmp.height, bmp.stride);
                mmdr_bitmap_free(&bmp);
            }
        }
        pecia_ratex_free(svg);
    fail:
        if (!ok) {
            char fail_path[MAX_PATH];
            snprintf(fail_path, sizeof(fail_path), "%s\\%016llx.fail", out_dir.c_str(), (unsigned long long)tk.hash);
            FILE* f = _wfopen(widen(fail_path).c_str(), L"wb");
            if (f) fclose(f);
        }
    });
    return true;
}

/* 生成带公式渲染的 HTML，build_dir 用于定位公式图片输出路径；
   doc_dir 非空时：把 HTML 里的本地相对图片拷贝进 build_dir（保留相对结构），
   并把完整 HTML 写入 build_dir/index.html（供浏览器直接打开）。 */
std::string md_to_html(const std::string& markdown, const std::string& build_dir,
                       const std::string& doc_dir,
                       std::vector<PreviewHeading>* headings_out,
                       const std::string& base_font)
{
    PpProf pp;   // 阶段性耗时（PECIA_PROFILE=1），打印的是累计值
    // 0. 替换 emoji 简码为 Unicode（跳过 fenced code block 内部，避免误伤代码）
    std::vector<std::pair<size_t, size_t>> fences0 = find_fenced_ranges(markdown);
    std::string md = replace_emoji(markdown, fences0);
    pp.mark("emoji");

    // 0b. 移除 [TOC] / <!-- TOC --> 占位文本。这必须在 md4c 解析之前
    // 完成：原实现把它放在生成 HTML 之后，对 md 的修改已经不会进入
    // body_html，页面上仍出现 [TOC] 文字（无效修复，已修正）。
    // 目录本身由预览工具条的目录按钮（可折叠侧栏）提供，标题列表经
    // md_to_html 的 headings 出参返回给 UI 层。
    {
        size_t p;
        while ((p = md.find("[TOC]")) != std::string::npos) md.replace(p, 5, "");
        while ((p = md.find("<!-- TOC -->")) != std::string::npos) md.replace(p, 12, "");
    }

    // emoji 替换会改变代码块之外文本的字节长度，使 fences0 在 md 上失准；
    // 对处理后的 md 重新定位 fenced 范围，供下面的 $$ 提取使用。
    std::vector<std::pair<size_t, size_t>> fences = find_fenced_ranges(md);

    // 1a. 提取 $$...$$ 公式。位置判定行内/块级：$$ 所在行若前后还有文字
    // （非独占行）→ 行内小图；独占一行（行首起、行尾止）→ 块级大图。
    // 仅处理非 fenced-code 内的 $$（代码块里的 $$ 是字面量，不能当公式）。
    std::vector<FormulaItem> formulas;

    size_t pos = 0;
    while ((pos = md.find("$$", pos)) != std::string::npos) {
        if (in_any_fence(fences, pos)) { pos += 2; continue; }
        size_t end = md.find("$$", pos + 2);
        if (end == std::string::npos) break;
        std::string formula = md.substr(pos + 2, end - pos - 2);
        while (!formula.empty() && (formula.front() == '\n' || formula.front() == ' '))
            formula.erase(formula.begin());
        while (!formula.empty() && (formula.back() == '\n' || formula.back() == ' '))
            formula.pop_back();
        if (formula.empty()) { md.erase(pos, end - pos + 2); continue; }
        // 行内判定：$$ 前本行有文字，或结束标记后本行还有文字
        bool isInline = false;
        {
            size_t p = pos;
            while (p > 0 && (md[p - 1] == ' ' || md[p - 1] == '\t')) --p;
            if (p > 0 && md[p - 1] != '\n') isInline = true;
        }
        if (!isInline) {
            size_t q = end + 2;
            while (q < md.size() && (md[q] == ' ' || md[q] == '\t')) ++q;
            if (q < md.size() && md[q] != '\n' && md[q] != '\r') isInline = true;
        }
        formulas.push_back({formula, !isInline});
        std::string placeholder = "\xFF\xFE_FORMULA_" + std::to_string(formulas.size()) + "_\xFF\xFE";
        md.replace(pos, end - pos + 2, placeholder);
        pos += placeholder.size();
    }

    // 1b. 提取 ```math / ```katex / ```latex 代码块公式
    // 仅匹配行首的围栏（表格单元格内的 ```math 等字样不是代码块）
    const char* formula_langs[] = {"```math", "```katex", "```latex"};
    for (const char* lang : formula_langs) {
        pos = 0;
        while ((pos = md.find(lang, pos)) != std::string::npos) {
            if (!is_at_line_start(md, pos)) { pos += strlen(lang); continue; }
            size_t start = pos;
            size_t cs = pos + strlen(lang);
            if (cs < md.size() && md[cs] == '\r') cs++;
            if (cs < md.size() && md[cs] == '\n') cs++;
            // closing fence must also be at line start
            size_t end = std::string::npos;
            size_t j = cs;
            while ((j = md.find("```", j)) != std::string::npos) {
                if (is_at_line_start(md, j)) { end = j; break; }
                j += 3;
            }
            if (end == std::string::npos) break;
            std::string formula = md.substr(cs, end - cs);
            while (!formula.empty() && (formula.front() == '\n' || formula.front() == ' '))
                formula.erase(formula.begin());
            while (!formula.empty() && (formula.back() == '\n' || formula.back() == ' '))
                formula.pop_back();
            if (formula.empty()) { md.erase(start, end + 3 - start); continue; }
            formulas.push_back({formula, true});   // 代码块公式恒为块级
            std::string ph = "\xFF\xFE_FORMULA_" + std::to_string(formulas.size()) + "_\xFF\xFE";
            md.replace(start, end + 3 - start, ph);
            pos = start + ph.size();
        }
    }

    // 1c. 提取 ```mermaid 代码块（仅行首围栏）
    std::vector<std::string> mermaid_blocks;
    pos = 0;
    while ((pos = md.find("```mermaid", pos)) != std::string::npos) {
        if (!is_at_line_start(md, pos)) { pos += 10; continue; }
        size_t start = pos;
        size_t cs = pos + 10;
        if (cs < md.size() && md[cs] == '\r') cs++;
        if (cs < md.size() && md[cs] == '\n') cs++;
        // closing fence must also be at line start
        size_t end = std::string::npos;
        size_t j = cs;
        while ((j = md.find("```", j)) != std::string::npos) {
            if (is_at_line_start(md, j)) { end = j; break; }
            j += 3;
        }
        if (end == std::string::npos) break;
        std::string content = md.substr(cs, end - cs);
        mermaid_blocks.push_back(content);
        std::string ph = "\xFF\xFE_MERMAID_" + std::to_string(mermaid_blocks.size()) + "_\xFF\xFE";
        md.replace(start, end + 3 - start, ph);
        pos = start + ph.size();
    }

    // 2. md4c 解析
    pp.mark("formula+mermaid extract");
    std::string body_html;
    int ret = md_html(
        md.data(), (MD_SIZE)md.size(),
        &collect_html, &body_html,
        MD_FLAG_TABLES | MD_FLAG_PERMISSIVEAUTOLINKS | MD_FLAG_TASKLISTS | MD_FLAG_STRIKETHROUGH
        | MD_FLAG_SUPERSCRIPTS | MD_FLAG_SUBSCRIPTS
        | MD_FLAG_HIGHLIGHT | MD_FLAG_FOOTNOTES | MD_FLAG_ADMONITIONS,
        0
    );
    if (ret == -1)
        body_html = "<p><em>Error parsing markdown.</em></p>";

    // 任务列表：litehtml 不渲染 input 复选框（无表单控件），且其 ::before
    // 在块级 li 上会单独成行。直接把 md4c-html 输出的 input 替换为
    // ☐/☑ 文本（勾选/未勾选），一步完成。
    {
        const char *checked =
            "<li class=\"task-list-item\"><input type=\"checkbox\" "
            "class=\"task-list-item-checkbox\" disabled checked>";
        const char *plain =
            "<li class=\"task-list-item\"><input type=\"checkbox\" "
            "class=\"task-list-item-checkbox\" disabled>";
        const char *repChecked = "<li class=\"task-list-item\">\xE2\x96\xA0 ";   // ■
        const char *repPlain = "<li class=\"task-list-item\">\xE2\x96\xA1 ";       // □
        size_t p = 0;
        while ((p = body_html.find(checked, p)) != std::string::npos) {
            body_html.replace(p, strlen(checked), repChecked);
            p += strlen(repChecked);
        }
        p = 0;
        while ((p = body_html.find(plain, p)) != std::string::npos) {
            body_html.replace(p, strlen(plain), repPlain);
            p += strlen(repPlain);
        }
    }

    // 4. 从 HTML 提取标题并注入 id 属性（用于锚点跳转）
    pp.mark("md4c html");
    // 直接从 md4c 生成的 HTML 提取标题，避免因代码块中的 # 行或 setext 风格标题（===/---）导致索引错位。
    {
        struct HInfo {
            size_t tag_start;   // '<hN' 的位置
            size_t tag_end;     // '>' 的位置
            int    level;
            std::string text;
        };
        std::vector<HInfo> headings;

        size_t sp = 0;
        while (sp < body_html.size()) {
            size_t p = body_html.find("<h", sp);
            if (p == std::string::npos) break;

            if (p + 3 >= body_html.size()) break;
            char lc = body_html[p + 2];
            if (lc < '1' || lc > '6') { sp = p + 2; continue; }
            int level = lc - '0';

            // '<hN' 后面必须是 '>' 或 ' '（属性），不能是其他字母（如 <header>）
            char sep = body_html[p + 3];
            if (sep != '>' && sep != ' ') { sp = p + 2; continue; }

            size_t te = body_html.find('>', p);
            if (te == std::string::npos) break;

            std::string close_tag = "</h" + std::to_string(level) + ">";
            size_t cp = body_html.find(close_tag, te + 1);
            if (cp == std::string::npos) { sp = te + 1; continue; }

            // 提取标题文本（去除嵌套标签如 <a>、<code> 等）
            std::string inner = body_html.substr(te + 1, cp - te - 1);
            std::string text;
            bool in_tag = false;
            for (char c : inner) {
                if (c == '<') in_tag = true;
                else if (c == '>') in_tag = false;
                else if (!in_tag) text.push_back(c);
            }
            // 解码基本 HTML 实体
            {
                size_t ep = 0;
                while ((ep = text.find('&', ep)) != std::string::npos) {
                    size_t es = text.find(';', ep);
                    if (es == std::string::npos) break;
                    std::string ent = text.substr(ep, es - ep + 1);
                    if (ent == "&amp;")      text.replace(ep, ent.size(), "&");
                    else if (ent == "&lt;")  text.replace(ep, ent.size(), "<");
                    else if (ent == "&gt;")  text.replace(ep, ent.size(), ">");
                    else if (ent == "&quot;")text.replace(ep, ent.size(), "\"");
                    else if (ent == "&#39;") text.replace(ep, ent.size(), "'");
                    else if (ent == "&nbsp;")text.replace(ep, ent.size(), " ");
                    else { ep = es + 1; continue; }
                    ep++;
                }
            }
            // 去除首尾空白
            size_t first = text.find_first_not_of(" \t\n\r");
            if (first == std::string::npos) {
                // 空标题，跳过
                sp = cp + close_tag.size();
                continue;
            }
            size_t last = text.find_last_not_of(" \t\n\r");
            text = text.substr(first, last - first + 1);

            headings.push_back({p, te, level, text});
            sp = cp + close_tag.size();
        }

        // 逆序注入 id，避免位置偏移影响后续查找；同时按文档顺序填充出参
        for (int i = (int)headings.size() - 1; i >= 0; i--) {
            std::string id_val = "toc-" + std::to_string(i);
            std::string tag = body_html.substr(headings[i].tag_start,
                                               headings[i].tag_end - headings[i].tag_start);
            size_t id_pos = tag.find("id=\"");
            if (id_pos != std::string::npos) {
                // 替换已有 id
                size_t id_end = tag.find('"', id_pos + 4);
                if (id_end != std::string::npos) {
                    body_html.replace(headings[i].tag_start + id_pos + 4,
                                      id_end - id_pos - 4, id_val);
                }
            } else {
                // 插入新 id
                body_html.insert(headings[i].tag_end, " id=\"" + id_val + "\"");
            }
            if (headings_out) {
                PreviewHeading h;
                h.level = headings[i].level;
                h.text = headings[i].text;
                h.id = id_val;
                headings_out->push_back(std::move(h));
            }
        }
        if (headings_out) std::reverse(headings_out->begin(), headings_out->end());

        // 标题锚点 id 已注入（toc-N），供锚点滚动/目录跳转使用
    }

    // 3. 渲染公式为 PNG
    pp.mark("headings+ids");
    std::string formula_dir = build_dir + "\\formulas";
    std::vector<uint64_t> formula_hashes;
    if (!formulas.empty()) {
        render_formulas(formulas, formula_dir, formula_hashes);
    }

    // 3b. 渲染 mermaid 图表为 PNG（FFI 进程内渲染，resvg 0.47；哈希缓存+失败缓存+并行）
    std::string mermaid_dir = build_dir + "\\mermaid";
    std::vector<uint64_t> mermaid_hashes;
    if (!mermaid_blocks.empty()) {
        CreateDirectoryW(widen(mermaid_dir).c_str(), NULL);
        struct MTask { std::string key; uint64_t hash; };
        std::vector<MTask> mtasks;
        mermaid_hashes.resize(mermaid_blocks.size());
        for (size_t i = 0; i < mermaid_blocks.size(); i++) {
            uint64_t h = fnv64(mermaid_blocks[i]);
            mermaid_hashes[i] = h;
            char out_path[MAX_PATH], fail_path[MAX_PATH];
            snprintf(out_path, sizeof(out_path), "%s\\%016llx.png", mermaid_dir.c_str(), (unsigned long long)h);
            if (GetFileAttributesW(widen(out_path).c_str()) != INVALID_FILE_ATTRIBUTES) continue;
            snprintf(fail_path, sizeof(fail_path), "%s\\%016llx.fail", mermaid_dir.c_str(), (unsigned long long)h);
            if (GetFileAttributesW(widen(fail_path).c_str()) != INVALID_FILE_ATTRIBUTES) continue;
            mtasks.push_back({mermaid_blocks[i], h});
        }
        run_parallel(mtasks.size(), [&](size_t idx) {
            const MTask& tk = mtasks[idx];
            MmdrBitmap bmp = {0};
            bool ok = false;
            if (mmdr_mermaid_to_png(tk.key.c_str(), 0, 0, &bmp) == 0 &&
                bmp.pixels && bmp.width > 0 && bmp.height > 0) {
                char out_path[MAX_PATH];
                snprintf(out_path, sizeof(out_path), "%s\\%016llx.png", mermaid_dir.c_str(), (unsigned long long)tk.hash);
                ok = write_png_file(out_path, bmp.pixels, bmp.width, bmp.height, bmp.stride);
                mmdr_bitmap_free(&bmp);
            }
            if (!ok) {
                char fail_path[MAX_PATH];
                snprintf(fail_path, sizeof(fail_path), "%s\\%016llx.fail", mermaid_dir.c_str(), (unsigned long long)tk.hash);
                FILE* f = _wfopen(widen(fail_path).c_str(), L"wb");
                if (f) fclose(f);
            }
        });
    }
    // 4. 替换公式占位符为 img 标签（块级居中大图 / 行内小图垂直居中）
    for (size_t i = 0; i < formulas.size(); i++) {
        std::string placeholder = "\xFF\xFE_FORMULA_" + std::to_string(i + 1) + "_\xFF\xFE";
        std::string fwd = formula_dir;
        for (auto& c : fwd) if (c == '\\') c = '/';
        const char *cls = formulas[i].display ? "formula-img" : "math-inline";
        char hash_hex[20];
        snprintf(hash_hex, sizeof(hash_hex), "%016llx", (unsigned long long)formula_hashes[i]);
        std::string img_tag = "<img src=\"" + fwd + "/" + hash_hex + ".png\" class=\"" + cls + "\">";
        size_t p = 0;
        while ((p = body_html.find(placeholder, p)) != std::string::npos) {
            body_html.replace(p, placeholder.size(), img_tag);
            p += img_tag.size();
        }
    }

    // 4b. 替换 mermaid 占位符为 img 标签
    std::string mermaid_fwd = mermaid_dir;
    for (auto& c : mermaid_fwd) if (c == '\\') c = '/';
    for (size_t i = 0; i < mermaid_blocks.size(); i++) {
        std::string placeholder = "\xFF\xFE_MERMAID_" + std::to_string(i + 1) + "_\xFF\xFE";
        char hash_hex[20];
        snprintf(hash_hex, sizeof(hash_hex), "%016llx", (unsigned long long)mermaid_hashes[i]);
        std::string img_tag = "<img src=\"" + mermaid_fwd + "/" + hash_hex + ".png\" class=\"mermaid-img\">";
        size_t p = 0;
        while ((p = body_html.find(placeholder, p)) != std::string::npos) {
            body_html.replace(p, placeholder.size(), img_tag);
            p += img_tag.size();
        }
    }

    // 5. 包装为完整 HTML（锚点 id 已注入，目录由外部处理）
    // [TOC] / <!-- TOC --> 占位文本已在解析前移除（见函数开头 0b）。

    // 6. 包装为完整 HTML
    // 预览基准字体（编辑器 editor_font）放在候选列表首位，实现"预览跟随编辑器
    // 字体"；后续候选保留，供导出到浏览器时在没有该字体的机器上兜底。
    std::string body_fonts = "'Microsoft YaHei', 'Segoe UI', Arial, Helvetica, sans-serif";
    std::string code_fonts = "'Consolas', 'Courier New', monospace";
    if (!base_font.empty()) {
        body_fonts = "'" + base_font + "', " + body_fonts;
        code_fonts = "'" + base_font + "', " + code_fonts;
    }
    std::ostringstream html;
    html << "<!DOCTYPE html>\n<html>\n<head>\n"
         << "<meta charset=\"utf-8\">\n"
         << "<style>\n"
         << "/* 行高必须落在整数像素上：16 x 1.6 = 25.6px 不是整数，\n"
         << "   逐行累加再取整会让间隔在 26/25 之间交替；\n"
         << "   16 x 1.625 = 26.0px，累加永远是整数，间隔恒定 26。 */\n"
         << "* { line-height: 1.625; }\n"
         << "/* 块级元素默认外边距清零：必须用标签选择器，不能靠 *。\n"
         << "   litehtml 自带样式表(master_css.h)里 p{margin:1em 0}、\n"
         << "   h1{margin:.67em 0}、blockquote{1em}、hr{.5em} 都是标签\n"
         << "   选择器(特异性 0-0-1)，而 * 是 0-0-0，压根盖不住它们 ——\n"
         << "   结果顶层段落被 body>*+* 设成 8px、嵌套段落和标题却仍是\n"
         << "   1em / 0.67em，行间隔因此忽大忽小。这里显式清零，间距\n"
         << "   统一交给下面的 * + * 规则。 */\n"
         << "body, p, ul, ol, li, dl, dt, dd, blockquote, pre, table, hr,\n"
         << "h1, h2, h3, h4, h5, h6, figure, div { margin: 0; }\n"
         << "body {\n"
         << "  font-family: " << body_fonts << ";\n"
         << "  font-size: 16px;\n"
         << "  color: #222;\n"
         << "  padding: 20px;\n"
         << "  margin: 0;\n"
         << "  background: #fff;\n"
         << "  word-wrap: break-word;\n"
         << "  overflow-wrap: break-word;\n"
         << "  word-break: break-word;\n"
         << "}\n"
         << "/* 块级元素之间留 8px 呼吸（仅限结构性块：标题/代码块/表格/\n"
         << "   引用/分隔线/提示框等）。 */\n"
         << "body > * + *,\n"
         << "blockquote > * + *,\n"
         << "li > * + *,\n"
         << "td > * + *,\n"
         << "th > * + *,\n"
         << "div.admonition > * + * { margin-top: 8px; }\n"
         << "li + li { margin-top: 8px; }\n"
         << "/* 正文级文本流不加任何额外间距：行间隔一律只由 line-height\n"
         << "   (16px x 1.625 = 26px) 决定。这样\"段内硬换行<br>\"、\"单元格内\n"
         << "   自动折行\"、\"段与段之间\"、\"列表项之间\"四种间距全部相同，\n"
         << "   不会出现同一片普通文字忽紧忽松。\n"
         << "   注意顺序：p+p (0-0-2) 特异性高于上面的 body>*+* (0-0-1)，\n"
         << "   所以能覆盖它，而 h2+p / p+pre 等仍保留 8px。 */\n"
         << "p + p, li + li { margin-top: 0; }\n"
         // 行内元素不吃块间距：上面的 `li > * + *` / `td > * + *` 之类规则
         // 会把「紧随 <strong> 的行内 <code>」也选中，给它加 8px margin-top。
         // litehtml 把行内元素的 margin 一起算进背景盒（content_offset_height），
         // 于是同一个 <code> 在脚注里比正文高 8px（实测 25 vs 17）、色块还会
         // 上下错位（实测 y 偏移 -4）。行内元素不该有垂直外边距，这里归零。
         // 特异性 0-0-1 与 `li > * + *` 相同，靠「更靠后」获胜。
         << "code, strong, em, b, i, u, s, del, a, span, mark, img,\n"
         << "sup, sub { margin: 0; }\n"
         << "a { color: #0366d6; text-decoration: underline; }\n"
         << "ul, ol { padding-left: 32px; }\n"
         << "/* 行内 code 的上下 padding 必须为 0。\n"
         << "   code 是 inline，litehtml 会把行内元素的纵向 padding/margin\n"
         << "   都算进行盒与背景盒高度：正文行高 26px，带纵向 padding 的\n"
         << "   行会被撑高、色块也跟着变厚。保留左右 padding，只清纵向。 */\n"
         // 行内 code 的字号必须用相对单位。固定 px（曾为 14px）时，
         // 正文里刚好等于 16px x 0.875，看着没问题；但标题会放大到
         // h2=24px / h1=32px，code 仍是 14px，色块只有 15px 高，在
         // 24px 的标题文字中间"特别矮"（实测：h2 内 code 15px vs 标题
         // 行高 39px）。改成 0.875em 后 code 随父元素缩放——
         // h1→28px、h2→21px、h3→17.5px、正文 16px→14px，任何语境下
         // 都比周围文字小 12.5%，与浏览器一致；pre 内仍是 16x0.875=14。
         << "code { background: #f0f0f0; padding: 0 6px; border-radius: 3px;\n"
         << "  font-family: " << code_fonts << "; font-size: 0.875em; }\n"
         << "pre { background: #f6f8fa; padding: 16px; border-radius: 6px;\n"
         << "  border: 1px solid #e1e4e8; white-space: pre-wrap; word-break: break-word; }\n"
         << "pre code { display: block; background: none; padding: 0; border-radius: 0; white-space: pre-wrap; word-break: break-word; }\n"
         << "h1 { font-size: 2em; font-weight: bold; }\n"
         << "h2 { font-size: 1.5em; font-weight: bold; }\n"
         << "h3 { font-size: 1.25em; font-weight: bold; }\n"
         << "h4 { font-size: 1em; font-weight: bold; }\n"
         << "h5 { font-size: 0.875em; font-weight: bold; }\n"
         << "h6 { font-size: 0.85em; font-weight: bold; }\n"
         // 标题内的行内 code 不再缩小。行内 code 全局是 0.875em（正文
         // 16px→14px，视觉上刚好），但标题字号被放大到 h1=32/h2=24 后，
         // 0.875em 只剩 28/21px，与同一行的粗体、斜体相比"明显更小"
         // （2026-10-20 用户实测反馈）。标题里的代码属于强调，跟随标题
         // 字号即可，所以这里用 1em 覆盖。
         << "h1 code, h2 code, h3 code, h4 code, h5 code, h6 code { font-size: 1em; }\n"
         << "table { border-collapse: collapse; border-spacing: 0; }\n"
         << "th, td { border: 1px solid #dfe2e5; padding: 6px 13px; }\n"
         << "th { background: #f6f8fa; }\n"
         << "blockquote { border-left: 4px solid #dfe2e5; padding: 0 1em;\n"
         << "  color: #6a737d; }\n"
         << "img { max-width: 100%; display: block; }\n"
         << "sup { vertical-align: super; font-size: smaller; }\n"
         << "sub { vertical-align: sub; font-size: smaller; }\n"
          << ".formula-img { display: block; margin: 0 auto; max-width: 90%; }\n"
          << ".math-inline { display: inline; vertical-align: middle; }\n"
          << ".mermaid-img { max-width: 100%; height: auto; display: block; }\n"
          << "/* 垂直 padding 不给：inline 元素的上下 padding 会被计进行框，\n"
          << "   使含 ==高亮== 的表格行比其它行高一截，看起来像行距不齐。 */\n"
          << "/* mark 保持 inline（不是 inline-block）：litehtml 的行内背景盒\n"
          << "   高度取字体度量（ascent+descent），与 line-height 无关 —— 这\n"
          << "   正是浏览器里荧光笔的观感（16px 字号实测 17px 高）。若改成\n"
          << "   inline-block，背景会撑成整行高（实测 26px），比同一句里的\n"
          << "   <code> 色块高 9px，两种色块高矮不一。\n"
          << "   旧注释称「跨行 inline 的背景 y 偏移一个行高」，在 0.10 上\n"
          << "   复测不成立：换行后每片段高 17、相邻片段 y 间距恒 26，引用/\n"
          << "   列表/表格内均正确，故不再用 inline-block 绕行。 */\n"
          << "mark, .highlight { background: #ffff8c; padding: 0 4px; }\n"
          << "del, s { text-decoration: line-through; }\n"
          << ".task-list-item { list-style: none; }\n"
          << "ul ul { list-style-type: circle; }\n"
          << "ul ul ul { list-style-type: square; }\n"
          << "ol ol { list-style-type: lower-roman; }\n"
          << "ol ol ol { list-style-type: lower-alpha; }\n"
          << ".footnotes { margin-top: 2em; padding-top: 1em; border-top: 1px solid #ddd; font-size: 14px; color: #666; }\n"
         << ".footnotes li { margin-bottom: 4px; }\n"
         << "/* 脚注回流箭头：FLTK 的 fl_draw() 只按 face 用【单个】字体绘制，\n"
         << "   没有 CSS 那种逐字符字体回退。字体缺 U+2199 字形时不是回退到\n"
         << "   符号字体，而是直接画一个方框（豆腐块）。实测微软雅黑/Segoe UI\n"
         << "   本身都有该字形，但预览字体由用户在 View > Font 里选，换成不含\n"
         << "   箭头字形的字体（等宽/手写体等）就必然出现方块。故显式指定符号\n"
         << "   字体族，让该字符有确定归属。Segoe UI Symbol 是 Windows 自带且\n"
         << "   覆盖 U+2199；找不到时下面的 sans-serif 至少不会更糟。 */\n"
         << ".footnote-backref { font-family: 'Segoe UI Symbol', 'Segoe UI', sans-serif; }\n"
         << "div.admonition { padding: 8px 16px; border-left: 4px solid #0969da; background: #f0f6ff; }\n"
         << "div.admonition-note { border-left: 4px solid #0969da; background: #f0f6ff; padding: 8px 16px; }\n"
         << "div.admonition-tip { border-left: 4px solid #1a7f37; background: #dafbe1; padding: 8px 16px; }\n"
         << "div.admonition-important { border-left: 4px solid #8250df; background: #fbefff; padding: 8px 16px; }\n"
         << "div.admonition-warning { border-left: 4px solid #bf8700; background: #fff8c5; padding: 8px 16px; }\n"
         << "div.admonition-caution { border-left: 4px solid #cf222e; background: #ffebe9; padding: 8px 16px; }\n"
          << "p.admonition-title { font-weight: bold; margin: 0 0 4px 0; }\n"
          << "</style>\n</head>\n<body>\n"
         << body_html
         << "\n</body>\n</html>\n";

    std::string html_str = html.str();
    pp.mark("wrap html");

    // 注意：这里【绝不】做远程图片本地化。
    // md_to_html() 是渲染与导出两条路径共用的，产出的 HTML 有两个消费者，
    // 它们解析相对路径的基准完全不同：
    //   · 预览容器 MyContainer::resolve_image_path() —— 以【源文档目录】
    //     （setBaseUrl 传入的 doc_dir）为基准；
    //   · 浏览器/导出 —— 以【build_dir】为基准。
    // 曾经在这里调 localize_remote_images()，把远程 URL 改写成一串
    // "pecia_img_xxxxxx.png" 之类的相对文件名并拷进 build_dir。预览容器
    // 拿到这种相对路径会去源文档目录找，那里根本没有这些文件 —— 所有在线
    // 图片一起断链。更糟的是改写后 URL 不再是 http 开头，
    // is_remote_url() 为 false，容器连下载分支都不再进，重渲染也救不回来。
    // （首次打开看起来正常，是因为那一刻缓存还没生成、远程 URL 被原样
    //   保留；之后每次重新渲染缓存都在，就全挂。）
    // 本地化只属于"落盘"场景，由调用方自己做：
    //   · MainWindow::openPreviewInBrowser → localizeRemoteImagesInIndexHtml()
    //   · MainWindow::exportHtmlToFile     → localize_remote_images(..., fetch)

    // 7. 把本地相对图片拷贝进 build_dir（保留相对路径结构）：
    //    浏览器打开 index.html 时按相对路径解析，图片完整显示。
    //    仅处理非 URL、非绝对路径的本地相对引用；公式/meimaid 为绝对路径，跳过。
    if (!doc_dir.empty() && !build_dir.empty()) {
        std::string docBase = doc_dir;
        for (auto& c : docBase) if (c == '/') c = '\\';
        // 归一成"带尾分隔符的目录"：契约是 doc_dir 指向文档所在目录，但实际
        // 传进来的形态有两种（下面第 2 种正是曾经把浏览器打开路径弄坏的元凶）：
        //   A) 文件全路径  "...\ex1.markdown\ex1.md"   ← exportHtmlToFile / e2e
        //   B) 目录 + 尾分隔符 "...\ex1.markdown\"     ← MainWindow 预览路径
        // 早先直接拼一级，形态 A 得到 "...\ex1.md\sample-png.png"（多一层文件
        // 名）；后来改成"先无条件弹掉尾分隔符、再无条件砍掉最后一级"，形态 A
        // 对了，形态 B 却把**目录名本身**吃掉一层（"...\ex1.markdown\" →
        // "...\build\"），于是又找不到文件 —— 表现为「导出正常、但在浏览器里
        // 打开时本地图片全断」。故必须先判形态再动手。
        bool wasDir = !docBase.empty() &&
                      (docBase.back() == '\\' || docBase.back() == '/');
        while (!docBase.empty() && (docBase.back() == '\\' || docBase.back() == '/'))
            docBase.pop_back();
        if (!wasDir) {                          // 形态 A：砍掉文件名
            size_t sep = docBase.find_last_of('\\');
            docBase = (sep == std::string::npos) ? std::string()
                                                : docBase.substr(0, sep + 1);
        } else if (!docBase.empty()) {
            docBase += '\\';                    // 形态 B：原样保留目录名
        }

        std::string hay = html_str;
        size_t imgPos = 0;
        while ((imgPos = hay.find("<img", imgPos)) != std::string::npos) {
            size_t srcPos = hay.find("src=\"", imgPos);
            if (srcPos == std::string::npos) break;
            size_t srcEnd = hay.find('"', srcPos + 5);
            if (srcEnd == std::string::npos) break;
            std::string src = hay.substr(srcPos + 5, srcEnd - srcPos - 5);
            imgPos = srcEnd + 1;
            // 跳过 URL / data: 引用（远程不落盘拷贝）
            if (src.find("://") != std::string::npos ||
                src.find("data:") != std::string::npos) {
                continue;
            }
            // md4c-html 对 src 里的中文/空格等输出 %XX（NEED_URL_ESC），
            // 磁盘文件名是解码后的原文：先解码再判绝对路径/拷贝。
            src = url_decode(src);
            // 跳过绝对路径引用（/ 或盘符）
            if (!src.empty() && (src[0] == '/' || src[0] == '\\')) continue;
            if (src.size() > 2 && src[1] == ':') continue;
            std::string rel = src;
            if (!isSafeRelative(rel)) continue;   // ".." escape: skip
            for (auto& c : rel) if (c == '/') c = '\\';
            std::string srcPath = docBase + rel;
            DWORD attr = GetFileAttributesW(widen(srcPath).c_str());
            if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
                continue;
            }
            // 目标目录（可能含子目录，如 images/）
            std::string dstPath = build_dir + "\\" + rel;
            auto slash = dstPath.find_last_of("\\");
            if (slash != std::string::npos) {
                std::string sub = dstPath.substr(0, slash);
                size_t p = build_dir.size() + 1;
                while ((p = sub.find("\\", p)) != std::string::npos) {
                    CreateDirectoryW(widen(sub.substr(0, p)).c_str(), NULL);
                    p++;
                }
                CreateDirectoryW(widen(sub).c_str(), NULL);
            }
            // 目标已存在且大小相同 → 跳过复制（刷新不再重复拷贝未变化的图片）
            DWORD da = GetFileAttributesW(widen(dstPath).c_str());
            if (da != INVALID_FILE_ATTRIBUTES && !(da & FILE_ATTRIBUTE_DIRECTORY)) {
                HANDLE hs = CreateFileW(widen(srcPath).c_str(), GENERIC_READ, FILE_SHARE_READ,
                                        NULL, OPEN_EXISTING, 0, NULL);
                HANDLE hd = CreateFileW(widen(dstPath).c_str(), GENERIC_READ, FILE_SHARE_READ,
                                        NULL, OPEN_EXISTING, 0, NULL);
                if (hs != INVALID_HANDLE_VALUE && hd != INVALID_HANDLE_VALUE) {
                    LARGE_INTEGER szs, szd;
                    GetFileSizeEx(hs, &szs);
                    GetFileSizeEx(hd, &szd);
                    CloseHandle(hs);
                    CloseHandle(hd);
                    if (szs.QuadPart == szd.QuadPart) continue;
                }
            }
            CopyFileW(widen(srcPath).c_str(), widen(dstPath).c_str(), FALSE);
        }
    }

    // 8. 完整 HTML 写入 build_dir/index.html（浏览器打开入口）。
    //    写盘副本先净化：Markdown 内嵌的 <script>/<iframe>/事件属性/
    //    javascript: 等原样进入 file:// 页面会在浏览器执行；内嵌
    //    litehtml 预览不执行脚本，所以只净化浏览器副本，返回值不变。
    if (!build_dir.empty()) {
        std::string browserHtml = html_str;
        sanitizeBrowserHtml(browserHtml);
        std::string indexPath = build_dir + "\\index.html";
        FILE* f = _wfopen(widen(indexPath).c_str(), L"wb");
        if (f) {
            fwrite(browserHtml.data(), 1, browserHtml.size(), f);
            fclose(f);
        }
    }

    return html_str;
}

// ---------------------------------------------------------------------------
// 轻量标题提取（extractOutlineHeadings）
//
// 为什么不用 md_to_html 顺带拿：那条路要跑公式/mermaid FFI 渲染、写 PNG、
// 拼整页 HTML——面板只是想显示个目录，尤其预览关闭时根本没理由拉起整条
// 渲染链。这里只调 md_parse 收集 MD_BLOCK_H，成本是一次纯 C 解析。
//
// id 规则必须与 md_to_html 的 <hN> 注入逐字一致（按文档顺序 toc-N、跳过
// 空标题），否则预览开启时大纲点击会跳错锚点。
// ---------------------------------------------------------------------------
namespace {

struct OutlineCtx {
    std::vector<PreviewHeading>* out = nullptr;
    bool  inHeading = false;
    unsigned level = 0;
    std::string text;
    // 行号要用"累计扫描"而不是"每行从头数 \n"：后者每个标题都回扫一遍，
    // 整体退化成 O(N*M)。md4c 的 text 回调给出的是指向原文的指针且按文档
    // 顺序前移，所以只需从上次扫到的位置继续往前数换行即可，全程 O(N)。
    const char* src = nullptr;   // 原文起始（用于算指针偏移）
    size_t scanned = 0;          // 已扫描到的字节下标
    int    curLine = 1;          // 行号游标（1-based）
    bool   haveLine = false;     // 当前标题的首个文本是否已定位
    int    headingLine = 0;
};

int oe_enter_block(MD_BLOCKTYPE type, void* detail, void* userdata) {
    auto* c = static_cast<OutlineCtx*>(userdata);
    if (type == MD_BLOCK_H && detail) {
        c->inHeading = true;
        c->level = static_cast<const MD_BLOCK_H_DETAIL*>(detail)->level;
        c->text.clear();
        c->haveLine = false;    // 等第一个文本回调来定位行号
        c->headingLine = 0;
    }
    return 0;
}

int oe_leave_block(MD_BLOCKTYPE type, void*, void* userdata) {
    auto* c = static_cast<OutlineCtx*>(userdata);
    if (type != MD_BLOCK_H || !c->inHeading) return 0;
    c->inHeading = false;
    // 与 md_to_html 的标题文本清理一致：去首尾空白；空标题跳过（不计 id）
    const size_t first = c->text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return 0;
    const size_t last = c->text.find_last_not_of(" \t\r\n");
    PreviewHeading h;
    h.level = (int)c->level;
    h.text = c->text.substr(first, last - first + 1);
    h.id = "toc-" + std::to_string(c->out->size());
    h.line = c->headingLine;   // 0 = 未能定位（理论上不会发生；见 src/haveLine）
    c->out->push_back(std::move(h));
    return 0;
}

int oe_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
    auto* c = static_cast<OutlineCtx*>(userdata);
    if (!c->inHeading) return 0;
    // 用首个文本回调的指针定位该标题所在行：text 指向原文内部（md4c 不会
    // 帮我们复制），所以 offset = text - src，再把行号游标推到这个位置。
    if (!c->haveLine && c->src && text >= c->src) {
        const size_t off = static_cast<size_t>(text - c->src);
        if (off >= c->scanned) {          // 只往前推进（正常情况必然成立）
            for (size_t i = c->scanned; i < off; ++i)
                if (c->src[i] == '\n') ++c->curLine;
            c->scanned = off;
        }
        c->haveLine = true;
        c->headingLine = c->curLine;
    }
    switch (type) {
    case MD_TEXT_NORMAL:
    case MD_TEXT_CODE:        // 行内 `code`：与预览侧"剥标签留文本"一致
    case MD_TEXT_LATEXMATH:
    case MD_TEXT_NULLCHAR:
        c->text.append(text, size);
        break;
    case MD_TEXT_ENTITY: {
        // 与 md_to_html 同一组实体；其余按字面保留（md4c 不认识列表外实体）
        const std::string ent(text, size);
        if      (ent == "&amp;")  c->text.push_back('&');
        else if (ent == "&lt;")   c->text.push_back('<');
        else if (ent == "&gt;")   c->text.push_back('>');
        else if (ent == "&quot;") c->text.push_back('"');
        else if (ent == "&#39;")  c->text.push_back('\'');
        else if (ent == "&nbsp;") c->text.push_back(' ');
        else                      c->text += ent;
        break;
    }
    // 硬折行/软折行都折叠成空格：面板行高固定，标签里出现 '\n' 会被 fl_draw
    // 当成多行文本画出行外（预览侧同一情况也是坏的，这里不复制坏行为）。
    case MD_TEXT_BR:
    case MD_TEXT_SOFTBR: c->text.push_back(' '); break;
    default: break;
    }
    return 0;
}

// ★ enter_span / leave_span 必须是真回调、不能是 NULL：md4c.c 的
//   MD_ENTER_SPAN / MD_LEAVE_SPAN 宏是 `ret = ctx->parser.enter_span(...)`
//   直接调用，没有任何 NULL 保护。只填 block/text 回调的话，文档里第一个
//   行内标记（`代码`、**粗体**、[链接](…)）就会间接调用空函数指针，
//   崩成 0xC0000005 at 0（2026-10-20 实测：侧栏开着打开任何 md 必崩，
//   栈帧全落在 VCRUNTIME140 的异常传播路径上，很难第一眼看出根因）。
//   md4c.h 只承诺 syntax 可以是 NULL；debug_log 也有 MD_LOG 的 NULL 保护，
//   但这两个一起给空实现，省得后人再踩。
int oe_noop_span(MD_SPANTYPE, void*, void*) { return 0; }
void oe_noop_log(const char*, void*) {}

} // namespace

void extractOutlineHeadings(const std::string& markdown,
                            std::vector<PreviewHeading>& headings)
{
    headings.clear();
    OutlineCtx ctx;
    ctx.out = &headings;
    ctx.src = markdown.data();   // 行号定位的基准（见 oe_text）
    MD_PARSER parser{};
    parser.abi_version = 0;
    // 与 md_to_html 完全相同的扩展开关：标题识别不受这些开关影响，但保持
    // 一致可以在加新扩展时少一处"两边行为不同"的隐患。
    parser.flags = MD_FLAG_TABLES | MD_FLAG_PERMISSIVEAUTOLINKS | MD_FLAG_TASKLISTS | MD_FLAG_STRIKETHROUGH
                 | MD_FLAG_SUPERSCRIPTS | MD_FLAG_SUBSCRIPTS
                 | MD_FLAG_HIGHLIGHT | MD_FLAG_FOOTNOTES | MD_FLAG_ADMONITIONS;
    parser.enter_block = oe_enter_block;
    parser.leave_block = oe_leave_block;
    // span 回调见 oe_noop_span 上方的★注释：必须非 NULL
    parser.enter_span = oe_noop_span;
    parser.leave_span = oe_noop_span;
    parser.text = oe_text;
    parser.debug_log = oe_noop_log;
    parser.syntax = nullptr;   // md4c.h：Reserved. Set to NULL.
    md_parse(markdown.data(), (MD_SIZE)markdown.size(), &parser, &ctx);
}

// 去掉首尾空白/CRLF 后剩下的内容
static std::string trimLine(const std::string &s)
{
    const size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    const size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

// setext 下划线：整行由 '=' 或 '-' 构成（至少一个）
static bool isSetextUnderline(const std::string &s)
{
    const std::string t = trimLine(s);
    if (t.empty()) return false;
    const char c = t[0];
    if (c != '=' && c != '-') return false;
    for (char ch : t)
        if (ch != c) return false;
    return true;
}

bool isHeadingSourceLine(const std::string &rawLine, const std::string &label,
                         const std::string &nextLine)
{
    if (label.empty()) return false;
    const std::string body = trimLine(rawLine);
    if (body.empty()) return false;

    size_t i = 0;
    while (i < body.size() && body[i] == '#') ++i;   // 数行首井号
    if (i > 0) {                                     // ATX：## 标题
        const size_t b = body.find_first_not_of(" \t", i);
        if (b == std::string::npos) return false;
        return body.compare(b, body.size() - b, label) == 0;
    }
    // 非 '#' 行：只有紧跟 setext 下划线时才是 setext 标题的正文行
    if (!isSetextUnderline(nextLine)) return false;
    return body == label;
}
