// LuaEngine.cpp - embedded Lua 5.5 sandbox for user text-processing scripts
#include "LuaEngine.h"
#include "core/OpLog.h"

#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/fl_ask.H>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include <cstring>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <vector>
#include <atomic>
#include <string>

#include <windows.h>
#include <shellapi.h>

extern "C" {
#define PCRE2_CODE_UNIT_WIDTH 8
#include "pcre2.h"
}
#include "core/HostOps.h"   // C++ header - must stay OUTSIDE extern "C"

// Script UI hooks (m025): declared here so the win: bindings inside the
// anonymous namespace below can see them; defined after the namespace.
extern LuaUiHooks g_uiHooks;

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
// a Lua error once a run burns through the budget, so the editor and the
// MCP pipe server are never frozen by a runaway script.
// Scripts run serially on the FLTK main thread (via Fl::awake), so a plain
// counter is race-free here.
//
// ONE derivation for EVERY call path (script bar / console / MCP ops): the
// wall-clock deadline IS the requested timeout (clamped), and the instruction
// budget is derived from that deadline with a single constant - so the agent
// path can never be more permissive than the UI path.
// --------------------------------------------------------------------------
const long long kDefaultRunWallMs = 5000;     // engine default (nothing asked for)
const long long kMaxRunWallMs = 5000;         // hard cap for ANY requested timeout
const long long kMaxInstructions = 5'000'000; // default budget (= 5000 ms * 1000)
const long long kInstructionsPerMs = kMaxInstructions / kDefaultRunWallMs;  // 1000
std::atomic<long long> g_instructionBudget{0};

// Wall-clock backstop (m026 audit item M1). The instruction budget above only
// counts VM instructions: time spent INSIDE a C function (PCRE2 backtracking,
// huge string ops) is invisible to the hook, so a budget computed from
// timeout_ms can stretch far past the promised milliseconds. The deadline is
// checked by the same hook - it fires once the script executes another batch
// of VM instructions (the hook's count) after a long C call returns - and caps
// any run at the requested timeout (hard cap kMaxRunWallMs) regardless of how
// the instruction budget was diluted.
//
// The hook cannot be removed from the script side: the `debug` library is
// taken away in the constructor (disableDebugLibrary), so debug.sethook() can
// no longer disarm the budget AND the deadline at the same time.
// Known boundary, stated honestly:
// Lua has no preemptive cancellation, so one single non-regex C call that
// itself never returns cannot be interrupted - regex (the realistic risk) is
// separately bounded by the per-pattern match limit in rex_compile().
std::chrono::steady_clock::time_point g_runDeadline{};

void s_instructionHook(lua_State *L, lua_Debug *) {
    // LUA_MASKCOUNT invokes the hook every N instructions; the count we pass
    // to lua_sethook is the decrement per call. Exceeding the budget raises
    // a Lua error (via longjmp), which unwinds pcall and reports a timeout
    // to the user instead of hanging the GUI thread forever.
    // Lua 5.5 copies the hook to every thread the script creates
    // (lua_newthread), so a runaway loop inside a coroutine is caught too.
    long long remaining = g_instructionBudget.fetch_sub(2048) - 2048;
    if (remaining <= 0) {
        g_instructionBudget.store(0);
        luaL_error(L, "script timeout: execution exceeded the instruction limit");
    }
    if (std::chrono::steady_clock::now() >= g_runDeadline) {
        g_instructionBudget.store(0);
        luaL_error(L, "script timeout: execution exceeded the wall-clock limit");
    }
}

// RunBudget - the two numbers that bound ONE run.
struct RunBudget {
    long long wallMs = kDefaultRunWallMs;      // wall-clock deadline
    long long instructions = kMaxInstructions;  // VM instruction budget
};

// Derive the budget of one run from the caller's request.
//
//   * the wall-clock deadline honours the requested timeout
//     (opts->timeoutMs), clamped to kMaxRunWallMs - the UI thread must not
//     be stalled longer than that, whatever the caller asked for;
//   * the instruction budget is derived from that SAME deadline
//     (kInstructionsPerMs), so every call path gets the same treatment.
//
// opts->maxInstructions may only TIGHTEN the derived budget, never raise it:
// the MCP path used to ask for 60M instructions (12x the UI path's 5M) for
// the very same 3 seconds, which is exactly the gap this closes.
RunBudget deriveRunBudget(const LuaRunOptions *opts) {
    RunBudget b;
    long long wallMs = kDefaultRunWallMs;
    if (opts && opts->timeoutMs > 0) wallMs = opts->timeoutMs;
    if (wallMs > kMaxRunWallMs) wallMs = kMaxRunWallMs;
    if (wallMs < 1) wallMs = 1;
    b.wallMs = wallMs;
    b.instructions = wallMs * kInstructionsPerMs;
    if (opts && opts->maxInstructions > 0 && opts->maxInstructions < b.instructions)
        b.instructions = opts->maxInstructions;   // tighter cap only
    return b;
}

