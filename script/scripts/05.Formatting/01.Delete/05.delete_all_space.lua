-- ==Meta==
-- @name All Spaces
-- @name.zh-CN 所有空格
-- ==/Meta==

-- 删除选中文本中的全部空格类字符（ASCII 空格、制表符、全角空格）。
-- 注意：换行符不会被删除（避免把多行文本合并成一行）。

local sel = editor:get_selection()
if #sel == 0 then
    print("No selection - select the text to process first.")
    return
end
-- 全角空格用字面匹配（放入字符类会按字节匹配，误伤含 0x80 字节的汉字）
local out = sel:gsub("[ \t]", ""):gsub("\u{3000}", "")
editor:replace_selection(out)
print("Removed " .. #sel - #out .. " space characters.")
