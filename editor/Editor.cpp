// Editor.cpp - text editor widget implementation
#include "Editor.h"
#include "Document.h"
#include "core/SettingsProvider.h"
#include "core/Theme.h"
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

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>  // ShellExecuteW
#include <imm.h>       // ImmGetContext / ImmGetCompositionStringW
#include <FL/platform.H>  // fl_xid
#include <vector>
#endif

// ---- FontUtils implementation -------------------------------------------

namespace FontUtils {

int strCaseCmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return ca - cb;
        ++a; ++b;
    }
    return *a - *b;
}

Fl_Font fontNameToId(const char *name) {
    if (!name || !name[0]) return FL_COURIER;
    int num = Fl::set_fonts();
    for (int i = 0; i < num; ++i) {
        int attr = 0;
        const char *fn = Fl::get_font_name(i, &attr);
        if (fn && strCaseCmp(fn, name) == 0) {
            return i;
        }
    }
    // Common fallback mapping
    if (strCaseCmp(name, "Courier New") == 0 || strCaseCmp(name, "Courier") == 0)
        return FL_COURIER;
    if (strCaseCmp(name, "Helvetica") == 0)
        return FL_HELVETICA;
    if (strCaseCmp(name, "Times") == 0 || strCaseCmp(name, "Times New Roman") == 0)
        return FL_TIMES;
    return FL_COURIER;
}

// 主要文字系统各一个代表字体（其余文字由 Windows 字体回退/链接机制兜底）：
//   拉丁（英西法德葡意等数十种语言共用）+ 西里尔（俄语等）+ 希腊 → Consolas
//   中文 → SimHei（黑体）/ NSimSun（宋体）
//   日文 → MS Gothic（等宽）
//   韩文 → Gulim（等宽）
//   阿拉伯/希伯来等 → Segoe UI
//   天城文系（印地/孟加拉等）→ Nirmala UI
//   泰文 → Leelawadee UI
const char *FONT_LIST[] = {
    "Consolas",
    "SimHei",
    "NSimSun",
    "MS Gothic",
    "Gulim",
    "Segoe UI",
    "Nirmala UI",
    "Leelawadee UI",
    nullptr
};
const int FONT_COUNT = (int)(sizeof(FONT_LIST) / sizeof(FONT_LIST[0])) - 1;

} // namespace FontUtils

#if defined(_WIN32)
// ---- Windows IME in-place composition -----------------------------------

// FLTK does not expose WM_IME_* messages, so we subclass the top window's
// window procedure and route composition messages to the focused Editor.
// When we consume WM_IME_COMPOSITION (return 1), the IME treats the editor
// as doing in-place rendering and hides its floating composition window
// (which is what draws the second caret at the text cursor).

static std::string utf16ToUtf8(const wchar_t *s) {
    std::string out;
    if (!s || !*s) return out;
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return out;
    out.resize(n - 1);
    WideCharToMultiByte(CP_UTF8, 0, s, -1, &out[0], n, nullptr, nullptr);
    return out;
}

// Convert a UTF-16 code-unit offset (as returned by GCS_CURSORPOS) into
// a UTF-8 byte offset into the same string.
static int utf16OffsetToUtf8Offset(const wchar_t *s, int utf16Units) {
    int units = 0, bytes = 0;
    for (const wchar_t *p = s; *p && units < utf16Units; ++p) {
        int u = ((*p & 0xFC00) == 0xD800 && (p[1] & 0xFC00) == 0xDC00) ? 2 : 1;
        if (units + u > utf16Units) break;
        unsigned int cp = (u == 2)
            ? (0x10000 + ((*p - 0xD800) << 10) + (p[1] - 0xDC00))
            : (unsigned int)*p;
        bytes += (cp < 0x80) ? 1 : (cp < 0x800) ? 2 : (cp < 0x10000) ? 3 : 4;
        units += u;
        if (u == 2) ++p;
    }
    return bytes;
}

struct ImeWindowHook {
    HWND hwnd;
    WNDPROC orig;
};
static std::vector<ImeWindowHook> g_imeHooks;

