// test_assert.h - minimal assertion harness for Pecia unit tests.
// No external framework: a failing CHECK prints the file:line and the
// expression, increments the failure counter, and the test main() returns
// non-zero. Kept deliberately tiny so the test target builds in seconds.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>
#include <type_traits>

namespace test {
inline int &failCount() { static int n = 0; return n; }
inline int &checkCount() { static int n = 0; return n; }

// Format one failed CHECK_EQ operand. The old implementation cast everything
// to long long, which silently truncated floats (42.5f printed as "42").
// Floating-point operands now keep their fractional part as a double.
template <typename T>
inline void appendValue(std::string &out, T v) {
    if constexpr (std::is_floating_point_v<T>) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", static_cast<double>(v));
        out += buf;
    } else {
        out += std::to_string((long long)v);
    }
}
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
            std::string sa, sb;                                                \
            test::appendValue(sa, va);                                         \
            test::appendValue(sb, vb);                                         \
            std::fprintf(stderr, "FAIL %s:%d: %s == %s  (%s vs %s)\n",          \
                         __FILE__, __LINE__, #a, #b,                           \
                         sa.c_str(), sb.c_str());                              \
        }                                                                      \
    } while (0)

#define TEST_MAIN()                                                            \
    int main() {                                                               \
        int fails = test::failCount();                                         \
        std::fprintf(stderr, "%d checks, %d failures\n",                       \
                     test::checkCount(), fails);                               \
        return fails ? 1 : 0;                                                  \
    }
