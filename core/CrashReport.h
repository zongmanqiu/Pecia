// CrashReport.h - shared top-level exception handler for all three
// Pecia executables (Pecia, PeciaLua, PeciaAIChat).
//
// install() must be called at the very start of main() (before any
// window or config setup) so failures during startup are captured too.
// On an unhandled exception the handler writes <tag>_crash_*.dmp and
// appends <tag>_crash.txt next to the executable, then shows a message
// box pointing the user at the dump file.
#pragma once

// Cap for the <tag>_crash.txt log (bytes). Older entries are trimmed
// when a crash pushes the file past this size.
#define CRASH_LOG_MAX_BYTES (256 * 1024)

class CrashReport {
public:
    // Install the handler. `exeTag` prefixes the crash file names
    // (e.g. "pecia", "pecialua", "peciaaichat") so the three processes
    // sharing one directory never overwrite each other's dumps.
    static void install(const char *exeTag);
};
