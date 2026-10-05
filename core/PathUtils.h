// PathUtils.h - shared path utility functions.
//
// exeDir() resolves the directory containing the running executable so
// that lang/, theme/, script/, image/ resources are found next to the
// exe regardless of the launch working directory (Explorer double-click,
// IDE, Start menu, etc.).  Previously each caller (Config, I18n, Theme,
// ScriptManager) had its own static copy; this header centralises them.
#pragma once

#include <string>
#include <filesystem>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <FL/filename.H>       // FL_PATH_MAX
#endif

namespace pathutil {

// Returns the directory containing the running executable as a UTF-8
// string (no trailing slash).  Returns an empty string if it cannot be
// determined; callers should then fall back to CWD-relative paths.
inline std::string exeDir() {
#if defined(_WIN32)
    wchar_t wbuf[FL_PATH_MAX] = L"";
    if (GetModuleFileNameW(nullptr, wbuf, FL_PATH_MAX) > 0) {
        int len = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            std::string utf8(len - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, &utf8[0], len, nullptr, nullptr);
            auto p = utf8.find_last_of("\\/");
            if (p != std::string::npos) utf8.resize(p);
            return utf8;
        }
    }
#endif
    return std::string();
}

// Build a std::filesystem::path from a UTF-8 string.
//
// On Windows, std::filesystem::path(const std::string&) interprets the bytes
// using the *ANSI codepage* (GBK on zh-CN systems), NOT UTF-8.  Passing the
// UTF-8 output of exeDir() to it therefore mangles any non-ASCII component:
// "你好" (E4 BD A0 E5 A5 BD) becomes "浣犲ソ" and Config then creates a bogus
// directory for settings.ini.  Always go through this helper instead.
inline std::filesystem::path fromUtf8(const std::string &utf8) {
#if defined(_WIN32)
    if (utf8.empty()) return std::filesystem::path();
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    if (len <= 0) return std::filesystem::path(utf8);  // conversion failed: last resort
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &wide[0], len);
    return std::filesystem::path(std::move(wide));
#else
    return std::filesystem::path(utf8);
#endif
}

// exeDir() as a properly-encoded filesystem::path (falls back to CWD).
inline std::filesystem::path exeDirPath() {
    std::string dir = exeDir();
    if (dir.empty()) return std::filesystem::current_path();
    return fromUtf8(dir);
}

// Is this a Markdown document? (.md / .markdown, case-insensitive.)
// Drives "Options > Interface & System > Open .md files with preview", which
// must fire for .md and .markdown but never for .txt, .cpp or an extension-less
// path.
//
// Deliberately ASCII-only, NOT std::filesystem::path::extension(): on Windows
// that constructor reinterprets UTF-8 bytes as ANSI(GBK) (see fromUtf8 above),
// which mangles non-ASCII path components. Extension matching only cares about
// the last '.' and ASCII letters, so no decoding is needed -- and none can go
// wrong.
//
// The directory part is skipped so "C:\\my.dir\\notes" is not treated as a
// ".dir" file. A trailing dot ("notes.") is not an extension either.
inline bool isMarkdownPath(const std::string &path) {
    const auto sep = path.find_last_of("\\/");
    const size_t base = (sep == std::string::npos) ? 0 : sep + 1;
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos || dot < base || dot + 1 >= path.size())
        return false;
    std::string ext = path.substr(dot);        // includes the leading '.'
    for (char &c : ext)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return ext == ".md" || ext == ".markdown";
}

} // namespace pathutil
