// test_paste_media.cpp - Editor::pasteMedia() end-to-end over the REAL
// clipboard (r042e). The feature had zero coverage since 1.1.2 and its
// skip paths were silent, which read as broken; the probe proved the core
// logic was fine, so these tests pin BOTH the working paths and the new
// status-line notices:
//   1. file (CF_HDROP)      -> true, copy lands, link inserted
//   2. bitmap (CF_DIB)      -> true, png lands, link inserted
//   3. plain text           -> false, NO notice (text must win, silently)
//   4. unsaved document     -> false + notice "paste.nosave"
//   5. read-only document   -> false + notice "paste.readonly"
// Fixtures own their Editor and destroy it in ~PmFixture. In smoke/headless
// mode the Editor destructor is safe here: no window/IME hook was ever
// attached (pasteMedia only reads the clipboard and writes files), so the
// teardown reduces to clearing highlight_data() and deleting the style
// buffer. The Document (and the text buffer it owns) is a fixture member and
// outlives the Editor destructor, which runs before member destruction.
#include "editor/Editor.h"
#include "editor/Document.h"
#include "editor/ImageAttach.h"
#include "core/SettingsProvider.h"

#include <FL/Fl.H>
#include <FL/Fl_Text_Buffer.H>

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static int failures = 0;
static int checks = 0;
#define CHECK(cond) do { ++checks; \
    if (!(cond)) { ++failures; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

// ---- settings stub: only the paste-media switch is on ---------------------
class PmSettings : public SettingsProvider {
public:
    bool getPasteMediaPath() const override { return true; }
    int  getTabWidth() const override { return 4; }
    bool getAutoIndent() const override { return false; }
    bool getDetectUrls() const override { return false; }
    void getFont(int &font, int &size) const override { font = FL_COURIER; size = 12; }
    bool getHighlightCurrentLine() const override { return false; }
    bool getShowWhitespace() const override { return false; }
    bool getWrap() const override { return false; }
    bool getMultiTab() const override { return true; }
    int  getLongLineMarker() const override { return 0; }
    bool getAlwaysOnTop() const override { return false; }
    bool getShowStatusbar() const override { return false; }
    bool getTrimTrailingWhitespace() const override { return false; }
    void getLang(char *buf, int len, const char *) const override {
        strncpy_s(buf, len, "en", _TRUNCATE);
    }
    void getWindow(int &, int &, int &, int &, int &) const override {}
};

// ---- clipboard writers (real clipboard, real formats) ---------------------
struct DropFilesRec {           // DROPFILES is behind an IE-level guard here
    DWORD pFiles;
    POINT pt;
    BOOL  fNC;
    BOOL  fWide;
};

static bool setHdrop(const std::wstring &path) {
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    SIZE_T bytes = sizeof(DropFilesRec) + (path.size() + 2) * sizeof(wchar_t);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!g) { CloseClipboard(); return false; }
    auto *df = static_cast<DropFilesRec *>(GlobalLock(g));
    // GlobalAlloc(GMEM_MOVEABLE) does not zero the block: pt/fNC (and any
    // padding) were uninitialized garbage handed to the clipboard consumer.
    ZeroMemory(df, sizeof(DropFilesRec));
    df->pFiles = sizeof(DropFilesRec);
    df->fWide = TRUE;
    wcscpy(reinterpret_cast<wchar_t *>(df + 1), path.c_str());
    GlobalUnlock(g);
    BOOL ok = SetClipboardData(CF_HDROP, g) != nullptr;
    CloseClipboard();
    return ok != FALSE;
}

