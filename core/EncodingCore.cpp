// EncodingCore.cpp - implementation of the pure encoding/text helpers.
// See EncodingCore.h. No UI, no file I/O.
#include "core/EncodingCore.h"

#include <stdlib.h>   // getenv (test-only switch)
#include <string.h>   // memcpy, memmove

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

bool isValidUtf8(const unsigned char *data, int len) {
    if (!data || len <= 0) return true;
    int i = 0;
    while (i < len) {
        unsigned char b = data[i];
        if (b <= 0x7F) {
            // ASCII
            ++i;
            continue;
        }
        int need;
        unsigned char minFirst;
        if      ((b & 0xE0) == 0xC0) { need = 1; minFirst = 0xC2; }  // 2-byte
        else if ((b & 0xF0) == 0xE0) { need = 2; minFirst = 0xE0; }  // 3-byte
        else if ((b & 0xF8) == 0xF0) { need = 3; minFirst = 0xF0; }  // 4-byte
        else return false;            // illegal leading byte (0x80-0xBF / 0xF8-0xFF)

        if (b < minFirst) return false;  // overlong encoding

        // Check continuation bytes. If the buffer ends before the sequence
        // is complete, it is a sampling-window cut (e.g. first 8 KB of a
        // file for encoding detection), not invalid data - accept it.
        // But any continuation byte that IS present must be valid (0x80-
        // 0xBF), otherwise the sequence is genuinely malformed.
        int have = len - (i + 1);          // bytes available after lead
        int check = have < need ? have : need;
        for (int j = 1; j <= check; ++j) {
            if ((data[i + j] & 0xC0) != 0x80) return false;  // not a continuation byte
        }
        if (check < need) return true;     // window cut mid-sequence: OK

        // Extra checks for surrogates and 4-byte range
        if (need == 2) {
            unsigned char b2 = data[i + 1];
            // reject UTF-16 surrogates encoded as UTF-8 (U+D800..U+DFFF)
            if (b == 0xED && b2 >= 0xA0) return false;
        } else if (need == 3) {
            // reject code points above U+10FFFF
            if (b > 0xF4) return false;
            if (b == 0xF4 && data[i + 1] > 0x8F) return false;
            // reject 3-byte overlong encodings (E0 80-9F): the lead check
            // b >= minFirst only catches E0 itself, not a second byte below
            // A0, so U+0800..U+0FFF encoded in 3 bytes slipped through.
            if (b == 0xE0 && data[i + 1] < 0xA0) return false;
        } else if (need == 4) {
            // reject 4-byte overlong encoding (F0 80-8F): the lead check
            // only catches F0, so U+0000..U+FFFF in 4 bytes slipped through.
            if (b == 0xF0 && data[i + 1] < 0x90) return false;
        }

        i += 1 + need;
    }
    return true;
}

// Test-only switch: PECIA_FORCE_NO_GB18030=1 makes the probe report
// "unavailable" so the hidden-item path (GB18030 must not appear anywhere in
// the save-as list) can be exercised on a machine that does have CP54936.
// This is a runtime environment variable, not a build variant, so there is
// still exactly one binary; production behaviour is untouched when it is unset.
static bool forceNoGb18030() {
    const char *v = getenv("PECIA_FORCE_NO_GB18030");
    return v != nullptr && v[0] == '1';
}

// Probe GB18030 once. IsValidCodePage can return TRUE for a valid but
// unavailable page; an actual 4-byte decode is the definitive test.
static bool probeGb18030() {
#if defined(_WIN32)
    if (forceNoGb18030()) return false;
    if (!IsValidCodePage(54936)) return false;
    const char probe[] = "\x81\x30\x81\x30"; // U+0080
    wchar_t w[2] = {};
    return MultiByteToWideChar(54936, MB_ERR_INVALID_CHARS, probe, 4, w, 2) > 0;
#else
    return false;
#endif
}

bool isEncodingAvailable(Encoding e) {
    switch (e) {
    case Encoding::UTF8:
    case Encoding::UTF8_BOM:
    case Encoding::UTF16_LE:
    case Encoding::UTF16_BE:
    case Encoding::ANSI:
        return true;
    case Encoding::GB18030: {
        static const bool available = probeGb18030();
        return available;
    }
    }
    return false;
}