static LRESULT CALLBACK imeWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_IME_STARTCOMPOSITION || msg == WM_IME_COMPOSITION || msg == WM_IME_ENDCOMPOSITION) {
        Editor *ed = dynamic_cast<Editor *>(Fl::focus());
        if (ed && ed->window() && fl_xid(ed->window()) == hwnd) {
            if (msg == WM_IME_STARTCOMPOSITION) {
                ed->imeCompositionStart();
                return 0;
            }
            if (msg == WM_IME_ENDCOMPOSITION) {
                ed->imeCompositionEnd();
                return 0;
            }
            // WM_IME_COMPOSITION
            HIMC himc = ImmGetContext(hwnd);
            bool handled = false;
            if (himc) {
                if (lParam & GCS_COMPSTR) {
                    LONG n = ImmGetCompositionStringW(himc, GCS_COMPSTR, nullptr, 0);
                    if (n > 0) {
                        std::wstring w(n / 2, L'\0');
                        ImmGetCompositionStringW(himc, GCS_COMPSTR, &w[0], n);
                        LONG cp = 0;
                        if (lParam & GCS_CURSORPOS)
                            ImmGetCompositionStringW(himc, GCS_CURSORPOS, &cp, sizeof(cp));
                        ed->imeCompositionUpdate(w.c_str(), (int)cp);
                        handled = true;
                    }
                }
                if (lParam & GCS_RESULTSTR) {
                    LONG n = ImmGetCompositionStringW(himc, GCS_RESULTSTR, nullptr, 0);
                    if (n > 0) {
                        std::wstring w(n / 2, L'\0');
                        ImmGetCompositionStringW(himc, GCS_RESULTSTR, &w[0], n);
                        ed->imeCompositionCommit(w.c_str());
                        handled = true;
                    }
                }
                ImmReleaseContext(hwnd, himc);
            }
            return handled ? 1 : 0;
        }
    }
    for (auto &h : g_imeHooks) {
        if (h.hwnd == hwnd) return CallWindowProcW(h.orig, hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ensureImeHook(HWND hwnd) {
    if (!hwnd) return;
    for (auto &h : g_imeHooks) if (h.hwnd == hwnd) return;
    ImeWindowHook h;
    h.hwnd = hwnd;
    h.orig = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
    g_imeHooks.push_back(h);
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)&imeWindowProc);
}

// ---- IME composition handling -------------------------------------------

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

bool Editor::imeCompositionUpdate(const wchar_t *comp, int cursorUtf16) {
    if (!buffer()) return false;
    if (m_doc && m_doc->isReadOnly()) return false;
    if (!m_compActive) imeCompositionStart();
    if (!m_compActive) return false;

    m_compText = utf16ToUtf8(comp);
    m_compCursorBytes = utf16OffsetToUtf8Offset(comp, cursorUtf16);

    // Park the caret at the composition cursor (clamped to the buffer end
    // when composing at the very end of the document).
    int caret = m_compStart + m_compCursorBytes;
    if (caret > buffer()->length()) caret = buffer()->length();
    insert_position(caret);
    show_insert_position();
    notifyCursorMoved();
    redraw();
    return true;
}

bool Editor::imeCompositionCommit(const wchar_t *result) {
    if (!buffer()) return false;
    if (m_doc && m_doc->isReadOnly()) return false;
    if (!m_compActive) {
        m_compStart = insert_position();
        m_compEnd = m_compStart;
    }
    std::string s = utf16ToUtf8(result);
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
    return true;
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

#endif // _WIN32


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
    // Thick I-beam cursor (3px wide vertical bar with top/bottom caps)
    // so the caret is more visible than the default 1px NORMAL_CURSOR.
    cursor_style(Fl_Text_Display::HEAVY_CURSOR);
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
    if (m_styleBuf) {
        highlight_data(nullptr, nullptr, 0, 'A', 0, 0);
        delete m_styleBuf;
    }
}

// ---- Scrollbar drawing ---------------------------------------------------

