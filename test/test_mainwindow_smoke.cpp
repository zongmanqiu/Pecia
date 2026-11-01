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
#include "editor/Document.h"
#include "editor/Editor.h"
#include "ui/SidePanel.h"
#include "ui/SmokeTest.h"
#include "mdview/container.h"
#include "mdview/preview_panel.h"

#include <FL/Fl.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Output.H>
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

// ---------------------------------------------------------------------------
// 仅预览没有编辑器可用，键盘翻页（PageUp/PageDown/Home/End）是预览唯一的
// 滚动方式。直接调用 ViewWidget::handle()：Fl::e_keysym 是 FLTK 公开的按键
// 状态，设置后走的是与真实按键同一条分发路径。
// ---------------------------------------------------------------------------
static void test_preview_keyboard_scroll()
{
    std::string md;
    for (int i = 1; i <= 120; ++i) {
        md += "## Section " + std::to_string(i) + "\n\n";
        md += "Paragraph used only to make the preview tall enough to scroll.\n\n";
    }
    PreviewPanel panel;
    panel.view()->resize(0, 0, 800, 600);
    panel.renderAsync(md, "", "");
    CHECK(waitPreviewContent(panel));
    CHECK(panel.view()->has_content());

    const int total = panel.view()->content_height();
    CHECK(total > panel.view()->h());
    CHECK(panel.view()->scroll_y() == 0);

    Fl::e_keysym = FL_Page_Down;
    CHECK(panel.view()->handle(FL_KEYBOARD) == 1);
    const int afterPage = panel.view()->scroll_y();
    CHECK(afterPage > 0);

    Fl::e_keysym = FL_End;
    CHECK(panel.view()->handle(FL_KEYBOARD) == 1);
    CHECK(panel.view()->scroll_y() > afterPage);

    Fl::e_keysym = FL_Home;
    CHECK(panel.view()->handle(FL_KEYBOARD) == 1);
    CHECK(panel.view()->scroll_y() == 0);

    // 已经在顶部，PageUp 必须停在 0（scroll_to 负责夹紧）
    Fl::e_keysym = FL_Page_Up;
    CHECK(panel.view()->handle(FL_KEYBOARD) == 1);
    CHECK(panel.view()->scroll_y() == 0);
    Fl::e_keysym = 0;

    std::fprintf(stderr, "preview keyboard scroll: total=%d afterPage=%d\n",
                 total, afterPage);
}

// ---------------------------------------------------------------------------
// 右键分派：预览只读，右键菜单由 MainWindow 组装，ViewWidget 只负责在
// FL_PUSH 时把窗口内坐标交出去（并不进入拖拽/滚动条逻辑）。这里用自定义
// 回调断言"右键 → 回调 + 坐标原样 + 返回 1"，因为真实的 popup() 会阻塞。
// ---------------------------------------------------------------------------
static void test_preview_right_click_dispatch()
{
    std::string md;
    for (int i = 1; i <= 40; ++i)
        md += "## Section " + std::to_string(i) + "\n\nText.\n\n";

    PreviewPanel panel;
    panel.view()->resize(0, 0, 800, 600);
    panel.renderAsync(md, "", "");
    CHECK(waitPreviewContent(panel));

    int gotX = -1, gotY = -1, calls = 0;
    panel.view()->set_context_callback([&](int x, int y) {
        gotX = x;
        gotY = y;
        ++calls;
    });

    const int scrollBefore = panel.view()->scroll_y();
    Fl::e_x = 120;
    Fl::e_y = 240;
    Fl::e_keysym = FL_Button + FL_RIGHT_MOUSE;   // event_button() == 右键
    CHECK(panel.view()->handle(FL_PUSH) == 1);
    Fl::e_keysym = 0;

    CHECK(calls == 1);
    CHECK(gotX == 120);
    CHECK(gotY == 240);
    // 右键不能开始拖拽/滚动：滚动位置必须原地不动
    CHECK(panel.view()->scroll_y() == scrollBefore);
}

// ---------------------------------------------------------------------------
// 预览链接点击（ViewWidget::dispatch_link_click + MyContainer::on_anchor_click）：
// 用真实 litehtml 布局做端到端命中测试 —— 从 DOM 里取 <a> 的 placement 算点击
// 坐标，合成 PUSH/RELEASE，断言 URL 原样交给上层、纯 #锚点不外传。
// 坐标换算（窗口坐标 → 文档坐标 → 分块内坐标）出错时这里会直接失败。
// ---------------------------------------------------------------------------
static litehtml::element::ptr findAnchorElement(const litehtml::element::ptr &el)
{
    if (!el) return nullptr;
    const char *href = el->get_attr("href");
    if (href && *href) return el;
    for (auto &child : el->children()) {
        litehtml::element::ptr found = findAnchorElement(child);
        if (found) return found;
    }
    return nullptr;
}

