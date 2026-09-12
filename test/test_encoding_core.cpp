// test_encoding_core.cpp - lock-in tests for core/EncodingCore.{h,cpp}:
// UTF-8 validation, BOM/UTF-16 detection heuristics, line-ending
// normalization, and trailing-whitespace trimming. These functions handle
// the bytes of arbitrary real-world files, so boundary cases (overlong
// encodings, truncated sequences, mixed line endings) are locked here.
#include "test_assert.h"
#include "core/EncodingCore.h"

#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// isValidUtf8
// ---------------------------------------------------------------------------

static void test_utf8_ascii() {
    const char *t = "plain ascii";
    CHECK(isValidUtf8((const unsigned char *)t, (int)strlen(t)));
    // empty / null inputs are trivially valid
    CHECK(isValidUtf8(nullptr, 0));
    CHECK(isValidUtf8((const unsigned char *)"", 0));
}

static void test_utf8_multibyte() {
    // "中文" in UTF-8: E4 B8 AD E6 96 87
    const unsigned char cn[] = {0xE4, 0xB8, 0xAD, 0xE6, 0x96, 0x87};
    CHECK(isValidUtf8(cn, 6));
    // 4-byte emoji: F0 9F 98 80
    const unsigned char emoji[] = {0xF0, 0x9F, 0x98, 0x80};
    CHECK(isValidUtf8(emoji, 4));
    // 2-byte: C3 A9 (é)
    const unsigned char e2[] = {0xC3, 0xA9};
    CHECK(isValidUtf8(e2, 2));
}

static void test_utf8_invalid() {
    // Isolated continuation byte
    const unsigned char cont[] = {0x80};
    CHECK(!isValidUtf8(cont, 1));
    // Illegal leading byte 0xFF
    const unsigned char ff[] = {0xFF};
    CHECK(!isValidUtf8(ff, 1));
    // Overlong encoding: C0 80 (would encode NUL with 2 bytes)
    const unsigned char overlong[] = {0xC0, 0x80};
    CHECK(!isValidUtf8(overlong, 2));
    // Truncated at buffer END is now tolerated (sampling window boundary,
    // see detectEncoding on partial buffers). Only truncation followed by
    // garbage, or mid-stream truncation, is invalid.
    const unsigned char trunc[] = {0xE4, 0xB8};
    CHECK(isValidUtf8(trunc, 2));   // trailing truncation: OK (window cut)
    // But truncation followed by a non-continuation byte is invalid
    const unsigned char truncBad[] = {0xE4, 0xB8, 0x41};
    CHECK(!isValidUtf8(truncBad, 3));
    // Bad continuation: E4 B8 41 ('A' is not a continuation byte)
    const unsigned char badcont[] = {0xE4, 0xB8, 0x41};
    CHECK(!isValidUtf8(badcont, 3));
    // UTF-16 surrogate encoded in UTF-8: ED A0 80 (U+D800)
    const unsigned char surrogate[] = {0xED, 0xA0, 0x80};
    CHECK(!isValidUtf8(surrogate, 3));
    // Code point above U+10FFFF: F5 80 80 80
    const unsigned char tooBig[] = {0xF5, 0x80, 0x80, 0x80};
    CHECK(!isValidUtf8(tooBig, 4));
}

// ---------------------------------------------------------------------------
// detectEncoding
// ---------------------------------------------------------------------------

static void test_detect_bom() {
    // UTF-8 BOM: EF BB BF
    const unsigned char bom8[] = {0xEF, 0xBB, 0xBF, 'a', 'b', 'c'};
    CHECK(detectEncoding(bom8, 6) == Encoding::UTF8_BOM);
    // UTF-16 LE BOM: FF FE
    const unsigned char bom16le[] = {0xFF, 0xFE, 'a', 0, 'b', 0};
    CHECK(detectEncoding(bom16le, 6) == Encoding::UTF16_LE);
    // UTF-16 BE BOM: FE FF
    const unsigned char bom16be[] = {0xFE, 0xFF, 0, 'a', 0, 'b'};
    CHECK(detectEncoding(bom16be, 6) == Encoding::UTF16_BE);
}

