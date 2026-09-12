// OpLog.cpp - lightweight user-operation logger for crash investigation.
// Records low-frequency semantic user actions (open/save/find/undo/redo/
// lua/encoding) to temp/<exe>_ops-<start timestamp>.log so a crash report
// can be traced step by step. One file per process start (timestamped,
// never grows unbounded; a 1 MB cap trims the tail as a safety net).
// Thread-safe, unbuffered append. Logging is best-effort: failures are
// silently ignored.
//
// Paths are handled as wide characters end to end so a Chinese install
// directory still produces a working log (fl_fopen resolves UTF-8 via
// the wide CRT under the hood).
#include "core/OpLog.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <windows.h>
#include <FL/fl_utf8.h>   // fl_fopen

// Serializes all log writes: the AI HTTP thread and the main thread both
// log, and trimTailIfNeeded rewrites the file - without a lock two threads
// could interleave lines or one thread's trim could clobber another's
// append. Best-effort logging, but never corrupt.
static std::mutex g_opLogMu;

// Run inside the normal process lifetime (not the crash handler), so
// std::wstring allocations are safe here.
static std::wstring widen(const char *utf8) {
    if (!utf8 || !*utf8) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &w[0], n);
    while (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

// temp/<exe-base-name>_ops-YYYYMMDD_HHMMSS.log next to the executable.
// Returned path is UTF-8; call sites open it through fl_fopen.
static const char *logFileName() {
    static char path[1024];
    static bool inited = false;
    if (!inited) {
        wchar_t exeW[MAX_PATH];
        DWORD n = GetModuleFileNameW(NULL, exeW, MAX_PATH);
        int u8n = (n == 0 || n >= MAX_PATH)
                      ? 0
                      : WideCharToMultiByte(CP_UTF8, 0, exeW, -1, path,
                                            sizeof(path), NULL, NULL);
        if (u8n <= 0) {
            strcpy_s(path, "pecia_ops.log");
            inited = true;
            return path;
        }

        // exe 基名（去掉目录和 .exe）；'\\' 与 '.' 均为单字节，可在 UTF-8 中直接查找
        char exeU8[1024];
        strcpy_s(exeU8, path);
        char *base = strrchr(exeU8, '\\');
        base = base ? base + 1 : exeU8;
        char *dot = strrchr(base, '.');
        if (dot) *dot = 0;

        // temp/ 目录（与预览缓存同一根，7 天统一清理）
        char dir[1024];
        strcpy_s(dir, exeU8);
        char *slash = strrchr(dir, '\\');
        if (!slash) strcpy_s(dir, ".");
        else *slash = 0;
        {
            size_t len = strlen(dir);
            snprintf(dir + len, sizeof(dir) - len, "\\temp");
        }
        CreateDirectoryW(widen(dir).c_str(), NULL);

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
    FILE *fp = fl_fopen(logFileName(), "r+");
    if (!fp) return;
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
    fp = fl_fopen(logFileName(), "w");
    if (fp) {
        fwrite(keep, 1, n, fp);
        fclose(fp);
    }
}

static void appendLine(const char *line) {
    trimTailIfNeeded();
    FILE *f = fl_fopen(logFileName(), "a");
    if (!f) return;
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
    std::lock_guard<std::mutex> lk(g_opLogMu);
    appendLine(line);
}

void opLogStr(const char *what, const std::string &s) {
    opLog("%s: %s", what, s.c_str());
}
