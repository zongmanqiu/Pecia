// test_regex.cpp - lock-in tests for the pecia_regex interface
// (PCRE2 UTF-8 wrapper): matching, capture substitution, Unicode
// awareness, compile errors, and the DLL load/unload contract.
#include "test_assert.h"
#include "plugin/pecia_regex.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static void test_version() {
    char buf[32];
    int n = pr_version(buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(n < (int)sizeof(buf));
    CHECK(strncmp(buf, "10.", 3) == 0);
}

static void test_findall_basic() {
    const char *text = "abc 123 abc 456";
    int starts[8], ends[8];
    int n = pr_findall(text, (int)strlen(text), "abc", starts, ends, 8, nullptr, 0);
    CHECK_EQ(n, 2);
    CHECK_EQ(starts[0], 0);
    CHECK_EQ(ends[0], 3);
    CHECK_EQ(starts[1], 8);
    CHECK_EQ(ends[1], 11);
}

static void test_findall_utf8_chinese() {
    // \w+ must match whole Chinese words (UCP), and '.' must not split
    // a multi-byte character.
    const char *text = "你好世界 hello";
    int starts[8], ends[8];
    int n = pr_findall(text, (int)strlen(text), "\\w+", starts, ends, 8, nullptr, 0);
    CHECK_EQ(n, 2);                       // 你好世界 + hello
    CHECK_EQ(starts[0], 0);
    CHECK_EQ(ends[0], 12);                // 4 chars x 3 bytes

    n = pr_findall(text, (int)strlen(text), "世.", starts, ends, 8, nullptr, 0);
    CHECK_EQ(n, 1);
    CHECK_EQ(starts[0], 6);               // after 你好 (6 bytes)
    CHECK_EQ(ends[0], 12);                // 世 + 界
}

static void test_substitute_captures() {
    const char *text = "file_01.txt, file_02.txt";
    char *out = nullptr;
    int outLen = 0;
    int rc = pr_substitute_all(text, (int)strlen(text),
                               "file_(\\d+)\\.txt", "doc_$1.md",
                               &out, &outLen, nullptr, 0);
    CHECK(rc > 0);
    CHECK(std::string(out, outLen) == "doc_01.md, doc_02.md");
    pr_free(out);
}

static void test_substitute_unicode_class() {
    const char *text = "苹果123香蕉456";
    char *out = nullptr;
    int outLen = 0;
    int rc = pr_substitute_all(text, (int)strlen(text), "\\p{Han}+",
                               "[CJK]", &out, &outLen, nullptr, 0);
    CHECK(rc > 0);
    CHECK(std::string(out, outLen) == "[CJK]123[CJK]456");
    pr_free(out);
}

static void test_substitute_empty_no_match() {
    const char *text = "hello world";
    char *out = nullptr;
    int outLen = 0;
    int rc = pr_substitute_all(text, (int)strlen(text), "zzz", "X",
                               &out, &outLen, nullptr, 0);
    CHECK_EQ(rc, 0);          // no match - out stays null
    CHECK(out == nullptr);
}

static void test_compile_error() {
    char err[128] = "";
    int starts[4], ends[4];
    int rc = pr_findall("abc", 3, "(unclosed", starts, ends, 4, err, sizeof(err));
    CHECK_EQ(rc, -1);
    CHECK(err[0] != 0);
}

static void test_empty_match_advances() {
    // "a*" matches empty at every position - must not loop forever.
    const char *text = "ba";
    int starts[16], ends[16];
    int n = pr_findall(text, (int)strlen(text), "a*", starts, ends, 16, nullptr, 0);
    CHECK(n > 0);
    CHECK(n <= 16);
}

static void test_capture_limit() {
    const char *text = "a b c d e";
    int starts[2], ends[2];
    // maxMatches caps the returned count; the caller sizes the arrays
    // from a count query (maxMatches=0) when it needs them all.
    int n = pr_findall(text, (int)strlen(text), "\\w", starts, ends, 2, nullptr, 0);
    CHECK_EQ(n, 2);
    CHECK_EQ(starts[0], 0);
    CHECK_EQ(starts[1], 2);
    CHECK_EQ(ends[1], 3);
}

int main() {
    test_version();
    test_findall_basic();
    test_findall_utf8_chinese();
    test_substitute_captures();
    test_substitute_unicode_class();
    test_substitute_empty_no_match();
    test_compile_error();
    test_empty_match_advances();
    test_capture_limit();
    int fails = test::failCount();
    std::fprintf(stderr, "%d checks, %d failures\n",
                 test::checkCount(), fails);
    return fails ? 1 : 0;
}