static void test_detect_utf16_no_bom() {
    // >=64 bytes with NULs in even positions -> UTF16_LE heuristic
    unsigned char data[64];
    for (int i = 0; i < 64; i += 2) { data[i] = 'a'; data[i + 1] = 0; }
    CHECK(detectEncoding(data, 64) == Encoding::UTF16_LE);
    // Short sample (<64 bytes) with NULs: falls through to UTF-8 check
    // (NUL is a valid ASCII byte, so it is valid UTF-8)
    unsigned char short16[] = {'a', 0, 'b', 0};
    CHECK(detectEncoding(short16, 4) == Encoding::UTF8);
}

static void test_detect_plain_utf8() {
    // Plain UTF-8 text (no BOM) -> UTF8
    const char *t = "hello world";
    CHECK(detectEncoding((const unsigned char *)t, (int)strlen(t)) == Encoding::UTF8);
    // Chinese UTF-8 without BOM -> UTF8 (must not mis-detect as GBK)
    const unsigned char cn[] = {0xE4, 0xB8, 0xAD, 0xE6, 0x96, 0x87};
    CHECK(detectEncoding(cn, 6) == Encoding::UTF8);
}

static void test_detect_ansi_fallback() {
    // 0xCE 0xC4 0xBC 0xFE is genuine GBK ("文...") - the new detector
    // now identifies it as GBK instead of falling back to ANSI.
    const unsigned char gbk[] = {0xCE, 0xC4, 0xBC, 0xFE};
    CHECK(detectEncoding(gbk, 4) == Encoding::GBK);
    // A single high byte cannot be classified - falls back to ANSI.
    const unsigned char lone[] = {0xA1};
    CHECK(detectEncoding(lone, 1) == Encoding::ANSI);
}

// A valid UTF-8 file whose sample buffer cuts a multi-byte character in
// half must still be detected as UTF-8, NOT as ANSI/GBK. This is the
// c1.md regression: DETECT_BUF=8192 landed inside "模" (E6 A8 A1),
// the truncated prefix failed isValidUtf8, and the file was GBK-decoded
// into mojibake. The caller (Document::loadFile) now extends the sample
// to a character boundary; detectEncoding itself must tolerate a trailing
// truncated sequence the same way.
static void test_detect_truncated_utf8_prefix() {
    // "模" = E6 A8 A1. Cut after the lead byte: E6 alone.
    const unsigned char cut1[] = {'a', 0xE6};
    CHECK(detectEncoding(cut1, 2) == Encoding::UTF8);
    // Cut after lead + one continuation: E6 A8.
    const unsigned char cut2[] = {'a', 0xE6, 0xA8};
    CHECK(detectEncoding(cut2, 3) == Encoding::UTF8);
    // A genuinely invalid sequence (bad continuation byte) must not be
    // accepted as UTF-8. 0x41 is not a continuation byte; the bytes fall
    // through to the code-page classifier (GBK accepts E6 41).
    const unsigned char bad[] = {'a', 0xE6, 0x41};
    CHECK(detectEncoding(bad, 3) != Encoding::UTF8);
}

// ---------------------------------------------------------------------------
// BIG5 / Shift-JIS / EUC-KR detection and conversion
// ---------------------------------------------------------------------------

static void test_detect_big5() {
    // "中文" in BIG5 (CP950): A4 A4 A4 E5
    const unsigned char big5[] = {0xA4, 0xA4, 0xA4, 0xE5};
    Encoding e = detectEncoding(big5, 4);
    CHECK(e == Encoding::BIG5);
}

static void test_detect_shift_jis() {
    // "こんにちは" in Shift-JIS (CP932)
    const unsigned char sjis[] = {0x82, 0xB1, 0x82, 0xF1, 0x82, 0xC9, 0x82, 0xBF, 0x82, 0xCD};
    Encoding e = detectEncoding(sjis, 10);
    CHECK(e == Encoding::SHIFT_JIS);
}

