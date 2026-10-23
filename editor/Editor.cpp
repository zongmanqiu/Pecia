// Editor.cpp - text editor widget implementation
#include "Editor.h"
#include "Document.h"
#include "core/SettingsProvider.h"
#include "core/Theme.h"
#include "ui/ThemeWidgets.h"  // scrollbarColors()
#include "core/Config.h"
#include "core/I18n.h"
#include "core/OpLog.h"
#include "ui/MainWindow.h"   // 右键菜单回调（Edit 子菜单项）

#include <FL/fl_draw.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_Menu_Bar.H>

#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>
#include <climits>

// MainWindow member, defined here because Editor is also linked into the
// match-highlight test without the rest of MainWindow_view.cpp. It copies
// every LIVE View item before Font into `out`, so the editor's right-click
// menu stays in sync with MenuTable automatically.
int MainWindow::buildViewContextMenu(Fl_Menu_Item *out, int cap) const {
    if (!out || cap < 3) return 0;

    Fl_Menu_Item *view = nullptr;
    for (const MenuKeyEntry &e : m_menuKeys) {
        if (e.key && strcmp(e.key, "menu.view") == 0) {
            view = e.item;
            break;
        }
    }
    if (!view && m_menu) view = (Fl_Menu_Item *)m_menu->find_item("View");
    if (!view) return 0;

    int n = 0;
    out[n++] = {view->text, 0, nullptr, nullptr, FL_SUBMENU};

    // Everything from Font onwards is intentionally not copied. Match the
    // translated label (and the original English one, in case the helper is
    // called before applyLanguageToMenu).
    const char *fontLabel = I18n::get("menu.view.font");
    const Fl_Menu_Item *font = nullptr;
    for (const Fl_Menu_Item *it = view + 1; it->text; ++it) {
        if ((fontLabel && *fontLabel && strcmp(it->text, fontLabel) == 0) ||
            strcmp(it->text, "Font") == 0) {
            font = it;
            break;
        }
    }

    for (const Fl_Menu_Item *it = view + 1; it->text && it != font; ++it) {
        if (it->flags & FL_MENU_INVISIBLE) continue;
        if (it->flags & FL_SUBMENU) continue;   // headers would need children
        if (n >= cap - 2) break;
        out[n] = *it;
        if (out[n].callback_) out[n].user_data((void *)this);
        ++n;
    }
    if (n <= 1) return 0;   // no items before Font -> no empty submenu

    // Word Wrap's divider separates it from Font in the main menu; there is
    // nothing after it in this submenu, so don't leave a trailing line.
    if (out[n - 1].flags & FL_MENU_DIVIDER)
        out[n - 1].flags &= ~FL_MENU_DIVIDER;
    out[n++] = {nullptr};   // end of View submenu
    return n;
}

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>  // ShellExecuteW
#endif

// 说明：这里原来有一份硬编码的 FONT_LIST（9 个"各文字系统代表字体"），
// 供 View > Font 的 radio 子菜单使用。实测它是错的 —— 其中 SimHei / NSimSun /
// Gulim 在很多机器上根本没装，Noto Sans SC 装了也会被 FLTK 的无参枚举漏掉，
// 选中即静默回退到 Courier。现在字体改为枚举系统已装
// （FontUtils::systemFontNames()），由 View > Font 打开的字体对话框呈现，
// 这份列表已删除。FontUtils 的实现也挪到了 editor/FontUtils.cpp
// （ui/FontDialog 也要用，不能拖整个编辑器进 smoke test）。

// ---- IME composition handling -------------------------------------------
//
// The Windows hook and the UTF-16 -> UTF-8 conversion live in
// ui/ImeInPlace.cpp (shared with every other text widget: dialogs, find/replace,
// AI chat and the Lua tool window). This side owns only the editor-specific
// storage: the composition string is not inserted into the buffer until commit,
// and the caret is parked at the composition cursor while composing.

void Editor::imeCompositionStart() {
    if (!buffer()) return;
    if (m_doc && m_doc->isReadOnly()) return;
    m_compActive = true;
    m_compStart = insert_position();
    m_compEnd = m_compStart;
    int s, e;
    if (buffer()->selection_position(&s, &e) && e > s) {
        m_compStart = s;   // composition replaces the selection
        m_compEnd = e;
    }
    m_compText.clear();
    m_compCursorBytes = 0;
}

void Editor::imeCompositionUpdate(const char *utf8, int cursorBytes) {
    if (!buffer()) return;
    if (m_doc && m_doc->isReadOnly()) return;
    if (!m_compActive) imeCompositionStart();
    if (!m_compActive) return;

    m_compText = utf8 ? utf8 : "";
    if (cursorBytes < 0) cursorBytes = 0;
    if (cursorBytes > (int)m_compText.size())
        cursorBytes = (int)m_compText.size();
    m_compCursorBytes = cursorBytes;

    // Park the caret at the composition cursor (clamped to the buffer end
    // when composing at the very end of the document).
    int caret = m_compStart + m_compCursorBytes;
    if (caret > buffer()->length()) caret = buffer()->length();
    insert_position(caret);
    show_insert_position();
    notifyCursorMoved();
    redraw();
}

void Editor::imeCompositionCommit(const char *utf8) {
    if (!buffer()) return;
    if (m_doc && m_doc->isReadOnly()) return;
    if (!m_compActive) {
        m_compStart = insert_position();
        m_compEnd = m_compStart;
    }
    std::string s = utf8 ? utf8 : "";
    int start = m_compStart, end = m_compEnd;
    if (start < 0) start = insert_position();
    if (end < start) end = start;
    int len = buffer()->length();
    if (start > len) start = len;
    if (end > len) end = len;
    if (start != end || !s.empty())
        buffer()->replace(start, end, s.c_str());
    insert_position(start + (int)s.size());
    show_insert_position();
    set_changed();
    if (when() & FL_WHEN_CHANGED) do_callback(FL_REASON_CHANGED);
    notifyCursorMoved();
    imeCompositionEnd();
    redraw();
}

void Editor::imeCompositionEnd() {
    if (!m_compActive) return;
    m_compActive = false;
    m_compText.clear();
    m_compCursorBytes = 0;
    m_compStart = -1;
    m_compEnd = -1;
    redraw();
}


// ---- Editor implementation -----------------------------------------------

Editor::Editor(int X, int Y, int W, int H, SettingsProvider *settings)
    : Fl_Text_Editor(X, Y, W, H),
      m_settings(settings),
      m_styleBuf(nullptr),
      m_lineHighlightOn(false),
      m_lastLineStart(-1),
      m_highlightDataSet(false),
      m_showSpaceSymbols(false),
      m_longLineMarker(0),
      m_inShiftTab(false) {
    // 10px scrollbar width
    scrollbar_size(10);
    // Caret: FLTK's own SIMPLE_CURSOR - two 1px vertical lines (X and X+1),
    // no top/bottom caps. 2px wide like the hand-drawn caret used before,
    // but drawn by FLTK itself so nothing has to be erased: the old
    // "wipe a band then paint our own" approach is what produced stray
    // 1px leftovers (visible as a double cursor) whenever the erase
    // geometry didn't match FLTK's cursor footprint exactly.
    cursor_style(Fl_Text_Display::SIMPLE_CURSOR);
    linenumber_fgcolor(textColor());    // match textPrimary
    m_styleTable[0] = Fl_Text_Display::Style_Table_Entry();
    m_styleTable[1] = Fl_Text_Display::Style_Table_Entry();
    m_styleTable[2] = Fl_Text_Display::Style_Table_Entry();
    m_styleTable[3] = Fl_Text_Display::Style_Table_Entry();
    m_styleTable[4] = Fl_Text_Display::Style_Table_Entry();
    m_styleTable[5] = Fl_Text_Display::Style_Table_Entry();
    m_styleTable[6] = Fl_Text_Display::Style_Table_Entry();
    m_styleTable[7] = Fl_Text_Display::Style_Table_Entry();
    m_styleTable[8] = Fl_Text_Display::Style_Table_Entry();
}

