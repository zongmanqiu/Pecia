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
#include "core/HostOps.h"
#include <map>

#include "core/Config.h"
#include "core/I18n.h"
#include "core/Theme.h"
#include "ui/Layout.h"
#include "ui/MainWindow.h"
#include "editor/Document.h"
#include "editor/Editor.h"
#include "ui/SidePanel.h"
#include "AIChat/AIChatPane.h"
#include "ui/SmokeTest.h"
#include "ui/ScriptDialogs.h"
#include "mdview/container.h"
#include "mdview/preview_panel.h"

#include <FL/Fl.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <vector>
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
// m025 gate: a window that dies with its preview OPEN must take the panel
// down with it. PreviewPanel owns its view as a CHILD OF THE WINDOW and arms a
// 30 ms pump timer pointing at itself; if only the window is destroyed, FLTK
// frees the widget, the panel survives with the timer armed, and the next tick
// runs pump() -> Fl_Widget::damage() on freed memory. That fault surfaces in
// whichever event loop happens to run next - it is how this gate died several
// windows after the preview tests (2026-11-26, backtraced with a VEH + map).
// ---------------------------------------------------------------------------
static void test_preview_panel_dies_with_window()
{
    std::fprintf(stderr, "[gate] preview_teardown\n");
    {
        MainWindow w(800, 600, "smoke-preview-teardown");
        w.show();
        settle();
        CHECK(w.insertAiText("# Teardown sample\n\nBody text.\n"));
        MainWindow::cbTogglePreviewOnly(&w, &w);   // 仅预览打开，且刻意不退出
        settle(0.2);                               // 渲染发生、pump 定时器挂起
    }
    // 面板若还活着，它的 30ms 泵就落在这几次事件循环里。
    for (int i = 0; i < 6; ++i) Fl::wait(0.1);
    MainWindow after(640, 480, "smoke-after-teardown");
    after.show();
    settle(0.2);
    CHECK(after.visible());
}

// ---------------------------------------------------------------------------
// m039 gate: the preview column and the AI column are INDEPENDENT.
//
// The user asked for "one more column, like the Markdown preview". An earlier
// attempt made the two share one slot behind a switcher tab, which is a
// different product: opening the preview threw away the conversation. This
// gate is the shape the correction has to keep:
//   - both flags can be true at once, and neither entry point clears the
//     other (that is exactly what the red proofs below pin);
//   - three rects side by side, in order, without overlap, with the chat
//     never narrower than its own button row;
//   - the pane's children stay inside the pane - it is a CHILD of the window,
//     so its coordinates are absolute (the first version painted the
//     conversation over the editor and every outer rect check stayed green);
//   - the wheel belongs to whichever column is under the pointer.
// ---------------------------------------------------------------------------
// The status bar's text, read through the window's own accessor. The old
// version hunted the widget tree for the English word "Size"; the bar is
// localized, so a content-sniffing lookup only ever worked in one language.
static std::string statusLine(MainWindow *w)
{
    return w ? w->statusBarText() : std::string();
}

