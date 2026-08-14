// SearchCore.cpp - implementation of the pure search/count/index logic.
// See SearchCore.h. All functions are free of UI state by design.
#include "core/SearchCore.h"

#include <string.h>   // memchr
#include <string>

bool isWordChar(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') || c == '_';
}

bool wholeWordOk(const char *text, int len, int found, int needleLen) {
    bool leftOk = (found == 0) || !isWordChar(text[found - 1]);
    bool rightOk = (found + needleLen >= len) ||
                    !isWordChar(text[found + needleLen]);
    return leftOk && rightOk;
}

int rawSearchForward(const char *text, int len, int fromPos,
                     const char *needle, int needleLen,
                     bool matchCase, bool wholeWord) {
    if (needleLen <= 0 || fromPos < 0) return -1;
    const unsigned char first = (unsigned char)needle[0];
    const unsigned char firstLo = (first >= 'A' && first <= 'Z') ? first + 32 : first;
    const unsigned char firstHi = (first >= 'a' && first <= 'z') ? first - 32 : first;
    const int scanLimit = len - needleLen + 1;
    int i = fromPos;
    if (i > scanLimit) return -1;
    while (i < scanLimit) {
        // Match the first character exactly when case-sensitive; when
        // case-insensitive, find whichever of the two case forms comes
        // first (the old code only looked for firstLo, so an uppercase
        // match earlier in the text was skipped).
        const char *p = (const char *)memchr(text + i, first, (size_t)(scanLimit - i));
        if (!matchCase) {
            const char *q = (const char *)memchr(text + i, firstLo, (size_t)(scanLimit - i));
            const char *r = (firstHi != firstLo)
                ? (const char *)memchr(text + i, firstHi, (size_t)(scanLimit - i))
                : nullptr;
            const char *alt = q && r ? (q < r ? q : r) : (q ? q : r);
            if (!p || (alt && alt < p)) p = alt;
        }
        if (!p) return -1;
        const int pos = (int)(p - text);
        bool ok = true;
        for (int k = 1; k < needleLen; ++k) {
            unsigned char a = (unsigned char)text[pos + k];
            unsigned char b = (unsigned char)needle[k];
            if (matchCase) {
                if (a != b) { ok = false; break; }
            } else {
                unsigned char la = (a >= 'A' && a <= 'Z') ? a + 32 : a;
                unsigned char lb = (b >= 'A' && b <= 'Z') ? b + 32 : b;
                if (la != lb) { ok = false; break; }
            }
        }
        if (ok && wholeWord) {
            ok = wholeWordOk(text, len, pos, needleLen);
        }
        if (ok) return pos;
        i = pos + 1;
    }
    return -1;
}

int rawSearchBackward(const char *text, int len, int fromPos,
                      const char *needle, int needleLen,
                      bool matchCase, bool wholeWord) {
    if (needleLen <= 0 || fromPos <= 0) return -1;
    const unsigned char first = (unsigned char)needle[0];
    const unsigned char firstLo = (first >= 'A' && first <= 'Z') ? first + 32 : first;
    const unsigned char firstHi = (first >= 'a' && first <= 'z') ? first - 32 : first;
    const int scanLimit = len - needleLen + 1;
    int last = -1;
    int i = 0;
    while (i < scanLimit && i < fromPos) {
        // Same first-char policy as rawSearchForward: exact match when
        // case-sensitive, earliest of the two case forms otherwise.
        const char *p = (const char *)memchr(text + i, first, (size_t)(scanLimit - i));
        if (!matchCase) {
            const char *q = (const char *)memchr(text + i, firstLo, (size_t)(scanLimit - i));
            const char *r = (firstHi != firstLo)
                ? (const char *)memchr(text + i, firstHi, (size_t)(scanLimit - i))
                : nullptr;
            const char *alt = q && r ? (q < r ? q : r) : (q ? q : r);
            if (!p || (alt && alt < p)) p = alt;
        }
        if (!p) break;
        const int pos = (int)(p - text);
        if (pos >= fromPos) break;
        bool ok = true;
        for (int k = 1; k < needleLen; ++k) {
            unsigned char a = (unsigned char)text[pos + k];
            unsigned char b = (unsigned char)needle[k];
            if (matchCase) {
                if (a != b) { ok = false; break; }
            } else {
                unsigned char la = (a >= 'A' && a <= 'Z') ? a + 32 : a;
                unsigned char lb = (b >= 'A' && b <= 'Z') ? b + 32 : b;
                if (la != lb) { ok = false; break; }
            }
        }
        if (ok && wholeWord) {
            ok = wholeWordOk(text, len, pos, needleLen);
        }
        if (ok) last = pos;
        i = pos + 1;
    }
    return last;
}

