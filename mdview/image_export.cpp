// image_export.cpp - make the "Open in browser" export self-contained.
//
// Deliberately free of md4c / litehtml / FFI dependencies: it only rewrites
// <img src> attributes in an HTML string, so the regression test can link
// this single file (linking preprocess.cpp would drag in both Rust staticlibs,
// whose LTO'd duplicate std symbols make any extra test target noisy).
#include "image_export.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// winhttp.h must follow windows.h: it relies on types declared there.
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

namespace {

std::wstring widen(const std::string& utf8)
{
    if (utf8.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], n);
    while (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

// FNV-1a over the raw URL. 只在"URL 推不出可用文件名"时用于兜底命名，
// 不再参与缓存文件名（历史上缓存名 pecia_img_<hash>.img 必须与容器逐字节
// 一致，如今容器与导出共用 remote_image_file_name()，不再需要这份约定）。
unsigned img_url_hash(const std::string& s)
{
    unsigned h = 2166136261u;
    for (unsigned char c : s) h = (h ^ c) * 16777619u;
    return h;
}

// Minimal entity decoding: md4c escapes "&" as "&amp;" inside attributes, but
// 文件名是从原始 URL 推出来的，所以先解码再取 basename。
std::string decode_attr_entities(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '&') {
            if (s.compare(i, 5, "&amp;") == 0)  { out += '&';  i += 5; continue; }
            if (s.compare(i, 4, "&lt;") == 0)   { out += '<';  i += 4; continue; }
            if (s.compare(i, 4, "&gt;") == 0)   { out += '>';  i += 4; continue; }
            if (s.compare(i, 6, "&quot;") == 0) { out += '"';  i += 6; continue; }
            if (s.compare(i, 5, "&#39;") == 0)  { out += '\''; i += 5; continue; }
            if (s.compare(i, 6, "&apos;") == 0) { out += '\''; i += 6; continue; }
        }
        out += s[i++];
    }
    return out;
}

// Extension whitelist. The cache file is a bare ".img", but browsers pick the
// decoder by extension - an SVG without ".svg" simply does not render.
std::string url_image_ext(const std::string& url)
{
    size_t end = url.find_first_of("?#");
    std::string path = url.substr(0, end == std::string::npos ? url.size() : end);
    size_t slash = path.find_last_of("/\\");
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    if (slash != std::string::npos && dot < slash) return {};
    std::string ext = path.substr(dot);
    for (auto& c : ext) c = (char)tolower((unsigned char)c);
    static const char* kExts[] = { ".png",  ".jpg", ".jpeg", ".gif", ".svg",
                                   ".webp", ".bmp", ".ico",  ".avif" };
    for (const char* e : kExts)
        if (ext == e) return ext;
    return {};
}