Editor::~Editor() {
    // Unregister the shared IME hook before the widget goes away.
    ImeInPlace::release(this);
    if (m_styleBuf) {
        highlight_data(nullptr, nullptr, 0, 'A', 0, 0);
        delete m_styleBuf;
    }
}

// ---- Scrollbar drawing ---------------------------------------------------
//
// 滑块几何复用 ui/ThemeWidgets.h 的 scrollbarThumbGeometry()，与工具窗共用一份，
// 且与 Fl_Slider::handle() 的命中测试逐字一致（详见该函数注释）。
//
// ★ 历史 bug：这里曾用 `thumb = max(int(L*slider_size), 20)`。那个 20px 下限让
//   **绘制**比FLTK 的**命中区**大（长文档时 slider_size 很小，命中区缩到
//   T=6px，而仍画 20px）。实测（800x600 / Consolas 16 / wrap 开）：2000 行
//   文档下可见滑块 20 行里有 10 行是死区（50%），478 行文档 40 行里有 1 行
//   死区、间隙 1~2px —— 正是"垂直滑块没办法滑到最后，总是有一点小间隙"。

void Editor::drawCustomScrollbar(Fl_Scrollbar *sb, bool horizontal) {
    if (!sb || !sb->visible_r()) return;

    // 两个轴都用控件自身的矩形（窗口绝对坐标）。水平条早先被硬编码成
    // sx=0 / sw=w()-vscroll_w —— 那只在"编辑器贴着窗口左缘"时成立，见
    // draw() 里 resize 处的注释。
    int sx = sb->x();
    int sy = sb->y();
    int sw = sb->w();
    int sh = sb->h();
    if (sw <= 0 || sh <= 0) return;

    Fl_Color trackColor, thumbColor;
    if (m_theme) scrollbarColors(m_theme->colors(), trackColor, thumbColor);
    else { trackColor = fl_rgb_color(240, 240, 240); thumbColor = fl_rgb_color(180, 180, 180); }

    // 只在编辑器自己的矩形里画：轨道/滑块绝不会溢到左侧面板或预览区
    fl_push_clip(x(), y(), w(), h());

    // Track
    fl_color(trackColor);
    fl_rectf(sx, sy, sw, sh);

    // Thumb —— 几何来自与 FLTK 命中测试共用的公式
    int thumb_len = 0, thumb_pos = 0;
    if (horizontal)
        scrollbarThumbGeometry(sb, sx, sw, sh, &thumb_len, &thumb_pos);
    else
        scrollbarThumbGeometry(sb, sy, sh, sw, &thumb_len, &thumb_pos);

    fl_color(thumbColor);
    if (horizontal)
        fl_rectf(thumb_pos, sy, thumb_len, sh);
    else
        fl_rectf(sx, thumb_pos, sw, thumb_len);

    fl_pop_clip();
}

// ---- Current-line highlight ----------------------------------------------

void Editor::applyLineHighlight() {
    if (!buffer()) return;
    int len = buffer()->length();

    // For huge buffers, building an equal-size style buffer costs another
    // copy of the whole file (hundreds of MB) and seconds of startup time
    // for a visual nicety. The current-line highlight is still drawn by
    // the draw() overlay rectangle + draw_vline (it does not need the
    // style buffer); only FindReplace's match highlighting is unavailable
    // on such files. Large files are typically opened read-only anyway.
    if (len > kStyleBufLimitBytes) {
        // Drop any style buffer that a previous (smaller) document attached,
        // so the editor doesn't keep a stale, mismatched highlight_data that
        // no longer matches the new buffer's length (would corrupt layout).
        if (m_styleBuf) {
            highlight_data(nullptr, nullptr, 0, 'A', 0, 0);
            delete m_styleBuf;
            m_styleBuf = nullptr;
        }
        m_lastLineStart = -1;
        m_highlightDataSet = false;
        return;
    }

    // Create style buffer on first call.
    if (!m_styleBuf) {
        m_styleBuf = new Fl_Text_Buffer(len > 0 ? len : 1);
    }

    // Resize / fill the style buffer so its length matches the
    // text buffer exactly. When lengths already match we still re-fill
    // with 'A' to reset any highlight styles left by FindReplace.
    if (len <= 0) {
        m_styleBuf->text("");
    } else {
        std::string fill(len, 'A');
        m_styleBuf->text(fill.c_str());
    }

    // Build the style table (uses current font / size).
    m_styleTable[0].color   = textColor();
    m_styleTable[0].font    = textfont();
    m_styleTable[0].size    = textsize();
    m_styleTable[0].attr    = 0;
    m_styleTable[0].bgcolor = bgColor();

    m_styleTable[1].color   = textColor();
    m_styleTable[1].font    = textfont();
    m_styleTable[1].size    = textsize();
    m_styleTable[1].attr    = Fl_Text_Display::ATTR_BGCOLOR;
    m_styleTable[1].bgcolor = m_theme ? m_theme->colors().highlight1 : fl_rgb_color(255, 255, 191);  // current line

    // URL style: underline only (style 'C')
    m_styleTable[2].color   = textColor();
    m_styleTable[2].font    = textfont();
    m_styleTable[2].size    = textsize();
    m_styleTable[2].attr    = Fl_Text_Display::ATTR_UNDERLINE;
    m_styleTable[2].bgcolor = bgColor();

    // Search-match style (style 'D'): highlight3 background, no underline.
    // All matches use highlight3; the current match additionally gets
    // FLTK's native text selection (highlight2) via buf->select().
    Fl_Color searchBg = m_theme ? m_theme->colors().highlight3
                                : fl_rgb_color(255, 255, 180);
    m_styleTable[3].color   = textColor();
    m_styleTable[3].font    = textfont();
    m_styleTable[3].size    = textsize();
    m_styleTable[3].attr    = Fl_Text_Display::ATTR_BGCOLOR;
    m_styleTable[3].bgcolor = searchBg;

    // URL on the current line (style 'E'): underline + line highlight.
    m_styleTable[4].color   = textColor();
    m_styleTable[4].font    = textfont();
    m_styleTable[4].size    = textsize();
    m_styleTable[4].attr    = Fl_Text_Display::ATTR_UNDERLINE | Fl_Text_Display::ATTR_BGCOLOR;
    m_styleTable[4].bgcolor = m_styleTable[1].bgcolor;

    // URL inside a search match (style 'F'): underline + match bg.
    m_styleTable[5].color   = textColor();
    m_styleTable[5].font    = textfont();
    m_styleTable[5].size    = textsize();
    m_styleTable[5].attr    = Fl_Text_Display::ATTR_UNDERLINE | Fl_Text_Display::ATTR_BGCOLOR;
    m_styleTable[5].bgcolor = m_styleTable[3].bgcolor;  // highlight2 (search match)

    // URL hover variants (styles 'G'/'H'/'I'): underline + linkHover colour.
    Fl_Color linkHover = m_theme ? m_theme->colors().highlight3
                                 : fl_rgb_color(6, 69, 173);
    m_styleTable[6].color   = linkHover;
    m_styleTable[6].font    = textfont();
    m_styleTable[6].size    = textsize();
    m_styleTable[6].attr    = Fl_Text_Display::ATTR_UNDERLINE;
    m_styleTable[6].bgcolor = bgColor();
    m_styleTable[7].color   = linkHover;
    m_styleTable[7].font    = textfont();
    m_styleTable[7].size    = textsize();
    m_styleTable[7].attr    = Fl_Text_Display::ATTR_UNDERLINE | Fl_Text_Display::ATTR_BGCOLOR;
    m_styleTable[7].bgcolor = m_styleTable[1].bgcolor;
    m_styleTable[8].color   = linkHover;
    m_styleTable[8].font    = textfont();
    m_styleTable[8].size    = textsize();
    m_styleTable[8].attr    = Fl_Text_Display::ATTR_UNDERLINE | Fl_Text_Display::ATTR_BGCOLOR;
    m_styleTable[8].bgcolor = m_styleTable[3].bgcolor;  // highlight2 (search match)

    // highlight_data() only needs to be called once.
    if (!m_highlightDataSet) {
        highlight_data(m_styleBuf, m_styleTable, 9, 'A', 0, 0);
        m_highlightDataSet = true;
    }

    if (len <= 0) {
        m_lastLineStart = -1;
        redraw();
        return;
    }

    // Mark the current line with style 'B'.
    int pos = insert_position();
    if (pos < 0) pos = 0;
    if (pos > len) pos = len;
    int lineStart = buffer()->line_start(pos);
    int lineEnd   = buffer()->line_end(pos);
    if (lineEnd > len) lineEnd = len;
    m_lastLineStart = lineStart;

    if (m_lineHighlightOn && lineEnd > lineStart) {
        std::string hl(lineEnd - lineStart, 'B');
        m_styleBuf->replace(lineStart, lineEnd, hl.c_str());
    }

    // Mark search-match ranges with style 'D' (painted over the line
    // highlight; URLs win over both via updateUrlHighlight below).
    for (const auto &r : m_matchRanges) {
        int a = r.first, b = r.second;
        if (a < 0) a = 0;
        if (b > len) b = len;
        if (b <= a) continue;
        std::string m(b - a, 'D');
        m_styleBuf->replace(a, b, m.c_str());
    }

    // Overlay URL highlights on top of everything else.
    updateUrlHighlight();

    redraw();
}

