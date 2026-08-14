// test_search_core.cpp - lock-in tests for the pure search/count/index
// logic in core/SearchCore.{h,cpp}. These encode the behavior that
// produced historical bugs (the 151/150 wrap-index bug, Find All corrupting
// the count, memchr-vs-FLTK consistency) so regressions are caught here
// instead of in the GUI.
#include "test_assert.h"
#include "core/SearchCore.h"

#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// rawSearchForward
// ---------------------------------------------------------------------------

static void test_forward_basic() {
    const char *text = "the quick brown fox jumps over the lazy dog";
    int len = (int)strlen(text);

    CHECK_EQ(rawSearchForward(text, len, 0, "quick", 5, false, false), 4);
    CHECK_EQ(rawSearchForward(text, len, 0, "the", 3, false, false), 0);
    CHECK_EQ(rawSearchForward(text, len, 0, "dog", 3, false, false), 40);
    // fromPos past the last match -> -1
    CHECK_EQ(rawSearchForward(text, len, 41, "dog", 3, false, false), -1);
    // fromPos inside a match -> finds the NEXT one
    CHECK_EQ(rawSearchForward(text, len, 1, "the", 3, false, false), 31);
    // needle not present
    CHECK_EQ(rawSearchForward(text, len, 0, "xyz", 3, false, false), -1);
    // empty needle / bad fromPos
    CHECK_EQ(rawSearchForward(text, len, 0, "", 0, false, false), -1);
    CHECK_EQ(rawSearchForward(text, len, -5, "the", 3, false, false), -1);
}

static void test_forward_case_insensitive() {
    const char *text = "Hello HELLO hello";
    int len = (int)strlen(text);

    // Case-insensitive default: finds the first "hello" at 0
    CHECK_EQ(rawSearchForward(text, len, 0, "hello", 5, false, false), 0);
    // From 1 -> the second occurrence (HELLO at 6)
    CHECK_EQ(rawSearchForward(text, len, 1, "hello", 5, false, false), 6);
    // Case-sensitive: "Hello" matches only at 0, "hello" only at 12
    CHECK_EQ(rawSearchForward(text, len, 0, "hello", 5, true, false), 12);
    CHECK_EQ(rawSearchForward(text, len, 0, "HELLO", 5, true, false), 6);
    // Case-insensitive must still match case-sensitively when asked
    CHECK_EQ(rawSearchForward(text, len, 0, "Hello", 5, true, false), 0);
}

static void test_forward_whole_word() {
    const char *text = "cat category scatter cat";
    int len = (int)strlen(text);

    // "cat" at 0-2, "category" at 4-11, "scatter" at 13-19, "cat" at 21-23
    CHECK_EQ(len, 24);
    // Whole-word: "cat" matches at 0 and 21 (not inside "category"/"scatter")
    CHECK_EQ(rawSearchForward(text, len, 0, "cat", 3, false, true), 0);
    CHECK_EQ(rawSearchForward(text, len, 1, "cat", 3, false, true), 21);
    CHECK_EQ(rawSearchForward(text, len, 22, "cat", 3, false, true), -1);
    // Without whole-word: "cat" at 0, then inside "category" at 4
    CHECK_EQ(rawSearchForward(text, len, 0, "cat", 3, false, false), 0);
    CHECK_EQ(rawSearchForward(text, len, 1, "cat", 3, false, false), 4);
    // Boundary at end of text (match ends at len)
    const char *t2 = "xx cat";
    CHECK_EQ(rawSearchForward(t2, (int)strlen(t2), 0, "cat", 3, false, true), 3);
    // Boundary at start of text
    const char *t3 = "cat xx";
    CHECK_EQ(rawSearchForward(t3, (int)strlen(t3), 0, "cat", 3, false, true), 0);
}

// ---------------------------------------------------------------------------
// rawSearchBackward
// ---------------------------------------------------------------------------

