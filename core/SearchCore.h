// SearchCore.h - pure search/count/index logic, free of any UI state.
// Extracted from FindReplace so the trickiest logic in the editor (the
// large-file raw searches, the async counter, and the Next/Prev index
// state machine that produced the 151/150-style wrap bugs) can be unit
// tested without instantiating FLTK widgets.
#pragma once

#include <string>

// Whole-word boundary check shared by all searches: the bytes immediately
// before and after a match at [found, found+needleLen) must not be word
// characters (alphanumeric or underscore). Extracted once here so the
// forward/backward raw searches and FindReplace's whole-word filter share
// a single implementation.
bool isWordChar(char c);
bool wholeWordOk(const char *text, int len, int found, int needleLen);

// Fast memchr-based forward search for ASCII needles. Replicates
// Fl_Text_Buffer::search_forward semantics (byte compare, ASCII
// case-insensitivity, whole-word boundaries) but scans with memchr,
// ~20x faster on dense needles in hundreds-of-MB files.
// Returns the match position, or -1 if none.
int rawSearchForward(const char *text, int len, int fromPos,
                     const char *needle, int needleLen,
                     bool matchCase, bool wholeWord);

// Fast single-match backward search (single pass, keeps the last match
// strictly before fromPos). Same semantics as rawSearchForward.
// Returns the match position, or -1 if none.
int rawSearchBackward(const char *text, int len, int fromPos,
                      const char *needle, int needleLen,
                      bool matchCase, bool wholeWord);

// Fast match counter for ASCII needles: scans text[start..end) with
// memchr + ASCII case folding. Same semantics as rawSearchForward.
// The sequence number (1-based) of the match at `targetPos` is stored
// into `targetIdx` when that position is encountered (0 = not captured).
void countRawRange(const char *text, int len, int start, int end,
                   const char *needle, int needleLen,
                   bool matchCase, bool wholeWord,
                   int targetPos, long long &count, int &targetIdx);

// Replace every non-overlapping occurrence of needle in `text` with
// repl, honouring matchCase/wholeWord (same semantics as
// rawSearchForward). The scan always advances strictly so an empty
// replacement cannot re-match itself. Returns the number of
// replacements, or -1 when needle is empty.
int replaceAllForward(std::string &text,
                      const char *needle, int needleLen,
                      const char *repl, int replLen,
                      bool matchCase, bool wholeWord);

// Result of one Next/Prev index step on a large (read-only) buffer.
// The index can only be adjusted incrementally when the new match is a
// direct continuation of the previous one; otherwise the caller must
// either rescan or reset.
enum class IndexStep {
    Advance,   // index updated in place (idx was +/-1'd)
    WrapFirst, // wrapped around: index is now 1 (Next)
    WrapLast,  // wrapped around: index is now total (Prev)
    Rescan,    // cannot infer the index; caller must scan for it
};

// findNext's large-buffer index update. `continued` semantics: the new
// match must lie strictly AFTER the previous match and the search must
// have started exactly at the previous match's end.
//   oldLast: previous match position (-1 if none)
//   found:   new match position
//   start:   position the forward search began from
//   matchLen: needle length
//   idxKnown: whether the current index is still valid for this needle
//   idx:      in/out current match index (1-based)
//   total:    total match count (for WrapLast in prevIndexStep)
IndexStep nextIndexStep(int oldLast, int found, int start, int matchLen,
                        bool idxKnown, int &idx, int total);

// findPrev's large-buffer index update. A direct continuation starts
// exactly one byte before the previous match and the new match must lie
// strictly BEFORE it. A backward wrap resets the index to the last match.
IndexStep prevIndexStep(int oldLast, int found, int start,
                        bool idxKnown, int &idx, int total);
