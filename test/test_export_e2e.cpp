// export_e2e.cpp - 端到端验证"Markdown > 导出 HTML"这条链路的产物质量。
//
// 复用与 MainWindow::exportHtmlToFile() 完全相同的两步调用，验证三件事：
//   1) md_to_html 产出非空、含完整 CSS 与正文
//   2) 远程图片全部被改写为同目录的本地文件（页面自包含）
//   3) 公式/mermaid 资源也落在输出目录里
//
// 不经过 GUI（跳过文件框），直接指定输出目录——文件框只是选路径，
// 真正决定产物质量的是这两步调用。
#include "mdview/image_export.h"
#include "mdview/preprocess.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <windows.h>

namespace fs = std::filesystem;

static std::string readAll(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// 仓库根与临时输出目录一律从 exe 位置推导，不写死绝对路径
// （与 core/PathUtils.h、build/build_readme.md 的约定一致）。
static fs::path exe_dir()
{
    wchar_t buf[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, buf, MAX_PATH)) return fs::current_path();
    return fs::path(buf).parent_path();
}

int main()
{
    // exe 在 build/ 下，故 exe_dir/.. 即仓库根，源码树在 repoRoot/main/。
    // 路径一律从 exe 位置推导，不出现绝对路径字面量。
    //
    // ★ 样例必须取**源码树里**的 main/example/ex1.markdown/ex1.md（用户
    //   2026-10-31："测试要严谨，最好使用源码里面的文件"）。早先候选表里写的是
    //   `repoRoot/example/...`，少了一层 main/，永远命不中，实际一直悄悄跑
    //   build/ 下那份打包副本 —— 那份谁都能改、也可能过期，拿它当断言的基准
    //   等于没有基准；而且找不到时会打 SKIP 直接 return 0，测试永远绿。
    const fs::path repoRoot = fs::weakly_canonical(exe_dir() / L"..");
    const fs::path outDir  = repoRoot / L"temp" / L"exp_out";

    const fs::path mdPath = repoRoot / L"main" / L"example" / L"ex1.markdown" / L"ex1.md";
    if (!fs::exists(mdPath)) {
        std::printf("FAIL: 源码样例缺失（应在 main/example/ex1.markdown/ex1.md）\n");
        return 1;
    }

    std::error_code ec;
    fs::remove_all(outDir, ec);
    fs::create_directories(outDir, ec);

    const std::string md = readAll(mdPath);
    if (md.empty()) { std::printf("FAIL: 读不到源 md\n"); return 1; }

    // 与 exportHtmlToFile() 一致：输出目录 + 源文档路径（解析相对图片）
    const std::string html = md_to_html(md, outDir.string(), mdPath.string(),
                                        nullptr, "");
    if (html.empty()) { std::printf("FAIL: md_to_html 返回空\n"); return 1; }

    // 第二步：远程图片本地化（导出路径允许补下载）
    std::string page = html;
    localize_remote_images(page, outDir.string(), default_remote_fetch());

    // ---- 回归门禁 ----

    // (1) base_font 形参只当【字体名】用。若调用方误传字号串（历史上
    // exportHtmlToFile 传的就是 "%dpx"），注入的 CSS 会变成
    // font-family: '15px' —— 浏览器直接忽略，等于设置完全失效。
    if (html.find("font-family: '") != std::string::npos) {
        const size_t q1 = html.find("font-family: '") + 14;
        const size_t q2 = html.find('\'', q1);
        if (q2 != std::string::npos) {
            const std::string first = html.substr(q1, q2 - q1);
            // 合法字体名不含数字+px 这类量纲后缀
            if (first.find("px") != std::string::npos ||
                first.find("pt") != std::string::npos ||
                first.find("em") != std::string::npos) {
                std::printf("FAIL: base_font 被当成字体名注入（含量纲单位：%s）\n",
                            first.c_str());
                return 1;
            }
        }
    }

    // (2) md_to_html() 不得自行本地化远程图片。
    // 这条断言守着 2026-10-05 修掉的那个回归。md_to_html() 是渲染与导出
    // 共用的，它的产物有两个消费者，解析相对路径的基准却不同：
    //   · 预览容器 MyContainer 以【源文档目录】为基准；
    //   · 浏览器/导出以【build_dir】为基准。
    // 一旦 md_to_html() 把远程 URL 改写成 "pecia_img_xxxx.png" 这类相对
    // 文件名，预览就会去源文档目录找一份根本不存在的文件，所有在线图片
    // 一起断链；且改写后 is_remote_url() 为 false，容器连下载分支都不进，
    // 重新渲染也救不回来。本地化只能由落盘路径显式调用。
    if (count_remote_image_srcs(html) == 0) {
        std::printf("FAIL: md_to_html() 把远程图片全吃掉了 —— 预览容器将无法下载\n");
        return 1;
    }
    std::printf("渲染产物保留的远程图片数 = %d\n", count_remote_image_srcs(html));
    if (html.find("pecia_img_") != std::string::npos) {
        std::printf("FAIL: md_to_html() 擅自本地化了远程图片"
                    "（预览会断链）\n");
        return 1;
    }

    const fs::path index = outDir / "index.html";
    {
        std::ofstream f(index, std::ios::binary);
        f << page;
    }

    // ---- 校验 ----
    int checks = 0, fails = 0;
    auto check = [&](bool ok, const char* what) {
        ++checks;
        if (!ok) { ++fails; std::printf("  FAIL  %s\n", what); }
        else      { std::printf("  ok    %s\n", what); }
    };

    // (3) 行内背景盒高度一致性（2026-10-20 用户反馈：同一段文字里 `code`
    //     的灰底与 ==高亮== 的黄底一高一低，脚注里的 code 尤其高）。
    //     实测两处根因都在 CSS，而且只有看渲染结果才发现：
    //       · `li > * + *` 这类块间距规则误伤了行内元素 —— 脚注里 <code>
    //         前面有 <strong>，于是吃到 8px margin-top；litehtml 把行内
    //         元素的 margin 算进背景盒，17px 变 25px，而正文 <p> 里不触发。
    //       · mark 曾用 inline-block 绕「跨行背景错位」，但 litehtml 的
    //         inline-block 背景盒取整行高（实测 26px），比 code 的 17px 高。
    //     断言落在契约上：①mark 不得是 inline-block（否则色块必然撑到行高）
    //     ②必须存在一条把行内元素 margin 归零的规则。
    {
        bool markInline = false;
        const size_t mk = html.find("mark, .highlight");
        if (mk != std::string::npos) {
            const size_t close = html.find('}', mk);
            if (close != std::string::npos &&
                html.substr(mk, close - mk).find("inline-block") == std::string::npos)
                markInline = true;
        }
        check(markInline, "mark 是行内盒（inline-block 会把色块撑成整行高）");

        bool inlineMarginOk = false;
        const std::string needle = "{ margin: 0; }";
        for (size_t p = html.find(needle); p != std::string::npos;
             p = html.find(needle, p + 1)) {
            // 选择器可能跨多行（行内标签清单较长），从上一个规则结尾开始取
            const size_t prev = html.rfind('}', p);
            const size_t start = (prev == std::string::npos) ? 0 : prev + 1;
            const std::string sel = html.substr(start, p - start);
            if (sel.find("code") != std::string::npos &&
                sel.find("mark") != std::string::npos) {
                inlineMarginOk = true;
                break;
            }
        }
        check(inlineMarginOk, "行内元素 margin 归零（块间距规则不得误伤行内盒）");

        // ③行内 code 的字号必须是相对单位（em/%）。
        //   写成绝对 px 时正文看着刚好，但标题会放大到 h1=32px / h2=24px，
        //   code 仍是 14px，色块只有 15px 高，在 24px 的标题文字里"特别矮"
        //   （2026-10-20 用户实测反馈）。断言仍落在契约上：色块要随上下文
        //   等比缩放，具体倍数不管。
        bool codeRelSize = false;
        const size_t c = html.find("code { background:");
        if (c != std::string::npos) {
            const size_t close = html.find('}', c);
            const size_t fs = html.find("font-size", c);
            if (close != std::string::npos && fs != std::string::npos && fs < close) {
                const size_t colon = html.find(':', fs);
                const size_t semi = html.find(';', colon);
                if (colon != std::string::npos && semi != std::string::npos) {
                    std::string val = html.substr(colon + 1, semi - colon - 1);
                    val.erase(0, val.find_first_not_of(" \t\r\n"));
                    val.erase(val.find_last_not_of(" \t\r\n") + 1);
                    codeRelSize = (val.find("em") != std::string::npos ||
                                   val.find('%') != std::string::npos);
                    std::printf("  code font-size = %s\n", val.c_str());
                }
            }
        }
        check(codeRelSize, "行内 code 字号相对父元素（绝对 px 会让标题内色块矮一截）");

        // ④标题内的行内 code 不得再缩小。
        //   全局 0.875em 在正文里刚好（16→14），但标题放大到 h1=32/h2=24 后
        //   只剩 28/21px，与同一行的粗体、斜体相比"明显更小"（2026-10-20
        //   用户实测反馈）。契约：h1..h6 **每一级**最终生效的字号都必须 >= 1em。
        //   不能只查第一条 "h1 code" 规则：同特异性的后置规则会覆盖前者，
        //   单独追加一条 "h3 code { font-size: 0.9em }" 就能让 h3 缩水而
        //   h1 仍合规（假绿）。这里逐级取"最后一个声明"作为生效值。
        bool headingCodeOk = true;
        for (int lv = 1; lv <= 6; ++lv) {
            const std::string sel = "h" + std::to_string(lv) + " code";
            std::string lastVal;
            bool haveDecl = false;
            for (size_t p = html.find(sel); p != std::string::npos;
                 p = html.find(sel, p + 1)) {
                // p 若落在某条规则的声明块内部（属性值里恰好含该串），跳过
                const size_t lb = html.rfind('{', p);
                const size_t rb = html.rfind('}', p);
                if (lb != std::string::npos && (rb == std::string::npos || lb > rb))
                    continue;
                const size_t open = html.find('{', p);
                const size_t close = (open == std::string::npos)
                                     ? std::string::npos : html.find('}', open);
                const size_t fs = (open == std::string::npos)
                                  ? std::string::npos : html.find("font-size", open);
                if (open == std::string::npos || close == std::string::npos ||
                    fs == std::string::npos || fs > close)
                    continue;
                const size_t colon = html.find(':', fs);
                const size_t semi = (colon == std::string::npos)
                                    ? std::string::npos : html.find(';', colon);
                if (semi == std::string::npos || semi > close) continue;
                std::string val = html.substr(colon + 1, semi - colon - 1);
                val.erase(0, val.find_first_not_of(" \t\r\n"));
                val.erase(val.find_last_not_of(" \t\r\n") + 1);
                lastVal = val;   // 后出现的同特异性规则覆盖前者
                haveDecl = true;
            }
            std::printf("  h%d code font-size = %s\n", lv,
                        haveDecl ? lastVal.c_str() : "(无规则，继承 0.875em)");
            // 必须是相对单位且 >= 1em；绝对 px 同样算缩水（无法跟随标题缩放）
            const bool ok = haveDecl &&
                            lastVal.find("em") != std::string::npos &&
                            std::atof(lastVal.c_str()) >= 1.0 - 1e-6;
            if (!ok) headingCodeOk = false;
        }
        check(headingCodeOk, "h1..h6 每一级内 code 字号都不小于标题（逐级校验生效值）");
    }

    std::printf("html 长度 = %zu 字节\n", page.size());
    check(page.find("<style") != std::string::npos, "含内联 CSS");
    check(page.find("<body") != std::string::npos, "含 body");
    check(fs::exists(index), "index.html 已写出");
    check(fs::file_size(index) == page.size(), "落盘字节数与内容一致");

    // 远程 src 是否清零（用产品同一套计数，语义一致）
    //
    // 这一项和下面两项依赖真实网络。断网时不能把"下载失败"报成"代码回归"，
    // 但也不能一律跳过（那会把真正的本地化 bug 藏起来）。所以先用产品自己的
    // fetch 打一次探针：只有探针也失败，才判定为环境问题并显式 SKIP。
    const int remaining = count_remote_image_srcs(page);
    bool networkOk = true;
    if (remaining > 0) {
        const fs::path probe = outDir / "__net_probe.tmp";
        RemoteFetchFn fetch = default_remote_fetch();
        networkOk = fetch && fetch("https://example.com/", probe.string());
        std::error_code rm;
        fs::remove(probe, rm);
        if (!networkOk) {
            std::printf("  SKIP  远程图片下载不可用（探针失败）——"
                        "本机可能离线；跳过 3 项网络相关断言\n");
        }
    }
    std::printf("残留远程图片数 = %d\n", remaining);
    if (networkOk)
        check(remaining == 0, "无残留远程 src（页面自包含）");

    // 本地化出来的图片文件必须真实存在且非空
    int imgFiles = 0;
    for (const auto& e : fs::directory_iterator(outDir)) {
        if (e.is_regular_file()) {
            const std::string n = e.path().filename().string();
            // 数"图片类文件"而不是某个前缀 —— 前缀是实现细节，2026-10-05
            // 就因为把 "pecia_img_" 写死在这里，命名改成沿用 URL 原名后本
            // 地化明明成功（目录里躺着 result.png / 320x180.webp 等），这个
            // 计数却恒为 0。断言必须落在契约上，不是实现上。
            static const char* kExts[] = { ".png", ".jpg", ".jpeg", ".gif",
                                           ".svg", ".webp", ".bmp" };
            bool isImg = false;
            for (const char* x : kExts) {
                const size_t l = std::strlen(x);
                if (n.size() > l && n.compare(n.size() - l, l, x) == 0) {
                    isImg = true;
                    break;
                }
            }
            // 排除文档自带的样例图（sample-*），只数远程图本地化产物
            if (isImg && n.rfind("sample-", 0) != 0) {
                ++imgFiles;
                if (fs::file_size(e.path()) == 0) {
                    std::printf("  FAIL  %s 是 0 字节\n", n.c_str());
                    ++fails;
                }
            }
        }
    }
    std::printf("远程图片本地化文件数 = %d\n", imgFiles);
    if (networkOk)
        check(imgFiles > 0, "至少本地化出 1 张远程图");

    // ---- 本地化后的文件名必须可读（2026-10-05 用户反馈）----
    // 旧方案把远程图一律命名 "pecia_img_<hash>.png"，一个目录里全是哈希，
    // 用户完全认不出哪个是哪张图。改为沿用 URL 里的原始文件名；只有 URL
    // 本身没有可用名字时才退回 "image_<hash>.<ext>"。无论走哪条分支，
    // 都不该再出现 "pecia_img_" 前缀。
    int hashedNames = 0, readableNames = 0;
    for (const auto& e : fs::directory_iterator(outDir)) {
        const std::string fn = e.path().filename().string();
        if (fn.rfind("sample-", 0) == 0) continue;      // 文档自带样例，不参与
        static const char* kExts2[] = { ".png", ".jpg", ".jpeg", ".gif",
                                        ".svg", ".webp", ".bmp" };
        bool isImg = false;
        for (const char* x : kExts2) {
            const size_t l = std::strlen(x);
            if (fn.size() > l && fn.compare(fn.size() - l, l, x) == 0) {
                isImg = true;
                break;
            }
        }
        if (!isImg) continue;
        // 哈希名长这样：image_<8位十六进制>.<ext>，或旧方案的 pecia_img_<hash>
        const size_t dot = fn.find_last_of('.');
        const std::string stem = fn.substr(0, dot);
        bool allHex = stem.rfind("image_", 0) == 0 ||
                      stem.rfind("pecia_img_", 0) == 0;
        if (allHex) ++hashedNames;
        else ++readableNames;
    }
    std::printf("可读文件名 = %d  哈希式文件名 = %d\n", readableNames, hashedNames);
    if (networkOk) {
        check(hashedNames == 0, "远程图不得用哈希命名（目录要能看）");
        check(readableNames > 0, "远程图沿用 URL 原文件名（可读）");
    }

    // ---- 本地图片必须被拷进输出目录（2026-10-05 修的第二个 bug）----
    // 页面里本地图片的 src 是相对路径（sample-png.png），浏览器按【输出
    // 目录】解析。若没把源文档旁的实体文件拷过去，导出页上这些图全断。
    // 曾经踩的坑：doc_dir 传进来的是【文件全路径】，拷贝时直接往上拼一级，
    // 得到 "...\ex1.markdown\ex1.md\sample-png.png" → 必然找不到。
    // 这里逐个验证：页面上每个非远程 src，在输出目录里都必须真实存在。
    int localRefs = 0, localMissing = 0;
    for (size_t p = 0; (p = page.find("<img", p)) != std::string::npos;) {
        const size_t sp = page.find("src=\"", p);
        if (sp == std::string::npos) break;
        const size_t se = page.find('"', sp + 5);
        if (se == std::string::npos) break;
        const std::string src = page.substr(sp + 5, se - sp - 5);
        p = se + 1;
        // 只看相对引用：远程 URL、data:、绝对路径（公式/mermaid）都不算
        if (src.find("://") != std::string::npos ||
            src.find("data:") != std::string::npos ||
            src.empty() || src[0] == '/' || src[0] == '\\' ||
            (src.size() > 2 && src[1] == ':'))
            continue;
        ++localRefs;
        if (!fs::exists(outDir / fs::path(src)))
            ++localMissing;
    }
    // ---- 产物目录里不得出现 .img 中间产物（2026-10-05 用户反馈）----
// 用户原话："为什么还要生成 img 后缀文件？我打不开啊，这不纯浪费空间？"
// 旧方案把远程图先下到 temp\*.img 缓存、导出时再拷进产物目录 —— 同一张图
// 存两份，缓存名 pecia_img_<hash>.img 既不可读也打不开。现在远程图直接落在
// 产物目录、用 URL 原名与真实扩展名，产物目录即完整资源目录（浏览器直接
// 打开即可，导出只需整个文件夹复制过去）。
// 这里同时守两件事：①目录里没有 .img ②temp 根下不再新增 pecia_img_ 缓存。
int imgExt = 0;
    for (const auto& e : fs::recursive_directory_iterator(outDir)) {
        if (e.is_regular_file() && e.path().extension() == ".img") ++imgExt;
    }
    std::printf("产物目录里的 .img 文件数 = %d\n", imgExt);
    check(imgExt == 0, "产物目录不得含 .img 中间产物（每张图只存一份、且用原名）");

    // temp 根下的 pecia_img_<hash>.img 缓存：先删掉历史残留（旧版本留下的，
    // 不删的话这条断言检查的是"旧垃圾还在不在"而不是"新代码会不会产生"），
    // 再跑一遍本地化，确认不会重新长出来。
    const fs::path tempRoot = repoRoot / L"build" / L"temp";
    std::error_code ec3;
    fs::create_directories(tempRoot, ec3);
    int staleBefore = 0;
    for (const auto& e : fs::directory_iterator(tempRoot)) {
        if (e.is_regular_file() &&
            e.path().filename().string().rfind("pecia_img_", 0) == 0) {
            fs::remove(e.path());
            ++staleBefore;
        }
    }
    std::string probe =
        "<html><body><img src=\"https://md-export-test.invalid/x.png\"></body></html>";
    localize_remote_images(probe, outDir.string(), nullptr);   // 渲染路径：不触网
    int hashedCaches = 0;
    for (const auto& e : fs::directory_iterator(tempRoot)) {
        if (e.is_regular_file() &&
            e.path().filename().string().rfind("pecia_img_", 0) == 0)
            ++hashedCaches;
    }
    std::printf("清理旧残留 %d 个，本地化后新产生的哈希缓存 = %d\n",
                staleBefore, hashedCaches);
    check(hashedCaches == 0, "不得再产生 pecia_img_<hash>.img 缓存（旧文件已清理）");

    std::printf("本地图片引用 = %d  缺失 = %d\n", localRefs, localMissing);
    check(localRefs > 0, "页面含本地图片引用（样例文档应带 sample-*）");
    check(localMissing == 0, "本地图片全部已拷入输出目录");

    // 公式 / mermaid 资源
    const bool hasFormulaDir = fs::is_directory(outDir / "formulas");
    const bool hasMermaidDir = fs::is_directory(outDir / "mermaid");
    std::printf("formulas 目录=%d  mermaid 目录=%d\n", (int)hasFormulaDir,
                (int)hasMermaidDir);

    // ---- 双形态门禁：doc_dir 的两种传法都必须能把本地图拷进 build_dir ----
    // 上面那段用的是【文件全路径】（形态 A，导出路径）。但 MainWindow 预览
    // 路径传的是【目录 + 尾分隔符】（形态 B）—— "在浏览器里打开"走的就是它。
    // 曾经只测形态 A，导致 2026-10-05 修完形态 A 时顺手把形态 B 弄坏：
    // 剥离逻辑"先无条件弹尾分隔符再无条件砍最后一级"把目录名吃掉一层，
    // 预览页里的 sample-* 全部断链，而导出页一切正常 —— 两者症状相反，
    // 只测一边必然漏。这里两种形态各跑一遍，输出到不同目录互不干扰。
    {
        const fs::path outB = repoRoot / L"temp" / L"exp_out_dirform";
        std::error_code ec2;
        fs::remove_all(outB, ec2);
        fs::create_directories(outB, ec2);

        // 形态 B：目录 + 尾分隔符，与 MainWindow::renderPreview 的 base 一致
        std::string dirForm = mdPath.parent_path().string();
        if (!dirForm.empty() && dirForm.back() != '\\' && dirForm.back() != '/')
            dirForm += '\\';

        const std::string htmlB =
            md_to_html(md, outB.string(), dirForm, nullptr, std::string());

        int copiedB = 0;
        for (const auto& e : fs::directory_iterator(outB)) {
            const std::string fn = e.path().filename().string();
            if (fn.rfind("sample-", 0) == 0) ++copiedB;   // 只数本地图
        }
        std::printf("[形态B 目录+尾分隔符] 已拷入本地图 = %d\n", copiedB);
        check(copiedB > 0,
              "形态B（目录+尾分隔符，= 浏览器打开路径）本地图片也必须被拷入");

        // 反向对照：形态 B 下 HTML 里的本地图引用不得被改写 —— 相对路径是
        // 浏览器打开时的解析基准，改了就等于把源目录的图弄丢。
        bool relKept = htmlB.find("src=\"sample-png.png\"") != std::string::npos;
        check(relKept, "形态B 下本地图引用仍为相对路径（可被浏览器解析）");
    }

    // ---- 回归：表格单元格内的 ```math 不得吞掉后续标题 ----
    // 2026-10-19：目录结构说明.md 的表格里描述了 preprocess 模块，
    // 单元格内含字面量 "```math"。旧实现用朴素 text.find("```") 搜索，
    // 不检查行首，把表格内的 ```math 当作公式块开始，直到下一个行首
    // ``` 才结束——中间的所有标题（包括 "## 三、build/ 目录"）都被
    // 提取为公式替换为占位符，导致目录面板缺失这些章节。
    {
        const std::string trap =
            "| 模块 | 说明 |\n"
            "|---|---|\n"
            "| preprocess | 公式(```math/$$)提取与渲染 |\n"
            "\n"
            "## 被保护的标题\n"
            "\n"
            "正文段落。\n";
        const fs::path trapDir = repoRoot / L"temp" / L"trap_out";
        fs::remove_all(trapDir, ec);
        fs::create_directories(trapDir, ec);
        const std::string trapHtml =
            md_to_html(trap, trapDir.string(), std::string(), nullptr, std::string());
        check(trapHtml.find("<h2") != std::string::npos,
              "表格内含 ```math 时，后续 ## 标题仍渲染为 <h2>（不被公式提取吞掉）");
        check(trapHtml.find("被保护的标题") != std::string::npos,
              "表格内含 ```math 时，标题文字仍出现在 HTML 中");
    }

    // ---- 回归门禁：extractOutlineHeadings（2026-10-20 崩溃的直接防线）----
    // 根因：md4c.c 的 MD_ENTER_SPAN / MD_LEAVE_SPAN 宏是
    //   `ret = ctx->parser.enter_span(...)` 直接调用，没有 NULL 保护。
    // 只填 block/text 回调的实现，遇到文档里第一个行内标记（`代码`、
    // **粗体**、[链接](…)）就间接调用空函数指针 → 0xC0000005 at 0，
    // 且栈帧全落在 VCRUNTIME140 的异常传播路径上，极难第一眼定位。
    // 症状：侧栏开着打开任何含行内标记的 md 必崩（侧栏关着不崩，
    // 因为只有 refreshOutline() 走这条解析路径）。
    // 本用例跑不完即红；同时钉死与 md_to_html 的锚点一致性。
    {
        const char* mdProbe =
            "# 标题含 **粗体**、*斜体*、`代码` 与 [链接](https://example.com)\n"
            "\n"
            "正文段落，带 `行内` 标记。\n"
            "\n"
            "## 二级 `inline` 标题\n"
            "\n"
            "###\n"                        // 空标题：不计入、不占 id
            "\n"
            "#### 四级 &amp; 实体标题\n"
            "\n"
            "Setext 一级\n"
            "===========\n";
        std::vector<PreviewHeading> hs;
        extractOutlineHeadings(mdProbe, hs);
        check(hs.size() == 4, "标题数量 = 4（空标题不计）");
        if (hs.size() == 4) {
            check(hs[0].level == 1 && hs[0].id == "toc-0",
                  "第一条：level=1, id=toc-0");
            check(hs[0].text == "标题含 粗体、斜体、代码 与 链接",
                  "行内标记被剥掉、文本按序拼接（粗体/斜体/代码/链接）");
            check(hs[1].level == 2 && hs[1].id == "toc-1",
                  "第二条：level=2, id=toc-1");
            check(hs[1].text == "二级 inline 标题", "行内 code 文本并入标题");
            check(hs[2].level == 4 && hs[2].id == "toc-2",
                  "空标题被跳过，id 仍连续（toc-2）");
            check(hs[2].text == "四级 & 实体标题", "&amp; 实体被解码为 &");
            check(hs[3].level == 1 && hs[3].id == "toc-3",
                  "setext（===）标题也被识别（toc-3）");
        }
        // 锚点一致性：同一文档两条路径给出的 id 序列必须相同，
        // 否则预览开启时点击大纲会跳错位置。
        std::vector<PreviewHeading> ref;
        md_to_html(mdProbe, std::string(), std::string(), &ref, std::string());
        bool idsMatch = ref.size() == hs.size();
        for (size_t i = 0; idsMatch && i < ref.size(); ++i)
            if (ref[i].id != hs[i].id || ref[i].level != hs[i].level)
                idsMatch = false;
        check(idsMatch, "extractOutlineHeadings 与 md_to_html 的锚点 id 序列一致");
        // 行号：大纲点击要拿着它把光标送进源码，"第一条在第 1 行"这种事
        // 差一行就跳错位置，必须钉死。mdProbe 里四条标题分别在第
        // 1 / 5 / 9 / 11 行（空标题那行不算，但它前面的空行照样占行号）。
        bool linesOk = hs.size() == 4;
        const int want[4] = {1, 5, 9, 11};
        for (size_t i = 0; linesOk && i < hs.size(); ++i)
            if (hs[i].line != want[i]) linesOk = false;
        check(linesOk, "源码行号正确（1/5/9/11，含 setext 取正文行）");
        // md_to_html 那条路是在 HTML 后处理里收标题的，拿不到源码偏移，
        // 契约就是"line 保持 0 = 未知"，UI 侧看到 0 会重提一次。
        bool refNoLine = !ref.empty();
        for (const auto &h : ref) if (h.line != 0) refNoLine = false;
        check(refNoLine, "md_to_html 产出的行号为 0（未知，由调用方补提）");
    }

    // ---- 行号的两个容易错的情形：代码块内的伪标题、缩进里的标题 ----
    {
        const char* mdFence = "```md\n"                      // 1
                         "# 代码块里的伪标题\n"          // 2（不是标题）
                         "## 再来一行\n"                 // 3（也不是）
                         "```\n"                        // 4
                         "\n"                           // 5
                         "## 真标题\n";                 // 6
        std::vector<PreviewHeading> hs;
        extractOutlineHeadings(mdFence, hs);
        check(hs.size() == 1, "围栏代码块里的 '#' 行不被当作标题");
        check(!hs.empty() && hs[0].line == 6,
              "代码块之后的标题行号正确（第 6 行，行号跨过了围栏内容）");

        const char* md2 = "> 引用里的标题\n"              // 1
                          "\n"                          // 2
                          "  ### 缩进三级标题\n";        // 3
        std::vector<PreviewHeading> hs2;
        extractOutlineHeadings(md2, hs2);
        check(hs2.size() == 1 && hs2[0].line == 3 && hs2[0].level == 3,
              "缩进/引用内的标题：level=3 且行号 = 3");
    }

    // ---- 行号漂移后的落点校验（isHeadingSourceLine）----
    // 大纲的行号是快照，用户之后增删行就漂了；落点前必须能认出"这一行到底
    // 是不是那个标题"，认不出来就交给调用方重提，绝不能跳到同名正文上去。
    {
        check(isHeadingSourceLine("## 标题\n", "标题"), "ATX 标题命中");
        check(isHeadingSourceLine("###### 六级标题\r\n", "六级标题"),
              "六级 ATX 且带 CRLF 也命中");
        check(!isHeadingSourceLine("## 别的\n", "标题"), "文字不符 → 不命中");
        check(!isHeadingSourceLine("标题\n", "标题", "下一行不是下划线\n"),
              "★ 同名正文行不算标题（行号漂移后不能误落到这里）");
        check(isHeadingSourceLine("标题\n", "标题", "====\n"),
              "setext：正文 + === 下划线 → 命中");
        check(isHeadingSourceLine("标题\n", "标题", "----\n"),
              "setext：正文 + --- 下划线 → 命中");
        check(!isHeadingSourceLine("   \n", "标题", "====\n"), "空行不命中");
        check(!isHeadingSourceLine("## 标题\n", ""), "label 为空 → 一律不命中");
    }

    std::printf("\n%d checks, %d failed\n", checks, fails);
    std::printf("产物: %s\n", index.string().c_str());
    return fails ? 1 : 0;
}
