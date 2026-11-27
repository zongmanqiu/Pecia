// ToolHost.h - the narrow contract the in-process panes/script UI need from
// the main window (m025). The Lua console window was removed in m037; the AI
// chat pane is now docked in the main window and smoke-tested through this
// same interface (test_ui_tool_smoke constructs it without a main window).
//
// Why an interface instead of MainWindow* directly: the pane can be
// smoke-tested standalone, and M5's UI-measurement gates need a scriptable
// fake. MainWindow implements this verbatim; no adapter layer, no second
// list of the same facts (single-source rule).
//
// All methods run on the UI thread (the panes are FLTK widgets in the same
// event loop as the host).
#pragma once

#include <string>
#include <utility>
#include <vector>

class IToolHost {
public:
    virtual ~IToolHost() = default;

    // Run a script against the active document (one undo step).
    // `paramValues` pre-fills --!param values so every caller - script bar,
    // MCP op, Lua - goes through one execution chain.
    virtual bool executeLuaScript(const std::string &script,
                                  std::string *output,
                                  std::string *errMsg,
                                  const std::vector<std::pair<std::string, std::string>> *paramValues) = 0;

    // Script-bar rescan after a script is saved into script/.
    virtual void reloadScriptBar() = 0;

    // "Send to AI" (file-as-state chat log append + window reveal).
    virtual bool sendToAiChat(const std::string &text, std::string *errMsg) = 0;

    // Document edits the AI chat performs.
    virtual bool insertAiText(const std::string &text, std::string *errMsg) = 0;
    virtual bool getSelectionText(std::string *text, std::string *errMsg) = 0;
    virtual bool clearSelection(std::string *errMsg) = 0;
};
