-- ==Meta==
-- @name Superscript
-- @name.zh-CN 上标
-- @date 20260813
-- ==/Meta==

-- 选中文本包裹 ^ 上标（切换：已有 ^ 包裹则移除）；无选区时插入成对
-- 标记，光标置于中间。

local sel = editor:get_selection()
if sel == "" then
    editor:replace_selection("^^")
    editor:set_cursor(editor:get_cursor() - 1)
    return
end
if sel:sub(1, 1) == "^" and sel:sub(-1) == "^" then
    editor:replace_selection(sel:sub(2, -2))      -- 切换：移除
else
    editor:replace_selection("^" .. sel .. "^")
end
