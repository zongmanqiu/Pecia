-- ==Meta==
-- @name （*）
-- @author 邱宗满
-- @date 20260809
-- ==/Meta==

local left, right = "（", "）"
local sel = editor:get_selection()
if #sel > 0 then
    editor:replace_selection(left .. sel .. right)
else
    local c = editor:get_cursor()
    editor:replace_selection(left .. right)
    editor:set_cursor(c + #left)
end