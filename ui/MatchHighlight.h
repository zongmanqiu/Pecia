// MatchHighlight.h - search-match highlight manager for FindReplace.
// Scans the buffer for every occurrence of the needle and hands the
// match byte-ranges to the Editor, which paints them (style 'D') in its
// own style buffer together with the current-line and URL highlights.
// Extracted from FindReplace so the match-scanning lifecycle lives in
// one small class instead of inside the find bar's implementation.
#pragma once

#include <utility>
#include <vector>

class Fl_Text_Editor;
class Editor;

// Highlight threshold: buffers larger than this skip match highlighting
// (an equal-size style buffer + per-match marking would freeze the UI).
// Find Next/Prev still work; the Find All button is disabled separately.
constexpr int kLargeFindHighlightLimitBytes = 100 * 1024 * 1024;

class MatchHighlight {
public:
    MatchHighlight() = default;

    // Match byte-ranges from the last highlightAll() call (tests).
    const std::vector<std::pair<int,int>> &ranges() const { return m_ranges; }

    // Scan the editor's buffer for every occurrence of `needle` and
    // register the match ranges on the editor (which repaints them).
    // Returns true if scanning was applied (buffer small enough).
    bool highlightAll(Editor *editor, const char *needle,
                      int needleLen, bool matchCase);

    // Remove the match ranges from the editor (normal drawing resumes).
    void clear(Editor *editor);

    bool active() const { return m_active; }
    void setActive(bool on) { m_active = on; }

private:
    std::vector<std::pair<int,int>> m_ranges;
    bool m_active = false;
};
