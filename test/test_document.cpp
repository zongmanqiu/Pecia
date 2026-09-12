// test_document.cpp - file-level integration tests for editor/Document:
// write a real file in a legacy encoding, loadFile() must detect the
// encoding, decode to UTF-8 in the buffer, and saveFile() must write
// bytes back that match the original file (round-trip byte identity).
// These tests exercise the whole chain that unit tests cannot reach:
// detectEncoding() + codepageToUtf8() + loadWithEncoding() + EOL
// detection + saveWithEncoding() + utf8ToCodepage().
//
// Note: Document::loadFile reports errors through UiBridge; with no
// bridge installed (uiBridge() == nullptr) those calls are no-ops, so
// error paths are testable without a GUI.
#include "test_assert.h"
#include "editor/Document.h"
#include "core/EncodingCore.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static fs::path g_scratch;

static void makeScratch() {
    g_scratch = fs::temp_directory_path() / "pecia_doc_test";
    fs::remove_all(g_scratch);
    fs::create_directories(g_scratch);
}

static fs::path writeFile(const char *name, const void *bytes, size_t len) {
    fs::path p = g_scratch / name;
    FILE *f = nullptr;
    fopen_s(&f, p.string().c_str(), "wb");
    if (!f) return {};
    fwrite(bytes, 1, len, f);
    fclose(f);
    return p;
}

static std::string readAll(const fs::path &p) {
    FILE *f = nullptr;
    fopen_s(&f, p.string().c_str(), "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string s((size_t)sz, '\0');
    if (sz > 0) fread(&s[0], 1, (size_t)sz, f);
    fclose(f);
    return s;
}

// ---------------------------------------------------------------------------
// Round-trip: write legacy bytes -> loadFile -> saveFile -> identical bytes
// ---------------------------------------------------------------------------

static void test_gbk_roundtrip_file() {
    // "中文测试" in GBK (CP936): D6 D0 CE C4 B2 E2 CA D4
    const unsigned char gbk[] = {0xD6, 0xD0, 0xCE, 0xC4, 0xB2, 0xE2, 0xCA, 0xD4};
    fs::path p = writeFile("t1.txt", gbk, sizeof(gbk));

    Document doc;
    CHECK(doc.loadFile(p.string().c_str()));
    CHECK(doc.encoding() == Encoding::GBK);
    // Decoded to UTF-8: E4 B8 AD E6 96 87 E6 B5 8B E8 AF 95
    CHECK_EQ(doc.buffer()->length(), 12);
    const unsigned char utf8exp[] = {0xE4, 0xB8, 0xAD, 0xE6, 0x96, 0x87,
                                     0xE6, 0xB5, 0x8B, 0xE8, 0xAF, 0x95};
    char *t = doc.buffer()->text();
    CHECK(t && memcmp(t, utf8exp, 12) == 0);
    free(t);
    CHECK(!doc.isDirty());   // load must not mark dirty

    // Round-trip: save back and compare bytes
    fs::path out = g_scratch / "t1_out.txt";
    CHECK(doc.saveFile(out.string().c_str()));
    std::string saved = readAll(out);
    CHECK(saved.size() == sizeof(gbk));
    CHECK(memcmp(saved.data(), gbk, sizeof(gbk)) == 0);
}

static void test_utf8_roundtrip_file() {
    // UTF-8 without BOM, multi-byte + ASCII + CRLF
    const char *utf8 = "hello \xE4\xB8\xAD\xE6\x96\x87\r\nworld";
    fs::path p = writeFile("t2.txt", utf8, (size_t)strlen(utf8));

    Document doc;
    CHECK(doc.loadFile(p.string().c_str()));
    CHECK(doc.encoding() == Encoding::UTF8);
    CHECK_EQ(doc.buffer()->length(), (int)strlen(utf8));
    char *t = doc.buffer()->text();
    CHECK(t && strcmp(t, utf8) == 0);
    free(t);
    CHECK(doc.eol() == EOL::CRLF);   // CRLF detected

    fs::path out = g_scratch / "t2_out.txt";
    CHECK(doc.saveFile(out.string().c_str()));
    std::string saved = readAll(out);
    CHECK(saved == utf8);
}

static void test_utf8_bom_roundtrip_file() {
    // UTF-8 with BOM: EF BB BF + content
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF, 'a', 'b', 'c'};
    fs::path p = writeFile("t3.txt", bom, sizeof(bom));

    Document doc;
    CHECK(doc.loadFile(p.string().c_str()));
    CHECK(doc.encoding() == Encoding::UTF8_BOM);
    // BOM stripped from buffer
    CHECK_EQ(doc.buffer()->length(), 3);
    char *t = doc.buffer()->text();
    CHECK(t && strcmp(t, "abc") == 0);
    free(t);

    fs::path out = g_scratch / "t3_out.txt";
    CHECK(doc.saveFile(out.string().c_str()));
    std::string saved = readAll(out);
    CHECK(saved.size() == sizeof(bom));
    CHECK(memcmp(saved.data(), bom, sizeof(bom)) == 0);   // BOM preserved
}