// Take the `debug` library away from the script.
//
// The runaway protection (instruction budget + wall-clock deadline) lives in
// a debug hook, and `debug.sethook()` is the one switch that removes it: a
// script could call it and then run `while true do end` for ever, freezing
// the GUI thread (the hook carried BOTH protections, so removing it removed
// the deadline too). The library is the only way to touch the hook from Lua,
// so we drop it.
//
// The global is cleared AND package.loaded["debug"] is cleared - otherwise
// `require("debug")` would hand the table back. Everything else stays open
// (os / io / package ...): the trust model is unchanged, we only take away
// the switch that disarmed the guard.
static void disableDebugLibrary(lua_State *L) {
    lua_pushnil(L);
    lua_setglobal(L, "debug");
    lua_getglobal(L, "package");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "loaded");
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            lua_setfield(L, -2, "debug");
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

// win table callbacks (defined below, referenced from the constructor).
int l_win_message(lua_State *L);
int l_win_question(lua_State *L);
int l_win_input(lua_State *L);
int l_win_select(lua_State *L);
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
    // m025: execution knobs + sink caps, live for the duration of one run().
    // Sink cap accounting lives here (not in the plain std::string) because
    // every writer to the print sink flows through appendOutput().
    LuaRunOptions    opts;   // headless/answer/caps for this run
    int              outCap = 32768;
    int              resCap = 32768;
    bool             outTruncated = false;
};

// Nested-run guard (m025): scripts run serially on the UI thread, but a
// pecia.* trampoline dispatching into another run() would overwrite the
// per-run registry slots mid-flight. run() bumps the counter on entry and
// lowers it on EVERY exit path (explicit, no RAII - longjmp never crosses
// run()'s frames, but explicit decrement survives any future unwind
// subtlety and is trivially auditable).
// (Declared here; appendOutput below and LuaScratch use it.)
std::atomic<int> g_runDepth{0};

// Cut a byte length down to a UTF-8 code-point boundary.
// A cap that lands in the middle of a multi-byte sequence would push
// invalid UTF-8 into the document / the MCP answer (a half Chinese
// character), so the cut is moved back to the start of that code point.
// `data` is the buffer being cut, `len` its length, `cut` the wanted size.
static size_t utf8SafeCut(const char *data, size_t len, size_t cut) {
    if (cut >= len) return len;
    if (cut == 0) return 0;
    unsigned char last = (unsigned char)data[cut - 1];
    if (last < 0x80) return cut;                   // ASCII: already a boundary
    if ((last & 0xC0) == 0x80) {
        // Last kept byte is a continuation byte: its code point started
        // earlier - check that the whole sequence fits in [0, cut).
        size_t i = cut - 1;
        while (i > 0 && ((unsigned char)data[i] & 0xC0) == 0x80) --i;
        unsigned char lead = (unsigned char)data[i];
        int seq = (lead >= 0xF0) ? 4 : (lead >= 0xE0) ? 3 : (lead >= 0xC0) ? 2 : 1;
        if (i + (size_t)seq > cut) return i;       // cut inside the code point
        return cut;
    }
    // Last kept byte is a lead byte: the code point it starts would be cut
    // in half - drop that byte (the rest of the sequence is not in [0, cut)).
    return cut - 1;
}

// One capped append into the print sink.
static void appendOutput(std::string *sink, LuaScratch *ctx,
                         const char *data, size_t n) {
    if (!sink || !data || !n) return;
    if (ctx) {
        int room = ctx->outCap - (int)sink->size();
        if (room <= 0) { ctx->outTruncated = true; return; }
        if ((int)n > room) {
            // Cut on a code-point boundary: a half character would be
            // invalid UTF-8 in the output.
            size_t take = utf8SafeCut(data, n, (size_t)room);
            sink->append(data, take);
            ctx->outTruncated = true;
            return;
        }
    }
    sink->append(data, n);
}

// Shared by run() and validate(): luaL_loadstring rejects a leading BOM.
std::string stripBom(const std::string &script) {
    if (script.size() >= 3 &&
        (unsigned char)script[0] == 0xEF &&
        (unsigned char)script[1] == 0xBB &&
        (unsigned char)script[2] == 0xBF)
        return script.substr(3);
    return script;
}

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

// ---------------------------------------------------------------------------
// pecia.* - the HostOps registry projected onto Lua (m020). One trampoline
// for every registered op; the registry is the single source of truth (no
// per-op hand-written bindings here, and the same table feeds MCP in m021).
// ---------------------------------------------------------------------------

// true when t[k] exists (nil-valued slots end the count)
static bool rawgeti_probe(lua_State *L, int idx, lua_Integer k)
{
    lua_rawgeti(L, idx, k);
    bool exists = !lua_isnil(L, -1);
    lua_pop(L, 1);
    return exists;
}

