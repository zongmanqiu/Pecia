// EncodingCore.cpp - implementation of the pure encoding/text helpers.
// See EncodingCore.h. No UI, no file I/O.
#include "core/EncodingCore.h"

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

Encoding detectEncoding(const unsigned char *data, int len) {
    if (len >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
        return Encoding::UTF8_BOM;
    if (len >= 2 && data[0] == 0xFF && data[1] == 0xFE)
        return Encoding::UTF16_LE;
    if (len >= 2 && data[0] == 0xFE && data[1] == 0xFF)
        return Encoding::UTF16_BE;
    // Heuristic: if the file has lots of NUL bytes in even positions, it's
    // probably UTF-16 without a BOM. We check the first 256 bytes.
    if (len >= 64) {
        int nullCount = 0;
        int checkLen = len < 256 ? len : 256;
        for (int i = 1; i < checkLen; i += 2) {
            if (data[i] == 0) ++nullCount;
        }
        if (nullCount > checkLen / 4) return Encoding::UTF16_LE;
    }
    // If the entire sample is well-formed UTF-8, treat it as UTF-8.
    // This catches the common case of a modern Chinese .txt file that
    // was saved as UTF-8 without a BOM. Only fall back to legacy code
    // pages when the bytes are not valid UTF-8.
    if (isValidUtf8(data, len)) return Encoding::UTF8;

    // The sample is not UTF-8/UTF-16: it is a legacy single/double-byte
    // code page. The byte ranges of GBK/BIG5/Shift-JIS/EUC-KR overlap so
    // heavily that "can these bytes decode" cannot tell them apart.
    // Instead, decode each byte pair under every candidate and look at
    // WHERE the resulting character falls in Unicode: the same bytes
    // decode to different scripts under different code pages.
    //   - hiragana/katakana   -> Shift-JIS
    //   - hangul syllables    -> EUC-KR
    //   - otherwise: the code page that decodes the most CJK ideographs
    //     with the fewest failures wins (GBK vs BIG5 distinction).
#if defined(_WIN32)
    struct Candidate { int cp; Encoding enc; };
    static const Candidate cands[] = {
        {936, Encoding::GBK},
        {950, Encoding::BIG5},
        {932, Encoding::SHIFT_JIS},
        {949, Encoding::EUC_KR},
    };

    struct Score {
        int cjk = 0, hira = 0, kata = 0, hangul = 0, bad = 0;
    } scores[4];
    for (int ci = 0; ci < 4; ++ci) {
        Score &s = scores[ci];
        int pos = 0;
        while (pos < len) {
            unsigned char b = data[pos];
            if (b < 0x80) { ++pos; continue; }
            if (pos + 1 >= len) break;   // truncated pair at sample end
            wchar_t w;
            int n = MultiByteToWideChar(cands[ci].cp, MB_ERR_INVALID_CHARS,
                                        (const char *)data + pos, 2, &w, 1);
            if (n <= 0) { ++s.bad; ++pos; continue; }
            pos += 2;
            if      (w >= 0x4E00 && w <= 0x9FFF) ++s.cjk;      // CJK ideographs
            else if (w >= 0x3040 && w <= 0x309F) ++s.hira;     // hiragana
            else if (w >= 0x30A0 && w <= 0x30FF) ++s.kata;     // katakana
            else if (w >= 0xAC00 && w <= 0xD7AF) ++s.hangul;   // hangul
        }
    }

    // Script signal: only Shift-JIS (932) produces genuine hiragana/
    // katakana; the other code pages may decode stray bytes into kana
    // ranges by accident, so gate the signal on the 932 candidate only.
    if (scores[2].hira + scores[2].kata > 0 &&
        scores[2].cjk + scores[2].hira + scores[2].kata >= 3)
        return cands[2].enc;   // Shift-JIS (932)

    // EUC-KR check: hangul syllables in EUC-KR live at lead bytes
    // 0xB0-0xC8 with trail 0xA1-0xFE. BIG5/GBK text can decode to
    // hangul under CP949 by accident, but genuine EUC-KR has a
    // distinctive concentration in this range.
    {
        int hangulLead = 0, total = 0;
        int pos = 0;
        while (pos + 1 < len) {
            unsigned char b = data[pos];
            if (b < 0x80) { ++pos; continue; }
            ++total;
            if (b >= 0xB0 && b <= 0xC8 && data[pos + 1] >= 0xA1 &&
                data[pos + 1] <= 0xFE)
                ++hangulLead;
            pos += 2;
        }
        if (total > 0 && hangulLead * 10 >= total * 7)
            return Encoding::EUC_KR;
    }

    // BIG5 vs GBK: compare how many CJK ideographs each code page
    // decodes. The correct page yields the most ideographs with the
    // fewest failures.
    int best = 0;
    for (int ci = 1; ci < 4; ++ci) {
        const Score &a = scores[best];
        const Score &b = scores[ci];
        int va = a.cjk - a.bad * 2;
        int vb = b.cjk - b.bad * 2;
        if (vb > va) best = ci;
    }
    // Tie-break toward GBK on the Chinese Windows locale.
    if (best > 0 && scores[best].cjk == scores[0].cjk &&
        scores[best].bad == scores[0].bad)
        best = 0;
    if (scores[best].cjk > 0) return cands[best].enc;
    return Encoding::ANSI;   // nothing decodes as CJK - system codepage
#else
    return Encoding::ANSI;
#endif
}

int encodingToCodePage(Encoding e) {
    switch (e) {
    case Encoding::GBK:       return 936;
    case Encoding::BIG5:      return 950;
    case Encoding::SHIFT_JIS: return 932;
    case Encoding::EUC_KR:    return 949;
    default:                  return 0;   // Unicode-based encodings
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

bool gbkToUtf8(const char *gbk, int len, char **out, int *outLen) {
    return codepageToUtf8(936, gbk, len, out, outLen);
}

bool utf8ToGbk(const char *utf8, int len, char **out, int *outLen) {
    return utf8ToCodepage(936, utf8, len, out, outLen);
}

bool codepageToUtf8(int cp, const char *src, int len, char **out, int *outLen) {
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

bool utf8ToCodepage(int cp, const char *utf8, int len, char **out, int *outLen) {
    wchar_t *w = nullptr; int wlen = 0;
    if (!utf8ToWide(utf8, len, &w, &wlen)) return false;
    int gl = WideCharToMultiByte(cp, 0, w, wlen, nullptr, 0, nullptr, nullptr);
    if (gl <= 0) { delete[] w; return false; }
    char *g = new char[gl + 1];
    WideCharToMultiByte(cp, 0, w, wlen, g, gl, nullptr, nullptr);
    g[gl] = 0;
    delete[] w;
    *out = g; *outLen = gl;
    return true;
}

#else
// Linux/macOS stubs - to be implemented with iconv later
bool utf8ToWide(const char *, int, wchar_t **, int *) { return false; }
bool wideToUtf8(const wchar_t *, int, char **, int *) { return false; }
bool gbkToUtf8(const char *, int, char **, int *) { return false; }
bool utf8ToGbk(const char *, int, char **, int *) { return false; }
bool codepageToUtf8(int, const char *, int, char **, int *) { return false; }
bool utf8ToCodepage(int, const char *, int, char **, int *) { return false; }
#endif