void Editor::updateUrlHighlight() {
    if (!buffer()) return;
    // Respect user preference. When detection is turned OFF, scrub any
    // leftover 'C' styles so no stale underlines remain.
    if (m_settings && !m_settings->getDetectUrls()) {
        clearUrlStyles();
        return;
    }
    int len = buffer()->length();
    if (len <= 0) return;
    // URL underlines are a nicety; scanning multi-megabyte files byte by
    // byte costs seconds for no visible benefit (URLs in such files are
    // rare, and the underline would scroll by too fast to read anyway).
    if (len > kUrlScanLimitBytes) return;
    // Ensure style buffer exists
    if (!m_styleBuf) {
        m_styleBuf = new Fl_Text_Buffer(len > 0 ? len : 1);
        std::string fill(len, 'A');
        m_styleBuf->text(fill.c_str());
        m_styleTable[0].color   = textColor();
        m_styleTable[0].font    = textfont();
        m_styleTable[0].size    = textsize();
        m_styleTable[0].attr    = 0;
        m_styleTable[0].bgcolor = bgColor();
        m_styleTable[2].color   = textColor();
        m_styleTable[2].font    = textfont();
        m_styleTable[2].size    = textsize();
        m_styleTable[2].attr    = Fl_Text_Display::ATTR_UNDERLINE;
        m_styleTable[2].bgcolor = bgColor();
        // URL hover variant (style 'G') - other variants get registered
        // by reapplyHighlightData() on the next theme/style pass.
        m_styleTable[6].color   = m_theme ? m_theme->colors().highlight3
                                          : fl_rgb_color(6, 69, 173);
        m_styleTable[6].font    = textfont();
        m_styleTable[6].size    = textsize();
        m_styleTable[6].attr    = Fl_Text_Display::ATTR_UNDERLINE;
        m_styleTable[6].bgcolor = bgColor();
        highlight_data(m_styleBuf, m_styleTable, 9, 'A', 0, 0);
        m_highlightDataSet = true;
    }
    const char *text = buffer()->text();
    if (!text) return;

    // Current line span: URLs on it get the line-highlight background
    // too (style 'E'); URLs inside a search match get the match
    // background (style 'F'); everywhere else plain underline ('C').
    int len2 = buffer()->length();
    int curPos = insert_position();
    if (curPos < 0) curPos = 0;
    if (curPos > len2) curPos = len2;
    int curStart = buffer()->line_start(curPos);
    int curEnd   = buffer()->line_end(curPos);
    if (curEnd > len2) curEnd = len2;

    for (int i = 0; i < len; ) {
        int protoLen = 0;
        if (i + 8 <= len && strncmp(text + i, "https://", 8) == 0) protoLen = 8;
        else if (i + 7 <= len && strncmp(text + i, "http://", 7) == 0) protoLen = 7;
        if (protoLen == 0) { ++i; continue; }

        int urlStart = i;
        int urlEnd = i + protoLen;
        while (urlEnd < len) {
            char c = text[urlEnd];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') break;
            ++urlEnd;
        }
        while (urlEnd > urlStart + protoLen) {
            char c = text[urlEnd - 1];
            if (c == '.' || c == ',' || c == ';' || c == ':' ||
                c == '!' || c == '?' || c == ')' || c == ']' || c == '}' ||
                c == '"' || c == '\'' || c == '>')
                --urlEnd;
            else break;
        }
        if (urlEnd > urlStart + protoLen) {
            char style = 'C';
            bool inMatch = false;
            for (const auto &r : m_matchRanges) {
                if (r.second <= urlStart) continue;
                if (r.first >= urlEnd) break;
                inMatch = true;   // overlap
                break;
            }
            if (inMatch) style = 'F';
            else if (m_lineHighlightOn && urlStart < curEnd && urlEnd > curStart) style = 'E';
            std::string urlStyle(urlEnd - urlStart, style);
            m_styleBuf->replace(urlStart, urlEnd, urlStyle.c_str());
        }
        i = urlEnd;
    }
    ::free((void *)text);

    // updateUrlHighlight() just rewrote the URL styles; re-apply the
    // hover variant if the mouse is still over a URL.
    if (m_overUrl) applyUrlHoverStyles();
}

// Replace any leftover URL styles ('C'/'E'/'F') with the plain style
// ('A'), keeping the line highlight ('B') and match styles ('D')
// untouched. Used when URL detection is turned off.
void Editor::clearUrlStyles() {
    if (!m_styleBuf || !buffer()) return;
    int len = m_styleBuf->length();
    if (len <= 0) return;
    const char *st = m_styleBuf->text();
    if (!st) return;
    bool hasUrl = false;
    for (int i = 0; i < len; ++i) {
        char c = st[i];
        if (c == 'C' || c == 'E' || c == 'F' ||
            c == 'G' || c == 'H' || c == 'I') { hasUrl = true; break; }
    }
    ::free((void *)st);
    if (!hasUrl) return;
    std::string out(len, 'A');
    const char *src = m_styleBuf->text();
    if (!src) return;
    for (int i = 0; i < len; ++i) {
        char c = src[i];
        out[i] = (c == 'C' || c == 'E' || c == 'F' ||
                  c == 'G' || c == 'H' || c == 'I') ? 'A' : c;
    }
    ::free((void *)src);
    m_styleBuf->replace(0, len, out.c_str());
}

// Byte range [start, end) of the URL containing `pos`, or false when
// `pos` is not inside a URL. Shared by Ctrl+Click (open in browser) and
// the hover-cursor check so both follow the exact same scanning rules.
bool Editor::urlRangeAt(int pos, int &start, int &end) {
    Fl_Text_Buffer *buf = buffer();
    if (!buf || pos < 0) return false;
    int len = buf->length();
    if (pos > len) return false;
    // Scan backward from pos for a http:// or https:// scheme.
    int s = pos;
    for (;;) {
        bool isHttp = s + 7 <= len &&
            buf->byte_at(s)     == 'h' && buf->byte_at(s + 1) == 't' &&
            buf->byte_at(s + 2) == 't' && buf->byte_at(s + 3) == 'p' &&
            buf->byte_at(s + 4) == ':' && buf->byte_at(s + 5) == '/' &&
            buf->byte_at(s + 6) == '/';
        bool isHttps = s + 8 <= len &&
            buf->byte_at(s)     == 'h' && buf->byte_at(s + 1) == 't' &&
            buf->byte_at(s + 2) == 't' && buf->byte_at(s + 3) == 'p' &&
            buf->byte_at(s + 4) == 's' && buf->byte_at(s + 5) == ':' &&
            buf->byte_at(s + 6) == '/' && buf->byte_at(s + 7) == '/';
        if (isHttp || isHttps) break;
        if (s == 0) return false;
        char c = buf->byte_at(s - 1);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') return false;
        --s;
    }
    int protoLen = (buf->byte_at(s + 4) == 's') ? 8 : 7;
    int e = s + protoLen;
    while (e < len) {
        char c = buf->byte_at(e);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') break;
        ++e;
    }
    while (e > s + protoLen) {
        char c = buf->byte_at(e - 1);
        if (c == '.' || c == ',' || c == ';' || c == ':' ||
            c == '!' || c == '?' || c == ')' || c == ']' || c == '}' ||
            c == '"' || c == '\'' || c == '>')
            --e;
        else break;
    }
    if (e <= s + protoLen || pos >= e) return false;
    start = s;
    end = e;
    return true;
}

