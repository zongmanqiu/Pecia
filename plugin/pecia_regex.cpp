// pecia_regex.cpp - PCRE2 (8-bit, UTF) wrapper exported by
// pecia_regex.dll. Intentionally small: compile/match/substitute only,
// no JIT (keeps the DLL lean). All text is treated as UTF-8 and
// patterns are compiled with PCRE2_UTF | PCRE2_UCP so \w etc. see
// Unicode characters and '.' never splits a multi-byte character.
#define PR_EXPORT 1
#include "pecia_regex.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include "pcre2.h"

#include <cstdlib>
#include <cstring>

namespace {

// Compile options: UTF-8 with Unicode properties (UCP makes \w, \d, \b
// character-class aware). No implicit multiline - scripts use (?m) or
// (?s) inline when they need it.
constexpr uint32_t kCompileOptions = PCRE2_UTF | PCRE2_UCP;

// Standard iteration options: PCRE2_NOTEMPTY_ATSTART | PCRE2_ANCHORED
// lets empty matches advance past the previous end.
constexpr uint32_t kIterOptions = PCRE2_NOTEMPTY_ATSTART | PCRE2_ANCHORED;

// Bounded backtrack budget. PCRE2's default MATCH_LIMIT is ~10 million, so
// an adversarial pattern like (a+)+b against a long run of 'a's would
// otherwise spin the caller (the Lua engine runs on the GUI thread) for a
// very long time. Cap it to keep the regex engine always responsive;
// exceeding the limit returns an error instead of hanging.
constexpr uint32_t kMatchLimit   = 100'000;
constexpr uint32_t kDepthLimit   = 100;

// Create the process-reusable, bounded match context (thread-safe after
// creation per PCRE2 docs). Ownership is static; never freed.
pcre2_match_context *matchCtx() {
    static pcre2_match_context *ctx = [] {
        pcre2_match_context *c = pcre2_match_context_create_8(nullptr);
        if (c) {
            pcre2_set_match_limit_8(c, kMatchLimit);
            pcre2_set_depth_limit_8(c, kDepthLimit);
        }
        return c;
    }();
    return ctx;
}

pcre2_code *compilePattern(const char *pattern, char *errbuf, int errcap) {
    int errcode = 0;
    PCRE2_SIZE erroffset = 0;
    pcre2_code *code = pcre2_compile_8(
        (PCRE2_SPTR8)pattern, PCRE2_ZERO_TERMINATED, kCompileOptions,
        &errcode, &erroffset, nullptr);
    if (!code) {
        if (errbuf && errcap > 0) {
            pcre2_get_error_message_8(errcode, (PCRE2_UCHAR8 *)errbuf, errcap);
        }
        return nullptr;
    }
    return code;
}

int fillError(char *errbuf, int errcap, const char *msg) {
    if (errbuf && errcap > 0) {
        int n = (int)strlen(msg);
        if (n >= errcap) n = errcap - 1;
        memcpy(errbuf, msg, n);
        errbuf[n] = 0;
    }
    return -2;
}

} // namespace