static bool setDib(int w, int h) {
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    SIZE_T pixels = (SIZE_T)w * h * 4;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + pixels);
    if (!g) { CloseClipboard(); return false; }
    auto *bih = static_cast<BITMAPINFOHEADER *>(GlobalLock(g));
    // Zero the fields we do not set (biXPelsPerMeter/biYPelsPerMeter/
    // biClrUsed/biClrImportant) instead of leaking uninitialized heap bits.
    ZeroMemory(bih, sizeof(BITMAPINFOHEADER));
    bih->biSize = sizeof(BITMAPINFOHEADER);
    bih->biWidth = w;
    bih->biHeight = -h;
    bih->biPlanes = 1;
    bih->biBitCount = 32;
    bih->biCompression = BI_RGB;
    bih->biSizeImage = (DWORD)pixels;
    auto *bits = reinterpret_cast<unsigned char *>(bih + 1);
    for (SIZE_T i = 0; i < pixels; i += 4) {
        bits[i] = 0x30; bits[i + 1] = 0x80; bits[i + 2] = 0xE0; bits[i + 3] = 0xFF;
    }
    GlobalUnlock(g);
    BOOL ok = SetClipboardData(CF_DIB, g) != nullptr;
    CloseClipboard();
    return ok != FALSE;
}

static bool setText(const wchar_t *text) {
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    SIZE_T bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!g) { CloseClipboard(); return false; }
    memcpy(GlobalLock(g), text, bytes);
    GlobalUnlock(g);
    BOOL ok = SetClipboardData(CF_UNICODETEXT, g) != nullptr;
    CloseClipboard();
    return ok != FALSE;
}

// ---- fixture: one editor wired to one saved document ----------------------
struct PmFixture {
    PmSettings settings;
    Editor *ed;
    Document doc;
    std::string dir;

    explicit PmFixture(bool saved) {
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        dir = std::string(tmp) + "pm_test";
        std::error_code ec;
        fs::create_directories(fs::path(image_attach::widen(dir)), ec);
        ed = new Editor(0, 0, 400, 200, &settings);
        if (saved) {
            std::string p = dir + "\\doc.md";
            { std::ofstream f(p.c_str(), std::ios::binary); f << "# t\n"; }
            doc.loadFile(p.c_str());
        }
        ed->setDoc(&doc);
        ed->buffer(doc.buffer());
    }

    // Safe headless destruction: run before members are torn down so `doc`
    // (and the buffer the Editor points at) is still alive during teardown.
    ~PmFixture() { delete ed; }

    void clearClipboard() {
        if (OpenClipboard(nullptr)) { EmptyClipboard(); CloseClipboard(); }
    }
};

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    Fl::lock();
    PmFixture base(true);
    std::string srcFile = base.dir + "\\src_file.txt";
    { std::ofstream f(srcFile.c_str(), std::ios::binary); f << "x"; }

    // ---- 1. file ----
    base.clearClipboard();
    CHECK(setHdrop(image_attach::widen(srcFile)));
    CHECK(base.ed->pasteMedia());
    CHECK(fs::exists(fs::path(image_attach::widen(base.dir + "\\doc\\src_file.txt"))));
    CHECK(strstr(base.doc.buffer()->text(), "doc/src_file.txt") != nullptr);

    // ---- 2. bitmap ----
    base.clearClipboard();
    CHECK(setDib(4, 3));
    CHECK(base.ed->pasteMedia());
    bool png = false;
    for (auto &e : fs::directory_iterator(fs::path(image_attach::widen(base.dir + "\\doc"))))
        if (e.path().extension() == L".png") png = true;
    CHECK(png);

    // ---- 3. text wins, silently ----
    base.clearClipboard();
    CHECK(setText(L"words"));
    CHECK(!base.ed->pasteMedia());

    // ---- 4. unsaved document -> false + paste.nosave ----
    {
        PmFixture unsaved(false);
        unsaved.clearClipboard();
        CHECK(setHdrop(image_attach::widen(srcFile)));
        std::string got;
        unsaved.ed->setPasteNotice([&got](const char *key) { got = key; });
        CHECK(!unsaved.ed->pasteMedia());
        CHECK(got == "paste.nosave");
    }

    // ---- 5. locked document -> false + paste.readonly ----
    {
        PmFixture ro(true);
        ro.doc.setEditLocked(true);   // isEditable() = !readOnly && !editLocked
        ro.clearClipboard();
        CHECK(setHdrop(image_attach::widen(srcFile)));
        std::string got;
        ro.ed->setPasteNotice([&got](const char *key) { got = key; });
        CHECK(!ro.ed->pasteMedia());
        CHECK(got == "paste.readonly");
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