// URL hover feedback: switch the hovered URL's style from 'C'/'E'/'F'
// to the linkHover variants 'G'/'H'/'I'. Called from checkUrlHover()
// when the mouse enters a URL, and re-applied after updateUrlHighlight()
// rewrites the URL styles (which would otherwise clobber the hover).
void Editor::applyUrlHoverStyles() {
    if (!m_styleBuf || m_urlHoverStart < 0) return;
    int len = m_styleBuf->length();
    int s = m_urlHoverStart, e = m_urlHoverEnd;
    if (s >= e || e > len) return;
    const char *st = m_styleBuf->text();
    if (!st) return;
    std::string out(st + s, e - s);
    ::free((void *)st);
    bool changed = false;
    for (size_t i = 0; i < out.size(); ++i) {
        char c = out[i];
        if (c == 'C')      { out[i] = 'G'; changed = true; }
        else if (c == 'E') { out[i] = 'H'; changed = true; }
        else if (c == 'F') { out[i] = 'I'; changed = true; }
    }
    if (changed) m_styleBuf->replace(s, e, out.c_str());
}

// Reverse of applyUrlHoverStyles: back to 'C'/'E'/'F'.
void Editor::restoreUrlHoverStyles() {
    if (!m_styleBuf || m_urlHoverStart < 0) return;
    int len = m_styleBuf->length();
    int s = m_urlHoverStart, e = m_urlHoverEnd;
    if (s >= e || e > len) return;
    const char *st = m_styleBuf->text();
    if (!st) return;
    std::string out(st + s, e - s);
    ::free((void *)st);
    bool changed = false;
    for (size_t i = 0; i < out.size(); ++i) {
        char c = out[i];
        if (c == 'G')      { out[i] = 'C'; changed = true; }
        else if (c == 'H') { out[i] = 'E'; changed = true; }
        else if (c == 'I') { out[i] = 'F'; changed = true; }
    }
    if (changed) m_styleBuf->replace(s, e, out.c_str());
}

// Mouse-over URL tracking, called from FL_MOVE and FL_MOUSEWHEEL (after
// the base class handled the scroll, so xy_to_position uses the new
// viewport) and FL_LEAVE. Replaces the old hand-cursor feedback.
void Editor::checkUrlHover() {
    bool over = false;
    int us = -1, ue = -1;
    if (m_settings && m_settings->getDetectUrls() && mMaxsize > 0) {
        int pos = xy_to_position(Fl::event_x(), Fl::event_y());
        if (pos >= 0) over = urlRangeAt(pos, us, ue);
    }
    if (over == m_overUrl && us == m_urlHoverStart && ue == m_urlHoverEnd)
        return;   // nothing changed
    if (m_overUrl) restoreUrlHoverStyles();
    m_overUrl = over;
    m_urlHoverStart = us;
    m_urlHoverEnd = ue;
    if (over) applyUrlHoverStyles();
    redraw();
}

void Editor::reapplyHighlightData() {
    if (!m_styleBuf) return;
    if (!m_theme) return;  // Theme must be set before calling this
    m_styleTable[0].color   = textColor();
    m_styleTable[0].font    = textfont();
    m_styleTable[0].size    = textsize();
    m_styleTable[0].attr    = 0;
    m_styleTable[0].bgcolor = bgColor();
    m_styleTable[1].color   = textColor();
    m_styleTable[1].font    = textfont();
    m_styleTable[1].size    = textsize();
    m_styleTable[1].attr    = Fl_Text_Display::ATTR_BGCOLOR;
    m_styleTable[1].bgcolor = m_theme->colors().highlight1;
    // URL style (style 'C'): underline only. Must be registered here or
    // FLTK renders 'C' with the default style (no underline).
    m_styleTable[2].color   = textColor();
    m_styleTable[2].font    = textfont();
    m_styleTable[2].size    = textsize();
    m_styleTable[2].attr    = Fl_Text_Display::ATTR_UNDERLINE;
    m_styleTable[2].bgcolor = bgColor();
    // Search-match style (style 'D'): highlight3 background, no underline.
    // All matches use highlight3; the current match additionally gets
    // FLTK's native text selection (highlight2) via buf->select().
    Fl_Color searchBg = m_theme ? m_theme->colors().highlight3
                                : fl_rgb_color(255, 255, 180);
    m_styleTable[3].color   = textColor();
    m_styleTable[3].font    = textfont();
    m_styleTable[3].size    = textsize();
    m_styleTable[3].attr    = Fl_Text_Display::ATTR_BGCOLOR;
    m_styleTable[3].bgcolor = searchBg;
    m_styleTable[4].color   = textColor();
    m_styleTable[4].font    = textfont();
    m_styleTable[4].size    = textsize();
    m_styleTable[4].attr    = Fl_Text_Display::ATTR_UNDERLINE | Fl_Text_Display::ATTR_BGCOLOR;
    m_styleTable[4].bgcolor = m_styleTable[1].bgcolor;
    m_styleTable[5].color   = textColor();
    m_styleTable[5].font    = textfont();
    m_styleTable[5].size    = textsize();
    m_styleTable[5].attr    = Fl_Text_Display::ATTR_UNDERLINE | Fl_Text_Display::ATTR_BGCOLOR;
    m_styleTable[5].bgcolor = searchBg;
    // URL hover variants (styles 'G'/'H'/'I'): underline + linkHover colour.
    Fl_Color linkHover = m_theme ? m_theme->colors().highlight3
                                 : fl_rgb_color(6, 69, 173);
    m_styleTable[6].color   = linkHover;
    m_styleTable[6].font    = textfont();
    m_styleTable[6].size    = textsize();
    m_styleTable[6].attr    = Fl_Text_Display::ATTR_UNDERLINE;
    m_styleTable[6].bgcolor = bgColor();
    m_styleTable[7].color   = linkHover;
    m_styleTable[7].font    = textfont();
    m_styleTable[7].size    = textsize();
    m_styleTable[7].attr    = Fl_Text_Display::ATTR_UNDERLINE | Fl_Text_Display::ATTR_BGCOLOR;
    m_styleTable[7].bgcolor = m_styleTable[1].bgcolor;
    m_styleTable[8].color   = linkHover;
    m_styleTable[8].font    = textfont();
    m_styleTable[8].size    = textsize();
    m_styleTable[8].attr    = Fl_Text_Display::ATTR_UNDERLINE | Fl_Text_Display::ATTR_BGCOLOR;
    m_styleTable[8].bgcolor = searchBg;
    highlight_data(m_styleBuf, m_styleTable, 9, 'A', 0, 0);
    m_highlightDataSet = true;
}

void Editor::setLineHighlight(bool on) {
    if (on == m_lineHighlightOn) return;
    m_lineHighlightOn = on;
    m_lastLineStart = -1;

    if (on) {
        applyLineHighlight();
    } else {
        // Keep the style mechanism alive (6-style table + highlight_data)
        // so URL underlines still show; only the current-line 'B' style
        // disappears. Without this, turning the line highlight off would
        // also kill URL underlines until the next full rebuild.
        if (m_styleBuf && buffer()) {
            int len = buffer()->length();
            if (len > 0) {
                std::string fill(len, 'A');
                m_styleBuf->text(fill.c_str());
            }
            reapplyHighlightData();
            m_lastLineStart = -1;
            updateUrlHighlight();
        } else {
            highlight_data(nullptr, nullptr, 0, 'A', 0, 0);
            m_highlightDataSet = false;
        }
    }
    damage(FL_DAMAGE_ALL);
    redraw();
}

void Editor::refreshUrlHighlight() {
    updateUrlHighlight();
}

