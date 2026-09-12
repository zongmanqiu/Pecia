// Updater.cpp - self-update support for Pecia (Windows x64).
// See Updater.h for the high-level flow. FLTK-free on purpose.
#include "Updater.h"

#include <windows.h>
#include <winhttp.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <wchar.h>   // _wtoi64 (Content-Length parsing)

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")

namespace {

// UTF-8 -> wide (WinHTTP / file APIs expect wide strings). Same care as
// AiApiClient: a naive std::wstring(s.begin(), s.end()) mangles non-ASCII.
std::wstring utf8ToWide(const std::string &s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                                (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                        (int)s.size(), &w[0], n);
    return w;
}

// ---------------------------------------------------------------------------
// Minimal JSON string decoder (copied from AiApiClient, standalone here).
// `i` must point at the opening quote; on success `out` holds the decoded
// string and `i` is past the closing quote.
// ---------------------------------------------------------------------------
bool decodeJsonString(const std::string &body, size_t &i, std::string &out) {
    if (i >= body.size() || body[i] != '"') return false;
    ++i;
    out.clear();
    while (i < body.size()) {
        char c = body[i];
        if (c == '"') { ++i; return true; }
        if (c == '\\') {
            if (i + 1 >= body.size()) return false;
            char e = body[i + 1];
            i += 2;
            switch (e) {
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case '/': out += '/';  break;
                case '"': out += '"';  break;
                case '\\': out += '\\'; break;
                case 'u': {
                    if (i + 4 > body.size()) return false;
                    unsigned cp = 0;
                    for (int k = 0; k < 4; ++k) {
                        char h = body[i + k];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                        else return false;
                    }
                    i += 4;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (i + 6 > body.size() || body[i] != '\\' || body[i + 1] != 'u')
                            return false;
                        unsigned lo = 0;
                        for (int k = 0; k < 4; ++k) {
                            char h = body[i + 2 + k];
                            lo <<= 4;
                            if (h >= '0' && h <= '9') lo |= (unsigned)(h - '0');
                            else if (h >= 'a' && h <= 'f') lo |= (unsigned)(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') lo |= (unsigned)(h - 'A' + 10);
                            else return false;
                        }
                        if (lo < 0xDC00 || lo > 0xDFFF) return false;
                        i += 6;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return false;
                    }
                    if (cp < 0x80) out += (char)cp;
                    else if (cp < 0x800) {
                        out += (char)(0xC0 | (cp >> 6));
                        out += (char)(0x80 | (cp & 0x3F));
                    } else if (cp < 0x10000) {
                        out += (char)(0xE0 | (cp >> 12));
                        out += (char)(0x80 | ((cp >> 6) & 0x3F));
                        out += (char)(0x80 | (cp & 0x3F));
                    } else {
                        out += (char)(0xF0 | (cp >> 18));
                        out += (char)(0x80 | ((cp >> 12) & 0x3F));
                        out += (char)(0x80 | ((cp >> 6) & 0x3F));
                        out += (char)(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: return false;
            }
        } else {
            out += c;
            ++i;
        }
    }
    return false;
}

// Find "key":"value" and decode value.
bool findJsonString(const std::string &body, const char *key, std::string &out) {
    std::string needle = std::string("\"") + key + "\"";
    size_t pos = body.find(needle);
    if (pos == std::string::npos) return false;
    pos = body.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    size_t q = body.find_first_not_of(" \t\r\n", pos + 1);
    if (q == std::string::npos || body[q] != '"') return false;
    return decodeJsonString(body, q, out);
}

// Parse "v1.2.3" / "1.2" into up to three ints.
void parseVersion(const std::string &v, int &maj, int &min, int &pat) {
    maj = min = pat = 0;
    std::string s = v;
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s = s.substr(1);
    int *dst[3] = { &maj, &min, &pat };
    size_t i = 0;
    int part = 0;
    while (i < s.size() && part < 3) {
        int val = 0;
        bool any = false;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') { val = val * 10 + (s[i] - '0'); any = true; ++i; }
        if (any) *dst[part] = val;
        ++part;
        if (i < s.size() && s[i] == '.') ++i; else break;
    }
}

// Returns 1 if `latest` is newer than `cur`, -1 if older, 0 if equal.
int compareVersion(const std::string &cur, const std::string &latest) {
    int cm[3], lm[3];
    parseVersion(cur, cm[0], cm[1], cm[2]);
    parseVersion(latest, lm[0], lm[1], lm[2]);
    for (int i = 0; i < 3; ++i) {
        if (lm[i] != cm[i]) return lm[i] > cm[i] ? 1 : -1;
    }
    return 0;
}

// Parse [http|https]://host[:port]/path.
bool splitUrl(const std::string &url, std::string &host, std::string &path,
              bool &tls, INTERNET_PORT &port) {
    std::string u = url;
    if (u.rfind("https://", 0) == 0)      { tls = true;  u = u.substr(8); }
    else if (u.rfind("http://", 0) == 0)  { tls = false; u = u.substr(7); }
    else                                  { tls = true; }
    size_t colon = u.find(':');
    size_t slash = u.find('/');
    if (colon != std::string::npos && (slash == std::string::npos || colon < slash)) {
        long p = atol(u.c_str() + colon + 1);
        if (p > 0 && p <= 65535) port = (INTERNET_PORT)p;
        u = u.substr(0, colon) + (slash != std::string::npos ? u.substr(slash) : std::string());
        slash = u.find('/');
    }
    if (slash != std::string::npos) { host = u.substr(0, slash); path = u.substr(slash); }
    else                           { host = u; path = "/"; }
    if (!tls && port == INTERNET_DEFAULT_HTTPS_PORT) port = INTERNET_DEFAULT_HTTP_PORT;
    return !host.empty();
}

// Follow up to `maxRedirects` HTTP 3xx redirects by re-issuing the request to
// the Location header. Gitee's release API / download CDN may redirect, and the
// desktop WinHTTP SDK on this machine does not expose WINHTTP_OPTION_ENABLE_
// REDIRECTS, so we handle it ourselves (also avoids silently following endless
// redirect loops). Returns false (err set) on a redirect we cannot follow.
bool followRedirect(const std::wstring &wloc, std::string &host, std::string &path,
                    bool &tls, INTERNET_PORT &port) {
    // Location may be relative ("/x/y") or absolute ("https://...").
    std::string loc;
    int n = WideCharToMultiByte(CP_UTF8, 0, wloc.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return false;
    loc.resize(n - 1);
    WideCharToMultiByte(CP_UTF8, 0, wloc.c_str(), -1, &loc[0], n, nullptr, nullptr);
    if (loc.empty()) return false;
    if (loc[0] == '/') {
        path = loc;                       // same host, just a new path
        return true;
    }
    return splitUrl(loc, host, path, tls, port);
}

// GET `url`, body into `out`. Synchronous, blocking. Manually follows
// redirects (no dependency on the missing WINHTTP_OPTION_ENABLE_REDIRECTS).
bool httpGetBody(const std::string &url, int timeoutMs, std::string &out, std::string &err) {
    std::string cur = url;
    for (int hop = 0; hop <= 8; ++hop) {
        std::string host, path; bool tls; INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
        if (!splitUrl(cur, host, path, tls, port)) { err = "invalid url"; return false; }
        std::wstring whost = utf8ToWide(host), wpath = utf8ToWide(path);

        HINTERNET hS = WinHttpOpen(L"PeciaUpdater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hS) { err = "WinHttpOpen failed"; return false; }
        HINTERNET hC = WinHttpConnect(hS, whost.c_str(), port, 0);
        if (!hC) { err = "WinHttpConnect failed"; WinHttpCloseHandle(hS); return false; }
        HINTERNET hR = WinHttpOpenRequest(hC, L"GET", wpath.c_str(), nullptr,
                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                          tls ? WINHTTP_FLAG_SECURE : 0);
        if (!hR) { err = "WinHttpOpenRequest failed"; WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return false; }

        WinHttpSetTimeouts(hR, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

        if (!WinHttpSendRequest(hR, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0)) {
            err = "WinHttpSendRequest failed (timeout or network)";
            WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return false;
        }
        if (!WinHttpReceiveResponse(hR, nullptr)) {
            err = "WinHttpReceiveResponse failed";
            WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return false;
        }
        DWORD status = 0, sz = sizeof(status);
        if (WinHttpQueryHeaders(hR, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, nullptr)) {
            if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                wchar_t loc[4096]; DWORD lsz = sizeof(loc);
                if (WinHttpQueryHeaders(hR, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                        loc, &lsz, nullptr) &&
                    followRedirect(std::wstring(loc, lsz / sizeof(wchar_t) ? lsz / sizeof(wchar_t) : 0),
                                   host, path, tls, port)) {
                    // Rebuild an absolute URL for the next hop.
                    std::string scheme = tls ? "https://" : "http://";
                    int p = (tls && port == INTERNET_DEFAULT_HTTPS_PORT) ? -1
                            : (port == INTERNET_DEFAULT_HTTP_PORT ? -1 : (int)port);
                    cur = scheme + host + (p > 0 ? (":" + std::to_string(p)) : "") + path;
                    WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
                    continue;
                }
                err = "server redirected but no Location header";
                WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return false;
            }
        }
        char buf[16384];
        DWORD read = 0;
        out.clear();
        while (WinHttpReadData(hR, buf, sizeof(buf), &read) && read > 0) {
            out.append(buf, read);
            read = 0;
        }
        WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
        return true;
    }
    err = "too many redirects";
    return false;
}

// GET `url`, stream body to a file opened with a wide (Chinese-safe) path.
// Manually follows redirects like httpGetBody.
bool httpDownload(const std::string &url, const std::wstring &savePathW,
                  int timeoutMs, std::string &err,
                  const DownloadProgress &progress) {
    std::string cur = url;
    for (int hop = 0; hop <= 8; ++hop) {
        std::string host, path; bool tls; INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
        if (!splitUrl(cur, host, path, tls, port)) { err = "invalid url"; return false; }
        std::wstring whost = utf8ToWide(host), wpath = utf8ToWide(path);

        HINTERNET hS = WinHttpOpen(L"PeciaUpdater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hS) { err = "WinHttpOpen failed"; return false; }
        HINTERNET hC = WinHttpConnect(hS, whost.c_str(), port, 0);
        if (!hC) { err = "WinHttpConnect failed"; WinHttpCloseHandle(hS); return false; }
        HINTERNET hR = WinHttpOpenRequest(hC, L"GET", wpath.c_str(), nullptr,
                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                          tls ? WINHTTP_FLAG_SECURE : 0);
        if (!hR) { err = "WinHttpOpenRequest failed"; WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return false; }

        WinHttpSetTimeouts(hR, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

        if (!WinHttpSendRequest(hR, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0)) {
            err = "WinHttpSendRequest failed (timeout or network)";
            WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return false;
        }
        if (!WinHttpReceiveResponse(hR, nullptr)) {
            err = "WinHttpReceiveResponse failed";
            WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return false;
        }
        DWORD status = 0, sz = sizeof(status);
        if (WinHttpQueryHeaders(hR, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, nullptr)) {
            if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                wchar_t loc[4096]; DWORD lsz = sizeof(loc);
                if (WinHttpQueryHeaders(hR, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                        loc, &lsz, nullptr) &&
                    followRedirect(std::wstring(loc, lsz / sizeof(wchar_t) ? lsz / sizeof(wchar_t) : 0),
                                   host, path, tls, port)) {
                    std::string scheme = tls ? "https://" : "http://";
                    int p = (tls && port == INTERNET_DEFAULT_HTTPS_PORT) ? -1
                            : (port == INTERNET_DEFAULT_HTTP_PORT ? -1 : (int)port);
                    cur = scheme + host + (p > 0 ? (":" + std::to_string(p)) : "") + path;
                    WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
                    continue;
                }
                err = "server redirected but no Location header";
                WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return false;
            }
        }
        HANDLE hf = CreateFileW(savePathW.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hf == INVALID_HANDLE_VALUE) {
            err = "cannot open save file (check write permission)";
            WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return false;
        }
        // Total size (0 = unknown): lets the UI show a real percentage
        // instead of a spinner. Redirect targets (CDN) usually send it.
        long long total = 0;
        {
            wchar_t clen[64]; DWORD clsz = sizeof(clen);
            if (WinHttpQueryHeaders(hR, WINHTTP_QUERY_CONTENT_LENGTH,
                                    WINHTTP_HEADER_NAME_BY_INDEX, clen, &clsz, nullptr)) {
                std::wstring ws(clen, clsz / sizeof(wchar_t));
                total = _wtoi64(ws.c_str());
            }
        }
        char buf[16384];
        DWORD read = 0, written = 0;
        long long received = 0;
        bool good = true;
        if (progress) progress(received, total);
        while (WinHttpReadData(hR, buf, sizeof(buf), &read) && read > 0) {
            if (!WriteFile(hf, buf, read, &written, nullptr) || written != read) { good = false; break; }
            received += read;
            if (progress) progress(received, total);
            read = 0;
        }
        CloseHandle(hf);
        if (!good) {
            err = "write failed";
            WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return false;
        }
        WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
        return true;
    }
    err = "too many redirects";
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::string getExeDirUtf8() {
    wchar_t exe[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) return std::string();
    wchar_t *slash = wcsrchr(exe, L'\\');
    if (slash) *(slash + 1) = 0;   // keep trailing backslash
    char buf[MAX_PATH * 2];
    int n = WideCharToMultiByte(CP_UTF8, 0, exe, -1, buf, (int)sizeof(buf), nullptr, nullptr);
    return n > 0 ? std::string(buf) : std::string();
}

UpdateInfo checkForUpdate(const std::string &currentVersion) {
    UpdateInfo info;
    std::string api = "https://gitee.com/api/v5/repos/qiuzongman/pecia/releases/latest";
    std::string body, err;
    if (!httpGetBody(api, 10000, body, err)) { info.error = err; return info; }

    std::string tag;
    if (!findJsonString(body, "tag_name", tag) || tag.empty()) {
        info.error = "cannot parse release info";
        return info;
    }
    info.latest = tag;
    findJsonString(body, "body", info.notes);   // optional changelog

    int cmp = compareVersion(currentVersion, tag);
    if (cmp <= 0) {
        // current >= latest -> nothing newer (cmp==0 equal, cmp<0 dev build ahead)
        info.hasUpdate = false;
        return info;
    }
    // latest is newer: build deterministic download URL (matches pack.bat naming).
    std::string base = tag;
    if (!base.empty() && (base[0] == 'v' || base[0] == 'V')) base = base.substr(1);
    info.url = "https://gitee.com/qiuzongman/pecia/releases/download/" + tag +
               "/Pecia_x64_" + base + ".zip";
    info.hasUpdate = true;
    return info;
}

bool downloadFile(const std::string &url, const std::string &savePathUtf8,
                  DownloadProgress progress) {
    std::wstring wpath = utf8ToWide(savePathUtf8);
    std::string err;
    return httpDownload(url, wpath, 60000, err, progress);
}

// Generates the updater batch script. Modeled on CurSeen's proven recipe,
// with three hardening fixes:
//   1) CRLF line endings - the previous generator emitted LF-only, which cmd
//      parses unreliably (project rule: .bat must be CRLF).
//   2) setlocal enabledelayedexpansion + !VAR! - survives paths containing
//      spaces, &, ( ) or Chinese characters.
//   3) Every step appends to pecia_update.log; the log is deleted only when
//      the update fully succeeds, so a failure leaves evidence instead of
//      silently doing nothing (the old version hid all output with SW_HIDE).
bool writeUpdateBat(const std::string &exeDirUtf8, const std::string &zipPathUtf8,
                    const std::string &batPathUtf8) {
    const std::string app = exeDirUtf8;   // ends with a trailing separator
    const std::string zip = zipPathUtf8;

    // A quoted robocopy destination must NOT end with a separator: inside
    // "...\" the trailing backslash escapes the closing quote, so cmd swallows
    // the rest of the line into the path (robocopy then dies with ERROR 123 /
    // exit 16 and copies nothing). CurSeen avoids this because Python's
    // dirname() has no trailing separator; getExeDirUtf8() does.
    std::string appNoSlash = app;
    while (!appNoSlash.empty() &&
           (appNoSlash.back() == '\\' || appNoSlash.back() == '/'))
        appNoSlash.pop_back();

    // cmd requires CRLF; keep every line terminated that way.
    auto L = [](const std::string &s) { return s + "\r\n"; };

    std::string b;
    b += L("@echo off");
    b += L("chcp 65001 >nul");
    b += L("setlocal enabledelayedexpansion");
    b += L("set \"APP=" + app + "\"");
    b += L("set \"APPD=" + appNoSlash + "\"");
    b += L("set \"ZIP=" + zip + "\"");
    b += L("set \"EXT=" + app + "pecia_update_tmp\"");
    b += L("set \"LOG=" + app + "pecia_update.log\"");
    b += L("set \"FAIL=0\"");
    b += L(">>\"!LOG!\" echo =============================================");
    b += L(">>\"!LOG!\" echo Pecia Updater %DATE% %TIME%");
    b += L(">>\"!LOG!\" echo APP=!APP!");
    b += L(">>\"!LOG!\" echo ZIP=!ZIP!");

    // 4. close the running program (user's step 4)
    b += L("echo [1/6] Closing Pecia...");
    b += L(">>\"!LOG!\" echo [1/6] closing");
    b += L(":waitkill");
    b += L("tasklist /fi \"ImageName eq Pecia.exe\" 2>nul | find \"Pecia.exe\" >nul");
    b += L("if errorlevel 1 goto killed");
    b += L("taskkill /f /im Pecia.exe >nul 2>nul");
    b += L("timeout /t 1 /nobreak >nul");
    b += L("goto waitkill");
    b += L(":killed");
    b += L("timeout /t 3 /nobreak >nul");   // let the file lock settle
    b += L("if exist \"!APP!Pecia.exe\" (>>\"!LOG!\" echo exe-present) else (>>\"!LOG!\" echo exe-MISSING)");

    // 5a. extract (user's step 5)
    b += L("echo [2/6] Extracting...");
    b += L(">>\"!LOG!\" echo [2/6] extracting");
    b += L("if exist \"!EXT!\" rmdir /s /q \"!EXT!\" 2>nul");
    b += L("mkdir \"!EXT!\" 2>nul");
    b += L("powershell -NoProfile -Command \"Expand-Archive -LiteralPath '!ZIP!' -DestinationPath '!EXT!' -Force\" >>\"!LOG!\" 2>&1");
    b += L("if errorlevel 1 set \"FAIL=1\"");
    b += L(">>\"!LOG!\" echo expand-error=!ERRORLEVEL!");

    // locate the payload (zip may wrap everything in one or two folders)
    b += L("echo [3/6] Locating payload...");
    b += L("set \"SRC=!EXT!\"");
    b += L("if not exist \"!SRC!\\Pecia.exe\" (");
    b += L("  for /d %%d in (\"!EXT!\\*\") do (");
    b += L("    if exist \"%%d\\Pecia.exe\" set \"SRC=%%d\"");
    b += L("  )");
    b += L(")");
    b += L("if not exist \"!SRC!\\Pecia.exe\" (");
    b += L("  for /d %%d in (\"!EXT!\\*\") do (");
    b += L("    for /d %%e in (\"%%d\\*\") do (");
    b += L("      if exist \"%%e\\Pecia.exe\" set \"SRC=%%e\"");
    b += L("    )");
    b += L("  )");
    b += L(")");
    b += L(">>\"!LOG!\" echo SRC=!SRC!");
    b += L("if not exist \"!SRC!\\Pecia.exe\" set \"FAIL=1\"");

    // 5b. overwrite the install directory
    b += L("echo [4/6] Copying files...");
    b += L(">>\"!LOG!\" echo [4/6] robocopy");
    b += L("robocopy \"!SRC!\" \"!APPD!\" /e /njh /njs /ndl /nc /ns /np /is /it /r:5 /w:2 >>\"!LOG!\" 2>&1");
    b += L("set \"RC=!ERRORLEVEL!\"");
    b += L(">>\"!LOG!\" echo robocopy-exit=!RC!");
    b += L("if !RC! GEQ 8 set \"FAIL=1\"");
    // belt and braces: the EXE is the one file that may still be locked
    b += L("if exist \"!SRC!\\Pecia.exe\" (");
    b += L("  copy /y \"!SRC!\\Pecia.exe\" \"!APP!Pecia.exe\" >>\"!LOG!\" 2>&1");
    b += L("  if errorlevel 1 set \"FAIL=1\"");
    b += L(")");

    // 6. remove the zip (user's step 6)
    b += L("echo [5/6] Cleaning up...");
    b += L("rmdir /s /q \"!EXT!\" 2>nul");
    b += L("del /q \"!ZIP!\" 2>nul");

    // 7. restart (user's step 7)
    b += L("echo [6/6] Starting Pecia...");
    b += L(">>\"!LOG!\" echo [6/6] start");
    b += L("start \"\" \"!APP!Pecia.exe\"");
    b += L(">>\"!LOG!\" echo start-error=!ERRORLEVEL!");
    b += L(">>\"!LOG!\" echo FAIL=!FAIL!");

    // keep the log only when something went wrong
    b += L("if \"!FAIL!\"==\"0\" del /q \"!LOG!\" 2>nul");
    b += L("del \"%~f0\"");
    b += L("exit");

    // UTF-8 with an explicit BOM so Chinese paths survive chcp 65001.
    std::string out = "\xEF\xBB\xBF" + b;
    std::wstring wbat = utf8ToWide(batPathUtf8);
    FILE *f = _wfopen(wbat.c_str(), L"wb");
    if (!f) return false;
    size_t wrote = fwrite(out.data(), 1, out.size(), f);
    int e = ferror(f);
    fclose(f);
    return (e == 0 && wrote == out.size());
}

void launchUpdater(const std::string &batPathUtf8) {
    std::wstring wbat = utf8ToWide(batPathUtf8);
    ShellExecuteW(nullptr, L"open", wbat.c_str(), nullptr, nullptr, SW_HIDE);
}
