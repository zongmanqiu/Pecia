// EncodingCore.h - pure encoding-detection / text-normalization logic,
// free of UI and file I/O. Extracted from Document so the trickiest
// text-handling code (UTF-8 validation, BOM detection, line-ending
// normalization, trailing-whitespace trim) can be unit tested directly.
#pragma once

#include <vector>

// Encoding set deliberately mirrors Windows Notepad on Windows 10 1903+:
//   ANSI, UTF-8, UTF-8 with BOM, UTF-16 LE, UTF-16 BE.
// GB18030 is a Windows 11 Notepad addition, but the Win32 code page (54936)
// exists independently of the Notepad UI; isEncodingAvailable() probes it at
// runtime and the UI only shows it when the OS really supports it.
// Fl_Text_Buffer stores text as UTF-8 internally, so non-UTF-8 encodings are
// converted on load and converted back on save.
enum class Encoding {
    UTF8,        // plain UTF-8 (no BOM) - default for new files
    UTF8_BOM,    // UTF-8 with BOM
    UTF16_LE,    // UTF-16 little-endian (Notepad "Unicode")
    UTF16_BE,    // UTF-16 big-endian (Notepad "Unicode big endian")
    ANSI,        // system ANSI codepage (CP_ACP; GBK on Chinese Windows)
    GB18030,     // Chinese national standard; runtime-probed (CP54936)
};

// Map an Encoding to its Windows code page ID. Returns 0 for the
// Unicode-based encodings and for ANSI (which means CP_ACP).
int encodingToCodePage(Encoding e);

// User-facing label, matching Notepad's wording.
const char *encodingDisplayName(Encoding e);

// True when this process can actually convert text in `e`. The five base
// encodings are always available. GB18030 is probed once: some Windows
// installations may not ship CP54936 data.
bool isEncodingAvailable(Encoding e);

// The save-as list, in Notepad-like order, filtered by availability.
std::vector<Encoding> availableEncodings();

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
// from ANSI.
bool isValidUtf8(const unsigned char *data, int len);

// Detect the encoding from the first bytes of a file. BOM takes
// precedence; then valid UTF-8; then a GB18030 4-byte sequence (only when
// the code page is available); otherwise ANSI (system codepage).
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

// Generic code-page -> UTF-8 (e.g. CP_ACP, 54936). Returns true on success.
bool codepageToUtf8(int cp, const char *src, int len, char **out, int *outLen);

// UTF-8 -> generic code page. Returns true on success. When `lossy` is not
// null it is set to true if at least one character had to be replaced by
// the default character (i.e. the text cannot be represented losslessly).
// GB18030 (54936) does not accept WC_NO_BEST_FIT_CHARS on Windows, so the
// function falls back to flags 0 when the API rejects that flag.
bool utf8ToCodepage(int cp, const char *utf8, int len, char **out, int *outLen,
                    bool *lossy = nullptr);