std::vector<Encoding> availableEncodings() {
    std::vector<Encoding> out = {
        Encoding::ANSI,
        Encoding::UTF8,
        Encoding::UTF8_BOM,
        Encoding::UTF16_LE,
        Encoding::UTF16_BE,
    };
    if (isEncodingAvailable(Encoding::GB18030)) out.push_back(Encoding::GB18030);
    return out;
}

const char *encodingDisplayName(Encoding e) {
    switch (e) {
    case Encoding::UTF8:      return "UTF-8";
    case Encoding::UTF8_BOM:  return "UTF-8 with BOM";
    case Encoding::UTF16_LE:  return "UTF-16 LE";
    case Encoding::UTF16_BE:  return "UTF-16 BE";
    case Encoding::ANSI:      return "ANSI";
    case Encoding::GB18030:   return "GB18030";
    }
    return "Unknown";
}

// A GB18030 4-byte sequence is 0x81-0xFE, 0x30-0x39, 0x81-0xFE, 0x30-0x39.
// The second/fourth bytes are not valid GBK trail bytes, so a match cannot
// be a coincidental GBK pair. Used only when the code page is available.
static bool hasGb18030FourByteSequence(const unsigned char *data, int len) {
    for (int i = 0; i + 3 < len; ++i) {
        if (data[i] >= 0x81 && data[i] <= 0xFE &&
            data[i + 1] >= 0x30 && data[i + 1] <= 0x39 &&
            data[i + 2] >= 0x81 && data[i + 2] <= 0xFE &&
            data[i + 3] >= 0x30 && data[i + 3] <= 0x39)
            return true;
    }
    return false;
}

