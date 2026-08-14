-- ==Meta==
-- @name Split
-- @name.zh-CN 拆分
-- @author 邱宗满
-- @date 20260810
--!param sep=, : separator to split on
--!pui.title = Split by Separator
--!pui.title.zh-CN = 按分隔符拆分
--!pui.sep = Separator
--!pui.sep.zh-CN = 分隔符
-- ==/Meta==

-- Split the selection into multiple lines at every occurrence of the
-- separator (e.g. CSV columns). The separator is treated as plain text,
-- not a pattern, so chars like "." or "*" are safe.

local sel = editor:get_selection()
if #sel == 0 then
    if pecia_lang == "zh-CN" then
        print("请先选择要处理的文本。")
    else
        print("No selection - select the text to process first.")
    end
    return
end

local sep = params.sep or ","
if sep == "" then
    if pecia_lang == "zh-CN" then
        print("请填写分隔符。")
    else
        print("Separator is empty - fill in the split character(s).")
    end
    return
end

-- Escape Lua pattern metacharacters so the separator is literal.
local esc = sep:gsub("([%^%$%(%)%%%.%*%+%-%?%[%]%{%}])", "%%%1")
local result = sel:gsub(esc, "\n")
editor:replace_selection(result)