void Editor::setMatchRanges(const std::vector<std::pair<int,int>> &ranges) {
    m_matchRanges = ranges;
    // Full rebuild (unlike refreshLineHighlight, which bails out when
    // the line highlight is off): matches must show either way.
    applyLineHighlight();
    redraw();
}

void Editor::clearMatchRanges() {
    if (m_matchRanges.empty()) return;
    m_matchRanges.clear();
    applyLineHighlight();
    redraw();
}

const char *Editor::debugStyleString() const {
    static char out[256];
    out[0] = 0;
    if (!m_styleBuf) return out;
    char *t = m_styleBuf->text();
    if (t) {
        snprintf(out, sizeof(out), "%s", t);
        ::free(t);
    }
    return out;
}

void Editor::refreshLineHighlight() {
    if (!m_lineHighlightOn) return;
    m_lastLineStart = -1;
    m_highlightDataSet = false;
    applyLineHighlight();
    damage(FL_DAMAGE_ALL);
}

// ---- Cursor / status sync ------------------------------------------------

void Editor::notifyCursorMoved() {
    if (m_cursorCb) m_cursorCb();
    updateLineHighlight();
}

bool Editor::updateLineHighlight() {
    if (!buffer() || !m_styleBuf) return false;
    int pos = insert_position();
    int len = buffer()->length();
    if (pos < 0) pos = 0;
    if (pos > len) pos = len;
    int curLineStart = buffer()->line_start(pos);

    // URL underlines are independent of the line-highlight toggle:
    // keep them fresh even when the current-line highlight is off.
    if (!m_lineHighlightOn) {
        updateUrlHighlight();
        return false;
    }
    if (curLineStart == m_lastLineStart) return false;

    // Helper: rewrite one line, keeping search-match styles ('D') so
    // the find-bar highlight survives cursor moves.
    auto rewriteLine = [&](int start, int end, char style) {
        if (end <= start) return;
        std::string out(end - start, style);
        const char *st = m_styleBuf->text();
        if (st) {
            for (int i = 0; i < end - start; ++i)
                if (st[start + i] == 'D') out[i] = 'D';
            ::free((void *)st);
        }
        m_styleBuf->replace(start, end, out.c_str());
    };

    // Revert the previously highlighted line to style 'A' (keeping
    // match styles); URL styles there are restored by updateUrlHighlight
    // below - otherwise the underline would vanish when the cursor
    // leaves a URL's line and never come back.
    if (m_lastLineStart >= 0 && m_lastLineStart < len) {
        int oldEnd = buffer()->line_end(m_lastLineStart);
        if (oldEnd > len) oldEnd = len;
        rewriteLine(m_lastLineStart, oldEnd, 'A');
    }

    // Mark the new current line with style 'B' (keeping match styles).
    int lineEnd = buffer()->line_end(pos);
    if (lineEnd > len) lineEnd = len;
    m_lastLineStart = curLineStart;
    rewriteLine(curLineStart, lineEnd, 'B');

    // Re-scan URLs so lines the cursor just left keep their underlines.
    updateUrlHighlight();

    redraw();
    return true;
}

void Editor::syncLineHighlightBuffer() {
    if (!m_lineHighlightOn || !buffer()) return;
    m_lastLineStart = -1;
    applyLineHighlight();
}

// ---- Space symbols and long-line marker ----------------------------------

void Editor::drawSpaceSymbols() {
    if (!m_showSpaceSymbols || !buffer()) return;
    Fl_Text_Buffer *buf = buffer();
    int len = buf->length();
    if (len <= 0) return;

    fl_font(textfont(), textsize());
    int spaceW = (int)fl_width(' ');
    int lineH  = fl_height();

    // text_area 与 position_to_xy/xy_to_position 使用【父窗口坐标】
    // （taY = 控件在父窗口的 y() + TOP_MARGIN，含控件偏移）——
    // 坐标必须加 x()/y()，否则 last 只算到“控件高度”而漏掉
    // 控件偏移对应的底部几行（实测固定缺底部 4 行）。
    // 顶部边界：用 FLTK 内部维护的 mFirstChar（可视区首字符）作为 first。
    // 理由：xy_to_position(x(), y()) 的 X 与 handle_vline 内部 startX
    // (= text_area.x - mHorizOffset) 坐标系错位时，find_x 会把左边距
    // 宽度误判为“前 N 个字符”而返回 first≈3，恰好跳过行首空格点。
    // topfix-20260813: first uses mFirstChar
    int first = mFirstChar;
    int last  = xy_to_position(x() + w(), y() + h(), Fl_Text_Display::CURSOR_POS);
    if (first < 0) first = 0;
    if (last > len) last = len;
    if (first >= last) return;

    char *vis = buf->text_range(first, last);
    if (!vis) return;

    // 覆盖层绘制：显式重置 clip 到整个控件（窗口坐标），避免
    // 增量重绘的裁剪区域把底部行的点裁掉。
    fl_push_clip(x(), y(), w(), h());

    // 灰点(170)在浅色行高亮背景上几乎不可见（光标所在行）——
    // 用更深的灰，保证在白底/行高亮浅黄底上都清晰。
    fl_color(fl_rgb_color(120, 120, 120));

    // 用 fl_draw 画 '·'（中间点）而不是 fl_rectf 小矩形：
    // 实测在双缓冲（Fl_Image_Surface）下底部行的 fl_rectf 点不显示，
    // 而文本层（同机制 fl_draw）在底部行正常，故改用文本输出。
    static const char kDot[] = "\xC2\xB7";   // '·' UTF-8
    for (int i = 0; vis[i]; ++i) {
        if (vis[i] != ' ') continue;
        int px, py;
        if (!position_to_xy(first + i, &px, &py)) continue;
        // 5 参版 fl_draw：文本在空格区域 (px,py,spaceW,lineH) 内
        // 垂直水平居中，不依赖基线计算（原 3 参版基线位置偏高）。
        fl_draw(kDot, px, py, spaceW, lineH, FL_ALIGN_CENTER, 0, 0);
    }
    free(vis);
    fl_pop_clip();
}

void Editor::drawLongLineMarker() {
    if (m_longLineMarker <= 0) return;

    fl_font(textfont(), textsize());

    // 列位置按"当前字体的每字符平均宽"推算，不能一律用 'M'：
    //   等宽字体 —— 'M' 的宽度就是精确列宽，用它最准；
    //   比例字体（Noto Sans SC 这类）—— 'M' 比平均字符宽得多，用它推列会把
    //   标记往右偏（列号越大偏得越多），必须换成采样串的平均宽。
    // 结果按 (字体, 字号) 缓存：只有字体或字号变了才重算，避免每帧去 GDI 建字体。
    static Fl_Font s_markerFont = (Fl_Font)-1;
    static int     s_markerSize = 0;
    static int     s_markerCharW = 0;

    if (s_markerFont != textfont() || s_markerSize != textsize() || s_markerCharW <= 0) {
        int charW = (int)fl_width('M');
        int attr = 0;
        const char *face = Fl::get_font_name(textfont(), &attr);
        if (face && !FontUtils::isMonospace(face)) {
            static const char kSample[] =
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
                "0123456789 .,;:'\"()[]-";
            int avg = (int)(fl_width(kSample) / (double)(sizeof(kSample) - 1) + 0.5);
            if (avg > 0) charW = avg;
        }
        s_markerFont  = textfont();
        s_markerSize  = textsize();
        s_markerCharW = charW > 0 ? charW : 1;
    }
    int charW = s_markerCharW;

    int lineX = text_area.x
                + m_longLineMarker * charW
                - mHorizOffset;
    if (lineX < text_area.x ||
        lineX >= text_area.x + text_area.w) return;

    fl_color(m_theme ? m_theme->colors().background2 : fl_rgb_color(220, 220, 220));
    fl_yxline(lineX, text_area.y,
              text_area.y + text_area.h - 1);
}

// ---- Line-number gutter separator ----------------------------------------

