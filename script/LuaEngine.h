// LuaEngine.h - embedded Lua 5.5 runtime for user text-processing scripts
#pragma once

#include <string>
#include <vector>

struct lua_State;
class Fl_Text_Buffer;

// LuaEditorHost
//   Bridge between the engine and the host document. The engine runs the
//   script against a scratch copy of the buffer (so the live document is
//   never touched mid-script), then applies the final result back as a
//   single undo transaction - one Ctrl+Z reverts the whole script
//   (FLTK_PATCHES.md Patch 6: undo_begin/undo_end + replace()).
//   After run() returns, cursorPos/selStart/selEnd/hasSelection hold the
//   script's final state and must be applied to the editor by the caller.
struct LuaEditorHost {
    Fl_Text_Buffer *buffer = nullptr;   // the real document buffer
    int  cursorPos = 0;                 // current insert position (byte offset)
    int  selStart = 0;                  // selection range; selStart == selEnd => none
    int  selEnd = 0;
    bool hasSelection = false;
};

// LuaEngine
//   Embedded Lua runtime for "select text -> run script -> replace"
//   style scripts. The engine exposes these globals to user code:
//
//     editor:get_selection()        -> string  (current selection, "" if none)
//     editor:replace_selection(s)   -> none    (replace selection with s;
//                                              if nothing is selected the
//                                              text is inserted at the cursor)
//     editor:get_cursor()           -> number  (current insert position,
//                                              byte offset)
//     editor:set_cursor(pos)        -> none    (move insert position;
//                                              clamped to buffer bounds)
//     win:message(text)             -> none    (modal OK message box)
//     win:question(text)            -> bool    (modal Yes/No box)
//     win:clipboard_get()           -> string  (clipboard text, UTF-8)
//     win:clipboard_set(text)       -> none    (write clipboard, UTF-8)
//     win:open(path_or_url)         -> none    (open with default handler)
//     print(...)                    -> none    (append to the engine's output
//                                              buffer, tab-separated, with
//                                              trailing newline)
//
// All standard libraries are loaded (base, package, coroutine, io, math, os,
// string, table, utf8) - the industry-standard trust model for
// user-authored local scripts (same as Lite XL / Loom / SciTE).
// The `debug` library is the ONE exception: it is removed on purpose,
// because debug.sethook() could disarm the runaway guard (instruction
// budget + wall-clock deadline) that keeps a script from freezing the UI.
// LuaUiHooks - dependency inversion for the script-facing dialogs (m025).
//
// Layering is `core <- script <- ui`, so LuaEngine must not include ui/.
// The script layer declares WHAT it needs (a themed prompt / a themed
// picker) and the UI layer installs the implementation at startup - the same
// seam pattern as core/UiBridge.h. With no hooks installed (unit tests,
// headless tools) the engine falls back to FLTK's built-in widgets.
struct LuaUiHooks {
    void *ud = nullptr;

    // Prompt: 1 = accepted (out filled), 0 = cancelled.
    int (*input)(void *ud, const char *prompt, const char *dflt,
                 char *out, int outCap) = nullptr;

    // Pick: chosen 0-based index, or -1 on cancel.
    int (*select)(void *ud, const char *prompt,
                  const char *const *items, int n, int pre) = nullptr;
};

// Install (nullptr clears) the process-wide script UI hooks. Called by
// MainWindow once its theme exists.
void luaSetUiHooks(const LuaUiHooks *hooks);

// LuaRunOptions - per-run execution knobs for the MCP-facing paths (m025).
// All defaults reproduce the current menu-run behavior byte-for-byte.
//
// The engine derives ONE budget from these knobs (see deriveRunBudget):
// the wall-clock deadline is the requested timeout (clamped to 5000 ms) and
// the instruction budget is derived from that deadline. maxInstructions may
// only TIGHTEN the derived budget - it can never raise it above the
// engine's own ceiling, so the agent path can never be more permissive
// than the script bar.
struct LuaRunOptions {
    long long maxInstructions = 0;   // 0 = engine default; may only tighten the budget
    int timeoutMs = 0;               // 0 = engine default (5000 ms); hard cap 5000 ms
    int outputCap = 0;               // print sink cap bytes (0 = 32768, clamped <= 65536)
    int resultCap = 0;               // `return "..."` cap bytes (same rule)
    bool headless = false;           // true: zero modal dialogs (MCP default)
    bool answer = false;             // headless answer for win:question
};

// LuaRunResult - structured outcome for MCP ops (m025).
struct LuaRunResult {
    std::string output;              // print() sink (capped)
    std::string result;              // string return value (capped)
    bool truncated = false;          // output hit its cap
    bool resultTruncated = false;    // result hit its cap
    bool applied = false;            // live buffer was modified
    // Wall-clock cost of the run in milliseconds. Measured inside the
    // engine (not by the caller) so the script bar and the agent path
    // report the SAME number for the same script - the status line shows
    // it, and a number that depended on the call site would be a lie.
    long long elapsedMs = 0;
};

class LuaEngine {
public:
    LuaEngine();
    ~LuaEngine();

    LuaEngine(const LuaEngine &) = delete;
    LuaEngine &operator=(const LuaEngine &) = delete;

    // Rebuild the global `pecia` table from the core/HostOps registry.
    // Safe to call repeatedly; ops removed from the registry disappear from
    // the table. Called during engine init after the builtin tables.
    void registerHostOpsLua();

    // Compile-only check (m025): loads the chunk with the same BOM handling
    // as run() but NEVER executes it and never touches any buffer.
    // Returns true when the chunk compiles; `err` carries Lua's own words.
    bool validate(const std::string &script, std::string *err);

    // Run a Lua script against the host document. The script operates on a
    // scratch copy of the buffer; if the result differs from the original,
    // the change is applied to the real buffer as ONE undo action. On
    // failure returns false and fills `errMsg`; on success, any output the
    // script produced via print() is appended to `output` (if non-null).
    // `paramValues` (optional) sets a global `params` table: each pair is
    // (parameter name, value string), so scripts read params.name.
    // `langCode` (optional) sets the global `pecia_lang` to the current
    // UI language code ("en", "zh-CN", ...) so scripts can localize
    // their own messages.
    // `result`/`opts` (m025, optional): structured outcome + execution knobs.
    // Nested runs are rejected ("nested run") - a script dispatching back
    // into run() would corrupt the per-run registry slots.
    bool run(const std::string &script,
             LuaEditorHost &host,
             std::string *output = nullptr,
             std::string *errMsg = nullptr,
             const std::vector<std::pair<std::string, std::string>> *paramValues = nullptr,
             const char *langCode = nullptr,
             LuaRunResult *result = nullptr,
             const LuaRunOptions *opts = nullptr);

private:
    lua_State *L = nullptr;
};
