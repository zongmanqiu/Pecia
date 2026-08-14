-- ==Meta==
-- @name Image
-- @name.zh-CN 图片
-- @date 20260812
-- ==/Meta==

-- 选中文本转为图片 ![选中文本]()，光标置于括号内等待输入图片地址；
-- 无选区时插入 ![]()，光标置于方括号内等待输入 alt 文本。

local sel = editor:get_selection()
if sel == "" then
    editor:replace_selection("![]()")
    editor:set_cursor(editor:get_cursor() - 3)   -- 光标在 [] 内
    return
end
editor:replace_selection("![" .. sel .. "]()")
editor:set_cursor(editor:get_cursor() - 1)       -- 光标在 () 内