void countRawRange(const char *text, int len, int start, int end,
                   const char *needle, int needleLen,
                   bool matchCase, bool wholeWord,
                   int targetPos, long long &count, int &targetIdx) {
    if (needleLen <= 0 || start >= end || end > len) return;
    int i = start;
    const unsigned char first = (unsigned char)needle[0];
    const unsigned char firstLo = (first >= 'A' && first <= 'Z') ? first + 32 : first;
    const unsigned char firstHi = (first >= 'a' && first <= 'z') ? first - 32 : first;
    const int scanLimit = len - needleLen + 1;
    auto wordChar = [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
               (c >= 'a' && c <= 'z') || c == '_';
    };
    while (i < end && i < scanLimit) {
        // Same first-char policy as rawSearchForward: exact match when
        // case-sensitive, earliest of the two case forms otherwise.
        const char *p = (const char *)memchr(text + i, first, (size_t)(scanLimit - i));
        if (!matchCase) {
            const char *q = (const char *)memchr(text + i, firstLo, (size_t)(scanLimit - i));
            const char *r = (firstHi != firstLo)
                ? (const char *)memchr(text + i, firstHi, (size_t)(scanLimit - i))
                : nullptr;
            const char *alt = q && r ? (q < r ? q : r) : (q ? q : r);
            if (!p || (alt && alt < p)) p = alt;
        }
        if (!p || (int)(p - text) >= end) break;
        const int pos = (int)(p - text);
        bool ok = true;
        for (int k = 1; k < needleLen; ++k) {
            unsigned char a = (unsigned char)text[pos + k];
            unsigned char b = (unsigned char)needle[k];
            if (matchCase) {
                if (a != b) { ok = false; break; }
            } else {
                unsigned char la = (a >= 'A' && a <= 'Z') ? a + 32 : a;
                unsigned char lb = (b >= 'A' && b <= 'Z') ? b + 32 : b;
                if (la != lb) { ok = false; break; }
            }
        }
        if (ok && wholeWord) {
            bool leftOk = (pos == 0) || !wordChar(text[pos - 1]);
            bool rightOk = (pos + needleLen >= len) || !wordChar(text[pos + needleLen]);
            ok = leftOk && rightOk;
        }
        if (ok) {
            ++count;
            if (targetIdx == 0 && pos == targetPos)
                targetIdx = (int)count;
        }
        i = pos + 1;
    }
}

// findNext's large-buffer index update. The index is only incrementable
// when this search continued directly from the previous match - the found
// match must actually lie AFTER it. A wrap-around (found before the old
// match) resets to the first match instead of incrementing.
IndexStep nextIndexStep(int oldLast, int found, int start, int matchLen,
                        bool idxKnown, int &idx, int /*total*/) {
    if (!idxKnown) return IndexStep::Rescan;
    bool continued = (oldLast >= 0 && start == oldLast + matchLen &&
                      found > oldLast);
    if (continued) {
        ++idx;
        return IndexStep::Advance;
    }
    if (found < oldLast) {
        idx = 1;               // wrapped to the first match
        return IndexStep::WrapFirst;
    }
    return IndexStep::Rescan;
}

// findPrev's large-buffer index update. A direct continuation starts
// exactly one byte before the previous match and the found match must lie
// BEFORE it; a backward wrap resets the index to the last match.
IndexStep prevIndexStep(int oldLast, int found, int start,
                        bool idxKnown, int &idx, int total) {
    if (!idxKnown) return IndexStep::Rescan;
    bool continued = (oldLast >= 0 && start == oldLast - 1 &&
                      found < oldLast);
    if (continued) {
        --idx;
        return IndexStep::Advance;
    }
    if (found > oldLast) {
        idx = total;           // wrapped to the last match
        return IndexStep::WrapLast;
    }
    return IndexStep::Rescan;
}

int replaceAllForward(std::string &text,
                      const char *needle, int needleLen,
                      const char *repl, int replLen,
                      bool matchCase, bool wholeWord) {
    if (needleLen <= 0) return -1;
    if (!repl || replLen <= 0) { repl = ""; replLen = 0; }
    int pos = 0;
    int count = 0;
    for (;;) {
        int found = rawSearchForward(text.data(), (int)text.size(), pos,
                                     needle, needleLen, matchCase, wholeWord);
        if (found < 0) break;
        text.replace(found, needleLen, repl, replLen);
        ++count;
        // Always advance strictly so empty replacements cannot re-match.
        pos = found + (replLen > 0 ? replLen : needleLen);
        if (pos >= (int)text.size()) break;
    }
    return count;
}
