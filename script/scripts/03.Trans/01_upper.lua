-- ==Meta==
-- @name To Uppercase
-- @name.zh-CN 转大写
-- @author 邱宗满
-- @date 20260810
-- ==/Meta==

-- Convert the selection to all-uppercase ASCII letters.
-- (Chinese text is unaffected; string.upper only touches ASCII.)

local sel = editor:get_selection()
if #sel == 0 then
    print("No selection - select the text to convert first.")
    return
end
editor:replace_selection(sel:upper())