// Non-empty regular file? A failed download can leave a truncated cache
// behind, and copying that into the export would bake a broken image in.
bool file_has_bytes(const std::string& path)
{
    HANDLE h = CreateFileW(widen(path).c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    GetFileSizeEx(h, &size);
    CloseHandle(h);
    return size.QuadPart > 0;
}

// One GET attempt over WinHTTP. Returns true only when the response carried a
// 2xx status AND at least one byte reached the file.
//
// Why WinHTTP instead of URLDownloadToFileW: urlmon's downloader proved
// unreliable here - the exact same three URLs failed with E_ABORT / silent
// 0-byte files on one run and succeeded on the next, and a single miss left
// the exported page pointing at a remote URL forever. WinHTTP also lets us
// reject non-2xx bodies (a 404 HTML error page would otherwise be saved as
// "<image>.png" and render as a broken image forever).
// Resolve a Location header against the current absolute URL. Returns false
// only when the result is not something we can re-request.
bool resolve_redirect(const std::string& base, const std::string& location,
                      std::string& out)
{
    if (location.empty()) return false;
    if (location.find("://") != std::string::npos) { out = location; return true; }
    // Protocol-relative: "//host/path" -> inherit the base's scheme.
    if (location.compare(0, 2, "//") == 0) {
        size_t sep = base.find("://");
        if (sep == std::string::npos) return false;
        out = base.substr(0, sep + 1) + location;
        return true;
    }
    // Absolute path: keep scheme://host[:port], swap the path.
    size_t schemeEnd = base.find("://");
    if (schemeEnd == std::string::npos) return false;
    size_t hostEnd = base.find('/', schemeEnd + 3);
    std::string origin = (hostEnd == std::string::npos) ? base : base.substr(0, hostEnd);
    if (location[0] == '/') {
        out = origin + location;
        return true;
    }
    // Relative: replace the last path segment.
    if (hostEnd == std::string::npos) { out = origin + "/" + location; return true; }
    size_t lastSlash = base.find_last_of('/');
    out = base.substr(0, lastSlash + 1) + location;
    return true;
}

// One GET attempt over WinHTTP, following redirects by hand: this SDK build
// has no WINHTTP_OPTION_ENABLE_REDIRECTS, so WinHTTP stops at the 3xx. That
// matters a lot here - gitee's raw endpoint answers 302 to a ~13-minute signed
// URL on raw.giteeusercontent.com, and the signed body is the actual image.
// Returns true only when a 2xx response with at least one byte was written.
//
// Why WinHTTP rather than URLDownloadToFileW: urlmon's downloader proved
// unreliable on this machine - the exact same URLs failed with E_ABORT or
// silently wrote 0 bytes on one run and succeeded on the next, and a single
// miss left the exported page pointing at a remote URL forever. WinHTTP also
// lets us reject non-2xx bodies (a 404 HTML error page would otherwise be
// saved as "<image>.png" and render as a broken image forever).
bool http_get_once(const std::string& url, const std::string& dest)
{
    std::string cur = url;
    for (int hop = 0; hop <= 8; ++hop) {
        URL_COMPONENTS uc;
        ZeroMemory(&uc, sizeof(uc));
        uc.dwStructSize = sizeof(uc);
        std::wstring host(256, L'\0');
        std::wstring path(2048, L'\0');
        uc.lpszHostName = &host[0];  uc.dwHostNameLength = 255;
        uc.lpszUrlPath  = &path[0];  uc.dwUrlPathLength  = 2047;
        if (!WinHttpCrackUrl(widen(cur).c_str(), 0, 0, &uc)) return false;
        host.resize(wcslen(host.c_str()));
        path.resize(wcslen(path.c_str()));
        if (path.empty()) path = L"/";

        // AUTOMATIC_PROXY honours both the system proxy and WinINET's import,
        // the same way core/Updater.cpp and core/AiApiClient.cpp already do it.
        HINTERNET hs = WinHttpOpen(L"Pecia/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hs) return false;
        // Bounded timeouts: the export path runs this on the UI thread, so a
        // black-holed network must fail fast rather than freeze the app.
        WinHttpSetTimeouts(hs, 15000, 15000, 20000, 20000);

        HINTERNET hc = WinHttpConnect(hs, host.c_str(), uc.nPort, 0);
        HINTERNET hq = hc ? WinHttpOpenRequest(hc, L"GET", path.c_str(), nullptr,
                                               WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               uc.nPort == 443 ? WINHTTP_FLAG_SECURE : 0)
                          : nullptr;
        DWORD status = 0;
        bool gotResponse = hq &&
            WinHttpSendRequest(hq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(hq, nullptr);
        if (gotResponse) {
            DWORD statusLen = sizeof(status);
            gotResponse = WinHttpQueryHeaders(hq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                              WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusLen, nullptr) != 0;
        }

        bool ok = false;
        if (gotResponse && status >= 300 && status < 400) {
            // Redirect: read Location, resolve it, retry on the next hop.
            wchar_t loc[4096];
            DWORD locLen = sizeof(loc) / sizeof(wchar_t);
            if (WinHttpQueryHeaders(hq, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                    loc, &locLen, nullptr)) {
                int n = WideCharToMultiByte(CP_UTF8, 0, loc, -1, nullptr, 0, nullptr, nullptr);
                if (n > 0) {
                    std::string location((size_t)n - 1, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, loc, -1, &location[0], n, nullptr, nullptr);
                    if (resolve_redirect(cur, location, cur)) continue;
                }
            }
        } else if (gotResponse && status >= 200 && status < 300) {
            HANDLE f = CreateFileW(widen(dest).c_str(), GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f != INVALID_HANDLE_VALUE) {
                BYTE buf[16384];
                DWORD got = 0, total = 0;
                while (WinHttpReadData(hq, buf, sizeof(buf), &got) && got) {
                    DWORD written = 0;
                    if (!WriteFile(f, buf, got, &written, nullptr)) break;
                    total += got;
                }
                CloseHandle(f);
                ok = (total > 0);
            }
        }
        if (hq) WinHttpCloseHandle(hq);
        if (hc) WinHttpCloseHandle(hc);
        WinHttpCloseHandle(hs);
        if (ok) return true;
        if (gotResponse && status >= 300 && status < 400) continue;  // hop handled
        return false;
    }
    return false;   // too many redirects
}

// A file name is only useful if a human can recognise it. The old scheme
// ("pecia_img_867b86cb.png") is collision-proof but tells the user nothing, and
// a folder full of them is unreadable. Prefer the URL's own basename, and fall
// back to the hash only when the URL has no usable one (e.g. ".../image").
//
// The CACHE file name stays hash-based on purpose: container.cpp derives it
// from the raw URL with the same FNV-1a, and changing it would silently break
// the "reuse what the preview already downloaded" fast path.
std::string sanitize_file_name(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
        if (c < 0x20 || c == '"' || c == '*' || c == '?' || c == '<' ||
            c == '>' || c == '|')
            out += '_';
        else
            out += (char)c;
    }
    // Trailing dots/spaces are illegal on Windows and make CopyFileW fail.
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    return out;
}

// basename of the URL path, extension appended separately. Returns "" when
// the URL carries no recognisable image file name.
std::string url_basename(const std::string& url, const std::string& ext)
{
    size_t end = url.find_first_of("?#");
    std::string path = url.substr(0, end == std::string::npos ? url.size() : end);
    size_t slash = path.find_last_of("/\\");
    std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    base = sanitize_file_name(base);
    if (base.empty()) return {};
    if (base.size() > 60) base.resize(60);
    // 纯数字不是问题：picsum.photos/320/180.jpg 这种"尺寸即文件名"的图床
    // 很常见，180.jpg 比哈希名好得多。真正要挡的只是"看着像版本号"的
    // 名字（v1.2、20240101 之类）—— 那多半是路径末段被误当成了文件名，
    // 这时退回哈希更诚实。仅当"首段是 v + 数字"时才拒。
    if (base.size() >= 2 && (base[0] == 'v' || base[0] == 'V') &&
        isdigit((unsigned char)base[1]))
        return {};
    return base + ext;
}

// Pass 1 of localization, also reused by count_remote_image_srcs(): collect
// the de-duplicated http(s) <img src> attribute values, still HTML-escaped.
// Collecting up front keeps pass 2 free of any cursor bookkeeping across
// replacements, which change the string length.
std::vector<std::string> collect_remote_img_srcs(const std::string& html)
{
    std::vector<std::string> attrs;
    size_t scan = 0;
    while ((scan = html.find("<img", scan)) != std::string::npos) {
        size_t srcPos = html.find("src=\"", scan);
        if (srcPos == std::string::npos) break;
        size_t srcEnd = html.find('"', srcPos + 5);
        if (srcEnd == std::string::npos) break;
        std::string attr = html.substr(srcPos + 5, srcEnd - srcPos - 5);
        scan = srcEnd + 1;
        if ((attr.rfind("http://", 0) == 0 || attr.rfind("https://", 0) == 0) &&
            std::find(attrs.begin(), attrs.end(), attr) == attrs.end())
            attrs.push_back(attr);
    }
    return attrs;
}

}  // namespace

bool default_remote_fetch_impl(const std::string& url, const std::string& dest)
{
    // urlmon failed intermittently on this machine, so retry a couple of
    // times: transient network hiccups must not leave a remote src behind in
    // the exported page. A failed attempt can leave a truncated file - always
    // remove it before the next try, and on final failure, so a broken image
    // never gets copied into the export.
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) Sleep(300 * attempt);
        DeleteFileW(widen(dest).c_str());
        if (http_get_once(url, dest) && file_has_bytes(dest)) return true;
    }
    DeleteFileW(widen(dest).c_str());
    return false;
}