Encoding detectEncoding(const unsigned char *data, int len) {
    if (len >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
        return Encoding::UTF8_BOM;
    if (len >= 2 && data[0] == 0xFF && data[1] == 0xFE)
        return Encoding::UTF16_LE;
    if (len >= 2 && data[0] == 0xFE && data[1] == 0xFF)
        return Encoding::UTF16_BE;
    // UTF-16 LE without BOM (kept: NUL bytes are valid UTF-8, so without
    // this check such a file would load as UTF-8 with embedded NULs).
    if (len >= 64) {
        int nullCount = 0;
        int checkLen = len < 256 ? len : 256;
        for (int i = 1; i < checkLen; i += 2) {
            if (data[i] == 0) ++nullCount;
        }
        if (nullCount > checkLen / 4) return Encoding::UTF16_LE;
    }
    if (isValidUtf8(data, len)) return Encoding::UTF8;
    if (isEncodingAvailable(Encoding::GB18030) &&
        hasGb18030FourByteSequence(data, len))
        return Encoding::GB18030;
    return Encoding::ANSI;   // system codepage, like Notepad
}

int encodingToCodePage(Encoding e) {
    switch (e) {
    case Encoding::GB18030: return 54936;
    default:                return 0;   // UTF-8/16 and ANSI (CP_ACP at call site)
    }
}

void normalizeLineEndings(const char *src, int srcLen, EOL target,
                          char **out, int *outLen) {
    *out = nullptr;
    *outLen = 0;

    // Quick scan: does this text even need normalization?
    bool hasCR = false;
    for (int i = 0; i < srcLen; i++) {
        if (src[i] == '\r') { hasCR = true; break; }
    }
    // If target is LF and there's no \r, the text is already normalized.
    if (!hasCR && target == EOL::LF) return;

    const char *eolStr;
    int eolLen;
    switch (target) {
        case EOL::CRLF: eolStr = "\r\n"; eolLen = 2; break;
        case EOL::LF:   eolStr = "\n";   eolLen = 1; break;
        case EOL::CR:   eolStr = "\r";   eolLen = 1; break;
        default:        eolStr = "\r\n"; eolLen = 2; break;
    }

    // Count output size
    int outSize = 0;
    for (int i = 0; i < srcLen; i++) {
        if (src[i] == '\r') {
            outSize += eolLen;
            if (i + 1 < srcLen && src[i + 1] == '\n') i++; // skip the \n
        } else if (src[i] == '\n') {
            outSize += eolLen;
        } else {
            outSize++;
        }
    }

    *out = new char[outSize + 1];
    int pos = 0;
    for (int i = 0; i < srcLen; i++) {
        if (src[i] == '\r') {
            memcpy(*out + pos, eolStr, eolLen);
            pos += eolLen;
            if (i + 1 < srcLen && src[i + 1] == '\n') i++;
        } else if (src[i] == '\n') {
            memcpy(*out + pos, eolStr, eolLen);
            pos += eolLen;
        } else {
            (*out)[pos++] = src[i];
        }
    }
    (*out)[pos] = '\0';
    *outLen = pos;
}

int trimTrailingWhitespace(char *text, int len) {
    if (!text || len <= 0) return len;
    int writePos = 0;
    int lineStart = 0;
    for (int i = 0; i <= len; ++i) {
        if (i == len || text[i] == '\n') {
            // Scan backward from end of line to find last non-space/tab.
            int last = i;
            while (last > lineStart &&
                   (text[last - 1] == ' ' || text[last - 1] == '\t'))
                --last;
            int copyLen = last - lineStart;
            memmove(text + writePos, text + lineStart, copyLen);
            writePos += copyLen;
            if (i < len) text[writePos++] = '\n';
            lineStart = i + 1;
        }
    }
    text[writePos] = '\0';
    return writePos;
}

// Remove leading whitespace and blank lines (spaces/tabs/newlines at the
// very start of the text). In-place; returns the new length.
int trimLeadingBlank(char *text, int len) {
    if (!text || len <= 0) return len;
    int i = 0;
    while (i < len &&
           (text[i] == ' ' || text[i] == '\t' ||
            text[i] == '\r' || text[i] == '\n'))
        ++i;
    if (i > 0) {
        memmove(text, text + i, len - i);
        len -= i;
    }
    text[len] = '\0';
    return len;
}

// Remove trailing whitespace and blank lines (spaces/tabs/newlines after
// the last non-blank character). In-place; returns the new length.
int trimEndingBlank(char *text, int len) {
    if (!text || len <= 0) return len;
    int i = len;
    while (i > 0) {
        char c = text[i - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') --i;
        else break;
    }
    text[i] = '\0';
    return i;
}

// Expand every tab into `tabWidth` spaces (fixed width, not tab stops).
// Returns a newly allocated buffer (caller delete[]); *outLen holds the
// result size.
void expandTabsToSpaces(const char *src, int srcLen, int tabWidth,
                        char **out, int *outLen) {
    *out = nullptr;
    *outLen = 0;
    if (!src || srcLen <= 0) return;
    if (tabWidth < 1) tabWidth = 1;
    int tabs = 0;
    for (int i = 0; i < srcLen; ++i)
        if (src[i] == '\t') ++tabs;
    int outSize = srcLen + tabs * (tabWidth - 1);
    char *buf = new char[outSize + 1];
    int pos = 0;
    for (int i = 0; i < srcLen; ++i) {
        if (src[i] == '\t') {
            for (int k = 0; k < tabWidth; ++k) buf[pos++] = ' ';
        } else {
            buf[pos++] = src[i];
        }
    }
    buf[pos] = '\0';
    *out = buf;
    *outLen = pos;
}

// ---------------------------------------------------------------------------
// Codepage conversions (Windows)
// ---------------------------------------------------------------------------

#if defined(_WIN32)

bool utf8ToWide(const char *utf8, int len, wchar_t **out, int *outLen) {
    if (!utf8 || len <= 0) { *out = nullptr; *outLen = 0; return true; }
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, len, nullptr, 0);
    if (wlen <= 0) return false;
    wchar_t *w = new wchar_t[wlen + 1];
    MultiByteToWideChar(CP_UTF8, 0, utf8, len, w, wlen);
    w[wlen] = 0;
    *out = w; *outLen = wlen;
    return true;
}

bool wideToUtf8(const wchar_t *w, int len, char **out, int *outLen) {
    if (!w || len <= 0) { *out = nullptr; *outLen = 0; return true; }
    int ulen = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
    if (ulen <= 0) return false;
    char *u = new char[ulen + 1];
    WideCharToMultiByte(CP_UTF8, 0, w, len, u, ulen, nullptr, nullptr);
    u[ulen] = 0;
    *out = u; *outLen = ulen;
    return true;
}

bool codepageToUtf8(int cp, const char *src, int len, char **out, int *outLen) {
    if (!out || !outLen) return false;
    *out = nullptr; *outLen = 0;
    if (!src || len <= 0) return true;
    // Code page -> Wide -> UTF-8
    int wl = MultiByteToWideChar(cp, 0, src, len, nullptr, 0);
    if (wl <= 0) return false;
    wchar_t *w = new wchar_t[wl + 1];
    MultiByteToWideChar(cp, 0, src, len, w, wl);
    w[wl] = 0;
    bool ok = wideToUtf8(w, wl, out, outLen);
    delete[] w;
    return ok;
}

bool utf8ToCodepage(int cp, const char *utf8, int len, char **out, int *outLen,
                    bool *lossy) {
    if (!out || !outLen) return false;
    *out = nullptr; *outLen = 0;
    if (lossy) *lossy = false;
    wchar_t *w = nullptr; int wlen = 0;
    if (!utf8ToWide(utf8, len, &w, &wlen)) return false;
    if (wlen <= 0) {
        delete[] w;
        *out = new char[1];
        (*out)[0] = 0;
        return true;
    }

    // GB18030 (54936) rejects WC_NO_BEST_FIT_CHARS with ERROR_INVALID_FLAGS
    // on Windows; every other code page used by Pecia accepts it. The flag
    // prevents best-fit substitutions, and lpUsedDefaultChar tells us when
    // a character could not be represented at all.
    // GB18030 (54936) rejects both WC_NO_BEST_FIT_CHARS and the
    // lpDefaultChar/lpUsedDefaultChar parameters (ERROR_INVALID_PARAMETER).
    // It can represent all Unicode, so no lossy detection is needed there.
    if (cp == 54936) {
        SetLastError(0);
        int gl = WideCharToMultiByte(cp, 0, w, wlen, nullptr, 0, nullptr, nullptr);
        if (gl <= 0) { delete[] w; return false; }
        char *g = new char[gl + 1];
        SetLastError(0);
        int gl2 = WideCharToMultiByte(cp, 0, w, wlen, g, gl, nullptr, nullptr);
        delete[] w;
        if (gl2 <= 0) { delete[] g; return false; }
        g[gl2] = 0;
        *out = g; *outLen = gl2;
        return true;
    }

    DWORD flags = WC_NO_BEST_FIT_CHARS;
    char defaultChar = '?';
    BOOL used = FALSE;
    SetLastError(0);
    int gl = WideCharToMultiByte(cp, flags, w, wlen, nullptr, 0, nullptr, nullptr);
    if (gl <= 0 && GetLastError() == ERROR_INVALID_FLAGS) {
        flags = 0;
        SetLastError(0);
        gl = WideCharToMultiByte(cp, flags, w, wlen, nullptr, 0, nullptr, nullptr);
    }
    if (gl <= 0) { delete[] w; return false; }
    char *g = new char[gl + 1];
    used = FALSE;
    SetLastError(0);
    int gl2 = WideCharToMultiByte(cp, flags, w, wlen, g, gl,
                                  &defaultChar, &used);
    delete[] w;
    if (gl2 <= 0) { delete[] g; return false; }
    g[gl2] = 0;
    if (lossy) *lossy = (used != FALSE);
    *out = g; *outLen = gl2;
    return true;
}

#else
// Linux/macOS stubs - to be implemented with iconv later
bool utf8ToWide(const char *, int, wchar_t **, int *) { return false; }
bool wideToUtf8(const wchar_t *, int, char **, int *) { return false; }
bool codepageToUtf8(int, const char *, int, char **, int *) { return false; }
bool utf8ToCodepage(int, const char *, int, char **, int *, bool *) { return false; }
#endif