static void test_detect_euc_kr() {
    // "안녕하세요" in EUC-KR (CP949)
    const unsigned char euckr[] = {0xBE, 0xC8, 0xB3, 0xE7, 0xC7, 0xCF, 0xBC, 0xBC, 0xBF, 0xE4};
    Encoding e = detectEncoding(euckr, 10);
    CHECK(e == Encoding::EUC_KR);
}

#if defined(_WIN32)

static void test_codepage_roundtrip() {
    // BIG5 "中文" -> UTF-8 -> back to BIG5
    const unsigned char big5[] = {0xA4, 0xA4, 0xA4, 0xE5};
    char *utf8 = nullptr; int uLen = 0;
    CHECK(codepageToUtf8(950, (const char *)big5, 4, &utf8, &uLen));
    CHECK_EQ(uLen, 6);   // 中文 is 6 UTF-8 bytes
    CHECK(memcmp(utf8, "\xE4\xB8\xAD\xE6\x96\x87", 6) == 0);
    char *back = nullptr; int bLen = 0;
    CHECK(utf8ToCodepage(950, utf8, uLen, &back, &bLen));
    CHECK_EQ(bLen, 4);
    CHECK(memcmp(back, big5, 4) == 0);
    delete[] utf8;
    delete[] back;

    // Shift-JIS "こんにちは" round trip
    const unsigned char sjis[] = {0x82, 0xB1, 0x82, 0xF1, 0x82, 0xC9, 0x82, 0xBF, 0x82, 0xCD};
    CHECK(codepageToUtf8(932, (const char *)sjis, 10, &utf8, &uLen));
    CHECK_EQ(uLen, 15);   // 5 kana * 3 bytes
    CHECK(utf8ToCodepage(932, utf8, uLen, &back, &bLen));
    CHECK_EQ(bLen, 10);
    CHECK(memcmp(back, sjis, 10) == 0);
    delete[] utf8;
    delete[] back;

    // EUC-KR "안녕하세요" round trip
    const unsigned char euckr[] = {0xBE, 0xC8, 0xB3, 0xE7, 0xC7, 0xCF, 0xBC, 0xBC, 0xBF, 0xE4};
    CHECK(codepageToUtf8(949, (const char *)euckr, 10, &utf8, &uLen));
    CHECK_EQ(uLen, 15);   // 5 hangul * 3 bytes
    CHECK(utf8ToCodepage(949, utf8, uLen, &back, &bLen));
    CHECK_EQ(bLen, 10);
    CHECK(memcmp(back, euckr, 10) == 0);
    delete[] utf8;
    delete[] back;
}

#endif // _WIN32


// ---------------------------------------------------------------------------
// normalizeLineEndings
// ---------------------------------------------------------------------------

static void test_eol_lf_to_crlf() {
    const char *src = "line1\nline2\n";
    char *out = nullptr;
    int outLen = 0;
    normalizeLineEndings(src, (int)strlen(src), EOL::CRLF, &out, &outLen);
    CHECK(out != nullptr);
    CHECK_EQ(outLen, 14);
    CHECK(strcmp(out, "line1\r\nline2\r\n") == 0);
    delete[] out;
}

static void test_eol_crlf_to_lf() {
    const char *src = "line1\r\nline2\r\n";
    char *out = nullptr;
    int outLen = 0;
    normalizeLineEndings(src, (int)strlen(src), EOL::LF, &out, &outLen);
    CHECK(out != nullptr);
    CHECK_EQ(outLen, 12);
    CHECK(strcmp(out, "line1\nline2\n") == 0);
    delete[] out;
}

static void test_eol_already_normalized() {
    // LF text normalized to LF: no allocation needed
    const char *src = "line1\nline2\n";
    char *out = (char *)0x1;   // sentinel: must stay untouched
    int outLen = -1;
    normalizeLineEndings(src, (int)strlen(src), EOL::LF, &out, &outLen);
    CHECK(out == nullptr);
    CHECK_EQ(outLen, 0);
}

