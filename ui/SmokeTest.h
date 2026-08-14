// SmokeTest.h - test hook for the UI smoke test.
//
// When set true, modal windows return from show()/runModal() immediately
// after building + showing (instead of blocking in their internal
// `while (shown()) Fl::wait()` loop). No behavior in normal runs changes.
// Declared header-only/inline so the flag is a single global shared by the
// main target and the test target.
#pragma once

namespace ui {
inline bool g_smokeMode = false;
}