// Lua value -> OpValue. Tables: array part -> List, hash part -> Map.
// Anything else (function/userdata/thread) is rejected as a conversion error.
static bool luaToOpValue(lua_State *L, int idx, OpValue &out)
{
    // idx may be -1 (a value pushed by our own recursion). Any push below
    // (lua_pushnil / lua_geti) would then shift that relative index, so lock
    // it to an absolute slot once at the entry - lua_next must walk the table.
    idx = lua_absindex(L, idx);
    switch (lua_type(L, idx)) {
    case LUA_TNIL:      out = OpValue(); return true;
    case LUA_TBOOLEAN:  out = OpValue::makeBool(lua_toboolean(L, idx) != 0); return true;
    case LUA_TNUMBER:   out = OpValue::makeInt((long long)lua_tointeger(L, idx)); return true;
    case LUA_TSTRING: {
        size_t n = 0;
        const char *t = lua_tolstring(L, idx, &n);
        out = OpValue::makeStr(std::string(t, n));
        return true;
    }
    case LUA_TTABLE: {
        // classify: contiguous [1..n] array vs named map.
        // NOT luaL_len: it raises "table index is nil" on EMPTY tables
        // (and does not error on mixed ones), so count the array part by
        // raw probes instead - deterministic and error-free.
        lua_Integer rawn = 0;
        while (rawgeti_probe(L, idx, rawn + 1)) ++rawn;
        bool isArray = rawn > 0;
        if (isArray) {
            // guard { [1]=a, key=b } shape: array part alone is not enough
            bool hasHashKey = false;
            lua_pushnil(L);
            while (lua_next(L, idx) != 0) {
                if (!lua_isinteger(L, -2) || lua_tointeger(L, -2) < 1
                    || lua_tointeger(L, -2) > rawn) { hasHashKey = true; lua_pop(L, 2); break; }
                lua_pop(L, 1);
            }
            if (hasHashKey) isArray = false;
        }

        if (isArray) {
            out = OpValue::makeList();
            for (lua_Integer i = 1; i <= rawn; ++i) {
                lua_geti(L, idx, (lua_Integer)i);
                OpValue item;
                bool okc = luaToOpValue(L, -1, item);
                lua_pop(L, 1);
                if (!okc) return false;
                out.push(std::move(item));
            }
            return true;
        }
        out = OpValue::makeMap();
        lua_pushnil(L);
        while (lua_next(L, idx) != 0) {
            if (lua_type(L, -2) != LUA_TSTRING) { lua_pop(L, 2); return false; }
            OpValue item;
            bool okc = luaToOpValue(L, -1, item);
            if (!okc) { lua_pop(L, 2); return false; }
            out.set(lua_tostring(L, -2), std::move(item));
            lua_pop(L, 1);
        }
        return true;
    }
    default:
        return false;
    }
}

static void opValueToLua(lua_State *L, const OpValue &v)
{
    switch (v.kind()) {
    case OpValue::Kind::Null:  lua_pushnil(L); break;
    case OpValue::Kind::Bool:  lua_pushboolean(L, v.getBool() ? 1 : 0); break;
    case OpValue::Kind::Int:   lua_pushinteger(L, (lua_Integer)v.getInt()); break;
    case OpValue::Kind::Str:   lua_pushlstring(L, v.getString().data(), v.getString().size()); break;
    case OpValue::Kind::List: {
        lua_createtable(L, (int)v.size(), 0);
        int i = 1;
        for (const auto &e : v.list()) { opValueToLua(L, e); lua_seti(L, -2, i++); }
        break;
    }
    case OpValue::Kind::Map: {
        lua_createtable(L, 0, (int)v.size());
        for (const auto &kv : v.map()) {
            opValueToLua(L, kv.second);
            lua_setfield(L, -2, kv.first.c_str());
        }
        break;
    }
    }
}

// generic op call: arg1 upvalue = dotted op name; call args start at 1.
// returns: value on success | nil, "err: detail" on failure.
static int l_pecia_op(lua_State *L)
{
    const char *name = lua_tostring(L, lua_upvalueindex(1));
    std::map<std::string, OpValue> args;
    int nargs = lua_gettop(L);
    if (nargs >= 1 && lua_istable(L, 1)) {
        // table-call form: pecia.doc_get_text({ start = 0 })
        OpValue m;
        if (!luaToOpValue(L, 1, m) || m.kind() != OpValue::Kind::Map)
            return luaL_error(L, "pecia: argument table has non-string keys");
        for (const auto &kv : m.map()) args[kv.first] = kv.second;
        if (nargs > 1) return luaL_error(L, "pecia: one table argument expected");
    } else {
        // positional form, args in declared registry order
        const OpDef *def = HostRegistry::instance().find(name);
        if (!def) return luaL_error(L, "pecia: op '%s' is not registered", name);
        for (size_t i = 0; i < def->args.size(); ++i) {
            int idx = (int)i + 1;
            if (idx > nargs) break;
            OpValue v;
            if (!luaToOpValue(L, idx, v))
                return luaL_error(L, "pecia: argument %d has unsupported type", (int)idx);
            args[def->args[i].name] = std::move(v);
        }
    }
    OpOutcome o = HostRegistry::instance().dispatch(name, args);
    if (!o.ok) {
        lua_pushnil(L);
        lua_pushstring(L, o.err.c_str());
        return 2;
    }
    opValueToLua(L, o.value);
    return 1;
}