static void test_backward_basic() {
    const char *text = "the quick brown fox jumps over the lazy dog";
    int len = (int)strlen(text);

    // Last "the" before end is at 31
    CHECK_EQ(rawSearchBackward(text, len, len, "the", 3, false, false), 31);
    // Last "the" before 31 is at 0
    CHECK_EQ(rawSearchBackward(text, len, 31, "the", 3, false, false), 0);
    // fromPos = 0 -> nothing
    CHECK_EQ(rawSearchBackward(text, len, 0, "the", 3, false, false), -1);
    // fromPos 1 -> "the"@0 is strictly before position 1
    CHECK_EQ(rawSearchBackward(text, len, 1, "the", 3, false, false), 0);
    // fromPos 5 -> the match at 0
    CHECK_EQ(rawSearchBackward(text, len, 5, "the", 3, false, false), 0);
    // Needle not present
    CHECK_EQ(rawSearchBackward(text, len, len, "xyz", 3, false, false), -1);
}

static void test_backward_whole_word() {
    const char *text = "cat category scatter cat";
    int len = (int)strlen(text);

    CHECK_EQ(rawSearchBackward(text, len, len, "cat", 3, false, true), 21);
    CHECK_EQ(rawSearchBackward(text, len, 21, "cat", 3, false, true), 0);
    CHECK_EQ(rawSearchBackward(text, len, 22, "cat", 3, false, true), 21);
}

// ---------------------------------------------------------------------------
// countRawRange
// ---------------------------------------------------------------------------

static void test_count_basic() {
    const char *text = "is this is that is";
    int len = (int)strlen(text);
    // "is"@0, inside "this"@3, "is"@8, "is"@16 -> 4 occurrences
    CHECK_EQ(len, 18);

    long long count = 0;
    int targetIdx = 0;
    countRawRange(text, len, 0, len, "is", 2, false, false, -1, count, targetIdx);
    CHECK_EQ(count, 4LL);
    CHECK_EQ(targetIdx, 0);   // targetPos -1 never matches

    // Sub-range [3..len): skips the "is" at 0
    count = 0;
    countRawRange(text, len, 3, len, "is", 2, false, false, -1, count, targetIdx);
    CHECK_EQ(count, 3LL);

    // targetPos capture: the match at position 8 is the 3rd occurrence
    count = 0; targetIdx = 0;
    countRawRange(text, len, 0, len, "is", 2, false, false, 8, count, targetIdx);
    CHECK_EQ(count, 4LL);
    CHECK_EQ(targetIdx, 3);

    // Case-insensitive counting: "Is" and "IS" also match
    const char *t2 = "is Is IS";
    count = 0;
    countRawRange(t2, (int)strlen(t2), 0, (int)strlen(t2), "is", 2, false, false, -1, count, targetIdx);
    CHECK_EQ(count, 3LL);
    // Case-sensitive: only lowercase "is" at 0
    count = 0;
    countRawRange(t2, (int)strlen(t2), 0, (int)strlen(t2), "is", 2, true, false, -1, count, targetIdx);
    CHECK_EQ(count, 1LL);

    // Whole-word counting: "is" inside "this" does not count, but the
    // standalone "is"@0, "is"@8 and "is"@16 all do
    count = 0;
    countRawRange(text, len, 0, len, "is", 2, false, true, -1, count, targetIdx);
    CHECK_EQ(count, 3LL);
}

// ---------------------------------------------------------------------------
// nextIndexStep / prevIndexStep  (the 151/150 wrap-bug area)
// ---------------------------------------------------------------------------

static void test_next_advance() {
    // Direct continuation: start = oldLast + matchLen, found > oldLast
    int idx = 3;
    IndexStep s = nextIndexStep(10, 15, 13, 3, true, idx, 100);
    CHECK(s == IndexStep::Advance);
    CHECK_EQ(idx, 4);
}

