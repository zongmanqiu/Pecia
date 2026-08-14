// LuaParamParser.h - parse `--!param`/`--!pui` declarations from a Lua
// script's header (first 40 lines). Used to auto-generate the parameter
// dialog before running a script.
#pragma once

#include <string>
#include <vector>

// One declared parameter:
//   --!param name[=default][: description]
// A default containing '|' makes it a dropdown:
//   --!param where=before|after: ...   -> choices {before, after}, default before
struct LuaParam {
    std::string name;         // Lua identifier (also the `params` key)
    std::string defValue;     // "" when no default
    std::string description;  // resolved for the requested language ("" = falls back to name)
    std::vector<std::string> choices;      // non-empty = dropdown (| separated)
    std::vector<std::string> choiceLabels; // display text per choice (empty = use value)
    bool isCheckbox = false;  // choices == {on,off}/{true,false}: render like the find bar's Match Case
};

// Everything the parameter dialog needs, resolved for one language.
struct LuaParamSet {
    std::string title;        // dialog title ("" = caller falls back to I18n)
    std::vector<LuaParam> params;
};

// Parse `--!param` and `--!pui` declarations from the script header and
// resolve all UI texts for `langCode` (e.g. "zh-CN"; "" or "en" = the
// base texts). Lookup chain per text: <key>.<lang> -> <key> (base, en)
// -> raw value (param name / option value). Only lines within the first
// 40 lines are considered (matches kScriptMetaMaxLines); malformed
// entries are skipped.
LuaParamSet luaParseParams(const std::string &script,
                           const std::string &langCode = "en");
