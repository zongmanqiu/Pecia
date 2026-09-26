#pragma once
#include <string>
#include <vector>

/* 文档标题（供预览目录树使用） */
struct PreviewHeading {
    int         level = 0;   // 1-6
    std::string text;        // 纯文本（已解码 HTML 实体、去首尾空白）
    std::string id;          // 锚点 id（toc-N，注入到对应 <hN> 标签）
};

/* 将 Markdown 文本转换为完整 HTML 页面（含 <html><body> 包装和基本 CSS）。
 * build_dir：可执行文件同级的 temp/<文档名>-<PID>/ 预览目录，公式/mermaid
 * 渲染的 PNG（哈希命名缓存）与 index.html 输出至此（ratex/mmdr 为 FFI 进程内渲染）。
 * headings 非空时按文档顺序回填标题列表（供目录树使用）。
 * base_font 非空时为预览基准字体名（编辑器 editor_font），被写在注入 CSS 的
 * 字体候选列表首位——预览正文/代码跟随编辑器字体；后面的候选保留，供导出到
 * 浏览器（index.html）时在没装该字体的机器上兜底。
 * 返回 UTF-8 编码的 HTML 字符串。标题锚点（id="toc-N"）自动注入。 */
std::string md_to_html(const std::string& markdown, const std::string& build_dir = "",
                       const std::string& doc_dir = "",
                       std::vector<PreviewHeading>* headings = nullptr,
                       const std::string& base_font = "");
