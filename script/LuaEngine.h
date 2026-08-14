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
// All standard libraries are loaded (base, package, coroutine, debug, io,
// math, os, string, table, utf8) - the industry-standard trust model for
// user-authored local scripts (same as Lite XL / Loom / SciTE).
class LuaEngine {
public:
    LuaEngine();
    ~LuaEngine();

    LuaEngine(const LuaEngine &) = delete;
    LuaEngine &operator=(const LuaEngine &) = delete;

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
    bool run(const std::string &script,
             LuaEditorHost &host,
             std::string *output = nullptr,
             std::string *errMsg = nullptr,
             const std::vector<std::pair<std::string, std::string>> *paramValues = nullptr,
             const char *langCode = nullptr);

private:
    lua_State *L = nullptr;
};