static void test_utf16le_roundtrip_file() {
    // UTF-16 LE with BOM: FF FE + "ab" (61 00 62 00)
    const unsigned char bom16[] = {0xFF, 0xFE, 0x61, 0x00, 0x62, 0x00};
    fs::path p = writeFile("t4.txt", bom16, sizeof(bom16));

    Document doc;
    CHECK(doc.loadFile(p.string().c_str()));
    CHECK(doc.encoding() == Encoding::UTF16_LE);
    CHECK_EQ(doc.buffer()->length(), 2);   // "ab"
    char *t = doc.buffer()->text();
    CHECK(t && strcmp(t, "ab") == 0);
    free(t);

    fs::path out = g_scratch / "t4_out.txt";
    CHECK(doc.saveFile(out.string().c_str()));
    std::string saved = readAll(out);
    CHECK(saved.size() == sizeof(bom16));
    CHECK(memcmp(saved.data(), bom16, sizeof(bom16)) == 0);
}

#if defined(_WIN32)
static void test_big5_roundtrip_file() {
    // "中文" in BIG5 (CP950): A4 A4 A4 E5
    const unsigned char big5[] = {0xA4, 0xA4, 0xA4, 0xE5};
    fs::path p = writeFile("t5.txt", big5, sizeof(big5));

    Document doc;
    CHECK(doc.loadFile(p.string().c_str()));
    CHECK(doc.encoding() == Encoding::BIG5);
    // U+4E2D U+6587 = E4 B8 AD E6 96 87
    const unsigned char utf8exp[] = {0xE4, 0xB8, 0xAD, 0xE6, 0x96, 0x87};
    CHECK_EQ(doc.buffer()->length(), 6);
    char *t = doc.buffer()->text();
    CHECK(t && memcmp(t, utf8exp, 6) == 0);
    free(t);

    fs::path out = g_scratch / "t5_out.txt";
    CHECK(doc.saveFile(out.string().c_str()));
    std::string saved = readAll(out);
    CHECK(saved.size() == sizeof(big5));
    CHECK(memcmp(saved.data(), big5, sizeof(big5)) == 0);
}
#endif

// ---------------------------------------------------------------------------
// Error paths
// ---------------------------------------------------------------------------

static void test_missing_file() {
    Document doc;
    fs::path missing = g_scratch / "nope.txt";
    CHECK(!doc.loadFile(missing.string().c_str()));   // no crash, returns false
    CHECK(doc.buffer()->length() == 0);
}

static void test_encoding_change_resaves() {
    // Load GBK, switch encoding to UTF-8, save -> file must be UTF-8 now.
    const unsigned char gbk[] = {0xD6, 0xD0, 0xCE, 0xC4};
    fs::path p = writeFile("t6.txt", gbk, sizeof(gbk));

    Document doc;
    CHECK(doc.loadFile(p.string().c_str()));
    doc.setEncoding(Encoding::UTF8);
    fs::path out = g_scratch / "t6_out.txt";
    CHECK(doc.saveFile(out.string().c_str()));

    std::string saved = readAll(out);
    const unsigned char utf8exp[] = {0xE4, 0xB8, 0xAD, 0xE6, 0x96, 0x87};
    CHECK(saved.size() == 6);
    CHECK(memcmp(saved.data(), utf8exp, 6) == 0);
}

// ---------------------------------------------------------------------------

static void run_all() {
    makeScratch();
    test_gbk_roundtrip_file();
    test_utf8_roundtrip_file();
    test_utf8_bom_roundtrip_file();
    test_utf16le_roundtrip_file();
#if defined(_WIN32)
    test_big5_roundtrip_file();
#endif
    test_missing_file();
    test_encoding_change_resaves();
}

int main() {
    run_all();
    int fails = test::failCount();
    std::fprintf(stderr, "%d checks, %d failures\n",
                 test::checkCount(), fails);
    return fails ? 1 : 0;
}