static void test_next_wrap_first() {
    // Wrap-around: found before oldLast -> index resets to 1
    int idx = 50;
    IndexStep s = nextIndexStep(100, 5, 103, 3, true, idx, 100);
    CHECK(s == IndexStep::WrapFirst);
    CHECK_EQ(idx, 1);
}

static void test_next_rescan() {
    // Jump (found after oldLast but start is not a continuation)
    int idx = 7;
    IndexStep s = nextIndexStep(10, 200, 50, 3, true, idx, 100);
    CHECK(s == IndexStep::Rescan);
    CHECK_EQ(idx, 7);   // untouched

    // Unknown index -> rescan
    idx = 0;
    s = nextIndexStep(10, 15, 13, 3, false, idx, 100);
    CHECK(s == IndexStep::Rescan);
}

static void test_prev_advance() {
    // Direct backward continuation: start = oldLast - 1, found < oldLast
    int idx = 5;
    IndexStep s = prevIndexStep(20, 15, 19, true, idx, 100);
    CHECK(s == IndexStep::Advance);
    CHECK_EQ(idx, 4);
}

static void test_prev_wrap_last() {
    // Backward wrap: found > oldLast -> index resets to total
    int idx = 2;
    IndexStep s = prevIndexStep(5, 95, 4, true, idx, 100);
    CHECK(s == IndexStep::WrapLast);
    CHECK_EQ(idx, 100);
}

static void test_prev_rescan() {
    int idx = 3;
    IndexStep s = prevIndexStep(20, 15, 10, true, idx, 100);  // not contiguous
    CHECK(s == IndexStep::Rescan);
    CHECK_EQ(idx, 3);

    idx = 0;
    s = prevIndexStep(20, 15, 19, false, idx, 100);           // idx unknown
    CHECK(s == IndexStep::Rescan);
}

// Historical regression: the 151/150 bug. When Next wrapped from the end
// back to the first match, the index had to reset to 1 - NOT increment to
// N+1. And when Prev wrapped from the start to the last match, the index
// had to reset to total - NOT decrement to 0.
static void test_wrap_regression() {
    // Forward wrap: oldLast = last match, found = first match (position < oldLast)
    int idx = 150;
    IndexStep s = nextIndexStep(500000, 0, 500000 + 5, 5, true, idx, 150);
    CHECK(s == IndexStep::WrapFirst);
    CHECK_EQ(idx, 1);

    // Backward wrap: oldLast = first match, found = last match (position > oldLast)
    idx = 1;
    s = prevIndexStep(0, 499990, -1, true, idx, 150);
    CHECK(s == IndexStep::WrapLast);
    CHECK_EQ(idx, 150);

    // Non-continuation Next after a jump: must NOT blindly increment
    idx = 5;
    s = nextIndexStep(100, 400, 399, 3, true, idx, 150);
    CHECK(s == IndexStep::Rescan);
    CHECK_EQ(idx, 5);
}

// ---------------------------------------------------------------------------
// Consistency: raw forward scan over the whole text must agree with a
// countRawRange over the same range (same matcher semantics).
// ---------------------------------------------------------------------------

static void test_raw_count_consistency() {
    const char *text = "foo bar foo baz foo qux foo";
    int len = (int)strlen(text);

    // Count all "foo" occurrences via countRawRange
    long long count = 0;
    int targetIdx = 0;
    countRawRange(text, len, 0, len, "foo", 3, false, false, -1, count, targetIdx);

    // Walk with rawSearchForward and count matches
    long long walked = 0;
    int pos = 0;
    for (;;) {
        int f = rawSearchForward(text, len, pos, "foo", 3, false, false);
        if (f < 0) break;
        ++walked;
        pos = f + 3;
    }
    CHECK_EQ(count, walked);
    CHECK_EQ(count, 4LL);
}

// ---------------------------------------------------------------------------
// wholeWordOk (shared boundary check extracted from FindReplace)
// ---------------------------------------------------------------------------

