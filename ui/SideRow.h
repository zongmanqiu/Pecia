#pragma once

// 侧栏一行（文件树节点或大纲标题）。
//
// 单独成文件（而不是留在 SidePanel.h 里）是为了让大纲的可见行计算
// （ui/OutlineRows.h）能脱离 FLTK 编译：单测只关心这个纯数据结构，不该把
// 整个面板控件和 FLTK 头文件（连带它们的三方警告）拖进来。

#include <string>

struct SideRow {
    std::string label;    // 显示文本
    std::string path;     // 文件/目录完整路径（大纲行为空）
    std::string anchor;   // 大纲锚点 id（文件行为空）
    std::string key;      // 大纲折叠 key（祖先标题链 + 自身文本；文件行为空）
    int  depth = 0;       // 缩进层级（0 = 顶层）
    int  level = 1;       // 标题级别 1..6（文件行恒 1）
    bool isDir = false;   // 目录行（可展开/折叠）
    bool isHeading = false;
    bool expanded = false;
    bool hasChildren = false;   // 目录：有子项；大纲：下面还有更深级别的标题
    bool openable = true;       // 文件行：false = 二进制（灰显、点了不打开）
};
