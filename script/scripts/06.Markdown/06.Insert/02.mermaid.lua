-- ==Meta==
-- @name Mermaid
-- @name.zh-CN Mermaid 图
-- @date 20260813
-- ==/Meta==

-- 插入 mermaid 图表代码块（预览渲染为 PNG 图片）。
-- 无选区时插入 flowchart 模板，光标置于内容处；
-- 有选区时把选区包进 mermaid 围栏。

local sel = editor:get_selection()
if sel == "" then
    editor:replace_selection("```mermaid\nflowchart TD\n    A[Start] --> B[End]\n```")
    editor:set_cursor(editor:get_cursor() - 4)
    return
end
editor:replace_selection("```mermaid\n" .. sel .. "\n```")
