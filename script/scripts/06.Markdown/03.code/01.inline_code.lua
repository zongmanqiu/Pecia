-- ==Meta==
-- @name Inline Code
-- @name.zh-CN 行内代码
-- @date 20260812
-- ==/Meta==

-- 选中文本包裹反引号行内代码（切换：已有反引号包裹则移除）；无选区
-- 时插入成对反引号，光标置于中间。

local sel = editor:get_selection()
if sel == "" then
    editor:replace_selection("``")
    editor:set_cursor(editor:get_cursor() - 1)
    return
end
if sel:sub(1, 1) == "`" and sel:sub(-1) == "`" then
    editor:replace_selection(sel:sub(2, -2))      -- 切换：移除
else
    editor:replace_selection("`" .. sel .. "`")
end