// pecia.ops() -> list of {name, perm, args:[{name,type,desc}]} (registry,
// deterministic order) - the doc generator and scripts introspect the same
// table the dispatcher uses.
static int l_pecia_ops(lua_State *L)
{
    auto snap = HostRegistry::instance().snapshot();
    lua_createtable(L, (int)snap.size(), 0);
    int i = 1;
    for (const OpDef *def : snap) {
        lua_createtable(L, 0, 3);
        lua_pushstring(L, def->name.c_str());
        lua_setfield(L, -2, "name");
        lua_pushstring(L, def->perm == OpPerm::Read ? "read"
                       : def->perm == OpPerm::Write ? "write" : "exec");
        lua_setfield(L, -2, "perm");
        lua_createtable(L, (int)def->args.size(), 0);
        for (size_t k = 0; k < def->args.size(); ++k) {
            lua_createtable(L, 0, 3);
            lua_pushstring(L, def->args[k].name.c_str());  lua_setfield(L, -2, "name");
            lua_pushstring(L, def->args[k].type);          lua_setfield(L, -2, "type");
            lua_pushstring(L, def->args[k].description);   lua_setfield(L, -2, "desc");
            lua_seti(L, -2, (int)k + 1);
        }
        lua_setfield(L, -2, "args");
        lua_seti(L, -2, i++);
    }
    return 1;
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
    if (out && str && len) appendOutput(out, getScratch(L), str, len);
    lua_pop(L, 1);
    return 0;
}

// lua_api.txt documents the win functions with colon calls (win:message),
// while the registrations read arg1 as the string (dot-call shape). Accept
// both: when arg1 is the receiver table (colon call), every subsequent
// argument index shifts by one.
static int win_self_shift(lua_State *L) {
    return (lua_istable(L, 1) && !lua_isstring(L, 1)) ? 1 : 0;
}

// win:message(text) - modal OK message box.
// headless (m025): the text is captured into the run's output sink instead
// of being shown - an MCP-triggered script must never block the UI thread.
int l_win_message(lua_State *L) {
    int a = 1 + win_self_shift(L);
    const char *text = luaL_optstring(L, a, "");
    LuaScratch *s = getScratch(L);
    if (s && s->opts.headless) {
        std::string *sink = getOutput(L);
        std::string line = std::string("[win:message] ") + (text ? text : "") + "\n";
        appendOutput(sink, s, line.data(), line.size());
        return 0;
    }
    // W, not A: script text is UTF-8, and MessageBoxA would render Chinese as
    // mojibake on any code page that is not GBK (same rule as the rest of the
    // project's native boxes, e.g. MainWindow_preview.cpp).
    MessageBoxW(nullptr, widen(text ? text : "").c_str(), L"Pecia",
                MB_OK | MB_ICONINFORMATION | MB_TASKMODAL);
    return 0;
}

// win:question(text) -> bool - modal Yes/No box.
// headless (m025): returns the caller-preset answer without showing UI.
int l_win_question(lua_State *L) {
    int a = 1 + win_self_shift(L);
    const char *text = luaL_optstring(L, a, "");
    LuaScratch *s = getScratch(L);
    if (s && s->opts.headless) {
        std::string *sink = getOutput(L);
        std::string line = std::string("[win:question] ") + (text ? text : "")
                         + (s->opts.answer ? " -> yes\n" : " -> no\n");
        appendOutput(sink, s, line.data(), line.size());
        lua_pushboolean(L, s->opts.answer ? 1 : 0);
        return 1;
    }
    int r = MessageBoxW(nullptr, widen(text ? text : "").c_str(), L"Pecia",
                        MB_YESNO | MB_ICONQUESTION | MB_TASKMODAL);
    lua_pushboolean(L, r == IDYES);
    return 1;
}

// win:input(prompt, default) -> string|nil   (m025)
// Headless: returns nil (scripts fall back to their default branch).
int l_win_input(lua_State *L) {
    int a = 1 + win_self_shift(L);
    const char *prompt = luaL_optstring(L, a, "Input:");
    const char *dflt = luaL_optstring(L, a + 1, "");
    LuaScratch *s = getScratch(L);
    if (s && s->opts.headless) {
        std::string *sink = getOutput(L);
        std::string line = std::string("[win:input] ") + prompt + " -> nil (headless)\n";
        appendOutput(sink, s, line.data(), line.size());
        lua_pushnil(L);
        return 1;
    }
    if (g_uiHooks.input) {
        char buf[4096];
        buf[0] = 0;
        if (g_uiHooks.input(g_uiHooks.ud, prompt, dflt, buf, (int)sizeof(buf)))
            lua_pushstring(L, buf);
        else
            lua_pushnil(L);
        return 1;
    }
    // No hooks installed (unit tests / tools without a main window).
    const char *r = fl_input(prompt, dflt);
    if (r) lua_pushstring(L, r); else lua_pushnil(L);
    return 1;
}

