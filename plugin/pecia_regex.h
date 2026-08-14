// pecia_regex.h - C interface exported by pecia_regex.dll.
// A tiny UTF-8 regex engine wrapper around PCRE2 (8-bit, Unicode).
// Loaded on demand by Lua scripts (LoadLibrary) and unloaded when the
// script finishes, so the regex engine costs nothing while unused.
#pragma once

#if defined(_WIN32) && defined(PR_EXPORT)
#define PR_API __declspec(dllexport)
#else
#define PR_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Version string of the embedded PCRE2, e.g. "10.47".
PR_API int pr_version(char *buf, int cap);

// Find every non-overlapping match of `pattern` in `text` (UTF-8 byte
// offsets). Returns the match count; -1 = pattern compile error,
// -2 = match runtime error. On error, errbuf (if any) holds a message.
// `starts`/`ends` are the first `maxMatches` offsets (NULL ok).
PR_API int pr_findall(const char *text, int textLen,
                      const char *pattern,
                      int *starts, int *ends, int maxMatches,
                      char *errbuf, int errcap);

// Replace every match of `pattern` in `text` with `replacement`
// ($1, ${name} capture groups supported). On success returns the new
// length and stores a malloc'd buffer in *out (free with pr_free);
// returns -1 (compile) / -2 (runtime/substitute) on error.
PR_API int pr_substitute_all(const char *text, int textLen,
                             const char *pattern, const char *replacement,
                             char **out, int *outLen,
                             char *errbuf, int errcap);

PR_API void pr_free(void *p);

#ifdef __cplusplus
}
#endif
