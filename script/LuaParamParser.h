// LuaParamParser.h - parse `--!param`/`--!pui` declarations from a Lua
// script's header (first 256 lines, see kScriptMetaMaxLines). Used to
// auto-generate the parameter dialog before running a script.
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
// 256 lines are considered (matches kScriptMetaMaxLines); malformed
// entries are skipped.
//
// `knownLangs` is the set of language codes whose `--!pui.<code>` suffix
// is treated as a localization override (e.g. "ja", "de", "zh-TW"). When
// empty, a built-in base set {en, zh-CN} is used so callers/tests stay
// self-contained. The production callers pass I18n::scanLanguages() so
// every shipped language is recognized automatically - adding a language
// then needs NO edit here (the old hard-coded list silently dropped any
// language past en/zh-CN, breaking script-carried translations).
LuaParamSet luaParseParams(const std::string &script,
                           const std::string &langCode = "en",
                           const std::vector<std::string> &knownLangs = {});
