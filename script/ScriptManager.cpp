// ScriptManager.cpp - implementation of script folder scanning.
// Uses pathutil::exeDir() (core/PathUtils.h) so scripts are found next
// to the executable regardless of the launch working directory.
#include "script/ScriptManager.h"

#include <FL/fl_utf8.h>       // fl_fopen
#include <FL/filename.H>      // FL_PATH_MAX

#include "core/I18n.h"
#include "core/PathUtils.h"

#if defined(_WIN32)
#include <windows.h>
#endif

#include <stdio.h>
#include <string.h>

#include <algorithm>

namespace {

// Localized folder display name from the folder's folder.ini:
//   name = Lines
//   name.zh-CN = 每行
// Same key=value / dotted-language format as the lang files and the
// script meta block. Fallback chain: name.<lang> -> name -> ""
// (caller then shows the raw folder name).
std::string folderDisplayName(const std::string &dir,
                              const std::string &langCode) {
    auto trim = [](std::string s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return std::string();
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    };
    std::string iniPath = dir + "/folder.ini";
    FILE *fp = fl_fopen(iniPath.c_str(), "rb");
    if (!fp) return std::string();
    std::string nameEn, nameLang;
    char buf[512];
    while (fgets(buf, sizeof(buf), fp)) {
        std::string line(buf);
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        if (key == "name") {
            nameEn = val;
        } else if (!langCode.empty() && key == "name." + langCode) {
            nameLang = val;
        }
    }
    fclose(fp);
    return !nameLang.empty() ? nameLang : nameEn;
}


// List *.lua file base names (no extension) in a directory. Returns
// false only if the directory cannot be opened; a directory that exists
// but contains no .lua files yields true with an empty list (the caller
// must NOT treat "no matches" as "missing dir").
bool listLuaFiles(const std::string &dir, std::vector<std::string> &out) {
#if defined(_WIN32)
    std::wstring wdir;
    {
        int len = MultiByteToWideChar(CP_UTF8, 0, dir.c_str(), -1, nullptr, 0);
        if (len <= 1) return false;
        wdir.resize(len - 1);
        MultiByteToWideChar(CP_UTF8, 0, dir.c_str(), -1, &wdir[0], len);
    }
    // Distinguish "directory missing" from "no .lua files": the
    // *.lua search itself returns INVALID_HANDLE_VALUE for both, so
    // check the bare directory first.
    DWORD attr = GetFileAttributesW(wdir.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
        return false;

    std::wstring pattern = wdir + L"\\*.lua";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return true;   // dir ok, no matches
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring wname(fd.cFileName);
        // Convert the WHOLE name to UTF-8 first (MultiByteToWideChar /
        // WideCharToMultiByte counts are byte-vs-wchar - mixing them truncates
        // multi-byte (e.g. Chinese) script names to garbage), then strip the
        // trailing ".lua".
        std::string name;
        int len = WideCharToMultiByte(CP_UTF8, 0, wname.c_str(), -1,
                                      nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            name.resize((size_t)len - 1);   // len includes the NUL terminator
            WideCharToMultiByte(CP_UTF8, 0, wname.c_str(), -1,
                                &name[0], len, nullptr, nullptr);
        }
        const char kLua[] = ".lua";
        size_t slen = strlen(kLua);
        if (name.size() > slen &&
            name.compare(name.size() - slen, slen, kLua) == 0)
            name.resize(name.size() - slen);
        if (!name.empty() &&
            std::find(out.begin(), out.end(), name) == out.end())
            out.push_back(name);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return true;
#else
    (void)dir; (void)out;   // POSIX path not used on this project
    return false;
#endif
}

// List level-1 subdirectories of a directory (name only).
bool listSubDirs(const std::string &dir, std::vector<std::string> &out) {
#if defined(_WIN32)
    std::wstring wdir;
    {
        int len = MultiByteToWideChar(CP_UTF8, 0, dir.c_str(), -1, nullptr, 0);
        if (len <= 1) return false;
        wdir.resize(len - 1);
        MultiByteToWideChar(CP_UTF8, 0, dir.c_str(), -1, &wdir[0], len);
    }
    std::wstring pattern = wdir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        std::wstring wname(fd.cFileName);
        if (wname == L"." || wname == L"..") continue;
        std::string name;
        int len = WideCharToMultiByte(CP_UTF8, 0, wname.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len > 1) {
            name.resize(len - 1);
            WideCharToMultiByte(CP_UTF8, 0, wname.c_str(), -1, &name[0], len, nullptr, nullptr);
        }
        out.push_back(name);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return true;
#else
    (void)dir; (void)out;
    return false;
#endif
}

} // namespace

ScriptCatalog scriptManagerScan() {
    std::string dir = pathutil::exeDir();
    std::string root = dir.empty() ? "script" : dir + "/script";

    // Try <exe dir>/script/ first, then CWD-relative "script/".
    ScriptCatalog cat;
    for (int ci = 0; ci < 2; ++ci) {
        std::string folder = (ci == 0) ? root : std::string("script");
        ScriptCatalog candidate = scriptManagerScanDir(folder);
        if (!candidate.rootPath.empty()) {
            cat = std::move(candidate);
            break;
        }
    }
    return cat;
}

// Scan one folder into a group: its direct .lua files plus its own
// level-2 subfolders (one more level, then stop).
ScriptGroup scanGroup(const std::string &name, const std::string &path) {
    ScriptGroup g;
    g.name = name;
    g.displayName = folderDisplayName(path, I18n::currentCode());
    listLuaFiles(path, g.scripts);

    std::vector<std::string> subs;
    listSubDirs(path, subs);
    for (const auto &sub : subs) {
        std::string subPath = path + "/" + sub;
        ScriptGroup subG;
        subG.name = sub;
        subG.displayName = folderDisplayName(subPath, I18n::currentCode());
        listLuaFiles(subPath, subG.scripts);
        if (!subG.scripts.empty())
            g.subGroups.push_back(std::move(subG));
    }
    return g;
}

ScriptCatalog scriptManagerScanDir(const std::string &rootDir) {
    ScriptCatalog catalog;
    if (rootDir.empty()) return catalog;

    std::vector<std::string> rootScripts;
    if (!listLuaFiles(rootDir, rootScripts)) return catalog;
    catalog.rootPath = rootDir;
    catalog.rootScripts = rootScripts;

    // Scan one level of subfolders into groups, with level-2 subfolders
    // as nested subGroups.
    std::vector<std::string> subs;
    listSubDirs(rootDir, subs);
    for (const auto &sub : subs) {
        std::string subPath = rootDir + "/" + sub;
        ScriptGroup g = scanGroup(sub, subPath);
        if (!g.scripts.empty() || !g.subGroups.empty())
            catalog.groups.push_back(std::move(g));
    }
    return catalog;
}

std::string scriptManagerPath(const std::string &relPath) {
    if (relPath.empty()) return std::string();
    ScriptCatalog cat = scriptManagerScan();
    if (cat.rootPath.empty()) return std::string();
    return scriptManagerPathIn(cat.rootPath, relPath);
}

std::string scriptManagerPathIn(const std::string &rootDir,
                                const std::string &relPath) {
    if (rootDir.empty() || relPath.empty()) return std::string();

    // relPath may be "name" (root) or "Group/name" (subfolder).
    std::string full = rootDir + "/" + relPath + ".lua";
    FILE *fp = fl_fopen(full.c_str(), "rb");
    if (fp) {
        fclose(fp);
        return full;
    }
    return std::string();
}

std::string scriptManagerNameFallback(const std::string &relPath) {
    // Display name = basename (last path segment): "Formatting/trim" ->
    // "trim". Handles both '/' and '\\' separators.
    size_t p = relPath.find_last_of("/\\");
    if (p == std::string::npos) return relPath;
    return relPath.substr(p + 1);
}

std::string scriptManagerDisplayName(const std::string &relPath) {
    // 1) The script's own meta block wins: current language, then "en".
    ScriptMeta meta = scriptLoadMeta(relPath);
    if (meta.valid) {
        auto it = meta.names.find(I18n::currentCode());
        if (it != meta.names.end()) return it->second;
        it = meta.names.find("en");
        if (it != meta.names.end()) return it->second;
    }
    // 2) No (usable) meta block: fall back to the file basename.
    return scriptManagerNameFallback(relPath);
}

// ---------------------------------------------------------------------------
// Script meta block (Tampermonkey-style Lua header comments)
// ---------------------------------------------------------------------------

namespace {

std::string metaTrim(const std::string &s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    return s.substr(b, e - b);
}

bool metaStartsWith(const std::string &s, const char *p) {
    return s.compare(0, strlen(p), p) == 0;
}

} // namespace

ScriptMeta scriptParseMeta(const std::string &headerText) {
    ScriptMeta meta;
    bool inBlock = false;
    size_t pos = 0;
    while (pos < headerText.size()) {
        size_t nl = headerText.find('\n', pos);
        std::string line = nl == std::string::npos
                               ? headerText.substr(pos)
                               : headerText.substr(pos, nl - pos);
        pos = nl == std::string::npos ? headerText.size() : nl + 1;

        std::string t = metaTrim(line);
        // Only Lua line comments count; anything else inside the block
        // terminates it (unclosed -> invalid).
        if (!metaStartsWith(t, "--")) {
            if (inBlock) return ScriptMeta();   // broken block
            continue;
        }
        t = metaTrim(t.substr(2));   // strip the "--" comment marker

        if (t == "==Meta==") { inBlock = true; continue; }
        if (t == "==/Meta==") {
            if (inBlock) { meta.valid = true; break; }
            continue;   // closing marker without an open block: ignore
        }
        if (!inBlock || t.empty() || t[0] != '@') continue;

        // "@key" or "@key.lang" followed by whitespace and the value.
        size_t sp = t.find_first_of(" \t");
        std::string key = t.substr(1, sp == std::string::npos ? std::string::npos : sp - 1);
        std::string value = sp == std::string::npos ? "" : metaTrim(t.substr(sp + 1));
        if (key.empty()) continue;

        std::string lang = "en";
        size_t dot = key.find('.');
        if (dot != std::string::npos) {
            lang = key.substr(dot + 1);
            key = key.substr(0, dot);
        }

        if (key == "name") {
            meta.names[lang] = value;
        }
        // Every other key (@author, @date, @description, ...) is a
        // plain comment for humans: ignored, not stored.
    }
    return meta;
}

ScriptMeta scriptLoadMeta(const std::string &relPath) {
    std::string full = scriptManagerPath(relPath);
    if (full.empty()) return ScriptMeta();
    FILE *fp = fl_fopen(full.c_str(), "rb");
    if (!fp) return ScriptMeta();
    // Read the header comment area: consecutive comment lines from the
    // top of the file (blank lines allowed between them). The scan stops
    // at the first real Lua code line or at the hard cap - so scripts
    // with many parameter/translation declarations still parse fine.
    std::string header;
    char buf[512];
    for (int i = 0; i < kScriptMetaMaxLines && fgets(buf, sizeof(buf), fp); ++i) {
        header += buf;
        std::string t = metaTrim(buf);
        if (!t.empty() && !metaStartsWith(t, "--"))
            break;   // first code line ends the header area
    }
    fclose(fp);
    return scriptParseMeta(header);
}