static void test_eol_mixed() {
    // Mixed \r\n, \n, \r -> all become CRLF
    const char *src = "a\r\nb\nc\rd";
    char *out = nullptr;
    int outLen = 0;
    normalizeLineEndings(src, (int)strlen(src), EOL::CRLF, &out, &outLen);
    CHECK(strcmp(out, "a\r\nb\r\nc\r\nd") == 0);
    delete[] out;

    // -> all become CR (classic Mac)
    normalizeLineEndings(src, (int)strlen(src), EOL::CR, &out, &outLen);
    CHECK(strcmp(out, "a\rb\rc\rd") == 0);
    delete[] out;
}

// ---------------------------------------------------------------------------
// trimTrailingWhitespace
// ---------------------------------------------------------------------------

static void test_trim_basic() {
    char buf[] = "hello   \nworld\t\nno trail\n";
    int newLen = trimTrailingWhitespace(buf, (int)strlen(buf));
    CHECK(strcmp(buf, "hello\nworld\nno trail\n") == 0);
    CHECK_EQ(newLen, (int)strlen("hello\nworld\nno trail\n"));
}

static void test_trim_all_spaces_line() {
    char buf[] = "   \n\t\nx\n";
    int newLen = trimTrailingWhitespace(buf, (int)strlen(buf));
    CHECK(strcmp(buf, "\n\nx\n") == 0);
    CHECK_EQ(newLen, 4);
}

static void test_trim_no_change() {
    char buf[] = "no trailing\nspaces here\n";
    int newLen = trimTrailingWhitespace(buf, (int)strlen(buf));
    CHECK(strcmp(buf, "no trailing\nspaces here\n") == 0);
    CHECK_EQ(newLen, (int)strlen("no trailing\nspaces here\n"));
}

static void test_trim_no_final_newline() {
    char buf[] = "line with spaces   ";
    int newLen = trimTrailingWhitespace(buf, (int)strlen(buf));
    CHECK(strcmp(buf, "line with spaces") == 0);
    CHECK_EQ(newLen, (int)strlen("line with spaces"));
}

// ---------------------------------------------------------------------------
// trimLeadingBlank / trimEndingBlank / expandTabsToSpaces (save formatting)
// ---------------------------------------------------------------------------

static void test_leading_blank() {
    char buf[] = "  \n\t\nhello\nworld\n";
    int newLen = trimLeadingBlank(buf, (int)strlen(buf));
    CHECK(strcmp(buf, "hello\nworld\n") == 0);
    CHECK_EQ(newLen, (int)strlen("hello\nworld\n"));
}

static void test_leading_blank_none() {
    char buf[] = "hello";
    int newLen = trimLeadingBlank(buf, (int)strlen(buf));
    CHECK(strcmp(buf, "hello") == 0);
    CHECK_EQ(newLen, 5);
}

static void test_ending_blank() {
    char buf[] = "hello\nworld\n  \n\t\n\n";
    int newLen = trimEndingBlank(buf, (int)strlen(buf));
    CHECK(strcmp(buf, "hello\nworld") == 0);
    CHECK_EQ(newLen, (int)strlen("hello\nworld"));
}

static void test_ending_blank_none() {
    char buf[] = "hello";
    int newLen = trimEndingBlank(buf, (int)strlen(buf));
    CHECK(strcmp(buf, "hello") == 0);
    CHECK_EQ(newLen, 5);
}

static void test_expand_tabs() {
    const char *src = "a\tb\n\t\n";
    char *out = nullptr;
    int outLen = 0;
    expandTabsToSpaces(src, (int)strlen(src), 4, &out, &outLen);
    CHECK(out != nullptr);
    CHECK(strcmp(out, "a    b\n    \n") == 0);
    CHECK_EQ(outLen, (int)strlen("a    b\n    \n"));
    delete[] out;
}

static void test_expand_tabs_width1() {
    const char *src = "a\tb";
    char *out = nullptr;
    int outLen = 0;
    expandTabsToSpaces(src, (int)strlen(src), 1, &out, &outLen);
    CHECK(strcmp(out, "a b") == 0);
    delete[] out;
}

