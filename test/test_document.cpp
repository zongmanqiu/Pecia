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
#include "core/UiBridge.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

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
// Round-trip: write bytes -> loadFile -> saveFile -> identical bytes
// ---------------------------------------------------------------------------

#if defined(_WIN32)
// Encode a wide string in `cp`; empty when the code page cannot represent it.
static std::string codepageBytes(int cp, const wchar_t *w) {
    int n = WideCharToMultiByte(cp, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s((size_t)n - 1, '\0');
    WideCharToMultiByte(cp, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}
#endif

static void test_ansi_roundtrip_file() {
#if defined(_WIN32)
    // ANSI is the system codepage. Use a non-ASCII character that this
    // machine can represent; skip on an ACP that cannot (rare).
    const int acp = (int)GetACP();
    const std::string bytes = codepageBytes(acp, L"\u00E9!");
    if (bytes.empty()) return;
    // Skip on an ACP where these bytes happen to be valid UTF-8 (é alone
    // can be a truncated sequence and pass the sampling-window check).
    if (isValidUtf8((const unsigned char *)bytes.data(), (int)bytes.size())) return;
    fs::path p = writeFile("t1.txt", bytes.data(), bytes.size());

    Document doc;
    CHECK(doc.loadFile(p.string().c_str()));
    CHECK(doc.encoding() == Encoding::ANSI);
    CHECK(!doc.isDirty());

    fs::path out = g_scratch / "t1_out.txt";
    CHECK(doc.saveFile(out.string().c_str()));
    std::string saved = readAll(out);
    CHECK(saved == bytes);
#endif
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

static void test_utf16be_roundtrip_file() {
    // UTF-16 BE with BOM: FE FF + "ab" (00 61 00 62)
    const unsigned char bom16[] = {0xFE, 0xFF, 0x00, 0x61, 0x00, 0x62};
    fs::path p = writeFile("t5.txt", bom16, sizeof(bom16));

    Document doc;
    CHECK(doc.loadFile(p.string().c_str()));
    CHECK(doc.encoding() == Encoding::UTF16_BE);
    CHECK_EQ(doc.buffer()->length(), 2);
    char *t = doc.buffer()->text();
    CHECK(t && strcmp(t, "ab") == 0);
    free(t);

    fs::path out = g_scratch / "t5_out.txt";
    CHECK(doc.saveFile(out.string().c_str()));
    std::string saved = readAll(out);
    CHECK(saved.size() == sizeof(bom16));
    CHECK(memcmp(saved.data(), bom16, sizeof(bom16)) == 0);
}

static void test_gb18030_roundtrip_file() {
#if defined(_WIN32)
    if (!isEncodingAvailable(Encoding::GB18030)) return;
    // Include an emoji so the file contains a 4-byte GB18030 sequence,
    // which is what makes detection distinguish it from ANSI.
    const wchar_t sample[] = { 0x4E2D, 0x6587, 0xD83D, 0xDE00, 0 };
    const std::string bytes = codepageBytes(54936, sample);
    if (bytes.empty()) return;
    fs::path p = writeFile("t6.txt", bytes.data(), bytes.size());

    Document doc;
    CHECK(doc.loadFile(p.string().c_str()));
    CHECK(doc.encoding() == Encoding::GB18030);
    const char *utf8exp = "\xE4\xB8\xAD\xE6\x96\x87\xF0\x9F\x98\x80";
    char *t = doc.buffer()->text();
    CHECK(t && strcmp(t, utf8exp) == 0);
    free(t);

    fs::path out = g_scratch / "t6_out.txt";
    CHECK(doc.saveFile(out.string().c_str()));
    std::string saved = readAll(out);
    CHECK(saved == bytes);
#endif
}

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
    // Load an ANSI file, switch to UTF-8, save -> the file must be UTF-8.
#if defined(_WIN32)
    const std::string bytes = codepageBytes((int)GetACP(), L"\u00E9!");
    if (bytes.empty()) return;
    if (isValidUtf8((const unsigned char *)bytes.data(), (int)bytes.size())) return;
    fs::path p = writeFile("t7.txt", bytes.data(), bytes.size());

    Document doc;
    CHECK(doc.loadFile(p.string().c_str()));
    char *text = doc.buffer()->text();
    CHECK(text != nullptr);
    const std::string expected = text ? text : "";
    free(text);

    doc.setEncoding(Encoding::UTF8);
    fs::path out = g_scratch / "t7_out.txt";
    CHECK(doc.saveFile(out.string().c_str()));
    CHECK(readAll(out) == expected);
#endif
}

static void test_can_encode() {
    Document doc;
    doc.buffer()->text("\xF0\x9F\x98\x80");   // U+1F600 emoji
    CHECK(doc.canEncode(Encoding::UTF8));
    CHECK(doc.canEncode(Encoding::UTF8_BOM));
    CHECK(doc.canEncode(Encoding::UTF16_LE));
    CHECK(doc.canEncode(Encoding::UTF16_BE));
    CHECK(!doc.canEncode(Encoding::ANSI));      // no ANSI codepage has emoji
    if (isEncodingAvailable(Encoding::GB18030))
        CHECK(doc.canEncode(Encoding::GB18030));
}

// ---------------------------------------------------------------------------
// B3 regression: a >= 2 GB file must be refused WITH A PROMPT, not silently.
// ftell() returns a 32-bit long on Windows and overflows to a negative
// value for such files; the old code then took the "sz < 0" branch and
// returned false without showing anything, so the user saw no reaction
// at all and the "too large" branch was unreachable.
// ---------------------------------------------------------------------------

// Records what the core layer asked the UI to show.
class RecordingBridge : public UiBridge {
public:
    int messageCount = 0;
    std::string lastTitle, lastMsg;
    void message(const char *title, const char *msg, const char *button) override {
        (void)button;
        ++messageCount;
        lastTitle = title ? title : "";
        lastMsg = msg ? msg : "";
    }
    ConfirmChoice confirm(const char *title, const char *msg,
                          const char *b0, const char *b1,
                          const char *b2) override {
        (void)title; (void)msg; (void)b0; (void)b1; (void)b2;
        return ConfirmChoice::Dismissed;   // act like the user cancelled
    }
};

// Create a file with a given LOGICAL size but ~zero disk usage (sparse).
static bool makeSparseFile(const fs::path &p, long long logicalSize) {
#if defined(_WIN32)
    HANDLE h = CreateFileW(p.wstring().c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD got = 0;
    FILE_SET_SPARSE_BUFFER sb;
    sb.SetSparse = TRUE;
    DeviceIoControl(h, FSCTL_SET_SPARSE, &sb, sizeof(sb), nullptr, 0, &got, nullptr);
    LARGE_INTEGER sz;
    sz.QuadPart = logicalSize;
    if (!SetFilePointerEx(h, sz, nullptr, FILE_BEGIN) || !SetEndOfFile(h)) {
        CloseHandle(h);
        return false;
    }
    CloseHandle(h);
    return true;
#else
    (void)p;
    (void)logicalSize;
    return false;   // test body is #if'd out on other platforms
#endif
}

static void test_over_2gb_refused_with_prompt() {
#if defined(_WIN32)
    fs::path p = g_scratch / "over_2gb.bin";
    fs::remove(p);
    CHECK(makeSparseFile(p, 2306867200LL));   // 2.2 GB logical, ~0 bytes on disk

    RecordingBridge bridge;
    setUiBridge(&bridge);

    Document doc;
    const bool opened = doc.loadFile(p.string().c_str());

    setUiBridge(nullptr);

    CHECK(!opened);                    // refused...
    CHECK(bridge.messageCount == 1);   // ...with exactly one error dialog
    CHECK(!bridge.lastTitle.empty());
    CHECK(!bridge.lastMsg.empty());    // and it is a real message, not an empty shell
    fs::remove(p);
#endif
}

static void test_under_2gb_not_refused() {
#if defined(_WIN32)
    // ~1.86 GB: below the ceiling, so it must reach the >100 MB confirm
    // (which our fake bridge dismisses) - NOT the "too large" error.
    fs::path p = g_scratch / "under_2gb.bin";
    fs::remove(p);
    CHECK(makeSparseFile(p, 2000000000LL));

    RecordingBridge bridge;
    setUiBridge(&bridge);

    Document doc;
    doc.loadFile(p.string().c_str());

    setUiBridge(nullptr);

    CHECK(bridge.messageCount == 0);   // no error dialog for a sub-2 GB file
    fs::remove(p);
#endif
}

// ---------------------------------------------------------------------------
// F1 regression: saving onto a target that carries the read-only attribute
// fails (the atomic MoveFileExW replace gets ERROR_ACCESS_DENIED) and must
// REPORT it. The old code deleted the temp file and returned false without
// showing anything, so the document stayed dirty and the user had no idea
// the save had failed.
// ---------------------------------------------------------------------------

static void setReadOnlyAttr(const fs::path &p, bool on) {
#if defined(_WIN32)
    SetFileAttributesW(p.wstring().c_str(),
                       on ? FILE_ATTRIBUTE_READONLY : FILE_ATTRIBUTE_NORMAL);
#else
    (void)p;
    (void)on;
#endif
}

static void test_save_to_readonly_target_fails_loudly() {
#if defined(_WIN32)
    fs::path p = g_scratch / "readonly_target.md";
    fs::remove(p);
    {
        FILE *f = nullptr;
        fopen_s(&f, p.string().c_str(), "wb");
        CHECK(f != nullptr);
        if (f) { fputs("# read-only target\r\n", f); fclose(f); }
    }
    setReadOnlyAttr(p, true);

    RecordingBridge bridge;
    setUiBridge(&bridge);

    Document doc;
    CHECK(doc.loadFile(p.string().c_str()));   // opens fine: the attribute is not m_readOnly
    doc.buffer()->append("edited\r\n");
    CHECK(doc.isDirty());
    const bool saved = doc.saveFile(p.string().c_str());

    setUiBridge(nullptr);

    CHECK(!saved);                      // the save failed...
    CHECK(bridge.messageCount == 1);    // ...and said so - NOT silently
    CHECK(!bridge.lastMsg.empty());

    setReadOnlyAttr(p, false);
    fs::remove(p);
#endif
}

static void test_save_writable_target_no_dialog() {
    // A normal save must not pop any dialog (guards against over-eager
    // error reporting on the success path).
    fs::path p = g_scratch / "writable_target.md";
    fs::remove(p);
    {
        FILE *f = nullptr;
        fopen_s(&f, p.string().c_str(), "wb");
        CHECK(f != nullptr);
        if (f) { fputs("# writable target\r\n", f); fclose(f); }
    }

    RecordingBridge bridge;
    setUiBridge(&bridge);

    Document doc;
    CHECK(doc.loadFile(p.string().c_str()));
    doc.buffer()->append("edited\r\n");
    CHECK(doc.saveFile(p.string().c_str()));

    setUiBridge(nullptr);

    CHECK(bridge.messageCount == 0);   // success => silence
    fs::remove(p);
}

// ---------------------------------------------------------------------------

static void run_all() {
    makeScratch();
    test_ansi_roundtrip_file();
    test_utf8_roundtrip_file();
    test_utf8_bom_roundtrip_file();
    test_utf16le_roundtrip_file();
    test_utf16be_roundtrip_file();
    test_gb18030_roundtrip_file();
    test_missing_file();
    test_encoding_change_resaves();
    test_can_encode();
    test_over_2gb_refused_with_prompt();
    test_under_2gb_not_refused();
    test_save_to_readonly_target_fails_loudly();
    test_save_writable_target_no_dialog();
}

int main() {
    run_all();
    int fails = test::failCount();
    std::fprintf(stderr, "%d checks, %d failures\n",
                 test::checkCount(), fails);
    return fails ? 1 : 0;
}
