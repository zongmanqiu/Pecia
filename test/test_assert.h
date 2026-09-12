// test_assert.h - minimal assertion harness for Pecia unit tests.
// No external framework: a failing CHECK prints the file:line and the
// expression, increments the failure counter, and the test main() returns
// non-zero. Kept deliberately tiny so the test target builds in seconds.
#pragma once

#include <cstdio>
#include <cstdlib>

namespace test {
inline int &failCount() { static int n = 0; return n; }
inline int &checkCount() { static int n = 0; return n; }
}

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++test::checkCount();                                                  \
        if (!(cond)) {                                                         \
            ++test::failCount();                                               \
            std::fprintf(stderr, "FAIL %s:%d: %s\n",                           \
                         __FILE__, __LINE__, #cond);                           \
        }                                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        ++test::checkCount();                                                  \
        auto va = (a);                                                         \
        auto vb = (b);                                                         \
        if (!(va == vb)) {                                                     \
            ++test::failCount();                                               \
            std::fprintf(stderr, "FAIL %s:%d: %s == %s  (%lld vs %lld)\n",      \
                         __FILE__, __LINE__, #a, #b,                           \
                         (long long)va, (long long)vb);                        \
        }                                                                      \
    } while (0)

#define TEST_MAIN()                                                            \
    int main() {                                                               \
        int fails = test::failCount();                                         \
        std::fprintf(stderr, "%d checks, %d failures\n",                       \
                     test::checkCount(), fails);                               \
        return fails ? 1 : 0;                                                  \
    }