static void test_expand_tabs_none() {
    const char *src = "no tabs here";
    char *out = nullptr;
    int outLen = 0;
    expandTabsToSpaces(src, (int)strlen(src), 4, &out, &outLen);
    CHECK(out != nullptr);
    CHECK(strcmp(out, "no tabs here") == 0);
    delete[] out;
}

// ---------------------------------------------------------------------------
// Codepage conversions (Windows: UTF-8 <-> wide <-> GBK round trips)
// ---------------------------------------------------------------------------

#if defined(_WIN32)

static void test_wide_roundtrip() {
    // "中" in UTF-8: E4 B8 AD
    const char *cn = "\xE4\xB8\xAD";
    wchar_t *w = nullptr;
    int wlen = 0;
    CHECK(utf8ToWide(cn, 3, &w, &wlen));
    CHECK(w != nullptr);
    CHECK_EQ(wlen, 1);
    CHECK_EQ((int)w[0], 0x4E2D);   // U+4E2D = 中

    char *back = nullptr;
    int backLen = 0;
    CHECK(wideToUtf8(w, wlen, &back, &backLen));
    CHECK_EQ(backLen, 3);
    CHECK(memcmp(back, cn, 3) == 0);
    delete[] w;
    delete[] back;
}

static void test_gbk_roundtrip() {
    // GBK bytes for "中文" (CP936): D6 D0 CE C4. The exact byte values
    // depend on the system's CP936 tables, so we assert round-trip
    // consistency plus a shape check (6 UTF-8 bytes) rather than exact
    // bytes - a locale where CP936 maps differently would otherwise
    // produce a brittle failure.
    const char *gbk = "\xD6\xD0\xCE\xC4";
    char *utf8 = nullptr;
    int uLen = 0;
    CHECK(gbkToUtf8(gbk, 4, &utf8, &uLen));
    CHECK(utf8 != nullptr);
    CHECK_EQ(uLen, 6);   // 中文 is 6 UTF-8 bytes

    // Reverse: UTF-8 -> GBK must reproduce the input exactly.
    char *back = nullptr;
    int bLen = 0;
    CHECK(utf8ToGbk(utf8, uLen, &back, &bLen));
    CHECK_EQ(bLen, 4);
    CHECK(memcmp(back, gbk, 4) == 0);
    delete[] utf8;
    delete[] back;
}

static void test_conversion_empty() {
    wchar_t *w = (wchar_t *)0x1;
    int wlen = -1;
    CHECK(utf8ToWide(nullptr, 0, &w, &wlen));
    CHECK(w == nullptr);
    CHECK_EQ(wlen, 0);
}

#endif // _WIN32

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

static void run_all() {
    test_utf8_ascii();
    test_utf8_multibyte();
    test_utf8_invalid();
    test_detect_bom();
    test_detect_utf16_no_bom();
    test_detect_plain_utf8();
    test_detect_ansi_fallback();
    test_detect_truncated_utf8_prefix();
    test_detect_big5();
    test_detect_shift_jis();
    test_detect_euc_kr();
    test_eol_lf_to_crlf();
    test_eol_crlf_to_lf();
    test_eol_already_normalized();
    test_eol_mixed();
    test_trim_basic();
    test_trim_all_spaces_line();
    test_trim_no_change();
    test_trim_no_final_newline();
    test_leading_blank();
    test_leading_blank_none();
    test_ending_blank();
    test_ending_blank_none();
    test_expand_tabs();
    test_expand_tabs_width1();
    test_expand_tabs_none();
#if defined(_WIN32)
    test_wide_roundtrip();
    test_gbk_roundtrip();
    test_conversion_empty();
    test_codepage_roundtrip();
#endif
}

int main() {
    run_all();
    int fails = test::failCount();
    std::fprintf(stderr, "%d checks, %d failures\n",
                 test::checkCount(), fails);
    return fails ? 1 : 0;
}