void Editor::drawCustomScrollbar(Fl_Scrollbar *sb, bool horizontal) {
    if (!sb || !sb->visible_r()) return;

    int sx = sb->x();
    int sy = sb->y();
    int sw = sb->w();
    int sh = sb->h();

    // For horizontal scrollbar, use full widget width
    if (horizontal) {
        sx = 0;
        int vscroll_w = (mVScrollBar && mVScrollBar->visible()) ? mVScrollBar->w() : 0;
        sw = w() - vscroll_w;
    }

    double minv = sb->minimum();
    double maxv = sb->maximum();
    double val  = sb->value();
    float  sl   = sb->slider_size();

    Fl_Color trackColor = m_theme ? m_theme->colors().scrollbarTrack : fl_rgb_color(240, 240, 240);
    Fl_Color thumbColor = m_theme ? m_theme->colors().scrollbarThumb : fl_rgb_color(180, 180, 180);

    // Track
    fl_color(trackColor);
    fl_rectf(sx, sy, sw, sh);

    // Thumb
    if (horizontal) {
        int thumb_w = (int)(sw * sl);
        if (thumb_w < 20) thumb_w = 20;
        double range = maxv - minv;
        int thumb_x = sx;
        if (range > 0.0)
            thumb_x = sx + (int)((val - minv) * (sw - thumb_w) / range);
        fl_color(thumbColor);
        fl_rectf(thumb_x, sy, thumb_w, sh);
    } else {
        int thumb_h = (int)(sh * sl);
        if (thumb_h < 20) thumb_h = 20;
        double range = maxv - minv;
        int thumb_y = sy;
        if (range > 0.0)
            thumb_y = sy + (int)((val - minv) * (sh - thumb_h) / range);
        fl_color(thumbColor);
        fl_rectf(sx, thumb_y, sw, thumb_h);
    }
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
    m_styleTable[1].bgcolor = m_theme ? m_theme->colors().lineHighlight : fl_rgb_color(220, 232, 245);  // light blue

    // URL style: underline only (style 'C')
    m_styleTable[2].color   = textColor();
    m_styleTable[2].font    = textfont();
    m_styleTable[2].size    = textsize();
    m_styleTable[2].attr    = Fl_Text_Display::ATTR_UNDERLINE;
    m_styleTable[2].bgcolor = bgColor();

    // Search-match style (style 'D'): yellow background, no underline.
    Fl_Color searchBg = m_theme ? m_theme->colors().searchHighlight
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
    m_styleTable[5].bgcolor = searchBg;

    // URL hover variants (styles 'G'/'H'/'I'): underline + linkHover colour.
    Fl_Color linkHover = m_theme ? m_theme->colors().linkHover
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
        m_styleTable[6].color   = m_theme ? m_theme->colors().linkHover
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
    m_styleTable[0].color   = textColor();
    m_styleTable[0].font    = textfont();
    m_styleTable[0].size    = textsize();
    m_styleTable[0].attr    = 0;
    m_styleTable[0].bgcolor = bgColor();
    m_styleTable[1].color   = textColor();
    m_styleTable[1].font    = textfont();
    m_styleTable[1].size    = textsize();
    m_styleTable[1].attr    = Fl_Text_Display::ATTR_BGCOLOR;
    m_styleTable[1].bgcolor = m_theme ? m_theme->colors().lineHighlight : fl_rgb_color(220, 232, 245);
    // URL style (style 'C'): underline only. Must be registered here or
    // FLTK renders 'C' with the default style (no underline).
    m_styleTable[2].color   = textColor();
    m_styleTable[2].font    = textfont();
    m_styleTable[2].size    = textsize();
    m_styleTable[2].attr    = Fl_Text_Display::ATTR_UNDERLINE;
    m_styleTable[2].bgcolor = bgColor();
    Fl_Color searchBg = m_theme ? m_theme->colors().searchHighlight
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
    // URL hover variants (styles 'G'/'H'/'I')
    Fl_Color linkHover = m_theme ? m_theme->colors().linkHover
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
    int charW = (int)fl_width('M');
    int lineX = text_area.x
                + m_longLineMarker * charW
                - mHorizOffset;
    if (lineX < text_area.x ||
        lineX >= text_area.x + text_area.w) return;

    fl_color(m_theme ? m_theme->colors().scrollbarThumb : fl_rgb_color(220, 220, 220));
    fl_yxline(lineX, text_area.y,
              text_area.y + text_area.h - 1);
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
    // Install the IME window-proc hook once the window exists (the HWND is
    // created before the editor can receive focus or keyboard input).
    if (event == FL_FOCUS || event == FL_KEYBOARD) {
        Fl_Window *win = window();
        if (win) ensureImeHook(fl_xid(win));
    }
    // Drop any in-progress composition when focus leaves the editor
    // (the IME may not always send WM_IME_ENDCOMPOSITION on focus change).
    if (event == FL_UNFOCUS) imeCompositionEnd();
#endif

    // Hover over a URL: switch its style to the hover colour (the old
    // hand-cursor feedback was removed - it conflicted with the editor's
    // text-editing semantics and was barely visible).
    if (event == FL_MOVE) {
        checkUrlHover();
    }
    else if (event == FL_LEAVE && m_overUrl) {
        m_overUrl = false;
        restoreUrlHoverStyles();
        redraw();
    }

    // 右键：直接复用主菜单的 Edit 子菜单（不复制数组——复制版曾崩溃）；
    // 筛选：临时给查找类项加 FL_MENU_INVISIBLE（popup 后恢复）
    if (event == FL_PUSH && Fl::event_button() == FL_RIGHT_MOUSE) {
        MainWindow *win = nullptr;
        for (Fl_Widget *w = window(); w; w = w->window()) {
            if ((win = dynamic_cast<MainWindow *>(w)) != nullptr) break;
        }
        if (win && win->menuBar()) {
            // 白名单：经典编辑 7 项 + 无快捷键项（Search Selection），其余（查找类）隐藏
            auto is_hidden_item = [](const Fl_Menu_Item &it) -> bool {
                int s = it.shortcut();
                if (s == 0) return false;   // Search Selection（无快捷键）保留
                return !(s == (FL_COMMAND | 'z') || s == (FL_COMMAND | 'y') ||
                         s == (FL_COMMAND | 'x') || s == (FL_COMMAND | 'c') ||
                         s == (FL_COMMAND | 'v') || s == FL_Delete ||
                         s == (FL_COMMAND | 'a'));
            };
            const Fl_Menu_Item *m = nullptr;
            // find_item 正确处理扁平数组层级（遍历会撞上子菜单终止符 {nullptr} 提前停止）
            Fl_Menu_Item *edit = (Fl_Menu_Item *)win->menuBar()->find_item("Edit");
            if (!edit) edit = (Fl_Menu_Item *)win->menuBar()->find_item(I18n::get("menu.edit"));
            if (edit) m = edit + 1;   // Edit 子菜单首项（Undo）
            if (m) {
                std::vector<Fl_Menu_Item *> hidden;
                for (Fl_Menu_Item *it = const_cast<Fl_Menu_Item *>(m);
                     it && it->text; it++) {
                    if (is_hidden_item(*it)) {
                        it->flags |= FL_MENU_INVISIBLE;
                        hidden.push_back(it);
                    }
                }
                const Fl_Menu_Item *hit = m->popup(
                    Fl::event_x(), Fl::event_y(), nullptr, nullptr, win->menuBar());
                for (auto *it : hidden) it->flags &= ~FL_MENU_INVISIBLE;
                if (hit && hit->callback_) hit->do_callback(this);
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
    Fl_Text_Editor::draw();

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
                Fl_Color hlColor = m_theme ? m_theme->colors().lineHighlight
                                           : fl_rgb_color(220, 232, 245);
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

    // Cursor: a plain 3px vertical bar with no caps. FLTK's default
    // cursor has top/bottom caps; wipe it and draw ours on top.
    {
        int curX, curY;
        if (mCursorOn && Fl::focus() == this &&
            position_to_xy(insert_position(), &curX, &curY)) {
            fl_font(textfont(), textsize());
            int barH = mMaxsize;
            fl_color(bgColor());
            fl_rectf(curX - 2, curY, 5, barH);
            fl_color(cursor_color());
            fl_rectf(curX - 1, curY, 3, barH);
        }
    }

#if defined(_WIN32)
    // (IME composition anchoring is handled solely by FLTK's draw_cursor()
    // which calls fl_set_spot() every frame at the caret. Re-anchoring here
    // in addition caused the IME window to be relocated multiple times per
    // frame to different positions, producing visible flicker while typing
    // pinyin.)
#endif
    // Overlay space symbols
    drawSpaceSymbols();

    // 覆盖层：长行标记
    drawLongLineMarker();

    // Reposition horizontal scrollbar to span full widget width
    if (mHScrollBar && mHScrollBar->visible()) {
        int vscroll_w = (mVScrollBar && mVScrollBar->visible()) ? mVScrollBar->w() : 0;
        int newW = w() - vscroll_w;
        if (mHScrollBar->x() != 0 || mHScrollBar->w() != newW) {
            mHScrollBar->resize(0, mHScrollBar->y(), newW, mHScrollBar->h());
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
        fl_color(m_theme ? m_theme->colors().scrollbarThumb : fl_rgb_color(240, 240, 240));
        fl_rectf(mVScrollBar->x(), mHScrollBar->y(),
                 mVScrollBar->w(), mHScrollBar->h());
    }
}



