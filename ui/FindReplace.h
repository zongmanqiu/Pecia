// FindReplace.h - inline find/replace bar at the top of the editor
#pragma once

#include <FL/Fl_Group.H>
#include <functional>
#include <string>

class Fl_Input;
class Fl_Button;
class Fl_Check_Button;
class CenteredOutput;
class Fl_Box;
class Fl_Text_Editor;
class Fl_Text_Buffer;
class MatchHighlight;

class Theme;

class FindReplace : public Fl_Group {
public:
    FindReplace(int x, int y, int w, int h);
    ~FindReplace();

    void setEditor(Fl_Text_Editor *e);

    void activate(const char *initialText = nullptr, bool focusReplace = false);

    // Recompute derived layout (match-count width, hint label visibility)
    // after the widget's outer width changes (e.g. window resize).
    void layout(int w);

    void findNext();
    void findPrev();
    void replaceOne();
    void replaceAll();

    // Refresh all button/checkbox labels after a language change.
    void refreshLabels();

    bool caseSensitive() const;
    bool wholeWord() const;

    // Highlight / clear all search matches in the editor.
    void highlightAllMatches();
    void clearMatchHighlight();
    void setCloseCallback(std::function<void()> cb) { m_onClose = std::move(cb); }
    void setHighlightCallback(std::function<void(bool)> cb) { m_onHighlightChange = std::move(cb); }

    // Set theme for color values and immediately apply them.
    void setTheme(const Theme *theme);

    // Set the UI chrome font size (labels, inputs).
    void setFontSize(int sz);

protected:
    // Override handle() to update hint label visibility on focus changes.
    int handle(int event) FL_OVERRIDE;

private:
    static bool wholeWordOk(Fl_Text_Buffer *buf, int found, int needleLen);
    int searchForward(Fl_Text_Buffer *buf, int start,
                       const char *needle, int needleLen,
                       int matchCase, int *foundPos);
    int searchBackward(Fl_Text_Buffer *buf, int start,
                        const char *needle, int needleLen,
                        int matchCase, int *foundPos);
    void updateMatchIndex(Fl_Text_Buffer *buf, const char *needle,
                          int matchLen, int matchCase,
                          int found, int oldLast, int start, bool forward);

    Fl_Input        *m_findInput;
    Fl_Input        *m_replaceInput;
    Fl_Button       *m_btnNext;
    Fl_Button       *m_btnPrev;
    Fl_Button       *m_btnFindAll;
    Fl_Button       *m_btnReplace;
    Fl_Button       *m_btnReplaceAll;
    Fl_Check_Button *m_chkCase;
    Fl_Check_Button *m_chkWholeWord;
    Fl_Check_Button *m_chkWrap;
    CenteredOutput  *m_matchCount;
    Fl_Box          *m_findLabel;
    Fl_Box          *m_replaceLabel;
    Fl_Button       *m_btnClose;

    Fl_Text_Editor  *m_editor;
    int m_lastPos;       // 位置 of the last match found by findNext/findPrev
                        // (-1 = no match yet, used to compute "current/total")
    int m_totalCount;    // Total number of matches (cached)
    int m_currentIndex;  // 1-based index of current match (0 = none)

    // Large-buffer match counting: for >100 MB (read-only) buffers the
    // full-file match count is computed once per needle in bounded chunks
    // driven by timeouts (the UI never freezes). While scanning, the
    // sequence number of the current match is captured as a byproduct;
    // after the count completes, Next/Prev update the index
    // incrementally (no rescan).
    int         m_countChunkPos = 0;    // bytes scanned so far
    long long   m_countChunkTotal = 0;  // matches found so far
    bool        m_countChunkActive = false;  // a chunked count is in flight
    bool        m_countDone = false;         // count completed for m_countNeedle
    bool        m_countIdxKnown = false;     // m_currentIndex captured for the current match
    bool        m_countIndexOnly = false;    // scan stops after capturing the index
    char       *m_countText = nullptr;       // snapshot of the buffer for raw counting
    int         m_countTextLen = 0;
    std::string m_countNeedle;          // needle the ongoing/finished count refers to
    // Buffer the current chunked count belongs to. Switching tabs swaps
    // m_editor (and thus the buffer) while a count is in flight; the count
    // callback must then discard its stale snapshot instead of displaying
    // the old file's match numbers on the new tab.
    Fl_Text_Buffer *m_countOwner = nullptr;
    void startAsyncCount();
    void startIndexScan();
    void cancelCount();             // abort an in-flight chunked count (tab switch)
    static void countChunkCb(void *data);

    // Search-match highlighting (own style buffer lifecycle).
    MatchHighlight *m_highlight;

    // Cached horizontal range for the match-count display: it stretches
    // from m_countStartX (just after the last checkbox) to
    // (close button x - GAP) on the right. layout() re-uses this on
    // every resize so the count always fills the remaining space.
    int m_countStartX;

    std::function<void()> m_onClose;
    std::function<void(bool)> m_onHighlightChange;  // true = highlight on, false = off

    const Theme *m_theme = nullptr;
    int          m_fontSize = 16;   // font size for labels and inputs

    int countMatches();
    void updateMatchCount();
    void updateHintVisibility();
    void updateButtonStates();   // enable/disable All/Replace/ReplaceAll per read-only
    bool editorReadOnly() const; // true if the bound editor's document is read-only

    static void cbFindNext(Fl_Widget *w, void *data);
    static void cbFindPrev(Fl_Widget *w, void *data);
    static void cbFindAll(Fl_Widget *w, void *data);
    static void cbReplace(Fl_Widget *w, void *data);
    static void cbReplaceAll(Fl_Widget *w, void *data);
    static void cbClose(Fl_Widget *w, void *data);
};
