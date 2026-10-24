// test_mainwindow_smoke.cpp - assembly-level smoke test for the REAL main window.
//
// Why this exists
// ---------------
// Until 2026-10-23 the main window was not compiled into a single test: all
// eleven ui/MainWindow_*.cpp files, ui/MenuTable.cpp and ui/SidePanel.cpp had
// zero automated coverage. The other UI smoke test covers the dialogs only, so
// "menu / tabs / side panel / preview split are wired correctly" rested
// entirely on manual clicking.
//
// This target links the SAME sources Pecia.exe links (everything except
// main.cpp), so what it constructs is the product, not a lookalike: the custom
// frame, the native subclass chain, the shadow, the tab bar, the side panel,
// the preview pane, the status bar, and the menu table.
//
// What it proves: the window assembles, shows, lays out, survives every
// show/hide style toggle, can clone the view context menu, and can create and
// close a tab - without faulting. An access violation aborts the process, so
// reaching the end is itself the assertion.
//
// What it does NOT prove: pixels. Nothing here compares rendered output; the
// layout/paint contracts live in the per-module gates (vscroll_geom,
// vscroll_reach, export_e2e, match_highlight).

#include "test_assert.h"

#include "core/Config.h"
#include "core/I18n.h"
#include "core/Theme.h"
#include "ui/Layout.h"
#include "ui/MainWindow.h"
#include "ui/SidePanel.h"
#include "ui/SmokeTest.h"
#include "mdview/container.h"
#include "mdview/preview_panel.h"

#include <FL/Fl.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_Preferences.H>
#include <FL/Fl_RGB_Image.H>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace fs = std::filesystem;

