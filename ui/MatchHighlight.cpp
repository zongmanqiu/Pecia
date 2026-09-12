// MatchHighlight.cpp - search-match highlight manager implementation.
// See MatchHighlight.h. The match-marking loop uses searchForward via a
// callback so the find bar (which owns the FLTK search semantics) does
// not need to be coupled into this class.
#include "ui/MatchHighlight.h"

#include "editor/Editor.h"

#include <FL/Fl_Text_Buffer.H>

#include <cstring>

bool MatchHighlight::highlightAll(Editor *editor, const char *needle,
                                  int needleLen, bool matchCase) {
    if (!editor) return false;
    Fl_Text_Buffer *buf = editor->buffer();
    if (!buf) return false;
    int len = buf->length();
    if (len <= 0) return false;

    // Large files: no highlight AND no counting (both require full-file
    // work: an equal-size style buffer, or a full scan per match - either
    // freezes the UI for seconds on hundreds of MB). Find Next/Prev still
    // work normally.
    if (len > kLargeFindHighlightLimitBytes) {
        return false;
    }

    // Collect every match as a half-open byte range. search_forward
    // returns matches in buffer order, so the ranges are already sorted.
    m_ranges.clear();
    if (needle && *needle) {
        int pos = 0;
        int found = 0;
        while (buf->search_forward(pos, needle, &found, matchCase ? 1 : 0) != 0) {
            m_ranges.emplace_back(found, found + needleLen);
            pos = found + needleLen;
            if (pos >= len) break;
        }
    }

    // The Editor owns the style buffer and repaints matches (style 'D')
    // together with the current-line and URL highlights in one layer.
    editor->setMatchRanges(m_ranges);
    m_active = true;
    return true;
}

void MatchHighlight::clear(Editor *editor) {
    if (!editor) return;
    editor->clearMatchRanges();
    m_ranges.clear();
    m_active = false;
}
