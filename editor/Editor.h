// Editor.h - Text editor widget with line highlight, URL detection, custom scrollbars
#pragma once

#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Text_Buffer.H>
#include <functional>
#include <string>
#include <utility>
#include <vector>

class Document;
class SettingsProvider;
#include "core/Theme.h"
class Fl_Scrollbar;

// Font utilities shared between Editor and MainWindow
namespace FontUtils {
    // Map a Windows font name to an FLTK Fl_Font id.
    // Falls back to FL_COURIER if the font is not found.
    Fl_Font fontNameToId(const char *name);

    // Case-insensitive ASCII string compare (portable, no platform deps).
    int strCaseCmp(const char *a, const char *b);

    // Editor font choices for the View > Font submenu.
    extern const char *FONT_LIST[];
    extern const int FONT_COUNT;
}

// Custom text editor subclass.
//
// Features:
//   - Custom-style scrollbars (10px, track/thumb flat colors, no arrows)
//   - Horizontal scrollbar spans full widget width (not just text area)
//   - Current-line highlight via FLTK style buffer (same mechanism as
//     search match highlight, light blue background)
//   - URL detection and highlight (underline style)
//   - Space-symbol dots
//   - Long-line marker (vertical guide line)
//   - Tab-to-spaces conversion, Shift+Tab outdent, auto-indent
//
// Line highlight uses highlight_data() / style buffer, the same FLTK
// mechanism used by FindReplace for match highlighting. This gives us
// proper text-on-colored-background rendering without cursor glitches.
// If FindReplace also activates its style buffer, it simply overrides
// the line highlight (search takes visual priority).
class Editor : public Fl_Text_Editor {
public:
    // Callback invoked when cursor moves (for status bar updates, etc.)
    using CursorCallback = std::function<void()>;

    Editor(int X, int Y, int W, int H, SettingsProvider *settings = nullptr);
    ~Editor();

    void setDoc(Document *d) { m_doc = d; }
    Document *doc() const { return m_doc; }
    void setShiftTab(bool on) { m_inShiftTab = on; }
    bool isShiftTab() const { return m_inShiftTab; }

    // Enable or disable current-line highlight.
    void setLineHighlight(bool on);
    bool lineHighlight() const { return m_lineHighlightOn; }

    // Refresh URL highlights in the style buffer (called from cbModify).
    void refreshUrlHighlight();

    // Enable or disable space-symbol dots.
    void setShowSpaceSymbols(bool on);
    bool showSpaceSymbols() const { return m_showSpaceSymbols; }

    // Set the long-line marker column (0 = off, >0 = draw vertical line
    // at that character column). Call redraw() to refresh.
    void setLongLineMarker(int col);

    // Sync the style table font/size fields with the current widget
    // textfont/textsize, then re-register highlight_data() so FLTK
    // picks up the new values.
    void syncStyleFont();

    // Set the editor font face by name.
    void setFontFace(const char *name);

    // Vertical scroll ratio in [0,1] (Markdown preview scroll sync).
    float verticalScrollRatio() const;

#if defined(_WIN32)
    // IME in-place composition (Windows only). The input method's
    // composition string is rendered by us inside the editor (underlined,
    // caret at GCS_CURSORPOS) so the IME hides its floating composition
    // window (which draws a second caret at the text cursor).
    void imeCompositionStart();
    bool imeCompositionUpdate(const wchar_t *comp, int cursorUtf16);
    bool imeCompositionCommit(const wchar_t *result);
    void imeCompositionEnd();
#endif

    // Re-apply highlight_data() after it was cleared by external code
    // (e.g. FindReplace calling highlight_data(nullptr, ...)).
    void refreshLineHighlight();

    // Search-match highlighting (FindReplace): the editor owns the style
    // buffer, so match ranges are registered here and painted by the
    // unified applyLineHighlight() rebuild (style 'D'). Keeps matches,
    // current-line and URL underlines in one consistent layer instead of
    // swapping style buffers and losing one of them.
    void setMatchRanges(const std::vector<std::pair<int,int>> &ranges);
    void clearMatchRanges();

    // Style-buffer snapshot for tests (e.g. 'A'/'B'/'C'/'D'/'E'/'F',
    // plus the hover variants 'G'/'H'/'I').
    const char *debugStyleString() const;

    // Call after directly modifying the buffer from handle(): cbModify
    // runs inside buf->insert()/remove() and sees the *old* cursor
    // position, so the status bar / line highlight would land on the
    // previous line.  Re-firing both here (after insert_position() has
    // been updated) keeps them in sync with the real cursor.
    void notifyCursorMoved();

    // Called when the cursor moves. Only touches the style buffer if
    // the cursor actually moved to a *different* line.
    bool updateLineHighlight();

    // Called when buffer text changes — lengths may differ, so we
    // must do a full rebuild.
    void syncLineHighlightBuffer();

    // Set a callback to be invoked when cursor position changes
    // (used by MainWindow to update status bar).
    void setCursorCallback(CursorCallback cb) { m_cursorCb = std::move(cb); }

