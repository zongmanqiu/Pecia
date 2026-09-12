// CrashReport.cpp - shared top-level exception handler for all three
// Pecia executables (Pecia, PeciaLua, PeciaAIChat).
//
// On an unhandled exception it writes, in temp/ next to the executable:
//   <tag>_crash_YYYYMMDD_HHMMSS.dmp   - MiniDump for offline analysis
//   <tag>_crash_YYYYMMDD_HHMMSS.txt   - exception info, stack addresses,
//                                       environment snapshot and the tail
//                                       of the operation log (last ~30 lines)
// and shows a message box telling the user where the files are.
// temp/ is swept by the 7-day cleanup; the newest N dump files are kept.
//
// Analysis without PDB: every build links with /MAP + /DYNAMICBASE:NO
// (fixed image base), so the logged addresses map 1:1 onto the build's
// .map file - keep the .map from each release and any crash from any
// user can be resolved to exact functions offline.
#include "CrashReport.h"

#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>

#include <time.h>

#pragma comment(lib, "dbghelp.lib")

namespace {

char g_tag[32] = "pecia";
const char *g_tagCStr() { return g_tag; }

// temp/ directory next to the running executable (shared with preview
// caches and op logs; swept by the 7-day cleanup).
void tempDir(wchar_t *out, int cap) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wchar_t *slash = wcsrchr(exePath, L'\\');
    if (slash) *slash = 0;
    swprintf_s(out, cap, L"%s\\temp", exePath);
}

// Crash 专用窄字符缓冲：崩溃处理时堆可能已损坏，全部用静态缓冲。
static char g_crashBuf[64 * 1024];

