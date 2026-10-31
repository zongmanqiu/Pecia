// test_image_attach.cpp - 粘贴图片/文件的纯逻辑（editor/ImageAttach.h）。
//
// 守的是四件容易悄悄错掉的事：
//   1. 目录名"消毒"——空格/括号不消毒，`![](我的 笔记(2026)/a.png)` 在
//      CommonMark 里会被括号截断，链接直接失效；中文必须原样保留。
//   2. 默认目录名跟着文档名走（bba.txt → bba），去掉的是**最后一个**后缀。
//   3. DIB → BGRA：行序自下而上要翻转、24bpp 行尾有对齐填充、以及 32bpp 但
//      alpha 全 0 的 DIB（截图工具的常态）必须按不透明处理，否则存出来是
//      一张全透明的 PNG。
//   4. 链接形态：图片用 ![]()、其它文件用 []()。项目从不生成绝对路径链接
//      （浏览器预览/导出会断图）。PNG 编码走系统 WIC，这里只验烟：文件头
//      是 \x89PNG 且大小合理。
//
// 剪贴板读写依赖真实系统状态，不在门禁内（那是 E2E 的活）。

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>      // GetTempPathW / MAX_PATH（PNG 落盘冒烟用）

#include "editor/ImageAttach.h"
#include "test/test_assert.h"

