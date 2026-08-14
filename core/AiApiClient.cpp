// AiApiClient.cpp - OpenAI-compatible chat client via WinHTTP.
#include "AiApiClient.h"

#include <windows.h>
#include <winhttp.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#pragma comment(lib, "winhttp.lib")

// ---------------------------------------------------------------------------
// JSON helpers (minimal, defensive)
// ---------------------------------------------------------------------------

std::string aiJsonEscape(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        default:
            if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04X", c);
                out += buf;
            } else {
                out += (char)c;
            }
        }
    }
    return out;
}

static bool jsonDecodeString(const std::string &body, size_t &i, std::string &out) {
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
                // Combine a UTF-16 surrogate pair (e.g. \uD83D\uDE00 emoji /
                // supplementary-plane chars) into a single code point. Each
                // half alone would otherwise be encoded as invalid (CESU-8).
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    // High surrogate: require a following "\uXXXX" low half.
                    if (i + 6 > body.size() || body[i] != '\\' || body[i + 1] != 'u')
                        return false;    // lone high surrogate is invalid JSON
                    unsigned lo = 0;
                    for (int k = 0; k < 4; ++k) {
                        char h = body[i + 2 + k];
                        lo <<= 4;
                        if (h >= '0' && h <= '9') lo |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') lo |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') lo |= (unsigned)(h - 'A' + 10);
                        else return false;
                    }
                    if (lo < 0xDC00 || lo > 0xDFFF) return false;  // not a low half
                    i += 6;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return false;    // lone low surrogate is invalid JSON
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

// Extract choices[0].message.content. Skips reasoning_content (present for
// R1-style models) and returns the LAST message string if several appear.
static bool extractContent(const std::string &body, std::string &out) {
    size_t pos = 0;
    bool found = false;
    while (true) {
        size_t k = body.find("\"content\"", pos);
        if (k == std::string::npos) break;
        size_t colon = body.find(':', k + 9);
        if (colon == std::string::npos) break;
        size_t q = body.find_first_not_of(" \t", colon + 1);
        if (q == std::string::npos || body[q] != '"') {
            pos = colon + 1;
            continue;
        }
        std::string val;
        if (jsonDecodeString(body, q, val)) {
            out = val;
            found = true;
        }
        pos = q;
    }
    return found;
}

// ---------------------------------------------------------------------------
// HTTP client
// ---------------------------------------------------------------------------

AiChatResult aiChatCompletion(const std::string &apiKey,
                              const std::string &model,
                              const std::vector<AiChatMessage> &messages,
                              const std::string &endpoint,
                              int timeoutMs) {
    AiChatResult res;

    if (apiKey.empty() || model.empty() || endpoint.empty()) {
        res.error = "API key / model / endpoint is empty";
        return res;
    }

    // Parse the endpoint URL: [http|https]://host[:port]/path
    std::string host, path;
    bool useTls = true;              // default to https
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    {
        std::string u = endpoint;
        if (u.rfind("https://", 0) == 0) { useTls = true;  u = u.substr(8); }
        else if (u.rfind("http://", 0) == 0) { useTls = false; u = u.substr(7); }
        // Honor an explicit :port in the host portion.
        size_t colon = u.find(':');
        size_t slash = u.find('/');
        if (colon != std::string::npos && (slash == std::string::npos || colon < slash)) {
            long p = atol(u.c_str() + colon + 1);
            if (p > 0 && p <= 65535) port = (INTERNET_PORT)p;
            u = u.substr(0, colon) + (slash != std::string::npos ? u.substr(slash) : std::string());
            slash = u.find('/');
        }
        if (slash != std::string::npos) {
            host = u.substr(0, slash);
            path = u.substr(slash);
        } else {
            host = u;
            path = "/";
        }
        if (!useTls && port == INTERNET_DEFAULT_HTTPS_PORT) port = INTERNET_DEFAULT_HTTP_PORT;
    }
    std::wstring whost(host.begin(), host.end());
    std::wstring wpath(path.begin(), path.end());

    std::string body = "{";
    body += "\"model\":\"" + aiJsonEscape(model) + "\",";
    body += "\"messages\":[";
    for (size_t i = 0; i < messages.size(); ++i) {
        if (i > 0) body += ",";
        body += "{\"role\":\"" + std::string(messages[i].role) +
                "\",\"content\":\"" + aiJsonEscape(messages[i].content) + "\"}";
    }
    body += "],";
    body += "\"max_tokens\":1024,";
    body += "\"temperature\":0.3,";
    body += "\"stream\":false";
    body += "}";

    HINTERNET hSession = WinHttpOpen(L"Pecia/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { res.error = "WinHttpOpen failed"; return res; }

    HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), port, 0);
    if (!hConnect) {
        res.error = "WinHttpConnect failed";
        WinHttpCloseHandle(hSession);
        return res;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", wpath.c_str(), nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            useTls ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        res.error = "WinHttpOpenRequest failed";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return res;
    }

    WinHttpSetTimeouts(hRequest, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    std::wstring headers = L"Content-Type: application/json\r\n";
    std::string auth = "Authorization: Bearer " + apiKey + "\r\n";
    headers += std::wstring(auth.begin(), auth.end());

    BOOL ok = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)-1,
                                 (LPVOID)body.c_str(), (DWORD)body.size(),
                                 (DWORD)body.size(), 0);
    if (!ok) {
        res.error = "WinHttpSendRequest failed (timeout or network)";
        goto done;
    }
    ok = WinHttpReceiveResponse(hRequest, nullptr);
    if (!ok) {
        res.error = "WinHttpReceiveResponse failed";
        goto done;
    }

    {
        DWORD status = 0, statusLen = sizeof(status);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusLen,
                            WINHTTP_NO_HEADER_INDEX);
        res.httpStatus = (int)status;
    }

    {
        std::string reply;
        char buf[16384];
        DWORD read = 0;
        while (WinHttpReadData(hRequest, buf, sizeof(buf), &read) && read > 0) {
            reply.append(buf, read);
            read = 0;
        }
        if (res.httpStatus == 200) {
            std::string content;
            if (extractContent(reply, content)) {
                res.ok = true;
                res.content = content;
            } else {
                res.error = "No content in response: " + reply.substr(0, 300);
            }
        } else {
            res.error = "HTTP " + std::to_string(res.httpStatus) + ": " +
                        reply.substr(0, 300);
        }
    }

done:
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    if (!res.ok && res.error.empty()) res.error = "Unknown failure";
    return res;
}
