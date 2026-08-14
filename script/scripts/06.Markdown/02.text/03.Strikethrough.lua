-- ==Meta==
-- @name Strikethrough
-- @name.zh-CN 删除线
-- @date 20260812
-- ==/Meta==

-- 选中文本包裹 ~~ 删除线（切换：已有 ~~ 包裹则移除）；无选区时插入
-- 成对标记，光标置于中间。

local sel = editor:get_selection()
if sel == "" then
    editor:replace_selection("~~~~")
    editor:set_cursor(editor:get_cursor() - 2)
    return
end
if sel:sub(1, 2) == "~~" and sel:sub(-2) == "~~" then
    editor:replace_selection(sel:sub(3, -3))      -- 切换：移除
else
    editor:replace_selection("~~" .. sel .. "~~")
end
