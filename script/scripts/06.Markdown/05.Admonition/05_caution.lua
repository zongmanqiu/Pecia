-- ==Meta==
-- @name Caution
-- @name.zh-CN 危险
-- @date 20260813
-- ==/Meta==

-- 插入 caution 提醒块（红色）。无选区：插入模板，光标置于内容处；
-- 有选区：把选区包进提醒块（每行自动加 > 前缀）。

local sel = editor:get_selection()
if sel == "" then
    editor:replace_selection("> [!CAUTION]\n> ")
    return
end
local body = sel:gsub("\r\n", "\n"):gsub("\n", "\n> ")
editor:replace_selection("> [!CAUTION]\n> " .. body)
