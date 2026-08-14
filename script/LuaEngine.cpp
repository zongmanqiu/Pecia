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

#include <windows.h>
#include <shellapi.h>

namespace {

// --------------------------------------------------------------------------
// pecia_regex.dll - on-demand UTF-8 regex engine (PCRE2 wrapper).
// The DLL is NOT linked into the exe: it is loaded via LoadLibrary on the
// first regex.* call and unloaded when the script finishes, so normal
// operation costs zero memory. Function pointers are resolved once per
// load; the l_regex_* Lua functions stay registered (they lazily load).
// --------------------------------------------------------------------------
using FnFindAll = int (*)(const char *, int, const char *, int *, int *, int,
                          char *, int);
using FnSubst   = int (*)(const char *, int, const char *, const char *,
                          char **, int *, char *, int);
using FnFree    = void (*)(void *);

HMODULE g_regexDll = nullptr;
FnFindAll g_fnFindAll = nullptr;
FnSubst   g_fnSubst = nullptr;
FnFree    g_fnFree = nullptr;

bool loadRegexDll() {
    if (g_regexDll) return true;
    // LoadLibrary searches the exe directory first, so pecia_regex.dll
    // sits next to PeciaLua.exe / Pecia.exe and needs no path.
    g_regexDll = LoadLibraryA("pecia_regex.dll");
    if (!g_regexDll) return false;
    g_fnFindAll = (FnFindAll)GetProcAddress(g_regexDll, "pr_findall");
    g_fnSubst   = (FnSubst)GetProcAddress(g_regexDll, "pr_substitute_all");
    g_fnFree    = (FnFree)GetProcAddress(g_regexDll, "pr_free");
    if (!g_fnFindAll || !g_fnSubst || !g_fnFree) {
        FreeLibrary(g_regexDll);
        g_regexDll = nullptr;
        g_fnFindAll = nullptr;
        g_fnSubst = nullptr;
        g_fnFree = nullptr;
        return false;
    }
    return true;
}

void unloadRegexDll() {
    if (g_regexDll) {
        FreeLibrary(g_regexDll);
        g_regexDll = nullptr;
        g_fnFindAll = nullptr;
        g_fnSubst = nullptr;
        g_fnFree = nullptr;
    }
}

// regex.gsub(text, pattern, replacement) -> replaced text | nil, errmsg
// Replaces every non-overlapping match; $1 / ${name} captures work.
int l_regex_gsub(lua_State *L) {
    size_t tl = 0, pl = 0, rl = 0;
    const char *text = luaL_checklstring(L, 1, &tl);
    const char *pat  = luaL_checklstring(L, 2, &pl);
    const char *repl = luaL_checklstring(L, 3, &rl);
    if (!loadRegexDll()) {
        lua_pushnil(L);
        lua_pushliteral(L,
            "regex engine unavailable (pecia_regex.dll missing)");
        return 2;
    }
    char err[128] = "";
    char *out = nullptr;
    int outLen = 0;
    int rc = g_fnSubst(text, (int)tl, pat, repl, &out, &outLen,
                       err, sizeof(err));
    if (rc < 0) {
        lua_pushnil(L);
        lua_pushstring(L, err[0] ? err : "regex error");
        return 2;
    }
    if (rc == 0 || outLen <= 0) {
        lua_pushlstring(L, text, tl);   // no match - unchanged
        return 1;
    }
    lua_pushlstring(L, out, (size_t)outLen);
    if (g_fnFree) g_fnFree(out);
    return 1;
}

// regex.findall(text, pattern) -> { {start,end}, ... } | nil, errmsg
// Byte offsets into the UTF-8 text; empty strings from zero-width
// matches have start == end.
int l_regex_findall(lua_State *L) {
    size_t tl = 0, pl = 0;
    const char *text = luaL_checklstring(L, 1, &tl);
    const char *pat  = luaL_checklstring(L, 2, &pl);
    if (!loadRegexDll()) {
        lua_pushnil(L);
        lua_pushliteral(L,
            "regex engine unavailable (pecia_regex.dll missing)");
        return 2;
    }
    char err[128] = "";
    int n = g_fnFindAll(text, (int)tl, pat, nullptr, nullptr, 0,
                        err, sizeof(err));
    if (n < 0) {
        lua_pushnil(L);
        lua_pushstring(L, err[0] ? err : "regex error");
        return 2;
    }
    std::vector<int> starts((size_t)n), ends((size_t)n);
    if (n > 0) {
        g_fnFindAll(text, (int)tl, pat, starts.data(), ends.data(), n,
                    err, sizeof(err));
    }
    lua_createtable(L, n, 0);
    for (int i = 0; i < n; ++i) {
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, starts[(size_t)i]);
        lua_rawseti(L, -2, 1);
        lua_pushinteger(L, ends[(size_t)i]);
        lua_rawseti(L, -2, 2);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

// RAII: unload the regex DLL when the script run ends (every exit path).
struct RegexUnloadGuard {
    ~RegexUnloadGuard() { unloadRegexDll(); }
};


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
        ShellExecuteA(nullptr, "open", target, nullptr, nullptr, SW_SHOWNORMAL);
    }
    return 0;
}

} // namespace

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

    // Register the `regex` table: UTF-8 regex engine (pecia_regex.dll,
    // loaded on first use, unloaded when the run ends).
    static const luaL_Reg regexMethods[] = {
        { "gsub",      l_regex_gsub     },
        { "findall",   l_regex_findall  },
        { nullptr, nullptr }
    };
    lua_newtable(L);
    luaL_setfuncs(L, regexMethods, 0);
    lua_setglobal(L, "regex");
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

    std::string sink;   // local sink so output is safe even if caller passes nullptr

    // Stash per-run state in the registry.
    lua_pushlightuserdata(L, &kScratchKey);
    lua_pushlightuserdata(L, &ctx);
    lua_rawset(L, LUA_REGISTRYINDEX);

    lua_pushlightuserdata(L, &kOutputKey);
    lua_pushlightuserdata(L, &sink);
    lua_rawset(L, LUA_REGISTRYINDEX);

    // Unload the regex DLL (if any script used it) when the run ends.
    RegexUnloadGuard regexGuard;

    bool ok = true;
    if (luaL_loadstring(L, src.c_str()) != LUA_OK) {
        if (errMsg) {
            const char *s = lua_tostring(L, -1);
            *errMsg = s ? s : "load error";
        }
        lua_pop(L, 1);
        ok = false;
    } else if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        if (errMsg) {
            const char *s = lua_tostring(L, -1);
            *errMsg = s ? s : "runtime error";
        }
        lua_pop(L, 1);
        ok = false;
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
