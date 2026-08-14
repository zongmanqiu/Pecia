// ScriptManager.h - scan and run Lua scripts from the exe-adjacent
// script/ folder, mirroring how I18n loads lang/ files. Supports TWO
// directory levels below the root:
//
//   build/Pecia.exe
//   build/script/
//       pair.lua            -> root script (Misc button item)
//       Formatting/
//           trim.lua        -> group "Formatting" (one dropdown button)
//           sort.lua
//       No/
//           number_dot.lua  -> group "No"
//           AAA/
//               A.lua       -> submenu "AAA" inside "No" (level 2)
//
// Deeper nesting (level 3+) is ignored.
#pragma once

#include <map>
#include <string>
#include <vector>

// One script group = one subfolder of script/. Level-1 groups appear as
// toolbar buttons / menu entries; level-2 groups appear as nested
// submenus inside their level-1 parent.
// displayName is resolved from the folder's folder.ini (name / name.<lang>,
// same dotted-language convention as the script meta block); empty =
// show the raw folder name.
struct ScriptGroup {
    std::string name;                    // subfolder name (also the key)
    std::string displayName;             // localized ("" = use name)
    std::vector<std::string> scripts;    // .lua base names (no extension)
    std::vector<ScriptGroup> subGroups;  // level-2 subfolders
};

// Full scan result of the script/ folder.
struct ScriptCatalog {
    std::string rootPath;               // absolute script/ path ("" if none)
    std::vector<std::string> rootScripts; // .lua in script/ root
    std::vector<ScriptGroup> groups;    // level-1 subfolders (with level-2 submenus)
};

// Scan <exe dir>/script/ (fallback: script/ relative to CWD) for *.lua
// files at the root and in two levels of subfolders. Mirrors I18n::exeDir()
// resolution. Returns an empty catalog if no script/ folder exists.
ScriptCatalog scriptManagerScan();

// Scan a specific script root directory (used by unit tests; production
// code calls scriptManagerScan() which resolves the exe-adjacent path).
// Non-existent directories yield an empty catalog.
ScriptCatalog scriptManagerScanDir(const std::string &rootDir);

// Full path of a script by slash-separated relative path, e.g.
// "pair" or "Formatting/trim" (no extension). Returns "" if not found.
std::string scriptManagerPath(const std::string &relPath);

// Like scriptManagerPath() but rooted at an explicit directory (tests).
std::string scriptManagerPathIn(const std::string &rootDir,
                                const std::string &relPath);

// Untranslated display name for a script: its basename (last path
// segment), e.g. "Formatting/trim" -> "trim".
std::string scriptManagerNameFallback(const std::string &relPath);

// Final display name shown in menus/toolbar: the script's own meta
// block (@name for the current language, else @name en), falling back
// to the file basename when the script has no (usable) meta block.
// Scripts carry their own translations - no built-in i18n keys.
std::string scriptManagerDisplayName(const std::string &relPath);

// Script metadata parsed from the script's Tampermonkey-style header
// block (Lua comments):
//
//   -- ==Meta==
//   -- @name My Script
//   -- @name.zh-CN 我的脚本
//   -- @author Zhang San   (comment only - not read by Pecia)
//   -- @date 20260809      (comment only - not read by Pecia)
//   -- ==/Meta==
//
// Pecia only reads the @name family (@name for the base language,
// @name.<lang> for overrides) - that is the only thing the UI shows
// (script bar button text, shortcut dialog rows). Every other @key is
// treated as a plain comment and ignored, so authors can add whatever
// human-readable fields they like (author, date, ...).
//
// The block must start within the first kScriptMetaMaxLines lines (same
// window as the --!param declarations). "en" is the base language.
// A block that is not closed (or that hits a non-comment line before
// closing) is treated as absent.
constexpr int kScriptMetaMaxLines = 256;

struct ScriptMeta {
    bool valid = false;    // block present AND closed
    std::map<std::string, std::string> names;   // lang -> name ("en" = base)
};

// Parse a meta block from raw header text (the first lines of a script).
// Only @name / @name.<lang> entries are extracted; all other keys are
// ignored (they are comments for humans).
ScriptMeta scriptParseMeta(const std::string &headerText);

// Load the first kScriptMetaMaxLines of a script file and parse its meta
// block. relPath is slash-separated, without extension ("pair",
// "Formatting/trim"). Returns valid=false when the script is missing or
// has no (closed) meta block.
ScriptMeta scriptLoadMeta(const std::string &relPath);

// Backward-compatible single-level list (root scripts only), for callers
// that just want the flat list.
std::vector<std::string> scriptManagerList();
