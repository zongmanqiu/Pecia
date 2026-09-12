// OpLog.h - lightweight user-operation logger for bug reproduction.
// Records semantic user actions (open/save/find/undo/redo/lua/encoding)
// to a log file next to the executable (build/pecia_ops.log) so a bug
// report can be traced step by step. Thread-safe, unbuffered append.
// Logging is best-effort: failures are silently ignored.
#pragma once

#include <string>

// Append one operation line "HH:MM:SS.mmm  <text>" to the ops log.
// The log lives in the executable's directory (build/pecia_ops.log).
void opLog(const char *fmt, ...);

// Convenience: log with a UTF-8 string parameter (e.g. a file path).
void opLogStr(const char *what, const std::string &s);
