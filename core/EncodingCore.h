// EncodingCore.h - pure encoding-detection / text-normalization logic,
// free of UI and file I/O. Extracted from Document so the trickiest
// text-handling code (UTF-8 validation, BOM/UTF-16 heuristics, line-ending
// normalization, trailing-whitespace trim) can be unit tested directly.
#pragma once

// All encodings Pecia can transparently load. Fl_Text_Buffer stores text
// as UTF-8 internally, so any non-UTF-8 encoding is converted on load and
// converted back on save.
enum class Encoding {
    UTF8,         // plain UTF-8 (no BOM)
    UTF8_BOM,     // UTF-8 with BOM
    UTF16_LE,     // UTF-16 little-endian (typical Windows .txt)
    UTF16_BE,     // UTF-16 big-endian
    GBK,          // GBK / GB2312 (Windows Chinese ANSI codepage 936)
    ANSI,         // system ANSI codepage (fallback)
    BIG5,         // Traditional Chinese (codepage 950)
    SHIFT_JIS,    // Japanese (codepage 932)
    EUC_KR,       // Korean (codepage 949)
};

// Map an Encoding to its Windows code page ID. Returns 0 for the
// Unicode-based encodings (UTF-8/UTF-16) that do not use a code page.
int encodingToCodePage(Encoding e);

// Line-ending style (auto-detected on load, used for save normalization).
enum class EOL {
    CRLF,   // Windows: \r\n
    LF,     // Unix / modern macOS: \n
    CR,     // Classic Mac: \r (rare, but supported)
};

// Validate that a byte sequence is well-formed UTF-8. Returns true iff
// every byte is part of a valid UTF-8 code point sequence (no overlong
// encodings, no isolated continuation bytes, no truncated sequences at
// the end). Used by detectEncoding() to distinguish plain UTF-8 (no BOM)
// from GBK/ANSI.
bool isValidUtf8(const unsigned char *data, int len);

// Detect the encoding from the first bytes of a file. BOM takes
// precedence; otherwise we look for NUL bytes (UTF-16) and fall back
// to UTF-8 / ANSI.
Encoding detectEncoding(const unsigned char *data, int len);

// Convert all line endings in a UTF-8 string to a single target style.
// Handles \r\n, lone \n, and lone \r uniformly. Returns a newly allocated
// buffer (caller delete[]). If no conversion is needed, *out is set to
// nullptr and *outLen to 0.
void normalizeLineEndings(const char *src, int srcLen, EOL target,
                          char **out, int *outLen);

// Remove spaces and tabs at end of each line. Works in-place on the UTF-8
// text buffer. Lines are delimited by \n. Returns the new length (string
// is NUL-terminated at that position).
int trimTrailingWhitespace(char *text, int len);

// Remove leading whitespace and blank lines (spaces/tabs/newlines at the
// very start of the text). In-place; returns the new length.
int trimLeadingBlank(char *text, int len);

// Remove trailing whitespace and blank lines (spaces/tabs/newlines after
// the last non-blank character). In-place; returns the new length.
int trimEndingBlank(char *text, int len);

// Expand every tab into `tabWidth` spaces (fixed width, not tab stops).
// Returns a newly allocated buffer (caller delete[]); *outLen holds the
// result size.
void expandTabsToSpaces(const char *src, int srcLen, int tabWidth,
                        char **out, int *outLen);

// ---------------------------------------------------------------------------
// Codepage conversions (Windows). Callers own the allocated output buffer
// (delete[] it). On non-Windows these return false.
// ---------------------------------------------------------------------------

// UTF-8 -> wide string (wchar_t). Returns true on success.
bool utf8ToWide(const char *utf8, int len, wchar_t **out, int *outLen);

// Wide string -> UTF-8. Returns true on success.
bool wideToUtf8(const wchar_t *w, int len, char **out, int *outLen);

// GBK (CP936) -> UTF-8. Returns true on success.
bool gbkToUtf8(const char *gbk, int len, char **out, int *outLen);

// UTF-8 -> GBK (CP936). Returns true on success.
bool utf8ToGbk(const char *utf8, int len, char **out, int *outLen);

// Generic code-page -> UTF-8 (e.g. 936 GBK, 950 BIG5, 932 Shift-JIS,
// 949 EUC-KR). Returns true on success.
bool codepageToUtf8(int cp, const char *src, int len, char **out, int *outLen);

// UTF-8 -> generic code page. Returns true on success.
bool utf8ToCodepage(int cp, const char *utf8, int len, char **out, int *outLen);