static void test_columns_independent()
{
    std::fprintf(stderr, "[gate] columns\n");
    std::string md = "# Column sample\n\n";
    for (int i = 1; i <= 60; ++i)
        md += "## Section " + std::to_string(i) + "\n\nBody text.\n\n";
    MainWindow w(1200, 700, "smoke-columns");
    w.show();
    settle();
    CHECK(w.insertAiText(md));
    settle(0.2);

    std::fprintf(stderr, "c.1\n");
    // 1. Nothing open: neither the pane nor the second bar was paid for.
    CHECK(!w.m_aiChat);
    CHECK(!w.m_aiDivider);
    CHECK(!w.m_aiChatDocked);

    std::fprintf(stderr, "c.2\n");
    // 2. View > AI Chat alone: a pane inside this window, not a second HWND.
    MainWindow::cbAIChat(&w, &w);
    settle();
    CHECK(w.m_aiChatDocked);
    CHECK(!w.previewColumnOn());
    CHECK(w.m_aiChat && w.m_aiChat->visible());
    CHECK(w.m_aiChat->window() == static_cast<Fl_Window *>(&w));
    CHECK(w.m_aiChat->x() > 0);
    CHECK(w.m_aiChat->w() >= w.m_aiChat->minRowWidth());
    CHECK(w.m_aiChat->h() > 100);
    // no seam without a preview beside it
    CHECK(!w.m_aiDivider->visible());

    // 2b. m040: the button row is exactly two buttons - no attach checkbox,
    //     no API entry, no To-Document entry - and they do not run into each
    //     other. The column floor is derived from this row, so a silent third
    //     button here would be clipped by the column instead of noticed.
    {
        std::vector<Fl_Widget *> btns;
        int checkboxes = 0;
        for (int i = 0; i < w.m_aiChat->children(); ++i) {
            Fl_Widget *kid = w.m_aiChat->child(i);
            if (!kid) continue;
            if (dynamic_cast<Fl_Check_Button *>(kid)) ++checkboxes;
            else if (dynamic_cast<Fl_Button *>(kid)) btns.push_back(kid);
        }
        int overlap = 0;
        for (size_t a = 0; a < btns.size(); ++a)
            for (size_t b = a + 1; b < btns.size(); ++b)
                if (btns[a]->x() < btns[b]->x() + btns[b]->w()
                    && btns[b]->x() < btns[a]->x() + btns[a]->w()) ++overlap;
        CHECK(checkboxes == 0);
        CHECK((int)btns.size() == 2);
        CHECK(overlap == 0);
        std::fprintf(stderr, "chat row: buttons=%d minRow=%d colW=%d\n",
                     (int)btns.size(), w.m_aiChat->minRowWidth(), w.m_aiChat->w());
    }
    // the pane owns its own rectangle (absolute coordinates, see above)
    {
        int outside = 0;
        for (int i = 0; i < w.m_aiChat->children(); ++i) {
            Fl_Widget *kid = w.m_aiChat->child(i);
            if (!kid) continue;
            if (kid->x() < w.m_aiChat->x() || kid->y() < w.m_aiChat->y()
                || kid->x() + kid->w() > w.m_aiChat->x() + w.m_aiChat->w()
                || kid->y() + kid->h() > w.m_aiChat->y() + w.m_aiChat->h()) ++outside;
        }
        CHECK(outside == 0);
    }
    const int aiAloneW = w.m_aiChat->w();

    std::fprintf(stderr, "c.3\n");
    // 3. View > Markdown Preview joins - it must NOT displace the chat.
    MainWindow::cbTogglePreview(&w, &w);
    settle(0.3);
    CHECK(w.previewColumnOn());
    CHECK(w.m_aiChatDocked);
    CHECK(w.m_aiChat->visible());
    PreviewPanel *panel = w.previewPanel();
    CHECK(panel && panel->view() && panel->view()->visible());

    std::fprintf(stderr, "c.4\n");
    // 4. Three rects, in order, no overlap, chat keeps its button row.
    Fl_Tabs *tabs = nullptr;
    for (int i = 0; i < w.children(); ++i)
        if ((tabs = dynamic_cast<Fl_Tabs *>(w.child(i)))) break;
    CHECK(tabs != nullptr);
    const int edRight  = tabs ? tabs->x() + tabs->w() : 0;
    const int prevLeft = panel->view()->x();
    const int prevRight = prevLeft + panel->view()->w();
    const int aiLeft   = w.m_aiChat->x();
    CHECK(tabs && tabs->visible());
    CHECK(edRight <= prevLeft);
    CHECK(prevRight <= aiLeft);
    CHECK(w.m_aiChat->x() + w.m_aiChat->w() <= w.w());
    CHECK(w.m_aiChat->w() >= w.m_aiChat->minRowWidth());
    CHECK(w.m_aiChat->w() < aiAloneW);          // the preview took its share
    CHECK(w.m_aiDivider->visible());            // the seam between them
    CHECK(w.m_aiDivider->x() + kDividerW == aiLeft);

    std::fprintf(stderr, "c.5\n");
    // 5. Closing the preview leaves the conversation up (the core of m039).
    MainWindow::cbTogglePreview(&w, &w);
    settle(0.3);
    CHECK(!w.previewColumnOn());
    CHECK(w.m_aiChatDocked);
    CHECK(w.m_aiChat->visible());
    CHECK(w.m_aiChat->w() == aiAloneW);
    CHECK(!w.m_aiDivider->visible());
    // The panel dies with the column (deferred tick): the pointer above is
    // dangling from here on, so every reopen re-reads it from the window.
    panel = w.previewPanel();

    std::fprintf(stderr, "c.6\n");
    // 6. And the other way: closing the chat must leave the preview alone.
    MainWindow::cbAIChat(&w, &w);                 // chat off
    MainWindow::cbTogglePreview(&w, &w);          // preview on
    settle(0.3);
    CHECK(!w.m_aiChatDocked);
    CHECK(w.previewColumnOn());
    panel = w.previewPanel();
    CHECK(panel && panel->view()->visible());
    CHECK(!w.m_aiChat->visible());
    MainWindow::cbAIChat(&w, &w);                 // chat back, both up
    settle();
    CHECK(w.m_aiChatDocked && w.previewColumnOn());
    CHECK(panel && panel->view()->visible());

    std::fprintf(stderr, "c.7\n");
    // 7. ai.send is an idempotent open, never the menu's toggle.
    w.showAIChat();
    settle();
    w.showAIChat();
    settle();
    CHECK(w.m_aiChatDocked);
    CHECK(w.m_aiChat->visible());

    std::fprintf(stderr, "c.8\n");
    // 8. The second bar drags the preview|AI seam, not the editor.
    {
        const int aiBefore = w.m_aiChat->w();
        const int prevBefore = panel->view()->w();
        w.m_aiDivider->onDrag(0.90f);             // AI column wider
        settle();
        CHECK(w.m_aiChat->w() > aiBefore);
        CHECK(panel->view()->w() < prevBefore);
        w.m_aiDivider->onDrag(0.20f);             // and back
        settle();
        CHECK(w.m_aiChat->w() < aiBefore + 4);
    }

    std::fprintf(stderr, "c.9\n");
    // 9. The wheel scrolls the column under the pointer, not the other one.
    {
        settle(0.3);              // a render still landing would move scroll_y
        const int beforePrev = panel->view()->scroll_y();
        const int aiMid = w.m_aiChat->x() + w.m_aiChat->w() / 2;
        const int prevMid = panel->view()->x() + panel->view()->w() / 2;
        Fl::e_dy = 3;
        Fl::e_x = aiMid;
        Fl::e_y = w.m_aiChat->y() + 40;
        Fl::e_state = 0;
        static_cast<Fl_Widget *>(&w)->handle(FL_MOUSEWHEEL);
        settle();
        const int afterChat = panel->view()->scroll_y();
        CHECK(afterChat == beforePrev);                      // chat, not preview

        Fl::e_x = prevMid;                                   // over the preview
        static_cast<Fl_Widget *>(&w)->handle(FL_MOUSEWHEEL);
        settle();
        const int afterPrev = panel->view()->scroll_y();
        CHECK(afterPrev > beforePrev);
        std::fprintf(stderr, "wheel band: prev=%d chatX=%d prevX=%d afterChat=%d afterPrev=%d\n",
                     beforePrev, aiMid, prevMid, afterChat, afterPrev);
        Fl::e_dy = 0;
    }

    std::fprintf(stderr, "c.10\n");
    // 10. The request timer reads in the status line (the strip is gone).
    {
        // The elapsed seconds go through the language file, so the gate
        // asks for the LOCALIZED rendering instead of a hardcoded "AI 7s".
        char aiSeg[64];
        snprintf(aiSeg, sizeof(aiSeg), I18n::get("status.ai_elapsed"), 7LL);
        w.m_aiChatElapsed = 7;
        w.updateStatusBar();
        CHECK(statusLine(&w).find(aiSeg) != std::string::npos);
        w.m_aiChatElapsed = -1;
        w.updateStatusBar();
        CHECK(statusLine(&w).find(aiSeg) == std::string::npos);
    }

    std::fprintf(stderr, "c.11\n");
    // 11. A narrow window keeps the floors instead of overlapping.
    w.size(760, 700);
    settle();
    CHECK(w.m_aiChat->w() >= w.m_aiChat->minRowWidth());
    CHECK(panel->view()->w() >= 160);
    CHECK(tabs->w() >= (int)((w.w() - tabs->x()) * 0.20f) - 2);
    CHECK(w.m_aiChat->x() + w.m_aiChat->w() <= w.w());
    CHECK(panel->view()->x() + panel->view()->w() <= w.m_aiChat->x());

    // 12. Squeezed to the limit, the chat column still fits its own row:
    //     the floor is measured, so the editor yields before a button is
    //     pushed out of the pane (red proof K: aiMin = 1 makes this red).
    w.size(420, 700);
    settle();
    {
        int outside = 0;
        for (int k = 0; k < w.m_aiChat->children(); ++k) {
            Fl_Widget *kid = w.m_aiChat->child(k);
            if (!kid) continue;
            if (kid->x() < w.m_aiChat->x() || kid->x() + kid->w() > w.m_aiChat->x() + w.m_aiChat->w())
                ++outside;
        }
        CHECK(outside == 0);
        CHECK(w.m_aiChat->w() >= w.m_aiChat->minRowWidth());
        std::fprintf(stderr, "squeeze: w=%d colW=%d minRow=%d tabs=%d,%d prev=%d,%d chat=%d,%d\n",
                     w.w(), w.m_aiChat->w(), w.m_aiChat->minRowWidth(),
                     tabs->x(), tabs->w(), panel->view()->x(), panel->view()->w(),
                     w.m_aiChat->x(), w.m_aiChat->w());
    }

    w.hide();
    settle(0.2);
}

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
    std::fprintf(stderr, "[gate] right_click\n");
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
    std::fprintf(stderr, "[gate] link_click\n");
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
    std::fprintf(stderr, "[gate] preview_only_menu\n");
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
    // （经窗口自己的取文本接口读，不再在控件树里找 "Size"——状态栏是本地化的，
    //   按内容找只在一个语言下成立）
    w.tick();
    {
        const std::string bar = w.statusBarText();
        CHECK(!bar.empty());
        CHECK(std::strchr(bar.c_str(), ':') == nullptr);
        CHECK(std::strstr(bar.c_str(), I18n::get("status.size")) != nullptr);
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
    {
        const std::string bar = w.statusBarText();
        CHECK(!bar.empty());
        CHECK(std::strchr(bar.c_str(), ':') != nullptr);
    }

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
    std::fprintf(stderr, "[gate] preview_only_lock\n");
    MainWindow w(800, 600, "smoke-preview-only-lock");
    w.show();
    settle();
    CHECK(w.insertAiText("# Locked sample\n\nBody text.\n"));
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

// ---------------------------------------------------------------------------
// m025: the Lua execution ops as the MCP client sees them (real registry,
// real MainWindow, real handlers - not a re-implementation of the shape).
//
// Nails:
//   - lua.validate compiles without executing (document untouched), and a
//     script that WOULD write still writes nothing under validate;
//   - lua.run pass path applies and reports applied/verdict;
//   - lua.run error path leaves the document byte-identical;
//   - a runaway loop is cut off by the instruction budget (no hang);
//   - headless captures win:message into output instead of showing a box;
//   - console.set -> console.get round-trips the drafting buffer;
//   - script.run on an unknown name fails with not_found;
//   - every Exec op respects the edit lock (perm_denied, no side effect).
// ---------------------------------------------------------------------------
static void test_host_lua_ops()
{
    HostRegistry &reg = HostRegistry::instance();
    auto call = [&reg](const char *name, std::map<std::string, OpValue> args) {
        return reg.dispatch(name, args);
    };
    auto textOf = [](const OpOutcome &o, const char *key) -> std::string {
        if (o.value.kind() != OpValue::Kind::Map) return std::string();
        const OpValue *v = o.value.find(key);
        return v ? v->getString() : std::string();
    };
    auto intOf = [&](const OpOutcome &o, const char *key) -> long long {
        if (o.value.kind() != OpValue::Kind::Map) return 0LL;
        const OpValue *v = o.value.find(key);
        return v ? v->getInt() : 0LL;
    };
    auto boolOf = [&](const OpOutcome &o, const char *key) -> bool {
        if (o.value.kind() != OpValue::Kind::Map) return false;
        const OpValue *v = o.value.find(key);
        return v && v->getBool();
    };
    auto docText = [&]() {
        OpOutcome o = call("doc.get_text", {});
        return o.ok ? o.value.getString() : std::string("<ERR>");
    };

    MainWindow w(900, 650, "smoke-lua-ops");
    w.show();
    settle();
    CHECK(w.insertAiText("# Ops sample\n\nBody text.\n", nullptr));
    settle();

    // every op the round promised is registered (tools/list projects this
    // same registry, so this also proves the MCP surface)
    for (const char *op : { "lua.validate", "lua.run", "script.run",
                            "console.get", "console.set", "script.save",
                            "ai.send", "ai.get" }) {
        CHECK(reg.find(op) != nullptr);
    }

    // ---- validate: compiles, executes nothing -----------------------------
    {
        const std::string before = docText();
        std::map<std::string, OpValue> a;
        a["code"] = OpValue::makeStr("local x = ");
        OpOutcome o = call("lua.validate", a);
        CHECK(o.ok);
        CHECK(!boolOf(o, "ok"));
        CHECK(!textOf(o, "error").empty());
        CHECK(intOf(o, "line") >= 1);
        CHECK(docText() == before);            // zero side effect
    }
    {
        // a script that would rewrite the document under run()
        std::map<std::string, OpValue> a;
        a["code"] = OpValue::makeStr("return 'HACKED'");
        OpOutcome o = call("lua.validate", a);
        CHECK(o.ok);
        CHECK(boolOf(o, "ok"));
        CHECK(docText() == "# Ops sample\n\nBody text.\n");   // validate ran nothing
    }

    // ---- run: pass path applies once, reports verdict ---------------------
    {
        std::map<std::string, OpValue> a;
        a["code"] = OpValue::makeStr("return 'APPLIED'");
        OpOutcome o = call("lua.run", a);
        CHECK(o.ok);
        CHECK(boolOf(o, "applied"));
        CHECK(textOf(o, "verdict") == "pass");
        CHECK(docText() == "APPLIED");
        // restore the seed document for the sections below
        CHECK(w.insertAiText("# Ops sample\n\nBody text.\n", nullptr));
        settle();
    }

    // ---- run: runtime error leaves the document untouched -----------------
    {
        const std::string before = docText();
        std::map<std::string, OpValue> a;
        a["code"] = OpValue::makeStr("error('boom')");
        OpOutcome o = call("lua.run", a);
        CHECK(o.ok);                                   // protocol-level ok
        CHECK(textOf(o, "verdict") == "error");       // semantic failure
        CHECK(textOf(o, "error").find("boom") != std::string::npos);
        CHECK(!boolOf(o, "applied"));
        CHECK(docText() == before);
    }

    // ---- run: runaway loop is cut off (bounded, no hang) ------------------
    {
        const std::string before = docText();
        std::map<std::string, OpValue> a;
        a["code"] = OpValue::makeStr("local i = 0 while true do i = i + 1 end");
        a["timeout_ms"] = OpValue::makeInt(200);
        const ULONGLONG t0 = GetTickCount64();
        OpOutcome o = call("lua.run", a);
        const ULONGLONG dt = GetTickCount64() - t0;
        CHECK(o.ok);
        CHECK(textOf(o, "verdict") == "error");
        CHECK(textOf(o, "error").find("timeout") != std::string::npos);
        CHECK(docText() == before);
        CHECK(dt < 5000);                     // the hook fired, not a hang
    }

    // ---- headless: win:message captured into output, no modal -------------
    {
        std::map<std::string, OpValue> a;
        a["code"] = OpValue::makeStr("win:message('captured') local q = win:question('go?') return q and 'yes' or 'no'");
        a["answer"] = OpValue::makeBool(true);
        OpOutcome o = call("lua.run", a);
        CHECK(o.ok);
        CHECK(textOf(o, "output").find("[win:message] captured") != std::string::npos);
        CHECK(textOf(o, "result") == "yes");
    }

    // ---- output cap: oversized print truncates with the flag set ---------
    {
        std::map<std::string, OpValue> a;
        a["code"] = OpValue::makeStr("for i=1,500 do print('0123456789') end return ''");
        a["out_cap"] = OpValue::makeInt(1000);
        OpOutcome o = call("lua.run", a);
        CHECK(o.ok);
        CHECK(boolOf(o, "truncated"));
        CHECK((int)textOf(o, "output").size() == 1000);
    }

    // ---- explicit selection: agents have no mouse ------------------------
    {
        CHECK(w.insertAiText("one two three\n", nullptr));
        settle();
        std::map<std::string, OpValue> a;
        a["code"] = OpValue::makeStr("return string.upper(input)");
        a["sel_start"] = OpValue::makeInt(4);
        a["sel_end"] = OpValue::makeInt(7);
        OpOutcome o = call("lua.run", a);
        CHECK(o.ok);
        CHECK(boolOf(o, "applied"));
        CHECK(docText() == "one TWO three\n");   // line end survives the read
        // out-of-bounds ranges are refused, not clamped silently
        a["sel_end"] = OpValue::makeInt(9999);
        OpOutcome bad = call("lua.run", a);
        CHECK(!bad.ok);
        CHECK(bad.err.find("bad_arg:") == 0);
    }

    // ---- console.set / console.get / console.show (m037: no window) ------
    // The draft is persisted in script.last.txt next to the exe, so the test
    // must hand back whatever the user had there.
    {
        std::string savedDraft;
        OpOutcome pre = call("console.get", {});
        if (pre.ok) savedDraft = pre.value.getString();

        std::map<std::string, OpValue> a;
        a["code"] = OpValue::makeStr("-- drafted by the agent\nreturn 1");
        OpOutcome s = call("console.set", a);
        CHECK(s.ok);
        OpOutcome g = call("console.get", {});
        CHECK(g.ok);
        CHECK(g.value.getString() == "-- drafted by the agent\nreturn 1");

        // console.show must put the draft on screen WITHOUT a new window:
        // it opens an ordinary editor tab, so tab.list grows by one and the
        // active document is the draft text.
        int tabsBefore = 0;
        OpOutcome tl = call("tab.list", {});
        if (tl.ok) tabsBefore = (int)tl.value.list().size();
        OpOutcome sh = call("console.show", {});
        CHECK(sh.ok);
        settle();
        OpOutcome tl2 = call("tab.list", {});
        CHECK(tl2.ok && (int)tl2.value.list().size() == tabsBefore + 1);
        CHECK(docText() == "-- drafted by the agent\nreturn 1");

        // an empty draft is refused with a reason, not silently ignored
        std::map<std::string, OpValue> cl;
        cl["code"] = OpValue::makeStr("");
        call("console.set", cl);
        OpOutcome empty = call("console.show", {});
        CHECK(!empty.ok);
        CHECK(!empty.err.empty());

        std::map<std::string, OpValue> back;
        back["code"] = OpValue::makeStr(savedDraft);
        call("console.set", back);
    }

    // ---- script-run result is visible to a human (m037) ------------------
    {
        // The status bar is read through the accessor, not by sniffing the
        // widget tree for the word "Size": the bar's content is localized,
        // so a content-sniffing lookup would only work in one language.
        w.reportScriptOutcome(true, "a\nb\nc\n", "");
        w.tick();
        CHECK(w.statusBarText().find(I18n::get("lua.ok_short")) != std::string::npos);

        // the result belongs to the document it ran on: switching tabs drops it
        w.switchToTab(0);
        w.tick();
        CHECK(w.statusBarText().find(I18n::get("lua.ok_short")) == std::string::npos);

        // an agent-triggered run must leave the same trace, without an info
        // window (the notice is empty right now, so only the op can set it)
        {
            std::map<std::string, OpValue> a;
            a["code"] = OpValue::makeStr("error 'boom'");
            OpOutcome o = call("lua.run", a);
            CHECK(o.ok);                                  // transport fine
            CHECK(textOf(o, "verdict") == "error");       // the script failed
            w.tick();
            CHECK(w.statusBarText().find(I18n::get("lua.fail_short")) != std::string::npos);
        }

        w.reportScriptOutcome(false, "", "Lua error: bad script");
        w.tick();
        CHECK(w.statusBarText().find(I18n::get("lua.fail_short")) != std::string::npos);

        // ---- the number is the run's COST, not the output's line count ----
        // The status line used to read "Script done 3" / "脚本完成 0": a
        // counter that said nothing about the run (and whose meaning was
        // nobody's guess). It now carries the wall-clock cost.
        w.reportScriptOutcome(true, "a\nb\nc\n", "", true, 42);
        w.tick();
        {
            char timed[128];
            snprintf(timed, sizeof(timed), I18n::get("lua.ok_time"), 42LL);
            CHECK(w.statusBarText().find(timed) != std::string::npos);
            // the old meaning is gone: the three newlines of the output must
            // not show up as the number
            char oldForm[128];
            snprintf(oldForm, sizeof(oldForm), "%s %d", I18n::get("lua.ok_short"), 3);
            CHECK(w.statusBarText().find(oldForm) == std::string::npos);
        }

        // ---- the notice follows the UI language ----------------------------
        // It is stored as an i18n KEY, so switching the language re-renders
        // it; a pre-rendered string would keep the old wording on screen
        // (the complaint the change answers).
        {
            char enTimed[128];
            snprintf(enTimed, sizeof(enTimed), I18n::get("lua.ok_time"), 42LL);
            const std::string enShort = I18n::get("lua.ok_short");
            const std::string saved = I18n::currentCode();

            CHECK(I18n::load("zh-CN"));
            w.tick();
            char zhTimed[128];
            snprintf(zhTimed, sizeof(zhTimed), I18n::get("lua.ok_time"), 42LL);
            CHECK(w.statusBarText().find(zhTimed) != std::string::npos);
            // the English wording is gone: the notice was really re-rendered
            CHECK(w.statusBarText().find(enTimed) == std::string::npos);
            CHECK(w.statusBarText().find(enShort) == std::string::npos);

            CHECK(I18n::load(saved.c_str()));
            w.tick();
            CHECK(w.statusBarText().find(enTimed) != std::string::npos);
        }
    }

    // ---- menu structure: Tools dissolved (r042); AI Chat lives in View ----
    {
        // The Tools menu must be gone entirely...
        CHECK(w.menuBar()->find_index("Tools") < 0);
        // ...and View must carry exactly one AI Chat entry (the pane toggle).
        // Note: buildMenuKeys rewrote the labels to the UI language, so the
        // lookup path must be assembled from the i18n key, not the static
        // English table label.
        std::string path = std::string(I18n::get("menu.view")) + "/" + I18n::get("menu.view.aichat");
        int idx = w.menuBar()->find_index(path.c_str());
        CHECK(idx >= 0);
        if (idx >= 0) {
            const Fl_Menu_Item *it = w.menuBar()->menu() + idx;
            CHECK(it->callback_ == (Fl_Callback *)MainWindow::cbAIChat);
            CHECK(it->flags & FL_MENU_TOGGLE);            // it is a pane toggle
            CHECK(it->flags & FL_MENU_DIVIDER);           // line before Font group
        }
    }

    // ---- script.run: unknown name fails with not_found -------------------
    {
        std::map<std::string, OpValue> a;
        a["name"] = OpValue::makeStr("no-such-script-xyz");
        OpOutcome o = call("script.run", a);
        CHECK(!o.ok);
        CHECK(o.err.find("not_found:") == 0);
    }

    // ---- doc.api: the loop's very first step ------------------------------
    // The instructions tell every agent to read lua_api.txt before writing a
    // script. It ships as script/lua_api.txt (RELEASE_CHECKLIST §三), not next
    // to the exe, so a handler that only looked in the exe directory answered
    // not_found for every request.
    {
        std::map<std::string, OpValue> a;
        a["name"] = OpValue::makeStr("lua_api.txt");
        OpOutcome o = call("doc.api", a);
        CHECK(o.ok);
        CHECK(o.value.getString().find("pecia.doc_get_text") != std::string::npos);

        // traversal is refused before any file is opened
        a["name"] = OpValue::makeStr("..\\settings.ini");
        OpOutcome esc = call("doc.api", a);
        CHECK(!esc.ok);
        CHECK(esc.err.find("bad_arg:") == 0);

        a["name"] = OpValue::makeStr("definitely-not-shipped.txt");
        OpOutcome miss = call("doc.api", a);
        CHECK(!miss.ok);
        CHECK(miss.err.find("not_found:") == 0);
    }

    // ---- edit lock: see test_host_lua_ops_locked() (fresh window) ---------
    w.hide();
    settle();
}

// The lock half of the m025 gate runs on its own window: the preview-only
// mode is per-window state, and reusing the ops window (which has already
// been through run/console/script calls) does not reproduce the product's
// entry conditions.
static void test_host_lua_ops_locked()
{
    HostRegistry &reg = HostRegistry::instance();
    auto call = [&reg](const char *name, std::map<std::string, OpValue> args) {
        return reg.dispatch(name, args);
    };
    auto docText = [&]() {
        OpOutcome o = call("doc.get_text", {});
        return o.ok ? o.value.getString() : std::string("<ERR>");
    };

    MainWindow w(800, 600, "smoke-lua-ops-lock");
    w.show();
    settle();
    CHECK(w.insertAiText("# Locked ops sample\n\nBody text.\n", nullptr));
    settle();
    const std::string before = docText();

    MainWindow::cbTogglePreviewOnly(&w, &w);   // 与 test_preview_only_edit_lock 同法
    settle();
    Editor *ed = findEditor(&w);
    CHECK(ed && ed->doc() && ed->doc()->isEditLocked());

    std::map<std::string, OpValue> a;
    a["code"] = OpValue::makeStr("return 'x'");
    for (const char *op : { "lua.validate", "lua.run" }) {
        OpOutcome o = call(op, a);
        CHECK(!o.ok);
        CHECK(o.err.find("perm_denied:") == 0);
    }
    // script.run: the gate runs before the handler, so an unknown name does
    // not leak past a locked document.
    {
        std::map<std::string, OpValue> sa;
        sa["name"] = OpValue::makeStr("no-such-script-xyz");
        OpOutcome o = call("script.run", sa);
        CHECK(!o.ok);
        CHECK(o.err.find("perm_denied:") == 0);
    }
    CHECK(docText() == before);
    a["code"] = OpValue::makeStr("overwritten while locked");
    OpOutcome s = call("console.set", a);
    CHECK(!s.ok);
    CHECK(s.err.find("perm_denied:") == 0);
    CHECK(docText() == before);

    w.hide();
    settle();
}

// ---------------------------------------------------------------------------
// doc.get_text must hand back the buffer byte-for-byte. The MCP loop is
// read -> build an anchor from what you read -> doc.apply_edit, so any
// rewriting of line ends on the read side silently breaks the write side.
// The LF case alone does not prove the fix: the bug that lived here dropped
// '\n' and kept '\r', so only a CRLF document shows the two behaviours
// differing.
// ---------------------------------------------------------------------------
static void test_doc_get_text_is_byte_faithful()
{
    HostRegistry &reg = HostRegistry::instance();
    auto call = [&reg](const char *name, std::map<std::string, OpValue> args) {
        return reg.dispatch(name, args);
    };

    // The registry is a PROCESS singleton: the most recently constructed
    // MainWindow owns its entries (a window's teardown removes them). So
    // each part below uses its own window and the parts run one after the
    // other, never nested.
    {
        // LF document written through the host itself
        MainWindow w(900, 650, "smoke-get-text-lf");
        w.show();
        settle();

        CHECK(w.insertAiText("# One\n\nTwo\n", nullptr));
        settle();
        {
            OpOutcome o = call("doc.get_text", {});
            CHECK(o.ok);
            CHECK(o.value.getString() == "# One\n\nTwo\n");
        }

        // doc.open must REFUSE while the active document has unsaved
        // changes: it may neither discard them silently, nor hand the file
        // to a SECOND Pecia process (which would leave the agent editing the
        // old document while another window shows the new one - and, for a
        // non-product exe like this test, would spawn a copy of the test
        // itself, which spawned another copy...).
        const fs::path p = fs::temp_directory_path() / "pecia_smoke_gettext_crlf.md";
        {
            std::ofstream o(p, std::ios::binary | std::ios::trunc);
            o << "# Line one\r\n\r\nSecond body\r\n";
        }
        std::map<std::string, OpValue> a;
        a["path"] = OpValue::makeStr(p.string());
        OpOutcome refused = call("doc.open", a);
        CHECK(!refused.ok);
        CHECK(refused.err.find("unsaved_changes") != std::string::npos);
        {
            // ... and the document is untouched.
            OpOutcome o = call("doc.get_text", {});
            CHECK(o.ok);
            CHECK(o.value.getString() == "# One\n\nTwo\n");
        }
        std::error_code ec;
        fs::remove(p, ec);
        w.hide();
        settle();
    }

    {
        // CRLF document loaded from disk: line ends must come back intact.
        // A FRESH window, so the document is clean and doc.open may replace
        // it in place.
        MainWindow w(900, 650, "smoke-get-text-crlf");
        w.show();
        settle();

        const fs::path p = fs::temp_directory_path() / "pecia_smoke_gettext_crlf.md";
        {
            std::ofstream o(p, std::ios::binary | std::ios::trunc);
            o << "# Line one\r\n\r\nSecond body\r\n";
        }
        std::map<std::string, OpValue> a;
        a["path"] = OpValue::makeStr(p.string());
        OpOutcome opened = call("doc.open", a);
        CHECK(opened.ok);
        settle();

        OpOutcome o = call("doc.get_text", {});
        CHECK(o.ok);
        const std::string got = o.value.getString();
        CHECK(got == "# Line one\r\n\r\nSecond body\r\n");

        // the read/edit loop on the exact bytes just read: apply_edit locates
        // the edit AT the anchor, so the anchor starts where old_text starts
        std::map<std::string, OpValue> e;
        e["old_text"] = OpValue::makeStr("Second body");
        e["new_text"] = OpValue::makeStr("Second BODY");
        e["anchor"] = OpValue::makeStr("Second body\r\n");
        OpOutcome ed = call("doc.apply_edit", e);
        CHECK(ed.ok);
        OpOutcome again = call("doc.get_text", {});
        CHECK(again.value.getString() == "# Line one\r\n\r\nSecond BODY\r\n");

        // a byte window must not invent or drop anything either
        std::map<std::string, OpValue> win;
        win["start"] = OpValue::makeInt(2);
        win["length"] = OpValue::makeInt(9);
        OpOutcome part = call("doc.get_text", win);
        CHECK(part.ok);
        CHECK(part.value.getString() == "Line one\r");

        std::error_code ec;
        fs::remove(p, ec);
        w.hide();
        settle();
    }
}

// ---------------------------------------------------------------------------
// m026 gate: the script run that the menu defers by one event-loop tick must
// stop when the window that armed it is gone. ~MainWindow cannot revoke that
// timeout (its data is a heap struct, not `this`), so the liveness token is
// the only guard. Half A proves the plumbing really fires - without it half B
// would pass vacuously if someone made the callback always skip.
// ---------------------------------------------------------------------------
static void test_deferred_script_run_stops_with_window()
{
    static char rel[] = "01.No/01_number_dot";   // a shipped root-group script
    auto bufferOf = [](MainWindow *w) -> Fl_Text_Buffer * {
        Editor *ed = findEditor(w);
        return (ed && ed->doc()) ? ed->doc()->buffer() : nullptr;
    };
    auto textOf = [](Fl_Text_Buffer *b) {
        return b ? std::string(b->text_range(0, b->length())) : std::string();
    };

    // ---- A: live window, the deferred run lands on the next tick ---------
    {
        MainWindow w(800, 600, "smoke-defer-alive");
        w.show();
        settle();
        w.user_data(&w);
        CHECK(w.insertAiText("abc\ndef\n", nullptr));
        settle();
        Fl_Text_Buffer *buf = bufferOf(&w);
        CHECK(buf != nullptr);
        if (buf) buf->select(0, buf->length());
        MainWindow::cbRunScript(&w, rel);        // arms add_timeout(0.0, ...)
        settle(0.2);
        const std::string after = textOf(buf);
        // The numbering script really ran: both lines gained their index.
        CHECK(after == "1. abc\n2. def\n");
        w.hide();
        settle();
    }

    // ---- B: window dies before the tick; the token must stop it ----------
    {
        MainWindow *w = new MainWindow(800, 600, "smoke-defer-dead");
        w->show();
        settle();
        w->user_data(w);
        CHECK(w->insertAiText("abc\ndef\n", nullptr));
        settle();
        MainWindow::cbRunScript(w, rel);
        delete w;                                // freed before the deferred tick
        // Take the window's heap block back and fill it: a late tick that
        // ignores the token must then read loud garbage instead of intact
        // stale members (measured: without this, the crash rate is 1/10).
        const std::vector<char> scrub(1u << 20, '\x41');
        settle(0.2);
        Fl::wait(0.2);
        CHECK(!scrub.empty());
    }

    // The event loop and a fresh window must still be sound afterwards.
    MainWindow after(700, 500, "smoke-defer-after");
    after.show();
    settle();
    CHECK(after.visible());
    after.hide();
    settle();
}

// ---------------------------------------------------------------------------
// m025 M5: the dialogs a script opens must be MEASURABLE and DRIVABLE from
// the host registry - that is the whole reason Pecia hosts them instead of
// letting Lua pop a Win32 box.
// ---------------------------------------------------------------------------
static void test_script_dialog_measurement()
{
    HostRegistry &reg = HostRegistry::instance();
    auto call = [&reg](const char *name, std::map<std::string, OpValue> args) {
        return reg.dispatch(name, args);
    };

    MainWindow w(800, 600, "smoke-ui-measure");
    w.show();
    settle();

    PromptDialog dlg("Prompt", "Give a name", "abc",
                     w.themePtr(), w.uiFontSizeForDialogs());
    dlg.show();
    settle();

    // ---- ui.windows sees it ------------------------------------------------
    OpOutcome ws = call("ui.windows", {});
    CHECK(ws.ok);
    int dlgId = -1;
    for (const OpValue &e : ws.value.list()) {
        const OpValue *kind = e.find("kind");
        const OpValue *id = e.find("id");
        if (kind && kind->getString() == "input" && id) dlgId = (int)id->getInt();
    }
    CHECK(dlgId >= 0);
    if (dlgId < 0) { dlg.hide(); settle(); return; }

    // ---- ui.inspect returns the real tree with usable paths ----------------
    std::map<std::string, OpValue> ia;
    ia["id"] = OpValue::makeInt(dlgId);
    OpOutcome ins = call("ui.inspect", ia);
    CHECK(ins.ok);
    int inputPathDepth = -1, okButtons = 0;
    std::vector<OpValue> inputPath;
    for (const OpValue &node : ins.value.list()) {
        const OpValue *cls = node.find("class");
        const OpValue *pth = node.find("path");
        const OpValue *lab = node.find("label");
        const OpValue *val = node.find("value");
        if (!cls || !pth) continue;
        if (cls->getString() == "Fl_Input") {
            inputPathDepth = (int)pth->list().size();
            inputPath = pth->list();
            if (val) CHECK(val->getString() == "abc");
        }
        if (lab && lab->getString() == "OK") ++okButtons;
    }
    CHECK(inputPathDepth == 1);          // one path level under the window
    CHECK(okButtons >= 1);               // the OK button is addressable

    // ---- ui.send type writes into the input, visible on re-inspect ---------
    if (!inputPath.empty()) {
        std::map<std::string, OpValue> sa;
        OpValue p = OpValue::makeList();
        for (const OpValue &v : inputPath) p.push(v);
        sa["id"] = OpValue::makeInt(dlgId);
        sa["path"] = std::move(p);
        sa["event"] = OpValue::makeStr("type");
        sa["text"] = OpValue::makeStr("XYZ");
        OpOutcome s = call("ui.send", sa);
        CHECK(s.ok);
        OpOutcome ins2 = call("ui.inspect", ia);
        bool seen = false;
        for (const OpValue &node : ins2.value.list()) {
            const OpValue *cls = node.find("class");
            const OpValue *val = node.find("value");
            if (cls && cls->getString() == "Fl_Input" && val &&
                val->getString() == "abcXYZ") seen = true;
        }
        CHECK(seen);
    }

    // ---- ui.pixel reads the dialog off-screen (no foreground needed) ------
    {
        std::map<std::string, OpValue> pa;
        pa["id"] = OpValue::makeInt(dlgId);
        pa["x"] = OpValue::makeInt(4);
        pa["y"] = OpValue::makeInt(4);
        OpOutcome px = call("ui.pixel", pa);
        CHECK(px.ok);
        const std::string hex = px.value.getString();
        CHECK(hex.size() == 7 && hex[0] == '#');
    }

    // ---- ui.wait: present now, gone after hide ----------------------------
    {
        std::map<std::string, OpValue> wa;
        wa["title"] = OpValue::makeStr("Prompt");
        wa["timeout_ms"] = OpValue::makeInt(200);
        OpOutcome t1 = call("ui.wait", wa);
        CHECK(t1.ok && t1.value.find("seen") && t1.value.find("seen")->getBool());

        dlg.hide();
        settle();
        std::map<std::string, OpValue> wb;
        wb["id"] = OpValue::makeInt(dlgId);
        wb["timeout_ms"] = OpValue::makeInt(200);
        OpOutcome t2 = call("ui.wait", wb);
        CHECK(t2.ok && t2.value.find("seen") && t2.value.find("seen")->getBool());
    }

    // ---- hidden but alive: the id still reads, visible=false --------------
    // (unregistration is the destructor's job - a hidden dialog is a real
    // widget tree an agent may still want to inspect.)
    {
        std::map<std::string, OpValue> ia2;
        ia2["id"] = OpValue::makeInt(dlgId);
        OpOutcome again = call("ui.inspect", ia2);
        CHECK(again.ok);
        bool anyVisible = false;
        for (const OpValue &node : again.value.list()) {
            const OpValue *v = node.find("visible");
            if (v && v->getBool()) anyVisible = true;
        }
        CHECK(!anyVisible);
    }

    w.hide();
    settle();
}

int main() {
    // A crash must not swallow the checkpoint that was running: stderr is
    // unit-buffered here, so the last line printed IS the last line reached.
    setvbuf(stderr, nullptr, _IONBF, 0);
    // Never let the OS put up the Windows Error Reporting "Unknown Hard
    // Error" dialog when the test dies: a modal box on a crashed test run
    // blocks the whole build gate behind a click nobody will ever make
    // (and it hides the real failure behind a system dialog).
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
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
    // The D2 block needs multi-tab mode; the rest of the run asserts on the
    // SINGLE-tab rule ("close the last tab hides the window"). Leaving
    // multi_tab=1 behind changed what the remaining checks see - and when
    // the run died before ScopedConfigBackup's destructor, the file kept
    // multi_tab=1 for the NEXT run too (a crashed run silently changed the
    // next one's outcome).
    cfgBackup.forceSetting("multi_tab", "0");

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
    // ── 预览面板不得活过它的窗口（pump 定时器 UAF 回归）────────────────────
    test_preview_panel_dies_with_window();
    test_columns_independent();

    // ── m025：Lua 执行 ops（MCP 客户端看到的同一张注册表）─────────────────
    test_host_lua_ops();
    test_host_lua_ops_locked();
    test_doc_get_text_is_byte_faithful();
    test_deferred_script_run_stops_with_window();
    test_script_dialog_measurement();

    int fails = test::failCount();
    std::fprintf(stderr, "%d checks, %d failures\n", test::checkCount(), fails);
    return fails ? 1 : 0;
}