// MainWindow creates its Config through the production default constructor,
// which resolves to <exe dir>/settings.ini - here that is build/settings.ini,
// the same file a developer's Pecia.exe uses. Constructing the window writes
// theme.name into it, so without this guard running the test would quietly
// rewrite the local build's theme choice. Snapshot the two config files and
// put them back on scope exit (including on a failed CHECK that returns early).
class ScopedConfigBackup {
public:
    ScopedConfigBackup() {
        wchar_t exe[MAX_PATH];
        if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return;
        m_dir = fs::path(exe).parent_path();
        for (const char *n : {"settings.ini", "recent file.ini"}) {
            const fs::path p = m_dir / n;
            std::error_code ec;
            if (!fs::exists(p, ec)) continue;
            std::ifstream in(p, std::ios::binary);
            if (!in) continue;
            std::string data((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
            m_saved.push_back({p, data});
        }
    }
    ~ScopedConfigBackup() {
        std::error_code ec;
        for (const auto &kv : m_saved) {
            std::ofstream out(kv.first, std::ios::binary | std::ios::trunc);
            if (out) out.write(kv.second.data(), (std::streamsize)kv.second.size());
        }
    }

    // Force a key to a value in <exe dir>/settings.ini.
    //
    // The window's behaviour depends on settings.ini next to the exe
    // (MainWindow builds its own Config from it), so a leftover value
    // on a developer's machine silently changes what the test asserts.
    // multi_tab=1 in particular: newFile() then ADDS a tab instead of
    // replacing the current one, so "close the last tab -> the window
    // hides" no longer holds and the test fails for reasons that have
    // nothing to do with the code under test. Pin the value the test
    // needs; the destructor restores the original file.
    void forceSetting(const char *key, const char *value) {
        const fs::path p = m_dir / "settings.ini";
        std::string data;
        {
            std::ifstream in(p, std::ios::binary);
            if (!in) {
                // No settings.ini yet: create a minimal one.
                std::ofstream out(p, std::ios::binary | std::ios::trunc);
                out << key << "=" << value << "\n";
                return;
            }
            data.assign((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
        }
        const std::string k = std::string(key) + "=";
        bool replaced = false;
        std::string out;
        out.reserve(data.size() + 64);
        size_t pos = 0;
        while (pos <= data.size()) {
            size_t eol = data.find('\n', pos);
            if (eol == std::string::npos) eol = data.size();
            const std::string line = data.substr(pos, eol - pos);
            if (line.rfind(k, 0) == 0) {
                out += k + value + "\n";
                replaced = true;
            } else {
                out += line;
                if (eol < data.size()) out += "\n";
            }
            if (eol >= data.size()) break;
            pos = eol + 1;
        }
        if (!replaced) {
            if (!out.empty() && out.back() != '\n') out += "\n";
            out += k + value + "\n";
        }
        std::ofstream o(p, std::ios::binary | std::ios::trunc);
        if (o) o << out;
    }

private:
    fs::path m_dir;
    std::vector<std::pair<fs::path, std::string>> m_saved;
};

// Pump the event loop briefly so the 0-second startup timers (firstFrameFixCb /
// fixTaskbarCb, which finish the frame + shadow hand-off) actually run. Using
// Fl::wait with a small timeout keeps the test headless-ish: no user input.
static void settle(double seconds = 0.05) { Fl::wait(seconds); }

// ---------------------------------------------------------------------------
// A5 gate: editing a big document must not blank the preview.
//
// Symptom (found 2026-10-23, docs/STRESS_TEST_PLAN.md A5): open a 5 MB
// Markdown file, turn on the preview, scroll the preview to the middle, then
// type ONE character in the editor. The preview panel goes BLANK -- pure
// white, no text, no "Rendering..." hint -- and stays blank for many
// seconds. Scrolling or another edit brings it back.
//
// It is a SILENT failure: no crash, no exception, no error message. The user
// sees an empty white box and cannot tell whether the program froze or the
// document is broken.
//
// What is asserted (the chain's contract, not internals):
//   1. after re-rendering an edited big document the preview still has
//      content and a usable height;
//   2. the scroll position survived the refresh (typing one character must
//      not throw the user back to the top);
//   3. the panel ACTUALLY PAINTS. "has_content() is true" was not enough on
//      its own: the block table can be active while every visible block
//      failed to lay out, and then draw() paints the white background and
//      nothing else -- exactly the blank panel the user saw.
//
// The document only has to exceed the chunk threshold (the virtualized
// path, where the bug lives); its exact size is irrelevant to the failure.
// ---------------------------------------------------------------------------
static void test_preview_edit_refresh()
{
    // Build a document whose HTML comfortably exceeds the chunk threshold
    // (256 KB of HTML -> the virtualized path, where the bug lives). The
    // structure mirrors the real stress sample: headings, paragraphs, a
    // table, a code fence, a list and a long unbreakable token.
    std::string md;
    md.reserve(1400 * 900);
    md += "# Preview edit-refresh sample\r\n\r\n";
    for (int i = 1; i <= 1400; ++i) {
        md += "## Section " + std::to_string(i) + "\r\n\r\n";
        md += "Paragraph with mixed content: **bold**, *italic*, `code`.\r\n\r\n";
        md += "- list item A\r\n- list item B\r\n\r\n";
        md += "> a quote line\r\n\r\n";
        md += "| col A | col B |\r\n|---|---|\r\n| 1 | 2 |\r\n\r\n";
        md += "```cpp\r\nint main() { return 0; }\r\n```\r\n\r\n";
        md += "https://example.com/path/a-very-long-token-without-any-spaces\r\n\r\n";
    }

    // Drive the FULL production path: renderAsync -> worker thread ->
    // pump() -> applyResult(). The blank panel was observed AFTER
    // applyResult, so testing load_and_render() directly would miss it
    // (and would also miss the scroll-position restore that applyResult
    // performs).
    PreviewPanel panel;
    panel.view()->resize(0, 0, 800, 600);   // resize syncs the container viewport

    // Paint the panel into an offscreen surface and COUNT the non-white
    // pixels. A blank panel is all white -- so the count is what matters,
    // not "is there any ink at all" (a scrollbar or a stray border pixel
    // would satisfy that, and the blank panel slipped through a boolean
    // check once already).
    auto paint = [&panel]() -> int {
        Fl_Image_Surface surface(800, 600);
        Fl_Surface_Device::push_current(&surface);
        panel.view()->draw();
        Fl_RGB_Image *img = surface.image();
        Fl_Surface_Device::pop_current();
        int painted = 0;
        if (img && img->array) {
            const unsigned char *buf = img->array;
            const int w = img->w(), h = img->h(), d = img->d();
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const unsigned char *px = buf + (y * w + x) * d;
                    if (px[0] != 0xFF || px[1] != 0xFF || px[2] != 0xFF)
                        ++painted;
                }
            }
        }
        return painted;
    };

    auto waitForRender = [&panel]() {
        for (int i = 0; i < 400; ++i) {
            Fl::wait(0.05);
            if (panel.hasContent() && panel.view()->has_content()) {
                // Give the layout a moment to settle before checking.
                Fl::wait(0.2);
                return true;
            }
        }
        return false;
    };

    panel.renderAsync(md, "", "");
    CHECK(waitForRender());
    CHECK(panel.view()->has_content());
    CHECK(panel.view()->block_mode());   // must be on the virtualized path

    // Paint once BEFORE the edit. This is what the user does (they look at
    // the preview), and it is also what makes the bug reproducible: the
    // draw instantiates the blocks near the viewport, which MEASURES their
    // real heights and calibrates the px-per-KB density factor. Without
    // that calibration the block heights are estimates, and the estimate
    // can be off by 2-3x -- which is exactly what made the viewport fall
    // into a "hole" between blocks after the edit re-split the document.
    const int paintedBefore = paint();
    CHECK(paintedBefore > 1000);

    // Scroll to the middle, like a user reading the document.
    const int height1 = panel.view()->content_height();
    CHECK(height1 > 0);
    panel.view()->scroll_to(height1 / 2);
    const int scrollBefore = panel.view()->scroll_y();
    CHECK(scrollBefore > 0);
    paint();   // paint at the new position: more blocks get measured

    // The edit: one character changes the whole document, so the preview
    // re-renders from scratch (clear_images + clear_text_width_cache +
    // split + re-layout of the visible blocks).
    std::string md2 = md;
    const size_t at = md2.find("Paragraph with mixed content");
    if (at != std::string::npos) md2.insert(at, "K");

    panel.renderAsync(md2, "", "");
    // Wait for the SECOND render: the panel still reports content from the
    // first one, so we must give the worker time to finish and the pump
    // time to apply it.
    for (int i = 0; i < 400; ++i) {
        Fl::wait(0.05);
        if (panel.view()->current_html().find("KParagraph") != std::string::npos) break;
    }
    Fl::wait(0.3);

    CHECK(panel.view()->has_content());
    CHECK(panel.view()->content_height() > 0);
    // The scroll position must survive the refresh: typing one character
    // must not throw the user back to the top of a long document.
    CHECK(panel.view()->scroll_y() > 0);

    // And the panel must still PAINT. This is the actual bug: after the
    // edit the block table was re-split with estimated heights, the
    // viewport ended up inside a "hole" between two blocks, and the panel
    // painted the white background with nothing on it.
    const int painted = paint();
    std::fprintf(stderr,
                 "preview edit-refresh: painted=%d (was %d) scroll=%d (was %d) height=%d\n",
                 painted, paintedBefore, panel.view()->scroll_y(), scrollBefore,
                 panel.view()->content_height());
    // The panel must still show its content.
    //
    // The threshold is RELATIVE, not absolute: adding one character must
    // not shrink the visible content. In the broken build the viewport
    // ended up inside a "hole" between two blocks and the panel painted
    // 6000 non-white pixels instead of 71538 -- a 12x loss, but not zero,
    // so a "is it blank?" check would have missed it. The quarter-of-before
    // bound leaves room for the scroll position being restored slightly
    // differently while still catching the real regression.
    CHECK(painted > paintedBefore / 4);
}

// ---------------------------------------------------------------------------
// D2 regression: tab switching must not leak GDI objects. NOTE: the
// originally reported symptom ("preview toggle leaks") turned out to be
// misattributed - those early measurements were polluted by clicks landing
// on the tab close buttons. The PreviewPanel create/render/destroy
// lifecycle itself measures clean (delta 0 over 40 cycles, checked by
// test_preview_toggle_gdi below); the real leak is in the tab-switch path.
// ---------------------------------------------------------------------------

static int gdiObjectCount() {
    return (int)GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
}


// Wait until the panel reports rendered content (same protocol as the
// edit-refresh test above).
static bool waitPreviewContent(PreviewPanel &p) {
    for (int i = 0; i < 600; ++i) {
        Fl::wait(0.03);
        if (p.hasContent()) {
            Fl::wait(0.15);
            return true;
        }
    }
    return false;
}

static void test_preview_toggle_gdi() {
    // ~220 sections: comfortably past the 256 KB chunk threshold, so the
    // render exercises the block path (the path real users hit).
    std::string md;
    md.reserve(220 * 900);
    md += "# GDI toggle sample\r\n\r\n";
    for (int i = 1; i <= 220; ++i) {
        md += "## Section " + std::to_string(i) + "\r\n\r\n";
        md += "Paragraph with **bold**, *italic* and `code`.\r\n\r\n";
        md += "- item A\r\n- item B\r\n\r\n";
    }
    // The preview needs a writable asset dir (md_to_html writes index.html
    // and copies local images next to it). Use a scratch dir under %TEMP%,
    // NOT the exe directory: this binary lives in build/, i.e. the OUTPUT
    // dir, so writing there dropped a stray index.html beside Pecia.exe -
    // which then ends up in the release package.
    std::error_code ec;
    const std::string buildDir =
        (fs::temp_directory_path() / "pecia_smoke_preview").string();
    fs::create_directories(buildDir, ec);

    // Warm-up: the first cycle populates one-time process caches (fonts,
    // theme), which must not count against the toggle loop.
    {
        PreviewPanel p;
        p.renderAsync(md, buildDir, "");
        CHECK(waitPreviewContent(p));
    }
    Fl::wait(0.3);
    const int base = gdiObjectCount();

    const int kRounds = 40;
    for (int r = 0; r < kRounds; ++r) {
        PreviewPanel p;
        p.renderAsync(md, buildDir, "");
        CHECK(waitPreviewContent(p));
    }
    Fl::wait(0.5);
    const int after = gdiObjectCount();
    std::fprintf(stderr, "preview toggle GDI: base=%d after=%d delta=%d over %d toggles\n",
                 base, after, after - base, kRounds);
    // The leak was linear (+5 per 10 toggles => +20 over 40). A healthy
    // build stays flat. The bound is loose enough for driver-level jitter
    // but far below the leak rate.
    CHECK(after - base <= 8);
}

int main() {
    // Same portability rule as the product entry point: never let FLTK core
    // touch fltk.org prefs outside the exe. Must precede any FLTK call.
    Fl_Preferences::file_access(Fl_Preferences::APP_OK);
    Fl::visual(FL_DOUBLE | FL_RGB);

    // Startup sequence MUST mirror main.cpp (see 开发指南 十节 §7). A window
    // built with a different global environment is not the product's window.
    const int fontCount = Fl::set_fonts("*");
    theme_registerBorderBox();
    CHECK(fontCount > 100);   // CJK faces enumerated, not ANSI-only

    Fl::scheme("gtk+");
    ui::g_smokeMode = true;   // no modal loops / detached service threads

    // Snapshot build/settings.ini before MainWindow writes theme.name into it.
    ScopedConfigBackup cfgBackup;
    // Pin the tab mode this test asserts on: the "close the last tab hides
    // the window" rule only holds in single-tab mode (see forceSetting).
    cfgBackup.forceSetting("multi_tab", "0");

    Config cfg;
    initLayoutHeights(cfg);
    I18n::load("en");

    // Seed 10 recent files BEFORE the window is constructed so its Config
    // loads them; the Recent Files submenu labels are checked below.
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const fs::path exeDir = fs::path(exePath).parent_path();
    const fs::path recentPath = exeDir / "recent file.ini";
    std::error_code recentEc;
    const bool hadRecentFile = fs::exists(recentPath, recentEc);
    {
        std::ofstream out(recentPath, std::ios::binary | std::ios::trunc);
        for (int i = 1; i <= 10; ++i)
            out << "File" << i << "=C:\\work\\doc" << i << ".md\n";
    }

    // ── Construct + show: frame, title bar, menu bar, tabs, editor, status ──
    MainWindow win(1000, 700, "smoke-main");
    CHECK(win.w() > 0 && win.h() > 0);
    CHECK(win.children() > 0);

    win.show();
    CHECK(win.visible());
    settle();

    // ── Recent Files labels: every row carries a two-digit index ───────────
    // Regression: the 10th row used to drop its number entirely because the
    // code only formatted `%d` for i < 9. All rows now use "%02d" so the
    // 10th entry keeps its number and the list stays aligned.
    {
        Fl_Menu_Bar *bar = nullptr;
        for (int i = 0; i < win.children(); ++i) {
            auto *mb = dynamic_cast<Fl_Menu_Bar *>(win.child(i));
            if (mb && mb->menu() && mb->find_item("File/Recent Files")) {
                bar = mb;
                break;
            }
        }
        CHECK(bar != nullptr);
        const Fl_Menu_Item *ri =
            bar ? bar->find_item("File/Recent Files") : nullptr;
        CHECK(ri != nullptr);
        const Fl_Menu_Item *sub =
            ri ? static_cast<const Fl_Menu_Item *>(ri->user_data()) : nullptr;
        CHECK(sub != nullptr);
        int rows = 0;
        if (sub) {
            for (const Fl_Menu_Item *it = sub; it->text; ++it) ++rows;
        }
        CHECK_EQ(rows, 10);
        for (int i = 0; sub && i < rows && i < 10; ++i) {
            char want[4];
            std::snprintf(want, sizeof(want), "%02d", i + 1);
            CHECK(sub[i].text && std::strncmp(sub[i].text, want, 2) == 0);
        }
    }
    if (!hadRecentFile) fs::remove(recentPath, recentEc);

    // ── Show/hide toggles: every one re-lays out the content area ──────────
    // These are the exact callbacks the View menu fires. A regression in the
    // contentLeft()约定 (children parked at x=0 under the side panel) does not
    // crash, so this only proves the re-layout path runs; the geometry contract
    // is documented in 开发指南 §架构速览.
    MainWindow::cbToggleSidePanel(&win, &win);
    settle();
    CHECK(win.visible());

    MainWindow::cbToggleStatusbar(&win, &win);
    MainWindow::cbToggleMenuBar(&win, &win);
    MainWindow::cbToggleScriptBar(&win, &win);
    MainWindow::cbToggleLineNumbers(&win, &win);
    MainWindow::cbToggleWrap(&win, &win);
    MainWindow::cbToggleHighlightLine(&win, &win);
    MainWindow::cbToggleShowWhitespace(&win, &win);
    settle();
    CHECK(win.w() > 0 && win.h() > 0);

    // Restore the bars we turned off so the rest of the run exercises the
    // default arrangement.
    MainWindow::cbToggleMenuBar(&win, &win);
    MainWindow::cbToggleStatusbar(&win, &win);
    settle();

    // ── View context menu clone (MainWindow::buildViewContextMenu) ─────────
    // The editor's right-click "View" submenu is generated from the live View
    // menu. A broken clone shows up as an empty submenu, not a crash.
    {
        Fl_Menu_Item items[96];
        std::memset(items, 0, sizeof(items));
        const int n = win.buildViewContextMenu(items, 96);
        CHECK(n > 0);
        CHECK(n < 96);
        // Every produced item must carry a label or be a separator/terminator.
        bool anyLabel = false;
        for (int i = 0; i < n; ++i)
            if (items[i].text && (items[i].flags & FL_SUBMENU) == 0)
                anyLabel = true;
        CHECK(anyLabel);
    }

    // ── Tab lifecycle + the "last tab closes the window" rule ─────────────
    // MainWindow_files.cpp:107 - closing the last remaining tab hides the
    // window instead of leaving an empty editor. That is what makes Ctrl+W /
    // the tab's ✕ quit the app on the last document; if it ever regresses the
    // user gets a dead window with no menu-visible way out. Pinned here.
    win.newFile();
    settle();
    CHECK(win.visible());

    win.closeCurrentTab();
    settle();
    CHECK(!win.visible());          // last tab closed -> window hidden

    // Bring it back for the remaining checks (main.cpp would exit instead).
    win.show();
    settle();
    CHECK(win.visible());

    // ── Status-bar refresh path (cursor / line-col / encoding / size) ──────
    win.tick();
    CHECK(win.visible());

    // ── D2: tab switching must not leak GDI objects ──────────────────────
    // KNOWN ISSUE (2026-10-23, not yet root-caused): each tab switch leaks
    // ~0.5 GDI objects in the FLTK tab-strip drawing path. Measured:
    // 30 switches +13~15, 300 switches +150 (perfectly linear). Ruled out
    // by experiment: the PreviewPanel create/render/destroy lifecycle
    // (delta 0 over 40 cycles), the editor draw path (hiding the editors
    // still leaks), the font descriptor cache (node count flat), the win32
    // clip/region stack (push/pop balanced, no overflow warning), USER
    // objects (delta 0), the tab close-button symbol (still leaks with
    // FL_WHEN_CLOSED cleared) and focus/cursor changes (delta 0).
    // This gate holds the line at the measured level plus margin so that a
    // WORSE regression is caught; see docs/STRESS_TEST_PLAN.md (D2 row).
    {
        cfgBackup.forceSetting("multi_tab", "1");
        MainWindow win2(1000, 700, "smoke-d2");
        win2.show();
        settle();
        for (int i = 0; i < 9; ++i) { win2.newFile(); settle(); }
        CHECK(win2.visible());

        Fl::wait(0.3);
        const int base = gdiObjectCount();
        for (int r = 0; r < 30; ++r) {
            win2.switchToTab(r % 10);
            settle();
        }
        Fl::wait(0.3);
        const int after = gdiObjectCount();
        std::fprintf(stderr, "tab switch GDI: base=%d after=%d delta=%d over 30 switches\n", base, after, after - base);
        CHECK(after - base <= 40);   // measured ~15; margin for jitter only
        win2.hide();
    }

    // ── Side-panel outline: draw cost must follow VISIBLE rows, not total ──
    // 5 MB 文档的大纲有 5498 条标题。绘制一帧若把所有行都过一遍（每行都要
    // 量文本宽度，本机一次 fl_width 约 65us），一帧就是 0.6~1.6 秒 ——
    // 滚动和点大纲都会卡死（用户 2026-10-23 实测上报）。视口裁剪修好后，
    // 5000 行与 200 行的重绘成本应当同阶，这里就用这个比值当门禁：
    // 绝对毫秒数随机器浮动，比值不会。
    {
        Fl_Window host(260, 600);
        SidePanel panel(0, 0, 260, 600);
        host.add(&panel);
        host.show();
        settle();

        auto buildOutline = [](int n, std::vector<std::string> &labels,
                               std::vector<int> &levels, std::vector<std::string> &ids) {
            labels.clear(); levels.clear(); ids.clear();
            for (int i = 0; i < n; ++i) {
                labels.push_back("第 " + std::to_string(i + 1) + " 节 混排与排版压力测试标题");
                levels.push_back((i % 12 == 0) ? 1 : 2);
                ids.push_back("toc-" + std::to_string(i));
            }
        };
        auto timeRepaint = [&](int n) {
            std::vector<std::string> labels, ids;
            std::vector<int> levels;
            buildOutline(n, labels, levels, ids);
            panel.setTab(SidePanel::Outline);
            panel.setOutline(labels, levels, ids, std::vector<int>{});
            host.redraw();
            Fl::check();                    // 第一帧含截断缓存预热
            const auto t0 = std::chrono::steady_clock::now();
            host.redraw();
            Fl::flush();
            return std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - t0).count();
        };

        // 注意：不要用 small / near / far 这类名字——<rpcndr.h> 把 small 定义成
        // char（windows.h 的老宏），`const double small` 会变成 `double char`。
        const double msSmall = timeRepaint(200);
        const double msBig   = timeRepaint(5000);
        std::fprintf(stderr, "outline repaint: 200 rows %.2f ms, 5000 rows %.2f ms\n",
                     msSmall, msBig);
        // 5000 行不该比 200 行贵出一个量级；留 3 倍 + 50ms 的余量吸收抖动。
        CHECK(msBig <= msSmall * 3.0 + 50.0);
        host.hide();
    }

    // ── Hide ───────────────────────────────────────────────────────────────
    win.hide();
    CHECK(!win.visible());

    // ── Preview edit-refresh gate (A5) ────────────────────────────────────
    test_preview_edit_refresh();
    test_preview_toggle_gdi();

    int fails = test::failCount();
    std::fprintf(stderr, "%d checks, %d failures\n", test::checkCount(), fails);
    return fails ? 1 : 0;
}
