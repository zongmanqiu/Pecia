// LuaParamParser.cpp - implementation of --!param / --!pui header parsing.
// The dialog texts live INSIDE the script: --!pui lines provide the
// title, per-parameter labels, option labels and descriptions in a base
// language plus optional <lang> overrides (--!pui.title.zh-CN = ...),
// mirroring the @name / @name.zh-CN convention of the script meta block.
#include "LuaParamParser.h"

#include <cctype>

namespace {

bool isIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool isIdentChar(char c) {
    return isIdentStart(c) || (c >= '0' && c <= '9');
}

bool isLuaIdent(const std::string &s) {
    if (s.empty() || !isIdentStart(s[0])) return false;
    for (size_t i = 1; i < s.size(); ++i)
        if (!isIdentChar(s[i])) return false;
    return true;
}

// Language codes with override support (the set Pecia ships; extend
// when new languages are added).
const char *const kKnownLangs[] = { "en", "zh-CN" };

bool isKnownLang(const std::string &s) {
    for (const char *lang : kKnownLangs)
        if (s == lang) return true;
    return false;
}

// One raw --!pui entry: dotted key path + optional language + text.
struct UiEntry {
    std::vector<std::string> keyPath;  // e.g. {"where","before"} or {"title"}
    std::string lang;                  // "" = base (en)
    std::string text;
};

std::string trim(std::string s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

} // namespace

LuaParamSet luaParseParams(const std::string &script, const std::string &langCode) {
    LuaParamSet out;

    // Pass 1: collect --!param declarations (name, default, choices).
    std::vector<UiEntry> uiTexts;
    {
    size_t lineStart = 0;
    int lineNo = 0;
    // Header area = consecutive comment lines from the top (blank lines
    // allowed); the first real code line ends the scan. The hard cap only
    // guards against pathological files - large declaration sets fit.
    while (lineNo < 256 && lineStart <= script.size()) {
            size_t nl = script.find('\n', lineStart);
            size_t lineEnd = (nl == std::string::npos) ? script.size() : nl;
            std::string line = script.substr(lineStart, lineEnd - lineStart);

            size_t p = 0;
            while (p < line.size() && std::isspace((unsigned char)line[p])) ++p;
            if (p >= line.size() ||
                p + 1 >= line.size() || line[p] != '-' || line[p + 1] != '-') {
                // Blank line: keep scanning. Code line: header is over.
                if (p < line.size()) break;
                ++lineNo;
                lineStart = (nl == std::string::npos) ? script.size() + 1 : nl + 1;
                continue;
            }
            if (p + 1 < line.size() && line[p] == '-' && line[p + 1] == '-') {
                p += 2;
                while (p < line.size() &&
                       (line[p] == ' ' || line[p] == '\t' || line[p] == '!')) ++p;

                // --!pui key[.key2][.lang] = text
                if (line.compare(p, 3, "pui") == 0 &&
                    (p + 3 >= line.size() ||
                     line[p + 3] == ' ' || line[p + 3] == '.')) {
                    p += 3;
                    while (p < line.size() &&
                           (line[p] == ' ' || line[p] == '\t')) ++p;
                    size_t eq = line.find('=', p);
                    if (eq != std::string::npos) {
                        UiEntry e;
                        std::string keys = trim(line.substr(p, eq - p));
                        e.text = trim(line.substr(eq + 1));
                        // Split the dotted key; a final segment that is a
                        // known language code is the language override.
                        size_t s = 0;
                        std::vector<std::string> segs;
                        while (s <= keys.size()) {
                            size_t dot = keys.find('.', s);
                            std::string seg = keys.substr(s, dot == std::string::npos
                                                                ? std::string::npos
                                                                : dot - s);
                            if (!seg.empty()) segs.push_back(seg);
                            if (dot == std::string::npos) break;
                            s = dot + 1;
                        }
                        if (!segs.empty()) {
                            if (segs.size() > 1 && isKnownLang(segs.back())) {
                                e.lang = segs.back();
                                segs.pop_back();
                            }
                            e.keyPath = segs;
                            uiTexts.push_back(std::move(e));
                        }
                    }
                    ++lineNo;
                    lineStart = (nl == std::string::npos) ? script.size() + 1 : nl + 1;
                    continue;
                }

                // --!param name[=default][: description]
                static const char kMarker[] = "param ";
                size_t m = 0;
                while (m < sizeof(kMarker) - 1 && p + m < line.size() &&
                       line[p + m] == kMarker[m]) ++m;
                if (m == sizeof(kMarker) - 1) {
                    p += m;
                    size_t nameStart = p;
                    while (p < line.size() && isIdentChar(line[p])) ++p;
                    std::string name = line.substr(nameStart, p - nameStart);

                    LuaParam prm;
                    if (!isLuaIdent(name)) {
                        ++lineNo;
                        lineStart = (nl == std::string::npos) ? script.size() + 1 : nl + 1;
                        continue;
                    }
                    prm.name = name;

                    if (p < line.size() && line[p] == '=') {
                        ++p;
                        size_t dvStart = p;
                        while (p < line.size() && line[p] != ':') ++p;
                        std::string dv = line.substr(dvStart, p - dvStart);
                        size_t e = dv.size();
                        while (e > 0 && (dv[e - 1] == ' ' || dv[e - 1] == '\t')) --e;
                        dv.resize(e);
                        size_t sep = dv.find('|');
                        if (sep != std::string::npos) {
                            size_t s = 0;
                            while (s <= dv.size()) {
                                size_t nxt = dv.find('|', s);
                                std::string opt = dv.substr(s, nxt == std::string::npos
                                                                  ? std::string::npos
                                                                  : nxt - s);
                                if (!opt.empty()) prm.choices.push_back(opt);
                                if (nxt == std::string::npos) break;
                                s = nxt + 1;
                            }
                            prm.defValue = prm.choices.empty() ? dv : prm.choices[0];
                        } else {
                            prm.defValue = dv;
                        }
                    }

                    if (p < line.size() && line[p] == ':') {
                        ++p;
                        while (p < line.size() &&
                               (line[p] == ' ' || line[p] == '\t')) ++p;
                        prm.description = line.substr(p);
                    }
                    out.params.push_back(std::move(prm));
                }
            }

            ++lineNo;
            lineStart = (nl == std::string::npos) ? script.size() + 1 : nl + 1;
        }
    }

    // Pass 2: resolve UI texts for the requested language.
    auto uiText = [&](const std::vector<std::string> &keyPath, const char *fallback) {
        // exact language first, then base
        for (const char *lang : { langCode.c_str(), "" }) {
            for (const UiEntry &e : uiTexts) {
                if (e.lang == lang && e.keyPath == keyPath)
                    return e.text;
            }
            if (lang[0] == 0) break;   // base pass done
        }
        return std::string(fallback);
    };

    // Dialog title.
    out.title = uiText({ "title" }, "");

    // Per-parameter label/description and option display labels.
    for (LuaParam &prm : out.params) {
        prm.description = uiText({ prm.name }, prm.description.c_str());
        prm.choiceLabels.resize(prm.choices.size());
        for (size_t k = 0; k < prm.choices.size(); ++k) {
            std::vector<std::string> keyPath = { prm.name, prm.choices[k] };
            prm.choiceLabels[k] = uiText(keyPath, prm.choices[k].c_str());
        }
        // A two-option on/off (or true/false) parameter is a boolean
        // switch: rendered as a checkbox identical to the find bar's
        // "Match Case". Checked = first option (on/true).
        if (prm.choices.size() == 2) {
            auto isBool = [](const std::string &v) {
                return v == "on" || v == "off" || v == "true" || v == "false";
            };
            if (isBool(prm.choices[0]) && isBool(prm.choices[1]) &&
                prm.choices[0] != prm.choices[1]) {
                prm.isCheckbox = true;
            }
        }
    }

    return out;
}
