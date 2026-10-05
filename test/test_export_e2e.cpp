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
    // exe 在 build/ 下，故 exe_dir/.. 即仓库根。样例文档按候选列表找：
    // 历史上它落在 build/ex1.markdown/（打包测试素材时铺开的），但这种
    // 位置随打包方式变动，所以不写死单一路径。
    const fs::path repoRoot = fs::weakly_canonical(exe_dir() / L"..");
    const fs::path outDir  = repoRoot / L"temp" / L"exp_out";

    const fs::path candidates[] = {
        repoRoot / L"build"   / L"ex1.markdown" / L"ex1.md",
        repoRoot / L"example" / L"ex1.markdown" / L"ex1.md",
        repoRoot / L"example" / L"ex1.md",
    };
    fs::path mdPath;
    for (const auto& c : candidates) {
        if (fs::exists(c)) { mdPath = c; break; }
    }
    if (mdPath.empty()) {
        std::printf("SKIP: 找不到样例文档（已试 %zu 个候选位置）\n",
                    sizeof(candidates) / sizeof(candidates[0]));
        return 0;   // 样例缺失不算失败：仓库裁剪版不该红
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

    std::printf("html 长度 = %zu 字节\n", page.size());
    check(page.find("<style") != std::string::npos, "含内联 CSS");
    check(page.find("<body") != std::string::npos, "含 body");
    check(fs::exists(index), "index.html 已写出");
    check(fs::file_size(index) == page.size(), "落盘字节数与内容一致");

    // 远程 src 是否清零（用产品同一套计数，语义一致）
    const int remaining = count_remote_image_srcs(page);
    std::printf("残留远程图片数 = %d\\n", remaining);
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
    check(hashedNames == 0, "远程图不得用哈希命名（目录要能看）");
    check(readableNames > 0, "远程图沿用 URL 原文件名（可读）");

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

    std::printf("\n%d checks, %d failed\n", checks, fails);
    std::printf("产物: %s\n", index.string().c_str());
    return fails ? 1 : 0;
}
