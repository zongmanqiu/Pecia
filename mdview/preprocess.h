#pragma once
#include <string>
#include <vector>

/* 文档标题（供预览目录树使用） */
struct PreviewHeading {
    int         level = 0;   // 1-6
    std::string text;        // 纯文本（已解码 HTML 实体、去首尾空白）
    std::string id;          // 锚点 id（toc-N，注入到对应 <hN> 标签）
    // 标题在原 Markdown 文本中的行号（1-based）。只有 extractOutlineHeadings
    // 会填（md4c 的 text 回调给出的是指向原文的指针，指针差即可定位）；
    // md_to_html 那条路对其 <hN> 标签一无所知，保持 0 = 未知，由调用方在
    // 需要跳源码时另行提取。0 表示未知，不是第 0 行。
    int         line = 0;
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

/* 轻量标题提取：只跑一遍 md4c 块解析，不产 HTML、不渲染公式/图表（无 FFI，
 * 毫秒级）。用于左侧面板「大纲」标签——预览关闭时大纲依然可用，手动刷新
 * （Markdown > Refresh / Ctrl+R）也走这里。
 *
 * 锚点 id 与 md_to_html 注入 <hN> 的规则完全一致：按文档顺序 toc-N、跳过
 * 空标题。因此同一文档两条路径产出的锚点相同，scrollToAnchor 在预览开/关
 * 两种状态下都落在正确位置。
 *
 * 已知的有意分歧（仅影响标题显示文本，不影响锚点）：标题内含行内公式时，
 * md_to_html 提取到的是占位符字节（它先替换 $$ 再解析，标题提取发生在占位
 * 符还原之前），这里保留 $$ 原文——后者才是给人看的文本。
 *
 * 额外产出每条标题的 `line`（1-based 行号），供侧栏大纲点击时把编辑器光标
 * 送到源码里对应的行。行号是本次提取那一时刻的快照：用户在提取之后增删过
 * 行就会漂移，所以调用方落地前必须校验（见 jumpToHeadingLine 的做法）。 */
void extractOutlineHeadings(const std::string& markdown,
                            std::vector<PreviewHeading>& headings);

/* 源码行校验：第 N 行的内容是不是 label 对应的那条标题？
 *
 * 行号是"提取那一刻"的快照，用户在中间增删行就会漂移（大纲不会每次按键都
 * 重提，成本不值得）。因此落点前必须校一脚，绝不能把光标丢到"看起来位置对、
 * 其实是另一行"的地方。
 *
 * 判定：去掉行首空白与 ATX 井号后再比较。行首不是 '#' 时（setext 标题的正文
 * 行）必须看紧跟着的 nextLine 是不是 `===` / `---` 下划线——只凭"文字相同"
 * 就放行的话，行号漂移后可能落到一条碰巧同名的正文上去，那比跳不过去更糟。
 * 传入的是**整行原文**（可以带结尾 "\r\n"）。nextLine 传不到时留空，此时
 * 非 '#' 行一律判否（宁可判否让调用方重提，也不猜）。
 */
bool isHeadingSourceLine(const std::string &rawLine, const std::string &label,
                         const std::string &nextLine = std::string());