static void test_replace_all_forward() {
    // basic replacements
    std::string t1 = "the quick brown fox";
    CHECK_EQ(replaceAllForward(t1, "the", 3, "A", 1, false, false), 1);
    CHECK(t1 == "A quick brown fox");

    // multiple, non-overlapping
    std::string t2 = "aa aa aa";
    CHECK_EQ(replaceAllForward(t2, "aa", 2, "X", 1, false, false), 3);
    CHECK(t2 == "X X X");

    // overlapping must not match twice (aaaa -> two aa)
    std::string t3 = "aaaa";
    CHECK_EQ(replaceAllForward(t3, "aa", 2, "X", 1, false, false), 2);
    CHECK(t3 == "XX");

    // case-insensitive
    std::string t4 = "Foo foo FOO";
    CHECK_EQ(replaceAllForward(t4, "foo", 3, "B", 1, false, false), 3);
    CHECK(t4 == "B B B");
    // case-sensitive keeps Foo/FOO
    std::string t5 = "Foo foo FOO";
    CHECK_EQ(replaceAllForward(t5, "foo", 3, "B", 1, true, false), 1);
    CHECK(t5 == "Foo B FOO");

    // whole-word: "cat" must not match inside "category"
    std::string t6 = "cat category cat";
    CHECK_EQ(replaceAllForward(t6, "cat", 3, "X", 1, false, true), 2);
    CHECK(t6 == "X category X");
    // underscore is a word char
    std::string t7 = "cat cat_name";
    CHECK_EQ(replaceAllForward(t7, "cat", 3, "X", 1, false, true), 1);
    CHECK(t7 == "X cat_name");

    // empty replacement must not loop forever (delete all matches)
    std::string t8 = "aXbXcX";
    CHECK_EQ(replaceAllForward(t8, "X", 1, "", 0, false, false), 3);
    CHECK(t8 == "abc");
    // empty needle rejected
    std::string t9 = "abc";
    CHECK_EQ(replaceAllForward(t9, "", 0, "X", 1, false, false), -1);
    CHECK(t9 == "abc");

    // longer replacement grows the text
    std::string t10 = "a b";
    CHECK_EQ(replaceAllForward(t10, " ", 1, " X ", 3, false, false), 1);
    CHECK(t10 == "a X b");
}

static void test_whole_word_ok() {
    const char *text = "cat category scatter cat";
    int len = (int)strlen(text);   // 24

    // "cat" at 0: left boundary OK (pos 0), right char ' ' OK
    CHECK(wholeWordOk(text, len, 0, 3));
    // "cat" inside "category" at 4: right char 'e' is a word char
    CHECK(!wholeWordOk(text, len, 4, 3));
    // "scatter": 't' after "cat" is a word char
    CHECK(!wholeWordOk(text, len, 13, 3));
    // last "cat" at 21 ends exactly at len: right boundary OK
    CHECK(wholeWordOk(text, len, 21, 3));
    // left neighbor is a word char -> reject
    const char *t2 = "xcat";
    CHECK(!wholeWordOk(t2, 4, 1, 3));
    // right neighbor is a word char -> reject
    const char *t3 = "catx";
    CHECK(!wholeWordOk(t3, 4, 0, 3));
    // underscore counts as a word char
    const char *t4 = "_cat";
    CHECK(!wholeWordOk(t4, 4, 1, 3));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

static void run_all() {
    test_forward_basic();
    test_forward_case_insensitive();
    test_forward_whole_word();
    test_backward_basic();
    test_backward_whole_word();
    test_count_basic();
    test_next_advance();
    test_next_wrap_first();
    test_next_rescan();
    test_prev_advance();
    test_prev_wrap_last();
    test_prev_rescan();
    test_wrap_regression();
    test_raw_count_consistency();
    test_replace_all_forward();
    test_whole_word_ok();
}

int main() {
    run_all();
    int fails = test::failCount();
    std::fprintf(stderr, "%d checks, %d failures\n",
                 test::checkCount(), fails);
    return fails ? 1 : 0;
}
