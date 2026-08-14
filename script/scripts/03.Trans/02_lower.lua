-- ==Meta==
-- @name To Lowercase
-- @name.zh-CN 转小写
-- @author 邱宗满
-- @date 20260810
-- ==/Meta==

-- Convert the selection to all-lowercase ASCII letters.
-- (Chinese text is unaffected; string.lower only touches ASCII.)

local sel = editor:get_selection()
if #sel == 0 then
    print("No selection - select the text to convert first.")
    return
end
editor:replace_selection(sel:lower())