RemoteFetchFn default_remote_fetch()
{
    return &default_remote_fetch_impl;
}

// 让 remote_image_file_name 与容器侧的命名完全一致（同一函数，无第二份实现）。
std::string remote_image_file_name(const std::string& url)
{
    std::string ext = url_image_ext(url);
    if (ext.empty()) return {};            // no trustworthy suffix
    std::string base = url_basename(url, ext);
    if (!base.empty()) return base;
    char fb[80];
    std::snprintf(fb, sizeof(fb), "image_%08x%s", img_url_hash(url), ext.c_str());
    return fb;
}

void localize_remote_images(std::string& html, const std::string& build_dir,
                            RemoteFetchFn fetch)
{
    if (build_dir.empty() || html.find("<img") == std::string::npos) return;

    std::vector<std::string> attrs = collect_remote_img_srcs(html);

    // 图直接落在 build_dir —— 也就是预览/导出的产物目录本身，用 URL 原始
    // 文件名。之前这里是"先下到 temp\*.img 缓存、再拷进 build_dir"，同一张
    // 图存两份，且缓存名是 pecia_img_<hash>.img：既不可读也打不开，用户看得
    // 一头雾水。现在只有一份，且浏览器/导出直接可用。
    //
    // 前提：容器渲染时也是往同一个目录、同一个名字下（见
    // MyContainer::set_asset_dir + remote_image_file_name），两边不会打架。
    std::string root = build_dir;
    while (!root.empty() && (root.back() == '\\' || root.back() == '/'))
        root.pop_back();

    // Two different URLs may share a basename ("/a/logo.png", "/b/logo.png").
    // Keep a set of taken names and disambiguate with a numeric suffix, the
    // way a browser would when saving a folder full of downloads.
    std::vector<std::string> taken;

    for (const std::string& attr : attrs) {
        std::string url = decode_attr_entities(attr);
        std::string local = remote_image_file_name(url);
        if (local.empty()) continue;              // no trustworthy name: keep remote

        // 重名先解决，再决定要不要下载。顺序很要紧：两个不同 URL 可能算出
        // 同一个 basename（/a/logo.png 与 /b/logo.png），若先看"文件已存在
        // 就复用"，第二个 URL 会把第一个的图连同名字一起占住 —— 页面上两张
        // 图指向同一个文件，其中一张显示错了，而且是静默的。
        std::string unique = local;
        for (int n = 2; n < 1000; ++n) {
            if (std::find(taken.begin(), taken.end(), unique) == taken.end())
                break;
            size_t dot = local.find_last_of('.');
            unique = local.substr(0, dot) + "_" + std::to_string(n) + local.substr(dot);
        }
        taken.push_back(unique);

        const std::string dest = root + "\\" + unique;
        if (!file_has_bytes(dest)) {
            if (!fetch) continue;                 // render path: never hit network
            if (!fetch(url, dest)) continue;
            if (!file_has_bytes(dest)) continue;   // fetch claimed success but wrote
        }                                         // nothing (urlmon did this)

        // Same picture may appear more than once: replace every occurrence of
        // the original attribute value.
        for (size_t p = html.find(attr); p != std::string::npos;
             p = html.find(attr, p + unique.size()))
            html.replace(p, attr.size(), unique);
    }
}

int count_remote_image_srcs(const std::string& html)
{
    if (html.find("<img") == std::string::npos) return 0;
    return (int)collect_remote_img_srcs(html).size();
}