// 当前时间戳（文件命名用）：YYYYMMDD_HHMMSS
static void stampFile(char *out, size_t n) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(out, n, "%04u%02u%02u_%02u%02u%02u",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

// exe 基名（去掉目录和 .exe），与 OpLog 的日志命名规则保持一致。
static void exeBaseNameW(wchar_t *out, int cap) {
    wchar_t exePath[MAX_PATH];
    out[0] = 0;
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return;
    wchar_t *slash = wcsrchr(exePath, L'\\');
    const wchar_t *base = slash ? slash + 1 : exePath;
    _snwprintf_s(out, cap, _TRUNCATE, L"%s", base);
    wchar_t *dot = wcsrchr(out, L'.');
    if (dot) *dot = 0;
}

// 从 temp/ 下最新的 <exe 基名>_ops-*.log 读取最后 maxLines 行到 out（静态缓冲）。
// 全程走宽字符 API，中文安装目录下也能定位日志。
static void appendOpsTail(char *out, size_t cap) {
    size_t used = strlen(out);
    if (used >= cap - 2) return;

    wchar_t dirW[MAX_PATH], baseW[MAX_PATH];
    tempDir(dirW, MAX_PATH);
    exeBaseNameW(baseW, MAX_PATH);
    if (!dirW[0] || !baseW[0]) return;

    wchar_t patternW[MAX_PATH * 2];
    swprintf_s(patternW, sizeof(patternW) / sizeof(wchar_t), L"%s\\%s_ops-*.log", dirW, baseW);

    // 找最新的 ops 日志（修改时间最大）
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(patternW, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    wchar_t newestW[MAX_PATH * 2] = L"";
    ULONGLONG newestFt = 0;
    do {
        ULONGLONG ft = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32)
                     | fd.ftLastWriteTime.dwLowDateTime;
        if (ft >= newestFt) {
            newestFt = ft;
            swprintf_s(newestW, sizeof(newestW) / sizeof(wchar_t), L"%s\\%s", dirW, fd.cFileName);
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    if (!newestW[0]) return;

    // 读尾部（静态缓冲），保留最后 30 行
    FILE *f = nullptr;
    if (_wfopen_s(&f, newestW, L"rb") != 0 || !f) return;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    long readFrom = size - (long)(sizeof(g_crashBuf) / 2);
    if (readFrom < 0) readFrom = 0;
    fseek(f, readFrom, SEEK_SET);
    size_t n = fread(g_crashBuf, 1, sizeof(g_crashBuf) / 2, f);
    g_crashBuf[n] = 0;
    fclose(f);

    // 截取最后 30 行
    int lines = 0;
    char *p = g_crashBuf + strlen(g_crashBuf);
    while (p > g_crashBuf && lines < 30) {
        --p;
        if (*p == '\n') ++lines;
    }
    if (lines >= 30 && p > g_crashBuf) ++p;
    size_t add = strlen(p);
    if (used + add + 64 < cap) {
        strncat(out, "\n--- recent operations (last 30 lines) ---\n", cap - used - 1);
        used = strlen(out);
        if (used + add < cap) strncat(out, p, cap - used - 1);
    }
}

// 写入本次崩溃的独立文本文件（时间戳命名，不追加、不截断），
// 内容：异常信息 + 构建标识 + 栈地址 + 操作日志尾部。
static void writeCrashFile(const char *timeStamp, unsigned code, const void *addr,
                           void **bt, int n, const char *timeBuf) {
    wchar_t dirW[MAX_PATH];
    tempDir(dirW, MAX_PATH);
    if (!dirW[0]) return;
    CreateDirectoryW(dirW, NULL);
    wchar_t pathW[MAX_PATH * 2];
    // %hs = narrow (char*) argument in a wide format string.
    swprintf_s(pathW, sizeof(pathW) / sizeof(wchar_t), L"%s\\%hs_crash_%hs.txt",
               dirW, g_tagCStr(), timeStamp);

    static char body[128 * 1024];
    int used = snprintf(body, sizeof(body),
                        "=== crash 0x%08X at %p%s (exe %s)\n",
                        code, addr, timeBuf, g_tagCStr());
    for (int i = 0; i < n && used < (int)sizeof(body) - 32; ++i)
        used += snprintf(body + used, sizeof(body) - used, "  frame%02d = %p\n", i, bt[i]);
    appendOpsTail(body, sizeof(body));

    FILE *f = nullptr;
    if (_wfopen_s(&f, pathW, L"w") == 0 && f) {
        fputs(body, f);
        fclose(f);
    }
}

// 保留最近 kMaxDumps 个 dmp，旧的删除。
static const int kMaxDumps = 3;
static void pruneOldDumps() {
    wchar_t dirW[MAX_PATH];
    tempDir(dirW, MAX_PATH);
    if (!dirW[0]) return;
    wchar_t patternW[MAX_PATH * 2];
    swprintf_s(patternW, sizeof(patternW) / sizeof(wchar_t), L"%s\\%hs_crash_*.dmp",
               dirW, g_tagCStr());

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(patternW, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    int count = 0;
    do {
        ++count;
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    if (count <= kMaxDumps) return;
    // 删最旧的（按修改时间升序，删到只剩 kMaxDumps）
    hFind = FindFirstFileW(patternW, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    // 简单方案：收集所有文件，按时间排序后删除最旧
    struct DmpFile { wchar_t name[MAX_PATH]; ULONGLONG ft; };
    static DmpFile dumps[64];
    int dn = 0;
    do {
        if (dn < 64) {
            _snwprintf_s(dumps[dn].name, MAX_PATH, _TRUNCATE, L"%s", fd.cFileName);
            dumps[dn].ft = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32)
                         | fd.ftLastWriteTime.dwLowDateTime;
            ++dn;
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    // 冒泡：按时间升序，删最早的 (count - kMaxDumps) 个
    for (int i = 0; i < dn - 1; ++i)
        for (int j = 0; j < dn - 1 - i; ++j)
            if (dumps[j].ft > dumps[j + 1].ft) {
                DmpFile t = dumps[j];
                dumps[j] = dumps[j + 1];
                dumps[j + 1] = t;
            }
    int toDelete = dn - kMaxDumps;
    for (int i = 0; i < toDelete && i < dn; ++i) {
        wchar_t fullW[MAX_PATH * 2];
        swprintf_s(fullW, sizeof(fullW) / sizeof(wchar_t), L"%s\\%s", dirW, dumps[i].name);
        DeleteFileW(fullW);
    }
}

LONG WINAPI CrashHandler(PEXCEPTION_POINTERS ep) {
    unsigned code = (unsigned)ep->ExceptionRecord->ExceptionCode;
    const void *addr = ep->ExceptionRecord->ExceptionAddress;

    // Build identity: compile time of this binary + file timestamp.
    char timeBuf[64] = "";
    {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW(exePath, GetFileExInfoStandard, &fad)) {
            FILETIME ft = fad.ftLastWriteTime;
            FILETIME local;
            FileTimeToLocalFileTime(&ft, &local);
            SYSTEMTIME st;
            FileTimeToSystemTime(&local, &st);
            snprintf(timeBuf, sizeof(timeBuf),
                     " (%04u-%02u-%02u %02u:%02u:%02u)",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        }
    }

    // Stack addresses - resolve against the release's .map file
    // (fixed base /DYNAMICBASE:NO makes the addresses stable).
    void *bt[32];
    int n = (int)RtlCaptureStackBackTrace(0, 32, bt, nullptr);

    char ts[32];
    stampFile(ts, sizeof(ts));

    // 文本报告（时间戳命名，独立文件，含操作日志尾部）
    writeCrashFile(ts, code, addr, bt, n, timeBuf);

    // MiniDump in temp/ (timestamped, never overwritten).
    {
        wchar_t dir[MAX_PATH];
        tempDir(dir, MAX_PATH);
        wchar_t dmpPath[MAX_PATH + 32];
        // %hs = narrow (char*) argument in a wide format string.
        swprintf_s(dmpPath, L"%s\\%hs_crash_%hs.dmp",
                   dir, g_tagCStr(), ts);
        HANDLE hFile = CreateFileW(dmpPath, GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei = {};
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = ep;
            mei.ClientPointers = FALSE;
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                              hFile, MiniDumpNormal, &mei, nullptr, nullptr);
            CloseHandle(hFile);
        }
    }
    pruneOldDumps();

    // Tell the user where the crash data went (bilingual; i18n may not
    // be initialized yet at crash time). MessageBoxW + \u escapes keep
    // the text correct on every ANSI code page (MessageBoxA would render
    // UTF-8 bytes as GBK mojibake on Chinese Windows).
    wchar_t msg[1024];
    swprintf_s(msg,
        L"%hs crashed.\n\n"
        L"Exception code: 0x%08X\nAddress: %p\n\n"
        L"Crash data saved to the temp folder next to this program:\n"
        L"  temp\\%hs_crash_*.dmp / .txt\n"
        L"Please send that folder's crash files to the developer.\n\n"
        L"%hs \u5d29\u6e83\u3002\n"
        L"\u5f02\u5e38\u4ee3\u7801: 0x%08X\n\u5730\u5740: %p\n\n"
        L"\u5d29\u6e83\u6570\u636e\u5df2\u4fdd\u5b58\u5728\u672c\u7a0b\u5e8f\u540c\u7ea7\u76ee\u5f55\u7684 temp \u6587\u4ef6\u5939:\n"
        L"  temp\\%hs_crash_*.dmp / .txt\n"
        L"\u8bf7\u5c06\u8be5\u6587\u4ef6\u5939\u4e2d\u7684\u5d29\u6e83\u6587\u4ef6\u53d1\u7ed9\u5f00\u53d1\u8005\u3002",
        g_tagCStr(), code, addr, g_tagCStr(),
        g_tagCStr(), code, addr, g_tagCStr());
    MessageBoxW(nullptr, msg, L"Pecia - Crash Report",
                MB_OK | MB_ICONERROR | MB_TASKMODAL);
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

void CrashReport::install(const char *exeTag) {
    if (exeTag)
        snprintf(g_tag, sizeof(g_tag), "%s", exeTag);
    SetUnhandledExceptionFilter(CrashHandler);
}
