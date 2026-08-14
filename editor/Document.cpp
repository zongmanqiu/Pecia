// Document.cpp - Fl_Text_Buffer wrapper with full encoding support
#include "Document.h"

#include "core/UiBridge.h"
#include "core/I18n.h"
#include <FL/filename.H>
#include <FL/fl_string_functions.h>
#include <FL/fl_utf8.h>      // fl_fopen

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <new>          // std::bad_alloc

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Read at most this many bytes when detecting the encoding
static const int DETECT_BUF = 8192;

Document::Document()
    : m_buffer(new Fl_Text_Buffer())
    , m_encoding(Encoding::UTF8)
    , m_dirty(false)
    , m_readOnly(false)
    , m_eol(EOL::CRLF)
    , m_trimTrailing(false) {   // caller (MainWindow) sets from Config
    m_filePath[0] = '\0';
    m_buffer->add_modify_callback(modifyCallback, this);
}

Document::~Document() {
    delete m_buffer;
}

// ---------------------------------------------------------------------------
// Dirty tracking
// ---------------------------------------------------------------------------

void Document::modifyCallback(int pos, int nInserted, int nDeleted,
                              int /*nRestyled*/, const char *deletedText,
                              void *cbArg) {
    Document *self = static_cast<Document *>(cbArg);
    if (!self) return;
    // Ignore zero-size notifications (e.g. tab_distance() on an empty
    // buffer fires a "restyle" callback with 0/0).
    if (!nInserted && !nDeleted) return;
    // tab_distance() fires (0, len, len, ...) with the UNCHANGED text.
    // Only mark dirty when a whole-buffer equal-length replace actually
    // changed the content (otherwise attaching an editor to a loaded
    // document makes it look dirty and triggers a spurious "Unsaved
    // Changes" prompt on every file open).
    if (pos == 0 && nInserted == nDeleted && nInserted > 0 &&
        nInserted == self->m_buffer->length() && deletedText) {
        char *cur = self->m_buffer->text();
        bool same = cur && strcmp(cur, deletedText) == 0;
        free(cur);
        if (same) return;
    }
    self->setDirty(true);
}

void Document::setDirty(bool v) {
    m_dirty = v;
}

void Document::markClean() {
    m_dirty = false;
}

void Document::setEncoding(Encoding e) {
    m_encoding = e;
}

// ---------------------------------------------------------------------------
// EOL detection - scan text and pick the dominant line-ending style.
// If no line endings are found, keep the default (CRLF for new files).
// ---------------------------------------------------------------------------
void Document::detectEOL(const char *text, int len) {
    int crlfCount = 0, lfCount = 0, crCount = 0;
    for (int i = 0; i < len; ) {
        if (text[i] == '\r') {
            if (i + 1 < len && text[i + 1] == '\n') {
                ++crlfCount;
                i += 2;
            } else {
                ++crCount;
                i += 1;
            }
        } else if (text[i] == '\n') {
            ++lfCount;
            i += 1;
        } else {
            i += 1;
        }
    }
    // Pick the dominant style (tie goes to CRLF > LF > CR).
    if (crlfCount >= lfCount && crlfCount >= crCount)
        m_eol = EOL::CRLF;
    else if (lfCount >= crCount)
        m_eol = EOL::LF;
    else
        m_eol = EOL::CR;
    // If all counts are 0 (empty file), keep the constructor default (CRLF).
}

