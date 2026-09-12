// LuaEngine.cpp - embedded Lua 5.5 sandbox for user text-processing scripts
#include "LuaEngine.h"
#include "core/OpLog.h"

#include <FL/Fl_Text_Buffer.H>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include <cstring>
#include <ctime>
#include <vector>
#include <atomic>
#include <string>

#include <windows.h>
#include <shellapi.h>

extern "C" {
#define PCRE2_CODE_UNIT_WIDTH 8
#include "pcre2.h"
}

namespace {

// UTF-8 -> UTF-16：win:open 的目标可能是中文文件路径，须走宽字符 API
static std::wstring widen(const char *utf8) {
    if (!utf8 || !*utf8) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &w[0], n);
    while (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

// --------------------------------------------------------------------------
// Script execution budget. Lua has no built-in timeout, so an innocent
// `while true do end` (or a pathological regex) would block the GUI thread
// forever. We install an instruction-counting debug hook in run() and raise
// a Lua error once a run burns through kMaxInstructions, so the editor and
// the (30s-waiting) pipe server are never frozen by a runaway script.
// Scripts run serially on the FLTK main thread (via Fl::awake), so a plain
// counter is race-free here.
// --------------------------------------------------------------------------
const long long kMaxInstructions = 5'000'000;   // ~a few hundred ms of work
std::atomic<long long> g_instructionBudget{0};

void s_instructionHook(lua_State *L, lua_Debug *) {
    // LUA_MASKCOUNT invokes the hook every N instructions; the count we pass
    // to lua_sethook is the decrement per call. Exceeding the budget raises
    // a Lua error (via longjmp), which unwinds pcall and reports a timeout
    // to the user instead of hanging the GUI thread forever.
    long long remaining = g_instructionBudget.fetch_sub(2048) - 2048;
    if (remaining <= 0) {
        g_instructionBudget.store(0);
        luaL_error(L, "script timeout: execution exceeded the instruction limit");
    }
}

// win table callbacks (defined below, referenced from the constructor).
int l_win_message(lua_State *L);
int l_win_question(lua_State *L);
int l_win_clipboard_get(lua_State *L);
int l_win_clipboard_set(lua_State *L);
int l_win_open(lua_State *L);

// Stable addresses used as lightuserdata keys in the Lua registry.
// Each LuaEngine::run() stores its current scratch state / output sink at
// these addresses before running a script, and the C callbacks fetch them
// back. Cleared after every run so no callback can dereference a dangling
// pointer later.
static char kScratchKey;  // registry slot for LuaScratch*
static char kOutputKey;   // registry slot for std::string* (print sink)

// Per-run state shared with the callbacks. The script edits a scratch
// buffer, never the live document.
struct LuaScratch {
    Fl_Text_Buffer *buf;     // scratch buffer the script edits
    int            *cursor;  // scratch insert position
};

LuaScratch *getScratch(lua_State *L) {
    lua_pushlightuserdata(L, &kScratchKey);
    lua_rawget(L, LUA_REGISTRYINDEX);
    LuaScratch *s = static_cast<LuaScratch *>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return s;
}

std::string *getOutput(lua_State *L) {
    lua_pushlightuserdata(L, &kOutputKey);
    lua_rawget(L, LUA_REGISTRYINDEX);
    std::string *out = static_cast<std::string *>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return out;
}

// editor:get_selection() -> string
// Returns the current primary selection. If nothing is selected, returns "".
// Uses lua_pushlstring with the exact byte length so selections containing
// embedded NUL bytes survive the round-trip.
int l_get_selection(lua_State *L) {
    LuaScratch *s = getScratch(L);
    if (!s || !s->buf) { lua_pushliteral(L, ""); return 1; }
    int start = 0, end = 0;
    if (!s->buf->selection_position(&start, &end) || end <= start) {
        lua_pushliteral(L, "");
        return 1;
    }
    char *sel = s->buf->text_range(start, end);
    // text_range() never returns NULL, but guard defensively: pass the
    // exact byte length so embedded NULs survive; the returned buffer is
    // end-start+1 bytes (NUL-terminated), so the length argument must
    // match end-start exactly.
    lua_pushlstring(L, sel ? sel : "", (size_t)(end - start));
    if (sel) free(sel);
    return 1;
}

// editor:replace_selection(text)
// Replaces the current primary selection with `text`. If nothing is
// selected, inserts `text` at the cursor and selects the newly inserted
// text, so a subsequent replace_selection() call would replace it again
// (mirroring SciTE's behaviour and making chained operations intuitive).
int l_replace_selection(lua_State *L) {
    LuaScratch *s = getScratch(L);
    if (!s || !s->buf) return 0;
    // arg 1 = self, arg 2 = text. Use checklstring so embedded NUL bytes
    // survive (get_selection uses pushlstring for the same reason).
    size_t len = 0;
    const char *text = luaL_checklstring(L, 2, &len);
    int start = 0, end = 0;
    if (s->buf->selection_position(&start, &end)) {
        s->buf->replace(start, end, text, (int)len);
        s->buf->select(start, start + (int)len);
        *s->cursor = start + (int)len;
    } else {
        int pos = *s->cursor;
        if (pos < 0) pos = 0;
        s->buf->insert(pos, text, (int)len);
        s->buf->select(pos, pos + (int)len);
        *s->cursor = pos + (int)len;
    }
    return 0;
}

// editor:get_cursor() -> number
// Returns the current insert position (byte offset into the buffer).
// Used by wrapping-pair scripts to place the cursor between the two
// halves of an inserted empty pair.
int l_get_cursor(lua_State *L) {
    LuaScratch *s = getScratch(L);
    if (!s || !s->buf) { lua_pushinteger(L, 0); return 1; }
    int pos = *s->cursor;
    if (pos < 0) pos = 0;
    if (pos > s->buf->length()) pos = s->buf->length();
    lua_pushinteger(L, pos);
    return 1;
}

// editor:set_cursor(pos)
// Moves the insert position. Out-of-range values clamp to the buffer
// bounds. Also clears the selection: in editor semantics, moving the
// cursor cancels the selection (so after insert-pair + set_cursor the
// user can type inside the pair instead of replacing it).
int l_set_cursor(lua_State *L) {
    LuaScratch *s = getScratch(L);
    if (!s || !s->buf) return 0;
    int pos = (int)luaL_checkinteger(L, 2);
    if (pos < 0) pos = 0;
    if (pos > s->buf->length()) pos = s->buf->length();
    *s->cursor = pos;
    s->buf->unselect();
    return 0;
}

// print(...) -> appends to the engine's output sink.
// Mimics standard Lua print: arguments are converted to strings via tostring,
// separated by tabs, terminated with a newline.
int l_print(lua_State *L) {
    std::string *out = getOutput(L);
    int n = lua_gettop(L);
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (int i = 1; i <= n; i++) {
        if (i > 1) luaL_addchar(&b, '\t');
        const char *s = luaL_tolstring(L, i, nullptr);
        luaL_addstring(&b, s);
        lua_pop(L, 1);
    }
    luaL_addchar(&b, '\n');
    luaL_pushresult(&b);
    size_t len = 0;
    const char *str = lua_tolstring(L, -1, &len);
    if (out && str && len) out->append(str, len);
    lua_pop(L, 1);
    return 0;
}

// win:message(text) - modal OK message box.
int l_win_message(lua_State *L) {
    const char *text = luaL_optstring(L, 1, "");
    MessageBoxA(nullptr, text, "Pecia", MB_OK | MB_ICONINFORMATION | MB_TASKMODAL);
    return 0;
}

// win:question(text) -> bool - modal Yes/No box.
int l_win_question(lua_State *L) {
    const char *text = luaL_optstring(L, 1, "");
    int r = MessageBoxA(nullptr, text, "Pecia", MB_YESNO | MB_ICONQUESTION | MB_TASKMODAL);
    lua_pushboolean(L, r == IDYES);
    return 1;
}

// win:clipboard_get() -> string - read the clipboard (UTF-8).
int l_win_clipboard_get(lua_State *L) {
    if (!OpenClipboard(nullptr)) {
        lua_pushliteral(L, "");
        return 1;
    }
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    std::string text;
    if (h) {
        const wchar_t *w = static_cast<const wchar_t *>(GlobalLock(h));
        if (w) {
            int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
            if (n > 1) {
                text.resize(n - 1);
                WideCharToMultiByte(CP_UTF8, 0, w, -1, &text[0], n, nullptr, nullptr);
            }
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}

// win:clipboard_set(text) - write the clipboard (UTF-8).
int l_win_clipboard_set(lua_State *L) {
    size_t len = 0;
    const char *text = luaL_checklstring(L, 1, &len);
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, (int)len, nullptr, 0);
    if (wlen > 0 && OpenClipboard(nullptr)) {
        EmptyClipboard();
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (wlen + 1) * sizeof(wchar_t));
        if (h) {
            wchar_t *w = static_cast<wchar_t *>(GlobalLock(h));
            if (w) {
                MultiByteToWideChar(CP_UTF8, 0, text, (int)len, w, wlen);
                w[wlen] = 0;
                GlobalUnlock(h);
                SetClipboardData(CF_UNICODETEXT, h);
            } else {
                GlobalFree(h);
            }
        }
        CloseClipboard();
    }
    return 0;
}

// win:open(path_or_url) - open a file or URL with the default handler.
int l_win_open(lua_State *L) {
    const char *target = luaL_checkstring(L, 1);
    if (*target) {
        ShellExecuteW(nullptr, L"open", widen(target).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    return 0;
}

// insert(text, offset) - insert text at cursor, then shift cursor by offset.
// Replaces the common "editor:replace_selection + editor:set_cursor" pattern
// for the "no selection" branch. Used as a global function (no self parameter).
int l_insert(lua_State *L) {
    LuaScratch *s = getScratch(L);
    if (!s || !s->buf) return 0;
    size_t len = 0;
    const char *text = luaL_checklstring(L, 1, &len);
    int offset = (int)luaL_optinteger(L, 2, 0);
    int pos = *s->cursor;
    if (pos < 0) pos = 0;
    if (pos > s->buf->length()) pos = s->buf->length();
    s->buf->insert(pos, text, (int)len);
    s->buf->select(pos, pos + (int)len);
    int newPos = pos + (int)len + offset;
    if (newPos < 0) newPos = 0;
    if (newPos > s->buf->length()) newPos = s->buf->length();
    *s->cursor = newPos;
    return 0;
}

} // namespace

// ==========================================================================
// rex table — Textus-compatible PCRE2 regex API (directly linked, no DLL)
// ==========================================================================

// Compile a PCRE2 pattern with UTF-8 + UCP. Returns nullptr on error
// (error message pushed to Lua stack).
static pcre2_code_8 *rex_compile(lua_State *L, const char *pattern) {
    int errcode = 0;
    PCRE2_SIZE erroffset = 0;
    uint32_t opts = PCRE2_UTF | PCRE2_UCP;
    pcre2_code_8 *re = pcre2_compile(
        (PCRE2_SPTR8)pattern, PCRE2_ZERO_TERMINATED,
        opts, &errcode, &erroffset, nullptr);
    if (!re) {
        PCRE2_UCHAR8 errbuf[256];
        pcre2_get_error_message(errcode, errbuf, sizeof(errbuf));
        lua_pushfstring(L, "Regex compile error: %s (at offset %d)",
                        (const char *)errbuf, (int)erroffset);
        return nullptr;
    }
    return re;
}

// Expand $0 $& $1-$99 backreferences in a replacement template.
static std::string rex_expand_repl(const char *tpl, pcre2_match_data_8 *md,
                                   const char *subject, PCRE2_SIZE /*subject_len*/) {
    PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
    std::string result;
    const char *p = tpl;
    while (*p) {
        if (*p == '$') {
            ++p;
            if (*p == '$') { result += '$'; ++p; }
            else if (*p == '0' || *p == '&') {
                ++p;
                result.append(subject + ov[0], ov[1] - ov[0]);
            }
            else if (*p >= '1' && *p <= '9') {
                int n = *p - '0';
                ++p;
                if (*p >= '0' && *p <= '9') {
                    n = n * 10 + (*p - '0');
                    ++p;
                }
                PCRE2_SIZE ov_count = pcre2_get_ovector_count(md);
                if (2 * n + 1 < (int)(ov_count * 2)) {
                    PCRE2_SIZE start = ov[2 * n];
                    PCRE2_SIZE end = ov[2 * n + 1];
                    if (start != PCRE2_UNSET && end != PCRE2_UNSET)
                        result.append(subject + start, end - start);
                }
            } else {
                result += '$';
            }
        } else {
            result += *p++;
        }
    }
    return result;
}

// rex.version() -> string
static int l_rex_version(lua_State *L) {
    lua_pushfstring(L, "%d.%02d", PCRE2_MAJOR, PCRE2_MINOR);
    return 1;
}

// rex.new(pattern) -> regex userdata
static int l_rex_new(lua_State *L) {
    const char *pattern = luaL_checkstring(L, 1);
    pcre2_code_8 *re = rex_compile(L, pattern);
    if (!re) return lua_error(L);
    pcre2_code_8 **ud = (pcre2_code_8 **)lua_newuserdata(L, sizeof(pcre2_code_8 *));
    *ud = re;
    luaL_setmetatable(L, "rex_regex");
    return 1;
}

// Internal: find on a compiled regex. Returns from, to, cap1, cap2, ... or nil, nil.
static int rex_do_find(lua_State *L, pcre2_code_8 *re,
                       const char *subject, int subject_len, int init) {
    pcre2_match_data_8 *md = pcre2_match_data_create_from_pattern(re, nullptr);
    PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
    // init is 1-indexed Lua position; convert to 0-indexed byte offset
    int start = (init > 0) ? init - 1 : 0;
    if (start > subject_len) start = subject_len;
    int rc = pcre2_match(re, (PCRE2_SPTR8)subject + start, subject_len - start,
                         0, 0, md, nullptr);
    if (rc < 0) {
        pcre2_match_data_free(md);
        lua_pushnil(L);
        lua_pushnil(L);
        return 2;
    }
    // from (1-indexed), to (1-indexed, exclusive end = lrexlib convention)
    lua_pushinteger(L, start + (lua_Integer)ov[0] + 1);
    lua_pushinteger(L, start + (lua_Integer)ov[1]);
    int ncap = rc;
    for (int i = 1; i < ncap; ++i) {
        PCRE2_SIZE s = ov[2 * i];
        PCRE2_SIZE e = ov[2 * i + 1];
        if (s == PCRE2_UNSET || e == PCRE2_UNSET) {
            lua_pushnil(L);
        } else {
            lua_pushlstring(L, subject + start + s, e - s);
        }
    }
    pcre2_match_data_free(md);
    return 2 + ncap - 1;
}

// rex.find(subject, pattern, init?) -> from, to, cap1, cap2, ...
static int l_rex_find(lua_State *L) {
    size_t slen = 0;
    const char *subject = luaL_checklstring(L, 1, &slen);
    const char *pattern = luaL_checkstring(L, 2);
    int init = (int)luaL_optinteger(L, 3, 1);
    pcre2_code_8 *re = rex_compile(L, pattern);
    if (!re) return lua_error(L);
    int nret = rex_do_find(L, re, subject, (int)slen, init);
    pcre2_code_free(re);
    return nret;
}

// rex.match(subject, pattern, init?) -> string | nil
static int l_rex_match(lua_State *L) {
    size_t slen = 0;
    const char *subject = luaL_checklstring(L, 1, &slen);
    const char *pattern = luaL_checkstring(L, 2);
    int init = (int)luaL_optinteger(L, 3, 1);
    pcre2_code_8 *re = rex_compile(L, pattern);
    if (!re) return lua_error(L);
    pcre2_match_data_8 *md = pcre2_match_data_create_from_pattern(re, nullptr);
    PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
    int start = (init > 0) ? init - 1 : 0;
    if (start > (int)slen) start = (int)slen;
    int rc = pcre2_match(re, (PCRE2_SPTR8)subject + start, slen - start,
                         0, 0, md, nullptr);
    if (rc < 0) {
        lua_pushnil(L);
    } else {
        lua_pushlstring(L, subject + start + ov[0],
                        ov[1] - ov[0]);
    }
    pcre2_match_data_free(md);
    pcre2_code_free(re);
    return 1;
}

// rex.gmatch(subject, pattern) -> {{match, cap1, ...}, ...}
static int l_rex_gmatch(lua_State *L) {
    size_t slen = 0;
    const char *subject = luaL_checklstring(L, 1, &slen);
    const char *pattern = luaL_checkstring(L, 2);
    pcre2_code_8 *re = rex_compile(L, pattern);
    if (!re) return lua_error(L);
    pcre2_match_data_8 *md = pcre2_match_data_create_from_pattern(re, nullptr);
    PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
    lua_newtable(L);
    int idx = 0;
    int offset = 0;
    while (offset <= (int)slen) {
        int rc = pcre2_match(re, (PCRE2_SPTR8)subject + offset, slen - offset,
                             0, 0, md, nullptr);
        if (rc < 0) break;
        int mstart = (int)ov[0];
        int mend = (int)ov[1];
        if (mstart == mend && mend < (int)slen) {
            // Zero-width match: advance by 1 code unit to avoid infinite loop
            ++offset;
            continue;
        }
        ++idx;
        lua_newtable(L);
        // element 1 = whole match
        lua_pushlstring(L, subject + offset + mstart, mend - mstart);
        lua_rawseti(L, -2, 1);
        // elements 2..N = capture groups
        for (int i = 1; i < rc; ++i) {
            PCRE2_SIZE s = ov[2 * i];
            PCRE2_SIZE e = ov[2 * i + 1];
            if (s == PCRE2_UNSET || e == PCRE2_UNSET)
                lua_pushnil(L);
            else
                lua_pushlstring(L, subject + offset + s, e - s);
            lua_rawseti(L, -2, i + 1);
        }
        lua_rawseti(L, -2, idx);
        offset += mend;
    }
    pcre2_match_data_free(md);
    pcre2_code_free(re);
    return 1;
}

// rex.gsub(subject, pattern, repl, max?) -> result, count
static int l_rex_gsub(lua_State *L) {
    size_t slen = 0;
    const char *subject = luaL_checklstring(L, 1, &slen);
    const char *pattern = luaL_checkstring(L, 2);
    int has_repl_fn = lua_isfunction(L, 3);
    const char *repl_str = has_repl_fn ? nullptr : luaL_checkstring(L, 3);
    int max_count = (int)luaL_optinteger(L, 4, 0); // 0 = unlimited
    pcre2_code_8 *re = rex_compile(L, pattern);
    if (!re) return lua_error(L);

    // One manual loop for BOTH replacement kinds:
    //   * string tpl -> rex_expand_repl() ($0/$&/$1-$99/$$), honours max
    //   * Lua function -> repl(match, cap1, ...) called per match, honours max
    // Previously the string case used pcre2_substitute for max_count == 0 and
    // fell back to this loop only when max_count > 0 — but that fallback
    // unconditionally called parameter 3 as a Lua function, so a STRING repl
    // with an explicit max raised a runtime error. A single loop avoids the
    // split path entirely and keeps count semantics identical for both kinds.
    pcre2_match_data_8 *md = pcre2_match_data_create_from_pattern(re, nullptr);
    PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
    std::string result;
    int count = 0;
    int offset = 0;
    while (offset <= (int)slen) {
        if (max_count > 0 && count >= max_count) break;
        int rc = pcre2_match(re, (PCRE2_SPTR8)subject + offset, slen - offset,
                             0, 0, md, nullptr);
        if (rc < 0) break;
        int mstart = (int)ov[0];
        int mend = (int)ov[1];
        result.append(subject + offset, mstart);
        if (has_repl_fn) {
            lua_pushvalue(L, 3);
            lua_pushlstring(L, subject + offset + mstart, mend - mstart);
            for (int i = 1; i < rc; ++i) {
                PCRE2_SIZE s = ov[2 * i];
                PCRE2_SIZE e = ov[2 * i + 1];
                if (s == PCRE2_UNSET || e == PCRE2_UNSET)
                    lua_pushnil(L);
                else
                    lua_pushlstring(L, subject + offset + s, e - s);
            }
            lua_call(L, rc, 1);
            size_t rlen = 0;
            const char *rstr = lua_tolstring(L, -1, &rlen);
            if (rstr) result.append(rstr, rlen);
            lua_pop(L, 1);
        } else {
            result += rex_expand_repl(repl_str, md, subject + offset, slen - offset);
        }
        offset += mend;
        ++count;
    }
    result.append(subject + offset);
    pcre2_match_data_free(md);
    pcre2_code_free(re);
    lua_pushlstring(L, result.data(), result.size());
    lua_pushinteger(L, count);
    return 2;
}

// rex.split(subject, pattern) -> {piece1, piece2, ...}
static int l_rex_split(lua_State *L) {
    size_t slen = 0;
    const char *subject = luaL_checklstring(L, 1, &slen);
    const char *pattern = luaL_checkstring(L, 2);
    pcre2_code_8 *re = rex_compile(L, pattern);
    if (!re) return lua_error(L);
    pcre2_match_data_8 *md = pcre2_match_data_create_from_pattern(re, nullptr);
    PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
    lua_newtable(L);
    int idx = 0;
    int offset = 0;
    while (offset <= (int)slen) {
        int rc = pcre2_match(re, (PCRE2_SPTR8)subject + offset, slen - offset,
                             0, 0, md, nullptr);
        if (rc < 0) {
            // No more matches: push the rest
            lua_pushlstring(L, subject + offset, slen - offset);
            lua_rawseti(L, -2, ++idx);
            break;
        }
        int mstart = (int)ov[0];
        int mend = (int)ov[1];
        // Push piece before the match
        lua_pushlstring(L, subject + offset, mstart);
        lua_rawseti(L, -2, ++idx);
        if (mstart == mend && mend < (int)slen) {
            // Zero-width match: skip one byte to avoid infinite loop
            ++offset;
        } else {
            offset += mend;
        }
    }
    pcre2_match_data_free(md);
    pcre2_code_free(re);
    return 1;
}

// RegexWrapper methods (compiled regex object)

// re:find(subject, init?) -> from, to, cap1, cap2, ...
static int l_rex_re_find(lua_State *L) {
    pcre2_code_8 *re = *(pcre2_code_8 **)luaL_checkudata(L, 1, "rex_regex");
    size_t slen = 0;
    const char *subject = luaL_checklstring(L, 2, &slen);
    int init = (int)luaL_optinteger(L, 3, 1);
    return rex_do_find(L, re, subject, (int)slen, init);
}

// re:match(subject, init?) -> string | nil
static int l_rex_re_match(lua_State *L) {
    pcre2_code_8 *re = *(pcre2_code_8 **)luaL_checkudata(L, 1, "rex_regex");
    size_t slen = 0;
    const char *subject = luaL_checklstring(L, 2, &slen);
    int init = (int)luaL_optinteger(L, 3, 1);
    pcre2_match_data_8 *md = pcre2_match_data_create_from_pattern(re, nullptr);
    PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
    int start = (init > 0) ? init - 1 : 0;
    if (start > (int)slen) start = (int)slen;
    int rc = pcre2_match(re, (PCRE2_SPTR8)subject + start, slen - start,
                         0, 0, md, nullptr);
    if (rc < 0) {
        lua_pushnil(L);
    } else {
        lua_pushlstring(L, subject + start + ov[0],
                        ov[1] - ov[0]);
    }
    pcre2_match_data_free(md);
    return 1;
}

// re:gmatch(subject) -> {{match, cap1, ...}, ...}
static int l_rex_re_gmatch(lua_State *L) {
    pcre2_code_8 *re = *(pcre2_code_8 **)luaL_checkudata(L, 1, "rex_regex");
    size_t slen = 0;
    const char *subject = luaL_checklstring(L, 2, &slen);
    pcre2_match_data_8 *md = pcre2_match_data_create_from_pattern(re, nullptr);
    PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
    lua_newtable(L);
    int idx = 0;
    int offset = 0;
    while (offset <= (int)slen) {
        int rc = pcre2_match(re, (PCRE2_SPTR8)subject + offset, slen - offset,
                             0, 0, md, nullptr);
        if (rc < 0) break;
        int mstart = (int)ov[0];
        int mend = (int)ov[1];
        if (mstart == mend && mend < (int)slen) { ++offset; continue; }
        ++idx;
        lua_newtable(L);
        lua_pushlstring(L, subject + offset + mstart, mend - mstart);
        lua_rawseti(L, -2, 1);
        for (int i = 1; i < rc; ++i) {
            PCRE2_SIZE s = ov[2 * i];
            PCRE2_SIZE e = ov[2 * i + 1];
            if (s == PCRE2_UNSET || e == PCRE2_UNSET) lua_pushnil(L);
            else lua_pushlstring(L, subject + offset + s, e - s);
            lua_rawseti(L, -2, i + 1);
        }
        lua_rawseti(L, -2, idx);
        offset += mend;
    }
    pcre2_match_data_free(md);
    return 1;
}

// re:gsub(subject, repl, max?) -> result, count
static int l_rex_re_gsub(lua_State *L) {
    pcre2_code_8 *re = *(pcre2_code_8 **)luaL_checkudata(L, 1, "rex_regex");
    size_t slen = 0;
    const char *subject = luaL_checklstring(L, 2, &slen);
    int has_repl_fn = lua_isfunction(L, 3);
    const char *repl_str = has_repl_fn ? nullptr : luaL_checkstring(L, 3);
    int max_count = (int)luaL_optinteger(L, 4, 0);
    pcre2_match_data_8 *md = pcre2_match_data_create_from_pattern(re, nullptr);
    PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
    std::string result;
    int count = 0;
    int offset = 0;
    while (offset <= (int)slen) {
        if (max_count > 0 && count >= max_count) break;
        int rc = pcre2_match(re, (PCRE2_SPTR8)subject + offset, slen - offset,
                             0, 0, md, nullptr);
        if (rc < 0) break;
        int mstart = (int)ov[0];
        int mend = (int)ov[1];
        result.append(subject + offset, mstart);
        if (has_repl_fn) {
            lua_pushvalue(L, 3);
            lua_pushlstring(L, subject + offset + mstart, mend - mstart);
            for (int i = 1; i < rc; ++i) {
                PCRE2_SIZE s = ov[2 * i];
                PCRE2_SIZE e = ov[2 * i + 1];
                if (s == PCRE2_UNSET || e == PCRE2_UNSET) lua_pushnil(L);
                else lua_pushlstring(L, subject + offset + s, e - s);
            }
            lua_call(L, rc, 1);
            size_t rlen = 0;
            const char *rstr = lua_tolstring(L, -1, &rlen);
            if (rstr) result.append(rstr, rlen);
            lua_pop(L, 1);
        } else {
            result += rex_expand_repl(repl_str, md, subject + offset, slen - offset);
        }
        offset += mend;
        ++count;
    }
    result.append(subject + offset);
    pcre2_match_data_free(md);
    lua_pushlstring(L, result.data(), result.size());
    lua_pushinteger(L, count);
    return 2;
}

// re:split(subject) -> {piece1, piece2, ...}
static int l_rex_re_split(lua_State *L) {
    pcre2_code_8 *re = *(pcre2_code_8 **)luaL_checkudata(L, 1, "rex_regex");
    size_t slen = 0;
    const char *subject = luaL_checklstring(L, 2, &slen);
    pcre2_match_data_8 *md = pcre2_match_data_create_from_pattern(re, nullptr);
    PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
    lua_newtable(L);
    int idx = 0;
    int offset = 0;
    while (offset <= (int)slen) {
        int rc = pcre2_match(re, (PCRE2_SPTR8)subject + offset, slen - offset,
                             0, 0, md, nullptr);
        if (rc < 0) {
            lua_pushlstring(L, subject + offset, slen - offset);
            lua_rawseti(L, -2, ++idx);
            break;
        }
        int mstart = (int)ov[0];
        int mend = (int)ov[1];
        lua_pushlstring(L, subject + offset, mstart);
        lua_rawseti(L, -2, ++idx);
        if (mstart == mend && mend < (int)slen) { ++offset; }
        else { offset += mend; }
    }
    pcre2_match_data_free(md);
    return 1;
}

LuaEngine::LuaEngine() {
    L = luaL_newstate();
    if (!L) return;

    // Load ALL standard libraries (industry-standard trust model: scripts
    // are user-authored local files, so io/package/os etc. are fully
    // available - same as Lite XL / Loom / SciTE).
    luaL_openlibs(L);

    // Seed math.random from the system time so scripts get different
    // sequences across runs. math.randomseed never fails with an integer
    // argument, so an unprotected lua_call is safe here.
    lua_getglobal(L, "math");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "randomseed");
        if (lua_isfunction(L, -1)) {
            lua_pushinteger(L, (lua_Integer)time(nullptr));
            lua_call(L, 1, 0);
        } else {
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    // Override print with our sink-backed version (captures into the
    // output pane instead of the console).
    lua_pushcfunction(L, l_print);
    lua_setglobal(L, "print");

    // Register `insert(text, offset)` global: insert text at cursor,
    // shift cursor by offset. Convenience for "no selection" branches.
    lua_pushcfunction(L, l_insert);
    lua_setglobal(L, "insert");

    // Register the `editor` table. Methods use colon-call syntax,
    // so the first upvalue-less entry takes `self` as arg 1.
    static const luaL_Reg editorMethods[] = {
        { "get_selection",      l_get_selection      },
        { "replace_selection",  l_replace_selection  },
        { "get_cursor",         l_get_cursor         },
        { "set_cursor",         l_set_cursor         },
        { nullptr, nullptr }
    };
    lua_newtable(L);
    luaL_setfuncs(L, editorMethods, 0);
    lua_setglobal(L, "editor");

    // Register the `win` table: user-interaction helpers exposed to
    // scripts (message box, clipboard, open file/URL).
    static const luaL_Reg winMethods[] = {
        { "message",        l_win_message      },
        { "question",       l_win_question     },
        { "clipboard_get",  l_win_clipboard_get },
        { "clipboard_set",  l_win_clipboard_set },
        { "open",           l_win_open         },
        { nullptr, nullptr }
    };
    lua_newtable(L);
    luaL_setfuncs(L, winMethods, 0);
    lua_setglobal(L, "win");

    // Register the `rex` table: Textus-compatible PCRE2 regex API.
    // Functions are outside the anonymous namespace so they have external
    // linkage; the Lua API matches Textus's rex.* exactly.
    static const luaL_Reg rexMethods[] = {
        { "version",  l_rex_version  },
        { "new",      l_rex_new      },
        { "find",     l_rex_find     },
        { "match",    l_rex_match    },
        { "gmatch",   l_rex_gmatch   },
        { "gsub",     l_rex_gsub     },
        { "split",    l_rex_split    },
        { nullptr, nullptr }
    };
    lua_newtable(L);
    luaL_setfuncs(L, rexMethods, 0);
    lua_setglobal(L, "rex");

    // GC handler for rex_regex userdata: free the compiled PCRE2 pattern.
    static const luaL_Reg reGc[] = {
        { "__gc", [](lua_State *L) -> int {
            pcre2_code_8 **ud = (pcre2_code_8 **)luaL_checkudata(L, 1, "rex_regex");
            if (*ud) { pcre2_code_free(*ud); *ud = nullptr; }
            return 0;
        }},
        { nullptr, nullptr }
    };

    // Set up the metatable for rex_regex userdata (compiled regex objects).
    luaL_newmetatable(L, "rex_regex");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, reGc, 0);
    static const luaL_Reg reMethods[] = {
        { "find",   l_rex_re_find   },
        { "match",  l_rex_re_match  },
        { "gmatch", l_rex_re_gmatch },
        { "gsub",   l_rex_re_gsub   },
        { "split",  l_rex_re_split  },
        { nullptr, nullptr }
    };
    luaL_setfuncs(L, reMethods, 0);
    lua_pop(L, 1);  // pop metatable
}

LuaEngine::~LuaEngine() {
    if (L) lua_close(L);
}

bool LuaEngine::run(const std::string &script,
                    LuaEditorHost &host,
                    std::string *output,
                    std::string *errMsg,
                    const std::vector<std::pair<std::string, std::string>> *paramValues,
                    const char *langCode) {
    if (!L) {
        if (errMsg) *errMsg = "Lua state not initialized";
        return false;
    }
    if (!host.buffer) {
        if (errMsg) *errMsg = "no document buffer";
        return false;
    }

    // Strip a UTF-8 BOM if present: luaL_loadstring rejects it as an
    // illegal character. Files saved via the Lua console's Save As carry
    // a BOM (so the main editor loads them correctly); the console's
    // in-memory buffer never has one, so this only affects file-backed
    // scripts loaded by the main window.
    std::string src = script;
    if (src.size() >= 3 &&
        (unsigned char)src[0] == 0xEF &&
        (unsigned char)src[1] == 0xBB &&
        (unsigned char)src[2] == 0xBF) {
        src.erase(0, 3);
    }

    // Reset any leftover globals from a previous run BEFORE injecting the
    // fresh ones: a new run must not see the previous script's
    // `params`/`pecia_lang` (previously this cleanup ran AFTER the
    // injection below and nulled out the values just set - making those
    // globals always nil and breaking --!param scripts).
    lua_pushnil(L); lua_setglobal(L, "params");
    lua_pushnil(L); lua_setglobal(L, "pecia_lang");
    lua_pushnil(L); lua_setglobal(L, "input");

    // Optionally inject a `params` global table: params.name = value.
    if (paramValues) {
        lua_newtable(L);
        for (const auto &kv : *paramValues) {
            lua_pushlstring(L, kv.first.data(), kv.first.size());
            lua_pushlstring(L, kv.second.data(), kv.second.size());
            lua_rawset(L, -3);
        }
        lua_setglobal(L, "params");
    }

    // `pecia_lang` global: current UI language code ("en", "zh-CN", ...)
    // so scripts can show localized messages themselves.
    if (langCode && *langCode) {
        lua_pushstring(L, langCode);
        lua_setglobal(L, "pecia_lang");
    }

    // Snapshot the document text.
    char *origText = host.buffer->text();
    int origLen = host.buffer->length();

    // Build a scratch buffer mirroring the document: same text, selection
    // and cursor. All script edits land here; the live buffer is untouched
    // until the script finishes.
    Fl_Text_Buffer scratch(origLen > 0 ? origLen : 1);
    if (origLen > 0) scratch.insert(0, origText);
    int scratchCursor = host.cursorPos;
    if (host.hasSelection)
        scratch.select(host.selStart, host.selEnd);
    LuaScratch ctx = { &scratch, &scratchCursor };

    // Inject `input` global: selected text from the scratch buffer.
    // If nothing is selected, `input` is "" (consistent with Textus).
    {
        int is = 0, ie = 0;
        if (scratch.selection_position(&is, &ie) && ie > is) {
            char *sel = scratch.text_range(is, ie);
            lua_pushlstring(L, sel ? sel : "", (size_t)(ie - is));
            if (sel) free(sel);
        } else {
            lua_pushliteral(L, "");
        }
        lua_setglobal(L, "input");
    }

    std::string sink;   // local sink so output is safe even if caller passes nullptr

    // Reset any leftover stack state from a previous run.
    lua_settop(L, 0);

    // Stash per-run state in the registry.
    lua_pushlightuserdata(L, &kScratchKey);
    lua_pushlightuserdata(L, &ctx);
    lua_rawset(L, LUA_REGISTRYINDEX);

    lua_pushlightuserdata(L, &kOutputKey);
    lua_pushlightuserdata(L, &sink);
    lua_rawset(L, LUA_REGISTRYINDEX);

    bool ok = true;
    if (luaL_loadstring(L, src.c_str()) != LUA_OK) {
        if (errMsg) {
            const char *s = lua_tostring(L, -1);
            *errMsg = s ? s : "load error";
        }
        lua_pop(L, 1);
        ok = false;
    } else {
        // Guard against runaway scripts: install the instruction-counting
        // timeout hook for the duration of this pcall, then remove it.
        g_instructionBudget.store(kMaxInstructions);
        lua_sethook(L, &s_instructionHook, LUA_MASKCOUNT, 2048);
        int pc = lua_pcall(L, 0, 1, 0);
        lua_sethook(L, nullptr, 0, 0);   // remove the hook regardless of outcome
        if (pc != LUA_OK) {
            if (errMsg) {
                const char *s = lua_tostring(L, -1);
                *errMsg = s ? s : "runtime error";
            }
            lua_pop(L, 1);
            ok = false;
        } else {
            // Support `return "text"` (Textus convention): if the script
            // returns a string, replace the selection — or the entire
            // document when nothing is selected (matching Textus behavior).
            int rtype = lua_type(L, -1);
            if (rtype == LUA_TSTRING) {
                size_t rlen = 0;
                const char *rstr = lua_tolstring(L, -1, &rlen);
                int is = 0, ie = 0;
                if (scratch.selection_position(&is, &ie)) {
                    scratch.replace(is, ie, rstr, (int)rlen);
                    scratch.select(is, is + (int)rlen);
                    scratchCursor = is + (int)rlen;
                } else {
                    // No selection: replace entire document (Textus convention)
                    scratch.replace(0, scratch.length(), rstr, (int)rlen);
                    scratchCursor = (int)rlen;
                }
            }
            lua_pop(L, 1);  // pop return value
        }
    }

    if (output) output->append(sink);

    // Clear the registry slots so a stray callback can't dereference a
    // dangling pointer if Lua is later used without going through run().
    lua_pushlightuserdata(L, &kScratchKey);
    lua_pushnil(L);
    lua_rawset(L, LUA_REGISTRYINDEX);
    lua_pushlightuserdata(L, &kOutputKey);
    lua_pushnil(L);
    lua_rawset(L, LUA_REGISTRYINDEX);

    // On failure the scratch state is meaningless and the live buffer must
    // not be touched: return immediately (output already appended above).
    if (!ok) {
        free(origText);
        return false;
    }

    // Read the script's final state from the scratch.
    char *finalText = scratch.text();
    int finalLen = scratch.length();
    int fSelStart = 0, fSelEnd = 0;
    bool fHasSel = scratch.selection_position(&fSelStart, &fSelEnd);
    int finalCursor = scratchCursor;
    if (finalCursor < 0) finalCursor = 0;
    if (finalCursor > finalLen) finalCursor = finalLen;

    // Apply the result to the real buffer as ONE undo transaction: a
    // single Ctrl+Z reverts the whole script. PATCHED by Pecia
    // (FLTK_PATCHES.md Patch 6): with undo transactions available we no
    // longer need the text() workaround (which cleared the entire
    // undo/redo stack and made the script result un-undoable); replace()
    // inside undo_begin/undo_end is safe now - apply_undo no longer
    // pollutes the undo history.
    bool changed = (finalLen != origLen) ||
                   (origLen > 0 && memcmp(origText, finalText, origLen) != 0);
    if (changed) {
        host.buffer->undo_begin();
        host.buffer->replace(0, origLen, finalText ? finalText : "");
        host.buffer->undo_end();
    }
    // The script's final selection/cursor always win (positions refer to
    // the final text; when nothing changed they simply mirror the input).
    host.buffer->select(fSelStart, fSelEnd);
    host.cursorPos = finalCursor;
    host.hasSelection = fHasSel;
    host.selStart = fSelStart;
    host.selEnd = fSelEnd;

    free(origText);
    free(finalText);
    return ok;
}
