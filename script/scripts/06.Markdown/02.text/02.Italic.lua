-- ==Meta==
-- @name Italic
-- @name.zh-CN 斜体
-- @date 20260812
-- ==/Meta==

-- 选中文本包裹 * 斜体（切换：已有单星包裹则移除；** 加粗文本再执行
-- 本脚本会变为加粗斜体 ***）；无选区时插入成对标记，光标置于中间。

local sel = editor:get_selection()
if sel == "" then
    editor:replace_selection("**")
    editor:set_cursor(editor:get_cursor() - 1)
    return
end
if sel:sub(1, 1) == "*" and sel:sub(2, 2) ~= "*" and
   sel:sub(-1) == "*" and sel:sub(-2, -2) ~= "*" then
    editor:replace_selection(sel:sub(2, -2))      -- 切换：移除
else
    editor:replace_selection("*" .. sel .. "*")
end
