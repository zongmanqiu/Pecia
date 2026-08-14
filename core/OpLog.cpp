// OpLog.cpp - lightweight user-operation logger for crash investigation.
// Records low-frequency semantic user actions (open/save/find/undo/redo/
// lua/encoding) to temp/<exe>_ops-<start timestamp>.log so a crash report
// can be traced step by step. One file per process start (timestamped,
// never grows unbounded; a 1 MB cap trims the tail as a safety net).
// Thread-safe, unbuffered append. Logging is best-effort: failures are
// silently ignored.
#include "core/OpLog.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <windows.h>

// temp/<exe-base-name>_ops-YYYYMMDD_HHMMSS.log next to the executable.
static const char *logFileName() {
    static char path[MAX_PATH];
    static bool inited = false;
    if (!inited) {
        char exe[MAX_PATH];
        DWORD n = GetModuleFileNameA(NULL, exe, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) strcpy_s(exe, "pecia");
        // exe 基名（去掉目录和 .exe）
        char *base = strrchr(exe, '\\');
        base = base ? base + 1 : exe;
        char *dot = strrchr(base, '.');
        if (dot) *dot = 0;

        // temp/ 目录（与预览缓存同一根，7 天统一清理）
        char dir[MAX_PATH];
        {
            char exePath[MAX_PATH];
            GetModuleFileNameA(NULL, exePath, MAX_PATH);
            char *slash = strrchr(exePath, '\\');
            if (slash) *slash = 0;
            else strcpy_s(exePath, ".");
            snprintf(dir, sizeof(dir), "%s\\temp", exePath);
        }
        CreateDirectoryA(dir, NULL);

        // 启动时间戳（首次调用时定死，一次运行一个文件）
        SYSTEMTIME st;
        GetLocalTime(&st);
        snprintf(path, sizeof(path), "%s\\%s_ops-%04u%02u%02u_%02u%02u%02u.log",
                 dir, base,
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        inited = true;
    }
    return path;
}

// 1 MB 兜底：超限时重写为尾部（保留最近一半），防御异常循环。
static const long kOpLogMaxBytes = 1024 * 1024;

static void trimTailIfNeeded() {
    FILE *fp = nullptr;
    if (fopen_s(&fp, logFileName(), "r+") != 0 || !fp) return;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    if (size <= kOpLogMaxBytes) { fclose(fp); return; }
    static char keep[512 * 1024 + 1];
    long readFrom = size - (long)(sizeof(keep) - 1);
    if (readFrom < 0) readFrom = 0;
    fseek(fp, readFrom, SEEK_SET);
    size_t n = fread(keep, 1, sizeof(keep) - 1, fp);
    keep[n] = 0;
    fclose(fp);
    fp = nullptr;
    if (fopen_s(&fp, logFileName(), "w") == 0 && fp) {
        fwrite(keep, 1, n, fp);
        fclose(fp);
    }
}

static void appendLine(const char *line) {
    trimTailIfNeeded();
    FILE *f = nullptr;
    if (fopen_s(&f, logFileName(), "a") != 0 || !f) return;
    fputs(line, f);
    fclose(f);
}

static void getTimeStamp(char *out, size_t n) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(out, n, "%02u:%02u:%02u.%03u",
             (unsigned)st.wHour, (unsigned)st.wMinute,
             (unsigned)st.wSecond, (unsigned)st.wMilliseconds);
}

void opLog(const char *fmt, ...) {
    char stamp[32];
    getTimeStamp(stamp, sizeof(stamp));

    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    char line[1100];
    snprintf(line, sizeof(line), "%s  %s\n", stamp, msg);
    appendLine(line);
}

void opLogStr(const char *what, const std::string &s) {
    opLog("%s: %s", what, s.c_str());
}