// 行号区已经改成与编辑区同底色（background1），整条色带没了，边界全靠这条
// 1px 竖虚线。画在行号区最右一列（text_area.x 的前一列），纵向只覆盖文本区
// —— 不越过底部横向滚动条，跟长行标记同一纵向区间。
//
// 线色取 text2（次要/提示文字色，#8C8C8C / #737373）。三档实测：
//   background3 #F0F0F0 —— 太淡，几乎看不见（用户否掉）；
//   text1       #000000 —— 和正文同色，虚线读起来像"裁切线"，比行号数字还抢眼；
//   text2       #8C8C8C —— 清晰可辨又不抢正文，且天然跟随明暗主题。
// 定位是"参考线"不是"边框"，所以保留 3 亮 3 暗的虚线（实测占空比 50%）。
// 想更重/更轻只改 fl_color 那一行的取色键。
//
// 为什么不用 fl_line_style(FL_DASH)：实测 fl_yxline 走 GDI 的
// MoveToEx/LineTo，fl_line_style 建的虚线笔在这条路径上不生效
// （显式 dash 数组也一样），画出来仍是实线，只能手工分段。
//
// 想换回实线：把下面的 for 循环换成一行
//     fl_yxline(sx, text_area.y, text_area.y + text_area.h - 1);
//
// 必须是 Editor::draw() 里自绘：FLTK 的 Fl_Text_Display::draw_line_numbers()
// 里倒是有一行分隔线的代码，但它是被注释掉的，而且 FLTK 那版写死成
// x()+mLineNumWidth-1（忽略 box 缩进与左侧滚动条偏移），开着左侧面板时会错位。
void Editor::drawLineNumberSeparator() {
    if (linenumber_width() <= 0) return;      // 行号关闭时行号区宽度为 0
    int sx = text_area.x - 1;                  // 行号区最右一列像素
    if (sx < x()) return;
    fl_color(m_theme ? m_theme->colors().text2 : fl_rgb_color(140, 140, 140));
    int y1 = text_area.y + text_area.h - 1;
    for (int y = text_area.y; y <= y1; y += 6) {   // 3 亮 + 3 暗
        int yEnd = y + 2;
        if (yEnd > y1) yEnd = y1;
        fl_yxline(sx, y, yEnd);
    }
}

// ---- Visual display helpers ----------------------------------------------

void Editor::setShowSpaceSymbols(bool on) {
    if (on == m_showSpaceSymbols) return;
    m_showSpaceSymbols = on;
    damage(FL_DAMAGE_ALL);
    redraw();
}

void Editor::setLongLineMarker(int col) {
    if (col < 0) col = 0;
    if (col == m_longLineMarker) return;
    m_longLineMarker = col;
    damage(FL_DAMAGE_ALL);
    redraw();
}

void Editor::syncStyleFont() {
    if (m_styleBuf) {
        m_styleTable[0].font = textfont();
        m_styleTable[0].size = textsize();
        m_styleTable[1].font = textfont();
        m_styleTable[1].size = textsize();
        // Keep the URL style ('C') registered: highlight_data() resets
        // the whole style table, so forgetting it here kills underlines.
        m_styleTable[2].font = textfont();
        m_styleTable[2].size = textsize();
        m_styleTable[2].attr = Fl_Text_Display::ATTR_UNDERLINE;
        m_styleTable[3].font = textfont();
        m_styleTable[3].size = textsize();
        m_styleTable[3].attr = Fl_Text_Display::ATTR_BGCOLOR;
        m_styleTable[4].font = textfont();
        m_styleTable[4].size = textsize();
        m_styleTable[4].attr = Fl_Text_Display::ATTR_UNDERLINE | Fl_Text_Display::ATTR_BGCOLOR;
        m_styleTable[5].font = textfont();
        m_styleTable[5].size = textsize();
        m_styleTable[5].attr = Fl_Text_Display::ATTR_UNDERLINE | Fl_Text_Display::ATTR_BGCOLOR;
        m_styleTable[6].font = textfont();
        m_styleTable[6].size = textsize();
        m_styleTable[6].attr = Fl_Text_Display::ATTR_UNDERLINE;
        m_styleTable[7].font = textfont();
        m_styleTable[7].size = textsize();
        m_styleTable[7].attr = Fl_Text_Display::ATTR_UNDERLINE | Fl_Text_Display::ATTR_BGCOLOR;
        m_styleTable[8].font = textfont();
        m_styleTable[8].size = textsize();
        m_styleTable[8].attr = Fl_Text_Display::ATTR_UNDERLINE | Fl_Text_Display::ATTR_BGCOLOR;
        highlight_data(m_styleBuf, m_styleTable, 9, 'A', 0, 0);
    }
    damage(FL_DAMAGE_ALL);
    redraw();
}

void Editor::setFontFace(const char *name) {
    Fl_Font fid = FontUtils::fontNameToId(name);
    textfont(fid);
    // 行号字体必须跟着正文一起改：FLTK 构造函数里写死
    // `linenumber_font_ = FL_HELVETICA`（Windows 上落到 Arial），而 Pecia 从
    // 没设置过它，只设了 linenumber_size —— 于是"行号是 Arial、正文是
    // Consolas"，字号一样但字形明显不是一回事。
    // 顺带把字号也拉齐（setFontFace 可能在 textsize 之前/之后被调用）。
    linenumber_font(fid);
    linenumber_size(textsize());
    syncStyleFont();
}

// 垂直滚动比例 [0,1]：Markdown 预览滚动同步用（mVScrollBar 为 FLTK 1.4 的
// protected 成员，子类可直接访问）。
float Editor::verticalScrollRatio() const {
    if (!mVScrollBar) return 0.0f;
    float max = (float)mVScrollBar->maximum();
    if (max <= 0.0f) return 0.0f;
    float v = (float)mVScrollBar->value();
    if (v < 0.0f) v = 0.0f;
    if (v > max) v = max;
    return v / max;
}

// ---- Event handling ------------------------------------------------------

