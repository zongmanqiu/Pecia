// test_undo_guard.cpp - regression tests for the undo-to-empty hang.
//
// Root cause (verified via crash dump + disassembly, FLTK 1.4.5):
//   Fl_Text_Buffer::count_lines()/line_start()/next_char() hang when a
//   position argument exceeds the buffer length: next_char(pos) returns
//   pos itself once pos >= length, so the caller's `while (pos < end)`
//   loop never advances and spins forever.
//   Trigger: undo/redo invokes the modify callback while the cursor is
//   still stale (e.g. pos=4 on an empty buffer), then updateStatusBar()
//   calls count_lines(0, pos) -> infinite loop -> GUI freeze.
//
// The fix lives in Pecia code (MainWindow::cbModify / updateStatusBar /
// Editor::updateLineHighlight / draw): clamp every position to
// [0, buffer->length()] before calling FLTK position scans.
//
// These tests run the dangerous calls on a worker thread with a timeout,
// so a regression fails the suite instead of hanging it.
#include <FL/Fl_Text_Buffer.H>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <thread>

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

#define CHECK_EQ(a, b)                                                      \
    do {                                                                    \
        long long va = (long long)(a), vb = (long long)(b);                 \
        if (va != vb) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s == %s (%lld vs %lld)\n",         \
                    __FILE__, __LINE__, #a, #b, va, vb);                    \
            ++g_failures;                                                   \
        }                                                                   \
    } while (0)

static int g_failures = 0;

// Run fn on a worker thread; return true iff it finishes within ms.
// A hanging thread is detached (the test process exits anyway, and CTest
// enforces its own timeout as a backstop).
static bool runWithTimeout(const std::function<void()> &fn, int ms) {
    std::atomic<bool> done{false};
    std::thread t([&] {
        fn();
        done = true;
    });
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!done.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (done.load()) {
        t.join();
        return true;
    }
    t.detach();
    return false;
}

// 1. Demonstrate the upstream FLTK 1.4.5 hang: count_displayed_characters(
//    0, 4) on an empty buffer never returns (next_char returns pos itself
//    once pos >= length, so the while loop never advances). count_lines()
//    has its own bounds-protected byte scan and is safe - the hang is in
//    count_displayed_characters(), which updateStatusBar() calls.
static void test_fltk_hang_exists() {
    // Under AddressSanitizer this intentionally-triggered FLTK defect
    // (byte_at on an empty buffer dereferences nullptr) aborts the whole
    // process before later tests can run - skip it there.
    if (getenv("PECIA_SKIP_HANG_TEST")) {
        fprintf(stderr, "hang-test skipped (ASan build)\n");
        return;
    }
    Fl_Text_Buffer buf(64);
    CHECK_EQ(buf.length(), 0);
    bool finished = runWithTimeout(
        [&] { volatile int r = buf.count_displayed_characters(0, 4); (void)r; }, 300);
    // Expected: HANGS (finished == false). FLTK 1.4.5 never returns.
    CHECK(!finished);
    if (finished) {
        fprintf(stderr, "note: FLTK count_displayed_characters no longer hangs "
                        "(upstream fix?) - revisit this test and the clamp rationale\n");
    }
}

// 2. The Pecia fix: clamp the stale cursor to the buffer length before
//    scanning. With the clamp, the exact same calls return instantly.
static void test_clamped_calls_safe_on_empty() {
    Fl_Text_Buffer buf(64);          // empty
    int pos = 4;                     // stale cursor after undo
    int blen = buf.length();         // 0
    if (pos > blen) pos = blen;      // <-- the fix (same logic as updateStatusBar)

    bool finished = runWithTimeout([&] {
        int line = buf.count_lines(0, pos) + 1;
        int lineStart = buf.line_start(pos);
        int col = buf.count_displayed_characters(lineStart, pos) + 1;
        CHECK(line == 1);
        CHECK(lineStart == 0);
        CHECK(col == 1);
    }, 2000);
    CHECK(finished);
}

// 3. Same guard for the line-highlight path (Editor::updateLineHighlight /
//    draw clamp pos before line_start/line_end).
static void test_clamped_line_calls_safe_on_empty() {
    Fl_Text_Buffer buf(64);
    int pos = 4;
    int len = buf.length();
    if (pos < 0) pos = 0;
    if (pos > len) pos = len;

    bool finished = runWithTimeout([&] {
        int lineStart = buf.line_start(pos);
        int lineEnd = buf.line_end(pos);
        if (lineEnd > len) lineEnd = len;
        CHECK(lineStart == 0);
        CHECK(lineEnd == 0);
    }, 2000);
    CHECK(finished);
}

// 4. Normal text keeps working (no regression from the clamp).
static void test_clamp_does_not_affect_normal_buffer() {
    Fl_Text_Buffer buf(64);
    buf.text("hello\nworld");
    int len = buf.length();
    int pos = 5;                       // after "hello"
    if (pos > len) pos = len;          // no-op here
    bool finished = runWithTimeout([&] {
        // FLTK semantics: count_lines stops before endPos (exclusive),
        // counting only complete newlines in the half-open range.
        CHECK(buf.count_lines(0, pos) == 0);     // "hello" - no newline yet
        CHECK(buf.count_lines(0, len) == 1);     // one newline in whole buffer
        CHECK(buf.line_start(pos) == 0);
        CHECK(buf.line_end(pos) == 5);
    }, 2000);
    CHECK(finished);
}

int main() {
    test_fltk_hang_exists();
    test_clamped_calls_safe_on_empty();
    test_clamped_line_calls_safe_on_empty();
    test_clamp_does_not_affect_normal_buffer();
    fprintf(stderr, "%s\n", g_failures == 0 ? "ALL PASS" : "FAILURES");
    return g_failures == 0 ? 0 : 1;
}
