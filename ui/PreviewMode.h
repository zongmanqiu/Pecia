// PreviewMode.h - "有没有东西可预览"的判定（纯函数，供单测直接覆盖）。
//
// 为什么单独抽出来：空文档不允许进入"仅预览"。2026-10-31 用户指出的场景是
// "开着仅预览 → 新建文件"，编辑区被隐藏、预览面板一片空白，用户无处输入、
// 点什么都没反应，第一反应是窗口卡死。所以"空"必须是有明确定义的判据，而不是
// 散落在几处的 `text.empty()` —— 后者漏掉"只有几个换行/空格"的情况，屏幕上
// 同样是白纸。
#pragma once

#include <cstddef>

namespace preview_mode {

// 扫描上限：超过这个长度直接判为"非空"。
// 调用点跑在自动刷新的每个 tick 上，不能对十几 MB 的文档全文扫描；而真正可能
// 为空的文档一定很短（新建的、清空过的）。一个"8192 字节全空白"的病态文件会被
// 误判为非空 —— 代价只是回到旧行为（空面板），可以接受。
constexpr std::size_t kBlankScanCap = 8192;

// 文本是否在视觉上为空：去掉 UTF-8 BOM 后全是空格/制表/换行等无字形的 ASCII
// 空白。多字节字符（含全角空格 U+3000）一律视为有内容 —— 它们渲染出来至少占位，
// 判成空反而会让用户在"明明写了个空格"的文档上被强制退出仅预览。
inline bool isBlank(const char *text, std::size_t len) {
    if (!text || len == 0) return true;
    // UTF-8 BOM 不是内容：只带 BOM 的文件（某些编辑器保存空文件时的产物）在
    // 屏幕上同样是白纸，不该因此获得"可预览"资格。
    if (len >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text += 3;
        len -= 3;
        if (len == 0) return true;
    }
    if (len > kBlankScanCap) return false;
    for (std::size_t i = 0; i < len; ++i) {
        const char c = text[i];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '\f' && c != '\v')
            return false;
    }
    return true;
}

} // namespace preview_mode
