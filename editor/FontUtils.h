// FontUtils.h - font enumeration / lookup helpers.
//
// Shared by the Editor (face lookup), Config (tool-window font resolution),
// mdview (CSS candidate fallback) and ui/FontDialog (system font picker).
//
// Owns the one non-obvious rule of this project's font handling:
// Fl::set_fonts() MUST be called with a non-empty pattern (we use "*").
// The no-argument version enumerates ANSI_CHARSET entries only, which hides
// every CJK face; and FLTK only enumerates once per process (first call
// wins), so main.cpp calls set_fonts("*") as early as possible and every
// entry point here passes "*" too.
#pragma once

#include <FL/Fl.H>
#include <string>
#include <vector>

namespace FontUtils {
    // 默认字体名："Consolas"。两处使用 —— ①配置里没有 editor_font 时的种子值；
    // ②配了但系统上找不到该字体时的迁移目标（旧行为是静默回退 Courier，用户
    // 完全无感知：本机实测 editor_font=Gulim 时编辑器一直在按 Courier 渲染）。
    const char *defaultFontName();

    // Map a Windows font name to an FLTK Fl_Font id.
    // Falls back to defaultFontName() (Consolas), then FL_COURIER.
    Fl_Font fontNameToId(const char *name);

    // Same lookup but returns -1 when the face is not installed. Callers that
    // walk a CSS font-family candidate list need to tell "not installed" from
    // a real hit, which the fallback above hides.
    Fl_Font findFontByName(const char *name);

    // Case-insensitive ASCII string compare (portable, no platform deps).
    int strCaseCmp(const char *a, const char *b);

    // 系统已安装的**正体**字体名（已排序、已去重）。
    // 过滤三类噪声：'@' 前缀（垂直书写字体）、attr != 0 的变体 —— FLTK 会给
    // 每个 face 生成最多 4 个条目（regular / bold / italic / bold-italic），
    // 后三者名字带 " bold" / " italic" 后缀，对纯文本编辑器没有意义 —— 以及
    // 字符集本地化重复条目（"Arial Baltic"/"Times New Roman CYR"…，本机 15 条）。
    // 最后这类不只是重复：FLTK 为它们创建的 GDI 字体实际回退到了别的字体，
    // 宽高测量失真，连等宽判定都会被带偏。判据见实现里的 isCharsetTag()。
    std::vector<std::string> systemFontNames();

    // 该字体是否等宽。用 GDI TEXTMETRIC 判定，不用 fl_width("M")==fl_width("i")：
    // FLTK 枚举里存在 "Arial Baltic" / "Times New Roman CYR" 这类 charset
    // 本地化变体条目，FLTK 为它们创建的 GDI 字体实际回退到了别的字体，
    // 宽高测量失真，会被宽度比较法误判成等宽（实测 50 个里大半是噪声）。
    bool isMonospace(const char *name);
}