    // Set the theme (for color values). nullptr = use defaults.
    void setTheme(const Theme *theme) {
        m_theme = theme;
        if (m_theme) {
            linenumber_fgcolor(m_theme->colors().textPrimary);
            linenumber_bgcolor(m_theme->colors().bgChrome);
            selection_color(m_theme->colors().accentSelection);
            cursor_color(m_theme->colors().textPrimary);
        }
    }

    // Theme-aware text/background color helpers.
    Fl_Color textColor() const {
        return m_theme ? m_theme->colors().textPrimary : FL_BLACK;
    }
    Fl_Color bgColor() const {
        return m_theme ? m_theme->colors().bgEditor : FL_WHITE;
    }

protected:
    // Intercept Tab key: insert N spaces instead of \t.
    // Shift+Tab removes up to N leading spaces.
    // Ctrl+Click on a URL: extract and open in browser.
    // Read-only mode: block modification keys.
    // Enter: auto-indent (copy leading whitespace).
    int handle(int event) FL_OVERRIDE;

    // Override draw to overlay space symbols, long-line marker,
    // reposition horizontal scrollbar, and draw custom scrollbars.
    void draw() FL_OVERRIDE;

public:
    // Re-apply highlight_data() after theme or font change.
    void reapplyHighlightData();

private:
    // Draw a custom scrollbar over the given Fl_Scrollbar area.
    void drawCustomScrollbar(Fl_Scrollbar *sb, bool horizontal);

    // (Re)build the style buffer to match the text buffer, then mark
    // the current line with style 'B'.
    void applyLineHighlight();

    // Scan the buffer for http:// / https:// URLs and mark them
    // with style 'C' (underline) in the style buffer.
    void updateUrlHighlight();

    // Byte range [start, end) of the URL containing `pos`, or false
    // when `pos` is not inside a URL. Shared by Ctrl+Click (open in
    // browser) and the hover check.
    bool urlRangeAt(int pos, int &start, int &end);

    // URL hover feedback: mouse over a URL switches its style to the
    // hover colour (style 'G'/'H'/'I') instead of the old hand cursor.
    // Called from FL_MOVE / FL_MOUSEWHEEL / FL_LEAVE.
    void checkUrlHover();
    void applyUrlHoverStyles();
    void restoreUrlHoverStyles();

    // Scrub leftover URL styles ('C' -> 'A') when detection is off.
    void clearUrlStyles();

    // Active search-match ranges (FindReplace). Sorted, half-open.
    std::vector<std::pair<int,int>> m_matchRanges;

    // Draw a small centered dot for every visible space character.
    void drawSpaceSymbols();

    // Draw a vertical guide line at the configured long-line column.
    void drawLongLineMarker();

    Document   *m_doc = nullptr;
    SettingsProvider *m_settings = nullptr;   // settings access (tab width, auto-indent, etc.)
    const Theme     *m_theme = nullptr;       // theme colors

    // Style buffer for current-line highlight and URL highlight.
    // 'A' = normal, 'B' = highlighted line, 'C' = URL underline,
    // 'G'/'H'/'I' = URL hover variants (see m_styleTable).
    Fl_Text_Buffer *m_styleBuf = nullptr;
    bool            m_lineHighlightOn = false;
    int             m_lastLineStart = -1;
    bool            m_highlightDataSet = false;  // true after first highlight_data() call
    bool            m_showSpaceSymbols = false;  // draw a small dot for each space character
    int             m_longLineMarker = 0;        // 0 = off, >0 = column for vertical guide line
    bool            m_inShiftTab = false;        // suppress line-highlight sync during Shift+Tab loop
    bool            m_overUrl = false;           // mouse is over a detected URL
    int             m_urlHoverStart = -1;        // hovered URL byte range [start, end)
    int             m_urlHoverEnd = -1;

#if defined(_WIN32)
    bool            m_compActive = false;       // IME composition in progress
    int             m_compStart = -1;           // buffer position where the composition lands
    int             m_compEnd = -1;             // replacement range end (== start unless a selection is replaced)
    std::string     m_compText;                 // current composition string (UTF-8)
    int             m_compCursorBytes = 0;      // caret offset inside m_compText (bytes)
#endif

    // Large-file limits: beyond these sizes we skip the optional style
    // buffer / URL scanning that would otherwise duplicate the whole file
    // in memory and cost seconds at open time.
    static const int kStyleBufLimitBytes = 32 * 1024 * 1024;   // 32 MB
    static const int kUrlScanLimitBytes  = 2 * 1024 * 1024;    // 2 MB

    // Callback for cursor movement notification.
    CursorCallback  m_cursorCb;

    // Style table for highlight_data() — 9 entries:
    //   [0] = normal text ('A')
    //   [1] = highlighted line background ('B')
    //   [2] = URL underline ('C')
    //   [3] = search match background ('D')
    //   [4] = URL + line highlight ('E')
    //   [5] = URL + search match ('F')
    //   [6] = URL hover ('G'): underline + linkHover colour
    //   [7] = URL hover + line highlight ('H')
    //   [8] = URL hover + search match ('I')
    Fl_Text_Display::Style_Table_Entry m_styleTable[9];
};