namespace {

void test_sanitize_dir_name() {
    CHECK(image_attach::sanitizeDirName("bba") == "bba");
    // 中文保留（md4c-html 输出 %XX，容器侧 url_decode 解回原文）
    CHECK(image_attach::sanitizeDirName("我的笔记") == "我的笔记");
    // 空格 + 括号：Markdown 裸链接里会截断，必须换掉
    CHECK(image_attach::sanitizeDirName("我的 笔记(2026)") == "我的_笔记_2026");
    // 连续可疑字符折叠成一个下划线
    CHECK(image_attach::sanitizeDirName("a  (b)") == "a_b");
    // Windows 非法字符
    CHECK(image_attach::sanitizeDirName("a/b\\c:d*e?f\"g<h>i|j") == "a_b_c_d_e_f_g_h_i_j");
    // URL 里有特殊含义的字符
    CHECK(image_attach::sanitizeDirName("a#b%c&d") == "a_b_c_d");
    // 首尾的点和空格会被 Windows 吃掉（或变成隐藏目录），剥掉
    CHECK(image_attach::sanitizeDirName("  a.b  ") == "a.b");
    CHECK(image_attach::sanitizeDirName("..hidden") == "hidden");
    // 空 / 全非法 → 兜底名，绝不返回空串（否则会拼出 "D:\abc\" 这种根目录路径）
    CHECK(image_attach::sanitizeDirName("") == "attach");
    CHECK(image_attach::sanitizeDirName("   ") == "attach");
    CHECK(image_attach::sanitizeDirName("()") == "attach");
    // Windows 保留设备名：CON 之类不能当目录名
    CHECK(image_attach::sanitizeDirName("con") == "con_");
    CHECK(image_attach::sanitizeDirName("CON") == "CON_");
}

void test_default_dir_name_for_doc() {
    CHECK(image_attach::defaultDirNameForDoc("D:\\abc\\bba.txt") == "bba");
    // 只去最后一个后缀（a.b.md → a.b）
    CHECK(image_attach::defaultDirNameForDoc("D:\\abc\\a.b.md") == "a.b");
    CHECK(image_attach::defaultDirNameForDoc("D:/abc/bba.markdown") == "bba");
    // 没有后缀 / 没有目录
    CHECK(image_attach::defaultDirNameForDoc("bba") == "bba");
    // 只有目录没有文件名 → 兜底
    CHECK(image_attach::defaultDirNameForDoc("D:\\abc\\") == "attach");
    // 文件名里带空格括号，同样要消毒
    CHECK(image_attach::defaultDirNameForDoc("D:\\abc\\我的 笔记(2026).md") ==
          "我的_笔记_2026");
}

void test_image_ext() {
    CHECK(image_attach::isImageExt(".png"));
    CHECK(image_attach::isImageExt(".PNG"));
    CHECK(image_attach::isImageExt(".jpeg"));
    CHECK(image_attach::isImageExt(".Gif"));
    CHECK(image_attach::isImageExt(".svg"));
    // webp 解码链没有（stb_image + resvg），收进来只会是一张显示不出来的图
    CHECK(!image_attach::isImageExt(".webp"));
    CHECK(!image_attach::isImageExt(".txt"));
    CHECK(!image_attach::isImageExt(""));
    CHECK(!image_attach::isImageExt("png"));   // 必须是带点的扩展名
}

void test_stamp_name_and_links() {
    CHECK(image_attach::stampName(2026, 10, 31, 18, 40, 12, 0, ".png") ==
          "img_20261031_184012.png");
    // 重名去重后缀
    CHECK(image_attach::stampName(2026, 10, 31, 18, 40, 12, 3, ".png") ==
          "img_20261031_184012-3.png");
    // ext 为空时默认 .png
    CHECK(image_attach::stampName(2026, 1, 2, 3, 4, 5, 0, "") ==
          "img_20260102_030405.png");

    CHECK(image_attach::imageLink("bba/img_x.png") == "![](bba/img_x.png)");
    CHECK(image_attach::fileLink("bba/a.zip", "a.zip") == "[a.zip](bba/a.zip)");
    // display 为空时用相对路径兜底
    CHECK(image_attach::fileLink("bba/a.zip", "") == "[bba/a.zip](bba/a.zip)");
}

void test_dib_24bpp_bottom_up() {
    // 2x2、24bpp、每行 6 字节按 4 字节对齐成 8 字节
    const unsigned char dib[16] = {
        // 第一行（内存里在下面 = 图像最底行）：B,G,R = 10,20,30 与 40,50,60 + 2 填充
        10, 20, 30, 40, 50, 60, 0xEE, 0xEE,
        // 第二行（内存里在上面 = 图像最顶行）：70,80,90 与 100,110,120
        70, 80, 90, 100, 110, 120, 0xEE, 0xEE,
    };
    std::vector<unsigned char> out;
    CHECK(image_attach::dibToBgra(dib, sizeof(dib), 2, 2, 24, 0, false, out));
    CHECK_EQ(out.size(), (size_t)16);
    // 输出第一行 = 图像最顶行（翻转生效），BGRA 顺序，alpha 补 255
    CHECK_EQ(out[0], 70);  CHECK_EQ(out[1], 80);  CHECK_EQ(out[2], 90);  CHECK_EQ(out[3], 255);
    CHECK_EQ(out[4], 100); CHECK_EQ(out[5], 110); CHECK_EQ(out[6], 120); CHECK_EQ(out[7], 255);
    // 第二行 = 底行
    CHECK_EQ(out[8], 10);  CHECK_EQ(out[9], 20);  CHECK_EQ(out[10], 30); CHECK_EQ(out[11], 255);
    CHECK_EQ(out[12], 40); CHECK_EQ(out[13], 50); CHECK_EQ(out[14], 60); CHECK_EQ(out[15], 255);

    // top-down 时不翻转
    std::vector<unsigned char> out2;
    CHECK(image_attach::dibToBgra(dib, sizeof(dib), 2, 2, 24, 0, true, out2));
    CHECK_EQ(out2[0], 10);
    CHECK_EQ(out2[8], 70);

    // 缓冲区不够 → 拒绝（宁可什么都不做，也不读越界）
    std::vector<unsigned char> out3;
    CHECK(!image_attach::dibToBgra(dib, 8, 2, 2, 24, 0, false, out3));
    // 不支持的位深
    CHECK(!image_attach::dibToBgra(dib, sizeof(dib), 2, 2, 8, 0, false, out3));
}

void test_dib_32bpp_alpha() {
    // 32bpp 但 alpha 全是 0：截图工具常见，必须按不透明处理，
    // 否则 PNG 存下来是一张看不见的图
    const unsigned char allZero[8] = { 1, 2, 3, 0, 4, 5, 6, 0 };
    std::vector<unsigned char> out;
    CHECK(image_attach::dibToBgra(allZero, sizeof(allZero), 2, 1, 32, 0, true, out));
    CHECK_EQ(out[3], 255);
    CHECK_EQ(out[7], 255);

    // 有真实 alpha 时原样保留（半透明图层不能被打成不透明）
    const unsigned char withAlpha[8] = { 1, 2, 3, 128, 4, 5, 6, 0 };
    std::vector<unsigned char> out2;
    CHECK(image_attach::dibToBgra(withAlpha, sizeof(withAlpha), 2, 1, 32, 0, true, out2));
    CHECK_EQ(out2[3], 128);
    CHECK_EQ(out2[7], 0);
}

void test_png_write_smoke() {
    // WIC 编码只验烟：文件头必须是 PNG 签名，且不是 0 字节。
    // 留一份在 %TEMP% 方便人工打开确认（不是门禁的一部分）。
    wchar_t tmp[MAX_PATH] = L"";
    const DWORD n = GetTempPathW(MAX_PATH, tmp);
    if (n == 0 || n >= MAX_PATH) { CHECK(false); return; }
    const std::wstring path = std::wstring(tmp) + L"pecia_test_attach.png";

    const int w = 8, h = 8;
    std::vector<unsigned char> bgra(static_cast<size_t>(w) * h * 4, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            unsigned char *p = &bgra[(static_cast<size_t>(y) * w + x) * 4];
            p[0] = (unsigned char)(x * 30);   // B
            p[1] = (unsigned char)(y * 30);   // G
            p[2] = 0x40;                      // R
            p[3] = 0xFF;                      // A
        }
    CHECK(image_attach::writePngBgra(path, w, h, bgra.data()));

    FILE *f = _wfopen(path.c_str(), L"rb");
    if (!f) { CHECK(false); return; }
    unsigned char head[8] = { 0 };
    const size_t rd = fread(head, 1, sizeof(head), f);
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fclose(f);
    CHECK_EQ(rd, (size_t)8);
    CHECK(head[0] == 0x89 && head[1] == 'P' && head[2] == 'N' && head[3] == 'G');
    CHECK(size > 60);   // 8x8 的 PNG 不该小到只剩签名

    // 路径不可写时必须返回 false，不能假装成功
    CHECK(!image_attach::writePngBgra(L"Z:\\no-such-dir\\x.png", w, h, bgra.data()));
}

} // namespace

int main() {
    test_sanitize_dir_name();
    test_default_dir_name_for_doc();
    test_image_ext();
    test_stamp_name_and_links();
    test_dib_24bpp_bottom_up();
    test_dib_32bpp_alpha();
    test_png_write_smoke();

    const int fails = test::failCount();
    std::fprintf(stderr, "%d checks, %d failures\n", test::checkCount(), fails);
    return fails ? 1 : 0;
}