int Editor::handle(int event) {
#if defined(_WIN32)
    // Register with the shared IME hook (same implementation as every dialog
    // input and tool-window editor). The window exists before the editor can
    // receive focus, so attach() can install the hook here.
    if (event == FL_FOCUS || event == FL_KEYBOARD)
        ImeInPlace::attach(this);
    // Drop any in-progress composition when focus leaves the editor
    // (the IME may not always send WM_IME_ENDCOMPOSITION on focus change).
    if (event == FL_UNFOCUS) ImeInPlace::detach(this);
#endif

    // Hover over a URL: switch its style to the hover colour. While
    // Ctrl is held over a link, also show a hand cursor so the user
    // knows the click logic changed (Ctrl+Click opens the link).
    if (event == FL_MOVE) {
        checkUrlHover();
        // Cursor is applied AFTER the base-class handle() below, which
        // would otherwise reset it to the I-beam on every mouse move.
    }
    else if (event == FL_LEAVE) {
        if (m_overUrl) {
            m_overUrl = false;
            restoreUrlHoverStyles();
        }
        redraw();
    }

    // 右键：Edit 子菜单 + 内联 View 子菜单（FL_SUBMENU_POINTER 在 popup 中不可靠）
    if (event == FL_PUSH && Fl::event_button() == FL_RIGHT_MOUSE) {
        MainWindow *win = nullptr;
        for (Fl_Widget *w = window(); w; w = w->window()) {
            if ((win = dynamic_cast<MainWindow *>(w)) != nullptr) break;
        }
        if (win && win->menuBar()) {
            Fl_Menu_Item *edit = (Fl_Menu_Item *)win->menuBar()->find_item("Edit");
            if (!edit) edit = (Fl_Menu_Item *)win->menuBar()->find_item(I18n::get("menu.edit"));
            if (edit) {
                // Build a self-contained popup: only VISIBLE Edit items + View submenu.
                // Hidden items (Find/Replace/etc.) must NOT appear in the array —
                // FLTK's menuwindow allocates itemheight per array slot even for
                // FL_MENU_INVISIBLE items, causing empty lines.
                static Fl_Menu_Item popup[128];
                int n = 0;
                // Whitelist: classic edit items + Search Selection (no shortcut)
                auto is_visible_edit = [](const Fl_Menu_Item &it) -> bool {
                    if (!it.text) return false;
                    int s = it.shortcut();
                    if (s == 0) return true;  // keep zero-shortcut items (Search Selection, Send to AI, dividers)
                    return (s == (FL_COMMAND | 'z') || s == (FL_COMMAND | 'y') ||
                            s == (FL_COMMAND | 'x') || s == (FL_COMMAND | 'c') ||
                            s == (FL_COMMAND | 'v') || s == FL_Delete ||
                            s == (FL_COMMAND | 'a'));
                };
                // Copy only visible Edit items
                for (Fl_Menu_Item *it = edit + 1; it->text; it++) {
                    if (is_visible_edit(*it)) popup[n++] = *it;
                }
                // Add divider on last item (instead of empty divider item
                // which FLTK renders as a full-height empty row)
                if (n > 0) popup[n-1].flags |= FL_MENU_DIVIDER;
                // View submenu: built from the LIVE View menu (all items
                // before Font), so a new toggle only needs a MenuTable entry.
                n += win->buildViewContextMenu(
                    popup + n, (int)(sizeof(popup) / sizeof(popup[0])) - n);
                popup[n++] = {nullptr};  // end of popup
                // Set popup checkbox fill color (theme highlight2)
                g_popupCheckColor = win->m_theme.colors().highlight2;
                const Fl_Menu_Item *hit = popup->popup(
                    Fl::event_x(), Fl::event_y(), nullptr, nullptr, win->menuBar());
                g_popupCheckColor = 0;  // reset
                if (hit && hit->callback_) hit->do_callback(win->menuBar());
            }
        }
        return 1;
    }

    // Ctrl+Click on a URL: extract and open in browser.
    if (event == FL_PUSH && (Fl::event_state() & FL_CTRL)) {
        if (m_settings && !m_settings->getDetectUrls())
            return Fl_Text_Editor::handle(event);
        int pos = (mMaxsize > 0)
                  ? xy_to_position(Fl::event_x(), Fl::event_y())
                  : -1;
        int urlStart, urlEnd;
        if (pos >= 0 && urlRangeAt(pos, urlStart, urlEnd)) {
            char *url = buffer()->text_range(urlStart, urlEnd);
            if (url && *url) {
                // Protocol whitelist: only http:// and https:// are allowed
                bool safeProto = (_strnicmp(url, "https://", 8) == 0 ||
                                  _strnicmp(url, "http://", 7) == 0);
                if (safeProto) {
#if defined(_WIN32)
                int wlen = MultiByteToWideChar(CP_UTF8, 0, url, -1, nullptr, 0);
                if (wlen > 0) {
                    wchar_t *wurl = new wchar_t[wlen];
                    MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, wlen);
                    ShellExecuteW(nullptr, L"open", wurl, nullptr, nullptr, SW_SHOWNORMAL);
                    delete[] wurl;
                }
#endif
                }
            }
            ::free((void*)url);
            return 1;
        }
    }

    if (event == FL_KEYBOARD) {
        int key = Fl::event_key();
        unsigned state = Fl::event_state() & (FL_SHIFT | FL_CTRL | FL_ALT | FL_META);

        // Read-only: block any key that would modify text.
        if (m_doc && m_doc->isReadOnly()) {
            bool allow = false;
            if (key == FL_Left || key == FL_Right || key == FL_Up || key == FL_Down ||
                key == FL_Home || key == FL_End || key == FL_Page_Up || key == FL_Page_Down ||
                key == FL_Enter || key == FL_KP_Enter) allow = true;
            else if (key == 'c' && (state & FL_CTRL)) allow = true;
            else if (key == 'a' && (state & FL_CTRL)) allow = true;
            // Viewing shortcuts: open the find/replace/go-to bars (all
            // non-destructive - replacements themselves are blocked on
            // read-only documents inside FindReplace).
            else if (key == 'f' && (state & FL_CTRL)) allow = true;
            else if (key == 'h' && (state & FL_CTRL)) allow = true;
            else if (key == 'g' && (state & FL_CTRL)) allow = true;
            else if (key == FL_F + 3) allow = true;
            if (!allow) {
                Fl::event_is_click(0);
                return 1;
            }
        }

        // Tab ??insert N spaces
        if (key == FL_Tab && state == 0) {
            int n = m_settings ? m_settings->getTabWidth() : 4;
            if (n < 1) n = 1;
            if (n > 15) n = 15;
            char spaces[16];
            for (int i = 0; i < n; ++i) spaces[i] = ' ';
            spaces[n] = 0;
            Fl_Text_Buffer *buf = buffer();
            int selStart, selEnd;
            if (buf->selection_position(&selStart, &selEnd)) {
                buf->replace(selStart, selEnd, spaces);
                insert_position(selStart + n);
            } else {
                int pos = insert_position();
                buf->insert(pos, spaces);
                insert_position(pos + n);
            }
            show_insert_position();
            set_changed();
            if (when() & FL_WHEN_CHANGED) do_callback(FL_REASON_CHANGED);
            notifyCursorMoved();
            return 1;
        }

        // Shift+Tab ??remove up to N spaces
        if (key == FL_Tab && state == FL_SHIFT) {
            int n = m_settings ? m_settings->getTabWidth() : 4;
            if (n < 1) n = 1;
            if (n > 15) n = 15;
            Fl_Text_Buffer *buf = buffer();

            setShiftTab(true);

            for (int i = 0; i < n; ++i) {
                int pos = insert_position();
                if (pos <= 0) break;
                int lineStart = buf->line_start(pos);
                if (pos - 1 < lineStart) break;
                if (buf->byte_at(pos - 1) != ' ') break;
                buf->remove(pos - 1, pos);
                insert_position(pos - 1);
            }

            setShiftTab(false);
            show_insert_position();
            set_changed();
            if (when() & FL_WHEN_CHANGED) do_callback(FL_REASON_CHANGED);
            notifyCursorMoved();
            return 1;
        }

        // Enter ??auto-indent
        if ((key == FL_Enter || key == FL_KP_Enter) && state == 0) {
            bool autoIndent = m_settings ? m_settings->getAutoIndent() : true;
            if (autoIndent) {
                Fl_Text_Buffer *buf = buffer();
                int pos = insert_position();
                int lineStart = buf->line_start(pos);
                int lineEnd = buf->line_end(pos);
                char *lineText = buf->text_range(lineStart, lineEnd);
                std::string lead;
                if (lineText) {
                    for (int i = 0; lineText[i]; ++i) {
                        char c = lineText[i];
                        if (c == ' ' || c == '\t') lead.push_back(c);
                        else break;
                    }
                    free(lineText);
                }
                int selStart, selEnd;
                if (buf->selection_position(&selStart, &selEnd)) {
                    buf->remove(selStart, selEnd);
                    pos = selStart;
                }
                std::string ins = std::string("\n") + lead;
                buf->insert(pos, ins.c_str());
                insert_position(pos + (int)ins.size());
                show_insert_position();
                set_changed();
                if (when() & FL_WHEN_CHANGED) do_callback(FL_REASON_CHANGED);
                notifyCursorMoved();
                // 强制全量重绘：增量重绘可能不覆盖空格符号/其他覆盖层
                damage(FL_DAMAGE_ALL);
                redraw();
                return 1;
            }
        }
    }

    int oldPos = insert_position();
    int ret = Fl_Text_Editor::handle(event);
    if (insert_position() != oldPos) {
        notifyCursorMoved();
    }
    // After a scroll the viewport moved but the mouse did not: refresh
    // the URL hover state so no stale hover colour remains.
    if (event == FL_MOUSEWHEEL) checkUrlHover();
    return ret;
}

// ---- Draw overrides ------------------------------------------------------

