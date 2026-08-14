// clwrap.cpp - MSVC compiler launcher that normalizes the /showIncludes
// output prefix to English so Ninja's dependency parsing works even when
// cl.exe emits a localized (Chinese) prefix. Configure CMake with:
//   -DCMAKE_C_COMPILER_LAUNCHER=<path>\clwrap.exe
//   -DCMAKE_CXX_COMPILER_LAUNCHER=<path>\clwrap.exe
// Without this, header changes never trigger recompiles and stale .obj
// files corrupt class layouts at runtime (intermittent crashes).
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

static std::string joinArgs(int argc, char **argv) {
    std::string cmd;
    // argv[0] = our own path, argv[1] = the compiler (cl.exe) path that
    // the launcher mechanism prepends - skip both and forward the rest.
    for (int i = 2; i < argc; ++i) {
        if (!cmd.empty()) cmd += ' ';
        const std::string a = argv[i];
        bool needQuote = a.empty() || a.find_first_of(" \t\"") != std::string::npos;
        if (needQuote) {
            cmd += '"';
            for (char c : a) {
                if (c == '"') cmd += '\\"';
                else cmd += c;
            }
            cmd += '"';
        } else {
            cmd += a;
        }
    }
    return cmd;
}

// cl.exe's localized /showIncludes prefix in UTF-8:
//   "注意: 包含文件:"  followed by two spaces before the path
static const unsigned char kLocalPrefix[] = {
    0xE6, 0xB3, 0xA8, 0xE6, 0x84, 0x8F, ':', ' ',
    0xE5, 0x8C, 0x85, 0xE5, 0x90, 0xAB,
    0xE6, 0x96, 0x87, 0xE4, 0xBB, 0xB6, ':', ' ', ' '
};
// The prefix Ninja actually compares against (from rules.ninja's
// msvc_deps_prefix): this CMake stores a mangled (mojibake) variant of
// the localized prefix, so the wrapper must emit exactly those bytes:
//   "娉ㄦ剰: 鍖"
static const char kReplPrefix[] = {
    (char)0xE5, (char)0xA8, (char)0x89, (char)0xE3, (char)0x84, (char)0xA6,
    (char)0xE5, (char)0x89, (char)0xB0, ':', ' ',
    (char)0xE9, (char)0x8D, (char)0x96
};

int main(int argc, char **argv) {
    char clPath[MAX_PATH] = "cl.exe";
    {
        char buf[MAX_PATH];
        if (SearchPathA(nullptr, "cl.exe", nullptr, MAX_PATH, buf, nullptr))
            strcpy_s(clPath, buf);
    }
    std::string cmdline = std::string("\"") + clPath + "\" " + joinArgs(argc, argv);

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE rPipe = nullptr, wPipe = nullptr;
    if (!CreatePipe(&rPipe, &wPipe, &sa, 0))
        return 127;
    SetHandleInformation(rPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.hStdOutput = wPipe;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.dwFlags = STARTF_USESTDHANDLES;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessA(clPath, &cmdline[0], nullptr, nullptr, TRUE, 0,
                        nullptr, nullptr, &si, &pi)) {
        fprintf(stderr, "clwrap: cannot start cl.exe\n");
        CloseHandle(rPipe);
        CloseHandle(wPipe);
        return 127;
    }
    CloseHandle(wPipe);

    // Read the child's stdout in chunks, rewriting the localized prefix.
    DWORD exitCode = 0;
    char buf[8192];
    DWORD n = 0;
    size_t carry = 0;   // unconsumed bytes held for prefix matching
    while (ReadFile(rPipe, buf + carry, (DWORD)(sizeof(buf) - carry), &n, nullptr) && n > 0) {
        size_t len = carry + n;
        size_t out = 0;
        size_t i = 0;
        while (i < len) {
            // Try to match the localized prefix at i (need at least its length).
            size_t need = sizeof(kLocalPrefix);
            if (i + need > len) {
                // Short tail that might be a partial prefix: emit the bytes
                // processed so far, then keep the tail for the next chunk.
                if (i > 0) {
                    DWORD w = 0;
                    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, (DWORD)i, &w, nullptr);
                }
                size_t keep = len - i;
                memmove(buf, buf + i, keep);
                carry = keep;
                out = 0;
                break;
            }
            if (memcmp(buf + i, kLocalPrefix, sizeof(kLocalPrefix)) == 0) {
                // Emit the mojibake prefix Ninja expects, then skip the
                // two spaces cl puts between the prefix and the path.
                memcpy(buf + out, kReplPrefix, sizeof(kReplPrefix));
                out += sizeof(kReplPrefix);
                i += sizeof(kLocalPrefix);
                while (i < len && buf[i] == ' ') ++i;
            } else {
                buf[out++] = buf[i++];
            }
        }
        if (out > 0) {
            DWORD written = 0;
            WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, (DWORD)out, &written, nullptr);
        }
    }
    CloseHandle(rPipe);
    // Flush any tail bytes left from the final chunk.
    if (carry > 0) {
        DWORD w = 0;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, (DWORD)carry, &w, nullptr);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)exitCode;
}
