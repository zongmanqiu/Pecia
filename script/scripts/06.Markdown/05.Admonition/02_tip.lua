-- ==Meta==
-- @name Tip
-- @name.zh-CN 提示
-- @date 20260813
-- ==/Meta==

-- 插入 tip 提醒块（绿色）。无选区：插入模板，光标置于内容处；
-- 有选区：把选区包进提醒块（每行自动加 > 前缀）。

local sel = editor:get_selection()
if sel == "" then
    editor:replace_selection("> [!TIP]\n> ")
    return
end
local body = sel:gsub("\r\n", "\n"):gsub("\n", "\n> ")
editor:replace_selection("> [!TIP]\n> " .. body)
