// OpLog.h - lightweight user-operation logger for bug reproduction.
// Records semantic user actions (open/save/find/undo/redo/lua/encoding)
// to temp/<exe>_ops-<start timestamp>.log (next to the executable) so a bug
// report can be traced step by step. Thread-safe, unbuffered append.
// Logging is best-effort: failures are silently ignored.
#pragma once

#include <string>

// Append one operation line "HH:MM:SS.mmm  <text>" to the ops log.
// The log lives in <exe dir>/temp/<exe>_ops-<start timestamp>.log.
void opLog(const char *fmt, ...);

// Convenience: log with a UTF-8 string parameter (e.g. a file path).
void opLogStr(const char *what, const std::string &s);
