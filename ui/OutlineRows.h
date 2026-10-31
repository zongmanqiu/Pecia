#pragma once

// 大纲折叠的可见行计算（纯函数）。
//
// 单独成文件只为一个目的：能脱离窗口、FLTK、I18n 直接单测
// （test_outline_collapse）。大纲折叠最容易犯的错是"折一处全折光"——
// 真窗口里肉眼试一次看不出，只有把可见行集合断言死才抓得住。

#include <set>
#include <string>
#include <vector>

#include "ui/SideRow.h"   // SideRow（纯数据，不带 FLTK 依赖）

namespace outline_rows {

// ★ 折叠 key = 从根到该标题的各级标题文本（kKeySep 连接）+ 同名序号：
//
//   "A\x01B\x01C\x02 1"   ← 最后一节是"同一父节点下第几个同名标题"
//
// 两处都必须有：
//   · 缺"自身文本"：同一父节点下的所有兄弟 key 相同 → 折一个，整层全折；
//   · 缺"同名序号"：文档里同名标题（ex1.md 就有两个 H1「一级标题」）key
//     相同 → 折第一个，第二个也折 —— 而第二个往往挂着整篇内容，
//     表现就是"折一处，所有大纲都没了"（2026-10-31 用户实测）。
//
// 为什么不用锚点 id（toc-N）：按文档顺序编号，上面插一个标题就整体位移，
// 折叠状态会错位到别的节上；行号同理。文本链在文档被编辑后依然指向同一节。
// 唯一代价：在两个同名标题之间插入第三个同名标题，序号会顺移（可接受）。
constexpr char kKeySep   = '\x01';   // 祖先链分隔
constexpr char kIndexSep = '\x02';   // 同名序号分隔

// labels 为空时返回空表：合成"无标题"行要查 I18n，留给调用方（纯函数
// 不碰全局状态，测试才能离线跑）。
std::vector<SideRow> build(const std::vector<std::string> &labels,
                           const std::vector<int> &levels,
                           const std::vector<std::string> &ids,
                           const std::set<std::string> &collapsed);

}  // namespace outline_rows
