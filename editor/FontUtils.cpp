// FontUtils.cpp - implementation of editor/FontUtils.h.
//
// Lives in its own TU (not Editor.cpp) so the font picker dialog
// (ui/FontDialog.cpp, also linked into test_ui_smoke) can use it without
// dragging the whole editor widget in.
#include "FontUtils.h"

#include <FL/fl_draw.H>

#include <algorithm>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace FontUtils {

int strCaseCmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return ca - cb;
        ++a; ++b;
    }
    return *a - *b;
}

Fl_Font findFontByName(const char *name) {
    if (!name || !name[0]) return -1;
    // "*" = 枚举全部字符集（无参版本会漏掉所有非拉丁字体）。首次调用决定
    // 收录范围，所以每个入口都传，避免某个进程没走 main.cpp 就退化。
    int num = Fl::set_fonts("*");
    for (int i = 0; i < num; ++i) {
        int attr = 0;
        const char *fn = Fl::get_font_name(i, &attr);
        if (fn && strCaseCmp(fn, name) == 0) {
            return i;
        }
    }
    return -1;
}

const char *defaultFontName() {
    return "Consolas";
}

Fl_Font fontNameToId(const char *name) {
    Fl_Font id = findFontByName(name);
    if (id >= 0) return id;
    if (!name || !name[0]) return FL_COURIER;
    // Common fallback mapping
    if (strCaseCmp(name, "Courier New") == 0 || strCaseCmp(name, "Courier") == 0)
        return FL_COURIER;
    if (strCaseCmp(name, "Helvetica") == 0)
        return FL_HELVETICA;
    if (strCaseCmp(name, "Times") == 0 || strCaseCmp(name, "Times New Roman") == 0)
        return FL_TIMES;
    // 名字在系统里找不到：回退到默认字体而不是 Courier。Courier 是个"能用但
    // 没人会主动选"的字体，静默落到它上面等于把配置错误藏起来了。
    Fl_Font def = findFontByName(defaultFontName());
    return def >= 0 ? def : FL_COURIER;
}

namespace {

// GDI 会为每个字符集各枚举一条本地化条目 —— 本机实测有 15 条这类噪声：
// "Arial Baltic/CE/CYR/Greek/TUR"、"Courier New Baltic/CE/CYR/Greek/TUR"、
// "Times New Roman CYR" …。它们与基础名是同一套字形，出现在字体列表里纯属
// 重复；更糟的是 FLTK 给这些条目创建的 GDI 字体实际回退到了别的字体，测出的
// 宽高失准，连等宽判定都会被带偏（实测 50 个"等宽字体"里大半是它们）。
//
// 判据刻意收得很紧：名字必须 = 已收录的基础名 + 一个空格 + **单个**字符集标签。
// 因为 "Light" / "Semibold" / "Condensed" / "Emoji" / "Symbol" / "Variable Text"
// 这些后缀是真实独立的 family（Bahnschrift Light、Segoe UI Emoji、
// Segoe UI Variable Text…），一个都不能误杀 —— 所以只认标签名单、且只认单词后缀。
bool isCharsetTag(const char *tag) {
    static const char *const kTags[] = {
        "Baltic", "CE", "CYR", "Greek", "TUR", "HEB", "ARAB", "Arabic",
        "Thai", "Viet", "Vietnamese", "Cyrillic", "Turkish", "Hebrew",
        "Russian", "Western", "EastEurope", "CentralEuropean",
        "Johab", "Mac", "ANSI", "OEM", "DEFAULT"
    };
    for (const char *t : kTags) {
        if (strCaseCmp(tag, t) == 0) return true;
    }
    return false;
}

// name 是否是已收录条目的字符集变体（只与 accepted 里已有的名字比对）。
bool isCharsetVariant(const std::string &name,
                      const std::vector<std::string> &accepted) {
    for (const auto &base : accepted) {
        if (name.size() <= base.size() + 1) continue;
        if (name.compare(0, base.size(), base) != 0) continue;
        if (name[base.size()] != ' ') continue;
        const char *tail = name.c_str() + base.size() + 1;
        if (std::strchr(tail, ' ') != nullptr) continue;   // 多个词 = 真实 family
        if (isCharsetTag(tail)) return true;
    }
    return false;
}

} // namespace

std::vector<std::string> systemFontNames() {
    std::vector<std::string> out;
    // "*" 表示枚举全部字符集。若 main.cpp 已经调用过，这里只是取回缓存的
    // 条目数（FLTK 内部只在首次调用时枚举一次）。
    int num = Fl::set_fonts("*");
    for (int i = 0; i < num; ++i) {
        int attr = 0;
        const char *fn = Fl::get_font_name((Fl_Font)i, &attr);
        if (!fn || !fn[0]) continue;
        if (fn[0] == '@') continue;   // 垂直书写（竖排）字体
        if (attr != 0) continue;      // bold / italic / bold-italic 变体
        out.push_back(fn);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());

    // 去掉字符集本地化重复条目（"Arial Baltic" 这类），只保留基础名。
    std::vector<std::string> filtered;
    filtered.reserve(out.size());
    for (const auto &n : out) {
        if (isCharsetVariant(n, filtered)) continue;
        filtered.push_back(n);
    }
    return filtered;
}

bool isMonospace(const char *name) {
    if (!name || !name[0]) return false;
#if defined(_WIN32)
    // 字体名在 FLTK 里是 UTF-8，GDI 要宽字符。
    int wlen = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
    if (wlen <= 0) return false;
    std::vector<WCHAR> wname((size_t)wlen);
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wname.data(), wlen);
    HDC hdc = GetDC(nullptr);
    if (!hdc) return false;
    HFONT hf = CreateFontW(16, 0, 0, 0, FW_REGULAR, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, wname.data());
    HFONT old = (HFONT)SelectObject(hdc, hf);
    TEXTMETRICW tm;
    memset(&tm, 0, sizeof(tm));
    bool got = (GetTextMetricsW(hdc, &tm) != 0);
    SelectObject(hdc, old);
    DeleteObject(hf);
    ReleaseDC(nullptr, hdc);
    // TMPF_FIXED_PITCH 的语义是"可变间距"（微软命名反直觉）：该位为 0
    // 才是等宽字体。
    return got && (tm.tmPitchAndFamily & TMPF_FIXED_PITCH) == 0;
#else
    Fl_Font fid = findFontByName(name);
    if (fid < 0) return false;
    fl_font(fid, 16);
    return fl_width("M") == fl_width("i") && fl_width("W") == fl_width(".");
#endif
}

} // namespace FontUtils