// win:select(prompt, {opt1, opt2, ...}, pre) -> chosen string|nil   (m025)
// `pre` (1-based) is the initially highlighted row. With UI hooks installed
// this is a themed, registry-visible PickDialog; otherwise a plain popup.
int l_win_select(lua_State *L) {
    int a = 1 + win_self_shift(L);
    const char *prompt = luaL_optstring(L, a, "Select:");
    luaL_checktype(L, a + 1, LUA_TTABLE);
    int pre = (int)luaL_optinteger(L, a + 2, 1);
    LuaScratch *s = getScratch(L);
    if (s && s->opts.headless) {
        std::string *sink = getOutput(L);
        std::string line = std::string("[win:select] ") + prompt + " -> nil (headless)\n";
        appendOutput(sink, s, line.data(), line.size());
        lua_pushnil(L);
        return 1;
    }
    std::vector<std::string> opts;
    opts.reserve(64);
    {
        // iterate with rawgeti so non-string rows are skipped, not fatal
        for (lua_Integer i = 1; i <= 512; ++i) {
            lua_geti(L, a + 1, i);
            if (!lua_isstring(L, -1)) { lua_pop(L, 1); break; }
            opts.emplace_back(lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    if (opts.empty()) { lua_pushnil(L); return 1; }
    if (pre < 1) pre = 1;
    if (pre > (int)opts.size()) pre = 1;

    if (g_uiHooks.select) {
        std::vector<const char *> rows(opts.size());
        for (size_t i = 0; i < opts.size(); ++i) rows[i] = opts[i].c_str();
        const int idx = g_uiHooks.select(g_uiHooks.ud, prompt, rows.data(),
                                        (int)rows.size(), pre);
        if (idx >= 0 && idx < (int)opts.size()) lua_pushstring(L, opts[idx].c_str());
        else lua_pushnil(L);
        return 1;
    }

    std::vector<Fl_Menu_Item> items(opts.size() + 1);
    for (size_t i = 0; i < opts.size(); ++i) {
        items[i].text = opts[i].c_str();   // lives as long as the popup call
        items[i].shortcut_ = 0;            // 1.4: trailing underscore fields
        items[i].flags = 0;
    }
    items[opts.size()].text = nullptr;     // terminator (rest is zero-init)
    const Fl_Menu_Item *pick = items[0].popup(
        (Fl::w() - 200) / 2, (Fl::h()) / 3, prompt);
    if (!pick || pick == items.data() + opts.size()) { lua_pushnil(L); return 1; }
    const size_t idx = (size_t)(pick - items.data());
    lua_pushstring(L, opts[idx].c_str());
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
    int a = 1 + win_self_shift(L);
    size_t len = 0;
    const char *text = luaL_checklstring(L, a, &len);
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
    int a = 1 + win_self_shift(L);
    const char *target = luaL_checkstring(L, a);
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

// Process-wide script UI hooks (installed by the ui layer, m025). Lives
// OUTSIDE the anonymous namespace: luaSetUiHooks is the exported seam, while
// g_uiHooks stays file-local for the win: bindings above.
LuaUiHooks g_uiHooks;
void luaSetUiHooks(const LuaUiHooks *hooks)
{
    g_uiHooks = hooks ? *hooks : LuaUiHooks{};
}

void LuaEngine::registerHostOpsLua()
{
    if (!L) return;
    // refresh on every engine (re)creation: ops are registered by MainWindow
    // before scripts run, and window teardown removes them again.
    lua_newtable(L);   // fresh table: no stale keys after a window teardown
    const auto snap = HostRegistry::instance().snapshot();
    for (const OpDef *def : snap) {
        // dotted name -> underscore key ("doc.get_text" -> "doc_get_text")
        std::string fn;
        fn.reserve(def->name.size());
        for (char c : def->name) fn.push_back(c == '.' ? '_' : c);
        lua_pushstring(L, def->name.c_str());
        lua_pushcclosure(L, l_pecia_op, 1);
        lua_setfield(L, -2, fn.c_str());
    }
    lua_pushcfunction(L, l_pecia_ops);
    lua_setfield(L, -2, "ops");
    lua_setglobal(L, "pecia");
}

// ==========================================================================
// rex table — Textus-compatible PCRE2 regex API (directly linked, no DLL)
// ==========================================================================

// Compile a PCRE2 pattern with UTF-8 + UCP. Returns nullptr on error
// (error message pushed to Lua stack).
//
// Match limit (m026 audit item M1): PCRE2's default is 10 million backtracking
// steps per match, i.e. one pathological pattern can hold the GUI thread for
// seconds - and the instruction-count hook cannot fire inside this C call.
// pcre2_set_match_limit lives on a MATCH CONTEXT (not the code), so every
// pcre2_match below goes through rexMatchCtx(): one process-wide context with
// the limit at 1M steps, roughly 100-200 ms per match on the dev machine,
// far above what any legitimate text-processing pattern needs. Exceeding it
// surfaces as a PCRE2 "match limit exceeded" runtime error.
static pcre2_match_context_8 *rexMatchCtx() {
    static pcre2_match_context_8 *ctx = []() {
        pcre2_match_context_8 *c = pcre2_match_context_create(nullptr);
        if (c) (void)pcre2_set_match_limit(c, 1'000'000);
        return c;   // nullptr (alloc failure) is legal: PCRE2 falls back to defaults
    }();
    return ctx;
}

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
                         0, 0, md, rexMatchCtx());
    if (rc < 0) {
        pcre2_match_data_free(md);
        lua_pushnil(L);
        lua_pushnil(L);
        return 2;
    }
    // from (1-indexed), to (1-indexed, EXCLUSIVE end: sub(from, to-1) is the
    // match, matching the lua_api.txt examples). The value was previously
    // ov[1] un-+1'd, i.e. inclusive, contradicting both the comment and the docs.
    lua_pushinteger(L, start + (lua_Integer)ov[0] + 1);
    lua_pushinteger(L, start + (lua_Integer)ov[1] + 1);
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

// ---------------------------------------------------------------------------
// Shared bodies for the two call shapes: rex.gsub(...) and re:gsub(...).
// The pattern is the only difference between them, so the loops live here
// once - a fix can then never land in one shape only (the zero-width-match
// bugs lived in two near-identical copies of the same loop).
// ---------------------------------------------------------------------------

// rex.gmatch / re:gmatch - every match as {match, cap1, ...}.
static int rex_gmatch_body(lua_State *L, pcre2_code_8 *re,
                           const char *subject, size_t slen) {
    pcre2_match_data_8 *md = pcre2_match_data_create_from_pattern(re, nullptr);
    PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
    lua_newtable(L);
    int idx = 0;
    int offset = 0;
    while (offset <= (int)slen) {
        int rc = pcre2_match(re, (PCRE2_SPTR8)subject + offset, slen - offset,
                             0, 0, md, rexMatchCtx());
        if (rc < 0) break;
        int mstart = (int)ov[0];
        int mend = (int)ov[1];
        if (mstart == mend) {
            // Zero-width match: not reported, but the scan MUST move on.
            // Otherwise the same position matches again and the loop never
            // ends - on an empty subject it spun for ever (the guard used to
            // read `mend < slen`, which is false for an empty subject).
            int mpos = offset + mstart;
            if (mpos >= (int)slen) break;      // zero-width match at the end
            offset = mpos + 1;
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
    return 1;
}

// rex.gsub / re:gsub - global replace.
// replIdx: stack index of a Lua replacement function (0 = string template).
// replStr: the string template ($0/$&/$1-$99/$$), used when replIdx == 0.
static int rex_gsub_body(lua_State *L, pcre2_code_8 *re,
                         const char *subject, size_t slen,
                         int replIdx, const char *replStr, int maxCount) {
    pcre2_match_data_8 *md = pcre2_match_data_create_from_pattern(re, nullptr);
    PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
    std::string result;
    int count = 0;
    int offset = 0;
    while (offset <= (int)slen) {
        if (maxCount > 0 && count >= maxCount) break;
        int rc = pcre2_match(re, (PCRE2_SPTR8)subject + offset, slen - offset,
                             0, 0, md, rexMatchCtx());
        if (rc < 0) break;
        int mstart = (int)ov[0];
        int mend = (int)ov[1];
        result.append(subject + offset, mstart);
        if (replIdx) {
            lua_pushvalue(L, replIdx);
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
            if (!lua_toboolean(L, -1)) {
                // nil / false: keep the original match, exactly as Lua's
                // string.gsub does and as lua_api.txt documents ("不替换").
                result.append(subject + offset + mstart, mend - mstart);
            } else if (lua_isstring(L, -1)) {
                size_t rlen = 0;
                const char *rstr = lua_tolstring(L, -1, &rlen);
                result.append(rstr, rlen);
            }
            // Any other truthy value (e.g. true) is not convertible: append
            // nothing, so the match is removed. Deterministic, never raises
            // a conversion error from deep inside this C++ helper.
            lua_pop(L, 1);
        } else {
            result += rex_expand_repl(replStr, md, subject + offset, slen - offset);
        }
        ++count;
        if (mstart == mend) {
            // Zero-width match ('x*', '(?=...)', ...): the match consumed
            // nothing, so the byte AT the match position belongs to the
            // result and the scan has to move past it - the same position
            // would otherwise match again and the loop would never end.
            // The byte under the empty match is emitted AFTER the
            // replacement, exactly like Lua's string.gsub does.
            int mpos = offset + mstart;
            if (mpos < (int)slen) {
                result.append(subject + mpos, 1);
                offset = mpos + 1;
            } else {
                // Empty match at the very end of the subject: nothing is
                // left to copy and nothing is left to scan.
                offset = (int)slen;
                break;
            }
        } else {
            offset += mend;
        }
    }
    // Tail: the bytes after the last match. The length is known, so this
    // never runs a strlen() on subject+offset (offset may sit at the very
    // end of the subject, and reading past it pulled heap bytes - the
    // chunk header of the next allocation - into the result).
    if (offset < (int)slen) result.append(subject + offset, slen - offset);
    pcre2_match_data_free(md);
    lua_pushlstring(L, result.data(), result.size());
    lua_pushinteger(L, count);
    return 2;
}

// rex.split / re:split - split the subject on every match.
static int rex_split_body(lua_State *L, pcre2_code_8 *re,
                           const char *subject, size_t slen) {
    pcre2_match_data_8 *md = pcre2_match_data_create_from_pattern(re, nullptr);
    PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
    lua_newtable(L);
    int idx = 0;
    if (slen == 0) {
        // Empty subject: nothing to split. (A zero-width pattern would also
        // match for ever here - the old guard `mend < slen` is false for an
        // empty subject, so the scan never advanced.)
        pcre2_match_data_free(md);
        return 1;
    }
    int offset = 0;   // scan position
    int piece = 0;    // start of the piece being collected
    while (offset <= (int)slen) {
        int rc = pcre2_match(re, (PCRE2_SPTR8)subject + offset, slen - offset,
                             0, 0, md, rexMatchCtx());
        if (rc < 0) {
            // No more matches: push the rest
            lua_pushlstring(L, subject + piece, slen - piece);
            lua_rawseti(L, -2, ++idx);
            break;
        }
        int mstart = (int)ov[0];
        int mend = (int)ov[1];
        if (mend > mstart) {
            // A real separator: the piece ends where the separator starts.
            lua_pushlstring(L, subject + piece, (offset + mstart) - piece);
            lua_rawseti(L, -2, ++idx);
            piece = offset + mend;
            offset = piece;
        } else {
            // Zero-width match: it separates nothing (the byte under the
            // match stays in the current piece), but the scan MUST move on
            // or the loop never ends.
            int mpos = offset + mstart;
            if (mpos >= (int)slen) {
                lua_pushlstring(L, subject + piece, slen - piece);
                lua_rawseti(L, -2, ++idx);
                break;
            }
            offset = mpos + 1;
        }
    }
    pcre2_match_data_free(md);
    return 1;
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
                         0, 0, md, rexMatchCtx());
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
// The body is shared with re:gmatch (see rex_gmatch_body).
static int l_rex_gmatch(lua_State *L) {
    size_t slen = 0;
    const char *subject = luaL_checklstring(L, 1, &slen);
    const char *pattern = luaL_checkstring(L, 2);
    pcre2_code_8 *re = rex_compile(L, pattern);
    if (!re) return lua_error(L);
    int n = rex_gmatch_body(L, re, subject, slen);
    pcre2_code_free(re);
    return n;
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
    int n = rex_gsub_body(L, re, subject, slen,
                           has_repl_fn ? 3 : 0, repl_str, max_count);
    pcre2_code_free(re);
    return n;
}

// rex.split(subject, pattern) -> {piece1, piece2, ...}
static int l_rex_split(lua_State *L) {
    size_t slen = 0;
    const char *subject = luaL_checklstring(L, 1, &slen);
    const char *pattern = luaL_checkstring(L, 2);
    pcre2_code_8 *re = rex_compile(L, pattern);
    if (!re) return lua_error(L);
    int n = rex_split_body(L, re, subject, slen);
    pcre2_code_free(re);
    return n;
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
                         0, 0, md, rexMatchCtx());
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
    return rex_gmatch_body(L, re, subject, slen);
}

// re:gsub(subject, repl, max?) -> result, count
static int l_rex_re_gsub(lua_State *L) {
    pcre2_code_8 *re = *(pcre2_code_8 **)luaL_checkudata(L, 1, "rex_regex");
    size_t slen = 0;
    const char *subject = luaL_checklstring(L, 2, &slen);
    int has_repl_fn = lua_isfunction(L, 3);
    const char *repl_str = has_repl_fn ? nullptr : luaL_checkstring(L, 3);
    int max_count = (int)luaL_optinteger(L, 4, 0);
    return rex_gsub_body(L, re, subject, slen,
                          has_repl_fn ? 3 : 0, repl_str, max_count);
}

// re:split(subject) -> {piece1, piece2, ...}
static int l_rex_re_split(lua_State *L) {
    pcre2_code_8 *re = *(pcre2_code_8 **)luaL_checkudata(L, 1, "rex_regex");
    size_t slen = 0;
    const char *subject = luaL_checklstring(L, 2, &slen);
    return rex_split_body(L, re, subject, slen);
}

LuaEngine::LuaEngine() {
    L = luaL_newstate();
    if (!L) return;

    // Load ALL standard libraries (industry-standard trust model: scripts
    // are user-authored local files, so io/package/os etc. are fully
    // available - same as Lite XL / Loom / SciTE).
    luaL_openlibs(L);

    // Take the `debug` library away: debug.sethook() would remove the
    // runaway hook, and that hook carries BOTH the instruction budget and
    // the wall-clock deadline - a script could disarm both and spin for
    // ever. Everything else stays open.
    disableDebugLibrary(L);

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
        { "input",          l_win_input        },
        { "select",         l_win_select       },
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

    // Register the `pecia` table: one generic closure per HostOps registry
    // entry (registerHostOpsLua / core/HostOps.h). Runs on the UI thread
    // after MainWindow registered its operations.
    registerHostOpsLua();

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
                    const char *langCode,
                    LuaRunResult *result,
                    const LuaRunOptions *opts) {
    if (!L) {
        if (errMsg) *errMsg = "Lua state not initialized";
        return false;
    }
    if (!host.buffer) {
        if (errMsg) *errMsg = "no document buffer";
        return false;
    }
    // m025: a script dispatching back into the engine (pecia.* -> run) would
    // clobber this run's scratch/sink slots - reject the re-entry cleanly.
    // Explicit (non-RAII) depth accounting: +1 here, -1 on both exits below.
    if (g_runDepth.fetch_add(1) != 0) {
        g_runDepth.fetch_sub(1);
        if (errMsg) *errMsg = "nested run: scripts may not invoke the engine recursively";
        return false;
    }
    struct DepthRelease {
        ~DepthRelease() { g_runDepth.fetch_sub(1); }
    } depthRelease;

    // Wall-clock cost of THIS run, in milliseconds. The status line shows
    // it next to the outcome verb, so it is measured HERE and not at the
    // call site: the script bar's call site would also cover the --!param
    // dialog (the human's typing time), and the agent path has no dialog
    // at all - two different numbers for the same script would be a lie.
    // RAII so every exit path (success, Lua error, early return) reports
    // the same measurement.
    struct RunTimer {
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        LuaRunResult *out = nullptr;
        ~RunTimer() {
            if (!out) return;
            out->elapsedMs = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - t0).count();
        }
    } timer;
    timer.out = result;

    // Strip a UTF-8 BOM if present: luaL_loadstring rejects it as an
    // illegal character. Files saved via the Lua console's Save As carry
    // a BOM (so the main editor loads them correctly); the console's
    // in-memory buffer never has one, so this only affects file-backed
    // scripts loaded by the main window.
    std::string src = stripBom(script);

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
    if (opts) ctx.opts = *opts;
    auto clampCap = [](int c) { return c <= 0 ? 32768 : (c > 65536 ? 65536 : c); };
    ctx.outCap = clampCap(opts ? opts->outputCap : 0);
    ctx.resCap = clampCap(opts ? opts->resultCap : 0);

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
        // timeout hook for the duration of this pcall, then remove it. The
        // same hook also enforces the wall-clock deadline (see M1 above):
        // C-function time is invisible to the instruction count, so the
        // deadline is what actually honors the documented timeout_ms cap.
        // Both numbers come from ONE derivation (deriveRunBudget), so the
        // script bar and the MCP path get the same treatment.
        const RunBudget budget = deriveRunBudget(opts);
        g_instructionBudget.store(budget.instructions);
        g_runDeadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds(budget.wallMs);
        lua_sethook(L, &s_instructionHook, LUA_MASKCOUNT, 2048);
        int pc = lua_pcall(L, 0, 1, 0);
        lua_sethook(L, nullptr, 0, 0);   // remove the hook regardless of outcome
        // The budget and the deadline belong to THIS run only: clear them,
        // so a later run cannot inherit an already exceeded deadline.
        // (the parentheses dodge the windows.h min/max macros)
        g_instructionBudget.store(0);
        g_runDeadline = (std::chrono::steady_clock::time_point::max)();
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
                std::string replacement(rstr ? rstr : "", rlen);
                if ((int)replacement.size() > ctx.resCap) {
                    // Cut on a code-point boundary: a half character would be
                    // invalid UTF-8 in the document / the MCP answer.
                    size_t cut = utf8SafeCut(replacement.data(), replacement.size(),
                                             (size_t)ctx.resCap);
                    replacement.resize(cut);
                    if (result) result->resultTruncated = true;
                }
                if (result) result->result = replacement;
                if (!replacement.empty()) rstr = replacement.c_str();
                rlen = replacement.size();
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
    if (result) {
        result->output = sink;
        result->truncated = ctx.outTruncated;
    }

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
        if (result) result->applied = true;
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

// Compile-only check (m025): luaL_loadstring and nothing else. No scratch
// buffer is built, no globals are injected, no pcall happens - a chunk that
// merely LOADS can never touch the document or show UI. lua_pop leaves the
// stack exactly as found.
bool LuaEngine::validate(const std::string &script, std::string *err) {
    if (!L) {
        if (err) *err = "Lua state not initialized";
        return false;
    }
    std::string src = stripBom(script);
    if (luaL_loadstring(L, src.c_str()) != LUA_OK) {
        if (err) {
            const char *s = lua_tostring(L, -1);
            *err = s ? s : "load error";
        }
        lua_pop(L, 1);
        return false;
    }
    lua_pop(L, 1);   // drop the compiled closure without executing it
    return true;
}
