-- ==Meta==
-- @name LaTeX Math
-- @name.zh-CN LaTeX 公式
-- @date 20260813
-- ==/Meta==

-- 选中文本包裹 $$ 行间公式（预览渲染为 PNG 图片）；
-- 无选区时插入成对 $$，光标置于中间。

local sel = editor:get_selection()
if sel == "" then
    editor:replace_selection("$$$$")
    editor:set_cursor(editor:get_cursor() - 2)
    return
end
if sel:sub(1, 2) == "$$" and sel:sub(-2) == "$$" then
    editor:replace_selection(sel:sub(3, -3))      -- 切换：移除
else
    editor:replace_selection("$$\n" .. sel .. "\n$$")
end
