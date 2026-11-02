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

// Find "key":"value" and decode value. The colon must follow the key
// immediately (whitespace aside), and occurrences that fail that test are
// skipped: a changelog that mentions "version" inside its own text would
// otherwise answer for the real key.
bool findJsonString(const std::string &body, const char *key, std::string &out) {
    std::string needle = std::string("\"") + key + "\"";
    size_t from = 0;
    while (true) {
        size_t pos = body.find(needle, from);
        if (pos == std::string::npos) return false;
        from = pos + needle.size();
        size_t colon = from;
        while (colon < body.size() && (body[colon] == ' ' || body[colon] == '\t' ||
                                       body[colon] == '\r' || body[colon] == '\n')) ++colon;
        if (colon >= body.size() || body[colon] != ':') continue;
        size_t q = body.find_first_not_of(" \t\r\n", colon + 1);
        if (q == std::string::npos || body[q] != '"') continue;
        if (decodeJsonString(body, q, out)) return true;
        return false;   // right key, broken value: do not hunt further
    }
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

// Flat "key":"value" manifest reader. `version` and at least one package URL
// are mandatory: without a URL the check can be displayed but never acted on,
// which is worse than reporting the manifest as unusable.
bool parseManifest(const std::string &json, UpdateManifest &m) {
    if (!findJsonString(json, "version", m.version) || m.version.empty()) return false;
    findJsonString(json, "published", m.published);
    findJsonString(json, "notes", m.notes);
    findJsonString(json, "notes_en", m.notesEn);
    findJsonString(json, "url_gitee", m.urlGitee);
    findJsonString(json, "url_github", m.urlGithub);
    return !m.urlGitee.empty() || !m.urlGithub.empty();
}

// Whole file into a string (wide path, so Chinese install dirs work).
bool readWholeFile(const std::string &pathUtf8, std::string &out) {
    out.clear();
    std::wstring w = utf8ToWide(pathUtf8);
    if (w.empty()) return false;
    HANDLE h = CreateFileW(w.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    char buf[16384];
    DWORD got = 0;
    while (ReadFile(h, buf, sizeof(buf), &got, nullptr) && got > 0) out.append(buf, got);
    bool ok = (got == 0);   // ReadFile returning 0 bytes is EOF, the normal exit
    CloseHandle(h);
    return ok;
}

// Seconds since the file was last written; -1 when it cannot be read.
long long fileAgeSeconds(const std::string &pathUtf8) {
    std::wstring w = utf8ToWide(pathUtf8);
    if (w.empty()) return -1;
    HANDLE h = CreateFileW(w.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return -1;
    FILETIME ft;
    long long age = -1;
    if (GetFileTime(h, nullptr, nullptr, &ft)) {
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        ULARGE_INTEGER a, b;
        a.HighPart = now.dwHighDateTime;    a.LowPart = now.dwLowDateTime;
        b.HighPart = ft.dwHighDateTime;     b.LowPart = ft.dwLowDateTime;
        if (a.QuadPart >= b.QuadPart)
            age = (long long)((a.QuadPart - b.QuadPart) / 10000000ULL);
    }
    CloseHandle(h);
    return age;
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
// A 4xx/5xx is reported as a failure with the status in `err` -- an error page
// is not a manifest, and saving one as "update.zip" would only be caught later
// by the hash check with a far less useful message.
bool httpGetBody(const std::string &url, int timeoutMs, std::string &out, std::string &err,
                 DWORD *outStatus = nullptr) {
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
            if (status >= 400) {
                err = "HTTP " + std::to_string(status);
                if (outStatus) *outStatus = status;
                WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return false;
            }
            if (outStatus) *outStatus = status;
        }
        char buf[16384];
        out.clear();
        bool readOk = true;
        for (;;) {
            DWORD chunk = 0;
            // TRUE + 0 bytes = the server finished sending. FALSE = the
            // connection broke mid-body: a truncated manifest must not be
            // handed to the JSON parser as if it were complete.
            if (!WinHttpReadData(hR, buf, sizeof(buf), &chunk)) { readOk = false; break; }
            if (chunk == 0) break;
            out.append(buf, chunk);
        }
        WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
        if (!readOk) { err = "download interrupted"; return false; }
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
            if (status >= 400) {
                // Never save an error page as the package.
                err = "HTTP " + std::to_string(status);
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
        DWORD written = 0;
        long long received = 0;
        bool good = true;
        bool readOk = true;
        if (progress) progress(received, total);
        for (;;) {
            DWORD chunk = 0;
            // TRUE + 0 bytes = the body ended normally. FALSE = the link broke;
            // the old loop treated that as success, so a half-downloaded zip
            // reached the updater and the UI claimed "download complete".
            if (!WinHttpReadData(hR, buf, sizeof(buf), &chunk)) { readOk = false; break; }
            if (chunk == 0) break;
            if (!WriteFile(hf, buf, chunk, &written, nullptr) || written != chunk) {
                good = false;
                break;
            }
            received += chunk;
            if (progress) progress(received, total);
        }
        CloseHandle(hf);
        WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
        if (!good) {
            err = "write failed";
            DeleteFileW(savePathW.c_str());
            return false;
        }
        if (!readOk) {
            err = "download interrupted";
            DeleteFileW(savePathW.c_str());
            return false;
        }
        // A clean EOF is not proof of a complete file: when the server sent a
        // Content-Length, the received size must match it. CDN chunked
        // responses have no length (total == 0) and rely on the EOF above.
        if (total > 0 && received != total) {
            err = "truncated download (" + std::to_string(received) + " of " +
                  std::to_string(total) + " bytes)";
            DeleteFileW(savePathW.c_str());
            return false;
        }
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

int compareVersions(const std::string &cur, const std::string &latest) {
    int cm[3], lm[3];
    parseVersion(cur, cm[0], cm[1], cm[2]);
    parseVersion(latest, lm[0], lm[1], lm[2]);
    for (int i = 0; i < 3; ++i) {
        if (lm[i] != cm[i]) return lm[i] > cm[i] ? 1 : -1;
    }
    return 0;
}

std::string updateCachePathUtf8() {
    return getExeDirUtf8() + "update.cache.json";
}

bool readCachedManifest(UpdateManifest &out, long long &ageSeconds) {
    ageSeconds = -1;
    std::string raw;
    if (!readWholeFile(updateCachePathUtf8(), raw)) return false;
    UpdateManifest m;
    if (!parseManifest(raw, m)) return false;
    ageSeconds = fileAgeSeconds(updateCachePathUtf8());
    out = m;
    return true;
}

// Writes the fetched JSON verbatim, so the cache keeps every byte the server
// sent (re-serialising it would drift from what the mirrors actually serve).
static void writeCacheManifest(const std::string &rawJson) {
    std::string path = updateCachePathUtf8();
    std::wstring w = utf8ToWide(path);
    if (w.empty()) return;
    FILE *f = _wfopen(w.c_str(), L"wb");
    if (!f) return;                 // read-only install dir: check still works
    fwrite(rawJson.data(), 1, rawJson.size(), f);
    fclose(f);
}

UpdateCheckResult checkUpdate(const std::string &currentVersion,
                              const std::string &urlGitee,
                              const std::string &urlGithub, int source) {
    UpdateCheckResult r;
    const std::string urls[2] = { urlGitee, urlGithub };
    const int order[2] = { source == 1 ? 1 : 0, source == 1 ? 0 : 1 };
    std::string lastErr = "没有可用的更新清单地址";

    for (int k = 0; k < 2; ++k) {
        const int idx = order[k];
        if (urls[idx].empty()) continue;          // an unset source is skipped, not tried
        std::string body, err;
        if (!httpGetBody(urls[idx], 10000, body, err)) { lastErr = err; continue; }
        UpdateManifest m;
        if (!parseManifest(body, m)) { lastErr = "更新清单无法解析"; continue; }
        writeCacheManifest(body);
        r.online     = true;
        r.sourceUsed = idx;
        r.manifest   = m;
        r.hasUpdate  = compareVersions(currentVersion, m.version) > 0;
        return r;
    }

    // Neither source answered. The cache may still say "a newer version was seen
    // last time", but it must never be reported as "up to date" -- after the user
    // upgrades, the cache in the install dir is the one the PREVIOUS build wrote.
    UpdateManifest cached;
    long long age = -1;
    if (readCachedManifest(cached, age)) {
        r.fromCache   = true;
        r.cacheAgeSec = age;
        r.manifest    = cached;
        r.hasUpdate   = compareVersions(currentVersion, cached.version) > 0;
        return r;
    }
    r.error = lastErr;
    return r;
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

    // 4. close this installation's processes: Pecia + the Lua console + the AI
    //    chat window. Scope by ExecutablePath, NOT by image name: the old
    //    "taskkill /f /im Pecia.exe" killed every Pecia.exe on the machine
    //    (this program allows multi-instance use, and another copy lives in
    //    another directory), and its retry loop had no bound - a process that
    //    refused to die hung the updater forever.
    b += L("echo [1/6] Closing Pecia...");
    b += L(">>\"!LOG!\" echo [1/6] closing");
    b += L("set \"TRIES=0\"");
    b += L(":waitkill");
    b += L("set /a TRIES+=1");
    b += L("if !TRIES! GTR 10 goto killfail");
    b += L("powershell -NoProfile -ExecutionPolicy Bypass -Command \"$ps=@(Get-CimInstance Win32_Process | Where-Object { @('Pecia.exe','PeciaLua.exe','PeciaAIChat.exe') -contains $_.Name -and $_.ExecutablePath -and ([IO.Path]::GetDirectoryName($_.ExecutablePath) -ieq $env:APPD) }); foreach($p in $ps){ Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue }; if($ps.Count -gt 0){ exit 1 } else { exit 0 }\"");
    b += L("if errorlevel 1 (timeout /t 1 /nobreak >nul & goto waitkill)");
    b += L("goto killed");
    b += L(":killfail");
    b += L(">>\"!LOG!\" echo could not close every process of this installation after 10 tries");
    b += L("set \"FAIL=1\"");
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

    // 6. clean up ONLY on success. Deleting the zip unconditionally (what this
    // script used to do) throws away the one artefact a retry needs: a failed
    // robocopy left the user with neither a new version nor a package to retry
    // from, and the next attempt re-downloads 7 MB before failing the same way.
    b += L("echo [5/6] Cleaning up...");
    b += L("if \"!FAIL!\"==\"0\" (");
    b += L("  rmdir /s /q \"!EXT!\" 2>nul");
    b += L("  del /q \"!ZIP!\" 2>nul");
    b += L(") else (");
    b += L("  >>\"!LOG!\" echo kept for retry: !ZIP!");
    b += L("  echo Update failed - the package was kept for a retry.");
    b += L(")");

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