extern "C" {

PR_API int pr_version(char *buf, int cap) {
    if (!buf || cap <= 0) return 0;
    uint8_t vbuf[64];
    int n = pcre2_config_8(PCRE2_CONFIG_VERSION, vbuf);
    if (n < 0) return 0;
    int copy = n < cap ? n : cap - 1;
    memcpy(buf, vbuf, copy);
    buf[copy] = 0;
    return copy;
}

PR_API int pr_findall(const char *text, int textLen,
                      const char *pattern,
                      int *starts, int *ends, int maxMatches,
                      char *errbuf, int errcap) {
    pcre2_code *code = compilePattern(pattern, errbuf, errcap);
    if (!code) return -1;

    pcre2_match_data *md = pcre2_match_data_create_from_pattern_8(code, nullptr);
    if (!md) {
        pcre2_code_free_8(code);
        return fillError(errbuf, errcap, "out of memory");
    }

    int count = 0;
    PCRE2_SIZE offset = 0;
    while (count < maxMatches || maxMatches <= 0) {
        // Standard global-match iteration: try anchored (so empty matches
        // don't loop), fall back to an unanchored scan on NOMATCH.
        int rc = pcre2_match_8(code, (PCRE2_SPTR8)text, (PCRE2_SIZE)textLen,
                               offset, kIterOptions, md, matchCtx());
        if (rc == PCRE2_ERROR_NOMATCH) {
            rc = pcre2_match_8(code, (PCRE2_SPTR8)text, (PCRE2_SIZE)textLen,
                               offset, 0, md, matchCtx());
            if (rc == PCRE2_ERROR_NOMATCH) break;
        }
        if (rc < 0) {
            pcre2_match_data_free_8(md);
            pcre2_code_free_8(code);
            return fillError(errbuf, errcap, "match error");
        }
        PCRE2_SIZE *ov = pcre2_get_ovector_pointer_8(md);
        if (starts && count < maxMatches) starts[count] = (int)ov[0];
        if (ends && count < maxMatches) ends[count] = (int)ov[1];
        ++count;
        offset = ov[1];
        if (ov[1] == ov[0]) {          // empty match: force advance
            if ((PCRE2_SIZE)offset >= (PCRE2_SIZE)textLen) break;
            ++offset;
        }
    }
    pcre2_match_data_free_8(md);
    pcre2_code_free_8(code);
    return count;
}

PR_API int pr_substitute_all(const char *text, int textLen,
                             const char *pattern, const char *replacement,
                             char **out, int *outLen,
                             char *errbuf, int errcap) {
    if (!out || !outLen) return fillError(errbuf, errcap, "bad output pointer");
    *out = nullptr;
    *outLen = 0;

    // Phase 1: count matches first - a clean "no match" answer without
    // relying on pcre2_substitute's return-value semantics.
    int nm = pr_findall(text, textLen, pattern, nullptr, nullptr, 0,
                        errbuf, errcap);
    if (nm < 0) return nm;   // -1 compile error (errbuf set), -2 runtime
    if (nm == 0) return 0;   // no match: out stays null

    pcre2_code *code = compilePattern(pattern, errbuf, errcap);
    if (!code) return -1;

    PCRE2_SIZE outLenBytes = 0;
    int rc = pcre2_substitute_8(
        code, (PCRE2_SPTR8)text, (PCRE2_SIZE)textLen, 0,
        PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_OVERFLOW_LENGTH |
        PCRE2_SUBSTITUTE_EXTENDED,
        nullptr, matchCtx(),
        (PCRE2_SPTR8)replacement, PCRE2_ZERO_TERMINATED,
        nullptr, &outLenBytes);
    if (rc == PCRE2_ERROR_NOMEMORY) {
        // outLenBytes now holds the required size.
        char *buf = (char *)malloc(outLenBytes + 1);
        if (!buf) {
            pcre2_code_free_8(code);
            return fillError(errbuf, errcap, "out of memory");
        }
        PCRE2_SIZE done = outLenBytes;
        int rc2 = pcre2_substitute_8(
            code, (PCRE2_SPTR8)text, (PCRE2_SIZE)textLen, 0,
            PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_EXTENDED,
            nullptr, matchCtx(),
            (PCRE2_SPTR8)replacement, PCRE2_ZERO_TERMINATED,
            (PCRE2_UCHAR8 *)buf, &done);
        if (rc2 < 0) {
            free(buf);
            pcre2_code_free_8(code);
            return fillError(errbuf, errcap, "substitute error");
        }
        pcre2_code_free_8(code);
        buf[done] = 0;
        *out = buf;
        *outLen = (int)done;
        return *outLen;
    }
    pcre2_code_free_8(code);
    if (rc < 0) return fillError(errbuf, errcap, "substitute error");
    return fillError(errbuf, errcap, "substitute buffer too small");
}

PR_API void pr_free(void *p) {
    free(p);
}

} // extern "C"