void Editor::draw() {
    // IME 组合期间临时关掉 FLTK 自己的光标：缓冲区里还没有组合串，
    // position_to_xy(insert_position()) 算出来的是组合起点而不是串内插入点，
    // 让它画就会和下面自绘的组合光标同时出现（双光标）。常规情况一律交给
    // FLTK 画（cursor_style = SIMPLE_CURSOR：2px、无端帽）。
    const int savedCursorOn = mCursorOn;
    if (m_compActive) mCursorOn = 0;
    Fl_Text_Editor::draw();
    mCursorOn = savedCursorOn;

    // 行号区右缘的 1px 分隔线。必须画在下面的 fl_push_clip(text_area...)
    // 之前——那条 clip 把左侧行号区排除在外，线会被裁掉。
    drawLineNumberSeparator();

    // Overlay clipping: FLTK clips the text itself to text_area, but pops
    // that clip before returning. Every overlay we paint on top (current
    // line strip, caret, space dots, long-line marker) is therefore
    // unconstrained and, once the view is scrolled horizontally, leaks to
    // the LEFT over the line-number gutter (position_to_xy() returns a
    // px smaller than text_area.x, even negative) and to the RIGHT over
    // the vertical scrollbar. Clip the whole overlay pass horizontally to
    // text_area; vertically keep the full widget height (same as the IME
    // overlay below) so partially visible bottom rows are not cut off.
    fl_push_clip(text_area.x, y(), text_area.w, h());

    // Current-line highlight, two layers:
    //  1. Character background via the style buffer ('B' style, set by
    //     applyLineHighlight) - FLTK draws it natively, overlapping the
    //     text-selection highlight naturally.
    //  2. The line's trailing whitespace to the right edge: the style
    //     buffer cannot color empty space, so fill that strip here with
    //     a thin rect. It does NOT cover the characters themselves, so
    //     selection colors on this line still win.
    if (m_lineHighlightOn && buffer() && m_styleBuf) {
        int pos = insert_position();
        int blen = buffer()->length();
        if (pos < 0) pos = 0;
        if (pos > blen) pos = blen;
            int lineEnd   = buffer()->line_end(pos);
            int endX, endY;
            if (position_to_xy(lineEnd, &endX, &endY)) {
                fl_font(textfont(), textsize());
                int lineH = fl_height();
                Fl_Color hlColor = m_theme ? m_theme->colors().highlight1
                                           : fl_rgb_color(255, 255, 191);
            // Rect covers from the end of the line text to the widget's
            // RIGHT EDGE in window coordinates: x()+w(), NOT w() alone
            // (w() misses the widget's x offset; the main window editor
            // sits at x()=0 so it never noticed, but the tool editors
            // start at x()=PAD and showed a gap).
            int hlLeft = endX;
            int hlRight = x() + w();
            if (mVScrollBar && mVScrollBar->visible() &&
                mVScrollBar->x() > hlLeft)
                hlRight = mVScrollBar->x();
            if (hlRight > hlLeft) {
                fl_color(hlColor);
                fl_rectf(hlLeft, endY, hlRight - hlLeft, lineH);
            }
        }
    }

    // 光标不再自绘。
    //
    // 历史：早先这里先用底色擦掉 5px 宽的整条带、再画一条 3px 自己的光标条，
    // 结果把光标左右各 1px 的字符也抹了（"光标遮挡文字"），而且 FLTK 原生光标
    // （HEAVY_CURSOR = 3px 竖线 + 上下 5px 端帽）本来就得靠这段擦除盖掉 ——
    // 只要 FLTK 换了光标样式、或者缩放/DPI 让几何对不上，就会漏出一条残影，
    // 表现就是"双光标"。
    // 现在改成**完全交给 FLTK**：构造函数里 cursor_style(SIMPLE_CURSOR)，
    // FLTK 自己画 2px、无端帽，不需要任何擦除逻辑（擦除正是所有残影的来源）。
    // 闪烁由 MainWindow 的定时器调 show_cursor() 驱动，show_cursor 走
    // redisplay_range() → damage(FL_DAMAGE_SCROLL)，正好落在 FLTK 画光标的
    // damage 条件里，所以开/关都能正常重绘。
    // 唯一的例外是 IME 组合期间（见下面）：那时缓冲区里还没有组合串，
    // FLTK 按缓冲区算出的位置不在真正的插入点上，必须由我们自己画。

#if defined(_WIN32)
    // In-place IME composition overlay. The IME's own composition window is
    // hidden by the shared window hook (ui/ImeInPlace.cpp), so the string and
    // its caret are painted here - by the same drawComposition() used by every
    // dialog input and tool-window editor.
    if (m_compActive && !m_compText.empty() && m_compStart >= 0 && buffer()) {
        int cx, cy;
        if (position_to_xy(m_compStart, &cx, &cy)) {
            fl_font(textfont(), textsize());
            const int lineH = fl_height();
            const bool caretVisible = mCursorOn && Fl::focus() == this;
            ImeInPlace::drawComposition(
                m_compText.c_str(), m_compCursorBytes, cx, cy, lineH,
                m_theme ? m_theme->colors().text1 : FL_FOREGROUND_COLOR,
                cursor_color(), caretVisible, textfont(), textsize(), window(),
                text_area.x, y(), text_area.w, h());
        }
    }
#endif
    // Overlay space symbols
    drawSpaceSymbols();

    // 覆盖层：长行标记
    drawLongLineMarker();

    // ★ 光标重画（必须留在所有覆盖层之后）。
    //
    // FLTK 的光标是在 Fl_Text_Editor::draw() 内部画的，而本函数后面还要画
    // 「当前行底色延伸到右缘」那块 rect —— 它从**行尾**铺到窗口右缘。光标停在
    // 行尾（打字时的常态）时，正好被这块底色整个盖住，表现就是"看不到光标"。
    // 实测：点行尾后 10 帧累计差分为空（行尾 x=376 附近 0 列变化），而光标停在
    // 行中时 x=244/245 两列正常闪烁 —— 说明不是不画，是被盖住了。
    //
    // 这里**不自己画几何**，直接复用 FLTK 的 draw_cursor()：位置取自
    // position_to_xy()，宽度/形状/端帽完全由 cursor_style(SIMPLE_CURSOR) 决定，
    // 与 FLTK 原生那一次绘制逐像素一致，所以不存在"两套几何对不上"的残影
    // （双光标的根因是擦除和绘制几何不一致，这里既没擦除也没另画一套）。
    if (!m_compActive && mCursorOn && buffer() && Fl::focus() == this) {
        int selStart = 0, selEnd = 0;
        const bool hasSel = buffer()->selection_position(&selStart, &selEnd);
        const int cur = insert_position();
        // 与 FLTK 自身的条件一致：光标落在选区内时它本来就不画。
        if (!hasSel || cur < selStart || cur > selEnd) {
            int curX = 0, curY = 0;
            if (position_to_xy(cur, &curX, &curY)) draw_cursor(curX, curY);
        }
    }

    fl_pop_clip();

    // Reposition horizontal scrollbar to span the editor's full width.
    //
    // ★ 坐标是**窗口绝对坐标**（Fl_Widget::x() 就是窗口坐标，见 FL/Fl_Widget.H
    //   "the x position relative to the window"）。这里曾经写死 resize(0, ...)：
    //   编辑器还在窗口最左时看不出问题，加了左侧面板之后整条水平滚动条被搬到
    //   窗口左缘（横跨侧栏），编辑器右下角则露出 FLTK 默认绘制的深色槽
    //   （FL_DARK3 #959595），滑块也跟着偏了一个面板宽度 —— 点击位置和画出来的
    //   滑块对不上。改成 x() 后与竖直滚动条（本来就用绝对坐标）对齐。
    if (mHScrollBar && mHScrollBar->visible()) {
        int vscroll_w = (mVScrollBar && mVScrollBar->visible()) ? mVScrollBar->w() : 0;
        int newW = w() - vscroll_w;
        if (mHScrollBar->x() != x() || mHScrollBar->w() != newW) {
            mHScrollBar->resize(x(), mHScrollBar->y(), newW, mHScrollBar->h());
            mHScrollBar->damage(FL_DAMAGE_ALL);
            draw_child(*mHScrollBar);
        }
    }

    // Custom scrollbars on top of FLTK defaults
    drawCustomScrollbar(mVScrollBar, false);
    drawCustomScrollbar(mHScrollBar, true);

    // Corner square where both scrollbars meet
    if (mVScrollBar && mVScrollBar->visible() &&
        mHScrollBar && mHScrollBar->visible()) {
        fl_color(m_theme ? m_theme->colors().background1 : fl_rgb_color(240, 240, 240));
        fl_rectf(mVScrollBar->x(), mHScrollBar->y(),
                 mVScrollBar->w(), mHScrollBar->h());
    }
}



