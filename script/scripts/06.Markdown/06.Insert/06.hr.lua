-- ==Meta==
-- @name Horizontal Rule
-- @name.zh-CN 水平分割线
-- @date 20260812
-- ==/Meta==

-- 在光标处插入水平分割线 "---"（有选区时替换选区）。

local sel = editor:get_selection()
if sel == "" then
    editor:replace_selection("---")
    local pos = editor:get_cursor()
    editor:set_cursor(pos)      -- 光标保持在插入内容之后
else
    editor:replace_selection("---")
end