const char *Document::encodingName() const {
    switch (m_encoding) {
    case Encoding::UTF8:      return "UTF-8";
    case Encoding::UTF8_BOM:  return "UTF-8 (BOM)";
    case Encoding::UTF16_LE:  return "UTF-16 LE";
    case Encoding::UTF16_BE:  return "UTF-16 BE";
    case Encoding::GBK:       return "GBK";
    case Encoding::ANSI:      return "ANSI";
    case Encoding::BIG5:      return "BIG5";
    case Encoding::SHIFT_JIS: return "Shift-JIS";
    case Encoding::EUC_KR:    return "EUC-KR";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// File loading with encoding conversion
// ---------------------------------------------------------------------------

bool Document::loadFile(const char *path) {
    // Peek at the file to detect encoding. IMPORTANT: the sample must end
    // on a complete UTF-8 character. If DETECT_BUF cuts a multi-byte
    // character in half, isValidUtf8() rejects the trailing truncated
    // sequence and a perfectly valid UTF-8 file gets misdetected as
    // ANSI/GBK - which then decodes it into mojibake. So after the
    // initial read, extend the sample past any partial trailing sequence.
    FILE *fp = fl_fopen(path, "rb");
    if (!fp) {
        char _msg[512];
        snprintf(_msg, sizeof(_msg), I18n::get("error.open_failed"), path, strerror(errno));
        if (UiBridge *b = uiBridge()) b->message(I18n::get("error.title"), _msg, I18n::get("dlg.ok"));
        return false;
    }
    unsigned char buf[DETECT_BUF + 8];
    int n = (int)fread(buf, 1, DETECT_BUF, fp);

    // If the sample ends in the middle of a UTF-8 sequence, extend it to a
    // complete character. Two cases: (a) the last byte is a lead byte
    // (0xC2-0xF4) whose continuation bytes follow in the file; (b) the last
    // byte is itself a continuation byte (0x80-0xBF), meaning the lead byte
    // sits somewhere inside the sample and the sample was cut mid-character
    // - we cannot un-read, but we can extend past the truncated tail only
    // for case (a). Case (b) cannot happen here because fread fills the
    // whole buffer from the file start, so the only mid-character cut is at
    // the buffer end, which is always a lead byte.
    if (n > 0) {
        unsigned char last = buf[n - 1];
        int need = 0;
        if ((last & 0xE0) == 0xC0 && last >= 0xC2) need = 1;        // 2-byte lead
        else if ((last & 0xF0) == 0xE0) need = 2;                   // 3-byte lead
        else if ((last & 0xF8) == 0xF0) need = 3;                   // 4-byte lead
        if (need > 0 && n + need <= (int)sizeof(buf)) {
            int extra = (int)fread(buf + n, 1, need, fp);
            n += extra;
        }
    }
    fclose(fp);

    m_encoding = detectEncoding(buf, n);
    return loadWithEncoding(path, m_encoding);
}

bool Document::loadWithEncoding(const char *path, Encoding enc) {
    // Read whole file into memory
    FILE *fp = fl_fopen(path, "rb");
    if (!fp) {
        char _msg[512];
        snprintf(_msg, sizeof(_msg), "Failed to open file\n%s\n%s", path, strerror(errno));
        if (UiBridge *b = uiBridge()) b->message("Error", _msg, "OK");
        return false;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 0) { fclose(fp); return false; }

    // Technical ceiling: Fl_Text_Buffer addresses text with 32-bit int
    // positions; anything at/over 2 GB would overflow and corrupt. Files
    // below this are allowed (opening a multi-GB file may be slow or run
    // out of RAM - that is the user's choice).
    const long MAX_FILE_SIZE = 2147483647L;   // 2 GB - 1 (int position limit)
    if (sz >= MAX_FILE_SIZE) {
        double gb = sz / (1024.0 * 1024.0 * 1024.0);
        char _msg[512];
        snprintf(_msg, sizeof(_msg), I18n::get("error.too_large"), gb);
        if (UiBridge *b = uiBridge()) b->message(I18n::get("error.title"), _msg, I18n::get("dlg.ok"));
        fclose(fp);
        return false;
    }

    // Large files (>100 MB): force read-only viewing mode. The dialog is
    // informational only - the user gets no choice (editing multi-hundred-
    // MB files in a gap buffer is too slow and dangerous).
    const long LARGE_FILE_THRESHOLD = 100L * 1024 * 1024;  // 100 MB
    if (sz > LARGE_FILE_THRESHOLD) {
        double mb = sz / (1024.0 * 1024.0);
        char msg[256];
        snprintf(msg, sizeof(msg), I18n::get("error.largefile.msg"), mb);
        if (UiBridge *b = uiBridge()) b->message(I18n::get("error.largefile.title"), msg, I18n::get("dlg.ok"));
        m_readOnly = true;
    }

    // Allocation failure (out of memory): report it instead of crashing
    // the whole application (other tabs may hold unsaved work).
    char *raw = nullptr;
    try {
        raw = new char[sz + 1];
    } catch (const std::bad_alloc &) {
        char _msg[512];
        snprintf(_msg, sizeof(_msg), I18n::get("error.out_of_memory"),
                 sz / (1024.0 * 1024.0));
        if (UiBridge *b = uiBridge()) b->message(I18n::get("error.title"), _msg, I18n::get("dlg.ok"));
        fclose(fp);
        return false;
    }
    size_t rd = fread(raw, 1, (size_t)sz, fp);
    fclose(fp);
    raw[rd] = 0;

    char *utf8 = nullptr; int utf8Len = 0;

    switch (enc) {
    case Encoding::UTF8:
        // Already UTF-8 (or assumed so) - hand directly to Fl_Text_Buffer
        m_buffer->text(raw);
        detectEOL(raw, (int)rd);
        delete[] raw;
        break;

    case Encoding::UTF8_BOM: {
        // Skip the 3-byte BOM
        int start = (rd >= 3 && (unsigned char)raw[0] == 0xEF &&
                     (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) ? 3 : 0;
        m_buffer->text(raw + start);
        detectEOL(raw + start, (int)(rd - start));
        delete[] raw;
        break;
    }

    case Encoding::UTF16_LE:
    case Encoding::UTF16_BE: {
        // Skip BOM if present (2 bytes)
        int start = 0;
        if (rd >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE) start = 2;
        if (rd >= 2 && (unsigned char)raw[0] == 0xFE && (unsigned char)raw[1] == 0xFF) start = 2;
        int wlen = (int)(rd - start) / 2;
        wchar_t *w = new wchar_t[wlen + 1];
        const unsigned char *p = reinterpret_cast<const unsigned char *>(raw + start);
        if (enc == Encoding::UTF16_LE) {
            for (int i = 0; i < wlen; ++i) {
                w[i] = (wchar_t)(p[i * 2] | (p[i * 2 + 1] << 8));
            }
        } else {
            for (int i = 0; i < wlen; ++i) {
                w[i] = (wchar_t)((p[i * 2] << 8) | p[i * 2 + 1]);
            }
        }
        w[wlen] = 0;
        wideToUtf8(w, wlen, &utf8, &utf8Len);
        m_buffer->text(utf8 ? utf8 : "");
        if (utf8) detectEOL(utf8, utf8Len);
        delete[] w;
        delete[] raw;
        delete[] utf8;
        break;
    }

    case Encoding::GBK:
    case Encoding::ANSI:
    case Encoding::BIG5:
    case Encoding::SHIFT_JIS:
    case Encoding::EUC_KR: {
        // Legacy code pages (ANSI on Chinese Windows = CP936/GBK; the
        // others are their explicit code pages). Decode to UTF-8 via the
        // Windows API - the authoritative decoder.
        int cp = encodingToCodePage(enc);
        if (cp == 0) cp = CP_ACP;   // ANSI fallback: system codepage
        if (codepageToUtf8(cp, raw, (int)rd, &utf8, &utf8Len)) {
            m_buffer->text(utf8);
            detectEOL(utf8, utf8Len);
            delete[] utf8;
        } else {
            // Fallback: load as raw bytes
            m_buffer->text(raw);
            detectEOL(raw, (int)rd);
        }
        delete[] raw;
        break;
    }
    }

    fl_strlcpy(m_filePath, path, FL_PATH_MAX);
    markClean();
    return true;
}

// ---------------------------------------------------------------------------
// File saving with encoding conversion
// ---------------------------------------------------------------------------

bool Document::saveFile(const char *path) {
    return saveWithEncoding(path, m_encoding);
}

bool Document::saveFile() {
    if (!m_filePath[0]) return false;
    return saveFile(m_filePath);
}

bool Document::saveWithEncoding(const char *path, Encoding enc) {
    const char *text = m_buffer->text();
    int textLen = (int)strlen(text);

    // Save-time formatting pipeline: copy the text, then apply (in order)
    // per-line trailing trim -> leading blank trim -> ending blank trim
    // -> tab expansion -> EOL normalization. All steps are pure text
    // transforms; the live buffer is untouched.
    char *trimBuf = nullptr;
    const char *src = text;
    int srcLen = textLen;
    if (m_trimTrailing || m_trimLeadingBlank || m_trimEndingBlank) {
        trimBuf = new char[textLen + 1];
        memcpy(trimBuf, text, textLen);
        trimBuf[textLen] = '\0';
        src = trimBuf;
        srcLen = textLen;
        if (m_trimTrailing)
            srcLen = trimTrailingWhitespace(trimBuf, srcLen);
        if (m_trimLeadingBlank)
            srcLen = trimLeadingBlank(trimBuf, srcLen);
        if (m_trimEndingBlank)
            srcLen = trimEndingBlank(trimBuf, srcLen);
    }

    // Tab expansion (grows the buffer, so it needs its own allocation).
    char *expanded = nullptr;
    int expandedLen = 0;
    if (m_expandTabs) {
        expandTabsToSpaces(src, srcLen, m_tabWidth, &expanded, &expandedLen);
        if (expanded) {
            src = expanded;
            srcLen = expandedLen;
        }
    }

    // Normalize line endings before encoding conversion
    char *normalized = nullptr;
    int normalizedLen = 0;
    normalizeLineEndings(src, srcLen, m_eol, &normalized, &normalizedLen);

    const char *writeData = normalized ? normalized : src;
    int writeLen = normalized ? normalizedLen : srcLen;

    FILE *fp = fl_fopen(path, "wb");
    if (!fp) {
        char _msg[512];
        snprintf(_msg, sizeof(_msg), "Failed to save file\n%s\n%s", path, strerror(errno));
        if (UiBridge *b = uiBridge()) b->message("Error", _msg, "OK");
        ::free((void*)text);
        delete[] expanded;
        delete[] normalized;
        delete[] trimBuf;
        return false;
    }

    bool ok = true;
    switch (enc) {
    case Encoding::UTF8:
        fwrite(writeData, 1, writeLen, fp);
        break;

    case Encoding::UTF8_BOM: {
        const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
        fwrite(bom, 1, 3, fp);
        fwrite(writeData, 1, writeLen, fp);
        break;
    }

    case Encoding::UTF16_LE: {
        const unsigned char bom[2] = { 0xFF, 0xFE };
        fwrite(bom, 1, 2, fp);
        wchar_t *w = nullptr; int wlen = 0;
        if (utf8ToWide(writeData, writeLen, &w, &wlen)) {
            for (int i = 0; i < wlen; ++i) {
                unsigned char lo = w[i] & 0xFF;
                unsigned char hi = (w[i] >> 8) & 0xFF;
                fwrite(&lo, 1, 1, fp);
                fwrite(&hi, 1, 1, fp);
            }
            delete[] w;
        } else { ok = false; }
        break;
    }

    case Encoding::UTF16_BE: {
        const unsigned char bom[2] = { 0xFE, 0xFF };
        fwrite(bom, 1, 2, fp);
        wchar_t *w = nullptr; int wlen = 0;
        if (utf8ToWide(writeData, writeLen, &w, &wlen)) {
            for (int i = 0; i < wlen; ++i) {
                unsigned char lo = w[i] & 0xFF;
                unsigned char hi = (w[i] >> 8) & 0xFF;
                fwrite(&hi, 1, 1, fp);
                fwrite(&lo, 1, 1, fp);
            }
            delete[] w;
        } else { ok = false; }
        break;
    }

    case Encoding::GBK:
    case Encoding::ANSI:
    case Encoding::BIG5:
    case Encoding::SHIFT_JIS:
    case Encoding::EUC_KR: {
        int cp = encodingToCodePage(enc);
        if (cp == 0) cp = CP_ACP;
        char *out = nullptr; int outLen = 0;
        if (utf8ToCodepage(cp, writeData, writeLen, &out, &outLen)) {
            fwrite(out, 1, outLen, fp);
            delete[] out;
        } else {
            // Fallback: write as UTF-8
            fwrite(writeData, 1, writeLen, fp);
        }
        break;
    }
    }

    fclose(fp);
    ::free((void*)text);
    delete[] expanded;
    delete[] normalized;
    delete[] trimBuf;

    if (ok) {
        fl_strlcpy(m_filePath, path, FL_PATH_MAX);
        markClean();
    }
    return ok;
}