static void test_preview_link_click_dispatch()
{
    MyContainer holder;
    ViewWidget view(0, 0, 640, 480, &holder);
    view.load_and_render(
        "<html><body><p><a href=\"https://example.com/doc\">example link</a> "
        "<a href=\"#nowhere\">nav</a></p></body></html>");

    CHECK(holder.document() != nullptr);
    litehtml::element::ptr anchor = findAnchorElement(holder.document()->root());
    CHECK(anchor != nullptr);

    std::vector<std::string> opened;
    holder.set_link_callback([&](const std::string &u) { opened.push_back(u); });

    if (anchor) {
        const litehtml::position pos = anchor->get_placement();
        const int px = (int)pos.x + (int)pos.width / 2;
        const int py = (int)pos.y + (int)pos.height / 2;

        Fl::e_x = px;
        Fl::e_y = py;
        Fl::e_keysym = FL_Button + FL_LEFT_MOUSE;   // event_button() == 左键
        CHECK(view.handle(FL_PUSH) == 1);
        CHECK(view.handle(FL_RELEASE) == 1);
        Fl::e_keysym = 0;

        CHECK(opened.size() == 1);
        if (!opened.empty()) CHECK(opened[0] == "https://example.com/doc");
    }

    // 同一个链接"按下后拖动超过 3px"是滚动，不是点击：不能打开。
    if (anchor) {
        const litehtml::position pos = anchor->get_placement();
        Fl::e_x = (int)pos.x + 2;
        Fl::e_y = (int)pos.y + (int)pos.height / 2;
        Fl::e_keysym = FL_Button + FL_LEFT_MOUSE;
        CHECK(view.handle(FL_PUSH) == 1);
        Fl::e_x += 20;   // 拖动
        CHECK(view.handle(FL_DRAG) == 1);
        CHECK(view.handle(FL_RELEASE) == 1);
        Fl::e_keysym = 0;
        CHECK(opened.size() == 1);   // 没有新增
    }
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

// ---------------------------------------------------------------------------
// 仅预览下的"菜单 / 状态栏一致性"（2026-11-01 用户定）：
//   · 编辑区被隐藏 = 那些动作点不动 → 菜单栏「编辑」除"复制/全选/查找三项"
//     外全部置灰，判定与预览右键菜单**同一条**（isReadOnlyAllowed）；
//   · 状态栏的 line:col 只是上一个光标的陈旧值 → 整段隐藏。
// 用一个**独立窗口**跑这条契约：往主窗口的文档里写内容会让它变脏，后面的
// "关闭标签"就会弹保存确认框（模态），而这里没人会保存它。
// ---------------------------------------------------------------------------
static void test_preview_only_menu_and_status()
{
    MainWindow w(800, 600, "smoke-preview-only");
    w.show();
    settle();

    CHECK(w.insertAiText("# Preview only sample\n\nBody text.\n"));
    settle();
    MainWindow::cbTogglePreviewOnly(&w, &w);   // 没预览 → 以仅预览打开
    settle();

    auto sameLabel = [](const Fl_Menu_Item &it, const char *i18nKey,
                        const char *english) {
        const char *t = I18n::get(i18nKey);
        return it.text && ((t && *t && std::strcmp(it.text, t) == 0) ||
                           std::strcmp(it.text, english) == 0);
    };

    // 按 i18n 文案找「编辑」头（窗口会按持久化语言重写菜单标签，英文路径
    // "Edit" 只在 en 下成立）。
    Fl_Menu_Item *edit = nullptr;
    if (Fl_Menu_Bar *bar = w.menuBar()) {
        for (const Fl_Menu_Item *it = bar->menu(); it && it->text; it = it->next(1)) {
            if (sameLabel(*it, "menu.edit", "Edit") && (it->flags & FL_SUBMENU)) {
                edit = const_cast<Fl_Menu_Item *>(it);
                break;
            }
        }
    }
    CHECK(edit != nullptr);

    int rows = 0, inactive = 0, active = 0, mismatch = 0;
    if (edit) {
        for (const Fl_Menu_Item *it = edit + 1; it->text; ++it) {
            ++rows;
            const bool allowed =
                sameLabel(*it, "menu.edit.copy", "Copy") ||
                sameLabel(*it, "menu.edit.selectall", "Select All") ||
                sameLabel(*it, "menu.edit.find", "Find...") ||
                sameLabel(*it, "menu.edit.findnext", "Find Next") ||
                sameLabel(*it, "menu.edit.findprev", "Find Previous");
            const bool gray = (it->flags & FL_MENU_INACTIVE) != 0;
            if (gray) ++inactive; else ++active;
            if (gray != !allowed) ++mismatch;   // 该灰的灰、该亮的亮
        }
    }
    CHECK(rows == 15);       // Undo..Send to AI（菜单栏是完整一组，不是右键白名单）
    CHECK(inactive == 10);   // 只留 Copy / Select All / Find / Find Next / Find Prev
    CHECK(active == 5);
    CHECK(mismatch == 0);

    // 菜单栏整组置灰不能"传染"给右键菜单：预览右键仍要保留复制/全选可用
    // （buildContextMenu 从菜单项复制 flags，必须先清掉再按 readOnly 重算）。
    {
        Fl_Menu_Item pm[128];
        std::memset(pm, 0, sizeof(pm));
        CHECK(w.buildContextMenu(pm, 128, true) > 0);
        int gray = 0, bright = 0;
        for (const Fl_Menu_Item *it = pm; it->text; it = it->next(1)) {
            if (it->flags & (FL_SUBMENU | FL_SUBMENU_POINTER)) continue;
            if (it->flags & FL_MENU_INACTIVE) ++gray; else ++bright;
        }
        CHECK(gray == 7);
        CHECK(bright == 2);
    }

    // 状态栏：编码与大小里都没有冒号，所以"整串不含 ':'"就是没有 line:col
    Fl_Output *status = nullptr;
    for (int i = 0; i < w.children(); ++i) {
        auto *o = dynamic_cast<Fl_Output *>(w.child(i));
        if (o && std::strstr(o->value(), "Size")) { status = o; break; }
    }
    CHECK(status != nullptr);
    w.tick();
    if (status) {
        CHECK(std::strchr(status->value(), ':') == nullptr);
        CHECK(std::strstr(status->value(), "Size") != nullptr);
    }

    // 退出仅预览：菜单栏全部恢复可用，状态栏回到 line:col
    MainWindow::cbTogglePreviewOnly(&w, &w);
    settle(0.2);   // 让预览面板的延迟销毁（0.1s）落地
    int stillGray = 0;
    if (edit)
        for (const Fl_Menu_Item *it = edit + 1; it->text; ++it)
            if (it->flags & FL_MENU_INACTIVE) ++stillGray;
    CHECK(stillGray == 0);
    w.tick();
    if (status) CHECK(std::strchr(status->value(), ':') != nullptr);

    w.hide();
    settle();
}

// ---------------------------------------------------------------------------
// 仅预览 = 编辑能力锁（2026-11-01 用户定）：
//   · 文档进入 isEditLocked，一切写 buffer 的入口失效 —— 包括不经过光标的
//     键盘、Lua 脚本、AI/管道插入、整篇 APPLY；
//   · 查找保留、替换置灰（查找是刚需，替换不该可用）；
//   · 保存/另存为仍针对原文（只看 isReadOnly，不受编辑锁影响）；
//   · 退出仅预览后同一入口恢复可用。
// 用独立窗口：写内容会让文档变脏，避免污染主窗口的后续断言。
// ---------------------------------------------------------------------------
// 从控件树里定位编辑区（不碰 MainWindow::Tab 私有结构）。
static Editor *findEditor(Fl_Group *g)
{
    for (int i = 0; i < g->children(); ++i) {
        Fl_Widget *c = g->child(i);
        if (Editor *ed = dynamic_cast<Editor *>(c)) return ed;
        if (Fl_Group *sub = dynamic_cast<Fl_Group *>(c))
            if (Editor *ed = findEditor(sub)) return ed;
    }
    return nullptr;
}

static void test_preview_only_edit_lock()
{
    MainWindow w(800, 600, "smoke-preview-only-lock");
    w.show();
    settle();

    CHECK(w.insertAiText("# Locked sample\n\nBody text.\n"));
    settle();
    MainWindow::cbTogglePreviewOnly(&w, &w);   // 没预览 → 以仅预览打开
    settle();

    // Tab 是 MainWindow 的私有结构，测试不留后门：从控件树里找编辑器，
    // 文档状态走 Editor::doc()（公开）。handle() 借 Fl_Text_Editor 的公开
    // 入口调用（Editor 的同名覆盖是 protected，但虚派发仍落到它）。
    Editor *ed = findEditor(&w);
    CHECK(ed != nullptr && ed->doc() != nullptr);
    if (!ed || !ed->doc()) { w.hide(); settle(); return; }
    Document *doc = ed->doc();

    CHECK(doc->isEditLocked());
    CHECK(!doc->isEditable());
    const int len = doc->buffer()->length();

    // 三条不走光标的写入路径：能力锁必须在数据面就失效。
    std::string err, out;
    CHECK(!w.insertAiText("must not land\n", &err));
    CHECK(!w.applyDocumentSnapshot("# replaced\n", &err));
    CHECK(!w.executeLuaScript("# no-op\n", &out, &err));
    CHECK(doc->buffer()->length() == len);

    // 键盘：即使把按键直接投给（隐藏的）编辑器，也不能落字。
    Fl::e_keysym = 'x';
    Fl::e_text = const_cast<char *>("x");
    Fl::e_length = 1;
    static_cast<Fl_Text_Editor *>(ed)->handle(FL_KEYBOARD);
    CHECK(doc->buffer()->length() == len);
    Fl::e_keysym = 0;
    Fl::e_text = nullptr;
    Fl::e_length = 0;

    // 退出仅预览：锁解除，同一入口恢复可用（保存路径只看 isReadOnly）。
    MainWindow::cbTogglePreviewOnly(&w, &w);
    settle(0.2);
    CHECK(!doc->isEditLocked());
    CHECK(doc->isEditable());
    CHECK(w.insertAiText("now it lands\n", &err));

    w.hide();
    settle();
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

    // ── Right-click menu: editor == preview, only graying differs ──────────
    // 用户契约（2026-11-01）：编辑区与预览区右键菜单**完全等同** —— 同一份
    // buildContextMenu()：Edit 白名单 + View ▶ + Markdown ▶（后两者整份复刻
    // 菜单栏，以后加菜单项自动跟上）。预览只是把会改原文的动作置灰，保留
    // 复制 / 全选。这里逐槽位断言两个数组布局一致，再断言置灰范围。
    {
        Fl_Menu_Item editorMenu[128];
        Fl_Menu_Item previewMenu[128];
        std::memset(editorMenu, 0, sizeof(editorMenu));
        std::memset(previewMenu, 0, sizeof(previewMenu));
        const int ne = win.buildContextMenu(editorMenu, 128, false);
        const int np = win.buildContextMenu(previewMenu, 128, true);
        CHECK(ne > 0);
        CHECK(np == ne);   // 同一份内容：槽位数必须相同

        // 逐槽位比对文案与子菜单标志（置灰位是唯一允许的差异）
        bool layoutSame = (np == ne);
        for (int i = 0; layoutSame && i < ne; ++i) {
            const Fl_Menu_Item &a = editorMenu[i];
            const Fl_Menu_Item &b = previewMenu[i];
            if (!a.text || !b.text) {
                if (a.text != b.text) layoutSame = false;
                continue;
            }
            if (std::strcmp(a.text, b.text) != 0) layoutSame = false;
            if ((a.flags & (FL_SUBMENU | FL_SUBMENU_POINTER)) !=
                (b.flags & (FL_SUBMENU | FL_SUBMENU_POINTER)))
                layoutSame = false;
        }
        CHECK(layoutSame);

        auto sameLabel = [](const Fl_Menu_Item &it, const char *i18nKey,
                            const char *english) {
            const char *t = I18n::get(i18nKey);
            return it.text && ((t && *t && std::strcmp(it.text, t) == 0) ||
                               std::strcmp(it.text, english) == 0);
        };
        // 在同一层里找一项（next(1) 自动跳过子菜单内容与不可见项）。
        auto findInLevel = [&](const Fl_Menu_Item *level, const char *i18nKey,
                               const char *english) -> const Fl_Menu_Item * {
            for (const Fl_Menu_Item *it = level; it->text; it = it->next(1))
                if (sameLabel(*it, i18nKey, english)) return it;
            return nullptr;
        };

        // 预览：Edit 9 条（Undo/Redo/Cut/Copy/Paste/Delete/Select All +
        // Search Selection + Send to AI），只有 Copy / Select All 可用。
        const Fl_Menu_Item *viewHead = nullptr;
        const Fl_Menu_Item *mdHead = nullptr;
        int editRows = 0, inactiveEdit = 0, activeEdit = 0, grayMismatch = 0;
        for (const Fl_Menu_Item *it = previewMenu; it->text; it = it->next(1)) {
            if (it->flags & (FL_SUBMENU | FL_SUBMENU_POINTER)) {
                if (sameLabel(*it, "menu.view", "View")) viewHead = it;
                if (sameLabel(*it, "menu.markdown", "Markdown")) mdHead = it;
                continue;
            }
            ++editRows;
            const bool allowed =
                sameLabel(*it, "menu.edit.copy", "Copy") ||
                sameLabel(*it, "menu.edit.selectall", "Select All");
            const bool gray = (it->flags & FL_MENU_INACTIVE) != 0;
            if (gray) ++inactiveEdit; else ++activeEdit;
            if (gray != !allowed) ++grayMismatch;   // 该灰的灰、该亮的亮
        }
        CHECK(editRows == 9);
        CHECK(inactiveEdit == 7);
        CHECK(activeEdit == 2);
        CHECK(grayMismatch == 0);

        // View ▶ 整份复刻：开关组 + Font + Zoom（含子项）+ Statistics + Theme
        if (viewHead) {
            const Fl_Menu_Item *sub = viewHead + 1;
            CHECK(findInLevel(sub, "menu.view.menubar", "Menu Bar") != nullptr);
            CHECK(findInLevel(sub, "menu.view.font", "Font") != nullptr);
            CHECK(findInLevel(sub, "menu.view.statistics", "Statistics...") != nullptr);
            const Fl_Menu_Item *zoom = findInLevel(sub, "menu.view.zoom", "Zoom");
            CHECK(zoom != nullptr);
            if (zoom) {
                CHECK(findInLevel(zoom + 1, "menu.view.zoomin", "Zoom In") != nullptr);
                CHECK(findInLevel(zoom + 1, "menu.view.zoomout", "Zoom Out") != nullptr);
                CHECK(findInLevel(zoom + 1, "menu.view.zoomreset", "Reset Zoom") != nullptr);
            }
            const Fl_Menu_Item *theme = findInLevel(sub, "menu.view.theme", "Theme");
            CHECK(theme != nullptr);
            // Theme 必须原样保留 FL_SUBMENU_POINTER：cbSetTheme 靠
            // "项地址 == m_themeMenu[i]" 识别主题，复制一份会让选主题静默失效。
            if (theme) CHECK((theme->flags & FL_SUBMENU_POINTER) != 0);
        }

        // Markdown ▶ 整份复刻：预览开关 + 动作 + Auto Refresh 五档
        if (mdHead) {
            const Fl_Menu_Item *sub = mdHead + 1;
            CHECK(findInLevel(sub, "menu.markdown.preview", "Preview") != nullptr);
            CHECK(findInLevel(sub, "menu.markdown.previewonly", "Preview Only") != nullptr);
            CHECK(findInLevel(sub, "menu.markdown.refresh", "Refresh") != nullptr);
            CHECK(findInLevel(sub, "menu.markdown.openinbrowser", "Open in Browser") != nullptr);
            CHECK(findInLevel(sub, "menu.markdown.exporthtml", "Export HTML...") != nullptr);
            const Fl_Menu_Item *autoRefresh =
                findInLevel(sub, "menu.markdown.autorefresh", "Auto Refresh");
            CHECK(autoRefresh != nullptr);
            if (autoRefresh) {
                int radios = 0;
                for (const Fl_Menu_Item *it = autoRefresh + 1; it->text;
                     it = it->next(1))
                    if (it->flags & FL_MENU_RADIO) ++radios;
                CHECK(radios == 5);   // Off / 1s / 5s / 10s / 30s
            }
        }
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
    // ── 仅预览的键盘翻页 ──────────────────────────────────────────────────
    test_preview_keyboard_scroll();
    test_preview_right_click_dispatch();
    test_preview_link_click_dispatch();
    // ── 仅预览：菜单栏灰度 + 状态栏去行列号 ────────────────────────────────
    test_preview_only_menu_and_status();
    // ── 仅预览：编辑能力锁（键盘 / Lua / AI / APPLY 一律失效）───────────────
    test_preview_only_edit_lock();

    int fails = test::failCount();
    std::fprintf(stderr, "%d checks, %d failures\n", test::checkCount(), fails);
    return fails ? 1 : 0;
}
