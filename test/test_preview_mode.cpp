// test_preview_mode.cpp - "空文档"判据（ui/PreviewMode.h）。
//
// 守的是 2026-10-31 用户报的产品问题：开着"仅预览"新建文件后，编辑区被隐藏、
// 预览面板一片空白，用户无处输入也点不动任何东西，会以为窗口卡死。修法有两半
// （空文档一律退回分屏 + 空文档禁用"仅预览"入口），两半都依赖同一个判据：
// "这份文档到底有没有可预览的内容"。这个测试把判据钉死。
//
// 纯函数、不建窗口，所以能进 ctest 常规跑。

#include <cstring>
#include <string>

#include "test/test_assert.h"
#include "ui/PreviewMode.h"

namespace {

bool blankOf(const char *s) {
    return preview_mode::isBlank(s, s ? std::strlen(s) : 0);
}

void test_null_and_empty() {
    CHECK(preview_mode::isBlank(nullptr, 0));
    CHECK(preview_mode::isBlank(nullptr, 16));   // 指针为空时长度不该被相信
    CHECK(preview_mode::isBlank("", 0));
}

void test_whitespace_only() {
    CHECK(blankOf(" "));
    CHECK(blankOf("   \t\t"));
    CHECK(blankOf("\r\n\r\n"));
    CHECK(blankOf(" \t\r\n\f\v "));
    // Markdown 里几个空行是最常见的"看起来是白纸"的文档
    CHECK(blankOf("\n\n\n"));
}

void test_non_blank() {
    CHECK(!blankOf("a"));
    CHECK(!blankOf("  a  "));
    CHECK(!blankOf("#"));
    CHECK(!blankOf("\n\n- 列表\n"));
    // 多字节字符至少占位，必须算有内容（不能因为首字节 >= 0x80 就跳过）
    CHECK(!blankOf("\xE4\xB8\xAD"));                 // "中"
    CHECK(!blankOf("\xEF\xBC\x8C"));                 // "，"（U+FF0C）
}

void test_fullwidth_space_is_content() {
    // 全角空格 U+3000 = E3 80 80：视觉上是一格空白，但它在渲染结果里是有宽度的
    // 字符。判成"空"会让用户在故意排版时被强行退出仅预览，所以明确算有内容。
    CHECK(!blankOf("\xE3\x80\x80"));
}

void test_utf8_bom() {
    const char bomOnly[] = "\xEF\xBB\xBF";
    CHECK(preview_mode::isBlank(bomOnly, 3));
    // 只带 BOM 的文件（某些编辑器保存空文档的产物）屏幕上是白纸
    CHECK(!blankOf("\xEF\xBB\xBF" "a"));
    CHECK(blankOf("\xEF\xBB\xBF" " \r\n"));
    // 原始字节 "EF BB BF" 出现在正文中间时不是 BOM，只认文件开头
    CHECK(!blankOf("x\xEF\xBB\xBF"));
}

void test_scan_cap_boundary() {
    // 恰好到上限：仍然逐字节扫描
    std::string atCap(preview_mode::kBlankScanCap, ' ');
    CHECK(preview_mode::isBlank(atCap.data(), atCap.size()));

    // 超出上限：直接判非空（不在自动刷新的 tick 上扫描大文档）。
    // 这条把"超过上限就不扫"的行为写成契约，改动上限时必须同步改这个断言。
    std::string overCap(preview_mode::kBlankScanCap + 1, ' ');
    CHECK(!preview_mode::isBlank(overCap.data(), overCap.size()));

    // 上限内有一个非空白字符 → 非空
    std::string almostBlank(preview_mode::kBlankScanCap - 1, ' ');
    almostBlank.push_back('x');
    CHECK(!preview_mode::isBlank(almostBlank.data(), almostBlank.size()));
}

void test_respects_length() {
    // 只扫前 len 字节：缓冲区尾部残留的内容不算进来（调用方给的是 buffer 长度，
    // 不是 C 字符串，二者在 Fl_Text_Buffer 上并不总是相等）。
    const char buf[] = "   \nreal text";
    CHECK(preview_mode::isBlank(buf, 4));
    CHECK(!preview_mode::isBlank(buf, sizeof(buf) - 1));
}

} // namespace

int main() {
    test_null_and_empty();
    test_whitespace_only();
    test_non_blank();
    test_fullwidth_space_is_content();
    test_utf8_bom();
    test_scan_cap_boundary();
    test_respects_length();

    const int fails = test::failCount();
    std::fprintf(stderr, "%d checks, %d failures\n", test::checkCount(), fails);
    return fails ? 1 : 0;
}
