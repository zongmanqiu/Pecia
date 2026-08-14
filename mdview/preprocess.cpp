#include "preprocess.h"
#include <md4c-html.h>
#include <cstdio>
#include <cstdlib>
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

/* md4c 回调 */
static void collect_html(const MD_CHAR* chunk, MD_SIZE chunk_size, void* userdata)
{
    auto* out = static_cast<std::string*>(userdata);
    out->append(chunk, chunk_size);
}

/* Emoji 简码 → Unicode 映射 */
#include <unordered_map>

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

static std::string replace_emoji(const std::string& text)
{
    // 单遍扫描：遇到 ':' 才尝试匹配简码（原实现 100 个简码逐个 find+replace
    // 全文，O(100×N)；改为 O(N) 单遍，长文档明显更快）。
    const auto& m = emoji_map();
    std::string result;
    result.reserve(text.size() + 16);
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == ':') {
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
/* 把 RGBA 位图写入 PNG 文件（stb_image_write，stride 须为 w*4） */
static bool write_png_file(const char* path, const unsigned char* pixels, int w, int h, int stride)
{
    return stbi_write_png(path, w, h, 4, pixels, stride) != 0;
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

    CreateDirectoryA(out_dir.c_str(), NULL);

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
        if (GetFileAttributesA(out_path) != INVALID_FILE_ATTRIBUTES) continue;
        snprintf(fail_path, sizeof(fail_path), "%s\\%016llx.fail", out_dir.c_str(), (unsigned long long)h);
        if (GetFileAttributesA(fail_path) != INVALID_FILE_ATTRIBUTES) continue;
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
            FILE* f = fopen(fail_path, "wb");
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
                       std::vector<PreviewHeading>* headings_out)
{
    // 0. 替换 emoji 简码为 Unicode
    std::string md = replace_emoji(markdown);

    // 1a. 提取 $$...$$ 公式。位置判定行内/块级：$$ 所在行若前后还有文字
    // （非独占行）→ 行内小图；独占一行（行首起、行尾止）→ 块级大图。
    std::vector<FormulaItem> formulas;

    size_t pos = 0;
    while ((pos = md.find("$$", pos)) != std::string::npos) {
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
    const char* formula_langs[] = {"```math", "```katex", "```latex"};
    for (const char* lang : formula_langs) {
        pos = 0;
        while ((pos = md.find(lang, pos)) != std::string::npos) {
            size_t start = pos;
            size_t cs = pos + strlen(lang);
            if (cs < md.size() && md[cs] == '\r') cs++;
            if (cs < md.size() && md[cs] == '\n') cs++;
            size_t end = md.find("```", cs);
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

    // 1c. 提取 ```mermaid 代码块
    std::vector<std::string> mermaid_blocks;
    pos = 0;
    while ((pos = md.find("```mermaid", pos)) != std::string::npos) {
        size_t start = pos;
        size_t cs = pos + 10;
        if (cs < md.size() && md[cs] == '\r') cs++;
        if (cs < md.size() && md[cs] == '\n') cs++;
        size_t end = md.find("```", cs);
        if (end == std::string::npos) break;
        std::string content = md.substr(cs, end - cs);
        mermaid_blocks.push_back(content);
        std::string ph = "\xFF\xFE_MERMAID_" + std::to_string(mermaid_blocks.size()) + "_\xFF\xFE";
        md.replace(start, end + 3 - start, ph);
        pos = start + ph.size();
    }

    // 2. md4c 解析
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
    std::string formula_dir = build_dir + "\\formulas";
    std::vector<uint64_t> formula_hashes;
    if (!formulas.empty()) {
        render_formulas(formulas, formula_dir, formula_hashes);
    }

    // 3b. 渲染 mermaid 图表为 PNG（FFI 进程内渲染，resvg 0.47；哈希缓存+失败缓存+并行）
    std::string mermaid_dir = build_dir + "\\meimaid";
    std::vector<uint64_t> mermaid_hashes;
    if (!mermaid_blocks.empty()) {
        CreateDirectoryA(mermaid_dir.c_str(), NULL);
        struct MTask { std::string key; uint64_t hash; };
        std::vector<MTask> mtasks;
        mermaid_hashes.resize(mermaid_blocks.size());
        for (size_t i = 0; i < mermaid_blocks.size(); i++) {
            uint64_t h = fnv64(mermaid_blocks[i]);
            mermaid_hashes[i] = h;
            char out_path[MAX_PATH], fail_path[MAX_PATH];
            snprintf(out_path, sizeof(out_path), "%s\\%016llx.png", mermaid_dir.c_str(), (unsigned long long)h);
            if (GetFileAttributesA(out_path) != INVALID_FILE_ATTRIBUTES) continue;
            snprintf(fail_path, sizeof(fail_path), "%s\\%016llx.fail", mermaid_dir.c_str(), (unsigned long long)h);
            if (GetFileAttributesA(fail_path) != INVALID_FILE_ATTRIBUTES) continue;
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
                FILE* f = fopen(fail_path, "wb");
                if (f) fclose(f);
            }
        });
    }
    // 4. 替换公式占位符为 img 标签（块级居中大图 / 行内小图垂直居中）
    for (size_t i = 0; i < formulas.size(); i++) {
        std::string placeholder = "\xFF\xFE_FORMULA_" + std::to_string(i + 1) + "_\xFF\xFE";
        char img_tag[512];
        std::string fwd = formula_dir;
        for (auto& c : fwd) if (c == '\\') c = '/';
        const char *cls = formulas[i].display ? "formula-img" : "math-inline";
        snprintf(img_tag, sizeof(img_tag), "<img src=\"%s/%016llx.png\" class=\"%s\">",
                 fwd.c_str(), (unsigned long long)formula_hashes[i], cls);
        size_t p = 0;
        while ((p = body_html.find(placeholder, p)) != std::string::npos) {
            body_html.replace(p, placeholder.size(), img_tag);
            p += strlen(img_tag);
        }
    }

    // 4b. 替换 mermaid 占位符为 img 标签
    std::string mermaid_fwd = mermaid_dir;
    for (auto& c : mermaid_fwd) if (c == '\\') c = '/';
    for (size_t i = 0; i < mermaid_blocks.size(); i++) {
        std::string placeholder = "\xFF\xFE_MERMAID_" + std::to_string(i + 1) + "_\xFF\xFE";
        char img_tag[512];
        snprintf(img_tag, sizeof(img_tag), "<img src=\"%s/%016llx.png\" class=\"mermaid-img\">",
                 mermaid_fwd.c_str(), (unsigned long long)mermaid_hashes[i]);
        size_t p = 0;
        while ((p = body_html.find(placeholder, p)) != std::string::npos) {
            body_html.replace(p, placeholder.size(), img_tag);
            p += strlen(img_tag);
        }
    }

    // 5. 包装为完整 HTML（锚点 id 已注入，目录由外部处理）
    // 不再替换 [TOC]：目录由预览工具条的目录按钮（可折叠侧栏）提供，
    // 标题列表经 md_to_html 的 headings 出参返回给 UI 层。
    // 仍保留移除旧 [TOC] 文本的逻辑，避免页面上出现 [TOC] 文字。
    // 但仍保留替换旧版 [TOC] 为空的逻辑，避免页面上出现 [TOC] 文字
    {
        size_t p;
        while ((p = md.find("[TOC]")) != std::string::npos) md.replace(p, 5, "");
        while ((p = md.find("<!-- TOC -->")) != std::string::npos) md.replace(p, 12, "");
    }

    // 6. 包装为完整 HTML
    std::ostringstream html;
    html << "<!DOCTYPE html>\n<html>\n<head>\n"
         << "<meta charset=\"utf-8\">\n"
         << "<style>\n"
         << "* { line-height: 1.6; margin: 0; }\n"
         << "body {\n"
         << "  font-family: 'Microsoft YaHei', 'Segoe UI', Arial, Helvetica, sans-serif;\n"
         << "  font-size: 16px;\n"
         << "  color: #222;\n"
         << "  padding: 20px;\n"
         << "  margin: 0;\n"
         << "  background: #fff;\n"
         << "  word-wrap: break-word;\n"
         << "  overflow-wrap: break-word;\n"
         << "  word-break: break-word;\n"
         << "}\n"
         << "/* 所有连续块级元素之间统一 8px 间距 */\n"
         << "body > * + * { margin-top: 8px; }\n"
         << "/* 列表项之间同样 8px 间距 */\n"
         << "li + li { margin-top: 8px; }\n"
         << "a { color: #0366d6; }\n"
         << "ul, ol { padding-left: 32px; }\n"
         << "code { background: #f0f0f0; padding: 2px 6px; border-radius: 3px;\n"
         << "  font-family: 'Consolas', 'Courier New', monospace; font-size: 14px; }\n"
         << "pre { background: #f6f8fa; padding: 16px; border-radius: 6px;\n"
         << "  border: 1px solid #e1e4e8; white-space: pre-wrap; word-break: break-word; }\n"
         << "pre code { background: none; padding: 0; border-radius: 0; white-space: pre-wrap; word-break: break-word; }\n"
         << "h1 { font-size: 2em; font-weight: bold; }\n"
         << "h2 { font-size: 1.5em; font-weight: bold; }\n"
         << "h3 { font-size: 1.25em; font-weight: bold; }\n"
         << "h4 { font-size: 1em; font-weight: bold; }\n"
         << "h5 { font-size: 0.875em; font-weight: bold; }\n"
         << "h6 { font-size: 0.85em; font-weight: bold; }\n"
         << "table { border-collapse: collapse; }\n"
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
          << "mark, .highlight { background: #ffff8c; padding: 1px 4px; }\n"
          << "del, s { text-decoration: line-through; }\n"
          << ".task-list-item { list-style: none; }\n"
          << "ul ul { list-style-type: circle; }\n"
          << "ul ul ul { list-style-type: square; }\n"
          << "ol ol { list-style-type: lower-roman; }\n"
          << "ol ol ol { list-style-type: lower-alpha; }\n"
          << ".footnotes { margin-top: 2em; padding-top: 1em; border-top: 1px solid #ddd; font-size: 14px; color: #666; }\n"
         << ".footnotes li { margin-bottom: 4px; }\n"
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

    // 7. 把本地相对图片拷贝进 build_dir（保留相对路径结构）：
    //    浏览器打开 index.html 时按相对路径解析，图片完整显示。
    //    仅处理非 URL、非绝对路径的本地相对引用；公式/meimaid 为绝对路径，跳过。
    if (!doc_dir.empty() && !build_dir.empty()) {
        std::string docBase = doc_dir;
        for (auto& c : docBase) if (c == '/') c = '\\';
        if (!docBase.empty() && docBase.back() != '\\') docBase += '\\';

        std::string hay = html_str;
        size_t imgPos = 0;
        while ((imgPos = hay.find("<img", imgPos)) != std::string::npos) {
            size_t srcPos = hay.find("src=\"", imgPos);
            if (srcPos == std::string::npos) break;
            size_t srcEnd = hay.find('"', srcPos + 5);
            if (srcEnd == std::string::npos) break;
            std::string src = hay.substr(srcPos + 5, srcEnd - srcPos - 5);
            imgPos = srcEnd + 1;
            // 跳过 URL / 绝对路径 / data: 引用
            if (src.find("://") != std::string::npos ||
                src.find("data:") != std::string::npos ||
                (!src.empty() && (src[0] == '/' || src[0] == '\\')) ||
                (src.size() > 2 && src[1] == ':')) {
                continue;
            }
            std::string rel = src;
            for (auto& c : rel) if (c == '/') c = '\\';
            std::string srcPath = docBase + rel;
            DWORD attr = GetFileAttributesA(srcPath.c_str());
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
                    CreateDirectoryA(sub.substr(0, p).c_str(), NULL);
                    p++;
                }
                CreateDirectoryA(sub.c_str(), NULL);
            }
            // 目标已存在且大小相同 → 跳过复制（刷新不再重复拷贝未变化的图片）
            DWORD da = GetFileAttributesA(dstPath.c_str());
            if (da != INVALID_FILE_ATTRIBUTES && !(da & FILE_ATTRIBUTE_DIRECTORY)) {
                HANDLE hs = CreateFileA(srcPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                        NULL, OPEN_EXISTING, 0, NULL);
                HANDLE hd = CreateFileA(dstPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
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
            CopyFileA(srcPath.c_str(), dstPath.c_str(), FALSE);
        }
    }

    // 8. 完整 HTML 写入 build_dir/index.html（浏览器打开入口）
    if (!build_dir.empty()) {
        std::string indexPath = build_dir + "\\index.html";
        FILE* f = fopen(indexPath.c_str(), "wb");
        if (f) {
            fwrite(html_str.data(), 1, html_str.size(), f);
            fclose(f);
        }
    }

    return html_str;
}
