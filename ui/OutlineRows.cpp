#include "OutlineRows.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>

namespace outline_rows {

namespace {

// 标题级别（1..6）。越界/非法值按 H1 —— 与改动前的 rebuildRows 一致。
int levelAt(const std::vector<int> &levels, size_t i) {
    if (i < levels.size() && levels[i] > 0) return levels[i];
    return 1;
}

// 缩进层级的上限 = Markdown 的最深标题 H6 → depth 5（H1 是 0）。
//
// ★ 这里**不能**用 4（= H5）封顶：H5 和 H6 会同时被夹到 4，两级的文字左缘
//   完全重合，看起来"六级标题比五级标题更靠左/左对齐"（2026-10-31 用户实测）。
//   封顶只该挡住非法级别，不该挡住合法的 H6。
constexpr int kOutlineMaxDepth = 5;

}  // namespace

std::vector<SideRow> build(const std::vector<std::string> &labels,
                           const std::vector<int> &levels,
                           const std::vector<std::string> &ids,
                           const std::set<std::string> &collapsed)
{
    std::vector<SideRow> rows;
    if (labels.empty()) return rows;

    // 祖先栈：(level, key)。key = 父级 key + kKeySep + 本标题文本 + 同名序号。
    // 折叠一行 = 隐藏它下面所有 level 更深的标题，直到出现 level <= 它的为止。
    //
    // ★ 这里**不能**用「按 level 单调栈」那种写法：大纲 level 可以跳级
    //   （H1 直接跟 H3），单调栈在跳级处会把 H3 判成 H1 的孙子而不是儿子，
    //   隐藏范围就错了。
    std::map<std::string, int> seen;   // 基础 key（祖先链+自身文本）→ 已出现次数
    std::vector<std::pair<int, std::string>> stack;
    for (size_t i = 0; i < labels.size(); ++i) {
        SideRow r;
        r.label = labels[i];
        r.isHeading = true;
        r.level = levelAt(levels, i);
        r.depth = std::min(r.level - 1, kOutlineMaxDepth);
        r.anchor = (i < ids.size()) ? ids[i] : std::string();

        // 弹出所有不再是我祖先的层级（祖先 level 必须严格小于我）
        while (!stack.empty() && stack.back().first >= r.level)
            stack.pop_back();

        // 有子标题 = 紧邻的下一条标题级别更深（可以跳级，所以判「更深」
        // 而不是「恰好 +1」）。
        r.hasChildren = (i + 1 < labels.size()) &&
                        (levelAt(levels, i + 1) > r.level);
        // 自身文本 + 同名序号两段都不能省，见 OutlineRows.h
        const std::string baseKey =
            (stack.empty() ? std::string() : stack.back().second + kKeySep) + r.label;
        r.key = baseKey + kIndexSep + std::to_string(seen[baseKey]++);
        r.expanded = r.hasChildren && collapsed.count(r.key) == 0;

        // 祖先里有被折叠的 → 本行不显示（但被折叠的那一行自己要留着，
        // 否则箭头没了就再也展开不回来）。
        bool hidden = false;
        for (const auto &e : stack)
            if (collapsed.count(e.second)) { hidden = true; break; }

        if (!hidden) rows.push_back(r);
        stack.push_back({ r.level, r.key });
    }
    return rows;
}

}  // namespace outline_rows
